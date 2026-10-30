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
- 還沒做：視窗拖動與置前、`ui_layout.json` 版面保存、各視窗內部改成新樣式（下拉、數值框仍是 UE 預設外觀）、圖示（目前是文字鈕）。

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
- 效能：照 slot 規則要同時拿到兩個名額才量，還沒量。
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
- 鏡子的倒影沒有依 `m_ReflectLayers` 篩圖層（目前拍到全部物件）。
- 通用零件材質的 `_MainTex_ST` 是建材質時寫死的常數，Material Editor 改貼圖縮放／位移套不上（vmd 有一張場景的 `m_koi_stu_kawa00_00`）。
- `83572034_p1`：舞台後方缺月亮背景、兩側藍色發光道具顯示成灰白，舞台中間的紅色板子原作沒有。原因是 Material Editor 換著色器（`MaterialShaderList`）：全部 7,694 張場景的統計是 `Shader Forge/main_item_emission` 362 筆／14 張場景、`main_item_studio` 56／2、`main_item_studio_alpha` 31／3、`Unlit/Glitch3DProjection` 15／2（藍色發光手把）、`Goo` 8／1、`main_item` 7／4、`IBL_Shader` 3／1、`HarvexARC-XRay` 2／2（中間的紅板，原作是透視、看得到後面的月亮）、`main_opaque` 2／1、`HarvexARC-Gem` 1／1。`okmodel_view::ItemMaterialEdits::shader_supported` 不收 `main_item_emission`，所以保留原著色器。試過把它加進去（Unreal 共用母材質有 ForgeEmission 程式）：牆和地板變成白／粉色，KKS 原生（`F:\OpenKoikatsu\Saved\Tests\kks_p1emission_1030\Scene1.png`）是黑色——場景用 Material Editor 給 `_AnotherRamp` 換了一張暗色 ramp（`_AnotherRampFull` 1.3），換到 ForgeEmission 後這張 ramp 沒有生效，原因待查，已退回。`Glitch3DProjection`、`HarvexARC-XRay` 是第三方著色器，需要從各自的 zipmod 反組譯。

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
