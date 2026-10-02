# Bluetooth 表示デバイス / FreeFit APK 解析ノート

最終更新: 2026-10-02

## 対象範囲と注意事項

このワークスペースには `zwsvibe_V_1.2.34.apk.jadx` から復元したソースがある。
アプリのパッケージ名は `com.czw.freefit.ienjoy`、アプリ版は `V 1.2.34`。
以下ではアプリコードから分かる事実と、そこからの推測を分ける。

これは複数機種対応の FreeFit クライアントで、専用 SDK ではない。JieLi 系と
Bluetrum 系を両方含むため、APK に存在するコマンドが接続中の AB5691E で有効とは
限らない。実機確認にはまず読取り専用コマンドと公式アプリのキャプチャを使い、
初期化、削除、OTA、通話、スポーツ制御を対応確認目的で送らない。

## ハードウェア / プラットフォーム

### 観測済み識別情報

アプリの診断出力:

```text
version == 1.13
bluetoothName == 07-4735-03
```

アプリは `devicePlate-devicePlateFirmware-customerBranch` を結合して Bluetooth 名として
扱う。`07` は ZK/Bluetrum モード (`DEVICE_ZK_MODE = 7`) を選び、`06` は JieLi モード。
基板上の SoC は **Bluetrum AB5691E** と物理確認済みである。

### Flash の可能性

SPI Flash 単体チップが見当たらないだけでは、内蔵不揮発メモリだけを使うとは断定できない。
公開 AB5691 製品には外付け / プラグイン Flash 128 Mbit（16 MiB）と記すものがある。

- HT30: AB5691、`外挂Flash 128Mb`、FitCloudPro、OTA 対応。
  <https://m.sunsky-online.com/zh_CN/app/product/default%21description.do?itemNo=EDA007370501D>
- Aero Fit 34: AB5691、`Plug-in Flash 128Mb`、FitCloudPro、OTA 対応。
  <https://pdt.static.globalsources.com/IMAGES/PDT/SPEC/376/K1224025376.pdf>
- Koruima T13: AB5691、128 Mbit Flash。
  <https://www.koruima.com/pages/info?id=1195>

この基板でも、無印 WSON/USON/SOP-8 Flash、基板裏面の部品、シールドやコーティング下の
部品である可能性が高い。これは推測であり、結論の前に両面とシールド下を確認する。

AB5691 製品には HT30、HT31、T13、W7、MT64、Aero Fit 34 などがあり、FitCloudPro または
H Band を使う例が多い。同系列 OTA やプロトコルの参考にはなるが、別製品のファームを
この機器へ書き込んではならない。表示器、センサー、ピン配置、リソース配置、ブート鍵、
基板 ID が異なり得る。

## APK 構成と GATT

| 領域 | 主なコード | 役割 |
| --- | --- | --- |
| BLE 基盤 | `com.czw.bluetoothlib` | スキャン、GATT 通信、接続状態 |
| デバイス処理 | `com.czw.freefit.device.FreeFitDevice` | コマンド、応答振り分け、文字盤処理 |
| Bluetrum カスタム文字盤 | `ZKMakeDialUtil.java`、`ZKDialUtil.java` | 画像生成・分割 |
| 画像変換 | `ImgToBmpUtil.java` | RGB565 変換、特殊コンテナ生成 |
| Bluetrum OTA | `ZkOtaActivity.java` | URL から OTA を取得して `BleOtaManager` へ渡す |

ZK/Bluetrum モードの通常 GATT UUID（根拠: `BleConstantConfig.java`）:

| 用途 | UUID |
| --- | --- |
| Service | `6e40fc00-b5a3-f393-e0a9-e50e24dcca9e` |
| アプリ→機器 Write | `6e40fc20-b5a3-f393-e0a9-e50e24dcca9e` |
| 機器→アプリ Notify | `6e40fc21-b5a3-f393-e0a9-e50e24dcca9e` |

