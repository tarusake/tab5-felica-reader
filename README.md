# TAB5 + RC-S380 Suica残高リーダー

M5Stack TAB5のUSB-A Host端子にSony RC-S380をつなぎ、Suicaの最新利用履歴から残高を読み取って画面に表示するPlatformIOプロジェクトです。カードへの書き込みやネットワーク送信は行いません。

## 接続と実行

1. RC-S380をTAB5のUSB-A端子に接続します。
2. TAB5のUSB-C端子をMac/PCに接続します。
3. PlatformIOでビルド・書き込みします。

```sh
pio run
pio run -t upload --upload-port /dev/cu.usbmodem1101
pio device monitor --port /dev/cu.usbmodem1101 --baud 115200
```

ポート名は環境に合わせて変更してください。接続できない場合はTAB5のリセットボタンを約2秒押してダウンロードモードにしてから書き込みます。書き込みにより現在のアプリが置き換わります。

4. 「Suicaをかざしてください」と出たら、RC-S380の上にカードを1枚置きます。
5. 読み取り成功時に残高を表示します。カードを離すと待機画面に戻ります。

## 構成

- pioarduino 55.03.38 / Arduino ESP32 3.3.8、M5Unified / M5GFX
- ESP-IDF USB Host APIでRC-S380のBulk IN/OUTを直接操作
- Sony VID `054C`、RC-S380 PID `06C1` / `06C3`
- NFC-F 212 kbps、システムコード `0003`
- Read Without Encryptionでサービス `090F` のブロック0を読み取り、履歴内の10–11バイトをリトルエンディアンで残高として解釈

RC-S300やRC-S320など別の機種には対応していません。表示値は最新履歴に記録された残高です。カードやモバイルSuicaの状態によって読み取れない場合があります。

## トラブル時

- 接続待ちのまま：USB-A端子、データ通信対応ケーブル、TAB5の電源を確認してください。
- 初期化・通信エラー：RC-S380のUSBを抜き差ししてください。
- 読取失敗：カードを1枚だけ置き、位置を調整してください。
- シリアルログにUSBのVID/PIDと通信エラーを表示します。IDmや履歴全文はログに出しません。

### このMacのツールチェーン配置への対処

pioarduinoのパッケージ導入時、コンパイラが一段深いディレクトリに配置され、標準ヘッダーが欠落する問題が発生しました。この作業環境では、ダウンロード済みの公式ツールチェーンをプロジェクト内の `.tools/` に展開しています。`scripts/toolchain_path.py` がそのコンパイラを優先します。`.tools/` はGit管理外です。

同じMacで展開し直す場合：

```sh
mkdir -p .tools
tar -xJf "$HOME/.platformio/dist/riscv32-esp-elf-14.2.0_20260121-aarch64-apple-darwin.tar.xz" -C .tools
```

正常なツールチェーンが通常の場所に導入されている環境では、この展開は不要です。

## 参照資料

- [M5Stack TAB5公式仕様](https://docs.m5stack.com/en/core/Tab5)
- [Espressif USB Host API](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/peripherals/usb_host.html)
- [nfcpy RC-S380ドライバ：Port-100通信仕様の参照](https://github.com/nfcpy/nfcpy/blob/master/src/nfc/clf/rcs380.py)
- [Suica履歴データの読取例](https://gist.github.com/MCO-APPS/5949ec0acb934efc26a4)

## 検証状況

`pio run` によるビルドとTAB5への書き込み（フラッシュ内容のハッシュ検証を含む）は成功しました。Arduinoコア由来のコンパイル警告はあります。実機でのカード読取は未確認です。

カード未検出時に返る4バイトのRFタイムアウト応答を通信エラーと誤判定する不具合を修正済みです。応答判定の回帰テストは次のコマンドで実行できます。

```sh
c++ -std=c++11 tests/rf_response_test.cpp -o /tmp/tab5-rf-response-test
/tmp/tab5-rf-response-test
```
