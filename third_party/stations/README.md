# 駅コード・読み仮名データ

- `station_codes.csv`: [soltia48/suica-viewer](https://github.com/soltia48/suica-viewer), commit `e1fde64fb1d255bf9ca658e6c4cba9078ce26ae2`, `assets/station_codes.csv`を改変せず収録。MIT、Copyright (c) 2025 KIRISHIKI Yudai。全文は `LICENSE-CODES.txt`。
- `readings.csv`: [Seo-4d696b75/station_database](https://github.com/Seo-4d696b75/station_database), commit `43f0642170b70e42f7143e4eef450fafe141f35e`, `out/main/station.csv`から駅名・読み仮名を抽出、重複排除・並び替え。作成者 Seo-4d696b75、[CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)。提供元の利用規約は `LICENSE-READINGS.md`。
- `data/stations.inc`: 上記2データを `scripts/generate_stations.py` で照合・正規化した派生データ。CC BY-SA 4.0で提供します。このデータにはプロジェクト自作コードのMITを適用しません。再配布時は本表示と元のMIT表示を保持してください。

同名異読・競合するコードは推測で補わず除外します。廃駅・改称・コードの差異などがあるため、全駅や最新性は保証できません。駅名データと読み仮名は端末内で参照し、カードの履歴を外部へ送信しません。

履歴の解釈の参考: [SolidX/SuicaTools の SuicaLogEntry.cs / TransactionType.cs](https://github.com/SolidX/SuicaTools)。実装コードのコピーは行っていません。
