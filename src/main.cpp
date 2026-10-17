#include <M5Unified.h>
#include <Avatar.h>
#include <usb/usb_host.h>
#include <vector>
#include <algorithm>
#include "rf_response.h"
#include "balance_display.h"
#include "speech.h"
#include "suica_history.h"
#include "stations.h"
#include "station_toggle.h"

// USBクライアントの操作はArduinoのloopタスクに集約する。
// usb-daemonはホストライブラリ全体のイベント処理、Avatarは画面描画を担当する。
// このclientの接続通知・転送完了コールバックは、clientイベントを処理した側で呼ばれる。
static usb_host_client_handle_t client;
static usb_device_handle_t device;
// 接続通知ではアドレスを保存し、実際のopenはloopへ持ち越す。
// epIn/epOutはディスクリプタから取得するBulk転送用のアドレス。
static uint8_t pendingAddress, epIn, epOut, interfaceNumber;
static uint16_t inPacketSize;
// 切断通知済み／インターフェース取得済み／RF初期化済みを区別する。
static bool disconnected, claimed, ready;
static bool secondBeepPending;
static uint32_t secondBeepAt;
// The Avatar draw task owns all display writes after initialization.
static SemaphoreHandle_t faceStatusMutex;
static char faceStatus[128] = "RC-S380を接続してください";
static int faceBalance = -1;
static volatile uint32_t uiFrames;
static volatile int uiRenderedBalance = -1;

// Diagnostic capture of the actual LCD framebuffer, downsampled to 320x180.
static void captureScreen() {
    const int w = M5.Display.width(), h = M5.Display.height();
    std::vector<lgfx::rgb888_t> row(w);
    std::vector<uint8_t> output((w / 4) * 3);
    Serial.printf("SCREENSHOT\nP6\n%d %d\n255\n", w / 4, h / 4);
    for (int y = 0; y < h; y += 4) {
        M5.Display.readRect(0, y, w, 1, row.data());
        for (int x = 0; x < w / 4; ++x) {
            output[x * 3] = row[x * 4].R8();
            output[x * 3 + 1] = row[x * 4].G8();
            output[x * 3 + 2] = row[x * 4].B8();
        }
        Serial.write(output.data(), output.size());
    }
}

namespace ui {
constexpr uint32_t background = 0x030711U;
constexpr uint32_t panel = 0x091525U;
constexpr uint32_t border = 0x17445aU;
constexpr uint32_t mint = 0x35f4ffU;
constexpr uint32_t white = 0xe5faffU;
constexpr uint32_t muted = 0x83a9c3U;
constexpr uint32_t magenta = 0xff42bfU;
constexpr uint32_t grid = 0x0b2436U;
constexpr uint32_t amber = 0xffb24dU;
}