別モードでは別 characteristic の可能性があるので、書込み前には実機 GATT 列挙で確認する。

## 背景画像 / カスタム文字盤転送

### 実装済みの単純 RGB565 経路

PC ツールは `ZKMakeDialUtil` の `DEVICE_MODE_DIAL == 0` だけを実装する。アプリの流れ:

1. 幅・高さが未知なら `E4 53 01 00` (`getDialInfo`) を送信する。
2. `E4 53` 通知から、bytes 11–12 を転送パケットサイズ、13–14 を幅、15–16 を高さ、
   17–18 を角丸半径（いずれも big-endian）、byte 19 を回転
   (`0/1/2/3` = `0/90/180/270` 度) として読む。
3. 画像を中央トリミング・拡大縮小し、角丸マスクを適用する。画素形式は big-endian
   RGB565: `((R >> 3) << 11) | ((G >> 2) << 5) | (B >> 3)`。
4. `E4 51` の 20 byte 開始パケットを送る。
5. `E4 51` 通知を待ち、`E4 52` データを一つずつ送る。次は前の `E4 52` ACK 後だけ送る。

ホストの GATT 書込み成功は表示制御側の受理を意味しないため、逐次 ACK が重要である。

### `E4 51` 開始パケット

| オフセット | 内容 |
| --- | --- |
| 0–3 | `E4 51 01 00` |
| 4–5 | チャンク数、big-endian |
| 6–9 | パディング前 RGB565 バイト数、big-endian |
| 10 | `00` |
| 11–12 | チャンクごとの画像ペイロード長 (`frame_size - 14`)、big-endian |
| 13 | UI が選んだ文字盤タイプ |
| 14 | `01` |
| 15 | 時刻文字の位置 / スタイル |
| 16 | `00` |
| 17–18 | 時刻文字色 RGB565、big-endian |
| 19 | `00` |

### `E4 52` データパケット

GATT value 全体は `frame_size` byte、そのうち `frame_size - 14` byte が RGB565 データ。
最終ペイロードはゼロ埋めするが、開始パケットにはパディング前の真の長さを入れる。

| オフセット | 内容 |
| --- | --- |
| 0–3 | `E4 52 01 02` |
| 4–5 | 1 始まりのチャンク番号、big-endian |
| 6–9 | 元画像データ内のバイトオフセット、big-endian |
| 10 | 整数の進捗率 |
| 11 | 最終フラグ（最終だけ `01`） |
| 12–13 | bytes 0–11 とゼロ埋め済みペイロードの 16 bit 和、big-endian |
| 14… | RGB565 データ |

Java 実装は checksum 計算時、まだゼロの byte 14 も加算対象にする。データ ACK は byte 9 が
ゼロなら成功、開始 ACK は応答 byte 2 が 2 より大きいとエラーとして扱う。

### ほかの文字盤モードと `E4` コマンド

`DEVICE_MODE_DIAL != 0` では `ImgToBmpUtil.getNew565BitmapBin()` が
`77 66 5F 64 ...` (`wf_d`) で始まる大きな独自コンテナを作る。画像、サムネイル / 背景、
フォント・時計メタデータ、メディアフレームなどを含む可能性がある。対象デバイスの
このモードは未取得のため PC ツールでは未実装。非ゼロモードに単純 RGB565 を送ってはならない。

| 機能 | アプリ→機器 |
| --- | --- |
| カスタム文字盤の寸法照会 | `E4 53 01 00` |
| インストール済み文字盤 ID 照会 | `E4 56 01 00` |
| インストール済み文字盤の削除 | `E4 56 01 03 total_length id:utf8`（状態変更あり） |
| 文字盤切替間隔 | `E4 58 interval_lo interval_hi`（この値だけ little-endian） |
| 時刻文字位置 | `E4 54 position`。日付非表示対応時は `E4 54 position type` |
| 時刻文字色 | `E4 55 colour_byte0 colour_byte1`。日付非表示対応時は末尾に `type` |
| 日付表示 / 非表示 | `E4 57 hide_state type`（日付非表示対応時のみ） |
| 文字盤転送中止 | `E4 5A` |

