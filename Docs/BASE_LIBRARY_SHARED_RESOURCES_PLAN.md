# 基礎庫共用資源與版控計畫（2026-10-20）

目標：版控只放「共用資源」，角色卡與卡片用的 mod 留在本機；任何人 clone 後都能用原卡＋zipmod 匯入角色，OK 卡只靠共用資源就能還原。

## 一、資源分類

| 分類 | 內容 | 進版控 | 錄進 OK 卡（`okLc`） |
|---|---|---|---|
| 共用資源（基礎庫） | 原作 `abdata` 轉出的模型、paired、Studio 動畫與道具清單；全部 uncensor 身體（`KKS_UncensorSelector`、`KK_UncensorSelector`、`BetterPenetration`）；全域 mod（卡片不指名、Sideloader 對所有角色套用，例如 `[minih] headmod`、覆蓋原版 `abdata` 路徑的 mod） | ✅ | ❌ |
| 記錄索引 | `sources.okidx`、`converted.tsv`（也記著不在版控的 mod 對應哪個模型） | ✅ | — |
| 卡片用的 mod | 卡片以 GUID 指名、不在共用資源裡的 zipmod（角色專屬或共用） | ❌ 只留本機 | ✅ |
| 角色卡 | 原作卡、OK 卡 | ❌ 只留本機 | — |

共用 mod 的清單在 `Tools/base_library_shared_mods.txt`；`Tools/gen_base_library_ignore.lua` 依 `converted.tsv` 的來源產生 `base_v1/models/.gitignore`：任一來源是原作 `abdata` 或白名單 mod 就收，其餘排除。

## 二、讀卡的三種情境

| 情境 | 資源來源 | 預設身體（卡片沒存身體 GUID 時） |
|---|---|---|
| ① 指定遊戲路徑 | 基礎庫＋遊戲 `mods/`（整個資料夾，Sideloader 規則）＋卡片資料夾 | 遊戲的 `BepInEx/config/com.deathweasel.bepinex.uncensorselector.cfg` |
| ② 只有卡片＋zipmod | 基礎庫＋卡片資料夾的 zipmod | 卡片旁或專案 `Config/` 的 `uncensorselector.cfg`，都沒有就用 UncensorSelector 外掛本身的預設值 |
| ③ OK 卡 | 基礎庫＋卡內 `okLc` | 卡內記下的實際 GUID |

## 三、實作

1. **版控內容**：盤點全域 mod（覆蓋原版 `abdata` 路徑的 bundle、只替換資源的 mod）全部收進白名單並補轉換；uncensor 身體放回版控；產生排除清單。
2. **修正 A**：基礎庫記著、但本機沒有模型檔的來源視同沒記錄，改由 `gamefs` 後備讀真正的 zipmod；`recorded_zip_names()` 與開卡快取 key 一併處理。
3. **修正 B**：有遊戲路徑時讀遊戲的 UncensorSelector 設定，後備改成整個 `mods/`（同遊戲模式的解析）；不在白名單的全域 mod 照樣套用，但列出來。
4. **OK 卡**：轉卡時記下實際的身體／陰莖／睪丸 GUID；`okLc` 只錄卡片用的 mod；用到不在白名單的全域 mod 時停止並列出。

## 四、驗收（乾淨專案 `E:\OpenKoikatsuClean`）

| # | 情境 | 通過條件 |
|---|---|---|
| T1 | 指定遊戲路徑 `kks-verify` | 遊戲模式＝乾淨基礎庫＋後備，MATCH |
| T2 | T1 遊戲模式 vs `D:\KoikatuSunshine\mesh_dump` | 可見 mesh 名稱、頂點數、`manualSkinnedWorldBounds` 在容許範圍內 |
| T3 | 卡片＋指名的全部 zipmod，無遊戲路徑 | 能開，結果等於「乾淨遊戲＋卡片 zipmod＋選定身體」 |
| T4 | 卡片＋只有專屬 zipmod | 能開；缺的 zipmod 明確列出 |
| T5 | T1／T3 轉出的 OK 卡封閉重開 | 轉換時＝重開＝T1，`gaps=0`、`game_reads=0`，`okLc` 不含白名單 mod |
| T6 | ill_male_01／02 | 找出既有 DIFFERENT 的原因 |
| T7 | UE 讀卡器／StreetSocial 打包版 | 各開一張 OK 卡 |
| T8 | 遊戲多放一個不在白名單的全域 mod 再轉卡 | 轉卡停止並列出該 mod |

