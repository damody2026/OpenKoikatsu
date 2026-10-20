# StreetSocial：Saved/ 隨時可清掉（2026-10-12 全面盤點）

原則：啟動、規則、測試需要的資料一律放在 Content（或其他有版本控制的位置）。`C:\portable\OpenKoikatsu\Saved` 整個被清掉後：
- 建置產物可以用腳本重建，或在啟動時自動重建；
- 存檔類資料就是重新開始；
- 祕密（金鑰）從使用者自己的備份補回。

## 1. 啟動與執行需要的資料：都在版本控制內

| 資料 | 位置 |
|---|---|
| 啟動設定、預設名單、材質 | `Content/IndependentResources/StreetSocialData/Launch/*.json` |
| 對白型錄、規則、邀請、人設、表情 | `StreetSocialData/RuntimeV5/`、`rules.json`、`invitations.json`、`free_talk_personas.json`、`RuntimeV2/expressions.json` |
| 不動產、建築、職位、經濟、中國稅率 | `StreetSocialData/property.json` |
| 女主個性資料 | `StreetSocialData/Profiles/<id>.json`（存檔沒有時，ok-mind 從這裡複製；這裡也沒有的女主才請 DeepSeek 生成） |
| 理財資料（股價、賽事） | `StreetSocialData/`（ok-mind 的 data_dir） |
| 角色卡 | 女主 `Content/CharacterCards/CrowdAPNG`，男主 `output/StreetSocialAPNG/Male01.png`（git 追蹤） |
| 動畫、材質 | `Content/IndependentResources/RuntimeAnimations`、`RuntimeMaterials` |

## 2. 建置產物：啟動器缺了會自動重建

`Tools/run_street_social_development.lua`（也就是 `StreetSocial.cmd`）會：
- 沒有 stage 時，執行 `rebuild_street_social_runtime.lua`；
- 沒有 patch 時，執行 `rebuild_street_social_patch.lua`；
- 材質收據不見時，由 Content 推導出來；
- 選角 selection 和本次執行用的型錄，每次啟動都從 Content 重新產生；
- `ok_mind.dll` 從 `Tools/ok-mind/target/release` 複製到 Saved。這份 DLL 用 `cargo build --release` 產生，不在 Saved 裡。

## 3. 存檔類（清掉就是新遊戲，不需要重建）

以下都放在存檔旁邊（`Saved/StreetSocial/`）：
- 存檔 `relationships.json`；
- 帳簿 `accounts/`、女主歷史 `history/`、手機 `phone.json`；
- ok-mind 的 `mind/`：記憶、對話上下文、理財狀態；女主資料則會從 Content 補回；
- 玩家自選名單 `heroines.json`（不存在時用 Content 的預設女主，最多十人）；
- 語音設定 `voice-settings.json`、麥克風設定 `asr-settings.json`（不存在時用預設值）；
- TTS 快取 `TTSCache/`。

## 4. 祕密：DeepSeek 金鑰

不能放進 Content 或 git。遊戲依序找（`OKStreetFreeTalkSettingsPath`）：
1. 環境變數 `OK_FREE_TALK_SETTINGS`。
2. `Saved/StreetSocial/deepseek.json`。
3. `%LOCALAPPDATA%/OpenKoikatsu/deepseek.json`。

只要找到的那份比使用者備份新，就會自動更新備份。

## 5. 測試資料：已從 Saved 搬進 Content

| 測試 | 原本讀 | 現在讀 |
|---|---|---|
| `OKStreetCheckIntegration`（RulesAndRealCatalogue） | `Saved/StreetSocial/Integration01/` | `StreetSocialData/TestFixtures/Integration01/` |
| `RulesAndRealCatalogue`、`SourceExpressions` | `Saved/StreetSocial/IntegrationCompletion02/` | `TestFixtures/IntegrationCompletion02/` |
| `OKStreetCheckRelationship` | `Saved/StreetSocial/CompletionData/catalogue.json`（找不到就整段跳過） | `TestFixtures/CompletionData/`；找不到算失敗 |
| `OpenKoikatsu.StreetSocial.LongRun` | `Saved/StreetSocial/mind/heroines` | `StreetSocialData/Profiles` |

說明見 `TestFixtures/README.md`。這些資料是 10/07 用當時的工具產生的；現在的工具和型錄都已經改版，重新產生不會逐位元組相同，所以直接納入版本控制。

遊戲內測試套組改用 repo 內的 `Tools/run_street_social_tests.ps1`，啟動參數全從 Content 讀。

## 6. 驗證紀錄

**2026-10-12 模擬清除**：把 Saved 的 `deepseek.json` 和 `mind/heroines` 改名移開後再啟動。
- 5 份女主資料從 Content 補回，逐位元組相同。
- ok-mind 用使用者的金鑰備份正常開啟。
- 帳簿測試 20/20 通過。

測試資料：Content 內的副本與 Saved 原檔逐位元組相同（selection 只改了 `dialogue_catalogue` 路徑）。

**2026-10-12 驗收**（build StreetFreeTalk121，Editor target 建在 FreeTalk01Host）

| 測試 | 結果 |
|---|---|
| C++ 自動化 `OpenKoikatsu.StreetSocial` | 20 項中 19 項通過 |
| 遊戲內套組 | Accounts 20/20、Company 14/14、HeroCompany 14/14、Stage 19/19 |

- 讀 Content 測試資料的 `RulesAndRealCatalogue`、`SourceExpressions`、`LongRun` 都通過。
- 唯一失敗的是 `MovementFacingAndPlayerPrompts`：女主轉向速度上限。10/11 已把速度改成每秒 145 度，但這個測試沒跟著更新，和 Saved 無關。