アプリは `E4 56` をインストール済み文字盤情報として扱う。削除・中止を探索目的で送らない。

### 実機確認と転送失敗の診断

対象 `41:42:E1:92:76:86` は読み取り用照会へ次を返した。

```text
E4 53 02 00 02 00 F0 01 28 00 AC 00 ED 00 90 00 B2 00 14 00
```

| 項目 | 値 |
| --- | --- |
| 転送パケットサイズ | 237 byte |
| 背景画像寸法 | 144 × 178 pixel |
| 角丸半径 | 20 pixel |
| 回転 | 0° |
| 単純 RGB565 画像サイズ | 51,264 byte |
| `E4 52` ペイロード容量 | 223 byte (`237 - 14`) |
| 必要パケット数 | 230 |

これにより UUID、`E4 53`、各オフセットは実機で確認できた。最初の PC 転送では機器は
転送画面に入り 0% を表示したが、最初の `E4 52` ACK 待ちでタイムアウトした。`E4 51` は
次の ACK を返しており、開始要求までは受理されている。

```text
E4 51 02 00 00 E6 00 00 C8 40 00 00 DF 01 01 00 00 FF FF 00 03
```

原因候補は画像データの転送方式 / 検証段階に絞られる。重要な修正として、アプリが
`BleManager.write(..., data, false, ...)` に渡す `false` は **分割書込み**フラグである。
`BleManager.A0` は `BluetoothGatt.writeCharacteristic()` を直接呼び、
`setWriteType()` は使わない。従って Write Without Response ではなく、既定の ATT Write
Request を使う。`zk_background.py` は `E4 53`、`E4 51`、`E4 52` を `response=True` で
送るよう修正済みである。

最初の試行は時刻文字位置 `0` も使っていた。アプリは
`WatchFaceCustomFragment.timeTextDirection` を `5` に初期化して
`selectTimeDirection(4)` を呼ぶため、PC ツールの既定値も `5` に修正済み。

`btsnooz_hci.btsnoop` と `btsnooz_hci.log` は有効な btsnoop だが、接続 / 制御だけで
`E4` の GATT Write / Notify を含まない。公式アプリで識別しやすい画像を適用しながら
btsnoop/logcat を取り、`E4 51` / `E4 52` を比較する必要がある。

## PC ツール

`tools/zk_background.py` は上記単純経路の Python 実装。既定では安全側で動く。

- `--scan`、`--info`、`--output` は状態を変更しない。
- 背景を書き換えるのは `--send` 指定時だけ。
- `--send` は読み取り用 `E4 53` の結果と ACK を検証し、異常時には停止する。
- OTA や生ファームウェア書込み機能は含まない。

```powershell
py -m pip install -r tools/requirements.txt
py tools/zk_background.py --scan
py tools/zk_background.py --address AA:BB:CC:DD:EE:FF --info
py tools/zk_background.py --image background.png --width 240 --height 280 --output background.rgb565
py tools/zk_background.py --address AA:BB:CC:DD:EE:FF --image background.png --send
```

PC 接続前に Android アプリを切断または強制終了し、`--info` の結果が公式アプリと一致する
ことを確認する。

## その他の BLE プロトコル（静的 APK 解析）

### 通信路と表記

この節のパケットは Write UUID `6e40fc20-…` に書く値、または Notify UUID
`6e40fc21-…` から受ける値である。HCI パケットではなく、共通の外側長さ / checksum
フレームもない。先頭 byte がコマンド群を選ぶ。以下の値は unsigned 表記、`u8`、`be16`、
`be32` はそれぞれ 1 byte、16 bit big-endian、32 bit big-endian を表す。

