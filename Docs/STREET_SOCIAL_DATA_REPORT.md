# 街區交流資料匯出報告

日期：2026-10-07。這是五個性路 profile 的共用 catalogue，不是全遊戲會話完成宣告。

匯出器是通用的：`--personalities` 為空時會走 `communication/info_*.unity3d` 路徑表裡的每個 id。這次只輸出使用者指定的五個性格 `0,18,17,27,37`。沒有讀取任何角色卡。

## 命令與測試

```text
python -m unittest Tools\tests\test_street_social_catalogue.py
python Tools\build_street_social_catalogue.py --personalities 0,18,17,27,37 --transcode all --sample-count 5
```

單元測試 13 項，全部通過（0.171 秒）。覆蓋：正負性格聲音資料夾、FileName 與 `MinusThroughToString` 衝突時不猜、檔名不推類別、多段順序與中間缺失留空、`abdata/` 前綴視為同一 bundle、整數 file 不當語音、bundle 性格不符不取用、`callName` 不從索引亂配、436 列 heroine walker 無越界、短表報錯不繞回、NPC `Group.Talk` 不拆成三類。

第二次匯出結束碼 0。`layout_errors` 為 0。ffmpeg 使用已安裝的 imageio-ffmpeg `ffmpeg-win-x86_64-v7.1.exe`，沒有安裝套件。

## 輸出路徑

| 路徑 | 內容 |
|---|---|
| `Saved/StreetSocial/Data/catalogue.json` | schema 1，1230 nodes，原 OGG 為絕對路徑 |
| `Saved/StreetSocial/Data/rules.json` | 聲音表、路徑表、StepUp、條件支援、occurrence 原列 |
| `Saved/StreetSocial/Data/gaps.json` | 缺口計數、例子、每個性格的列數 |
| `Saved/StreetSocial/Data/summary.json` | 計數與解碼抽樣 |
| `Saved/StreetSocial/Data/audio_manifest.json` | 851 筆中文轉碼，含來源/輸出 sha256 |
| `Content/IndependentResources/StreetSocialData/` | 同上五份 JSON，另加 `zh_ogg/c00|c17|c18|c27|c37/` |

`Content` 的 `catalogue.json` 同樣是 schema 1、1230 nodes。中文 OGG 數量：c00/c17/c18/c27 各 178，c37 為 139，合計 851，與 manifest 的 `ok` 一致，`failed` 為 0。

## Node schema 1

頂層：`schema` = 1，`nodes` 為陣列。每個 node 必有：

| 欄位 | 型別 | 意義 |
|---|---|---|
| `id` | string | `p{personality}_r{row}_{category}`，同一列多角色時再加 role |
| `personality` | int | 性格 id，不是卡片 |
| `category` | enum | `daily` / `romance` / `intimacy` / `greeting` / `farewell` |
| `text_jp` | string | Communication 表原文，保留 `[P名]` `[H名]` `[Pあだ名]` |
| `text_zh` | string | 有對上才填，否則空字串 |
| `voice_files` | string[] | **原日文 OGG 絕對路徑**，只含對上的 clip，依原順序 |
| `favor` | int | 該列原始 favor 增量，不是好感門檻，也不是 ChangeParam 修正後的值 |
| `lewdness` | int | 該列原始 lewdness 增量 |
| `conditions` | object | 見下一節，不可只讀一個 bool |
| `source` | object | 表名、bundle、列號、stage、group、command、roles、pose、表情、anger、file |

另外有 `voice_files_zh`：與 `voice_files` 對應的中文 OGG 絕對路徑。這次 889 個有原音的 node 都有中文路徑。播放時中文優先；某一 slot 沒有中文就用同一 index 的原 OGG。沒有原 OGG 的 slot 不會改配別句。

`source.stage` 是關係階段 0/1/2（LoadData 的 stage），不是性格。`source.command_label_jp` 只在 `Group.Talk` 有值。`source.voice_sequence` 保留空槽：`callName`、缺檔、非整段語音都在這裡，**不要**把 `voice_files` 當成無洞的完整播放序列。

已檢查 1230 個 node：必備欄位齊、category 都在五個枚舉內、`personality`/`favor`/`lewdness` 都是 int、`voice_files` 有值者皆為絕對 `.ogg` 路徑。

類別證據（不靠檔名前綴）：