// Draw the interface into Avatar's sprite so all display writes share one task.
class StatusMouth : public m5avatar::Drawable {
    m5avatar::Mouth mouth{70, 110, 7, 80};
public:
    void draw(M5Canvas* canvas, m5avatar::BoundingRect rect,
              m5avatar::DrawContext* context) override {
        char status[sizeof(faceStatus)];
        int balance;
        xSemaphoreTake(faceStatusMutex, portMAX_DELAY);
        memcpy(status, faceStatus, sizeof(status));
        balance = faceBalance;
        xSemaphoreGive(faceStatusMutex);
        const bool error = strstr(status, "エラー") || strstr(status, "失敗");
        const bool connected = !strstr(status, "RC-S380");
        const uint32_t accent = error ? ui::amber : ui::mint;
        // Subtle technical grid and a moving scan line behind the live face.
        for (int x = 60; x <= 572; x += 32) canvas->drawFastVLine(x, 162, 420, ui::grid);
        for (int y = 162; y <= 582; y += 32) canvas->drawFastHLine(60, y, 512, ui::grid);
        canvas->drawCircle(316, 328, 185, ui::border);
        canvas->drawCircle(316, 328, 191, ui::grid);
        const int scanY = 172 + (millis() / 22) % 400;
        canvas->drawFastHLine(76, scanY, 480, ui::border);
        // Bracketed face viewport, with small ticks rather than a solid box.
        for (int side : {0, 1}) {
            int x = side ? 572 : 60;
            for (int bottom : {0, 1}) {
                int y = bottom ? 582 : 162;
                canvas->fillRect(side ? x - 30 : x, y, 30, 3, ui::mint);
                canvas->fillRect(x, bottom ? y - 26 : y, 3, 26, ui::mint);
            }
        }
        mouth.draw(canvas, rect, context);
        canvas->setTextSize(1);
        canvas->setTextDatum(m5gfx::textdatum_t::top_left);
        canvas->setFont(&fonts::FreeSansBold24pt7b);
        canvas->setTextColor(ui::white, ui::background);
        canvas->fillRect(48, 49, 5, 38, ui::magenta);
        canvas->fillRect(58, 49, 5, 24, ui::mint);
        canvas->setTextColor(ui::magenta, ui::background);
        canvas->drawString("STACK//PASS", 79, 45);
        canvas->setTextColor(ui::white);
        canvas->drawString("STACK//PASS", 76, 43);
        canvas->setFont(&fonts::FreeSans12pt7b);
        canvas->setTextDatum(m5gfx::textdatum_t::top_right);
        canvas->setTextColor(ui::muted, ui::background);
        canvas->drawString("CONTACTLESS / SUICA", 1228, 60);
        canvas->drawFastHLine(48, 115, 1184, ui::border);
        canvas->drawFastHLine(48, 115, 180, ui::mint);
        canvas->drawFastHLine(1100, 115, 132, ui::magenta);

        // Face stays on the left, with a quiet caption underneath.
        canvas->setTextDatum(m5gfx::textdatum_t::top_center);
        canvas->setFont(&fonts::lgfxJapanGothic_24);
        canvas->setTextColor(ui::muted, ui::background);
        canvas->drawString(balance >= 0 ? "読み取れたよ！" : "タッチして、アクセス。", 316, 524);

        // A fixed panel keeps the layout stable between waiting and success.
        canvas->fillRect(610, 166, 622, 414, ui::panel);
        canvas->drawRect(610, 166, 622, 414, ui::border);
        // Clipped corners and bright short rails give the panel a HUD silhouette.
        canvas->fillTriangle(610, 166, 634, 166, 610, 190, ui::background);
        canvas->drawLine(610, 190, 634, 166, accent);
        canvas->fillTriangle(1231, 579, 1207, 579, 1231, 555, ui::background);
        canvas->drawLine(1207, 579, 1231, 555, ui::magenta);
        canvas->fillRect(634, 166, 130, 3, accent);
        canvas->fillRect(1230, 190, 3, 72, ui::magenta);
        canvas->drawFastHLine(652, 246, 538, ui::border);
        for (int x = 1110; x < 1190; x += 10) canvas->fillRect(x, 204, 4, 12, ui::border);
        canvas->fillCircle(654, 206, 5, accent);
        canvas->setTextDatum(m5gfx::textdatum_t::top_left);
        canvas->setFont(&fonts::FreeSans12pt7b);
        canvas->setTextColor(accent, ui::panel);
        canvas->drawString(balance >= 0 ? "BALANCE" : error ? "PLEASE RETRY" : connected ? "READY TO SCAN" : "CONNECT READER", 674, 193);
        canvas->setFont(&fonts::lgfxJapanGothic_40);
        canvas->setTextColor(balance >= 0 ? ui::mint : ui::white, ui::panel);
        if (balance >= 0) {
            char amount[32];
            if (balance >= 1000) snprintf(amount, sizeof(amount), "%d,%03d", balance / 1000, balance % 1000);
            else snprintf(amount, sizeof(amount), "%d", balance);
            canvas->setTextSize(2);
            canvas->drawString(amount, 652, 282);
            canvas->setTextSize(1);
            canvas->setFont(&fonts::lgfxJapanGothic_24);
            canvas->setTextColor(ui::muted, ui::panel);
            canvas->drawString("円 / JPY", 656, 389);
            canvas->drawFastHLine(652, 450, 538, ui::border);
            canvas->setTextColor(ui::mint, ui::panel);
            canvas->drawString("読み取り完了", 656, 486);
        } else {
            canvas->drawString(error ? "もう一度お試しください" : connected ? "カードをかざす" : "リーダーを接続", 652, 290);
            canvas->setFont(&fonts::lgfxJapanGothic_24);
            canvas->setTextColor(ui::muted, ui::panel);
            canvas->drawString(error ? "カードやUSBの接続を確認してください" : connected ? "Suicaをリーダーの上に置いてください" : "RC-S380をUSBにつないでください", 656, 375);
        }
        canvas->drawFastHLine(48, 633, 1184, ui::border);
        canvas->fillCircle(60, 674, 5, connected ? accent : ui::muted);
        canvas->setTextDatum(m5gfx::textdatum_t::top_left);
        canvas->setFont(&fonts::lgfxJapanGothic_24);
        canvas->setTextColor(ui::muted, ui::background);
        canvas->drawString(status, 80, 658);
        const bool stationOn = stationSpeechEnabled();
        const uint32_t toggleColor = stationOn ? ui::mint : ui::muted;
        canvas->fillRoundRect(stationToggle::x, stationToggle::y,
            stationToggle::width, stationToggle::height, 10, ui::panel);
        canvas->drawRoundRect(stationToggle::x, stationToggle::y,
            stationToggle::width, stationToggle::height, 10, toggleColor);
        canvas->setFont(&fonts::lgfxJapanGothic_24);
        canvas->setTextColor(toggleColor, ui::panel);
        canvas->drawString("駅名読み上げ", 874, 664);
        canvas->setFont(&fonts::FreeSansBold12pt7b);
        canvas->drawString(stationOn ? "ON" : "OFF", 1100, 664);
        canvas->fillCircle(1198, 676, 7, toggleColor);
        uiRenderedBalance = balance;
        ++uiFrames;
    }
};