`FreeFitDevice.onDataChanged()` はほぼ先頭 byte で通知を振り分ける。`E4` は ZK
(Bluetrum) 文字盤、`FB` はイヤホン制御系列である。複数パケット利用者データは、ZK 経路で
ホスト側 GATT 書込み完了コールバックにより直列化される。

### 接続直後の照会と一般制御

| 機能 | アプリ→機器 | 機器→アプリ / 解釈 |
| --- | --- | --- |
| 対応機能の照会 | `03 00` | `83 ...`。長さ 10 以上なら能力レコード。bytes 1–3 は branch・デバイスモード・firmware plate、byte 4 は対応機能 bit、bytes 5–9 は OTA / 天気 / アラーム / 通知対応など、byte 12 はあれば `DEVICE_MODE_DIAL`。モード 9 は ZK モード 7 として扱う。 |
| ファームウェア版照会 | `1F` | `9F ...`。bytes 12–13 が `major.minor`、byte 14 があれば追加。 |
| 電池残量照会 | `14` | `94 percent`。`percent = byte 1`。 |
| 時刻同期 | `01 unix_s:be32 utc_offset_s:be32 clock_format language language_flag` | `81 00` は成功。24 時間制は `clock_format=1`、12 時間制は `2`。言語値はアプリ固有。 |
| 機器からの時刻要求 | — | `0D ...`。アプリが直ちに時刻同期を送る。 |
| 距離・温度単位 | `11 distance_unit temp_unit 00 00` | `0` は metric / Celsius、`1` は非 metric / 非 Celsius。`91` は単位ハンドラへ渡るが本 APK は本文未復号。 |
| リアルタイム歩数の有効化 | `31 enable` | `B1` は状態、`B2` / `B3` はライブ歩数。 |
| バンド探索 | `51 enable` | `D1 state`。 |
| 紛失防止 | `70 enable` | `F0 ...`。 |
| 工場出荷状態へ戻す | `71 01 02 03` | 応答検証なし。送信禁止。 |
| 汎用パラメータ照会 | `A3` | `A3 ...`。byte 1 をデバイス定数として保存。 |
| QR コード照会 | `A0` | `A0 ...`。QR ハンドラで解析。 |

能力応答 `83` は、アプリが選ぶプラットフォーム経路を知るため最も有用な安全側の照会候補。
対象の `07-4735-03` は ZK と整合するが、実際の `83` 応答の代わりにはならない。

### ユーザー情報、リマインダ、通話・通知

| 機能 | アプリ→機器 |
| --- | --- |
| ユーザープロフィール | `02 01 weight:be16 age height step_width sex step_goal:be32` |
| 通知 / 設定 bitfield | `02 02 skype line sedentary_time sedentary_enable call sms wechat qq kakaotalk facebook twitter whatsapp linkedin continuous_hr raise_to_wake hr_interval instagram other_push zalo_messenger_bits` |
| アラーム一覧 | `02 03` の後に最大 5 個の `enable hour minute repeat`。bytes 2–13 に 1–3 件目、byte 14 に件数、4・5 件目は bytes 15–18、19–22。 |
| 飲水リマインダ | `B0 00 enable start_hour start_min end_hour end_min interval` |
| 着信通知状態 | 有効 `55 01 00 00`、無効 `55 02 00 00` |
| 着信メタデータ | `55 kind name_len name:utf8 number_len number:utf8` |
| 電話の応答 / 拒否 | `55 03 00 00` / `55 04 00 00` |
| 汎用通知 | `73 packet_index notification_type payload:utf8[≤17]` を 100 ms 間隔で送信。 |
| ZK 通知 | `23 packet_index notification_type payload:utf8[≤17]`。最終短縮ブロックは `FF` 終端、満杯の最終ブロックには `23 last_index notification_type FF` を追加。 |
| カメラ制御の有効化 | `52 enable` |

機器からの `53 state` はスマートフォン探索、`55 01` / `55 02` は通話の拒否 / 応答、`A2`
は Android の撮影要求である。これらは PC では要求への応答でなく非同期イベントとして扱う。

