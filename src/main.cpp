#include <M5Unified.h>
#include <Avatar.h>
#include <usb/usb_host.h>
#include <vector>
#include <algorithm>
#include "rf_response.h"

// USB client calls belong to the Arduino loop task; Avatar owns display updates.
static usb_host_client_handle_t client;
static usb_device_handle_t device;
static uint8_t pendingAddress, epIn, epOut, interfaceNumber;
static uint16_t inPacketSize;
static bool disconnected, claimed, ready;
static bool secondBeepPending;
static uint32_t secondBeepAt;
// The Avatar draw task owns all display writes after initialization.
static SemaphoreHandle_t faceStatusMutex;
static char faceStatus[128] = "RC-S380を接続してください";
static int faceBalance = -1;

// Add the status/footer to the library's sprite, keeping all drawing in one task.
class StatusMouth : public m5avatar::Drawable {
    m5avatar::Mouth mouth{100, 150, 8, 100};
public:
    void draw(M5Canvas* canvas, m5avatar::BoundingRect rect,
              m5avatar::DrawContext* context) override {
        mouth.draw(canvas, rect, context);
        char status[sizeof(faceStatus)];
        int balance;
        xSemaphoreTake(faceStatusMutex, portMAX_DELAY);
        memcpy(status, faceStatus, sizeof(status));
        balance = faceBalance;
        xSemaphoreGive(faceStatusMutex);
        canvas->setFont(&fonts::lgfxJapanGothic_40);
        canvas->setTextDatum(m5gfx::textdatum_t::top_center);
        // Text colors are RGB values, not monochrome palette indices.
        // RGB 0x000001 converts to black in the 1-bit sprite.
        canvas->setTextColor(TFT_WHITE, TFT_BLACK);
        canvas->setTextSize(1);
        canvas->drawString(status, canvas->width() / 2, 354);
        if (balance >= 0) {
            char text[32];
            snprintf(text, sizeof(text), "%d 円", balance);
            canvas->setTextSize(2);
            canvas->drawString(text, canvas->width() / 2, 468);
            canvas->setTextSize(1);
        }
    }
};

// Eyes, mouth, expressions, blinking and gaze are rendered by M5Stack-Avatar.
static m5avatar::Avatar avatar(new m5avatar::Face(
    new StatusMouth(), new m5avatar::BoundingRect(235, 640),
    new m5avatar::Eye(28, false), new m5avatar::BoundingRect(130, 400),
    new m5avatar::Eye(28, true), new m5avatar::BoundingRect(130, 880),
    new m5avatar::Eyeblow(90, 0, false), new m5avatar::BoundingRect(75, 400),
    new m5avatar::Eyeblow(90, 0, true), new m5avatar::BoundingRect(75, 880)));
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

static void usbEvent(const usb_host_client_event_msg_t* event, void*) {
    if (event->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        pendingAddress = event->new_dev.address;
    } else if (event->event == USB_HOST_CLIENT_EVENT_DEV_GONE &&
               event->dev_gone.dev_hdl == device) {
        disconnected = true;
    }
}

static void transferDone(usb_transfer_t* transfer) {
    *static_cast<bool*>(transfer->context) = true;
}

// Pump asynchronous client events while waiting. Never free an in-flight DMA buffer.
static bool bulk(uint8_t endpoint, const uint8_t* output, size_t count,
                 std::vector<uint8_t>& input, uint32_t timeout = 1000) {
    if (!device || disconnected) return false;
    const bool reading = endpoint & 0x80;
    size_t capacity = reading ? ((512 + inPacketSize - 1) / inPacketSize) * inPacketSize : count;
    usb_transfer_t* transfer = nullptr;
    if (usb_host_transfer_alloc(capacity, 0, &transfer) != ESP_OK) return false;
    bool done = false;
    transfer->device_handle = device;
    transfer->bEndpointAddress = endpoint;
    transfer->callback = transferDone;
    transfer->context = &done;
    transfer->num_bytes = capacity;
    if (!reading) memcpy(transfer->data_buffer, output, count);
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
    usb_host_transfer_free(transfer);
    if (cancelled && !disconnected) usb_host_endpoint_clear(device, endpoint);
    return ok;
}

static bool send(const uint8_t* data, size_t length) {
    std::vector<uint8_t> unused;
    return bulk(epOut, data, length, unused);
}

static bool command(uint8_t code, const std::vector<uint8_t>& args,
                    std::vector<uint8_t>& response) {
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
        // ACK and response may be coalesced, or split between USB transfers.
        if (!bulk(epIn, nullptr, 0, packet)) return false;
        received.insert(received.end(), packet.begin(), packet.end());
        if (!gotAck && received.size() >= sizeof(ack)) {
            if (memcmp(received.data(), ack, sizeof(ack))) return false;
            received.erase(received.begin(), received.begin() + sizeof(ack));
            gotAck = true;
        }
        if (!gotAck || received.size() < 8) continue;
        const uint8_t prefix[] = {0, 0, 0xff, 0xff, 0xff};
        if (memcmp(received.data(), prefix, 5) ||
            uint8_t(received[5] + received[6] + received[7]) != 0) return false;
        size_t n = received[5] | (received[6] << 8);
        if (n < 2 || n > 512) return false;
        if (received.size() < n + 10) continue;
        sum = 0;
        for (size_t i = 8; i <= n + 8; ++i) sum += received[i];
        if (sum || received[n + 9] || received[8] != 0xd7 || received[9] != code + 1) return false;
        response.assign(received.begin() + 10, received.begin() + 8 + n);
        return true;
    }
    return false;
}