// Native Avatar components retain blinking, gaze and expression animations.
static m5avatar::Avatar avatar(new m5avatar::Face(
    new StatusMouth(), new m5avatar::BoundingRect(382, 316),
    new m5avatar::Eye(24, false), new m5avatar::BoundingRect(295, 206),
    new m5avatar::Eye(24, true), new m5avatar::BoundingRect(295, 426),
    new m5avatar::Eyeblow(64, 0, false), new m5avatar::BoundingRect(244, 206),
    new m5avatar::Eyeblow(64, 0, true), new m5avatar::BoundingRect(244, 426)));
static const uint8_t ack[] = {0, 0, 0xff, 0, 0xff, 0};

static void playReadConfirmation() {
    // A short double beep inspired by a ticket gate, rather than an audio sample.
    if (!M5.Speaker.tone(2500, 65)) {
        Serial.println("Speaker: failed to play read confirmation");
        return;
    }
    secondBeepAt = millis() + 95;
    secondBeepPending = true;
}

static void updateReadConfirmation() {
    if (secondBeepPending && int32_t(millis() - secondBeepAt) >= 0) {
        secondBeepPending = false;
        M5.Speaker.tone(2500, 65);
    }
}

static void show(const char* status, int balance = -1) {
    xSemaphoreTake(faceStatusMutex, portMAX_DELAY);
    snprintf(faceStatus, sizeof(faceStatus), "%s", status);
    faceBalance = balance;
    xSemaphoreGive(faceStatusMutex);
    const bool error = strstr(status, "エラー") || strstr(status, "失敗");
    avatar.setExpression(balance >= 0 ? m5avatar::Expression::Happy :
                         error ? m5avatar::Expression::Sad : m5avatar::Expression::Neutral);
    Serial.println(status);
}