| category | 證據 |
|---|---|
| `daily` | `KoikatsuSunshine_Data/level6` 的 `Button Talk 0` 文字是「世間話」。`TalkScene.buttonTalkContents[0]` 呼叫 `CommandFunc(0)`，進入 `Group.Talk` command 0 |
| `romance` | `Button Talk 1` 文字是「コイバナ」，command 1 |
| `intimacy` | `Button Talk 2` 文字是「エッチ」，command 2；`Game.isAddH == false` 時此鈕隱藏。這不是 R18 鈕「エッチしたい」 |
| `greeting` | `GetIntroductionADV` 讀 `Group.Introduction` |
| `farewell` | `GetEndConversationADV` 讀 `Group.EndConversation` |

`Topic`、`Listen`、`Tips`、`Event`、`See`、`Touch`、`Interlude`、`LeaveAlone`、`RankUp`，以及 NPC 的 `Group.Talk`，都不進入這五類。

## 條件支援（必須照這張表，不可把未知當 true）

`conditions` 物件：

```json
{"id": -1, "game_speak_filter": "always", "street_support": "supported"}
```

`Info.ConditionDeterminationSpeak` 的原作行為，和本匯出允許街道使用的結果：

| `conditions.id` | 原作說話過濾 | `street_support` | 這次 node 數 |
|---|---|---|---|
| `-1` | 一律可說 | `supported` | 750 |
| `0` | 一律不說 | `supported` | 55 |
| `40` | 暱稱不是普通且不是事件暱稱 | `unsupported` | 5 |
| `41` | 暱稱不是普通且 `talkEvent` 含 54 | `unsupported` | 5 |
| 其他（這次出現 1–14、101、102、104、105） | 原作 `default` 回傳 true | `unsupported` | 415 |

支援合計 805，不支援 425。原作對未知 id 回傳 true；街道runtime **不得**沿用這個 default。不支援的 node 可以顯示、可以播已經對上的語音，但不能當成條件已通過。

還沒有實作、也不會假裝通過的分支：

- `CreateBranchADV` 依屬性、喜好、`talkTemper` 或隨機權重選 success/failure。每條線都保留，匯出器不替任何分支選一個勝者。
- `GameAssist.ChangeParam` 的喜好、智力/體能/變態、同宿、祈願修正，以及 favor 夾在 `stepUpDic[stage]`、lewdness 夾在 0–100。node 上的 favor/lewdness 是表內原始增量。
- occurrence 表這次收進 `rules.json` 13 列，`street_support` 全部是 `unsupported`。
- 選項文字只在 `OptionDisplayItems` 有該 condition id 時寫入 `source.choice_label`。類別裡的字面預設「ああ」沒有拿來當台詞。
- 整數 `file`（這次是 `0`、`54`、`55`）加上 `adv/scenario/...` 是事件 id 或劇本開啟，不是語音。5 個性各 3 列，共 15，狀態 `not_voice`。

`StepUpData`（`abdata/action/list/stepup/00.unity3d`）門檻是資料，不是先前建議的 20/50/80：

| stage `no` | `limit` |
|---|---|
| 0 | 75 |
| 1 | 100 |
| 2 | 150 |
| 3 | 150 |

## 五個 profile

路徑表最後一份是 `info_63`（依檔名排序覆寫，與 `GameAssist.LoadInfo` 相同）。五張表都是 `CommunicationInfo`，各 436 列，walker 沒有越界。

| personality | 聲音夾 | 名稱 | 來源 bundle / asset | nodes | 有原 OGG 的 daily / romance / intimacy / greeting / farewell |
|---|---|---|---|---|---|
| 0 | c00 | セクシー | `communication/info_50.unity3d` / `Communication_00` | 246 | 35 / 43 / 31 / 47 / 22 |
| 17 | c17 | ひねくれ | `info_50` / `Communication_17` | 246 | 35 / 43 / 31 / 47 / 22 |
| 18 | c18 | 不幸少女 | `info_31` / `Communication_18` | 246 | 35 / 43 / 31 / 47 / 22 |
| 27 | c27 | 意地っ張り | `info_50` / `Communication_27` | 246 | 35 / 43 / 31 / 47 / 22 |
| 37 | c37 | 無感情 | `info_00` / `Communication_37` | 246 | 35 / 43 / 31 / 46 / 22 |

合計：greeting 275、daily 245、romance 330、intimacy 240、farewell 140。有原音的 node 889。每個 profile 的三類有音節點都超過 3，開場與道別也都有。

聲音表在 `abdata/etcetra/list/config/00.unity3d`。資料夾規則是 `c` + `MinusThroughToString("00")`，並且必須與 `VoiceInfo.FileName` 相同才採用。負編號：