## 五、發佈

重建現有 OK 卡（本機）→ 壓成單一 commit → 推到 `https://github.com/damody2026/OpenKoikatsu.git` 的 `master` → 從新 repo 重新 clone 跑 T1、T5 → C、E 的 `origin` 改成新 repo（舊歷史留本機 tag）→ 更新文件與記憶 → 恢復 agent。

## 六、驗收結果（2026-10-20，乾淨 clone `E:\OpenKoikatsuClean`，`OK_NOCACHE=1`）

測試卡：`D:\KoikatuSunshine\mesh_dump` 有 dump 的 7 張（伊莉雅、Jeanne、Yvonne、三月七、刻晴、ill_male_01／02）。

| # | 結果 |
|---|---|
| T1 指定遊戲路徑 `kks-verify` | 5 張女卡 MATCH；2 張預設男卡 DIFFERENT（完整基礎庫也一樣，見 T6） |
| T1b 指定遊戲路徑轉 OK 卡，封閉重開 vs 遊戲模式 | 5 張 MATCH，`gaps=0`、`game_reads=0` |
| T2 遊戲模式 vs mesh_dump | 啟用中的 mesh 名稱與頂點數：伊莉雅 73/74、Jeanne 25/26、Yvonne 34/36、三月七 40/40、ill_male_01 24/25；不一致的是 `o_dankon`／`o_dan_f`（`Default Female Penis = Random` 未解析）與 Yvonne 的一個頭髮配件（遊戲模式本身就和 dump 不同） |
| T3→T5 只有卡片＋指名的 zipmod 轉 OK 卡，封閉重開 vs 遊戲模式 | 5 張 MATCH，`gaps=0`、`game_reads=0` |
| T4 只有卡片＋專屬 zipmod | 能開，`OKCARDS ... absent=` 列出缺的 zipmod（例：Jeanne 缺 `[cbr95810] pubic hair`、`[minih] headmod`、`[moderchan]charamaker support`） |
| T6 預設男卡 | 已知差異：只差身體 `runtime_textures._Texture2` 的取樣資訊（skin shader 的 liquid 貼圖），幾何與貼圖相同；未修 |
| T7 UE 遊戲內 | 交給攻略 session 恢復後重新打包時一起驗證 |
| T8 不在白名單的全域 mod | 306 張樣本卡裡，卡片沒指名、依 ID 用到的 zipmod 只有兩個單張卡的服裝（`[fluctlight] Vladilena Milize`、`[hayashi] open front skirt`），照舊錄進那張 OK 卡並在轉卡時印出 `card uses N zipmod(s) it does not name`；沒有發現對所有角色生效、又不在白名單的 mod，所以不做「停止轉卡」 |

另外：306 張樣本卡（UserData 每 26 張取 1）在乾淨 clone 指定遊戲路徑全部轉成 OK 卡，0 失敗。

既有 OK 卡（StreetHeroines、CrowdAPNG）是在完整基礎庫上做的，`okLc` 不含第三方共用 mod 的資料；在有完整本機模型的 C、E 仍可開，要給乾淨 clone 用時以 `--oklib cards` 重轉。

2026-10-28 起 OK 卡一律嚴格開卡（只用卡內 `okLc`／`okLt` 與共用資源，見 [OK 基礎模型庫](OK_BASE_LIBRARY_PLAN.md)「OK 卡只靠自己開卡」）：上面這種靠本機第三方模型才開得了的卡，開卡會報錯，要用 `--oklib seal` 封卡，或從原作卡加 zipmod 資料夾重轉。

