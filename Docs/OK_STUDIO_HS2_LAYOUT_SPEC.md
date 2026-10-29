# OK Studio 版面規格：參考 HS2Studio6（2026-10-29）

這份是 [OK Studio UI 與工作流強化計畫](OK_STUDIO_UI_WORKFLOW_PLAN.md) 第 12 節「整合路線」的版面依據。

## 資料來源

- 數據從 `ref/HS2Studio6` 的 `zzzstudio.unity` 在 Play 模式下輸出。
- 讀入 KKS 原作卡 KK_007604 後，依序打開 40 個面板狀態，每個狀態輸出整棵 UI 樹。
- 每個節點記錄：矩形、種類（Button、Toggle、Slider、InputField、Dropdown、ScrollRect、Text、Image）、文字、字級、文字顏色、底色。

## 檔案

| 檔案 | 內容 | 版控 |
|---|---|---|
| `Docs/StudioUI/hs2studio6_layout_spec.json` | 整理後的規格：主題統計，以及每一步新開的視窗、矩形與控制項 | 是 |
| `Saved/StudioUIRef/layout/*.json` | 每個狀態的完整 UI 樹（40 個） | 否，只在本機 |
| `Saved/StudioUIRef/hs2/L_*.png` | 與每個 layout 同一刻的截圖 | 否，只在本機 |

重新產生：

```powershell
.\Tools\lua\lua.exe Tools\studio_layout_spec.lua
```

原始 layout 的輸出方式見計畫第 11 節（`layout <名稱>` 指令）。

## 單位與座標

- 單位是 Unity 畫布的參考單位，原點在左上，參考解析度 1600×900。
- 輸出時的 Game 視窗是 2194×1010，畫布實際寬約 1769 單位、高約 814 單位。
- 右側面板（工具列、工作區、角色分組、聊天）都靠右上錨定。所以規格裡的 x 要換算成「離右邊的距離」：x 的參考寬度是 1769，不是 1600。
- 子控制項的矩形相對於所屬視窗的左上角。
- OK Studio 採用時，以 1600×900 為基準依視窗大小等比縮放（Slate DPI scale），錨定方式照原樣：左上群組靠左上，右側群組靠右上，變換列靠左下。

## 主題（計數取自 40 個狀態的所有節點）

| 用途 | 顏色 | 說明 |
|---|---|---|
| 面板底 | `#163D7A`（α 0.98） | 工作區、角色分組、變換列、詳情窗的底 |
| 按鈕底 | `#2860A8`（α 0.98） | 主要按鈕；相近色 `#2D5EA5`、`#2B5EA0` 用在清單列 |
| 選中／開啟 | `#5BC60C` | 只用於「選中」「開啟」；右上工具列開啟中是 `#00A514` |
| 清單區底 | `#101E30` | 工作區樹、目錄清單的捲動區 |
| 輸入框底 | `#4F4F4F` | 搜尋框 |
| F2 設定窗底 | `#1E1E23` | 統一設定用深灰，不用藍 |
| 主要文字 | `#FFFFFF` | |
| 次要文字 | `#E8E0CC` | 標籤與數值 |
| 說明文字 | `#9BA3A8` | 提示與說明行 |

- **字級**：11 最多（清單、按鈕）、10（工具按鈕、提示）、12（標題列、欄位名）、16（視窗大標題）。
- **常用尺寸**：清單列高 18；右上工具圖示 33×33；工作區工具圖示 28×28；搜尋框高 22；變換列 478×60。

## 外殼（空場景 `00_empty`）

| 區塊 | 矩形 x, y, w, h | 內容 |
|---|---|---|
| RootToolbar | 7, 7, 35, 177 | add、anim、sound、system 四個 35 寬主圖示，直排 |
| PoseLibraryButton／QuickSearchButton | 4, 420／458, 33, 33 | 左側「姿」「找」 |
| TopTool_Groups／TopTool_Chat | 1449／1489, 7, 33, 33 | 「分组」「聊天」文字鈕（開啟中為綠） |
| TopTool_0～5 | 1529～1729, 7, 33, 33，間距 40 | menu、work、move、color、option、camera |
| CharacterGroupPanel | 1285, 47, 240, 429 | 角色分組：標題、展開／收起全部、部位樹、「挂到所选部位」、搜尋 |
| TreePanel | 1529, 47, 240, 429 | 工作區：標題 21 高、展開／收起全部 117×18、樹（捲動區 234×285，列高 18）、Timeline 鈕 74×22＋搜尋框 151×22、工具圖示 28×28 兩排 |
| StudioOnlinePanel | 1529, 574, 240, 230 | 聊天（OK 不做） |
| TransformBar | 0, 754, 478, 60 | 位置／旋轉／縮放三列：標籤 30 寬、X/Y/Z 數值框 45×13、每列「重置」30 寬、右側複製／貼上／重置變換、主軸開關、三個模式圖示 |