| No | FileName | 名稱 | 這次 catalogue |
|---|---|---|---|
| -13 | `c-13` | 案内人 | 未匯出。它是 `CommunicationNPC_13`，Talk 不拆成三鈕 |
| -100 | `c-100` | モブ | 聲音表有，路徑表沒有會話 asset |

抽樣一筆（personality 0，日常，stage 0，command 0「世間話」，role success，條件 -1 supported）：

- id `p0_r15_daily`
- 日文「そんな風に見える？　これでも君が初めてなのよ♡」
- 中文「看起來像那樣嗎？　我可是你才是第一個喔♡」
- 原音 `voice_extract/audio/c00/adv/com_ev_00_022.ogg`（存在，OggS）
- 中文 `Content/IndependentResources/StreetSocialData/zh_ogg/c00/com_ev_00_022.ogg`（存在）
- pose `ベース21`，表情 `笑顔`，anger 0，favor 3，lewdness 0

每個性格、每個類別各解碼了第一個原 OGG，共 25 個，`decode_failures` 為空。這不是全庫解碼。

## 語音與翻譯怎麼對上

1. 列上的 `assetbundle` + clip 對 `audio_index`。索引裡的 bundle 多了 `abdata/` 前綴；去掉這個前綴後必須與列上的 bundle 相同。性格資料夾還必須等於聲音表 FileName。不符就丟棄，不改配。
2. `voice_files` 只放對上的原 OGG。中文 WAV 來自 `voice_extract/cn_audio_comm/<folder>/<kind>/<clip>.wav`，轉成 Vorbis `-q:a 4 -ar 22050`。manifest 有來源 wav sha256 與輸出 ogg sha256。
3. `callNameN` 是 `Heroine.CallNameChange` 的執行期暱稱音，不選任何固定 OGG。
4. 中文句子：clip 的 `comm_lines` 原文必須等於該列 `text_jp`，才用 `comm_zh[最後一個 token]`。沒有 clip 的句子才用 zh-TW `Text` 的整句完全相同鍵。兩種譯文衝突則留空。

翻譯狀態：`comm_zh_last_clip` 937，`exact_jp` 209，對不上 84。

## 缺口（沒有補造成功）

| 缺口 | 數量 | 處理 |
|---|---|---|
| 有字無 clip，翻譯檔也沒有整句相同鍵 | 83 | `text_zh` 空，無語音 |
| `comm_zh` clip 擁有者原文與該列不同 | 1 | `p37_r421_greeting`，file `com_ev_37_467,callName0`，不用那筆翻譯 |
| clip 不在 `audio_index` | 50 | 不配音。例子：`com_ev_00_114`、`com_ev_00_115`、`com_ev_00_238`、`com_ev_00_239`、`com_ev_00_408`（道別列，bundle 為空） |
| 索引有、磁碟無 OGG | 5 | 多段開場的第一段。`com_ev_00_467_00`（17/18/27 同形）與 `com_ev_37_467` 路徑不存在。後段若存在仍留在原位，不前移補洞 |
| `callName` | 5 | 同上五個開場 node，狀態 `unsupported` |
| 整數 file / 劇本開啟 | 15 | `not_voice` |
| 436 列中 walker 沒讀到的列 | 每性格 22，合計 110 | 列號 397–406（`row += 10`）與 424–435（表尾，LoadData 沒有再讀）。沒有配類別 |

多段例子 `p0_r421_greeting`：`com_ev_00_467_00` 缺檔，`callName0` 不解析，`com_ev_00_467_01` 有原 OGG。`voice_files` 只有第二段那一個路徑。

資產原文與 `comm_lines` 有兩處差一個字，因此沒有採用該 clip 的翻譯：personality 0 `sun_com_00_024`（絞 / 搾），personality 27 `sun_com_27_061`（に / で）。語音仍依 clip 連結。

## 給街道 runtime 的界線

這份 catalogue 可以做五個性的三類話題、開場、道別，以及有原音/中文音的句子。它不能代表：

- 性格 0–39 的其餘 35 個 heroine 表（腳本可以再跑，這次沒跑）
- 固定角色 -13 的 NPC 會話
- 話題道具、傾聽、Tips、事件、觸摸、關係升級
- 分支選擇、occurrence、ChangeParam 修正
- 條件 id 除 -1 與 0 以外的可用性
- H 場景喘息或動作音；本表沒有把那些檔拿來填洞