// 通知内では状態だけ記録する。通信やcloseはloop側で行う。
// bulk()待機中にも呼ばれるので、転送中のデバイスをここで閉じないこと。
static void usbEvent(const usb_host_client_event_msg_t* event, void*) {
    if (event->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        pendingAddress = event->new_dev.address;
    } else if (event->event == USB_HOST_CLIENT_EVENT_DEV_GONE &&
               event->dev_gone.dev_hdl == device) {
        disconnected = true;
    }
}

// contextはbulk()内のdoneを指す。bulk()は通知までreturnしないため寿命が保たれる。
static void transferDone(usb_transfer_t* transfer) {
    *static_cast<bool*>(transfer->context) = true;
}

// 非同期Bulk転送を、この関数では完了まで待って同期的に扱う。
// 待機中もclientイベントを処理しないと、完了通知も切断通知も届かない。
// タイムアウト時も即座にfreeせず、完了通知までDMAバッファを保持する。
static bool bulk(uint8_t endpoint, const uint8_t* output, size_t count,
                 std::vector<uint8_t>& input, uint32_t timeout = 1000) {
    if (!device || disconnected) return false;
    const bool reading = endpoint & 0x80; // bit7=1はIN（リーダー→TAB5）
    // INの要求サイズは最大パケットサイズの倍数にする。OUTは実データ長を使う。
    size_t capacity = reading ? ((512 + inPacketSize - 1) / inPacketSize) * inPacketSize : count;
    // ホストAPIで転送オブジェクトとDMA用バッファを確保する。
    usb_transfer_t* transfer = nullptr;
    if (usb_host_transfer_alloc(capacity, 0, &transfer) != ESP_OK) return false;
    bool done = false;
    transfer->device_handle = device;
    transfer->bEndpointAddress = endpoint;
    transfer->callback = transferDone;
    transfer->context = &done;
    transfer->num_bytes = capacity;
    if (!reading) memcpy(transfer->data_buffer, output, count);
    // submit成功は受付完了であり、送受信の完了ではない。
    esp_err_t error = usb_host_transfer_submit(transfer);
    if (error != ESP_OK) {
        Serial.printf("USB submit ep=%02x: %s\n", endpoint, esp_err_to_name(error));
        usb_host_transfer_free(transfer); return false;
    }
    uint32_t start = millis();
    bool cancelled = false;
    while (!done) {
        usb_host_client_handle_events(client, pdMS_TO_TICKS(10));
        if (!done && !cancelled && (disconnected || millis() - start > timeout)) {
            // タイムアウト／抜去時は転送を止め、保留転送を返却させる。
            // flush後もdoneを待ち、ホストが参照中のバッファを解放しない。
            usb_host_endpoint_halt(device, endpoint);
            usb_host_endpoint_flush(device, endpoint);
            cancelled = true;
        }
    }
    bool ok = !cancelled && transfer->status == USB_TRANSFER_STATUS_COMPLETED;
    if (!ok && timeout >= 1000) {
        Serial.printf("USB transfer ep=%02x status=%d cancelled=%d bytes=%d\n",
                      endpoint, int(transfer->status), cancelled, transfer->actual_num_bytes);
    }
    if (ok && reading) input.assign(transfer->data_buffer,
                                   transfer->data_buffer + transfer->actual_num_bytes);
    if (ok && !reading) ok = transfer->actual_num_bytes == static_cast<int>(count);
    // 受信データをコピーしてから解放。未切断なら停止した通信路も復帰させる。
    usb_host_transfer_free(transfer);
    if (cancelled && !disconnected) usb_host_endpoint_clear(device, endpoint);
    return ok;
}

static bool send(const uint8_t* data, size_t length) {
    std::vector<uint8_t> unused;
    return bulk(epOut, data, length, unused);
}

