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
5. 読み取り成功時に残高を表示し、内蔵スピーカーから短い確認音が鳴ります。カードを離しても最後の読み取りから5秒間は残高を保持してから、待機画面に戻ります。

確認音は改札風の「ピピッ」（2500 Hz・65 msを2回、間隔30 ms）、音量は64（最大255）です。実際の改札音の録音ではありません。`src/main.cpp` の `M5.Speaker.setVolume(64)` で調整できます。読み取り成功が続いている間は繰り返し鳴らしません。表示保持中でも別の読み取りに成功すると残高を更新します。

画面左側の顔は [M5Stack-Avatar v0.10.0](https://github.com/stack-chan/m5stack-avatar/tree/v0.10.0) で描画します。まばたき・視線・呼吸のアニメーションはライブラリに任せ、読み取り成功時はHappy、エラー時はSad、待機中はNeutralに切り替えます。ネオンシアンとマゼンタを使ったサイバー風のカラーUIで、右側のパネルに桁区切りした残高、下部に状態を表示します。描画色深度は8ビットです。背景グリッド、移動する走査線、角を切ったHUD風パネルを表示します。

TAB5で日本語を含む大きな画面を描画すると標準の描画タスク（2048バイト）でスタック不足が発生するため、`scripts/avatar_stack.py` が導入済みライブラリの描画タスクを16384バイトに調整します。状態・残高の描画も同じタスクにまとめています。表情切替時の描画停止を避けるため、同スクリプトで `setExpression` のタスク停止・再開をアトミックな表情更新に置き換えています。

## 構成

- pioarduino 55.03.38 / Arduino ESP32 3.3.8、M5Unified / M5GFX
- TAB5の16MB Flash用 `default_16MB.csv` を明示（アプリ領域6.25MB×2）
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

`pio run` によるビルドとTAB5への書き込み（フラッシュ内容のハッシュ検証を含む）は成功しました。Arduinoコア由来のコンパイル警告はあります。実機でカード読取・残高描画を確認しました。表情切替後も描画フレームが継続し、LCDフレームバッファの画面取得で残高表示を確認しています。

カード未検出時に返る4バイトのRFタイムアウト応答を通信エラーと誤判定する不具合を修正済みです。応答判定の回帰テストは次のコマンドで実行できます。

```sh
c++ -std=c++11 tests/rf_response_test.cpp -o /tmp/tab5-rf-response-test
/tmp/tab5-rf-response-test
```

### 描画診断

シリアル接続から `d` を送ると描画フレーム数と描画側の残高を出力します。`s` を送ると実際のLCDフレームバッファを320×180のPPM画像として返します（`SCREENSHOT` 行に続くP6ヘッダーとRGBデータ）。画像取得中はUSBのポーリングが一時的に止まります。

## 残高の音声読み上げ（sanoTTS-jp）

読み取り成功時、確認音に続いて「のこり、○○えん、です」と内蔵スピーカーで読み上げます。「のこり」の後と「えん」「です」の間に、それぞれ100msの無音を入れます。固定の「のこり」「です」は起動時にバックグラウンドで合成し、RAMに保存して再利用します。準備中にカードを読んだ場合は、準備完了後に最新の残高を読み上げます。金額は「えん」までまとめて毎回合成します。Wi-Fiや漢字辞書は不要です。sanoTTS-jpとつくよみちゃんv4音声モデルはv1.2.0に固定しています。

- `src/speech.cpp`: 専用タスクで合成し、PSRAMへ全音声を保存してから22,050 Hz / モノラル / int16で再生します。音声バッファは20秒分、約882KBです。
- USB読み取りとAvatarは合成中も継続します。未処理の読み上げ要求は最新1件を保持し、合成中に別の要求が来た場合は古い合成結果の再生を省きます。再生中の音声は最後まで流します。
- P4向けカーネル最適化により、「です」を分離する前の実機の12,010円試験では初回約3.4秒、2回目以降約2.2秒で合成できました。話速の長さ係数は聞き取りやすさを優先して1.7に設定しています（大きいほどゆっくり）。
- シリアルから `v` を送ると12,010円の試験音声を再生します。

モデルと生成音声はMITではありません。配布時の帰属表示・用途条件を含め、[音声モデルの利用条件](third_party/sanotts/README.md)を確認してください。ライセンス本文と上流NOTICEを同梱しています。

### 読み上げテスト

```sh
c++ -std=c++11 tests/balance_speech_test.cpp -o /tmp/tab5-balance-speech-test
/tmp/tab5-balance-speech-test
cc -O2 -DSANOTTS_ENABLE_KANJI=0 -DSANOTTS_ENABLE_PIE=0 -c .pio/libdeps/tab5/SanoTTS-jp/src/core/g2p.c -o /tmp/tab5-g2p.o
c++ -std=c++11 -I.pio/libdeps/tab5/SanoTTS-jp/src/core tests/balance_g2p_test.cpp /tmp/tab5-g2p.o -o /tmp/tab5-balance-g2p-test
/tmp/tab5-balance-g2p-test
```

0〜65,535円の全金額について、上流G2Pが受け付け、モデルの入力上限350 IDs以内に収まることを検証します。

### ESP32-P4向け推論の最適化

`include/sanotts_p4_conv.h` のW8A32畳み込みカーネルを、`scripts/sanotts_p4.py` でsanoTTS-jp v1.2.0へ組み込みます。時間方向8出力の途中結果をレジスタに保持し、メモリ読み書きを減らします。1×1は専用処理、その他の奇数幅はパディング不要の中央部分で使用します。対象外のサイズや端のパディング処理には、上流の実装を使います。S3命令、オーバークロック、fast-math、モデルの変更は使用していません。

TAB5（360MHz）、Avatar・USB処理と並行で、12,010円の試験音声を比較しました。以下は段別プロファイラ有効時の単発測定で、負荷により変動します。「初回」は「のこり」の合成込み、2回目以降はその音声を再利用します。

| 合成時間 | 元の汎用C | P4最適化 | 短縮率 |
|---|---:|---:|---:|
| 初回 | 5,765 ms | 3,406 ms | 約41% |
| 2回目以降 | 3,722 ms | 2,228 ms | 約40% |

金額部分のPCMは32,768サンプル、FNV-1aは両方 `31ef3cc889ff6486` で一致しました。結合後の音声長2,482ms、ピーク・RMSも同一でした。ホストではカーネル幅1/3/7/9、チャネル数1〜304、バイアスの有無を比較し、浮動小数点のビット一致とメモリ境界を検証しています。全金額の音声を比較したものではありません。

通常ビルドでは段別プロファイラは無効です。比較時は `SAAN_P4_FAST=0` で上流カーネルへ戻せます。`SAAN_PROFILE=1` を加えると段別サイクル数を出力します。条件を揃えるため、比較するたびに再起動し、シリアルの `v` を2回送信します。

```sh
c++ -std=c++11 -O2 -fsanitize=address,undefined tests/p4_conv_test.cpp -o /tmp/tab5-p4-conv-test
/tmp/tab5-p4-conv-test
```