static bool statusCommand(uint8_t code, const std::vector<uint8_t>& args) {
    std::vector<uint8_t> response;
    return command(code, args, response) && response.size() == 1 && response[0] == 0;
}

static bool initializeReader() {
    if (!send(ack, sizeof(ack))) return false;
    // Cancel an earlier command and discard stale data before the first command.
    std::vector<uint8_t> discarded;
    for (int i = 0; i < 4 && bulk(epIn, nullptr, 0, discarded, 30); ++i) {}
    if (!statusCommand(0x2a, {1}) || !statusCommand(0x06, {0})) return false;
    if (!statusCommand(0x00, {1, 1, 15, 1})) return false; // NFC-F, 212 kbit/s
    // Port-100 protocol parameters: CRC, framing, guard time and RF collision avoidance.
    if (!statusCommand(0x02, {
        0x00,0x18, 0x01,1, 0x02,1, 0x03,0, 0x04,0, 0x05,0,
        0x06,0, 0x07,8, 0x08,0, 0x09,0, 0x0a,0, 0x0b,0,
        0x0c,0, 0x0e,4, 0x0f,0, 0x10,0, 0x11,0, 0x12,0, 0x13,6})) return false;
    return true;
}

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
    result.assign(response.begin() + 5, response.end());
    return ReadResult::Balance;
}

static ReadResult readBalance(int& balance) {
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
    return ReadResult::Balance;
}

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
    epIn = epOut = 0;
    uint8_t alt = 0;
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(config);
    for (size_t pos = config->bLength; pos + 2 <= config->wTotalLength;) {
        uint8_t len = bytes[pos], type = bytes[pos + 1];
        if (len < 2 || pos + len > config->wTotalLength) break;
        if (type == USB_B_DESCRIPTOR_TYPE_INTERFACE && len >= 9) {
            if (epIn && epOut) break;
            interfaceNumber = bytes[pos + 2]; alt = bytes[pos + 3]; epIn = epOut = 0;
        } else if (type == USB_B_DESCRIPTOR_TYPE_ENDPOINT && len >= 7 && (bytes[pos + 3] & 3) == 2) {
            uint8_t ep = bytes[pos + 2];
            if (ep & 0x80) { epIn = ep; inPacketSize = (bytes[pos + 4] | (bytes[pos + 5] << 8)) & 0x7ff; }
            else epOut = ep;
        }
        pos += len;
    }
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
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setFont(&fonts::lgfxJapanGothic_40);
    M5.Display.setTextDatum(m5gfx::textdatum_t::top_center);
    M5.Display.drawString("Suica 残高リーダー", M5.Display.width() / 2, 15);
    faceStatusMutex = xSemaphoreCreateMutex();
    configASSERT(faceStatusMutex);
    avatar.getFace()->getBoundingRect()->setPosition(64, 0);
    avatar.getFace()->getBoundingRect()->setSize(M5.Display.width(), M5.Display.height() - 64);
    avatar.setSpeechFont(&fonts::lgfxJapanGothic_40);
    avatar.init();
    M5.Power.setExtOutput(true, m5::ext_USB);
    show("RC-S380を接続してください");
    usb_host_config_t host = {};
    host.intr_flags = ESP_INTR_FLAG_LEVEL1;
    ESP_ERROR_CHECK(usb_host_install(&host));
    xTaskCreate([](void*) {
        while (true) { uint32_t flags; usb_host_lib_handle_events(portMAX_DELAY, &flags); }
    }, "usb-daemon", 4096, nullptr, 5, nullptr);
    usb_host_client_config_t cc = {};
    cc.max_num_event_msg = 8;
    cc.async.client_event_callback = usbEvent;
    ESP_ERROR_CHECK(usb_host_client_register(&cc, &client));
}

void loop() {
    updateReadConfirmation();
    usb_host_client_handle_events(client, pdMS_TO_TICKS(10));
    if (disconnected) { closeReader(); show("RC-S380を接続してください"); }
    if (pendingAddress) { uint8_t address = pendingAddress; pendingAddress = 0; openReader(address); }
    static uint32_t lastRead;
    static int lastBalance = -1;
    static ReadResult lastResult = ReadResult::NoCard;
    if (!ready) { lastBalance = -1; lastResult = ReadResult::NoCard; return; }
    if (millis() - lastRead < 500) return;
    lastRead = millis();
    int balance = -1;
    auto result = readBalance(balance);
    if (disconnected) return;
    if (result != lastResult || balance != lastBalance) {
        switch (result) {
        case ReadResult::Balance:
            show("残高", balance);
            Serial.printf("Balance: %d JPY\n", balance);
            // Playback is asynchronous, so USB event handling can continue.
            if (lastResult != ReadResult::Balance) playReadConfirmation();
            break;
        case ReadResult::NoCard: show("Suicaをかざしてください"); break;
        case ReadResult::Unsupported: show("読取失敗：カードを置き直す"); break;
        case ReadResult::TransportError: show("通信エラー：USBを接続し直す"); ready = false; break;
        }
        lastBalance = balance;
        lastResult = result;
    }
}