// USB Bulk上のPort-100コマンドを組み立て、ACKと応答フレームを検証する。
// USBの転送境界とPort-100のフレーム境界は一致しない。
static bool command(uint8_t code, const std::vector<uint8_t>& args,
                    std::vector<uint8_t>& response) {
    // 拡張フレーム: 00 00 FF FF FF / LEN(L,H) / LCS / D6 / CODE / 引数 / DCS / 00。
    // LENはD6とCODEも含む。LCS・DCSは対象範囲の和が8bitで0になるチェック値。
    uint16_t length = args.size() + 2;
    std::vector<uint8_t> frame = {0, 0, 0xff, 0xff, 0xff,
        uint8_t(length), uint8_t(length >> 8),
        uint8_t(-uint8_t(length) - uint8_t(length >> 8)), 0xd6, code};
    frame.insert(frame.end(), args.begin(), args.end());
    uint8_t sum = 0;
    for (size_t i = 8; i < frame.size(); ++i) sum += frame[i];
    frame.push_back(-sum);
    frame.push_back(0);
    if (!send(frame.data(), frame.size())) return false;
    std::vector<uint8_t> received, packet;
    bool gotAck = false;
    uint32_t start = millis();
    while (millis() - start < 1500) {
        // ACKと応答が一度に届く場合も、複数回に分かれる場合もある。
        // 連結した受信データから先頭のACKを除き、続く応答全体を待つ。
        if (!bulk(epIn, nullptr, 0, packet)) return false;
        received.insert(received.end(), packet.begin(), packet.end());
        if (!gotAck && received.size() >= sizeof(ack)) {
            if (memcmp(received.data(), ack, sizeof(ack))) return false;
            received.erase(received.begin(), received.begin() + sizeof(ack));
            gotAck = true;
        }
        if (!gotAck || received.size() < 8) continue;
        // LENとLCSを先に検証し、フレーム全体が届くまで解析を進めない。
        const uint8_t prefix[] = {0, 0, 0xff, 0xff, 0xff};
        if (memcmp(received.data(), prefix, 5) ||
            uint8_t(received[5] + received[6] + received[7]) != 0) return false;
        size_t n = received[5] | (received[6] << 8);
        if (n < 2 || n > 512) return false;
        if (received.size() < n + 10) continue;
        sum = 0;
        for (size_t i = 8; i <= n + 8; ++i) sum += received[i];
        // 応答方向はD7、応答コードは要求CODE+1。DCSと末尾00も確認する。
        if (sum || received[n + 9] || received[8] != 0xd7 || received[9] != code + 1) return false;
        // Port-100の外枠を取り除き、コマンド固有の応答だけを返す。
        response.assign(received.begin() + 10, received.begin() + 8 + n);
        return true;
    }
    return false;
}

// 初期化コマンド用。成功ステータス00が1バイトだけ返ることも確認する。
static bool statusCommand(uint8_t code, const std::vector<uint8_t>& args) {
    std::vector<uint8_t> response;
    return command(code, args, response) && response.size() == 1 && response[0] == 0;
}

static bool initializeReader() {
    if (!send(ack, sizeof(ack))) return false;
    // ACK送信で以前のコマンドを中断し、残った応答を最大4回読み捨てる。
    // ここでの短い受信タイムアウトは、読取失敗ではなく掃除の終了条件。
    std::vector<uint8_t> discarded;
    for (int i = 0; i < 4 && bulk(epIn, nullptr, 0, discarded, 30); ++i) {}
    if (!statusCommand(0x2a, {1}) || !statusCommand(0x06, {0})) return false;
    if (!statusCommand(0x00, {1, 1, 15, 1})) return false; // NFC-F, 212 kbit/s
    // 設定番号と値の組で、CRC・フレーム形式・ガード時間・RF衝突回避を設定する。
    if (!statusCommand(0x02, {
        0x00,0x18, 0x01,1, 0x02,1, 0x03,0, 0x04,0, 0x05,0,
        0x06,0, 0x07,8, 0x08,0, 0x09,0, 0x0a,0, 0x0b,0,
        0x0c,0, 0x0e,4, 0x0f,0, 0x10,0, 0x11,0, 0x12,0, 0x13,6})) return false;
    return true;
}