### 健康、活動、スポーツ

| 機能 | アプリ→機器 | 機器→アプリ / 復号形式 |
| --- | --- | --- |
| ライブ計測の開始 / 停止 | `60 measure_type enable`。`00` 心拍、`01` 血圧、`02` SpO2、`03` 全部 | `E0 type enabled` は状態。`E1 hr sys dia spo2 avg_hr high_hr low_hr [blood_sugar]` はライブデータ。 |
| 体温計測 | 開始 `E2 01`、停止 `E2 02` | `E2 mode status` は完了状態。`E3 ambient_tenths:be16 body_tenths:be16` は符号付き 0.1 °C。body の符号判定に逆コンパイル由来の疑義があるため負値は要実測。 |
| 日別スポーツ照会 | `13 (year-2000) month day` | `93`。通常レコードは bytes 1–14、byte 1 が `F2/F3/F4` なら同期状態 2/3/4。 |
| 睡眠照会 | `15 (year-2000) month day` | `95`。2 エントリ / packet、各々 `start_unix_s:be32 duration:u8 type:u8`。bytes 1–2 の `FF FF` は終端。 |
| 履歴計測照会 | `16 (year-2000) month day` | `96`。2 サンプル / 19 byte。各々 `marker:u8 timestamp:be32 hr sys dia spo2`。`FF FF` marker は終端。 |
| リアルタイム歩数 | — | `B2` / `B3` で byte 1=`01`。steps: bytes 2–5 `be32`、calories: 6–7 `be16`、distance: 8–11 `be32`。 |
| スポーツ開始 / 制御 | `9D sport_type control`。停止は `9D 00 01` | `9D status result ...`。byte 1=0 の場合 byte 2 が開始成功可否。 |
| 全スポーツ履歴照会 | `9B` | `9B ...`。別の蓄積型パーサで復号。 |
| 高度 / 気圧制御 | `B5 start:u8 altitude:be16 pressure:be24` | `B5 00` は Android 側高度 / 気圧送信処理開始、非ゼロは停止。 |

標準プロトコルの数値は概ね big-endian だが、全コマンド群に共通する規則ではない。

### 音楽、天気、位置情報

| 系統 | 方向とパケット |
| --- | --- |
| 音楽制御 | 機器通知 `99 action`。`00=pause`、`01=toggle`、`02=play`、`03=previous`、`04=next`、`05=volume up`、`06=volume down`。`41 04 level` は Android のメディア音量設定。 |
| 天気（汎用） | `05 condition temperature high_or_low`。各値は 1 byte。 |
| 天気（ZK） | 20 byte の `25 condition temperature third_value text:utf8[≤16]`。短い文字列では逆コンパイルコードは直後の最初の位置だけ `FF` にし、残りはゼロのまま。公式アプリの送信で要確認。 |
| GPS 点 | 機器 `DD ...` を GPS 点パーサが処理する。送信側 GPS / 住所処理は補助メソッドの欠落・不整合で完全復元できない。 |
| 連携 classic Bluetooth MAC | 機器 `54 state mac[6]`。8 byte パケットなら bytes 2–7 を MAC として整形。 |
| 低電池 | 機器 `72 ...`。 |

### `FB` 音声 / イヤホン サブプロトコル

`FB` は時計用ではなく、EQ、イヤホン名、ANC、イヤホン探索、音声モード、最大音量などを
扱う独立系列である。対象 AB5691E が対応すると仮定しない。

照会は `FB subcommand 00 06 item 01` の形。

| 照会内容 | パケット |
| --- | --- |
| EQ 設定 | `FB 01 00 06 02 01` |
| キーコード設定 | `FB 02 00 06 03 01` |
| Bluetooth 名 | `FB 04 00 06 05 01` |
| ANC | `FB 05 00 06 06 01` |
| 音声モード | `FB 07 00 06 08 01` |
| 最大音量 | `FB 08 00 06 09 01` |