## 各面板（每一步新開的主要視窗）

| 步驟 | 開法 | 主要視窗 | 矩形 x, y, w, h | 控制項數 | OK 對應 |
|---|---|---|---|---|---|
| 01 | add | AddPanel 子選單 | 47, 7, 80, 187 | 11 | U1-1 |
| 02 | add → KK角色 | KKCharacterLoaderBrowser | 142, 36, 620, 420 | 43 | U3-1 |
| 03 | 讀入角色 | 角色分組的部位樹、工作區節點 | 1288, 112, 224, 286 | 39 | U2-3、U2-4 |
| 04 | add → 项目 | StudioItemCatalog | 134, 7, 274, 234 | 25 | U3-2 |
| 05 | add → 光照 | StudioLightCatalog | 142, 36, 210, 166 | 9 | U8-4 |
| 06～08 | add → 地图／背景／前景框 | Map／Background／FrameCatalog | 142, 36, 480, 326 | 16～19 | U3-4、U8-2 |
| 10 | anim | AnimationMenu 子選單 | 47, 7, 80, 187 | 13 | U1-1 |
| 11 | anim → 状态 | CharacterStatePanel | 134, 7, 252, 326 | 79 | U1-4、U3-5 |
| 12 | anim → 骨骼 | CharacterKinematicPanel（FK 頁） | 134, 7, 320, 390 | 35 | U5-2、U5-3 |
| 13～19 | 骨骼 → IK／视线／颈部／表情／手势／姿势／权重 | KinPage*（頁內容） | 264, 7, 190～300, 240～390 | 7～21 | U5 |
| 20 | anim → 动画 | CharacterAnimePanel＋AnimationController | 135, 10, 446, 370／593, 40, 160, 332 | 45＋19 | U5、U6-6 |
| 21～26 | anim → 语音／服装／定制角色／定制饰品／HairShader | CharacterTool_* | 142, 36, 620, 440 | 17～57 | 不做（計畫第 9 節） |
| 23 | anim → 关节校正 | ManipulatePanel | 135, 47, 300, 429 | 13 | 不做 |
| 31～32 | sound → 背景音乐／外部读取 | SoundBgm／SoundExternalPanel | 142, 36, 440, 470 | 10～19 | 後續 |
| 40 | system | SystemMenu 子選單 | 47, 7, 80, 140 | 12 | U1-1 |
| 41、45 | system → 载入、點卡 | SceneCardCatalog（含預覽與覆盖／叠加／取消） | 394, 57, 980, 700 | 35／42 | U7-1、U7-2 |
| 42 | system → 备份恢复 | SceneRecoveryPanel | 574, 197, 620, 420 | 10 | U7-5 |
| 43 | system → 资源检查 | CharacterDependencyPanel | 574, 197, 620, 420 | 9 | U7-7 |
| 44 | system → 设置 | SettingsWindow（F2） | 416, 86, 760, 560 | 98 | U9-1 |
| 50 | 找 | QuickAccessSearch | 142, 36, 360, 590 | 12 | U3-3 |
| 51 | 姿 | PoseLibrary | 52, 172, 360, 368 | 11 | U5-5 |
| 52 | Timeline | TimelinePanel | 160, 458, 520, 260 | 24 | U6-1 |
| 53 | camera | 当前视口镜头（ManipulatePanel） | 135, 47, 300, 429 | 51 | U4-3、U4-4 |
| 54 | Mat. Editor | MaterialEditorPanel（全畫面） | 0, 0, 1769, 814 | 295 | U8-5 |

每個控制項的種類、文字、字級、相對矩形，見 JSON 的 `steps[].groups[].controls`。同一父節點底下重複的清單列只保留前 4 列，其餘只計數（`repeated_rows_omitted`）。

## 用法與限制

- 這份是版面與尺寸的依據，不是逐像素複製的目標。不照抄的部分見計畫第 9 節與第 3.8 節「反面教材」。
- 圖示（add、anim 等）與原作 UI 圖片不放進 repo。規格只記 sprite 名稱，OK Studio 用自製圖示，或在執行時從使用者本機的遊戲讀取。
- 文字取自 HS2Studio6 的簡體中文介面；OK Studio 照專案的文字表規則改用語意 id 與繁中翻譯，不直接抄字串。