// InCommRF（04）にFeliCaフレームを載せる。
// USB転送失敗とRFタイムアウト（カードなし）を別の結果として扱う。
static ReadResult exchangeFelica(const std::vector<uint8_t>& felica, std::vector<uint8_t>& result) {
    std::vector<uint8_t> args = {0xee, 0x02}; // 75 ms in 100 us units
    args.insert(args.end(), felica.begin(), felica.end());
    std::vector<uint8_t> response;
    if (!command(0x04, args, response)) {
        Serial.println("Port-100 InCommRF command failed");
        return ReadResult::TransportError;
    }
    auto classification = classifyRfResponse(response.data(), response.size());
    if (classification != ReadResult::Balance) {
        if (classification != ReadResult::NoCard) {
            Serial.printf("InCommRF response size=%u status=", unsigned(response.size()));
            for (size_t i = 0; i < std::min(size_t(4), response.size()); ++i) Serial.printf("%02x", response[i]);
            Serial.println();
        }
        return classification;
    }
    // RFステータス4バイトと受信ビット情報1バイトを除き、FeliCa応答を返す。
    result.assign(response.begin() + 5, response.end());
    return ReadResult::Balance;
}

static ReadResult readBalance(int& balance, const Station*& station, bool readStation) {
    station = nullptr;
    std::vector<uint8_t> response;
    auto result = exchangeFelica({6, 0, 0, 3, 0, 0}, response); // Polling system 0003
    if (result != ReadResult::Balance) return result;
    if (response.size() != 18 || response[1] != 1) return ReadResult::Unsupported;
    uint8_t idm[8];
    memcpy(idm, response.data() + 2, 8);
    std::vector<uint8_t> request = {16, 6}; // Read Without Encryption
    request.insert(request.end(), idm, idm + 8);
    request.insert(request.end(), {1, 0x0f, 0x09, 1, 0x80, 0}); // History 090F, block 0
    result = exchangeFelica(request, response);
    if (result != ReadResult::Balance) return result;
    if (response.size() < 12 || response[1] != 7 ||
        memcmp(response.data() + 2, idm, 8) || response[10] || response[11]) return ReadResult::Unsupported;
    if (response.size() != 29 || response[12] != 1) return ReadResult::Unsupported;
    // Latest history record: balance is little endian at offsets 10 and 11.
    balance = response[23] | (response[24] << 8);
    if (readStation) {
        // Search newest first, up to the 20 publicly readable history blocks.
        // Additional history failure must not invalidate the successful balance.
        for (int block = 0; block < 20; ++block) {
            if (block) {
                request.back() = uint8_t(block);
                if (exchangeFelica(request, response) != ReadResult::Balance ||
                    response.size() != 29 || response[1] != 7 ||
                    memcmp(response.data() + 2, idm, 8) || response[10] || response[11] || response[12] != 1) break;
            }
            const auto* record = response.data() + 13;
            if (!(record[4] || record[5])) break; // unused history
            uint32_t key;
            if (exitStationKey(record, 16, key)) {
                station = findStation(key);
                Serial.printf("Latest rail exit: block=%d code=%06lx station=%s\n", block,
                    (unsigned long)key, station ? station->name : "unknown");
                break; // Never substitute an older known station for an unknown latest one.
            }
        }
    }
    return ReadResult::Balance;
}

// bulk()の完了を待った後にloopから呼ぶ。claim済みのインターフェースを
// 返却してからデバイスを閉じ、次の接続を受けられる状態へ戻す。
static void closeReader() {
    ready = false;
    if (device) {
        if (claimed) usb_host_interface_release(client, device, interfaceNumber);
        usb_host_device_close(client, device);
    }
    claimed = false;
    device = nullptr;
    disconnected = false;
}

