# PhysX 移植：階段 0 基準

量測日期：2026-10-15。對應計畫：[PHYSX_ENGINE_PORT_PLAN.md](PHYSX_ENGINE_PORT_PLAN.md)。
原始資料放在 [PhysXPortBaseline/](PhysXPortBaseline/)。

## 分支與快照

| 項目 | 值 |
|---|---|
| 本專案分支 | `physx-port`（從 `master` 的 `0c982d24` 開出，帶著當時未提交的工作樹） |
| 引擎分支 | `E:\PhysX\UE5.8` 的 `UE5.8-physX-5.11-ok`，從 `99c1286b8` 開出；開分支前已還原 16 個 Flex 測試 `.uasset` |
| OK_Demo 快照 | `E:\PhysX\Archive\OK_Demo-2026-10-15.zip`（78.6 MB，2,095 檔） |
| 快照 SHA256 | `7DAC7F627FA9AE2EB2435F4C1AD173D6E389CDD2D2B2372CB6461F48DFC3C572` |
| 快照清單 | 同目錄 `OK_Demo-2026-10-15.files.tsv`：每個檔案的大小與 SHA256，最後一行列出 6 個 junction 及其目標 |

快照**不含**：
- Binaries、Intermediate、DerivedDataCache、Saved、ThirdPartyDownloads、所有 `target*`
- 6 個 junction 指向的內容（那些本來就在本專案）

已抽樣解壓比對，雜湊一致。

## Rust（`cargo test --release`，D: 時期的程式碼）

| crate | 結果 |
|---|---|
| ok-rules | 149 通過，0 失敗 |
| ok-mind | 37 通過，0 失敗 |
| ok-extract（本專案） | 447 通過、**48 失敗**、255 忽略 |
| ok-extract（OK_Demo `ToolsLocal`） | **測試編譯失敗**：`src/mods.rs:112` 用了 `ok_is_dir`，但測試模組沒有 `use crate::gamefs::GamePath`。階段 3 合併時修正 |

本專案 ok-extract 的 48 個失敗，清單在 [PhysXPortBaseline/ok-extract-failed-tests.txt](PhysXPortBaseline/ok-extract-failed-tests.txt)。
- 多數是因為 `C:\portable\KoikatuSunshine` 已經沒有 `abdata`（只剩 `BepInEx`），測試讀不到 Unity bundle 或卡片檔。
- 其餘是依賴已移除 fixture 的舊斷言。

這符合「最後不需要 Unity 檔」的方向。階段 3 要做到兩件事：
1. 不新增失敗。
2. 依賴 Unity 檔的測試改用 OK 基礎庫，或標成 `#[ignore]` 並寫明原因。

## OK_Demo 在 E: 上（PhysX 5.11）

### 雙人穿模稽核（`Scripts\PairAudit.ps1`，Speed=4，每姿勢 10 秒）
- 99 / 99 個雙人姿勢都有量測結果（`result=measured`），沒有崩潰。
- maxPenetration：平均 0.0467，最大 0.1190。
- violations：平均 1,752.1。
- 逐姿勢資料：[PhysXPortBaseline/okdemo-pairaudit.tsv](PhysXPortBaseline/okdemo-pairaudit.tsv)。

### `-DemoSelfTest`
5 個動作都進入「正在播放」，最後出現 `SelfTest done`。挑到的動作是：
1. 自訂姿勢
2. 站立單手口交
3. 正常位
4. 牽手騎乘位
5. 沙發騎乘位

紀錄：[PhysXPortBaseline/okdemo-selftest.txt](PhysXPortBaseline/okdemo-selftest.txt)。

## StreetSocial 在 D: 上（`run_street_social_development.ps1 -SelfTest`）

- 執行檔：`Saved\StreetSocial\Build\Conquest20.stage\...\OpenKoikatsu.exe`
- 自測結果：`passed = True`，6 個角色全部 GPU ready，`gpu_failures = 0`，蒙皮矩陣誤差 0。
- 從選角到 GPU 就緒：14.9 秒。

| 指標（10,000 個樣本） | 平均 ms | P95 ms |
|---|---|---|
| frame | 8.64 | 13.06 |
| game thread | 3.93 | 6.76 |
| render thread | 8.57 | 10.73 |
| RHI thread | 2.37 | 3.00 |
| GPU | 5.01 | 7.23 |
| 載入期間 frame（1,821 個樣本） | 7.48 | 9.36 |

⚠ 量測時，使用者自己的 StreetSocial 視窗也開著（從 18:13 開始執行）。上面的數字含有這個干擾。
階段 8 重量時要在同樣條件下量，或兩邊都在沒有其他遊戲執行時重量。

## 群眾房間（CrowdRoom 10 / 100）：未量

- `CrowdRoom10.cmd` 與 `CrowdRoom100.cmd` 需要以下三樣，目前都不在：
  - `Saved\Crowd\CrowdRoom100\receipt.json`
  - 一個建好的 host
  - `Saved\Crowd\Display-StreetStyle01.json`
- 這三樣都可以用腳本重建：顯示補丁用 `Tools/cook_crowd_display_patch.ps1 -Tag StreetStyle01`，host 與 receipt 用 `Tools/stage_crowd_room100.ps1`。
  當時誤判成「沒有產生腳本」，已在 `Docs/STREET_ERRORS.md` 更正。
- 這次移植不改群眾流程，群眾效能以 StreetSocial `-SelfTest` 的 frame 數據比對。

## BpGeneratorUltimate

- 外掛帶了 `Source\`，`UnrealEditor.modules` 的 BuildId 和 D: 引擎相同，和 E: 不同。
  所以在 E: 上會從原始碼重新編譯，不會直接載入預編的 binary。
- 它相依的 33 個引擎外掛在 E: 都有；E: 只比 D: 多一個 `Flex`。
- 結論：預期能在 E: 編譯。在階段 2 建置 Editor target 時確認。