### 嚴格開卡上線時處理一個 checkout 的 OK 卡（E 已做；C 在下次 C⇔E 整合時照做）

1. 合併程式、重編 `ok-extract` 前，先盤點 checkout 裡的 OK 卡（含 `okLb` 區塊的 PNG）：`Content/CharacterCards/` 各資料夾、`output/` 下的 APNG、`UserData/Studio/scene/*_cards`、`UserData/Studio/import/*_cards`。
2. `Content/CharacterCards/` 直接用 E 已處理好的卡覆蓋（`CrowdAPNG` 是從 `D:\KoikatuSunshine\UserData\chara\female` 前 100 張、只用 `D:/KoikatuSunshine/mods` 重轉的新卡；`StreetHeroines`、`Demo` 是封好的同名卡，身分 `okId` 不變）。
3. 其他 OK 卡二選一：
   - 封卡（保留原本的卡，補上 `okLt` 並把第三方資料複製進 `okLc`）：`Tools\lua\lua.exe Tools\seal_ok_cards.lua --backup <備份資料夾> <卡或資料夾>...`；它先備份、在新程序裡嚴格檢查通過才換掉原檔。卡片用到的 mod 在基礎庫也沒有（例如以前只錄了 outfit 0 的卡）就封不了，改用重轉。
   - 重轉：`ok-extract D:/KoikatuSunshine/mods --oklib cards <基礎庫> <原作卡清單> <輸出資料夾>`（輸入也可以是 OK 卡本身，會用卡內的原作卡資料重轉並保留身分）。
   - `UserData/Studio/import/*_cards` 是開 KStudio 場景時自動重轉的暫存，不必處理；`scene/*_cards` 被 `.okscene` 引用，要封或重轉。
4. 每張卡用新程序跑 `ok-extract X:/NoGame --oklib strict-check <基礎庫> <卡>` 驗收，不要多個轉換程序同時跑時驗收（負載高時曾漏錄背景預取讀到的資料；封卡已會自動補齊重試）。
5. 刪掉開卡快取（`%LOCALAPPDATA%\OpenKoikatsu\oklib-cache`）與暫存的卡包（`%TEMP%\okcard-packs`），重開遊戲確認。

### 60 張樣本卡回歸（UserData 抽樣，指定遊戲路徑轉 OK 卡 → 封閉重開 → 對照遊戲模式）

| | 乾淨 clone | 完整基礎庫（對照組） |
|---|---|---|
| MATCH | 45 | 47 |
| DIFFERENT | 15 | 13 |

13 張兩邊都 DIFFERENT（原本就有的差異）。乾淨 clone 多出的 2 張（`Ltsuki_ブラックハート風林火山`、`朱膳寺 瑠々愛_Leona`）三個 digest 完全相同，只多一個 `textures_keep_base` 缺口：重開 OK 卡時要的這個查詢沒錄進 pack，改成現算，結果相同。

過程中修掉的退步：重試轉卡沿用快取（25 張）、Material Editor 的 shader 包沒收進共用資源（3 張）。

### 只有原作卡（無 zipmod、無遊戲）

7 張參考卡各自放在空資料夾、不給遊戲路徑轉 OK 卡：全部能開、0 失敗。5 張女卡用到第三方 mod，轉卡時列出缺的 zipmod（例：Jeanne 缺 `[Cetol]JeanneV1.2`、`[minih] headmod`、`[nashi]Nipples`、`[cbr95810] pubic hair` 等），那些部件不在，結果與遊戲模式不同。`ill_male_01` 沒用到 mod，結果與「有 zipmod／有遊戲」轉出的 OK 卡相同（與遊戲模式只差已知的 `_Texture2` 取樣資訊）。結論：只有原作卡時，卡片只用原作與共用資源才能完整還原；用到其他 mod 必須提供那些 zipmod。