// 通知されたアドレスを開き、機種判定→Bulk通信路の取得→RF初期化の順に進む。
// USB接続成功だけではreadyにせず、リーダーの初期化成功まで待つ。
static void openReader(uint8_t address) {
    if (device || usb_host_device_open(client, address, &device) != ESP_OK) return;
    const usb_device_desc_t* desc;
    if (usb_host_get_device_descriptor(device, &desc) != ESP_OK) { closeReader(); return; }
    Serial.printf("USB VID:PID %04x:%04x\n", desc->idVendor, desc->idProduct);
    if (desc->idVendor != 0x054c || (desc->idProduct != 0x06c1 && desc->idProduct != 0x06c3)) {
        show("RC-S380を接続してください"); closeReader(); return;
    }
    const usb_config_desc_t* config;
    if (usb_host_get_active_config_descriptor(device, &config) != ESP_OK) { closeReader(); return; }
    // 同一インターフェース／代替設定のBulk INとOUTの組を探す。
    // 固定のエンドポイント番号を仮定せず、長さ付きディスクリプタを順に走査する。
    epIn = epOut = 0;
    uint8_t alt = 0;
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(config);
    for (size_t pos = config->bLength; pos + 2 <= config->wTotalLength;) {
        uint8_t len = bytes[pos], type = bytes[pos + 1];
        // 不正な長さで無限ループや設定領域外アクセスを起こさない。
        if (len < 2 || pos + len > config->wTotalLength) break;
        if (type == USB_B_DESCRIPTOR_TYPE_INTERFACE && len >= 9) {
            // 直前のインターフェースでIN/OUTが揃っていれば、その組を採用する。
            if (epIn && epOut) break;
            interfaceNumber = bytes[pos + 2]; alt = bytes[pos + 3]; epIn = epOut = 0;
        } else if (type == USB_B_DESCRIPTOR_TYPE_ENDPOINT && len >= 7 && (bytes[pos + 3] & 3) == 2) {
            // bmAttributes下位2bitが2ならBulk。INのwMaxPacketSizeも取得する。
            uint8_t ep = bytes[pos + 2];
            if (ep & 0x80) { epIn = ep; inPacketSize = (bytes[pos + 4] | (bytes[pos + 5] << 8)) & 0x7ff; }
            else epOut = ep;
        }
        pos += len;
    }
    // 通信路が揃い、インターフェースをclaimできた場合だけ初期化へ進む。
    if (!epIn || !epOut || !inPacketSize ||
        usb_host_interface_claim(client, device, interfaceNumber, alt) != ESP_OK) {
        show("USB接続エラー"); closeReader(); return;
    }
    claimed = true;
    ready = initializeReader();
    show(ready ? "Suicaをかざしてください" : "初期化失敗：USBを接続し直す");
}

void setup() {
    auto config = M5.config();
    config.internal_spk = true;
    M5.begin(config);
    Serial.begin(115200);
    M5.Speaker.setVolume(64); // 0..255; start at a moderate volume.
    M5.Display.setRotation(1);
    M5.Display.fillScreen(ui::background);
    faceStatusMutex = xSemaphoreCreateMutex();
    configASSERT(faceStatusMutex);
    avatar.getFace()->getBoundingRect()->setPosition(0, 0);
    avatar.getFace()->getBoundingRect()->setSize(M5.Display.width(), M5.Display.height());
    auto palette = avatar.getColorPalette();
    palette.set(COLOR_PRIMARY, M5.Display.color565(53, 244, 255));
    palette.set(COLOR_BACKGROUND, M5.Display.color565(3, 7, 17));
    avatar.setColorPalette(palette);
    avatar.setSpeechFont(&fonts::lgfxJapanGothic_24);
    avatar.init(8);
    // USB-Aホスト端子へ給電する。USB-Cの書き込み用接続とは別の端子。
    M5.Power.setExtOutput(true, m5::ext_USB);
    show("RC-S380を接続してください");
    usb_host_config_t host = {};
    host.intr_flags = ESP_INTR_FLAG_LEVEL1;
    ESP_ERROR_CHECK(usb_host_install(&host));
    // 列挙・接続管理などライブラリ全体のイベントは専用タスクで処理する。
    // アプリのコールバックを動かすclientイベント処理とは役割が異なる。
    xTaskCreate([](void*) {
        while (true) { uint32_t flags; usb_host_lib_handle_events(portMAX_DELAY, &flags); }
    }, "usb-daemon", 4096, nullptr, 5, nullptr);
    // 非同期クライアントを登録。接続・切断通知はusbEventで受ける。
    usb_host_client_config_t cc = {};
    cc.max_num_event_msg = 8;
    cc.async.client_event_callback = usbEvent;
    ESP_ERROR_CHECK(usb_host_client_register(&cc, &client));
    if (!beginSpeech()) Serial.println("TTS task creation failed");
}