設定は `FB subcommand 01 total_length payload checksum_le16`。例: ANC は
`FB 05 01 07 value sum_lo sum_hi`。音声モードと最大音量も subcommand `07` / `08` の
同じ 7 byte 形式。checksum byte がゼロの状態で全体を加算し、low byte を先に保存する。
big-endian の `E4 52` checksum とは異なる。

### PC デコーダ向け応答マップ

`FreeFitDevice.onDataChanged()` に明確なハンドラがある prefix は次のとおり。

`81` 時刻 ACK、`83` 能力、`94` 電池、`9D` スポーツ状態、`9F` バージョン、`A0/A1` QR、
`A2` カメラ要求、`A3` 汎用パラメータ、`B1/B2/B3` 歩数、`D1` バンド探索、`DD` GPS、
`E0/E1` ライブ健康情報、`E2/E3` 体温、`E4` ZK 文字盤、`F0` 紛失防止。`99` と `41` は
ウェアラブルからスマートフォンへの音楽制御 / 音量要求。

Python/C++ で実装する前に、機能ごとに公式アプリの最小キャプチャを取り、送信 Write、
対応 Notify、UI 操作の完了可否を記録して照合する。これにより、対応していないコマンドと
フィールド / エンディアンの誤りを安全に切り分けられる。

## OTA フローとファームウェア取得

### FreeFit API

現行アプリは次を呼ぶ。

```text
GET https://api.zwzn-tech.com/app/v1/firmware/update
    ?firmwareNo=<device firmware version>
    &bluetoothName=<plate>-<firmware>-<customer branch>
```

Retrofit 宣言は `RobotService.java`、ベース URL は `RetrofitFactory.java`。通常の HTTP
header にはブランド、モデル、OS、アプリ版、ショップソース、open ID、言語が入るが、
この endpoint には署名を付けない。

`firmwareNo=1.13` と `bluetoothName=07-4735-03`、および `0`、`1.00`、`1.12` でも
サーバー応答は次だった。

```json
{"resultStatus":false,"errorCode":"300104","errorMessage":"Currently the latest version","resultData":null}
```

`RequestUrlCopy.java` の旧 endpoint
`http://101.200.207.61:9660/app/v1/firmware/update` はこの環境でタイムアウトした。よって
FreeFit backend から対象機の公式 OTA URL は未取得で、デバイスへのファーム書込みもない。

### ローカルのファームウェアサンプル

`firmware_samples/` の README にはハッシュと入手元がある。

- `bluetrum_ab5682/Headset_A9_Pro_Flash_dump.bin`: AB5682B A9 Pro の公開 2 MiB 外部 Flash 生ダンプ。
- `bluetrum_ab5682/decrypted.bin`、`app1.bin`、`header.bin`: 対応する復号済み・抽出済み部品。
- `fitcloud_*.bin`: 公開 FitCloudPro OTA 例。うち 2 個は header が `RTL8762D` を示す Realtek 用。

AB5682B ダンプはブート / header、暗号化、リソース配置の同ベンダー参考資料にはなるが、
AB5691E 互換ではない。対象機器へ絶対に書き込まない。

- AB5682B サンプル: <https://github.com/atc1441/Bluetrum_AB5682_Hacking>
- AB560x 系 unpacker / リソース資料: <https://github.com/kagaimiq/bluetrum-tools>

## 次に収集する証拠

1. 公式アプリ切断中に `zk_background.py --info` を実行し、全 Notify 値と GATT サービス一覧を記録する。
2. 公式アプリで小さく識別しやすい画像を適用しつつ btsnoop/logcat を取得し、`E4 51` / `E4 52` を比較する。
3. ハードウェア読出し前に Flash パッケージまたはテストパッドを特定し、元イメージを保存して SHA-256 を計算する。
4. 正確に AB5691E 向けと確認できるイメージを得てから AB5682B と header・暗号化・リソース境界を比較する。別製品 OTA は更新候補に使わない。
