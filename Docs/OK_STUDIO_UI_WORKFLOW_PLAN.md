# OK Studio 工作室 UI 與工作流強化計畫

日期：2026-10-29（F 組，`F:\OpenKoikatsu`，引擎 `F:\UE5.8`）。狀態：**計畫，尚未實作**。

這份計畫以現有 OK Studio（`SceneEditor.cmd` 開的遊戲內工作室）為主體，參考 `ref/HS2Studio6`（zzzstudio：HS2 StudioNEOV2 與 KKS CharaStudio 合併復刻的 Unity 工程）的版面與操作流程，整理要補的 UI 與工作流。畫面還原（光影、LUT、天空等）不在本計畫範圍內，那部分繼續照 [OK Studio 計畫](OK_STUDIO_PLAN.md) 進行。

研究方式：
- 程式與文件：逐檔讀 `Plugins/OKStudio/Source/`、`Source/OpenKoikatsu/OKStudioScene.*`、`Tools/run_scene_editor.lua`、`Docs/OK_STUDIO_*.md`；HS2Studio6 讀它的 `Docs/`（StudioUIReplica、StudioFunctionPlan、Studio/*、快捷鍵設置、Rules/UI开发），並以 grep 對照 `Assets/Modules/HS/Runtime/UI/Replica/`、`Assets/Modules/HS/Runtime/Studio/` 的 C# 程式。
- 實機截圖（2026-10-29）：
  - OK Studio：在 F 以 `SceneEditor.cmd` 開樣本場景，截取遊戲視窗。
  - HS2Studio6：安裝 Unity Hub 3.22.5 與 Unity 6000.3.22f1（`F:\Unity\Editors\6000.3.22f1`，與 `ProjectSettings/ProjectVersion.txt` 相同），打開工程後進 `Assets/Scenes/zzzstudio.unity` 的 Play 模式，逐一打開各面板拍 Game 畫面。
  - 精簡包原本缺 KK 資源包，KKS 原作卡讀不起來。合入使用者提供的 `ref/HS2Studio6-BasicResources-WithMeta-20261029.zip` 後，4 張 KKS 原作卡都能讀入（衣服、頭髮、配件齊全），要先選中角色才能開的面板（骨骼 FK/IK 各頁、動畫、狀態、語音、服裝、定制角色／飾品、HairShader）也都拍到了。
  - 截圖、對照圖與觀察整理在第 3.8 節，重拍方式見第 11 節。

下文出處縮寫：

| 縮寫 | 路徑 |
|---|---|
| `Game.cpp` | `Plugins/OKStudio/Source/OKStudio/Private/OKStudioGame.cpp`（4,210 行） |
| `Game.h` | `Plugins/OKStudio/Source/OKStudio/Public/OKStudioGame.h` |
| `Doc.h`／`Doc.cpp` | `Plugins/OKStudio/Source/OKStudio/Public/OKSceneDocument.h`、`Private/OKSceneDocument.cpp` |
| `Actor.cpp` | `Plugins/OKStudio/Source/OKStudio/Private/OKSceneActor.cpp` |
| `Editor.cpp` | `Plugins/OKStudio/Source/OKStudioEditor/Private/OKStudioEditor.cpp` |
| HS:UIR | `ref/HS2Studio6/Docs/StudioUIReplica.md` |
| HS:FP | `ref/HS2Studio6/Docs/StudioFunctionPlan.md` |
| HS:MENU／KIN／SAVE／MCP／IFIX | `ref/HS2Studio6/Docs/Studio/菜单行为.md`、`角色运动学与动画.md`、`保存恢复与资源检查.md`、`MCP.md`、`编辑器玩法与交互修复计划.md` |
| HS:KEY／UIRULE | `ref/HS2Studio6/Docs/快捷键设置.md`、`ref/HS2Studio6/Docs/Rules/UI开发.md` |
| HS:UI/ | `ref/HS2Studio6/Assets/Modules/HS/Runtime/UI/Replica/` |
| HS:ST/ | `ref/HS2Studio6/Assets/Modules/HS/Runtime/Studio/` |

---

## 1. 目標

0. **`SceneEditor.cmd` 的畫面接近 HS2Studio6 工作室**
   - 對象是 `SceneEditor.cmd` → `Tools/run_scene_editor.lua` → `AOKStudioGameMode` 打開的遊戲內工作室，也就是 `SOKStudioPanel` 這套 Slate UI。
   - 畫面照 [版面規格](OK_STUDIO_HS2_LAYOUT_SPEC.md) 重排：
     - 外殼：左上主選單、右上工具列、角色分組、工作區、左下變換列。
     - 主題色、字級，以及每個視窗的位置、尺寸與控制項排列。
   - 驗收方式：同一張 KKS 卡、同一個面板，兩邊並排截圖，並比對版面數據（第 5 節的容許誤差）。
   - 預設視窗由 1600×1000 改成 1600×900，與 HS2Studio6 的參考解析度相同；其他大小依比例縮放。
1. **能用滑鼠直覺操作**：在視窗點選物件、多選、樹狀拖放、右鍵選單、快捷鍵在任何焦點下都有效。
2. **加入東西先看得到再選**：角色卡、道具、地圖、姿勢、場景卡都有縮圖、搜尋、收藏／最近使用。
3. **擺姿勢順手**：視窗中直接顯示並點選 FK 骨點與 IK 目標、整體軸＋骨軸並存、左右鏡像、姿勢庫有預覽。
4. **時間軸可以真的拿來做動畫**：軌道圖、關鍵影格標記可拖、多物件同時打 key、相機與燈光也能 key、輸出有進度與取消。
5. **不怕丟資料**：未存提示、自動輪換備份、讀取報告、先預覽再「覆蓋／疊加」。
6. **所有操作走同一張指令表**：UI 按鈕、快捷鍵、主控台、AI（MCP）呼叫同一個指令，也因此能做 UI 層級的自動驗收。

非目標：
- 重寫成 UMG。
- 逐像素相同：目標是版面、配色、尺寸與操作流程接近，字型與圖示可以不同。
- 原版 CharaStudio 的版面：以 HS2Studio6 為準。
- 移植 HS2 角色。

---

## 2. 現況盤點（OK Studio）

### 2.1 架構

- 外掛 `Plugins/OKStudio`：Runtime 模組 `OKStudio`、Editor 模組 `OKStudioEditor`。遊戲模組依賴 `OKStudio`。
- 啟動：`SceneEditor.cmd` → `Tools/run_scene_editor.lua` → `UnrealEditor.exe -game /Engine/Maps/Entry?game=/Script/OKStudio.OKStudioGameMode`；`AOKStudioGameMode`（`OKStudioGameMode.cpp:16-56`）下一個 tick 讀 `-OKScene=` 後呼叫 `UOKStudioGame::Start`。另有讀卡畫面「進入工作室」（`Source/OpenKoikatsu/OKStudioScene.cpp:1563-1590`）與主控台 `OKStudio.Play`。
- UI 是**純 Slate**：`SOKStudioPanel`（`Game.cpp:183-1238`）以 `GameViewport->AddViewportWidgetContent(..., 10)` 掛上（`Game.cpp:1339`），視窗互動在 `SOKStudioView`（`Game.cpp:102-180`）。
- 核心物件：
  - `UOKStudioGame`（`Game.h:19`）：工作室單例、繞行相機、**單一** `SelectedId`、Gizmo、時間軸播放、Undo、截圖與影片輸出、自測。
  - `UOKSceneDocument`：場景樹、JSON 存讀、`EvaluateTimeline`；已有 `AddObject/RemoveObject/Reparent/Duplicate`（`Doc.cpp:53` 起）。所有修改經 `NotifyChanged` 進 Undo 與同步。
  - `AOKSceneActor`：文件轉世界，分批建置；`ComponentOf`／`PreviewOf`（`Actor.cpp:866-876`）可由 Id 反查元件。
  - `FOKSceneCard`：1280×720 四格 APNG 場景卡，`okSc` chunk 內含 JSON 與 OK 角色卡。
  - `FOKStudioCatalog`：Items、Maps、FkBones（355 根）、Anims、Effects、AccessoryPoints、Filters。
- `.okscene` 欄位（`Doc.cpp:172-255`、`Doc.h:95-705`）：物件樹（Folder／Character／Item／Light／Camera／Effect、Parent、AttachBone）、角色狀態（卡、服裝、動畫、表情、視線、手、FK／IK、衣服狀態、汁、飾品顯示）、道具（Studio 目錄編號、顏色、花紋、MaterialEdits、FK）、燈光、相機（只有 Fov）、`FOKScenePost`（約 35 欄）、時間軸 `tracks[]`（`FOKSceneKey` 有 Transform、Value、Fk、角色狀態、Ease 五種、`bSmoothPath`）。
- 另有舊 UMG 工作室 `UOKStudioWidget`／`UOKStudioTimeline`（`Source/OpenKoikatsu/OKStudioScene.h:50-86`），掛在讀卡畫面「動作時間軸」按鈕（`OKGalleryWidget.cpp:391`）；[OK Studio 計畫](OK_STUDIO_PLAN.md) S2 記載要在 S7 併入，至今未併。
- 編輯器版 `FOKStudioEditor`（`Editor.cpp:275-325`）有 Outliner（真的 `STreeView`，可拖放改父，`Editor.cpp:332-345`）、Viewport、Details（反射）、Pose 分頁，但沒有時間軸、後製、姿勢檔。

### 2.2 遊戲內 UI 現況

版面：全畫面視窗＋左側固定 300 px 面板（標題、按鈕 WrapBox、場景檔區、物件列表、時間軸、狀態列）＋右側固定 330 px 屬性面板（`Game.cpp:192-265`）。沒有選單列、分頁、浮窗、分隔線、圖示。左側 18 顆平鋪按鈕（`Game.cpp:202-224`）：新場景、場景檔、+資料夾、+角色、+道具、+燈光、+相機、+特效、複製、移到根層、刪除、復原、重做、移動、旋轉、縮放、離開、用此相機。

| 項目 | 狀態 | 證據與缺口 |
|---|---|---|
| 物件樹 | 部分 | `SListView` 加縮排模擬（`RefreshRows :431`、`MakeRow :448`），`ESelectionMode::Single`（`:244`）；不能摺疊、拖放、右鍵、搜尋、改名 |
| 視窗點選物件 | 沒有 | 左鍵沒點到把手就繞行相機（`:116-120`） |
| 多選 | 沒有 | `SelectedId` 是單一 int |
| 改父／掛骨 | 部分 | 只有「移到根層」與「新物件放進所選資料夾」（`Add :1458`）；`AttachBone` 只能靠匯入或 Editor Details |
| 複製貼上 | 部分 | 只有「複製」整棵子樹；沒有剪貼簿、複製屬性或姿勢 |
| 加角色 | 部分 | 「+角色」固定用 `Cards[0]`（`:205`），再於屬性區 `SComboBox` 換卡（`:568`）；只有檔名，來源只有 `Content/CharacterCards`（`AvailableCards :1244`） |
| 道具目錄 | 部分 | 先加空道具，再選大分類→中分類→清單（`ItemBrowser :1121-1167`）；純文字，沒有縮圖、搜尋、收藏 |
| 特效 | 部分 | 「特效」按鈕循環切換（`:731`），沒有清單 |
| 動畫 | 已有 | Studio／H、群組、分類、清單（`AnimBrowser :865-918`）；沒有搜尋，`Character.Time` 沒有 UI |
| 表情／手 | 已有 | 眉、眼、嘴、手都是數字 SpinBox（`:576-618`），沒有名稱或預覽 |
| FK | 陽春 | 下拉選骨、XYZ、取目前姿勢、清除（`FkProperties :772-837`）；沒有骨骼樹、部位開關、鏡像 |
| IK | 陽春 | 13 個效果器下拉＋一個把手（`IkProperties :940-970`）；視窗內不顯示全部目標 |
| 姿勢檔 | 已有 | 讀／存 KK `.dat`，檔名自動用時間（`:814-836`）；沒有預覽、命名、刪除 |
| 道具 FK | 已有 | 每根骨三個 SpinBox（`ItemFkProperties :1204-1228`），骨頭多時很長 |
| Gizmo | 已有 | 自繪，世界軸；骨骼模式用骨骼 local 軸（`GuideHandles :1551-1615`、`ApplyGuideDrag :1638-1699`）；沒有 local／world 切換、平面把手、吸附 |
| 相機 | 部分 | 繞行／平移／縮放（`Orbit/Pan/Zoom :1711-1730`）；相機物件只有 FOV、「用此相機」；沒有聚焦所選、存視角、從視口建立相機 |
| 燈光 | 部分 | 種類循環、強度、範圍、角度、RGB SpinBox、陰影（`:750-761`）；`bCharactersOnly` 沒有 UI |
| 後製 | 部分 | 只在沒選物件時出現（`SceneProperties :1022-1118`）；ColorFilter、Background／Frame、BackgroundColor、RampG、FogColor、`MapTransform` 沒有 UI |
| 顏色 | 部分 | `#RRGGBB` 文字框（`:630`、`:660`、`:740`），沒有色盤 |
| 材質編輯 | 部分 | 原始屬性名＋值文字輸入（`:675-719`） |
| 存讀檔 | 部分 | 名稱框＋存檔（一律存場景卡，`SaveNamed :394`）、清單點一下就讀（`MakeFilesBox :346`）、KStudio 匯入要手打路徑；沒有縮圖、另存、刪除、未存提示、自動備份 |
| 時間軸 | 部分 | 單一 `SSlider`、設／刪關鍵幀、平滑、循環、播放、長度只能「+5 秒」（`MakeTimeline :270-343`）；沒有軌道、標記、拖 key、逐幀 |
| 輸出 | 已有 | 截圖 1080p／4K／透明（`:325-327`）、PNG 序列、mp4、`.okanim`（`:328-341`）；沒有進度條與取消、解析度固定 |
| 快捷鍵 | 很少 | 只有 Ctrl+Z／Y、W／E／R（`:138-151`），且要 `SOKStudioView` 有焦點 |
| Undo／Redo | 已有 | 整份 JSON 快照，停 0.4 秒合併一步，最多 100 步或約 256 MB（`:1346-1409`）；選取與相機不在歷史內 |
| Story | 已有 | 只能用命令列 `-OKStory`／`OKStory.Play`，面板沒有入口 |

### 2.3 文件記錄的缺口

- [OK Studio 計畫](OK_STUDIO_PLAN.md) S2～S7 仍「進行中」；舊「動作時間軸」待 S7 併入（`:17`）；MRQ 因 morph 斷言擱置（R3，使用者 2026-10-18 決定）；目標 120 fps 未達（`:574-621`，Slate＋viewport 約 4 ms）。
- 文件與 `Docs/ERROR_LOG.md` 的 Studio 相關列全是工程或畫面還原問題，**沒有 UI 操作性的紀錄**——代表目前沒人以「使用者工作流」的角度驗收過 UI。
- 驗收手段（`OKStudio.GameSelfTest` 24 步、`Tools/verify_ok_studio.lua`、3 個 UE Automation）都直接呼叫 API，**從未操作 Slate widget**；`Tools/ok-game-mcp` 也不支援 SceneEditor。

### 2.4 結構問題

`Game.cpp` 一個檔混了：Slate 視窗與面板（約 1,140 行，`:102-1238`）、`UOKStudioGame` 本體（`:1240-1938`）、七個 `Run*Test`（約 2,000 行，`:1940-3985`）、主控台指令（`:3990-`）。面板類別宣告在 cpp 裡，`Game.h:12` 只有前向宣告。任何 UI 改動都會跟其他 session 在這個檔撞在一起，必須先拆。

---

## 3. HS2Studio6 可借鏡的設計

以下只列與 OK Studio 對應、且他們有實作或驗證證據的部分。

### 3.1 版面與視窗生命週期

- 左上四個主選單：**添加**（角色、項目、地圖、光照、背景、前景框）、**動畫**（狀態、骨骼、動畫、語音、服裝、關節校正…）、**音效**、**系統**（設定、保存、載入、備份恢復、資源檢查、初始化、結束）（HS:UIR#實現狀態；`HS:UI/StudioReplicaUI.cs:895-945`）。
- 右上工具列 `menu／work／move／color／option／camera`，綠＝開、藍＝關（`HS:UI/StudioReplicaUI.cs:370-390`）。
- 右上工作區 240 寬，列高 18、縮排 12，搜尋框在樹下；底部 9 個工具鈕：解除父級、設父級、刪除、複製變換、複製子樹、新資料夾、新相機、新路線、材質編輯器（`HS:UI/StudioReplicaUI.cs:731-755`）。
- **詳情窗互斥**：切到別的條目先關舊窗；再點一次目前條目就關；選中非角色時角色詳情自動關（HS:MENU#選單切換與窗口生命週期）。**獨立浮窗**（Timeline、動畫控制器、道具搜尋、材質編輯器）不受互斥管。
- 通用窗口：標題可拖、點任何可見處置前、視口變窄時等比縮小（HS:UIRULE#遮擋時點擊置前）。

### 3.2 物件與目錄

- 角色載入器：資源管理器式路徑列、多根目錄樹、三欄縮圖、底部搜尋與翻頁、「定位當前」、背景分幀產生縮圖、讀失敗保留原角色並提示「部分恢復」（HS:UIR#角色入口與窗口切換）。
- 項目目錄三欄（分組→分類→條目），懸停約 180 ms 顯示完整名稱與預覽；沒縮圖時用獨立相機即時算 256 px 預覽，最多快取 12 張。
- **瀏覽與套用分開**：地圖要按「讀取」、圖像要按「應用」才生效（HS:UIR#產品區分）。
- 道具快搜（複刻 QuickAccessBox，`HS:UI/StudioReplicaUI.QuickSearch.cs`）：空格分詞 AND、`-詞` 排除；篩選／收藏／隱藏／全部；空搜尋顯示收藏與最近使用；左鍵加入、**中鍵加到所選物件底下**、右鍵收藏／隱藏。
- 樹：雙擊就地改名（Enter／失焦提交、Esc 取消、空白或同名不產生歷史）；Shift 選區間；設父級以最後選的為父；解除父級保留世界變換並拒絕循環；刪除是停用保留實例以便撤銷（HS:UIR#混合工作區命令與存檔）。
- **拖放掛載**：把道具拖到角色部位分組（頭髮、頭、臉、頸、胸背、腰、四肢、手、下腹）或樹上的物件；綠可放、紅拒絕；停 450 ms 自動展開、靠邊自動捲動（HS:UIR#角色掛點與道具跟隨；`HS:ST/StudioCharacterAttachments.cs`）。

### 3.3 姿勢

- 新加入人物預設開 IK；FK 預設開胸、身體、左右手、裙子，關頭髮、頸部；切分頁只瀏覽不切模式（HS:KIN#所選角色操縱）。
- FK 7 組白點加層級連線；IK 13 個目標，紅＝末端、藍＝肘膝。操作軸固定**世界尺寸**（0.1–1，預設 0.4），控制點固定**螢幕尺寸**（FK 約 24 px、IK 約 36 px）；控制點點選優先於遮住它的軸。
- **整體軸＋骨軸並存**，整體軸樞軸取匯入時雙腳中點並固定；左下「主軸：顯示／隱藏」（`HS:UI/StudioReplicaUI.cs:505`）。選到只能移動的 IK 點時暫時改用移動軸。
- 「數值」展開骨骼清單可輸入；「復位」回動畫基態；「跨控制複製」把目前解算姿勢寫進另一模式；權重子頁（`HS:UI/StudioReplicaUI.KinematicWeights.cs`）。
- **FK／IK 有獨立撤銷歷史**，撤銷時不動表情、視線、動畫，也保留目前視口鏡頭（HS:KIN#2026-10-24 撤銷範圍修正）。
- 鏡像他們尚未做（HS:FP#近期實施批次 5），是我們可以超前的地方。

### 3.4 動畫與時間軸

- 動作窗三欄（組、分類、動作），搜尋同時比對中文名與原名；**動畫控制器做成獨立浮窗**（速度、動作變化、輔助 1/2、循環、播放／暫停／停止），關列表後仍留著並記住位置（HS:KIN#動畫窗格與漢化）。
- Timeline（`HS:UI/StudioReplicaUI.Timeline.cs`）：播放、停止、回 0、拖進度、時長、循環、清空、打 key、刪 key、打姿勢幀、單骨加／刪幀；目標以**產品＋穩定 ID** 定位，改名或合併場景後仍對得上。
- 他們自己的已知坑（HS:FP#完善程度）：關鍵幀不在撤銷內、`Seek` 不取樣、**Timeline 在 Update 寫骨骼後 `StudioKinematics.LateUpdate` 又解算覆蓋**、沒有表情／鏡頭軌道、沒有影片輸出。我們的時間軸要從一開始就驅動文件／FK 狀態，而不是直接寫骨骼。

### 3.5 相機、燈光、輸出

- 相機面板：FOV、透視／正交、近遠面；使用此鏡頭、下一鏡頭、聚焦所選、**從視口建立鏡頭**；**十個書籤**（含聚焦距離），書籤操作進撤銷歷史（HS:UIR#燈光與鏡頭參數）。
- 導航（HS:KEY#工作室鏡頭導航）：可切原版軌道模式；**拖動必須從視口開始**；Ctrl+Space 回初始鏡頭。
- 燈光：作用對象（全部／角色／地圖）、內外錐角、陰影軟硬；只有選中的燈在視口顯示標記，輸出時隱藏。
- 截圖導出（F12）：720p／1080p／4K／自訂、透明背景、背景圖、前景框；自動排除 UI 與軸；不覆蓋既有檔；「本次目錄」（HS:UIR#工作室截圖導出）。

### 3.6 存讀與可靠性

- 場景卡窗：目錄樹＋4 欄×3 列縮圖；**點卡片只預覽，再選「覆蓋／疊加／取消」**；疊加保留現有物件並合併時間軸、不動鏡頭與環境（HS:UIR#場景卡瀏覽、覆蓋與疊加）。
- 單一物件讀取失敗只跳過，全部失敗才回滾；讀完跳「讀取報告」可複製。
- 存檔先暫存、預檢再替換，原子寫入。
- 自動備份預設開，每 5 分鐘、保留 10 份；載入中、拖動中、播放中延後；內容沒變跳過（HS:SAVE#自動備份與恢復入口）。
- **未保存提示**：初始化、讀取、恢復備份、結束時問「保存並繼續／不保存繼續／取消」（HS:SAVE#未保存提示）。
- 資源檢查：列出所選角色缺件、空材質、不支援 Shader，可「補齊後重試」（HS:SAVE#所選角色資源檢查）。

### 3.7 快捷鍵、規範、MCP

- 快捷鍵（HS:KEY；`Shared/Runtime/StudioShortcutSettings.cs:8`）：Z 隱藏 UI、W／E／R、L 世界／局部、F 聚焦、Delete、Ctrl+C／V、Ctrl+Z／Y、F2 設定、F5 畫面、F12 截圖、Ctrl+Space、Esc。**全部可重綁、有衝突檢查；文字框有焦點時停用；提示文字依綁定動態產生。**
- Undo：`MaxHistoryEntries = 100`（`HS:ST/StudioItemRuntime.cs:20`）；一次滑桿拖動只記一筆；載入場景後清空。
- UI 規範（HS:UIRULE）：只有「選中」「開啟」用綠色；所有清單同時支援滾輪與可拖捲軸；共用一個調色盤（再點同色塊關閉）；有「當前選中項」的清單頂部要有「定位當前」；說明用 `?` 圖示懸停顯示、點擊固定。
- MCP（HS:MCP#第一階段工具；`HS:ST/StudioMcpBridge.cs`、`StudioMcpCommands.cs`）：本機 TCP，14 個工具（status、scene_inspect、catalog_search、object_spawn、object_transform、object_reparent、object_delete、character_rig、character_pose、camera_set、capture、scene_save、undo、redo）；物件用穩定 ID；**AI 與手動共用同一份歷史**；使用者拖動中、載入中、播放中拒絕 AI 修改；存檔不准覆蓋既有檔。

### 3.8 實機截圖觀察（2026-10-29）

截圖只留本機，不進版控（`Saved/` 與 `ref/` 都已在 .gitignore）：

| 資料夾 | 內容 |
|---|---|
| `Saved/StudioUIRef/ok/` | OK Studio 現況，3 張 |
| `Saved/StudioUIRef/hs2/` | HS2Studio6 各面板，`00`～`67` |
| `Saved/StudioUIRef/compare/` | 兩邊並排的對照圖，10 張 |

**對照圖**

| 對照圖 | OK Studio 現況 | HS2Studio6 | 對應階段 |
|---|---|---|---|
| `compare/01_layout_scene_loaded.png` | 左右兩條固定側欄，左欄塞滿按鈕、物件列表、時間軸與輸出鈕，視窗區被擠窄 | 四個主圖示固定在左上；右上依序是工具列、角色分組欄、工作區；左下是變換列；中間視口留空 | U1 |
| `compare/02_add_character.png` | 選中角色後，在屬性面板的下拉選單裡只看得到檔名 | 角色載入器：女／男、刷新、目錄、路徑列、根目錄樹、縮圖格、搜尋與翻頁、大預覽與角色名、「添加默认角色／读取／替换选中角色」 | U3-1 |
| `compare/03_scene_files.png` | 場景檔是純文字清單，點一下就讀 | 場景卡窗有目錄樹（全部／HS／KK、包含子文件夹、選擇文件夾、搜尋）；點縮圖彈出大預覽，再選「覆盖／叠加／取消」 | U7-1 |
| `compare/04_timeline.png` | 左下一條滑桿，加一排平鋪按鈕 | 獨立 Timeline 浮窗：播放／停止／回 0s、時長、循環、清空軌道、打姿勢幀、打關鍵幀／刪關鍵幀、骨骼搜尋、打骨骼幀／刪骨骼幀、軌道列表 | U6 |
| `compare/05_item_catalog.png` | 先加空道具，再到屬性面板選分類 | 浮窗：HS／KK 頁籤、搜尋框、分組→分類→條目三欄、底部顯示「HS: 1 个项目」計數 | U3-2 |
| `compare/06_settings_post.png` | 沒選物件時，右欄才顯示後製參數 | F2 設定：左邊分類（游戏选项、快捷键、插件设置、键位指南、资源目录），頂部搜尋；每列有 `?` 說明與「应用／默认」；底部提示「滑条即时保存，文本点击应用」 | U8-2、U9-1 |
| `compare/07_pose_fk.png` | FK 在一長串屬性面板中段：下拉選骨＋XYZ | 「骨骼」窗左側子頁（FK、IK、视线、颈部、表情、手势、姿势、权重）；FK 頁有功能開關、「动画位置姿态／IK反向控制姿势」、部位開關（头发、颈部、胸部、身体、右手、左手、裙子）各帶「初始化／复位」、操作軸 0.4、控制點 1、「数值」 | U5 |
| `compare/08_animation.png` | 動畫在同一屬性面板裡，有 Studio／H 切換與群組下拉 | 動畫窗：全部／女／男／共用頁籤，分组→分类→动作三欄，搜尋＋重置，「34 个动作」計數；旁邊是獨立的「动画控制器」浮窗（速度、动作变化、辅助 1/2、循环、播放、停止） | U5、U6-6 |
| `compare/09_character_state.png` | 9 部位衣服各一顆循環鈕 | 狀態窗：「全穿／半脱／全裸」一鍵，加上每部位「穿／半／脱」三態按鈕、飾品逐槽「显示／隐藏」 | U3-5 |
| `compare/10_expression.png` | 眉、眼、嘴是數字 SpinBox | 表情頁：眉毛、眼睛、嘴巴顯示目前編號，各有「上一个／下一个」；眼睛開合、嘴巴開合滑桿；眨眼開關 | U5-7 |

**其他觀察**

- **讀取報告與佔位列**（`hs2/23_scene_loaded_later.png`）
  - 覆蓋讀入後跳出「场景卡读取报告」，逐條列出無法解析的欄位與找不到的 Studio Mod（含 group/category/no），附「复制问题清单」按鈕。
  - 缺件的道具不會消失，而是以「缺失 Studio Mod [19870101/101/3]」佔位列留在工作區；之後補齊資源，再存檔時不會丟掉。這點值得照做，已併入 U7-6。
- **變換列固定在左下**（`hs2/00_start.png`）
  - 位置、旋轉、縮放各一列 XYZ 數值，每列一個「重置」。
  - 旁邊有「复制变换／粘贴变换／重置变换」、「主轴：显示」和移動／旋轉／縮放三個模式圖示。
  - 滑鼠停在圖示上，提示會連快捷鍵一起顯示（例：「旋转 · E；世界/局部轴 · L」）。
  - 已併入 U4-2、U9-2。
- **添加光源**（`hs2/06_light_add.png`）：小窗列出方向光、点光源、聚光灯、区域光四顆按鈕，加一行「当前灯光：1」；連續添加時窗不關。
- **地圖與背景**（`hs2/07_map.png`、`hs2/08_background.png`）：左邊清單，右邊預覽，下方「读取地图」「设置」「应用背景」。瀏覽和套用是分開的。
- **相機**（`hs2/19_camera.png`）
  - 「当前视口镜头」窗：視口操作（原版轨道／自由飞行）、投影（透视／正交）、視野角度、正交大小、近／遠裁剪面、背景顏色、HDR、MSAA，加上「使用这个镜头」「切换到下一镜头」。
  - 標題列有「截图导出」入口。
- **系統選單**（`hs2/10_system_menu.png`）：设置、保存、载入、备份恢复、资源检查、初始化、返回标题、结束。
  - 備份恢復（`hs2/12_backup.png`）：「立即备份／刷新列表／备份设置」。
  - 資源檢查（`hs2/13_resource_check.png`）：「补齐后重试／资源目录／复制报告」；沒選角色時顯示「请先选择角色」。
- **快搜與姿勢庫**（`hs2/16_quick_search.png`、`hs2/17_pose_library.png`）
  - 都是左側小圖示「找／姿」開的獨立浮窗，可與其他窗並存。
  - 快搜有 HS／KK 篩選、「筛选列表」下拉、「刷新目录」，底部提示「输入关键词搜索；空白时显示收藏与最近使用」。
  - 姿勢庫有搜尋、刷新、上一页／下一页、計數。
- **材質編輯器**（`hs2/20_material_editor.png`）：模型、材質、屬性三欄，屬性可篩選，底部有匯出路徑與「应用／默认」。
- **角色的詳情窗按功能拆頁**（`hs2/52`～`67`）
  - 「anim」選單：状态、骨骼、动画、语音、服装、关节校正、定制角色、定制饰品、HairShader。每項開一個詳情窗，標題列都顯示「当前角色：<名字>」。
  - OK Studio 現在把角色的所有屬性放在同一條長捲動面板（`ok/02_character_selected.png`），要捲很久才找得到 FK 或表情。已併入 U1-3：角色詳情改成分頁窗。
- **骨骼各子頁**
  - IK（`hs2/61_bones_ik.png`）：全部、身體、右手、左手、右腳、左腳開關，各帶「复位」。
  - 視線（`hs2/62_bones_look.png`）：相机、正面、背离、固定、指定；指定位置（角色局部座標）XYZ；「取镜头位置」「恢复看镜头」；下方有一行說明文字。
  - 頸部（`hs2/63_bones_neck.png`）：相机、姿势、正面、背离、固定，加「跟随强度」滑桿。
  - 手勢（`hs2/65_bones_hand.png`）：左右手各有上一个、下一个、复位，顯示目前 ptn 與可選總數。
  - 姿勢（`hs2/66_bones_pose.png`）：大字「0 / 63」加上一个、下一个、「复位默认 T-pose」。
  - 權重（`hs2/67_bones_weight.png`）：整體權重、目前目標（上一个目标／下一个目标）、目標位置與旋轉權重、「复位整体与当前目标权重」。
- **讀卡後的回饋**
  - 角色一讀入，視窗立刻顯示 IK 控制點（紅＝末端、藍＝肘膝），工作區與角色分組欄同步出現該角色（13 個分組、54 個部位）。
  - 載入器下方訊息分兩種：完整時顯示「角色已添加」；有缺件時顯示「部分资源未恢复，请查看所选角色资源检查」。已併入 U3-1、U7-7。
- **其他角色窗**
  - 語音（`hs2/55_voice.png`）：左邊原版語音分組，中間性格清單，右邊播放佇列（加入选中语音、顺序到尾停止、播放队列、移除选中、上移／下移、清空）。
  - 服裝（`hs2/56_clothes.png`）：服裝卡瀏覽，「编辑当前角色材质」「服装：开启／关闭」「饰品：开启／关闭」。
  - 定制角色（`hs2/58_custom_char.png`）：左邊部位（上衣～接发），右邊部件清單分頁（例：122 项）。
  - 定制飾品（`hs2/59_custom_acc.png`）：飾品槽切換、校正節點、位置／旋轉／縮放數值、掛點清單（a_n_*）。
  - HairShader（`hs2/60_hairshader.png`）：後髮、前髮、鬢髮、接髮的部件清單，加「编辑头发 Shader / 材质」。
  - 這些屬於在 Studio 裡直接換裝與捏人，不在本計畫範圍（見第 9 節）。
- **反面教材**
  - 以 Game 視窗 1.8 倍縮放顯示時，Timeline 浮窗的「骨骼」那一列文字與按鈕互相重疊（`hs2/18_timeline.png`）。OK Studio 的 UI 驗收要在兩種以上的 DPI／視窗大小截圖檢查（見 U1 驗收）。
  - 聊天框與角色分組欄預設就開著，佔掉右側大片空間；OK Studio 不做聊天（第 9 節），工作區相關的欄預設只開一個。

---

## 4. 差距對照

| 領域 | OK Studio 現況 | HS2Studio6 參考 | 本計畫 |
|---|---|---|---|
| 版面 | 兩條固定側欄、平鋪按鈕 | 主選單＋工具列＋互斥詳情窗＋獨立浮窗 | U1 |
| 指令與快捷鍵 | 5 個鍵、依賴視窗焦點 | 註冊表、可重綁、衝突檢查 | U0、U9 |
| 視窗點選／多選 | 沒有 | 視口點選、Shift 加選 | U2 |
| 物件樹 | 縮排清單 | 改名、拖放、掛載、搜尋 | U2 |
| 角色／道具目錄 | 純文字、下拉 | 縮圖、搜尋、收藏、懸停預覽、中鍵加為子物件 | U3 |
| Gizmo／相機 | 世界軸、無聚焦、無書籤 | L 切軸、F 聚焦、10 書籤、從視口建鏡頭 | U4 |
| 姿勢 | 下拉選骨／效果器 | 視窗骨點、雙軸、跨控制複製、姿勢庫 | U5 |
| 時間軸 | 單一滑桿 | 軌道與 key 操作（但有求值順序坑） | U6 |
| 存讀可靠性 | 點了就讀、無提示 | 預覽→覆蓋／疊加、報告、自動備份、未存提示 | U7 |
| 外觀參數 | `#RRGGBB`、後製欄位缺 UI | 共用調色盤、F5 分類設定與預設 | U8 |
| AI／自動驗收 | API 自測、MCP 不支援 | 14 工具、共用歷史、忙碌時拒絕 | U0、U9 |

---

## 5. 設計原則

1. **文件是唯一真相**：UI、快捷鍵、時間軸、MCP 都只改 `UOKSceneDocument`，再由 `NotifyChanged` → Undo → `AOKSceneActor::Sync`。不在 UI 直接動 Actor 或骨骼（避開 HS2Studio6 時間軸被運動學覆蓋的坑）。
2. **一張指令表**：每個操作有 `Id`、顯示名、預設快捷鍵、`CanExecute`、`Execute`；按鈕、選單、快捷鍵、主控台 `OKStudio.Cmd <id> [json]`、MCP 都走它。這同時解決快捷鍵焦點問題與 UI 驗收問題。
3. **維持 Slate**：遊戲內 UI 已是 Slate（`Game.cpp:183`），繼續用；不引入 UMG。Editor 版 `FOKStudioEditor` 不追求功能對等，只保持能開檔、存檔、反射編輯。
4. **顏色規則**：沿用 HS2Studio6 的「只有選中／開啟用綠色」；一次性動作不用強調色。所有清單可滾輪與拖捲軸；有當前項的清單要有「定位當前」。
5. **效能預算**：UI 改動不得讓 `Tools/run_ok_studio_perf.lua` 量到的幀時間變差超過 0.5 ms；縮圖一律背景解碼、分幀建立。
6. **版面以規格為準**
   - 位置、尺寸、配色、字級都取自 `Docs/StudioUI/hs2studio6_layout_spec.json`，不憑截圖目測。
   - 容許誤差：視窗矩形 ±4 參考單位，控制項 ±2，顏色與字級要相同。
   - 規格沒有涵蓋的 OK 專有功能（Story、`.okanim` 輸出、KStudio 匯入、後製細項），沿用同一套主題，放在最接近的選單或視窗裡。
7. **不破壞格式**：`.okscene` 只加欄位（`version` 照舊或 +1 並保留讀舊版）；場景卡 `okSc` 不變。

---

## 6. 分階段計畫

每階段結束都要：`OKStudio.GameSelfTest` 全過（目前 24 步）、`Tools/lua/lua.exe Tools/verify_ok_studio.lua --tag <名稱>` 結果與前一版相同、`Tools/run_ok_studio_perf.lua` 幀時間不退步，再加上該階段自己的驗收。U1 起每階段都要用 `OKStudio.UiLayout` 輸出該階段動到的面板，與[版面規格](OK_STUDIO_HS2_LAYOUT_SPEC.md)對應步驟比對（第 5 節的容許誤差），並重拍並排截圖。

### U0　拆檔與指令表（先做，P0）

目的：讓後續 UI 工作可以分檔並行，並建立可自動驗收的入口。

| 項目 | 內容 |
|---|---|
| U0-1 拆 `Game.cpp` | `Run*Test` 七個自測搬到 `Private/OKStudioGameTests.cpp`；`SOKStudioView` 搬到 `Private/SOKStudioViewport.h/.cpp`；`SOKStudioPanel` 宣告移到 `Private/SOKStudioPanel.h`，再依區塊拆 `SOKStudioOutliner`、`SOKStudioInspector`（Character／Item／Light／Camera／Effect／Scene 各一個 `.cpp`）、`SOKStudioTimelineBar`、`SOKStudioFileBrowser`。主控台指令留在 `OKStudioGame.cpp` 或移到 `OKStudioConsole.cpp`。純搬移，不改行為。 |
| U0-2 指令表 | 新增 `Private/OKStudioCommands.h/.cpp`：`FOKStudioCommand { FName Id; FText Label; FInputChord Default; TFunction<bool()> CanExecute; TFunction<void(const FJsonObject*)> Execute; }`，註冊現有 18 顆按鈕的動作。按鈕改成 `MakeCommandButton(Id)`。 |
| U0-3 全域快捷鍵 | 用 `FSlateApplication::RegisterInputPreProcessor` 掛工作室專用前處理器：工作室執行中、焦點不在可編輯文字框時，依指令表派發快捷鍵。取代 `SOKStudioView::OnKeyDown`（`Game.cpp:138-151`）只在視窗有焦點才生效的做法。 |
| U0-4 主控台與控制通道 | 主控台 `OKStudio.Cmd <id> [json]`、`OKStudio.UiState`（輸出開著的窗、選取、焦點、指令可用狀態 JSON）。新增 `OKStudio.UiSelfTest`：只透過指令表操作完成「加角色→選取→移動→存檔→讀檔→復原」。 |

驗收：GameSelfTest 24/24、`verify_ok_studio.lua` 與拆檔前結果一致；`OKStudio.UiSelfTest` PASS；在視窗外（例如點過右側屬性面板後）按 Ctrl+Z 仍能復原。

### U1　SceneEditor 外殼改成 HS2Studio6 版面（P0）

尺寸、錨定與配色都取自[版面規格](OK_STUDIO_HS2_LAYOUT_SPEC.md)的「外殼」與「主題」兩節，下表只列對應關係。

| 項目 | 內容 |
|---|---|
| U1-1 左上主選單 | 取代現在左欄的 18 顆平鋪按鈕（`Game.cpp:202-224`）。<br>• 左上直排四個主圖示：加入、角色（anim）、音效、系統；35 寬，對應規格的 `RootToolbar`。<br>• 點一下展開 80 寬的子選單（`AddPanel`／`AnimationMenu`／`SystemMenu`）。<br>• 加入：角色、道具、燈光、相機、特效、資料夾、地圖、背景／前景框。<br>• 角色：狀態、骨骼、動畫。<br>• 系統：設定、儲存、讀取、備份恢復、資源檢查、匯入 KStudio、播放故事、離開。<br>• 選單項目一律來自指令表。<br>• 左側另放「姿」「找」兩個 33×33 小鈕（姿勢庫、快搜）。 |
| U1-2 右上工具列 | 33×33、間距 40 的圖示列，對應 `TopTool_*`：工作區、移動、顏色、選項、相機、角色分組。開啟中用綠 `#00A514`。 |
| U1-3 浮窗框架 | 新增 `SOKStudioWindow`：標題列可拖、點擊置前、關閉鈕、記住位置。<br>• 詳情窗（狀態、骨骼、動畫、道具目錄、燈光…）一次只開一個，預設位置照規格，例如狀態 134,7,252,326、骨骼 134,7,320,390、動畫 135,10,446,370。<br>• 獨立浮窗不受這個限制：動畫控制器 160×332、Timeline 520×260、快搜 360×590、姿勢庫 360×368、相機 300×429。 |
| U1-4 右側兩欄 | 右上依序是角色分組欄（240×429）與工作區（240×429），取代現在左欄的物件列表和右欄的長屬性面板（`SceneProperties :1022` 的後製改到「畫面設定」窗）。<br>• 工作區欄：標題、展開／收起全部、樹（列高 18）、Timeline 鈕加搜尋框、工具圖示兩排（28×28）。<br>• 角色的屬性改成「角色」選單底下的分頁詳情窗：狀態、骨骼（FK／IK／視線／頸部／表情／手勢／姿勢／權重）、動畫。<br>• 標題列顯示目前角色名（參考 `hs2/52`～`67`）。 |
| U1-5 左下變換列 | 478×60，對應 `TransformBar`：位置、旋轉、縮放三列 XYZ 加「重設」、複製／貼上／重設變換、主軸開關、移動／旋轉／縮放模式鈕（細節在 U4-2）。 |
| U1-6 主題與縮放 | `FOKStudioStyle` 集中定義規格的主題色、字級（11／10／12／16）與常用尺寸。以 1600×900 為基準依視窗高度等比縮放；右側群組靠右上錨定，變換列靠左下。`Tools/run_scene_editor.lua` 的預設視窗改成 1600×900。 |
| U1-7 隱藏 UI 與狀態 | `Z`（可重綁）隱藏全部 UI；提示訊息與背景工作進度改成畫面下方的小字列，不占用側欄。 |
| U1-8 版面輸出與保存 | 新增 `OKStudio.UiLayout <名稱>`：把 Slate 版面樹輸出成和 HS2Studio6 dump 相同欄位的 JSON，給 `Tools/studio_layout_spec.lua` 比對。窗口開關與位置記在 `Saved/OKStudio/ui_layout.json`。 |

要動：
- `SOKStudioPanel`（U0 拆檔後的各檔）
- 新增 `SOKStudioWindow.h/.cpp`、`SOKStudioRootMenu.cpp`、`SOKStudioTransformBar.cpp`、`OKStudioStyle.h/.cpp`
- `Tools/run_scene_editor.lua`（預設視窗大小）
- `Tools/studio_layout_spec.lua`（加入比對模式）

驗收：
- 開空場景，用 `OKStudio.UiLayout` 輸出版面，與規格的 `00_empty` 比對。四個主圖示、右上工具列、兩個右側欄、變換列的矩形都在 ±4 以內，顏色與字級相同。
- 加入選單、角色選單、系統選單分別與 `01`、`10`、`40` 比對。
- 並排截圖：`Saved/StudioUIRef/compare/` 重拍 01、06，HS2Studio6 那側用 `hs2/L_00_empty.png`、`hs2/L_01_add_menu.png`。
- `OKStudio.UiState` 確認詳情窗一次只開一個、浮窗不受限制。
- 1280×720 與 1600×900 兩種視窗大小都沒有元件被裁掉。
- 效能不退步。


#### 完成紀錄（2026-10-29，F 組，未 commit）

**U0**
- **拆檔**
  - 面板與視窗類別搬到 `Private/SOKStudioPanel.h`。
  - 自測搬到 `Private/OKStudioGameTests.cpp`。
  - 自測共用的骨骼輔助函式放在 `Private/OKStudioGameInternal.h`。
  - `OKStudioGame.cpp` 從 4,210 行減到 1,074 行。
- **指令表**：52 個指令，按鈕、快捷鍵、主控台共用；清單見 `OKStudio.UiState`。
- **全域快捷鍵**：用 `IInputProcessor` 實作，焦點在文字框或故事模式時不觸發。
  - Ctrl+Z／Y／D／S／C／V：復原、重做、複製物件、儲存、複製變換、貼上變換。
  - W／E／R 切換移動、旋轉、縮放；Z 隱藏介面；Delete 刪除；Esc 關閉視窗或選單。
- **主控台指令**：`OKStudio.Cmd`、`OKStudio.UiState`、`OKStudio.UiLayout`，以及 `OKStudio.RunScript`（逐行執行指令，支援 `wait N`、`shot NAME`、`quit`）。
- 還沒做：`OKStudio.UiSelfTest`。

**U1**
- **外殼**：左上主選單（加入、角色、音效、系統）與固定高度子選單、右上工具列、角色分組欄（10 組、54 個掛點，可掛到所選部位）、工作區樹（可摺疊、搜尋、眼睛鈕、工具鈕）、左下變換列（XYZ、重設、複製／貼上／重設變換、主軸、模式鈕）。
- **詳情窗**：一次只開一個，依頁面定位。狀態、骨骼（FK、IK、視線、頸部、表情、手勢、姿勢檔七個子頁）、動畫、屬性、畫面設定、場景檔、角色載入器、道具目錄（三欄）、添加光源、相機。
- **時間軸**：獨立浮窗。
- **主題與縮放**：`OKStudioStyle.h`；縮放與 HS2Studio6 相同，scale = √((W/1600)·(H/900))。
- `Tools/run_scene_editor.lua` 預設視窗改成 1600×900。
- 10-29 當時還沒做：視窗拖動與置前、`ui_layout.json` 版面保存、各視窗內部改成新樣式、圖示。10-30 已完成（`SOKStudioWindows.cpp` 浮窗與版面保存、`OKStudioStyle.h` 的 `SIcon` 向量圖示），見下方「驗收（2026-10-30）」。

**驗收**
- **版面比對**：`Tools/lua/lua.exe Tools/studio_layout_spec.lua --compare 00_empty,01_add_menu,10_anim_menu,11_state,12_bones_fk,20_animation,40_system_menu,04_item_catalog,05_light_add,53_camera,52_timeline`。所有外殼區塊與已對應的視窗誤差都在 0.2 單位以內（容許 ±4）。報告在 `Saved/StudioUIRef/compare/layout_*.txt`。
- **截圖**：用 `SceneEditor.cmd` 加 `OKStudio.RunScript Saved/StudioUIRef/scripts/u1_shell.txt` 拍攝。並排對照圖是 `Saved/StudioUIRef/compare/U1_*.png`（9 張）。
- **既有自測**：`Tools/lua/lua.exe Tools/verify_ok_studio.lua --tag U1Shell1029`，6 項過 5 項。
  - 通過：GameSelfTest、PatternTest、BloomTest、GradeTest、TransparencyTest。
  - 失敗：EyeSetTest 的「漩渦眼」（swirl）。同一份 log 顯示測試卡 `Content/CharacterCards/CrowdAPNG/C002.png` 缺 3 樣開卡資源（10-28 嚴格開卡之後的檢查）；10-23、10-24 的紀錄是通過。尚未用改動前的版本重跑確認是否與 UI 無關。
- 還沒做：效能（`run_ok_studio_perf.lua`），以及 1280×720 視窗下的檢查。

#### U1 剩餘到 U9 的實作清單與決定（2026-10-29，F 組）

使用者指示：剩下的問題自行決定，列出完整清單後全部實作。以下是逐項清單與做法上的決定；完成後在各階段補完成紀錄。

**檔案配置**：面板仍是 `SOKStudioPanel`（`SOKStudioPanel.h` 宣告，內聯的外殼程式留在原處），新功能的成員函式寫在分檔的 `.cpp`：
- `SOKStudioWindows.cpp`：浮窗框架、版面保存、對話框、角色卡瀏覽器、快搜、姿勢庫、場景瀏覽器、色盤、設定、截圖、輸出、報告、資源檢查、備份恢復。
- `SOKStudioTimeline.h/.cpp`：時間軸元件。
- `OKStudioSettings.h/.cpp`：`Saved/OKStudio/settings.json`（快捷鍵、相機速度、Gizmo 尺寸、吸附步進、自動備份、卡片根目錄、介面縮放）與 `favorites.json`。
- `OKStudioThumbnails.h/.cpp`：縮圖背景解碼與 LRU 快取。
- `OKStudioControl.cpp`：SceneEditor 控制通道（HTTP，127.0.0.1）。

**決定**
1. 快捷鍵衝突：計畫裡 F2 同時是「改名」（U2-3）和「設定窗」（U9-1）。改名用 F2（Windows 慣例，也可雙擊）；設定窗改成 F1。
2. Ctrl+C／Ctrl+V 改成複製／貼上物件（U2-5）；複製／貼上變換改成 Ctrl+Shift+C／Ctrl+Shift+V，變換列按鈕不變。
3. 視窗點選不做 ID buffer：角色用骨架上多個骨點投影到畫面的外框、其他物件用元件 bounds 投影；點中多個時取離相機最近的。
4. 多選時 Gizmo 以主選物件為樞紐，位移與旋轉在世界座標套用到其他選取物件；縮放各自乘上同一比例。
5. 道具懸停預覽：另開一個隱藏的場景 Actor 只放那一個道具，放在遠處，用 `FOKSceneCard::Capture` 拍 256 px，快取 12 張。
6. 地圖沒有縮圖來源（基礎庫沒轉出地圖縮圖），地圖瀏覽器只列名稱與分類，按「讀取」才換。這點記為限制。
7. 表情、手型名稱：基礎庫沒有名稱表，使用遊戲的編號加上已知類別名稱（眉、眼、嘴、手型）；名稱表放在 `OKStudioNames.h`，之後有來源再替換。
8. 時間軸新軌道：相機的 FOV 沿用 `Value`；燈光顏色加 `FOKSceneKey::Color`；畫面設定用 `ObjectId = -2` 的場景軌道，key 加 `Exposure`、`FocalDistance`；角色動畫時間加 `AnimTime`（< 0 不控制）。舊檔欄位不變。
9. 自動備份預設 5 分鐘、10 份；內容雜湊沒變就跳過；拖動中、載入中、播放／輸出中延後。
10. 控制通道沿用 street 的做法（`HTTPServer` 模組），埠由 `-StudioControlPort=`（預設 47890）指定，只在帶 `-Control` 時開。`ok-game-mcp` 新增 `studio_*` 工具，以 `OK_STUDIO_PORT` 指定埠。
11. U6-8：讀卡畫面的「動作時間軸」改成開 OK Studio 並把目前角色放進去。舊 UMG 工作室的「從影片產生動作」新時間軸沒有對應功能，所以影片動作面板保留，其餘舊工作室程式移除。

**清單**
| 階段 | 項目 |
|---|---|
| U0 | `OKStudio.UiSelfTest`：只透過指令表跑「加角色→選取→移動→存檔→讀檔→復原」，之後每階段加步驟 |
| U1 | 浮窗拖動（標題列）與置前（點擊）；詳情窗與浮窗位置、開關、各欄顯示存進 `Saved/OKStudio/ui_layout.json`；窗內控制項（下拉、數值框、勾選、文字框）改成主題樣式；1280×720 檢查；效能量測 |
| U2 | 視窗點選；多選（Shift／Ctrl、樹的區間選）；批次刪除、複製、隱藏，一次 Undo；拖放改父與掛骨；雙擊或 F2 改名；種類篩選；定位當前；右鍵選單；物件剪貼簿（跨場景、重配 Id）；Undo 記住選取 |
| U3 | 角色卡瀏覽器（多根目錄、縮圖網格、搜尋、翻頁、預覽、加入／替換）；道具目錄搜尋、懸停預覽、中鍵加為子物件；快搜（AND、排除、收藏、隱藏、最近使用）；特效清單；地圖瀏覽；表情、手型、衣服狀態改名稱清單 |
| U4 | 世界／局部（L）、平面把手、Ctrl 吸附、拖動時顯示數值、把手大小可調；F 聚焦、Ctrl+Space 回初始、前／側／上視角、自由飛行（按住右鍵 WASD＋QE）；從視角建相機、存視角到相機、近遠面、下一相機；十個視角書籤（Ctrl+數字存、數字讀）；只畫所選燈的範圍或錐角 |
| U5 | 視窗骨點（IK 13 點、FK 骨點與連線）與點選；FK／IK 部位開關；雙軸；鏡像（全部、左→右、右→左）；IK←FK；跨角色複製姿勢；姿勢庫（縮圖、命名、刪除、搜尋、試套）；可搜尋的骨骼樹（角色 FK 與道具 FK）；表情窗與視線按鈕組、注視點拖動 |
| U6 | 時間軸浮窗（時間尺、播放頭、物件列與子列、菱形 key）；key 點選、框選、拖動、刪除、複製貼上、改 Ease、K 打 key；時長輸入、逐幀、首尾、循環區間、速度、30 fps 吸附；新軌道；拖播放頭真的取樣；輸出對話框（範圍、fps、解析度、資料夾、進度、取消、開資料夾）；舊動作時間軸改入口 |
| U7 | 場景瀏覽器（縮圖、搜尋、排序、刪到 Trash、改名、覆蓋／疊加／取消）；`Merge`；另存、Ctrl+S、KStudio 匯入共用瀏覽器；原子寫入；未存提示與標題 `*`；自動備份與備份恢復窗；讀取報告與佔位列；資源檢查 |
| U8 | 色塊與色盤浮窗（最近使用色）；畫面設定分頁（相機、環境／天空、霧、泛光、景深、色調／LUT、暗角、背景／前景框，補 ColorFilter、Background、Frame、BackgroundColor、RampG、FogColor、MapTransform）；後製預設；燈光作用對象與種類按鈕；材質屬性下拉；截圖窗（F12） |
| U9 | 設定窗（F1）；快捷鍵重綁、衝突檢查、恢復預設、提示文字依綁定；`ok-game-mcp` 的 `studio_*` 工具與 `Docs/OK_STUDIO_MCP.md`；「播放故事」選單 |

### U2　選取、多選與物件樹（P0）

| 項目 | 內容 |
|---|---|
| U2-1 視窗點選 | `SOKStudioView::OnMouseButtonDown`（`Game.cpp:116`）先測把手，再做選取：角色用骨骼膠囊或部件 bounds（預覽角色是自訂渲染、未必有碰撞）、道具與燈光相機用元件 bounds／圖示，經 `AOKSceneActor::ComponentOf`／`PreviewOf` 反查 Id。左鍵點擊（按下到放開移動 < 4 px）才算選取，拖動仍繞行相機，維持既有相機手勢（[相機輸入](CAMERA_INPUT390.md)）。 |
| U2-2 多選 | `SelectedId` 改成 `TArray<int32> Selection` 加 `PrimaryId`；Shift／Ctrl 加選；Gizmo 以主選物件為樞紐、同時套用所有選取（`GuideHandles :1551`、`ApplyGuideDrag :1638`）；刪除、複製、隱藏對整批作用、一次 Undo。 |
| U2-3 真的樹 | 工作區改 `STreeView`（Editor 版 `Editor.cpp:332-345` 已有可參考的拖放改父）：摺疊、Shift 區間選、雙擊或 F2 改名（Enter／失焦提交、Esc 取消、空白或同名不入歷史）、可見性眼睛鈕、種類圖示、搜尋與依種類篩選、「定位當前」。 |
| U2-4 改父與掛骨 | 拖放到物件＝改父，保留世界變換、拒絕循環；拖到角色的部位節點＝設 `AttachBone`，部位分組沿用 HS2Studio6 的頭髮／頭／臉／頸／胸背／腰／四肢／手／下腹，對應 `FOKStudioCatalog::AccessoryPoints`；綠可放、紅拒絕、停 450 ms 展開。 |
| U2-5 右鍵選單與剪貼簿 | 右鍵：改名、複製、貼上、刪除、隱藏、移到根層、設為父、聚焦、存成…。Ctrl+C／Ctrl+V／Ctrl+D／Delete。剪貼簿格式用 `UOKSceneDocument::ToJson` 的物件片段（可跨場景貼上，貼上時重新配 Id）；另有「複製變換／貼上變換」「複製角色狀態／姿勢」。 |
| U2-6 選取進 Undo | 復原後恢復當時選取（但不恢復視口相機，同 HS2Studio6）。 |

要動：`Game.h`（`SelectedId` 的全部使用處）、`SOKStudioViewport`、`SOKStudioOutliner`、`Doc.h/.cpp`（`Duplicate(TArray)`、`PasteFragment`、`ReparentKeepWorld`）。

驗收：UiSelfTest 新增「視窗點選角色→Shift 加選道具→一起移動→一次 Undo 還原兩者」「拖道具到角色右手→存讀後仍跟隨」「Ctrl+C 後到新場景 Ctrl+V」；GameSelfTest 既有的 undo／子接點步驟不退步。

### U3　目錄與加入流程（P1）

| 項目 | 內容 |
|---|---|
| U3-1 角色卡瀏覽器 | 浮窗：多根目錄（`Content/CharacterCards`、`Config/DefaultGame.ini` 的遊戲 `UserData/chara`、使用者自訂資料夾，記在 `Saved/OKStudio/settings.json`）、路徑列、縮圖網格（PNG／APNG 第一格，背景解碼、分幀建立）、搜尋、翻頁、「定位當前」。點一下預覽，按「加入」或「替換所選角色」才生效；讀失敗保留原角色並提示。取代 `+角色` 固定用 `Cards[0]`（`Game.cpp:205`）與屬性區下拉（`:568`）。 |
| U3-2 道具目錄 | 三欄（大分類→中分類→條目）＋搜尋；懸停 180 ms 顯示完整名稱與預覽，沒有縮圖時用 SceneCapture 即時算 256 px、快取 12 張；左鍵加入、**中鍵加到所選物件底下**。取代先加空道具再選的流程（`ItemBrowser :1121-1167`）。 |
| U3-3 快搜 | 獨立浮窗（快捷鍵可設）：同時搜尋道具、特效、地圖、動畫、姿勢；空格 AND、`-詞` 排除；收藏、隱藏、最近使用存在 `Saved/OKStudio/favorites.json`。 |
| U3-4 特效與地圖 | 特效改清單選擇（取代循環切換 `:731`）；地圖瀏覽器有縮圖，按「讀取」才換地圖。 |
| U3-5 名稱化 | 表情（眉、眼、嘴）、手型、衣服狀態從數字 SpinBox 改成名稱清單（來源：卡片／目錄已有的名稱表），保留數值輸入作進階。 |

要動：新 `SOKStudioCardBrowser`、`SOKStudioCatalogBrowser`、`SOKStudioQuickSearch`、`OKStudioThumbnails.h/.cpp`（縮圖快取，共用 `FKKAnimatedPng` 解碼）；`FOKStudioCatalog` 補名稱／縮圖查詢。

驗收：100 張卡的資料夾開窗後 UI 不卡頓（perf 量測開窗期間幀時間）；搜尋與中鍵加子物件有 UiSelfTest 步驟；人工檢查清單：從零加一個角色＋三個道具＋換地圖，點擊次數比現況少（記錄在驗收文件）。

### U4　Gizmo 與相機（P1）

| 項目 | 內容 |
|---|---|
| U4-1 Gizmo | 世界／局部切換（L）、平面把手（XY／YZ／XZ）、Ctrl 吸附（移動 0.1 m、旋轉 15°、縮放 0.1，可設）、拖動時在狀態列顯示數值、把手大小固定螢幕尺寸可調。 |
| U4-2 數值輸入 | 照 HS2Studio6 左下變換列（`hs2/00_start.png`）做一條常駐變換列：位置／旋轉／縮放各一列 XYZ（可拖拉、可輸入）＋每列「重設」、「複製／貼上／重設變換」、移動／旋轉／縮放模式鈕、「主軸」開關。屬性面板不再重複放 Transform。 |
| U4-3 相機操作 | F 聚焦所選（多選取合併 bounds）、Ctrl+Space 回初始視角、前／側／上預設視角、可選的自由飛行模式（WASD＋QE，右鍵按住時才生效）；預設仍是現有繞行。拖動必須從視窗開始（現況已如此，[相機輸入](CAMERA_INPUT390.md)）。 |
| U4-4 相機物件 | 「從目前視角建立相機」「把目前視角存到所選相機」、近遠面、下一相機；十個視角書籤（Ctrl+數字存、數字讀，存在場景內），書籤操作進 Undo。 |
| U4-5 燈光標記 | 只顯示所選燈的範圍／錐角線；截圖與影片輸出時隱藏所有標記與 Gizmo（確認 `Screenshot :1914` 已排除）。 |

要動：`SOKStudioViewport`、`UOKStudioGame::GuideHandles/HitGuide/ApplyGuideDrag/Orbit/Pan/Zoom`、`Doc.h`（相機加 Near／Far、場景加 `CameraBookmarks[10]`）。

驗收：UiSelfTest「選角色→F→相機距離落在 bounds 內」「建相機→存書籤→移動→讀書籤回到原位」；吸附後數值為步進倍數；局部軸旋轉後的移動方向以數值檢查。

### U5　姿勢工作流（P1）

| 項目 | 內容 |
|---|---|
| U5-1 視窗骨點 | 姿勢模式下在視窗畫出：IK 13 目標（紅＝末端、藍＝肘膝）、FK 骨點與層級連線；骨點固定螢幕尺寸、點選優先於遮住它的軸。資料來源：`FOkSession::IkEffectorPosition`、`BoneWorldMatrix`。 |
| U5-2 部位開關 | FK 依部位分組（身體、胸、頭髮、頸、左右手、裙子）開關，預設照 HS2Studio6；IK 同理。取代單一下拉選骨（`FkProperties :772-837`）。 |
| U5-3 雙軸 | 選骨點時同時保留角色整體軸（樞軸為雙腳中點、縮小顯示），可用「主軸」開關隱藏；選到只能移動的 IK 點時自動改移動軸。 |
| U5-4 鏡像與複製 | 左右鏡像（整體、只左→右、只右→左），骨名對照用 `FOKStudioCatalog::FkBones` 的 `_L/_R` 規則；「FK←目前姿勢」（現有「取目前姿勢」）、「IK←FK」；跨角色複製姿勢。 |
| U5-5 姿勢庫 | 浮窗：KK `.dat` 與 OK 姿勢列表、縮圖（存姿勢時用目前視角拍 256 px）、命名、刪除、搜尋、套用前預覽（暫套、取消可還原）。目前存檔名自動用時間（`:814-836`）。 |
| U5-6 骨骼清單 | 「數值」展開骨骼樹可直接輸入；道具 FK（`ItemFkProperties :1204-1228`）改同一個樹元件，骨頭多時可搜尋。 |
| U5-7 表情與視線 | 表情窗：眉／眼／嘴名稱清單、開合滑桿、眨眼；視線與頸部（照動畫／看鏡頭／注視點／固定）改成按鈕組，注視點可在視窗拖動。 |

Undo：姿勢拖動仍走整份快照（現有機制已能還原 FK／IK），但一次拖動只記一筆；不另開獨立歷史，避免兩套歷史互相干擾（HS2Studio6 的獨立歷史是因為他們的運動學狀態不在場景資料內；我們的 FK／IK 已在 `FOKSceneCharacter::Fk/Ik` 裡）。

要動：`SOKStudioViewport`（骨點繪製與點選）、`SOKStudioInspector_Character`、新 `SOKStudioPoseLibrary`、`OKStudioPose.cpp`（鏡像）、`Game.cpp` 中 `ok_unity_euler_rotate` 相關骨骼拖曳（`:1667`）。

驗收：UiSelfTest「點右手 IK 點→移動→Undo」「鏡像左右後骨骼尤拉角對稱（數值比對）」「存姿勢→讀回→FK 相同」；既有 `OpenKoikatsu.Studio.SavedPose` Automation 仍過；人工截圖：一個角色擺出坐姿，記錄操作步數。

### U6　時間軸（P2）

| 項目 | 內容 |
|---|---|
| U6-1 時間軸浮窗 | 新 `SOKStudioTimeline`：時間尺、播放頭、每個有軌道的物件一列（可展開成 Transform／FK／角色狀態／數值子列），key 以菱形標示；取代單一 `SSlider`（`MakeTimeline :270-343`）。 |
| U6-2 key 操作 | 點選、框選、Shift 加選 key；拖動改時間、Delete 刪、Ctrl+C／V 複製到播放頭；右鍵改 Ease（現有五種）；「為所有選取物件打 key」（K）。key 操作進 Undo。 |
| U6-3 時間控制 | 時長直接輸入（取代只能 +5 秒）、逐幀（, .）、回開頭／結尾、循環區間、播放速度、吸附到整數幀（30 fps）。 |
| U6-4 新軌道 | 相機（Transform＋FOV）、燈光（顏色、強度）、後製數值（曝光、DOF 距離）。`FOKSceneKey::Value` 已能放數值，必要時加 `Property` 名稱欄位。 |
| U6-5 求值順序 | `EvaluateTimeline` 只寫文件欄位，再交給 `Sync`；動畫播放與 FK／IK 依文件狀態解算，**時間軸絕不直接寫骨骼**，避免 HS2Studio6 已知的覆蓋問題。拖動播放頭時真的取樣（他們的 `Seek` 不取樣是已知缺口）。 |
| U6-6 動畫時間 | 角色的 `Character.Time` 與 `Speed` 給 UI，並可上軌道，讓 Studio 動畫可以被定格在某一幀。 |
| U6-7 輸出 | PNG 序列／mp4／`.okanim` 改成對話框：範圍、fps、解析度、輸出資料夾；進度條與取消；完成後可開啟資料夾。 |
| U6-8 舊動作時間軸 | 把 `UOKStudioTimeline`（`Source/OpenKoikatsu/OKStudioScene.h:50-86`）的功能對照到新時間軸，完成後讀卡畫面「動作時間軸」改為開 OK Studio，舊 UMG 工作室移除（即 [OK Studio 計畫](OK_STUDIO_PLAN.md) S7 的併入項）。 |

要動：新 `SOKStudioTimeline.h/.cpp`；`Doc.h/.cpp`（key 選取不入檔、新軌道屬性）；`UOKStudioGame::SetTimelineTime/TickExport`；`Source/OpenKoikatsu/OKStudioScene.*`、`OKGalleryWidget.cpp:391`（U6-8）。

驗收：GameSelfTest 時間軸步驟不退步；新增「相機軌道兩個 key → 中間時刻 FOV 為插值」「拖 key 後 Undo」「輸出 3 秒 PNG 序列中途取消，不留半成品以外的檔」；人工做一段 5 秒雙物件動畫並輸出 mp4，記錄步驟。

### U7　存讀與可靠性（P1）

| 項目 | 內容 |
|---|---|
| U7-1 場景瀏覽器 | 浮窗：目錄樹＋縮圖網格（場景卡第一格）、搜尋、排序（時間／名稱）、刪除（移到 `Saved/OKStudio/Trash/`）、改名；點卡片只預覽，再選「覆蓋」「疊加」「取消」。取代點一下就讀（`MakeFilesBox :346`）。 |
| U7-2 疊加 | 新 `UOKSceneDocument::Merge`：重新配 Id、保留現有物件、時間軸合併、不改相機與後製。 |
| U7-3 另存與匯入 | 儲存（Ctrl+S，已有檔名就覆寫）、另存新檔、KStudio 匯入改用同一個瀏覽器選檔（取代手打路徑）。存檔先寫暫存檔再替換。 |
| U7-4 未存提示 | 文件有 dirty 旗標；新場景、開檔、恢復備份、離開時問「儲存並繼續／不儲存／取消」；標題列顯示 `*`。 |
| U7-5 自動備份 | 預設每 5 分鐘、保留 10 份，存在 `Saved/OKStudio/Backup/`；載入中、拖動中、播放／輸出中延後；內容雜湊沒變就跳過；「備份恢復」窗列出時間與縮圖。 |
| U7-6 讀取報告 | 單一物件失敗只跳過，讀完顯示報告（缺卡、缺 zipmod、缺道具、無法解析的欄位），附「複製問題清單」；KStudio 匯入沿用現有 `.import.json` 報告。缺件的物件保留為工作區佔位列（例：「缺少道具 [群組/分類/編號]」），原始欄位原樣留在文件裡，補齊資源後重讀即可恢復，存檔也不會遺失（參考 `hs2/23_scene_loaded_later.png`）。 |
| U7-7 資源檢查 | 「場景→資源檢查」列出所有角色與道具的缺件，連到對應物件（點了就選取）。 |

要動：新 `SOKStudioFileBrowser`（U0 已拆出）、`SOKStudioDialog`（通用確認框）、`Doc.cpp`（`Merge`、dirty、原子寫入）、`UOKStudioGame::Save/Load/Tick`（備份計時）。

驗收：UiSelfTest「改動後按開檔→出現未存提示→取消→文件未變」「疊加兩個場景後 Id 不衝突、存讀一致」；手動刪掉一張卡後讀場景，報告列出該卡且其他物件正常；強制中斷存檔（自測注入失敗）時舊檔不受損。

### U8　外觀參數補全（P2）

| 項目 | 內容 |
|---|---|
| U8-1 共用色盤 | 一個 `SOKStudioColorPicker` 浮窗（Slate `SColorPicker` 包裝＋最近使用色），所有顏色欄位改成色塊；再點同一色塊關閉、換選取時解除綁定。取代 `#RRGGBB` 文字框（`:630`、`:660`、`:740`）與燈光 RGB SpinBox（`:750-761`）。 |
| U8-2 後製面板 | 獨立「畫面設定」窗，依分類分頁（相機、環境／天空、霧、泛光、景深、色調／LUT、暗角、背景／前景框）；補上目前沒有 UI 的 ColorFilter、Background／Frame、BackgroundColor、RampG、FogColor、`MapTransform`。滑桿旁可直接輸入數值。 |
| U8-3 預設 | 後製預設存 JSON（`Saved/OKStudio/PostPresets/`），用目前相機拍縮圖。 |
| U8-4 燈光 | 補 `bCharactersOnly`（作用對象）UI；種類改按鈕組。 |
| U8-5 材質編輯 | 屬性名改為下拉（從材質實際參數列出），數值依型別給滑桿／色塊／貼圖選擇；保留原始文字輸入作進階（`:675-719`）。 |
| U8-6 截圖窗 | F12：預設解析度＋自訂、透明背景、輸出資料夾、檔名樣式、不覆蓋既有檔、完成後提示路徑。取代三顆固定按鈕（`:325-327`）。 |

驗收：GameSelfTest 的後製與截圖步驟不退步、`OKStudio.PostTest`／`GradeTest`／`BloomTest` 仍過；每個新增 UI 欄位做存讀來回比對（存檔 JSON 欄位值相同）。

### U9　設定、快捷鍵重綁與 AI 控制（P2）

| 項目 | 內容 |
|---|---|
| U9-1 設定窗 | F2，版面照 `hs2/14_settings.png`：左側分類（一般、快捷鍵、相機與 Gizmo、備份、資源目錄、快捷鍵一覽）、頂部搜尋設定項、每列附 `?` 說明與「套用／預設」，滑桿即時生效、文字欄按套用才生效。內容：快捷鍵、相機速度、Gizmo 尺寸、吸附步進、自動備份、卡片根目錄、介面縮放；存 `Saved/OKStudio/settings.json`。 |
| U9-2 快捷鍵重綁 | 從指令表列出所有指令，可重綁、衝突檢查、恢復預設；按鈕提示文字依綁定動態產生；文字框有焦點時停用（U0-3 已做）。說明選單的快捷鍵一覽也由指令表產生。 |
| U9-3 SceneEditor 控制通道 | `Tools/ok-game-mcp` 加入 SceneEditor 支援（Rust）：沿用 `-Control` 的本機通道，新增 `studio_*` 工具——state、inspect、catalog_search、spawn、transform、reparent、delete、pose、camera、capture、save、undo、redo、cmd（任意指令表 Id）。物件用文件 Id；AI 與手動共用同一份 Undo；使用者拖動中、載入中、播放／輸出中拒絕修改；存檔不覆蓋既有檔。說明寫進 [街區遊戲 MCP](STREET_SOCIAL_GAME_MCP.md) 或另開 `Docs/OK_STUDIO_MCP.md`。 |
| U9-4 Story 入口 | 場景選單加「播放故事」，不必再靠命令列 `-OKStory`。 |

驗收：`ok-game-mcp call studio_spawn ...` 等工具在 `-Sandbox` 下跑一輪完整腳本（加角色、擺姿勢、拍照、存檔）；重綁 W 到別的鍵後按鈕提示同步更新、舊鍵失效。

#### U1 剩餘到 U9 完成紀錄（2026-10-30，F 組，未 commit）

**程式位置**（`Plugins/OKStudio/Source/OKStudio/Private/`）
| 檔案 | 內容 |
|---|---|
| `SOKStudioPanel.h` | 外殼、指令表（約 130 個指令）、快捷鍵（依設定綁定）、樹、屬性頁、主題化的屬性列元件 |
| `SOKStudioWindows.cpp` | 浮窗（拖動、置前、`ui_layout.json`）、確認框與未存提示、樹的編輯（拖放、剪貼簿、右鍵）、場景瀏覽器、備份恢復、讀取報告、資源檢查、播放故事、截圖窗、時間軸窗、輸出窗、設定窗 |
| `SOKStudioBrowsers.cpp` | 角色卡瀏覽器、道具懸停預覽、快搜、特效清單、地圖瀏覽、色盤與色塊、畫面設定九個分頁與預設 |
| `SOKStudioPose.cpp` | 場景視窗的滑鼠（點選、Gizmo、骨點、注視點、飛行）與疊加繪製（多選外框、燈光範圍、骨點、注視點）；FK／IK 頁、鏡像、IK←FK、姿勢庫、骨骼樹、表情、手型、視線 |
| `SOKStudioTimeline.h/.cpp` | 時間軸元件 |
| `OKStudioGameView.cpp` | 點選、聚焦、視角預設、書籤、相機物件、自動備份、讀取報告、忙碌判斷 |
| `OKStudioSettings.h/.cpp`、`OKStudioThumbnails.h/.cpp` | 設定與收藏、縮圖 |
| `OKStudioControl.cpp` | 控制通道 `/studio/<op>` |
| `OKStudioUiSelfTest.cpp` | `OKStudio.UiSelfTest` |

資料模型（`OKSceneDocument`）：相機 `Near`／`Far`、`Bookmarks[10]`、key 的 `Color`／`AnimTime`／`Exposure`／`FocalDistance`、場景軌道（`SceneTrackId = -2`）、`ExportFragment`／`PasteFragment`／`Merge`、原子寫入；角色加 `Blink`。舊檔照常讀取。
`ok-game-mcp`：`studio_*` 19 個工具（`Tools/ok-game-mcp/src/studio.rs`），說明在 [OK Studio MCP](OK_STUDIO_MCP.md)。

**驗收（2026-10-30）**
- `OKStudio.UiSelfTest`：46/46（`Saved/OKStudio/ui_selftest.txt`）。涵蓋 U0 的指令表流程、U2 的點選／多選一起移動／一次 Undo／掛到右手後存讀／跨場景貼上、U3 的快搜與中鍵加子物件、U4 的聚焦／書籤／吸附／局部軸、U5 的 IK 拖動與 Undo／鏡像對稱／姿勢存讀、U6 的 FOV 與曝光插值／key 拖動 Undo／中途取消輸出、U7 的未存提示／疊加／原子寫入失敗不損舊檔／備份、U8 的色盤與新欄位存讀、U9 的快捷鍵重綁與版面保存。
- `Tools/lua/lua.exe Tools/verify_ok_studio.lua`：GameSelfTest（tag `Game1030b`）、PatternTest、BloomTest、GradeTest、TransparencyTest 通過；EyeSetTest 仍失敗，原因見下。GameSelfTest 的三個 Gizmo 檢查原本寫死「3 個把手」，改成只數 X／Y／Z 並另外確認平面把手與角色雙軸存在。
- 版面比對：`studio_layout_spec.lua --compare` 11 個步驟全部在 ±4 以內（最差 0.2）。截圖 `Saved/StudioUIRef/ok/U2_*.png`（30 張，腳本 `Saved/StudioUIRef/scripts/u2_all.txt`）。
- 1280×720：`studio_layout_spec.lua --bounds` 8 個畫面（空場景、系統選單、骨骼、時間軸、角色卡、場景檔、設定、快搜）都沒有元件超出視窗（腳本 `u2_720.txt`；腳本裡每步先 `r.setres 1280x720w`，因為執行中視窗曾被放大）。
- 控制通道：SceneEditor 加 `-Control -StudioControlPort=47890`，用 `ok-game-mcp call studio_*` 跑完加角色、FK＋鏡像、表情與視線、正面聚焦、截圖、存檔、Undo／Redo、時間軸打 key；截圖與存檔都拒絕覆蓋既有檔。
- 效能（2026-10-31，`run_ok_studio_perf.lua --count 8`，1920×1080；機器上同時有另外 2 個 Unreal 在跑）：`perf1031a`（逐像素深度測試開）與 `perf1031b`（`OK_UNITY_DEPTH_TEST=0`）比，關掉反而每張都慢 1～10 ms，代表這時的數字由其他遊戲的負載主導，深度測試本身沒有量得到的成本。和 10-24 的 `final1024` 比，遊戲執行緒多 1～3 ms、GPU 差不到 0.7 ms，同樣受負載影響，不能當成退步的證據；要在只有一個 Unreal 時重量才算數。結果在 `F:\OpenKoikatsu\Saved\OKStudio\perf\perf1031a.json`、`F:\OpenKoikatsu\Saved\OKStudio\perf\perf1031b.json`。
- EyeSetTest：2026-10-31 `verify_ok_studio.lua --tag F1031a` 6 項全過（含 EyeSetTest，`C002.png` 已由 e-okcard 的 `70b762429` 重轉）。
- **EyeSetTest 漩渦眼**：不是 UI 造成的。OK 卡 `C002.png` 平常開得起來（眼型 0 時 69 個部件）；切到眼型 23 時要換成 gag eye 部件重建，而轉卡時只記錄了卡片預設狀態的開卡閉包，模型 `3e5b33000000000056e2c0bf4f4b4331` 的骨架不在卡內也不在基礎庫，10-28 嚴格開卡後就開不了。`ok-extract --oklib seal` 照預設狀態重放，補不到（試過，卡片已還原）。要在 ok-extract 轉卡／seal 時把「會換部件的眼型」也記錄進閉包，或讓基礎庫收 gag eye 模型，屬 OK 卡工具（e-okcard）的範圍。測試另加了「整張卡開不了時換下一張本機卡」。
  - 10-30 e-okcard 回覆：E `duo2` commit `70b762429` 修好（尚未 push）。轉卡在各套服裝之後，對 gag 物件 1／2／3 各取一個眼型（21、23、27），用 outfit 0 再開一次並記錄，每張卡多 3 筆 skeleton，digest 不變。舊卡要用該版之後的 ok-extract 重轉，seal 只用基礎庫，補不到 mod 頭。F 等該 commit 合併進來後，重轉 `C002.png` 再跑 EyeSetTest。
  - 10-30 整合後：70b762429 已在 F 的 HEAD。但 `C002.png` 的 `okId`（原作卡 sha256 `663284207c…`）在 F 這台的遊戲 `UserData`、`Content/CharacterCards`、`Saved` 都找不到相符的原作卡，無法用新版重轉。需要原作卡的人（轉出這批 CrowdAPNG 的 checkout）重轉後放回本機。

**沒做到或做法不同的地方**
- 遠面：Unreal 的透視投影沒有遠面，`Far` 只存檔，不影響畫面。近面有效（`UOKStudioViewCamera`）。
- 地圖縮圖：KK Studio 的地圖清單（`abdata/studio/info` 的 `Map` 表）本身就沒有縮圖欄位，原作也只列名稱；維持只列名稱。
- 表情與手型名稱：U11 從遊戲資料補上眼型與手型名稱；眉型、嘴型遊戲資料沒有名稱，維持編號。
- U6-8：讀卡畫面「動作時間軸」改成開 OK Studio 並打開時間軸。舊 UMG 工作室已在 U11 移除（2026-10-30）。

---

### U10　完整 MCP：AI 擺初版、人手細調（2026-10-30 追加並完成，F 組，未 push）

使用者指示：工作室 UI 要接完整的 MCP，之後用 AI 擺初版、再由人細調。

| 項目 | 內容 |
|---|---|
| 控制通道新 op | `patch`（物件任意欄位深合併）、`scene`（畫面設定、地圖與位置、時長、使用中相機、書籤）、`bones`（FK 骨與世界座標、13 個 IK 點、注視點、54 個掛點）、`bounds`（世界範圍與 `built`）、`catalog`（依分類瀏覽道具／地圖／特效／動畫／濾鏡／天空／掛點）、`keys`（列出、打、刪、移、Ease、清除、平滑路徑；`-2` 是場景軌道）、`duplicate`、`export`（PNG／mp4／.okanim，進度在 `state.export`）、`scenes`、`report`、`ui_shot`（含 UI 的視窗截圖） |
| ok-game-mcp | 再加 11 個工具，`studio_*` 共 30 個 |
| 讀取類 op | 使用者拖動、載入、播放時也會回答；修改類照舊回 `busy` |
| 文件 | [OK Studio MCP](OK_STUDIO_MCP.md) 加「AI 擺初版、人手細調」與座標規則（角色 rotation 0 面向 +Y＝前視相機） |

實測：SceneEditor 加 `-Control`，用 `ok-game-mcp call` 依序目錄瀏覽／搜尋 → 加兩個角色與一張桌子 → `studio_bounds` → `studio_patch` 改名 → 動畫與 FK＋表情＋視線 → 天空、泛光、景深 → 兩個 key 加 Ease → 正面相機 → 截圖與 UI 截圖 → 複製／刪除 → 讀取報告 → 存檔 → 場景清單 → Undo／Redo，全部成功。實測中發現並修好：道具是由自己的預覽 Actor 顯示的，原本的範圍計算只看 `ComponentOf`，道具一律回 30 cm 代替框（也影響視窗點選、聚焦、材質屬性下拉、道具懸停預覽）。改完 UiSelfTest 仍 46/46。

EyeSetTest 漩渦眼（使用者 2026-10-30：缺的元件從 `D:\KoikatuSunshine` 補到共用資源）：還沒補成。`ok-extract --oklib record` 加了 `OK_RECORD_STATUS`（Studio 狀態 JSON，例如 `{"eyes_ptn":23}`），能以 Studio 狀態開卡做記錄（不寫 parity）。但 OK 卡開卡只走基礎庫，不讀遊戲的 zipmod；把 C002 去掉 `ok*` 區塊還原成原作卡再以遊戲記錄，0 筆未命中：遊戲版 `[minih] headmod v4` 的記錄已存在，但記在另一個模型 id 下，C002 指名的是 `3e5b33000000000056e2c0bf4f4b4331`。要在轉卡／seal 時對「會換部件的眼型」另外開一次並收進卡或基礎庫，已把細節交給 e-okcard。實驗動到的基礎庫版控檔已還原，C002 嚴格檢查仍通過。重現：`OK_RECORD_STATUS={"eyes_ptn":23} ok-extract X:/NoGame --oklib replay Content/IndependentResources/base_v1 Content/CharacterCards/CrowdAPNG/C002.png`。

### U11　用遊戲資料補完剩下的缺口（2026-10-30，F 組，未 push）

使用者：缺的元件可以到 `D:\KoikatuSunshine` 找，補到共用資源裡。

| 項目 | 結果 |
|---|---|
| 眼型名稱 | 角色清單 `cha_eyeset`（分類 2）41 個名稱（デフォルト、ウィンク、ぐるぐる目…）。`ok-extract D:/KoikatuSunshine --oklib studio-names <base_v1>` 寫進 `base_v1/studio_items.json` 的 `eye_names`（只新增 378 行，其他內容不變）；表情頁的眼型下拉顯示「編號 名稱」 |
| 手型名稱 | Studio `HandAnime` 表（左右各 21 個）寫進 `hand_names`；手型頁顯示中文名與原片段名（例：「1 握拳（goo）」） |
| 眉型、嘴型名稱 | 遊戲清單沒有，維持編號 |
| 地圖縮圖 | KK Studio 的地圖清單本來就沒有縮圖，維持只列名稱 |
| 影片動作 | 影片匯入的結果就是 `Saved/VideoMotions/*.okanim` 與 `*.pair.json`；角色加 `AnimFile`（任何 .okanim，含輸出的時間軸動畫），動畫頁列出雙人動作與動畫檔：雙人動作把女方給主選角色、男方給另一個選取（或下一個）角色，同時從 0 秒開始。指令 `anim.file`、`anim.pair`；MCP 的 `studio_pose` 加 `anim_file`、`video_pair`，`studio_catalog` 加 `video` |
| 舊 UMG 工作室 | 刪除 `Source/OpenKoikatsu/OKStudioScene.h/.cpp` 與 `Tools/video_motion/test_studio.lua`；讀卡畫面進 OK Studio 的程式搬到 `OKGalleryOkStudio.cpp`；人物編輯 MCP 的 `studio` 動作改成 enter／cmd／exit（場景編輯走 `studio_*`） |
| 診斷 | `OK_STUDIO_INFO_DUMP=1` 時 studio 目錄轉換印出各表與角色清單分類 |
| 影片動作的格式 | 影片動作檔是 session 片段（`OKCLIP01`），不是動作庫的 `OKANIM02`，第一版用 anim player 讀而失敗（角色標成「未顯示」）。改成：`AnimFile` 開頭是 `OKCLIP01` 且在 `Saved/VideoMotions` 時，以 `video:\|<名稱>` 交給角色自己的 session 播（`SetGameAnimation`，PoseMode 2），場景時鐘與時間軸也推動 PoseMode 2。實測 `網球示範.okanim` 播放 10.37 秒、循環 |
| 燈光開關（使用者回報） | 角色的卡通著色只讀一盞「角色光」（勾了「只照角色與道具」的方向光），而且不看它有沒有開：關燈、刪燈、或新增一般燈都不影響角色。改成：取「顯示中」的角色光，沒有就取第一盞顯示中的方向光；場景有過燈但全部關掉時角色進入陰影色；燈光頁加「開啟」勾選（等於樹上的顯示）。新燈預設強度 6 → 4（換算 1.0，Studio 角色光的預設），原本 1.5 倍把角色照到泛白。實測（`Saved/StudioUIRef/ok/L0_main.png`～`L4_dir_on_again.png`）：主光、方向光逐一關掉後角色變暗，再開回復 |
| 點光、聚光照角色 | 和原作一致，不需補：原作角色著色器 `Koikano/main_skin` 只有 ForwardBase（描邊、本體）與 ShadowCaster 三個 pass，沒有 ForwardAdd，所以 KKS 裡點光與聚光本來就不會逐像素照到角色（2026-10-30 讀 shader 的 pass 標籤確認）。卡通著色只用一盞前向光，點光與聚光照地圖與一般材質的道具 |

### U12　各種 KKS 場景卡正確載入（2026-10-30，F 組，使用者要求）

批次檢查：`OKStudio.CompareKStudio <資料夾> <數量> 60 exit`（`-OKStudioCompareOutput=<資料夾>`）。每張場景左邊是原作存的縮圖、右邊是 OK Studio 的畫面，另外寫 `report.txt`：每個場景一行，列出物件數量和每個沒顯示的物件及原因。第一輪抽樣 Official 40、vmd 10、根目錄 5 張，55 張全部載入並截圖（`F:\OpenKoikatsu\Saved\OKStudio\scenecheck1030`）。

| 問題 | 處理 |
|---|---|
| 比對指令在場景編輯器模式下直接退出 | `CompareKStudio` 會先 `Start` 新工作室，關掉原本的工作室；工作室一關，整個程式就退出。改成和 `PerfKStudio` 一樣，先沿用已在跑的工作室 |
| 沒網格的輔助道具被標成「未顯示」 | 例如「回転／投げ／振動／ピストン（obj無し）」和 3D 音效（換気扇、風），遊戲裡也不畫。改成當空物件，只用來放子物件，不算錯誤 |
| mod 道具變成空資料夾（vmd `87018458_p3` 有 30 個，舞台布景整個不見） | 新增 `ok-extract <遊戲> --oklib studio-mods <基礎庫> [場景資料夾 [執行緒]]`：<br>• 讀遊戲 `mods/` 每個 zipmod 的 `abdata/studio/info/**/ItemList_*.csv`，用 `converted.tsv` 對到 bundle 的模型 id，寫進 `base_v1/studio_mod_items.json`（第三方內容，只留本機，已加進 `.gitignore`）。<br>• 給了場景資料夾時，先把場景用到、但基礎庫還沒有的 mod bundle 轉進基礎庫（`convert_with`，不重寫 `studio_items.json`）。<br>• 場景解析讀 sideloader 的 `itemInfo`（`ModID`、`Slot`、`SceneDicKey`），匯出時在道具上標 `mod_guid`、`mod_slot`。<br>• 匯入器用 GUID＋Slot 找道具，換成 mod 自己的群組、分類和編號。<br>• 目錄改成用索引查找（mod 道具有兩萬多筆） |
| 場景角色轉不成 OK 卡（vmd `1545919667549` 的角色 560、3） | 舊場景裡的角色沒用 GUID 指名 zipmod，轉卡的閉包因此缺東西。e-okcard 修好（E `duo2` 的 `6dd189247`：從缺口找出沒指名的 zipmod 重轉），F 已 cherry-pick（`779526b70`）。轉卡失敗時，匯入器會把原作卡留在 `char_<key>.kks.png` 方便重現 |
| 子物件繼承了父道具的縮放（vmd 舞台場景鏡頭前擋著 25 m 高的燈籠） | 依反編譯的 `GuideObject.cs`（第 581～586 行）：每幀把 `localScale` 設成 `localScale / lossyScale * 自己的縮放`。所以物件的世界縮放永遠是自己設定的縮放；父物件的縮放只會放大子物件的位置，不會放大子物件本身。`WorldTransform` 改成從根往下算，每層把世界縮放換成物件自己的縮放；`Reparent` 保留世界縮放。往上找父物件的上限從 256 層提高到 `MaxDepth` 4096 層（場景最深約 1024 層） |

結果：mod 道具補轉後，vmd 57 張全部載入、沒有任何未顯示的物件（`F:\OpenKoikatsu\Saved\OKStudio\scenecheck1030b`）；舞台場景修正縮放後，鏡頭、桁架、燈具都和原作一致（`F:\OpenKoikatsu\Saved\OKStudio\scenecheck1030d`）。全部 7,694 張場景用到的 mod 道具共 146 種，47 個 bundle 補轉進基礎庫，全部都有模型了。

尚未一致、尚未處理：
- ~~螢幕道具在原作即時顯示另一台相機拍到的畫面（render texture），OK Studio 顯示灰色或靜態圖。~~ 已在 U13 完成。
- vmd 資料夾的場景靠 MMD 的 VMD 外掛驅動角色姿勢與位置，原作縮圖拍的是外掛播放中的瞬間；OK Studio 沒有這個外掛，角色停在 T 字姿勢。
- 部分地圖的光照與色調不同，例如 `ill_ss_sample_01` 的大浴場、`1546790632203` 的地板。
- cs0004179 火鍋裡約 1 cm 的豆腐沒露出來；豆腐的位置和大小都正確，推測是周圍食材有毫米級的高低差，把它蓋住了。

### U13　螢幕道具的即時畫面（2026-10-30，F 組，使用者選定）

場景裡的「相機道具＋螢幕道具」（例：mod HarvexARC Studio Effects 的 CH1～CH3-Camera 與 CH1～CH3-Screen）在原作是兩個獨立 prefab，靠同一張 RenderTexture 資產相連：相機的 `m_TargetTexture` 指向它，螢幕材質的 `_MainTex` 取樣它。地圖內「相機與螢幕同在一個 prefab」的情形（live stage）原本已支援，這次補的是跨道具的情形。

| 項目 | 做法 |
|---|---|
| 轉檔記錄 | `prefab_render_textures` 每台相機多記 `target_key`（RenderTexture 名稱＋path id 雜湊成 24 位元），另記 `shared`：取樣「本 prefab 沒有相機畫它」的 RenderTexture 的材質。基礎庫紀錄是 bincode，加欄位會讓舊紀錄讀不出來，所以新格式改存在 `prefab_render_textures2`；只有舊紀錄的地圖照舊讀（`PrefabRenderTexturesV1`） |
| mod 道具補記錄 | mod 的 Studio bundle 當初以角色來源轉換，沒有記節點樹與相機。轉檔時 `abdata/studio/` 的 zipmod bundle 補記；已轉過的用 `ok-extract <遊戲> --oklib studio-mod-nodes <基礎庫> <場景資料夾> [執行緒]` 只補場景用到的道具（全部場景：49 個 bundle、145 個 prefab、14 台相機，20 秒） |
| 螢幕 | 零件材質有 `_OKRTShared`（key、寬、高）時，`AKKPreviewActor` 換成不受光的螢幕材質並登記在 `SharedRenderTextureScreens`，預設黑色 |
| 相機道具 | `ok_okmodel_render_cameras` 回傳 prefab 裡的相機（位置、旋轉、FOV、近遠平面、目標尺寸）；`AOKSceneActor::AddRenderCamera` 在道具上建 `USceneCaptureComponent2D`（Unity 垂直 FOV 換成 Unreal 水平 FOV，沿 Unity +Z 看），每個 key 共用一張 render target |
| 配對 | `WireRenderTextures`：每次同步後，螢幕取同 key 第一台相機的畫面；只有被顯示中的螢幕用到的相機才每幀拍；螢幕不出現在自己的相機裡（不受光的螢幕拍自己會回授成白色） |
| 隱藏的相機道具 | Studio 隱藏道具只關 renderer 與粒子（`OCIItem.visible`），GameObject 還在，相機照樣畫。所以相機道具不論顯示與否都拍 |
| 後製 | Studio 的影像效果只在主相機上：相機道具的 capture 用 Studio 的「原樣輸出」設定（無 film curve、固定曝光、無 bloom／濾鏡／霧／暗角） |
| 除錯 | `OK_RT_DUMP=1`：相機道具出現 5 秒後，把畫面存到 `Saved/OKStudio/studio_rt_<key>.png`，並在 log 印出 Unity 座標的位置與方向 |

驗收：vmd 資料夾用到 HarvexARC 相機的 13 張場景全部載入（`F:\OpenKoikatsu\Saved\OKStudio\scenecheck_rt2`）。`80639555_p1` 大螢幕與兩側小螢幕顯示即時畫面，構圖與原作一致；`1585406034802` 的相機道具是隱藏的，修正後螢幕顯示角色特寫（`F:\OpenKoikatsu\Saved\OKStudio\mcp_ui\rt_debug_1585_nolight2.png`，暫時關掉前方的 tv_light 光暈資料夾後拍）。

仍不一致：
- `1585406034802` 螢幕前的「色影(四角2)」（`Shader Forge/main_item_studio_add`，透明度 0.06～0.38）疊起來比原作亮很多，把螢幕整片蓋白。
- 原作螢幕材質是受光的 Toony Colors Pro（`_Color` 約 0.89），暗場景裡比較暗；OK Studio 的螢幕不受光，略亮。
- `80639555_p1` 中間的小螢幕原作是暗色畫面，OK Studio 是白色，還沒查。
- ~~`2021_1022_2145_32_381` 整張黑畫面~~ 已修（2026-10-30）：場景相機在放大 5 倍的廚房（Petardo 的 CM3D2/Lighted 地圖 mod）牆外，Unity 只畫正面、看得穿；我們多加了卡通描邊外殼而整片黑。CM3D2 系列不再加描邊；CM3D2／Unlit 著色器改畫成不受光、陰影色白。修正後構圖與原作縮圖一致（`F:\OpenKoikatsu\Saved\OKStudio\mcp_ui\kitchen_fixed.png`），廚房牆面比原作亮，歸入地圖色調。
- 同一張的地圖色調（2026-10-30）：
  - CM3D2 移植的地圖著色器以上下顛倒的 V 取樣（牆取到米白磁磚、櫥櫃門取到家電），CM3D2 部件改用 1-v。
  - 場景的 131 號濾鏡是 `[mzam]Filter.zipmod` 的 `mzam_Lut_004`：`studio-mods` 現在把 mod 的 `Filter_*.csv` 寫進 `studio_mod_items.json` 的 `filters`，並把 LUT 記錄進基礎庫（本機 84 個）；目錄讀 mod 濾鏡時遊戲本身的編號優先。
  - 場景 ramp 850 是 MoreShadowTypes 的 mod ramp，原本用 `kk_850` 找不到，改成依編號找 `<mod>_850`。
  - vmd 全部 57 張整批比對都能載入（`F:\OpenKoikatsu\Saved\OKStudio\scenecheck_vmd3`）。`90697134_p0` 原本被大樓擋住：道具非等比縮放與 prefab 根轉動的組合用 FTransform 相乘算錯，改成矩陣相乘後構圖一致（`F:\OpenKoikatsu\Saved\OKStudio\mcp_ui\r90697_fixed.png`）；仍缺光束、中央球體的發光線框、霧與景深。`83626612_p1` 背景白（原作星空藍）、`86003286_p4` 缺暗色煙霧背景，待查。
  - 結果見 `F:\OpenKoikatsu\Saved\OKStudio\mcp_ui\kitchen_cap4.png`：愛心 250,117,132 對原作 251,112,131。牆面仍比原作亮約 30（原作的 CM3D2/Lighted 受光，我們畫成不受光），留待地圖色調一併處理。

### U14　以 KKS 原生畫面為準修地圖光照（2026-10-30，F 組）

原作縮圖常是 KK 舊版拍的，不能直接當標準。改用 `Tools/GamePoseProbe/run_scene_visual_audit.lua --tag <名稱> --scenes <場景>`：在 KKS 的 CharaStudio 開同一張場景，存下原生畫面（1280×720）、材質、燈光（含 culling mask）與每個 renderer 的圖層。例如 `F:\OpenKoikatsu\Saved\Tests\kks_maptone_1030\Scene1.png`。

| 問題 | 原因與處理 |
|---|---|
| 操場 `1546790632203` 地面棕色 | 黃沙是 Studio 道具「砂地1」（`o_koi_stu_yuka_02_01`），在 Unity 圖層 11。地圖太陽的 culling mask 是 2048，也照得到這一層。我們的道具光照模擬只取角色燈，所以第 11 層的道具主光源是黑的。改成和 Unity forward 選主平行光相同的規則：照得到道具圖層的平行光中，取最亮的一盞（角色燈或地圖太陽），寫進 `_LightColor0`／`_WorldSpaceLightPos0`。修正後沙地顏色與原生一致 |
| 大浴場 `ill_ss_sample_01` 偏暗偏藍 | 地圖自己的即時燈光（`Map/light/morning` 的點光源、聚光燈，以及只照水面的平行光）沒有建。新增 `prefab_light_modes` 記錄（`m_Lightmapping`、culling mask、陰影、GameObject 是否啟用；由 `studio-nodes` 記錄）與 `ok_okmodel_map_lights`。載入地圖時，不屬於烘焙燈、也不是地圖太陽的燈都建成 Unreal 燈光，掛在 map 的 lighting channel 上，並依時段顯示。衰減以 Unreal 非平方反比衰減近似 Unity 的衰減貼圖 |
| vmd 場景角色姿勢、`80639555_p1` 中間小螢幕 | 場景以 KKVMDPlay 外掛播放 VMD，存檔只記作者電腦上的 VMD 路徑，本機沒有這些檔案。原作的小螢幕拍到的是跳舞中角色的近景。現在匯入報告會列出每個角色用的 VMD 檔名，並說明找不到檔案、不支援播放 |

| 大浴場的貓偏白 | Standard 著色器的反照率是 `_MainTex × _Color`（線性），我們沒乘 `_Color`。Standard 系材質現在把 `_Color` 轉成線性後乘進反照率（卡通道具路徑不受影響）。修正後貓是灰色與豹紋，和原生一致 |
| 沒有反射資料的地圖反射到 UE 天空 | 原生 tsv 的 `reflection_default` 在沒有烘焙預設反射的地圖是 `UnityBlackCube`，probe 沒有貼圖時也一樣。我們之前退回材質預設的天空 cube。改成這種情況一律綁黑色 cube（`OKStudioBlackReflection`）；工作室天空開著時不變 |

| 大浴場地板偏藍、木桶偏綠 | 不是反射（關掉 UE 反射畫面不變）。地板沒有 lightmap，環境光來自 `unity_SHA*`；我們給的是地圖烘焙的 `m_AmbientProbe`，地圖 62 存的是 0。KKS 地圖沒有 LightProbes 物件（KK 地圖的是空的）。地圖 62 的 ambient mode 是 Trilight：Unity 執行時用天空、地平線、地面三色算環境 SH，烘焙值是過時的。拿原生 `renderer_sh` 對三色擬合，三個色版都精確到 1e-5：C0 = (天空+地面)/6 + 2·地平線/3，C1 = √(2/27)·(天空−地面)，C6 = 5/96·(地平線−(天空+地面)/2)，C8 = 3·C6（顏色先轉線性，再乘 ambient intensity）。Flat 是三色相同的特例。`studio-environments` 現在多記 `ambient_sky`／`ambient_equator`／`ambient_ground`，`OKStudioMapAmbient` 在 Trilight／Flat 時改用三色算。72 張地圖中有 15 張 Trilight、27 張 Flat 都受影響 |
| 操場舞台上方一大片模糊 | 不是景深：關掉 DOF、關掉半透明都一樣。這張場景把地圖移了 (−82.5, 3.43, −4.75)（SceneInfo.caMap，Studio 載入時 `MapCtrl.Reflect` 會套用；KK 縮圖舞台後方就是外牆的磚格）。外牆 `mo_koi_map34_sotokabe01_00` 因此緊貼舞台後方，但貼圖被拉滿整面牆。原因是 mesh 解碼挑主 UV 的啟發式：選「數值落在 ±8 內比例最高」的 channel。地圖的主 UV（TexCoord0）重複到 0–30，反而輸給全在 0–1 的 lightmap UV（TexCoord1）。原作地圖 205 個 mesh 中有 27 個、工作室道具中有網子、樓梯、樹等 6 個中招，角色 mesh 全部用 TexCoord0、不受影響。規則：TexCoord1 是 Unity 的 lightmap UV，只要 TexCoord0 是有效 UV（有限、不超過 4096、不是全 0），主 UV 就用 TexCoord0。基礎庫裡已記錄的 mesh 要重錄：`OKLIB_REFRESH=meshes_for_maindata ok-extract <遊戲> --oklib studio-nodes <基礎庫>`（studio-nodes 現在也會重錄已記錄的 `meshes_for_maindata`）|

| `87018458_p3` 舞台地板灰色、沒有倒影 | 地板上鋪的是 mod 鏡子道具（`yamadamod.studioItem_mirror`「鏡 四角 アルファ 64」，著色器 `FX/MirrorReflection_Transparent`），我們沒有鏡子。原作 PS（反組譯 bundle 裡的 DXBC）：`rgb = tex2Dproj(_ReflectionTex, 螢幕座標) × _MainTex × _Color`、`alpha = _MainTex.a × _Color.a`；腳本 `MirrorReflection` 的設定是 1024 解析度、clip 偏移 0、只照圖層 10／11。做法：`AKKPreviewActor` 遇到 `FX/MirrorReflection*` 的零件換成鏡子材質（參數沿用 Unity 名稱，Material Editor 改 `_Color` 仍有效），記下網格平面；`AOKSceneActor::UpdateMirrors` 每幀用主相機對平面鏡射的位置拍一張（右手系相機，所以畫面左右相反，材質取 `1-u`），近平面用 Unity `CalculateObliqueMatrix` 同樣的斜截面（換成 Unreal 反向 Z 投影：`clip.z = w − k·d`），沒有影像後製 |

修正後舞台地板變成暗色、有建築／鼓組／齒輪的倒影，和原作一致（`F:\OpenKoikatsu\Saved\OKStudio\mirror2\87018458_p3.png`）。全部 7,694 張場景中有 17 張用到這組鏡子道具。

兩側布幕：KK 縮圖是紅色，但 KKS 原生畫面（`F:\OpenKoikatsu\Saved\Tests\kks_stage87018_1030\Scene1.png`）的側幕就是藍灰色（Sims4 `LargeFancyCurtains` 的 IBL_Shader 在 KKS 下的樣子），我們和原生一致，不改。

| `87688028_p5` 背景的電路板圖片變成白色 | 圖片是 Material Editor 換上的 `_MainTex`（JPEG，存在場景的 `TextureDictionary`）。Material Editor 把這個字典和其他清單一樣存成 msgpack bytes，我們只當成 map 讀，所以場景裡每一張換過的貼圖都被丟掉（`material_edits` 是空的）。改成先把 bytes 解開；舊存檔的純 map 照樣讀。另外，沒有原生 Forge 程式的通用零件材質把主貼圖參數叫 `KKAppearanceMain`，Material Editor 的 `_MainTex` 找不到參數就沒套上；現在 `_MainTex` 找不到時改套 `KKAppearanceMain`。修正後背景與原作一致（`F:\OpenKoikatsu\Saved\OKStudio\metex1\87688028_p5.png`） |

尚未一致：
- 鏡子的倒影沒有依 `m_ReflectLayers` 篩圖層（目前拍到全部物件）。2026-10-31 查：17 張用鏡子的場景都是遊戲本身的鏡子道具（`p_koi_stu_marukage00_00`、`mo_koi_mirror00`，在 `abdata/studio/00.unity3d`），這個 bundle 裡沒有任何帶反射圖層欄位的 MonoBehaviour（逐一掃過，鏡子物件上只有 ItemComponent），所以沒有可讀的圖層設定，倒影拍全部物件就是預設行為；之後若遇到自帶 MirrorReflection 腳本的 mod 鏡子再讀它的 `m_ReflectLayers`。
- ~~通用零件材質的 `_MainTex_ST` 是建材質時寫死的常數，Material Editor 改貼圖縮放／位移套不上（vmd 有一張場景的 `m_koi_stu_kawa00_00`）。~~ 2026-10-31：通用零件材質（`MakePartMaterial`）取主貼圖的四處座標改成讀向量參數 `_MainTex_ST`（預設值是材質自己的值，`MakeUnityMaskUVParameter`），Material Editor 的 `MainTex` 縮放／位移（`texture_st`，套成 `_MainTex_ST` 覆寫）就套得上。例：`83283571_p1` 的 `m_koi_stu_kawa00_00` 縮放 2.5 與 2.0——這個材質其實是 `Xffect/heat_distortion_t2`（畫面相乘的水面，`OKModulate` 材質），它的參數原本叫 `MainST`，也改名成 `_MainTex_ST`；修正後 log 不再列為 unapplied（`F:\OpenKoikatsu\Saved\OKStudio\mainst3\run.log`，這個視角下畫面幾乎沒變）。`OK material overrides=… unapplied=…` 這行現在另外註明原因：`(no matching part)` 或 `(parameter not in <母材質>)`。同一張場景的 `water_square` `_RimColor` 落在 `WorldGridMaterial`（材質沒建成，用了預設材質），待查。
  - 2026-10-31 `water_square`：mod `DA.xv2stuff` 的水面（Water Plane 1）用第三方著色器 `StylizedWater/Desktop`，沒有主貼圖、也沒有對應的材質圖，所以零件沒有材質。從 zipmod 的 bundle 取出 19 個 DXBC 程式與 `m_ParsedForm` 各子程式的常數名稱／位移，照 ForwardBase DIRECTIONAL 的 PS（blob 4）寫成 `OKStylizedWaterMaterial`：兩次捲動的 `_Normals`（世界或 UV 鋪排）、相機光的 GGX、依水深加深的 `_WaterColor`、交界的 rim 與 `_Shadermap` 泡沫、`_FresnelColor`、表面高光，最後 lerp(依法線折射的 GrabPass, 光照色, alpha)。GrabPass 對到 SceneColor 節點（OffsetFraction 帶折射位移），`_CameraDepthTexture` 對到 SceneDepth／PixelDepth。`okmodel_view` 讓這個著色器也載入 `_Normals`、`_Shadermap`（和 Standard 系列同一段）。結果地面從灰色預設材質變成和原作一樣的青色水面（`F:\OpenKoikatsu\Saved\OKStudio\water1\83283571_p1.png`），Material Editor 的 `RimColor`／`RimSize` 也套上了。沒做：`_Reflection` 立方體貼圖（沒有立方體貼圖的轉出，反射項為 0）；環境光用固定的 `_OKAmbient` 0.2。
- `83572034_p1`：舞台後方缺月亮背景、兩側藍色發光道具顯示成灰白，舞台中間的紅色板子原作沒有。原因是 Material Editor 換著色器（`MaterialShaderList`）：全部 7,694 張場景的統計是 `Shader Forge/main_item_emission` 362 筆／14 張場景、`main_item_studio` 56／2、`main_item_studio_alpha` 31／3、`Unlit/Glitch3DProjection` 15／2（藍色發光手把）、`Goo` 8／1、`main_item` 7／4、`IBL_Shader` 3／1、`HarvexARC-XRay` 2／2（中間的紅板，原作是透視、看得到後面的月亮）、`main_opaque` 2／1、`HarvexARC-Gem` 1／1。`okmodel_view::ItemMaterialEdits::shader_supported` 原本不收這些著色器，一律保留原著色器。處理（KKS 原生畫面 `F:\OpenKoikatsu\Saved\Tests\kks_p1emission_1030\Scene1.png` 為準）：
  - `main_item_emission`：Unreal 共用母材質已有 ForgeEmission 程式。第一次加進去時牆變白／粉——不是 ramp 的問題：控制通道新增 `parts` 的 `params`／`has_param`（列出零件材質的參數值）一看，換成 ForgeEmission 的零件 `_Color` 停在 1、`_ShadowColor` 停在原材質的粉色。Studio 的 ItemComponent 不管換成什麼著色器都照樣寫道具顏色（`_Color`..`_Color3`、`_ShadowColor`），我們只對 `main_item_studio` 的零件套道具顏色（`SessionPart::item_draw`）。改成：Material Editor 把 `main_item_studio` 換成別的著色器時記 `_OKStudioItemColors`，零件仍算道具顏色零件。修正後牆面和地板是黑的，與原生一致。
  - `HarvexARC-XRay`：反組譯 bundle 的 PS：`c = Inside·s + 0.5·max(1 − n_z^RimSize, 0)·(Rim − Inside·s) + Emission`，`alpha = s`（`s = _InsideSize/4`），兩個 pass（SrcAlpha 混合、One One 疊加）、不寫深度、不剔面，合起來是 `dst·(1−s) + c·(1+s)`，用一個 AlphaComposite 材質（`OKXRayMaterial`）畫。
  - `Unlit/Glitch3DProjection`：反組譯 PS（區塊雜訊、Fresnel、世界高度掃描線、閃爍貼圖調整 alpha），`OKGlitchMaterial`；頂點程式偶發的故障位移沒有做（靜止狀態）。程式的色調是 `_Color4`，不是 `_Color`（Studio 會寫 `_Color`）。
  - Material Editor 換著色器時會套 zipmod manifest 裡 `<Shader><Property DefaultValue>` 的預設值：Glitch 的 `Color4` = (0.37, 0.84, 0.93, 0.2)、XRay 的 `InsideColor` = (0,0,0,1)、`EmissionColor` = (0,0,0,0)；材質圖的預設值照這些設定。
  修正後中間的 XRay 板子透出後面的夜空，兩側的藍色全像板、手把、主機都是青色故障效果，與原作一致（`F:\OpenKoikatsu\Saved\OKStudio\mesh6\83572034_p1.png`）。仍缺月亮：月亮與夜空罩是 `main_item_studio_alpha` 的半透明道具，原生這個著色器寫深度（ZWrite On），夜空罩後畫也蓋不掉較近的月亮；Unreal 的半透明不寫深度，夜空罩就蓋掉了月亮。
  - 2026-10-31 修正（通用）：著色器的寫深度從 bundle 讀出——`unity::shader_pass_state` 多記主 pass 的 `zWrite`（固定值或綁定的材質屬性），材質記成 `_OKShaderZWrite`，`shader_states.json` 也收（`ok-extract <遊戲> --oklib shader-states <基礎庫>` 重產，舊檔沒有這欄就當不知道；本機重產 679 種著色器：172 關、300 開、207 看材質屬性）。混合繪製又寫深度的道具零件記進 `AKKPreviewActor::ZWriteBlendedParts`；`AOKSceneActor::PlaceBackground` 每幀看視點是否在零件的包圍盒內：在裡面的（夜空罩這類殼）排序優先度改成 -5000 減半徑，在天空盒之後、其他半透明之前畫，大的殼先畫；離開就回到原本的優先度。理由：Unity 依包圍盒中心由遠到近畫，包住視點的殼中心很近、最後才畫，但寫深度讓它留在被包住的東西後面；不改材質，cook 後的共用母材質也適用。限制：沒包住視點的寫深度半透明零件之間仍照 Unreal 的距離排序（不寫深度的差異只在兩者重疊又排序相反時）。結果月亮出現（`F:\OpenKoikatsu\Saved\OKStudio\moon1\83572034_p1.png`）；原作月亮只有月牙亮、其餘暗，我們整面亮，屬另一個著色差異，待查。vmd 57/57 載入（`F:\OpenKoikatsu\Saved\OKStudio\scenecheck_vmd13`），和前一輪比明顯變化的：`86003286_p4` 600 m 的夜空罩（`stgd2ns051_sky`）先畫，背景從粉紅變成和原作一樣的深藍（單獨重跑結果相同，`F:\OpenKoikatsu\Saved\OKStudio\chess1`）；`1537365251587` 在整批裡海面變亮，單獨跑與換成同樣前一張場景重跑都和上一輪一致（`beach1`、`pair1`），屬一次性的拍攝時機差異；`1565534473667` 只差眨眼。
  - 2026-10-31 `shader_states.json` 自動重產：檔案多記 `version`（目前 2：含寫深度）與 `inventory`（遊戲 bundle 與 zipmod 的數量、總大小、最新修改時間）。工作室場景 Actor 開始時（`AOKSceneActor::BeginPlay`）以設定的遊戲目錄（`KKGameRoot` 的 `Root`）呼叫 `ok_studio_shader_states_ensure`：檔案不存在、版本舊或 inventory 不符（換了遊戲或裝了新 mod）時，在背景執行緒重讀全部 shader 寫回（寫暫存檔再改名；`shader_states.json.lock` 防兩個程序同時跑，超過兩小時的鎖視為失效）；讀取端依檔案修改時間自動重載，執行中的遊戲在重產完成後開的道具就用新資料。不用再手動跑 `shader-states`。本機實測：舊檔（無版本）在工作室啟動後約 4 分鐘換成 version 2。限制：沒有遊戲目錄的環境無法重產；OKStudio 模組裡的 ok_extract 沒註冊 log 回呼，重產訊息不會出現在遊戲 log。
  - 月亮的月牙差異（未修）：月亮 `YK_Yue` 是 8 個三角形的看板，貼圖 alpha 平均只有 32/255，只有右緣接近不透明。Unity 先畫月亮（中心較遠）並在整個看板寫入深度，後畫的夜空罩在月亮範圍內深度測試失敗，所以月亮半透明的部分疊在黑色背景上，只看到亮的月牙；我們讓殼先畫，月亮疊在深藍夜空上，整個圓盤看得到。要完全一致，得讓後畫的寫深度半透明零件逐像素避開較早畫、較近的寫深度零件（custom depth＋依 Unity 繪製順序的排名），這要改共用母材質（`M_ForgeStudioAlpha` 等）並重存母材質、各線重新 cook，留待之後處理。`model-view` 加上診斷用的 `OK_MODEL_VIEW_PNG=<資料夾>`，可把各網格的主貼圖寫出。vmd 57/57 通過（`F:\OpenKoikatsu\Saved\OKStudio\scenecheck_vmd12`）；另外兩張場景用到：`91204049_p0` 一個 Glitch 方塊、`86703995_p3` 的 penthouse 一個 XRay 材質（KKS 原生也是 XRay，`F:\OpenKoikatsu\Saved\Tests\kks_xray_86703995\Scene1.png`）。`86703995_p3` 換成 XRay 後露出後方一些白色物件，這張場景本來就和原生差很多（藍色天空塊、地板顏色），待查。
  - 2026-10-31 逐像素修正（通用，取代上面「包住視點的殼先畫」）：
    - 材質：`OKAddUnityTranslucentDepthTest`（`KKRuntimePose`，`KKRuntimeVertexAmbient.cpp`）把半透明材質的 opacity 包一層測試。零件在 custom primitive data 0 帶自己的 Unity 繪製名次（1..255；0 不測）；某像素的 custom depth 比自己近、而且 custom stencil（畫那個深度的零件名次）比自己早，這個像素就丟掉。材質允許半透明寫 custom depth，`OpacityMaskClipValue` 設 0：Unity 的 ZWrite 對程式沒丟掉的每個片段都寫深度，不論 alpha。共用母材質（`PrepareRuntimeSharedMaterial.cpp`，母材質根目錄改成 `StudioDisplay29`，`DefaultGame.ini` 加進 cook 清單）與自建的半透明圖（`MakePartMaterial`、頂點色、ForgeAlpha／ForgeOpaque、鏡面、XRay、Glitch、水面、粒子、舞台特效、modulate）都套用。
    - 場景：`AKKPreviewActor::BlendedParts` 記角色以外的混合零件（半透明、加算、modulate），附 Unity render queue（`ok_part_material_value` 的 `_OKRenderQueue`；-1 用著色器的，天空盒是 1000）、是否寫深度。工作室模型另記 `_OKRendererGroup`／`_OKSubmesh`（`okmodel_view`，卡片路徑不加）。`AOKSceneActor::RankBlendedParts` 每幀依 Unity 順序排名：先比 queue，再比距離（2500 以下由近到遠，以上由遠到近）。同一個 renderer 的 submesh 用 renderer 整體包圍盒中心的距離，同距離時依 submesh 順序。名次寫進 custom primitive data；寫深度的零件另開 custom depth，stencil 寫名次（`r.CustomDepth 3`）。UE 實際的繪製順序也跟著調：同一 renderer 的 submesh 用 `TranslucencySortDistanceOffset` 拉到 renderer 的距離；各 queue 依序每級加減 100 個 sort priority，3000 不變，保留零件原本的堆疊優先度。
    - 月亮（`83572034_p1`，`F:\OpenKoikatsu\Saved\OKStudio\moon10\83572034_p1.png`）：Night Field 的地面、夜空殼、月亮是同一個 renderer 的 submesh 0／1／2。Unity 先畫殼、再畫月亮，月亮疊在殼上。上面「Unity 先畫月亮」的推測是錯的：先畫月亮的話，殼在月盤處被深度擋掉，會透出 447 m 外的白色 Skybox1，實測整個月盤變白（`moon5`）。目前月盤的深藍比原作亮：月盤 alpha 約 0.18，混合模式和原作一樣是 SrcAlpha／OneMinusSrcAlpha（從 bundle 讀出），差異的來源還沒找到。
    - 中途退步並已修正：拿掉「殼先畫」後，`86003286_p4` 的背景變成天空殼的粉紅色。原因是兩個放大的圓影（`o_shadow`）queue 4999，Unity 在殼之後才畫，UE 原本只看距離。改成依 queue 調整排序後恢復深藍（`F:\OpenKoikatsu\Saved\OKStudio\chess4\86003286_p4.png`）。
    - 只在主視角測：名次是依主視角算的，鏡面、螢幕這類 SceneCapture 從別處看，套用會錯丟像素（`1547385457380` 萬花筒兩側原作有的藍色光條不見了）。custom primitive data 1–3 另存排名用的相機位置，材質只在畫面相機和它的距離小於 1 cm 時測。modulate、加算混合不讀 opacity，UE 會把掛在 opacity 上的測試節點整個省略（`1547730231799` 的 `Xffect/heat_distortion` 地面在沙灘上蓋了一層灰），所以 emissive 也乘一個做同樣測試、回傳 1 的節點。`OK_UNITY_DEPTH_TEST=0` 可關掉排名（診斷用）。最後一輪 vmd 57/57（`F:\OpenKoikatsu\Saved\OKStudio\scenecheck_vmd17`）：和 vmd13 比，`86003286_p4`、`1547385457380`、`1585406034802` 一致；`1537365251587` 出現原作有的海浪線與近岸淺水層；`1547730231799` 的沙色更接近原作；`83283571_p1` 的水面是原作的青綠色。
    - 診斷：控制通道 `parts` 加 `owner`（只列或只隱藏某個預覽 actor 的零件）。log 的 `OK blended part=` 列出每個混合零件的 queue、寫深度、renderer、submesh；`OK sky dome` 多印 queue。
  - 2026-10-31 立方體貼圖轉出（通用）：`ParsedBundle::cube_slot_atlases` 把材質裡指向 Cubemap 的欄位（依名稱比對 bundle 與相依檔的 Cubemap）轉成 cube atlas（`cubemap_atlas`：六面並排，mip 往下），放進 `shader_extra_textures[<欄位>]`。`attach_material_texs` 與 `okmodel_view` 都會跑；原本只有 Skybox/Cubemap 的 `_Tex` 有轉。C++ 端的 atlas 取樣抽成 `OKCubeAtlasSample`（D3D 立方體面規則，Unity 空間方向），天空盒與水面共用。`StylizedWater/Desktop` 照 DXBC 加上反射項：`reflect(-V,n)` 取 `_Reflection`，乘 `_Glossiness`、`saturate(1-roughness)`、`(1-n·v)^5`，以 sRGB 載入。共用母材質（`SharedShaders/*Bindings.inl`）的 cube 貼圖只有 `unity_SpecCube0／1`，沒有材質欄位的 Cubemap；它們由 `OKSceneActor` 綁上地圖反射探針轉出的真正 `UTextureCube`（`OKStudioMapReflection`），不需要改走 atlas。材質欄位的 Cubemap 只出現在自建圖的家族（天空盒、水面），已走 atlas。
  - 2026-10-31 實測水面（`83283571_p1`，[DA]xv2stuff 的 Water Plane 1）：`_Reflection` 原本還是黑色 32×32，三個原因都修了。(1) 這個模型在 atlas 功能之前就轉好，`meshes_for_maindata` 記錄裡的材質沒有 `_Reflection` 欄位（當時外部 bundle 的 Cubemap 取不到名稱）；`studio-mod-nodes` 改成和 `studio-nodes` 一樣，可用 `OKLIB_REFRESH=meshes_for_maindata` 重錄已有的網格記錄。(2) 函式庫模型 replay 時 `cube_slot_atlases` 會讀 bundle 內部而記一筆「未 memo」缺口；改成 cube atlas 是獨立的 memo 查詢 `cube_atlas_named`（只記真的 Cubemap），replay 時有記錄才用；`studio-nodes`／`studio-mod-nodes` 會補錄。(3) `SessionPart::from_mesh` 對一般 shader 只留白名單內的貼圖，`StylizedWater/Desktop` 不在名單上，`_Normals`／`_Shadermap`／`_Reflection` 全被丟掉（水面法線其實也一直沒接上）；加了水面的分支。結果：`_Reflection` 是 3072×1024 的 atlas，`_Normals` 256×256、`_Shadermap` 1024×1024，水面出現波紋與反射（`F:\OpenKoikatsu\Saved\OKStudio\water1031\83283571_p1_after.png`，之前 `F:\OpenKoikatsu\Saved\OKStudio\scenecheck_vmd17\83283571_p1.png`）。這張場景整體過曝（火焰、泛光）是既有問題，另外查。診斷：`ok-extract X:/NoGame --oklib model-view <lib> <id> session:<prefab>` 列出 session 各部件的 shader 貼圖。本機只對這張場景的 mod 道具跑了 `OKLIB_REFRESH=meshes_for_maindata ok-extract D:/KoikatuSunshine --oklib studio-mod-nodes <base_v1> F:/OpenKoikatsu/Saved/OKStudio/mainst_in 4`（第三方 mod 模型不進版控）；原版道具與地圖的 cube 欄位要在整合時跑 `studio-nodes` 補錄進共用基礎庫。

  - 2026-10-31 過曝與月盤偏亮：用 `Tools/GamePoseProbe/run_scene_visual_audit.lua --tag kks_bright_1031` 拍了原生畫面（`F:\OpenKoikatsu\Saved\Tests\kks_bright_1031\Scene1.png`、`Scene2.png`），兩者**不同源**，也都不是泛光或曝光：
    - `83283571_p1` 的白天空：原生天空盒是 mod 的 `Funly/Sky Studio/Skybox/3D Standard`（`m_skybox_simple_daytime`，粉紅夕陽），OK Studio 沒有這個 shader，畫成白色；關掉泛光（`ShowFlag.Bloom 0`）仍是白的（`F:\OpenKoikatsu\Saved\OKStudio\bright1031\b_nobloom.png`）。另外火焰粒子比原生大很多、鋼架被橘光照亮，要分別查粒子尺寸與點光強度。
    - 月盤：原生是新月，未照亮的半邊全黑；我們整個月盤以暗藍顯示。月亮是 `main_item_studio_alpha`（`YK_Yue`），泛光參數（強度 0.79、門檻 0.903）也不高，比較像是月亮貼圖暗面的 alpha 或該 shader 的明暗項沒對上，下一步是讀 `YK_Yue` 暗面像素的 RGBA 與 OK 的 alpha 混合結果逐像素比。
    - 待做：Funly Sky Studio 天空盒（照 DXBC 自建圖，和 `Skybox/Cubemap` 同一條路）、火焰粒子尺寸、月盤暗面。
    - 2026-10-31 更正：`83283571_p1` 畫面上的天空不是 Funly 天空盒（Funly 3D Standard 早已由 `OKStudioSkyMaterial` 支援，而且畫在 100 m 外最遠處），是 mod 道具 `[nashi]Studio Skybox` 的 Skybox4（`Skybox/Cubemap` 天空球）。逐像素比對：原生約是貼圖原色的 1.5～1.8 倍（含泛光），我們乘了 3.62。這個 mod 自帶的 `Skybox/Cubemap` 是在 gamma 專案編譯的：DXBC 是 `add o0.xyz, r0, r0`（unity_ColorSpaceDouble 編進程式成 2），而 KKS 內建的是線性的 4.5948，所以天空亮了 2.3 倍；景深與泛光再把這片過亮的天空擴散到鋼架上，整張畫面發白。修正：轉換時讀 bundle 自己那份 `Skybox/Cubemap` 的像素程式，有 4.5948 常數就是線性、沒有就是 2，存成材質值 `_OKSkyDouble`（memo 查詢 `skybox_color_space_double`）；UE 的天空球乘它而不是寫死 4.5948，bundle 沒帶自己的 shader 時照舊。結果：`F:\OpenKoikatsu\Saved\OKStudio\bright1031\f_fixed.png`（之前 `a_base.png`，原生 `F:\OpenKoikatsu\Saved\Tests\kks_bright_1031\Scene1.png`），天空回到粉紅夕陽、鋼架不再泛白。本機用 `OKLIB_REFRESH=meshes_for_maindata ok-extract D:/KoikatuSunshine --oklib studio-mod-nodes <base_v1> <這張場景的資料夾> 4` 重錄了這個 mod 模型；其他已轉的 `Skybox/Cubemap` 天空球要用同樣的方式重錄才會帶上新值（沒帶時照舊是 4.5948）。剩下的差異是中央光束一團白（光束與火焰道具的大小、點光強度），另外查。
    - 同一輪：`blend_for_mesh` 對 render queue ≥ 3000 的材質一律判成半透明。這張場景的鋼架原本是 alpha 材質，Material Editor 換成 `main_item_studio` 後 queue 仍是 3000，被當成半透明（log 裡 205 個 `main_item_studio queue=3000` 的混合零件）。`main_item_studio` 本身是 One/Zero、寫深度（`_ALPHATEST_ON` 版本會 clip），queue 只決定順序，改成照 shader 判斷：有 alpha test 是 masked，否則 opaque。`verify_ok_studio.lua` 6/6、`cargo test blend` 通過。
    - 查中央白團與右側鋼架的橘白光時的發現（還沒修）：(1) 逐類隱藏道具，只有隱藏 Skybox4 天空球時白團與橘光才消失，光束、火焰、煙、Gas 都不是來源；放大比對，鋼架本身是暗的，是太陽附近過亮的天空在鋼架間透出、再被泛光擴散。原生同一處的天空偏粉紅，整體像多了一層粉紅色調（可能是 AmplifyColor LUT 或雲層），下一步是比對原生的 LUT 與雲層零件（`mo_koi_sky_kumo*`，Standard queue 3000）。(2) 控制通道 `exec` 下的 `ShowFlag.Bloom 0`／`ShowFlag.DepthOfField 0` 不影響 `capture`（它用自己的 SceneCapture 與場景後處理），之前「關掉泛光仍是白的」那張 `b_nobloom.png` 其實沒有關到；要分離泛光得改 capture 或用 `ui_shot`。(3) 原生的光束道具（ビーム・扇）畫成放射狀光線，我們看不到光線。診斷：`OK_SKY_DEBUG=2`（原色）、`4`（alpha）、`5`（×alpha）；`cargo test cubemap_info_dump -- --ignored`（`OK_CUBE_BUNDLE`）列出 bundle 內 cubemap 的格式、編碼與色彩空間。

### U16～U22　對照 HS2Studio6 的第二批缺口（2026-10-31，F 組，依使用頻率排序）

2026-10-31 對照第 3 節與 `Saved/StudioUIRef/hs2/` 逐項讀程式盤點（30 項：9 項沒有、17 項部分完成），整理成以下項目。排序依擺姿勢與擺場景時每分鐘會碰到幾次。

| 階段 | 內容 | 目前缺的 |
|---|---|---|
| U16 角色細調頁 | 表情：眉／眼／嘴顯示目前編號＋上一個／下一個、眼睛開合滑桿；手勢：左右手上一個／下一個／復位與「目前／總數」；視線與頸部：加「正面」「背離」模式、「取鏡頭位置」「恢復看鏡頭」、頸部跟隨強度；遊戲內建姿勢：「n / 總數」上一個／下一個、「復位 T-pose」 | 現在只有下拉與數值框；沒有正面／背離；內建姿勢清單沒有 |
| U17 衣服一鍵 | 狀態窗加「全穿／半脫／全裸」 | 只有逐部位三態 |
| U18 動作瀏覽與控制器 | 全部／女／男／共用頁籤、搜尋（譯名與原名）、項目數；獨立的「動畫控制器」浮窗（速度、動作變化、輔助 1／2、循環、播放／暫停／停止），關掉清單仍留著 | 只有群組下拉與清單；控制只有內嵌滑桿 |
| U19 物件樹操作 | 群組／解散群組（Ctrl+G；吸附改 Ctrl+Shift+G）、帶位移複製、只顯示所選（solo）、鎖定不被視窗點選、拖動時靠邊自動捲動；底部工具列補「設父級」「複製變換」「材質編輯」；道具／動作／地圖／特效目錄的「定位當前」 | 沒有群組、solo、鎖定、自動捲動 |
| U20 FK／IK 部位開關 | 每個部位真的啟用／停用 FK 與 IK（不只篩選清單）、各部位初始化／復位、IK「全部」；新角色預設 IK 開、FK 開胸／身體／雙手／裙子；IK 權重頁；明確的 FK←IK | 部位鈕只篩選清單；沒有權重 |
| U21 時間軸骨骼軌 | 骨骼搜尋＋單骨打 key／刪 key、「清空軌道」 | 只能整個物件打 key |
| U22 其他 | 相機正交與正交大小、遠裁面套用；視窗格線／地面開關與「隱藏全部輔助」；場景統計（物件／角色／道具數）；最近開啟的場景；返回標題；面光源；背景圖瀏覽與「套用」；材質編輯器三欄窗；`?` 說明圖示；截圖窗的前景框選項與「本次目錄」 | 見各項 |

**完成紀錄（2026-10-31，F 組）**
- U16（部分）：
  - 表情的眉／眼／嘴、手勢的左右手：清單下方加「◀ ▶」與「目前／總數」（`SOKStudioPanel::Named`，從 -1「照卡片／照動畫」循環）；手勢加「復位」。
  - 視線與頸部加「正面」「背離」模式（`EOKSceneLook::Front`／`Away`，加在列舉最後，舊檔不受影響）：正面是角色正前方頭高的遠點，背離是鏡頭對頭部的鏡射點；控制通道也接受 `front`／`away`。另加「取鏡頭位置」（注視點設為目前鏡頭位置並改成注視模式）與「恢復看鏡頭」。
  - 還沒做：眼睛開合滑桿（`AKKPreviewActor` 只有 `SetMouthOpen`，眼睛沒有後端）、頸部跟隨強度、遊戲內建姿勢清單。
- U17：狀態窗「全穿／半脫／全裸」一鍵。
- U18（部分）：動作瀏覽改用主題按鈕（目前分頁亮綠），加搜尋（空格＝且、`-詞`＝排除；比對動作名與分組、分類名，結果跨分組並標出所在分組／分類）與「N 個動作」計數。修正：每次重建都把分頁同步回目前播放的動作，播 Studio 動作時按「H 動作」會被拉回；改成只在目前動作改變時同步。還沒做：女／男／共用頁籤（目錄沒有性別資料）、獨立的動畫控制器浮窗。
- 截圖：`F:\OpenKoikatsu\Saved\StudioUIRef\ok\U16b_state.png`、`U16b_look.png`、`U16b_expression.png`、`U16b_hand.png`、`U18b_anim.png`（腳本 `Saved/StudioUIRef/scripts/u16_new2.txt`，`OKStudio.RunScript` 要給絕對路徑）。`OKStudio.UiSelfTest` 46/46。
- U16 其餘：表情頁「睜眼」（`FOKSceneCharacter::EyesOpen`，-1 照角色；接到 `AKKPreviewActor::SetFaceControl` 的手動開合，-1 時還原原本的開合與手動旗標）；頸部「跟隨強度」（`NeckFollow`，目標方向從頭的正前方 slerp 過去）；姿勢頁「◀ n／總數 ▶」逐一套用 `UserData/Studio/pose` 的姿勢檔（KKS 沒有內建姿勢預設，HS2Studio6 的內建清單在 KK 對應的就是 Studio 姿勢檔）與「復位 T-pose」。
- U18 其餘：動作瀏覽加「全部／女／男／共用」頁籤。目錄原本沒有性別：`studio_anims.rs` 的 `motion_sex` 從分組／分類名推（含「男」→ 男；含「女」或「レズ」→ 女；其他 → 共用），寫進目錄的 `sex` 欄位；舊目錄沒有欄位時 C++ 讀取端用同一規則推。動畫控制器浮窗還沒做。
- U19：樹工具列（展開／收起下方）加「群組」（Ctrl+G，所選放進第一個物件旁的新資料夾，世界位置不變；吸附改 Ctrl+Shift+G）、「解散」（Ctrl+Shift+U，子物件移到資料夾的上層後刪掉資料夾）、「錯開複製」（Ctrl+Shift+D，複製後沿畫面右方移 20 cm）、「獨顯」（Shift+H，只顯示所選與其上下層，燈光與目前相機保留；再按一次還原各物件原本的顯示）、「鎖定」（`FOKSceneObject::bLocked`，畫面點選略過它，樹上仍可選）、「設父」「拷變換」「貼變換」。樹拖放時游標靠清單上下緣 20 px 會自動捲動。目錄的「定位當前」還沒做。
- U20：FK／IK 部位按鈕改成真的開關（`FkPartsOff`／`IkPartsOff` 位元，關掉的部位保留數值、改跟動畫；清單只列開著的部位）；FK 頁加「復位」一列逐部位清掉 FK；IK 頁加「全部」；新角色預設 FK 關頭髮與頸部、其他與 IK 全開；調某根骨骼時自動打開它的部位。IK 目標加「權重」（`FOKSceneIkTarget::Weight`，經新的 `ok_session_set_studio_ik_weights` 送到 FullBodyBipedIK 的 effector position／rotation weight 與彎曲 goal weight）；加「FK←IK」（把 IK 擺出的身體與雙手姿勢寫成 FK，清掉 IK）。
- U21：時間軸 FK 改成每根骨骼各自一條軌（`SampleTrackFk`：找 Time 前後最近、含這根骨骼的 key 內插，只出現在部分 key 的骨骼也會平順地動）；FK 頁加「骨骼打 key」「骨骼刪 key」（只動目前骨骼，沒有 key 的時間點新增一個只含這根骨骼的 key，其餘值取該時間點的軌道值），時間軸窗加「清空軌道」。骨骼搜尋沿用 FK 頁的搜尋框。
- U22（部分）：相機物件加「正交」與「正交寬度」（`FOKSceneCamera::bOrtho`／`OrthoWidth`；用這台相機時 `UOKStudioViewCamera` 改成正交投影，遠面在正交時套用，透視時 Unreal 沒有遠裁面，仍只存檔）；相機頁加「視窗格線」（場景地面 10 m、50 cm 一格，線段分段投影，穿到鏡頭後方的部分截掉）與「隱藏全部輔助」（把手、骨點、注視點、光源範圍、選取框、格線都不畫，把手與骨點也點不到）；系統選單加「最近場景」浮窗（上方是場景統計：各類物件數與時間軸 key 數；下方是最近開啟或儲存的 10 個場景，`favorites.json` 的 `recent_scenes`），「離開」改名「返回標題」（行為本來就是回到啟動工作室的模式）。控制通道的 `click` 也能按勾選框。截圖 `F:\OpenKoikatsu\Saved\OKStudio\mcp_ui\u22_ortho_grid.png`。還沒做：面光源、背景圖瀏覽與套用、材質編輯器三欄窗、`?` 說明圖示、截圖窗的前景框與本次目錄。
- U22（第二批）：光源加「面光」類型（`EOKSceneLightType::Area`，`URectLightComponent`，寬／高 cm 預設 100；和點光、聚光一樣只照地圖與 Standard 部件，卡通角色與道具仍只吃一盞方向光），添加光源頁加「面光源」四種，視窗裡畫出發光矩形與朝向；畫面設定「背景／前景」分頁加「背景圖一覽」「前景框一覽」縮圖格（基礎庫 `studio/bg`、`studio/frame` 與 `UserData/Studio/bg`、`frame`，點一下立即套用，綠框標出目前的，「無」拿掉）；截圖窗加「含前景框」（關掉時拍攝期間把前景框藏起來）與「本次目錄」（這次啟動的截圖放進 `session_<日期時間>` 子資料夾，「開資料夾」也開那裡）。控制通道 `state` 多回報燈光 `light_type`、`background`、`frame_picture`，`ui` 多回報 `shot_frame`、`shot_directory`。還沒做：材質編輯器三欄窗、`?` 說明圖示。
- U22（第三批）與 U18／U19 剩餘：
  - 材質編輯器浮窗（`app.material`；樹工具列「材質」、道具屬性頁「材質編輯器…」）：左欄是所選道具的模型（renderer），中欄是它的來源材質與 shader，右欄是那個材質的全部顏色、數值與貼圖槽（值取自卡片 bundle 的材質，可篩選屬性名）。改值寫成道具的材質編輯（隨場景儲存，名稱前標 ●），每列「預設」拿掉該筆，底部「此材質全部預設」。浮窗跟著選取與道具載入完成自動更新。
  - `?` 說明：共用的 `HelpIcon`，懸停顯示、點一下固定在「說明」浮窗（再點一次收起）；設定窗每列、屬性列（`RowHelp` 表：燈光種類、範圍、聚光角、作用對象、面光寬高、正交寬度、透明度、描邊寬度、背景圖、前景框、背景色）與截圖窗「含前景框」都有。
  - 動畫控制器浮窗（`app.animcontrol`，動畫頁「動畫控制器」）：所選角色的動作名、速度、時間、◀ 變化／變化 ▶（同分類的上一個／下一個動作）、播放（回到暫停前的速度）、暫停、從頭、停止；即時讀選取，關掉動作清單也留著。輔助 1／2 與循環還沒有對應資料，沒做。
  - 定位當前：動畫頁「定位當前」（`anim.locate`，切到正在播的動作的種類／分組／分類並捲到它）、添加道具頁「定位當前道具」（切到所選道具的大／中分類並捲到它）。
  - 控制通道：`click` 加 `"tag": true`，比對控件的 `FTagMetaData`（縮圖與「無」掛 `bg:none`／`frame:none`／檔名 tag）；`state` 的角色多 `speed`、`anime_no`，`ui` 多 `material_part`、`material_properties`、`material_name`、`help`。
  - 驗證：`check_ok_studio_buttons.lua` 86/86（新增縮圖點選套用背景與前景框、兩個「無」、設定 `?` 固定與收起、樹「材質」開材質編輯器並列出 m_koi_stu_desk00_01 的 46 個屬性、此材質全部預設、定位當前道具、控制器播放／暫停／播放、動作定位當前）；UiSelfTest 46/46；`verify_ok_studio.lua` 6/6。截圖 `F:\OpenKoikatsu\Saved\OKStudio\mcp_ui\u22_material_editor.png`。
  - 定位當前讀回：`ui.anim_browse`（種類／分組／分類）、`ui.anim_selected`（清單選中的動作名）、`ui.item_browse`（大／中分類）、`ui.item_located`（捲到的道具）。實測：先切到別的分組或「H 動作」，按定位當前後回到正在播的 ポーズ7（0/7/20）、書桌（2/12/681）。按鈕檢查 89/89。
- 2026-10-31 畫面差異（先用診斷確認是誰畫的）：
  - `83283571_p1` 中央白團：逐項隱藏與關閉後處理確認是 SunShafts（`scene` 操作 `{"post":{"bSunShafts":false}}` 後白團和右側鋼架的橘白光都消失）。原生相機的順序是 BloomAndFlares → AmplifyColor → SunShafts，我們把光束和霧、AO 放在同一個泛光前的材質裡，加上去的光束再被泛光擴散成一整團。改成同一段程式跑兩個 pass：霧與 AO 在泛光前，SunShafts 在調色之後（`BL_SceneColorAfterDOF` priority 110）；每個取樣另外夾在 0～1（原生的光束來源是 8 位元 RT）。結果窗口後方出現一條條光線，白團消失：`F:\OpenKoikatsu\Saved\OKStudio\bright1031\u_shafts_after_grade.png`（之前 `r_before.png`）。
  - 「火焰太大、鋼架被橘光照亮」：對照舊圖，其實是過亮天空與被泛光放大的光束，上一項一起修掉。真正的火焰（mod 道具 Fire 1／Fire 4，分組 8422 的 ParticleSystem）目前完全沒畫（`bounds` 的 `built` 為 false、沒有部件）；ParticleSystem 不轉換是 10-17 的決定，要畫得改用 OK 的 Niagara 特效對應。
  - `83572034_p1` 月盤暗面：H_enry 的 `main_item_studio_alpha` 主 pass 是 `AlphaToMask On`（DXBC 確認沒有 0.5 clip，alpha＝貼圖 a×`_alpha`，SrcAlpha/OneMinusSrcAlpha、寫深度）。月亮貼圖（DXT5）暗面 alpha 0.17、月牙接近 1；原生用一個取樣畫，alpha 低於一半的像素不畫，所以只剩月牙。修正：`shader_pass_state` 讀主 pass 的 `alphaToMask`，記成 `_OKShaderAlphaToMask`；建零件時對仍是自己 shader（state key 相符）的 `main_item_studio_alpha` 依 8x MSAA 的 alpha-to-coverage 把貼圖 alpha 換成 alpha×round(alpha×8)/8（第一版是 alpha < 0.5 清成 0，把 `83283571_p1` 的柔和光束「ビーム・扇」整個砍掉，見 U27 記錄）。不透明的 `main_item_studio` 也帶同一個旗標但輸出 alpha 1，不能裁（第一版裁了，活塞頂變成白盤）。逐點：月盤內 (640,90) 原生 (0,0,3)、修正前 (2,7,63)、修正後 (0,0,0)；月牙 (800,120) 兩邊一致。`F:\OpenKoikatsu\Saved\OKStudio\moon1031\d_alpha_only.png`。已轉好的模型不必重錄：`shader_states.json` 改版到 3 也記 AlphaToMask，遊戲啟動時發現目錄版本舊就在背景重建（`shader_states::ensure_in_background`），讀模型時 `complete` 依 shader 名稱補上旗標（記錄時沒帶 AlphaToMask 的舊模型也補）；KKS 原版的 `main_item_studio_alpha` 同樣是 AlphaToMask On。
  - 同一張場景還有未修的差異：原生中央有一個玻璃房間（角色在白磚房裡），我們沒有；兩側螢幕的內容也不同。
- 驗證：新增 `Tools/check_ok_studio_buttons.lua`，經控制通道新的 `click` 操作（`{"text","index","contains"}`，找畫面上文字相符的 `STextBlock`，對它所在的 `SButton` 呼叫 `SimulateClick`，跑的就是按鈕自己的處理）逐一按下 U16～U21 新加的按鈕，再用 `bones`／`state`／`keys` 讀回結果。實測 57/57（加上 U22 後 65/65，第二批後 72/72）；`OKStudio.UiSelfTest` 46/46；`verify_ok_studio.lua` 6/6；`cargo test studio_ik --include-ignored` 通過。

### U23～U28　對照 HS2Studio6 的第三批（2026-10-31，F 組）

U16～U22 做完後，把各階段記錄裡「還沒做」的項目、第 3 節規格裡仍缺的部分與畫面比對的未修差異收在一起，依擺姿勢、擺場景時碰到的頻率排序。

| 階段 | 內容 | 目前缺的 |
|---|---|---|
| U23 角色細調補完 | 眼睛開合滑桿、頸部跟隨強度、遊戲內建姿勢清單（「n／總數」上一個／下一個、復位 T-pose） | **已在 87b9c7877 做完**（列表時漏查程式碼，誤列為未做） |
| U24 姿勢鏡像 | 所選角色的 FK 姿勢左右鏡像（含手指）、只鏡像左／右半邊；IK 目標同時鏡像 | **已在 87b9c7877 做完**；按鈕測試補上左→右讀回 |
| U25 動畫控制器補完 | 輔助 1／2（Studio 的 `animeOptionParam1/2`）、動作模式（`animePattern`）、循環開關 | **已做**：輔助與動作模式（下方記錄）、強制循環 |
| U26 mod 粒子道具 | mod 的 ParticleSystem 道具（`83283571_p1` 的 Fire 1／Fire 4）現在完全不畫：依名稱與貼圖對到 OK 的 Niagara 特效，對不到的列進讀取報告 | **已做**（下方記錄） |
| U27 場景比對剩餘 | `83572034_p1` 中央的玻璃房間不見、兩側螢幕內容不同；`83283571_p1` 原生整體偏粉紅（AmplifyColor LUT 或雲層零件）；光束道具ビーム・扇本身的放射光 | 玻璃房間是原生截圖工具的假差異；偏粉紅已修（cube atlas 面上下顛倒）；齒輪的放射光是 SunShafts，queue > 2500 的零件不在原生深度貼圖裡、要當光源，已修（下方記錄）；舞台前地板偏亮未查 |
| U28 效能與小視窗 | `run_ok_studio_perf.lua` 只開一個 Unreal 時量；1280×720 視窗下逐窗檢查不出框 | 小視窗**已做**（下方記錄）；效能未量（等全機只剩一個 Unreal） |

**U28 小視窗記錄（2026-11-01）**：控制通道新增 `ui_fit`（`{"open": 浮窗}`／`{"close": …}`／`{"page": n, "bones": k}` 開窗；不帶參數時回報）：走訪工作室畫布上每個顯示中的區塊（浮窗、詳情窗、外殼各欄），列出超出所屬視窗的元件、被截斷的文字（文字想要的寬度大於分到的寬度）、跑出畫面的視窗；捲動區與清單內只看寬度。刻意以省略號截斷的文字掛 `Ellipsis` 標記，不算問題。`Tools/ok_studio_ui_fit.lua --scene <有角色的 .okscene>` 逐一打開 21 個浮窗、詳情頁（骨骼 7 個子頁）並各拍一張 UI 截圖（`Saved/OKStudio/mcp_ui/<tag>_*.png`），有問題時結束碼 1。

- 1280×720（畫布 1600×900，介面依高度等比縮放，所以視窗框本身都在畫面內）第一次跑出 89 項：變換列數值顯示 6 位小數被截斷（每個詳情頁都出現）、骨骼頁的勾選框長文字超出右緣 35、`FK←目前姿勢` 按鈕、注視點與姿勢檔的說明、表情的「張嘴（-1 照動畫）」、角色卡／場景檔／姿勢庫縮圖下的長檔名、道具分類 `STR_MOD(キャラ)`、讀取報告與兩個瀏覽器的提示句、MMD 窗的偏移欄。
- 修法：變換列位置與旋轉顯示 1 位小數、縮放 2 位（SpinBox 的小數位只在建立時讀一次，依數值調整的寫法不會更新）；共用數值框最多 3 位；分段標題、列名稱、勾選框文字改成自動換行（勾選框的文字改放在方塊旁的平面按鈕裡，放在 SCheckBox 內會照全長排列不換行，點文字一樣切換）；縮圖檔名與分類名改成省略號並加完整名稱提示；提示句改換行；`FK←目前姿勢` 改成 `FK←姿勢`；MMD 偏移欄加寬。修完 0 項。

**U25 記錄（2026-10-31）**：OK Studio 播的是動作庫預先烘好的 `.okanim`，每個動作只有控制器預設值那一份，Session 的 Animator 參數 API 對它回 0 個參數（實測 60 多個動作）。改法：

- 轉換器（`ok-extract <遊戲> --oklib anim-convert <lib> <card> [filter] [threads]`）對 H 動作讀控制器，找出 blend tree 真正沿著哪些 Studio 控制變化（`anim::studio_blend_axes`：輔助 1＝`height`／`height1`、輔助 2＝`breast` 系、動作模式＝`motion`），在樹的門檻值與 0、1 的每個格點各烘一份 `<動作>@<控制>-<值>_….okanim`（預設值那點也重烘，不混用舊檔：舊檔可能是別張卡轉的）。轉完逐軸比對：沿某軸所有變體一模一樣（控制器有這個參數、取樣出來的動作卻不跟著變）就去掉那一軸，留第一個值的變體改名、其餘刪除。目錄 `studio_anims.json` 每個動作多記 `axes`（控制、參數名、預設值、取樣值）與 `variants`；讀控制器很慢（全部 H 動作約 20 分鐘），所以記 `blend_checked`，之後的轉換沿用。
- 全部 4,918 個 H 動作的控制器：4,318 個的樹用到身長（0、0.5、1）、558 個另有動作模式（0、1）、14 個用 `height1`、胸部參數沒有任何動作用到、26 個都沒有。實際取樣後比對（853 條軸輸出不變被去掉，例如 `2/112/*` 的身長 0～1 輸出完全相同，`h_motion_along_height`）：3,605 個沿身長、425 個沿身長＋動作模式、137 個只沿動作模式、751 個沒有變體。共 13,639 個變體檔、361 MB（`base_v1/anims/studio/*@*.okanim`，8 執行緒取樣約 3 分鐘；讀控制器約 20 分鐘只在第一次）。
- OK Studio：有變體的動作把格點組成巢狀 1D blend（每層一個控制，`Axis.param` 指定讀第幾個播放器參數，等於多線性內插）；`FOKSceneCharacter::AnimFloats` 以控制名（`option1`／`option2`／`pattern`）記值，沒設的用控制器預設值。動畫控制器浮窗列出這個動作有的控制（輔助 1（身長）、動作模式）與「預設」；KStudio 場景匯入帶入 `animeOptionParam1/2` 與 `animePattern`。控制通道：`state` 每個角色的 `anim_controls`（播放器目前的值），`pose` 的 `anim_controls` 設值（空物件＝全部回預設）。
- 強制循環（KStudio 的 forceLoop）：動畫控制器「強制循環」，播放時間對動作長度取模；KStudio 匯入帶入。
- **變體打包檔（2026-10-31 起進版控）**：13,639 個小檔改成一個打包檔 `base_v1/anims/studio_variants.okpack`（351 MB，Git LFS，`.gitattributes` 的 `base_v1/anims/*.okpack` 規則）。格式：`OKVPACK1`、u32 個數、依名稱排序的索引（u16 名稱長度、UTF-8 名稱、u64 偏移、u32 長度），後面接各變體的 `.okanim` 原始位元組；名稱就是 `studio_anims.json` 的 `variants` 裡的路徑（`anims/studio/<動作>@<控制>-<值>….okanim`），json 照舊記 `axes`／`variants`。OK Studio（`OKSceneActor.cpp` 的 `ReadStudioVariant`）先找同名散檔，沒有就讀打包檔：索引每個檔案只讀一次，單一變體依偏移讀出，不整包載入；打包檔不在時照舊播單一動作（沒有輔助／動作模式）。Rust 端是 `studio_anims::variant_pack`。打包檔讀出的 13,639 個變體和原散檔逐位元組相同；`studio_anims.json` 一起進版控。
- **重新產生變體**：乾淨 clone 建好 ok-extract（`cargo build --release --bin ok-extract --manifest-path Tools/ok-extract/Cargo.toml`）後，一條指令：

  ```powershell
  .\Tools\ok-extract\target\release\ok-extract.exe D:/KoikatuSunshine --oklib anim-convert Content/IndependentResources/base_v1 - hanime_ 8
  ```

  第一個參數換成自己的 KKS 遊戲目錄。`-` 表示用遊戲的預設角色取樣（不讀任何卡；取樣結果帶一點角色的基本姿勢，用同一個角色才能逐位元組重現，任何安裝都有預設角色）；也可以給一張 OK 卡或原作卡。轉換時先把打包檔展開成散檔（已經存在的會跳過、去軸比對也用它們），完成後全部寫回打包檔並刪掉散檔。只想把現有散檔打包時用 `ok-extract X:/NoGame --oklib anim-pack Content/IndependentResources/base_v1`。第一次要讀全部 H 動作的控制器（約 20 分鐘），之後 `studio_anims.json` 記了 `blend_checked` 就沿用；取樣約 3 分鐘（8 執行緒）；最後重建目錄約 20 分鐘。完成時印出 `OKANIM … controls dropped` 與 `OKANIM catalogue 5805 animations`。

**U26 記錄（2026-10-31）**：mod 粒子道具（只有 ParticleSystem）原本完全不畫。`studio_mod_items.json` 的道具若來自粒子包（bundle 路徑含 particle／effect／fx），另存一份（沒有轉出模型的也留，但不進道具瀏覽器）；KStudio 匯入時依名稱與 prefab 對 `effects.json` 新增的 `words`（最長的字優先，可寫成 `["cigarette", 0.1]` 帶縮放）換成 OK 的 Niagara 特效，對不到的列進讀取報告。`83283571_p1`：RSkoi 的 Fire 1／Fire 4 變成火焰、Gas 變成煙、Dust 變成光粉；手上的 Cigarette Smoke 第一版對成全尺寸的煙，畫面中央一大團白，加上 0.1 縮放後消失。`F:\OpenKoikatsu\Saved\OKStudio\u26\83283571_p1_fx2.png`。粒子包裡有模型的道具（螢幕、蝴蝶）照常當模型，只有沒模型又對不到的才進讀取報告。

**U27 記錄（2026-10-31）**：`83572034_p1` 中央「玻璃房間不見、兩側螢幕內容不同」不是我們的問題：場景的地圖是 33 號（空き部室，prefab `p_koi_stu_map49_00`），地圖位移 caMap 是 y＝-100，OK Studio 照樣放在地下 100 m；原生截圖工具（`kks_bright_1031\Scene2`）沒套 caMap，把房間畫在舞台中央，螢幕相機才拍到房間裡的她。場景 PNG 自己的 KK 縮圖中央是暗的、螢幕拍到舞台上的她，和 OK Studio 一致（`F:\OpenKoikatsu\Saved\OKStudio\u27\83572034_p1_a.png`）。

- 光束放射光：齒輪上的「ビーム・扇」（13/26/488）是 `main_item_studio_alpha` 的網格特效（AlphaToMask On），本來就有建，是 AlphaToMask 的第一版模型（alpha < 0.5 清成 0）把 alpha 0.3～0.5 的柔和光束整個砍掉。改成 8x MSAA 的 alpha-to-coverage：貼圖 alpha 換成 alpha×round(alpha×8)/8（`unity::alpha_to_coverage_png`）；道具透明度（Studio 的 `_alpha`）在 `AKKPreviewActor` 也照同一式換算。月亮暗面 (640,90) 現在 (0,1,7)，原生 (0,0,3)；第一版 (0,0,0)、修前 (2,7,63)。
- 偏粉紅：AO 的粉紅 Tint (0.954,0.727,0.727) 與粉紅的 `ambientShadow` (0.389,0.044,0.199) 都已讀進來。差異是逐區量出來的（1280×720，區域平均 RGB）：天空左側 原生 201,110,135／我們 208,123,148；鋼架 51,45,68／128,109,116，關掉 SunShafts 後 57,63,88。所以鋼架變亮全是光束後處理：我們太陽附近的天空偏白（(900,200) 原生 225,134,141、我們 230,219,200；(640,160) 252,182,163 對 249,249,232），白色天空減掉門檻後是飽和的光源，光束把鋼架與舞台一帶打亮，原生那裡是粉紅天空、光源弱。天空 cubemap 已確認是 LDR（alpha 全 1，`OK_SKY_DEBUG=4`），`_Tint` 0.9 × `unity_ColorSpaceDouble` 2；太陽附近為何偏白（取樣方向、atlas 解析度或色彩轉換）**尚未查明**，`OK_SKY_DEBUG=2` 的原始貼圖值在 (900,200) 是 211,168,157，乘上 1.57 會飽和成白，原生卻是 225,134,141（G 更低）：原生在同一個畫面位置取到的是天空的另一個方向（舞台幾何位置兩邊一致，所以是天空的取樣方向或太陽在 cube 裡的位置不同，我們的太陽亮區偏低、偏大）。下一步查 Skybox/Cubemap 在網格上的取樣方向（物件空間頂點、_Rotation、atlas 面的對應）。另外：粒子特效剛載入的幾秒會噴出大團白煙（第一個 capture 會拍到），穩定後消失。
- **偏粉紅已修（2026-10-31 晚）**：原因是 cube atlas 的每個面上下顛倒。`unity::cubemap_atlas` 把面資料當成 Texture2D（由下往上存）翻了一次，但 cubemap 的面資料本來就是由上往下存，翻完反而顛倒。原生量到天球 `_Rotation` 是 0、半徑約 575 m，取樣方向與我們一致；用 `OK_SKY_FLIP` 實驗四種翻法，只有「每面上下翻回來」吻合：天空左側 201,109,135（原生 201,110,135）、天空上方 216,146,146（原生 216,145,144）、鋼架 59,57,81（原生 51,45,68，修前 128,109,116）。基礎庫裡已錄的 atlas 都是這個方向，所以改在讀取端：`OKCubeAtlasSample`（天空球、水面反射）與地圖反射探針的取樣都改成從面的底部取列，不必重錄模型。對照圖 `C:\Users\Damody\AppData\Local\Temp\claude\C--portable-OpenKoikatsu\06cb08af-708c-4559-954f-993ce591a332\scratchpad\sky_cmp.png`（左原生、右修後）。
- 光束放射光的更正：齒輪周圍的放射光不是「ビーム・扇」（那兩個是噴火裝置上 45 cm 的小扇形，id 211／227），而是原生 SunShafts（useDepthTexture，太陽在畫面中央）穿過窗戶與齒輪縫隙的光芒。我們的 SunShafts 在窗戶上比原生亮（243 對 229）、齒輪一帶比原生弱（58 對 106，關掉光束是 50）：光芒沒有往外拉長。數學（門檻、距離權重、四次徑向模糊）已逐項對照，**還沒查**的是來源像素（原生的來源是半解析度 RT、每次模糊後存 8 位元）與窗戶是否寫入深度。
- **齒輪一帶的光束已修（2026-11-01，F 組）**：原生的光芒是從齒輪白環、砲管這些近物往外拉的（`OK_STUDIO_SHAFTS_DEPTH=0` 讓所有像素都當光源，形狀就和原生一樣）。原因是 Unity 的 `_CameraDepthTexture` 只畫 render queue ≤ 2500 的物件，而 Studio 基本形「(キャラ)」的材質 `m_koi_stu_kihon00_02` 是 `main_item_studio`、**queue 3000**（studio/61、65、87 的 bundle 都一樣；探針的 `Scene1.scene.tsv` 也顯示齒輪、牆、窗都是這個材質）。SunShafts 在這些像素看到的是遠平面，它們的顏色就成了光源。我們把這類零件畫成不透明、寫深度，所以光束只從窗戶後的天空來。修法：`AKKPreviewActor::DepthTextureAbsentParts` 記下畫成不透明但 `_OKRenderQueue` > 2500 的元件；場景開了 SunShafts 時 `AOKSceneActor::MarkDepthTextureAbsent` 讓它們畫進 custom depth、stencil 255（半透明繪製順序用的 rank 是 1～255 且只比自己小的，不受影響），光束 pass 在可見面是這類元件（stencil 255 且 custom depth 等於場景深度）時當成遠處。沒開 SunShafts 的場景不多一道 custom depth。結果（1280×720，區域平均亮度）：齒輪區 原生 132／修前 58／修後 134，右上齒輪 147／151，窗戶 140／149。對照圖 `C:\Users\Damody\AppData\Local\Temp\claude\C--portable-OpenKoikatsu\06cb08af-708c-4559-954f-993ce591a332\scratchpad\shaft_mark_cmp.png`（左原生、右修後）。剩下：舞台前方地板我們亮很多（關光束時就是 167 對原生 87，另一個問題），它也是 queue 3000 的道具，所以台前多了一些光芒（107 對 74）。
- **舞台前地板偏亮：查到一半（2026-11-01，F 組）**：那塊是物件 175（「キューブ(キャラ）」10 cm 厚的板，z 0 到 −5 m，`m_koi_stu_kihon00_02` queue 3000，底色 0.70、圖樣 28 的 u 縮放 0.01、Material Editor 的 `AnotherRamp`（128×8 多段灰階色帶）與 `AnotherRampFull` 1.5）。水面（StylizedWater，−0.81 m）與海水方塊（179，`main_item_studio_alpha` queue 4907，頂面 −0.87 m）都在它露出部分的下方，隱藏方塊只改變 y ≥ 590 的水面，所以不是水的問題。原生板面（1280×720，x=640）由上往下 133,125,125 → 45,69,90，中間有一塊塊柔和的深色斑；我們整片 192,164,154。原生探針記錄主光 `Directional Chara` 是 Soft 陰影、強度 0.8、只照 layer 10（道具都在 layer 10），所以原生道具走的是 `DIRECTIONAL SHADOWS_SCREEN` 版本；我們的 `ForgeStudio` 是 `DIRECTIONAL` 版本（blob 12），沒有陰影項，Studio 道具也關了逐物件的 toon 陰影擷取。推測：Unity 的螢幕空間陰影用相機深度貼圖重建位置，queue 3000 的板不在深度貼圖裡，它拿到的是後方深度貼圖物件那裡的陰影衰減，再經 AnotherRamp 的色帶變成斑塊。
- **舞台前地板：原因查明（2026-11-01，F 組）**：上面的陰影推測**不成立**。原生探針新增 `--shadow-mask`（`Tools/GamePoseProbe/run_scene_visual_audit.lua`；主光掛 `LightEvent.AfterScreenspaceMask` 複製螢幕空間陰影遮罩、相機掛 `BeforeForwardAlpha` 複製 `_CameraDepthTexture`，輸出 `Scene1.shadow-mask.png`、`.depth.r32f`、`.shadow-mask.txt`），拍在 `F:\OpenKoikatsu\Saved\Tests\kks_shadowmask_1101\`。遮罩確實只含深度貼圖收的物件（角色、鐵架、幾棟建築、一塊大平面；齒輪、牆、舞台方塊都不在，陰影距離 50 m、4 cascades），橫板一整片都是 51（全陰影）；但同一排的原生亮度在 101～184 之間，而且和前一次原生截圖的斑塊位置不同。斑塊是會動的：特效「閃亮光粉」（id 184，原生 `CommonSpace/스테이지/바닥+바다/koi_dust`）除了亮粉 `dust_parts`（`dust_01_lit`）還有一層暗色煙 `smoke_02_lit`（`Ethical Motion/Particles/Unity5/Lit Alpha Blend Soft Particle`）飄在舞台前。我們的 OK 特效庫刻意不轉原作粒子，「閃亮光粉」對到 `NS_OK_Sparkle`，只有亮粉，沒有煙。沒有煙的地方原生 175～184、我們 192，接近。要補的是特效庫：給「閃亮光粉」（kks 13/73/1116、1311）加一層低處的暗煙，或另做一個「光粉與煙」效果對到這個原作道具。深度貼圖的複製值看起來有雜訊（相鄰像素 0.03～0.3 跳動），還沒驗證可用。
- **台前暗煙已補（2026-11-01，F 組）**：原作的 Dust 是 mod 粒子包 `[rskoi] studio particle effects` 的 `koi_dust`（亮粉＋一大片低處暗煙），以前因名字含 dust 被對到通用的「閃亮光粉」。`effects.json` 新增兩個效果：`dust`（光粉與低煙；用 `koi_dust` 這個字對到該道具，亮粉沿用 `NS_OK_Sparkle`）與 `dust_smoke`（低處暗煙，`NS_OK_DustSmoke`）。特效庫多了三個欄位：`constants`（`OKStudio.MakeEffects` 生系統時覆寫樣板模組的常數，例如 `InitializeParticle.Uniform Sprite Size Max`、`ShapeLocation.Box Size`、`SpawnRate.SpawnRate`）、`material`（sprite 材質；`M_OK_SmokeSprite` 缺少時用程式建：半透明、不受光、柔和圓形、粒子 alpha、碰到場景 1 m 內淡出。樣板的 `DefaultSpriteMaterial` 是加色混合，深色煙加上去等於沒加）、`layers`（同時顯示的其他效果與位移）。`color` 可帶第四個數字當 alpha。新指令 `OKStudio.DumpEffectInputs <id>` 列出效果每個模組輸入、rapid-iteration 常數與 sprite 材質。結果（1280×720 區域平均；原生兩次截圖，煙會動）：板面 原生 106／137、修前約 190、修後 133；地板 87／120、172、117；台前光芒 74／79、107、92；齒輪區 132、134、135；窗戶 140、149、149；海面 90／86、—、104。既有的 `.okscene` 存的是 `sparkle`，要從原作 PNG 重新匯入才會換成 `dust`。
- **SunShafts 偶爾沒出來（2026-11-01，F 組）**：一次直接載入原作 PNG 後整個程序都沒有光束（窗戶 91、齒輪區 98，正常 149、137），重跑又有。後處理材質是執行時建的，`ReadyInstance` 在 shader map 不完整時補送編譯，但每個 pass 只送 4 次、而且每幀都送；那次是剛建好 `NS_OK_DustSmoke` 後第一次啟動，編譯器忙，4 次在前幾幀就用完，之後 map 一直不完整，光束永遠不會掛上，也沒有任何訊息。改成不完整時每 2 秒補送一次、不設上限；每個 pass 狀態改變時記一行 `OK studio post pass N: <狀態> (<補送次數> submits)`；控制通道 `state` 多回報 `post_opaque`、`post_shafts`、`sun_shafts`、`post_blendables`，`capture` 在場景開著霧／AO／光束但對應 pass 未 ready 時回傳 `warning_post_opaque`／`warning_post_shafts`。正常情況 1 次補送就 ready；那次的忙碌情況沒有再重現。舊 `.okscene` 只存 `"effect": "sparkle"` 與名稱，沒有原作道具的 GUID／prefab，讀檔時分不出原本是不是 `koi_dust`，所以不在讀檔時自動改；要從原作 PNG 重新匯入。
- **舊場景的 sparkle 自動改成 dust（2026-11-01，F 組）**：場景的效果物件新增 `source`（`FOKSceneEffect::Source`），匯入原作場景時記下 mod 粒子道具的名稱＋prefab（小寫，`FOKStudioCatalog::ParticleText`）。讀 `.okscene` 時，有 `source` 的效果物件依效果庫目前的字詞重新對應（`EffectForParticleText`），對到不同效果就換掉並記 log `OK scene: effect <id> (<source>) now <效果>`。效果庫日後新增或改字詞，舊檔也會跟著換。驗收：重新讀 `83283571_p1.png` 寫出的 okscene 有 `"source": "dust koi_dust"`；把它的效果改回 `sparkle` 再讀，log 為 `effect 184 (dust koi_dust) now dust`。**限制**：這次之前存的 okscene 沒有 `source`，無法知道原本是哪個道具，要重新匯入原作 PNG 才會改。
- **海面與台前光芒（2026-11-01，F 組，未完成）**：
  - StylizedWater 的深度改成和 SunShafts 一樣，把原生深度貼圖沒有的 queue > 2500 部件（stencil 255 且 custom depth 等於場景深度）當成無限遠（`bReadsUnityDepthTexture`，`OKSceneActor` 只要有讀深度的水也會標記）。量測幾乎不變（海面 102、左側 104；原生 90、63），因為這張場景前景水底下的是 queue ≤ 2500 的石地。
  - 前景的淺青是三層 `Xffect/heat_distortion(_t2)`（`p_koi_stu_umi00_00`、`kawa00` 兩層，貼圖都是 `t_koi_stu_umi00_00`）乘在石地上。反組譯 `heat_distortion`（`studio/map/*.unity3d` 裡同名 shader，`dump_shader_dxbc`）：GrabPass 後輸出 扭曲背景×`_MainTex`.rgb、alpha `_MainTex`.a，SrcAlpha/OneMinusSrcAlpha，和 t2 相同，所以乘色模型正確。KKS 是 Gamma 色彩空間，乘色係數改成用編碼值做 lerp 再解碼（`OKModulateInstance`）；這張圖前景幾乎沒變（係數本來就接近 1）。
  - 其他場景重比（2026-11-01）：4432 稻田與 `1547730231799` 沙灘（都有 `mo_stneo_zimen_00` 的 heat_distortion 層），原生探針 `Saved/Tests/kks_heat_1101` 對 UE 的 Gamma 版與線性版：兩版逐像素相同（平均差 0、最大差 4/765），因為這兩張的貼圖 alpha 是 1，兩種公式此時相等；與原生的區域平均差都在 10 以內（稻田 46,57,65 對 43,56,63；沙灘 240,224,177 對 238,219,167）。沒有變差，保留 Gamma 版。
  - 結論：原生前景的暗青（23,80,99）是水在那裡不透明；我們的水在前景幾乎透明（隱藏水面前後只差 3～7）。水面石地的實際深度差、水材質數值（探針只錄到 `_Glossiness`、`_Cutoff`）都還沒對過，下一步要讓探針錄出水材質全部數值與真正的 `_CameraDepthTexture`（目前錄到的 `depth.r32f` 看起來是顏色，含天空雲層，不能當深度用）。
  - 台前光芒仍是 93～98（原生 74～79）；隨機暗煙會讓平台與地板在 120～150 之間跳動，單張截圖不能當結論。
- **台前光芒與海面（2026-11-01）**：暗煙 alpha 調成 0.6 後（兩次各 40 秒拍）板面 126／128（原生 106／137）、地板 135／126（87／120）、台前光芒 90／93（74／79）、齒輪 136／134、窗戶 149。台前光芒關掉光束是 79，和原生一樣，多出的是光束：原生台前板面被煙壓得更暗，光源較弱。海面兩側（左右各 280 px）我們不論有沒有光束、煙都在 96～116，原生 63，是 StylizedWater 本身偏亮，與這次無關，**另查**。

**實際做一個完整場景（2026-10-31，F 組）**：全程用畫面上的按鈕（控制通道 `click`，按鈕自己的處理函式）做：地圖 0 自室 → 加女角 C001 → 女 H 挿入／騎乗位／弱ループ、動作模式 1 → 加男角 Male01 → 道具ハート1 → 特效閃亮光粉 → 相機 → 另存 → 新場景 → 開啟讀回。讀回後地圖、兩個角色（位置、動作、`pattern`）、道具、特效、相機都一致，讀取報告沒有缺件。過程中卡手或不直覺、已修掉的：

| 遇到的 | 修法 |
|---|---|
| 讀地圖後畫面中央出現引擎的英文警告「Multiple directional lights are competing…」（地圖自己的平行光、地圖太陽與場景主光的 ForwardShadingPriority 都是 0） | 每盞平行光給不同的優先序：地圖的依序 0、1…，地圖太陽 1000，場景的燈 2000＋id |
| 新增選單的角色卡、地圖、特效、場景檔視窗都開在選單旁同一個位置，一層疊一層；再按一次「角色」會把被蓋住的卡片視窗關掉，選單同時亮三個 | 這幾個清單（含道具目錄、添加光源兩頁）同時只開一個，開新的關舊的；按鈕切換時，被別的視窗蓋住的視窗改成拉到最前而不是關掉（頁面按鈕同樣）；從「新增」切到角色等其他根選單時收起新增清單（HS2Studio6 的做法），動畫頁不再蓋在卡片視窗上 |
| 新道具、新特效放在世界原點，自室的原點在桌子裡，加了看不到 | 放在目前視角的焦點（選了角色按 F 聚焦後加，就出現在角色身邊） |
| 加道具、特效、相機後狀態列還是「加入角色 Male01」 | 改成顯示剛加入的種類與名稱 |
| 選到燈光、相機、道具時，角色專用的「動畫／狀態／骨骼」頁留著一個只寫「先選擇一個角色」的空白大面板 | 選取不是角色時這些頁暫時隱藏（不關閉），選回角色再出現 |
| H 動作要男女各點一次：選男角 → H 動作 → 分組 → 體位 → 同一個動作，再把位置對齊、動作模式再設一次 | 動畫頁加「配給對方」：把另一個選取的角色（沒有就用最近的角色）設成對方性別的同一個動作，放到同一個位置，帶上同樣的輔助／動作模式與速度，兩人從頭同步（`anim.partner`） |
| 資料夾只有兩張卡時縮圖被拉大到填滿整格 | 卡片格固定大小，靠左上排 |
| 場景檔瀏覽器的資料夾列被一排 `*_cards` 佔滿（KStudio 匯入時拆出的卡片） | 旁邊有同名場景的 `<場景>_cards` 不列 |
| 新場景後存檔名稱欄仍是上一個場景的名字（按另存會問要不要覆蓋舊檔） | 新場景給新的預設名、讀檔後換成讀到的檔名，欄位直接顯示實際要存的名字 |
| 控制通道的 `click` 點不到清單列（動作清單、樹狀列），測試只能繞過 UI | `click` 也找 `STableRow`，在列中央送一次 Slate 左鍵 |

看過但不改的：新角色是 T-pose（原作 Studio 的新角色播「初期ポーズ」，這個狀態本身就是 `tpose`）；C001 的雙馬尾在任何動作下都朝後挺著（髮束本身就硬，T-pose 時看起來垂下是角度）。驗證：`check_ok_studio_buttons.lua` 97/97（第一次在用過的場景上跑是 96/97：測試直接用了我手動設成 1 的 C001，「預設」正確還原成動作自己的 0；乾淨的遊戲重跑 97/97）、`OKStudio.UiSelfTest` 46/46、`verify_ok_studio.lua --tag scene1031` 6/6。

**第二個完整場景（2026-11-01，F 組）**：教室講台上三個人。流程寫成步驟檔 `Tools/ok_studio_flows/scene2_classroom.txt`，用 `Tools/ok_studio_ui_flow.lua` 經控制通道照畫面操作跑完（按鈕、清單列、輸入框都走它們自己的處理；只有把角色擺到講台用 `transform` 代替拖把手）：

1. 地圖 4 教室1-1。
2. 角色卡連加 C002、C011，再到 Demo 加 Male01。
3. 移到講台、面向座位。
4. C002 換服裝組 2、表情眉 2／眼 3 笑顏閉眼／嘴 1。
5. 三人各播 Studio 動作 立ち6／立ち3／立ち1。
6. 道具搜尋「本」加「本1」，在角色分組「手」選右手，「掛到所選部位」。
7. 用兩個視角各加一台相機（全景、C002 特寫），逐台「用此相機」拍照。
8. 時間軸：選 Male01，在開頭與結尾各打 key（結尾往座位走 150 cm）；從開頭播放 4 秒後按播放／停止定格，截圖窗「拍攝」。
9. 「另存新檔」輸入名稱，再按「新場景」，然後從場景檔視窗「覆蓋（開啟）」讀回。

讀回後，以下都和存檔前一致，讀取報告沒有缺件：
- 地圖、三人的位置／服裝組／表情／動作；
- 書掛在 C011 的 `a_n_hand_R`；
- 兩台相機與作用中的相機 1；
- Male01 的兩個 key；
- 播放頭 4.27 秒。

對照數據：
- 兩台相機的畫面幾乎全不同（30,578／32,400 個取樣點）。
- 定格後相隔 2 秒的兩張只差 12／32,400：場景有時間軸軌道時，角色動作跟著時間軸時間走，暫停就一起停。

截圖：
- `F:\OpenKoikatsu\Saved\OKStudio\mcp_ui\20261101_031235_*.png`（各步驟）；
- `F:\OpenKoikatsu\Saved\OKStudio\flow\20261101_031235\`（cam1／cam2／freeze_a／freeze_b）；
- `F:\OpenKoikatsu\UserData\Studio\screenshot\okstudio_20261101_031405.png`（定格截圖）。

過程中卡手或不直覺、已修掉的：

| 遇到的 | 修法 |
|---|---|
| 連加三張卡，三人都生在原點疊在一起，教室的原點又在桌子裡 | 新角色站在視角焦點下方的地面；那裡已有角色就沿畫面右方每 60 cm 錯開（`UOKStudioGame::AddCharacter`） |
| 在表情頁按 F（聚焦所選），框的是全身，臉在畫面上緣外 | 骨骼頁的視線／頸部／表情分頁聚焦時對準臉：`cf_j_head` 上方 8 cm、距離 90 cm（`FocusSelection(bFace)`） |
| 選著掛在 C011 手上的書時加相機，新相機被放進 C011 底下（會跟著她動），名稱還是英文「Camera 6」 | 新物件只放進所選的資料夾，或所選物件所在的資料夾，不進角色或道具底下（掛到角色身上是角色分組的工作）；相機、資料夾預設叫「相機 N」「資料夾 N」 |
| 系統選單固定 140 高，MMD 導演合進來後 14 項只露出 7 項，其餘要靠 3 px 的捲軸 | 選單高度至少容得下全部項目（`SubMenuHeight`），HS2Studio6 的高度當下限 |
| 新場景後時間軸播放頭留在上個場景暫停的位置 | 新場景時時長回 10 秒、停止播放、播放頭與循環範圍歸零 |
| 暫停在 4.15 秒存檔，讀回後物件停在 4.15 秒的樣子，播放頭卻是 0 | 播放頭隨場景存讀（文件新欄位 `playhead`，`SetTimelineTime` 同步） |

控制通道為了照畫面操作補的（AI 操作與測試用）：
- `type`：給提示文字或內容相符的輸入框鍵盤焦點，以按鍵事件清空並逐字輸入，`enter` 送出。道具搜尋框是送出才套用，所以要帶 Enter。
- `click` 加 `"tip": true`：只有圖示的按鈕（時間軸的開頭、播放、打 key 等）用提示文字找。`IconButton` 把提示文字另掛成按鈕的 `tip:` tag，比對時不執行任何提示框 lambda（之前讀提示框文字讓遊戲崩潰過，見 ERROR_LOG）。
- 時間軸播放中，原本整個控制通道都回 `busy: playing`，連播放／停止都按不到。現在播放中允許 `click`、`type`、`capture`，和使用者播放中照樣能按按鈕一致。
- `click` 點清單列之前，先釋放殘留的滑鼠擷取。這輪有一次樹狀列點下去只改了反白、沒改選取；之後兩次完整流程都沒再出現，有釋放時 log 會寫一行。
- `Tools/ok_studio_ui_flow.lua` 的步驟支援 `?ui欄位=值`／`?ui欄位!=值`／`?closed:視窗`。根選單、時間軸、截圖窗都是切換鈕，這樣只在沒開時才按。

看過但不改的：
- 「前視／後視」是世界座標的方向，不是所選角色的正面；角色擺向別處時要自己轉鏡頭。
- 時間軸窗會蓋住屬性窗下半。
- 沒有選取時，變換列顯示位置 0、縮放 0。

驗證：
- `check_ok_studio_buttons.lua` 97/97。系統選單那一項原本假設選單是關的，流程結束時選單開著，第一次跑成 96/97；改成沒開才打開。
- `OKStudio.UiSelfTest` 46/46，在新程序跑。在用過的實例裡跑時，「畫面點角色」會因視角不同失敗；接著 Undo 多退一步，第 123 行取到空指標崩潰，已加上防護。
- `verify_ok_studio.lua --tag scene2c_1101` 6/6。GameSelfTest 曾因播放頭只在存檔時寫入而失敗（存檔前的預期內容沒有它），改成播放頭一變就同步到文件。

### U15　工作室模式只顯示工作室 UI（2026-10-30，使用者要求）

從卡片牆等其他模式進入工作室時，畫面上原本的 UI 會留著。改成：工作室面板加到 viewport 後，從 viewport 的 Slate 樹往下找到面板，沿路把其他兄弟節點（其他模式的 UMG、Slate 面板、圖層）全部收起來，並把 HUD 的 `bShowHUD` 關掉；`Stop` 時還原成原本的可見狀態。

控制通道新增 `leave`（`POST /studio/leave`，可帶 `{"name":"<名稱>"}`）：等同按工作室的關閉鈕，回到啟動工作室的模式。工作室關閉時控制通道也會關，所以帶 `name` 時在一秒後另拍一張含 UI 的整個視窗截圖，存到 `Saved/OKStudio/mcp_ui/<名稱>.png`。實測（卡片牆 `-game -Control "-ExecCmds=OKStudio.Play"`）：在工作室時畫面只有工作室 UI；`leave` 之後卡片牆的清單、按鈕與角色資訊完整還原。

診斷用的控制通道指令：`exec`（`{"command":"ShowFlag.Translucency 0"}`，在遊戲世界下主控台指令）、`parts`（`{"match":"Part_133","hide":true}`，列出或隱藏名稱含 match 的預覽元件，附 owner、包圍球中心與半徑）。注意 `camera` 只要設了 focus／yaw／pitch／distance 就會切到自由相機；要回到場景相機用 `{"use_camera":<相機物件 id>}`，`capture` 拍的是目前的相機。另外，`run_scene_visual_audit.lua` 的原生畫面沒有套用場景的地圖位移（caMap），有移動地圖的場景要以 KK 縮圖為準。

## 7. 優先順序總表

| 優先 | 階段 | 理由 |
|---|---|---|
| P0 | U0 拆檔與指令表 | 所有後續工作的前提；同時解決快捷鍵焦點問題與 UI 驗收手段 |
| P0 | U1 SceneEditor 外殼改成 HS2Studio6 版面 | 畫面一打開就接近 HS2Studio6；之後每個面板都放進這個外殼 |
| P0 | U2 選取與物件樹 | 現況最大的操作障礙（不能在視窗點選、不能多選、不能拖放） |
| P1 | U7 存讀與可靠性 | 使用者資料安全；未存提示與自動備份成本低、效益高，可與 U3 並行 |
| P1 | U3 目錄與加入 | 加東西是最常做的事 |
| P1 | U5 姿勢 | Studio 的核心創作工作 |
| P1 | U4 Gizmo 與相機 | 姿勢與擺物件的效率 |
| P2 | U6 時間軸 | 工作量最大，依賴 U2 多選與 U4 相機物件 |
| P2 | U8 外觀參數 | 補 UI 為主，風險低 |
| P2 | U9 設定與 AI | 指令表穩定後再開放 |

建議順序：U0 → U1 → U2 →（U7 ∥ U3）→ U5 → U4 → U6 → U8 → U9。U0 完成、面板拆成獨立檔之後，U3／U7／U8 碰的檔案不重疊，可以分給不同 session 並行。

---

## 8. 風險

| 風險 | 對策 |
|---|---|
| OK Studio 由 E 線負責（[雙線開發](DUAL_LINE_DEVELOPMENT.md)），F 組改動要合回 | 動工前與協調者確認 F 組是否接手 OK Studio UI；U0 拆檔是大搬移，要在 E 線沒有未合併的 `OKStudioGame.cpp` 改動時一次完成並盡快合回，避免長期分叉 |
| 共用工作樹多 session 同改 `OKStudioGame.cpp` | U0 先拆檔；之後每階段只碰自己的面板檔；提交只交自己的 hunk |
| 視窗點選角色：預覽角色是自訂渲染、GPU skin，未必有可用碰撞 | 先用骨骼膠囊（骨位置來自 `BoneWorldMatrix`）與部件 bounds 判定；不夠準再考慮 hit proxy 或 ID buffer |
| 多選＋整份 JSON 快照 Undo 的記憶體與延遲 | 現有上限 100 步／256 MB、沒變的物件不重建（`Game.cpp:1346-1409`）；多選操作合併成一步；量測大場景（`OKStudio.StressItems`）的快照時間 |
| 縮圖大量解碼拖慢 UI | 背景執行緒解碼、每幀建立數量上限、LRU 快取；perf 預算 0.5 ms |
| 時間軸新軌道與既有 `EvaluateTimeline`、Story、`.okanim` 輸出相容 | 只加欄位；舊檔讀取測試列入 `verify_ok_studio.lua`；`.okanim` 輸出結果與改動前位元組比對 |
| 全域快捷鍵前處理器攔到別的 Slate（主控台、故事模式） | 只在工作室執行、非故事模式（`SetStoryMode`，`Game.cpp:1732`）、焦點不在文字輸入時派發 |
| Unreal 實例上限（全機 2 個） | 驗收按 `C:\portable\OpenKoikatsu\Saved\SESSION_RULES_ALL_20261028.md` 的 slot 規則排隊；UiSelfTest 設計成單一程序跑完 |
| 舊 UMG 動作時間軸移除影響讀卡畫面 | U6-8 最後做，先對照功能清單、讀卡畫面改入口後再刪 |

---

## 9. 不做的項目

- **HS2 角色、HS 場景匯入、HS2/KK 混合產品頁**：OK Studio 只處理 KK／KKS 與 OK 卡。
- **局域網聊天與場景同步**（HS:UIR#工作室聯機聊天）。
- **硬體光追出圖**（HS2Studio6 的 TrueTrace）、**MRQ 算圖**（已依使用者 2026-10-18 決定擱置）。
- **原外掛原樣移植**（KKSPlugin、VNGE、NodesConstraints、VMD）：需要時把功能重寫進指令表與面板，不做外掛載入層。
- **Route（路線）系統**：時間軸的平滑路徑（`bSmoothPath`）已涵蓋主要需求，本計畫不另做 HS2Studio6 式路線物件。
- **Editor 版 `FOKStudioEditor` 功能對等**：Editor 版只維持開檔、存檔、反射編輯；新 UI 只做在遊戲內工作室。
- **改用 UMG 或改寫成 Editor Mode**。
- **畫面還原**（光影、LUT、天空、各樣本場景差異）：繼續由 [OK Studio 計畫](OK_STUDIO_PLAN.md) 處理。
- **在 Studio 裡換裝與捏人**：HS2Studio6 的定制角色、定制飾品、HairShader、服裝卡套用（`hs2/56`、`58`～`60`）都屬人物編輯器的工作，本計畫不做；OK Studio 保留換卡、換服裝組、衣服狀態。
- **語音播放佇列**（`hs2/55_voice.png`）：之後若要做，另開計畫。
- **SkinEffects、ClothingBlendShape、ClothColliders、角色 MaterialEditor、汁貼花**：屬資料／渲染功能，仍是 [OK Studio 計畫](OK_STUDIO_PLAN.md) `:331` 的待辦，不在本計畫。

---

## 10. 驗收文件

每階段除了第 6 節的自動驗收，還要在 `Saved/StudioUIRef/compare/` 重拍對應的 OK Studio 畫面，與第 3.8 節的 HS2Studio6 截圖並排檢查，並在完成紀錄中附上對照圖路徑。

每階段完成時在本檔對應章節下方補一段「完成紀錄」：日期、commit、`OKStudio.GameSelfTest`／`OKStudio.UiSelfTest` 結果、`verify_ok_studio.lua` 的 tag、perf 數字與前一版的差、人工檢查截圖路徑。做錯的事照專案規則寫進 `Docs/ERROR_LOG.md`。

---

## 11. 截圖對照的重拍方式

兩邊都不動實體滑鼠：OK Studio 走程式內通道，HS2Studio6 走 Unity 內的 EventSystem（原因見 `Docs/ERROR_LOG.md` 2026-10-29「對別的前景視窗送了合成滑鼠點擊」）。

**HS2Studio6**

1. 先在 Unity Hub 登入並啟用授權。
2. 啟動 Unity，它會直接打開 `zzzstudio.unity` 並進入 Play：

   ```powershell
   & 'F:\Unity\Editors\6000.3.22f1\Editor\Unity.exe' -projectPath 'F:\OpenKoikatsu\ref\HS2Studio6' -logFile 'F:\Unity\logs\hs2studio6.log' -executeMethod OKLocalCapture.Menu
   ```

3. 把指令一行一條寫進 `F:\Unity\okcmd.txt`。輔助腳本會依序執行，結果寫進 `F:\Unity\okcmd.out`，全部做完時最後一行是 `DONE`。可用的指令：

   | 指令 | 作用 |
   |---|---|
   | `list` | 把畫面上可按的元件（路徑、文字、螢幕座標）寫到 `F:\Unity\okui.json` |
   | `clickname <文字或物件名>` | 點第一個名稱或文字包含它的元件 |
   | `click <x> <y>` | 點螢幕座標，原點在左下 |
   | `wait <n>` | 等 n/20 秒 |
   | `shot <名稱>` | 截 Game 畫面到 `Saved/StudioUIRef/hs2/<名稱>.png` |
   | `layout <名稱>` | 輸出整棵 UI 樹到 `Saved/StudioUIRef/layout/<名稱>.json`（第 12 節的版面規格由此產生） |
   | `reload` | 停止 Play、重新編譯後再進 Play |
   | `quit` | 停止 Play 後正常結束 Unity；直接結束行程會留下場景備份，下次啟動卡在「Recovering Scene Backups」對話框 |

4. 輔助腳本在 `ref/HS2Studio6/Assets/OKLocalCapture/Editor/OKLocalCapture.cs`，只留本機。
5. 本機工程的資源狀態（2026-10-29）：
   - 已合入 `ref/HS2Studio6-BasicResources-WithMeta-20261029.zip`：新增 390 檔，與工程內容相同的 865 檔不動，沒有衝突檔。
   - 另外用它的轉換器（`Tools/StandaloneConverter/offline_bundle.py`，模板由 `KKMakerBuilder.BuildTemplate()` 產生）從本機 KKS 安裝轉出一批 KK 包；其中 166 個已改用資源包裡的官方版本，資源包沒有的 23 個（多為 BGM）留著。
   - KK 包只剩 5 個追加 BGM 沒有。
   - 測試用的 KKS 原作卡放在 `ref/HS2Studio6/UserData/KK/chara/female/`（4 張，只留本機）。

**OK Studio**

- 目前：用 `SceneEditor.cmd` 開場景後截取遊戲視窗。截圖前要先拿全機 Unreal 名額（slot），截完放掉。
- U0-4 完成後：改用 `OKStudio.Cmd <id>` 打開面板、`OKStudio.UiState` 確認狀態，截圖由遊戲內輸出。屆時兩邊都可以用腳本重拍，對照圖可以每個階段自動更新。

---

## 12. 整合路線：讓 OK Studio 逼近 HS2Studio6

HS2Studio6 是 C# 與 uGUI，不能在 Unreal 裡執行。做法是把它當規格書，在 OK Studio 裡用 Slate 重做。

1. **版面規格**（已完成）
   - 40 個面板狀態的完整 UI 樹，已整理成 [版面規格](OK_STUDIO_HS2_LAYOUT_SPEC.md) 與 `Docs/StudioUI/hs2studio6_layout_spec.json`。
   - 內容包括：主題色、字級、外殼各區塊錨定與尺寸、每個視窗的矩形與控制項。
2. **外殼**（U0、U1）
   - 拆檔、建立指令表後，照規格的外殼重排：左上 add／anim／sound／system 四個主鈕加子選單、右上工具列、角色分組欄加工作區樹、左下變換列。
   - 主題色與尺寸做成一份 Slate 樣式，以 1600×900 為基準等比縮放。
   - 詳情窗同時只開一個；Timeline、動畫控制器、快搜、姿勢庫、相機窗是獨立浮窗。
3. **逐窗補上**（U2～U9）：照規格表的「OK 對應」欄，每個 HS2Studio6 視窗接到 OK 已有的後端：
   - 狀態：`FOKSceneCharacter::ClothesStates`、`AccessoriesShown`
   - 骨骼各頁：`FOkSession::SetFk`、`PrepareStudioIk`、`IkEffectorPosition`、`SetStudioFixedNeck/Eyes`、`SetHeadLook`
   - 動畫：`FOKStudioCatalog::Anims`
   - 場景卡：`FOKSceneCard`、`UOKSceneDocument`
4. **對照驗收**
   - 同一張 KKS 原作卡在兩邊都讀得起來。
   - U0-4 的 `OKStudio.Cmd`／`OKStudio.UiState` 完成後，OK Studio 也輸出 Slate 版面（相同的 JSON 欄位）。
   - 每階段比對「面板矩形數值差」，並重拍並排截圖（第 10 節）。
5. **不照抄**
   - 原作圖示與 UI 圖片不進 repo。
   - 簡中字串改用文字表的語意 id。
   - 不做聊天、HS 角色、Studio 內換裝捏人。
   - 保留 OK 自有的 Story、`.okanim` 輸出、KStudio 匯入與畫面還原。

OK Studio 依[雙線開發](DUAL_LINE_DEVELOPMENT.md)歸 E 線，動工前先與協調者確認由誰接手。U0 拆檔要一次做完並盡快合回。