void loop() {
    M5.update();
    for (int i = 0; i < M5.Touch.getCount(); ++i) {
        const auto touch = M5.Touch.getDetail(i);
        if (touch.wasPressed() && stationToggle::contains(touch.x, touch.y)) {
            const bool enabled = !stationSpeechEnabled();
            setStationSpeechEnabled(enabled);
            Serial.printf("Station speech mode: %s\n", enabled ? "ON" : "OFF");
            break; // A multi-touch press still toggles only once per update.
        }
    }
    if (Serial.available()) {
        const int diagnostic = Serial.read();
        if (diagnostic == 's') captureScreen();
        if (diagnostic == 't') { playReadConfirmation(); speakBalance(12010, findStation(0x000101)); }
        if (diagnostic == 'v') { playReadConfirmation(); speakBalance(12010); }
        if (diagnostic == 'd') Serial.printf("UI frames=%lu balance=%d\n", (unsigned long)uiFrames, int(uiRenderedBalance));
    }
    updateReadConfirmation();
    // 通信待ちでない間も接続／切断通知を受け取る。
    // 先に切断を片付け、その後で新しいデバイスを開く。
    usb_host_client_handle_events(client, pdMS_TO_TICKS(10));
    if (disconnected) { closeReader(); show("RC-S380を接続してください"); }
    if (pendingAddress) { uint8_t address = pendingAddress; pendingAddress = 0; openReader(address); }
    static uint32_t lastRead;
    static int lastBalance = -1;
    static ReadResult lastResult = ReadResult::NoCard;
    static ReadResult lastRawResult = ReadResult::NoCard;
    static BalanceDisplay display;
    if (!ready) {
        lastBalance = -1;
        lastResult = lastRawResult = ReadResult::NoCard;
        display.reset();
        return;
    }
    if (millis() - lastRead < 500) return;
    lastRead = millis();
    int balance = -1;
    const Station* station = nullptr;
    const auto rawResult = readBalance(balance, station, stationSpeechEnabled() && lastRawResult != ReadResult::Balance);
    // 読取中にUSBが抜けた場合、結果を画面・音声へ渡さない。
    if (disconnected) return;
    const bool confirmRead = rawResult == ReadResult::Balance && lastRawResult != ReadResult::Balance;
    lastRawResult = rawResult;
    const auto result = display.update(rawResult, balance, millis());
    if (result != lastResult || balance != lastBalance) {
        switch (result) {
        case ReadResult::Balance:
            show("残高", balance);
            Serial.printf("Balance: %d JPY\n", balance);
            break;
        case ReadResult::NoCard: show("Suicaをかざしてください"); break;
        case ReadResult::Unsupported: show("読取失敗：カードを置き直す"); break;
        case ReadResult::TransportError: show("通信エラー：USBを接続し直す"); ready = false; break;
        }
        lastBalance = balance;
        lastResult = result;
    }
    // Sound follows actual card reads, independently of the held display state.
    if (confirmRead) {
        playReadConfirmation();
        speakBalance(balance, station);
    }
}
