# OK Studio 盤點（2026-10-16）

目標：把原作 CharaStudio（含使用者安裝的 Studio 相關 BepInEx 外掛）的功能，以 Unreal 外掛的形式做進 OK 工具生態。只用 OK 共用資源與 OK 卡，不依賴原作遊戲；舊 okPv APNG 卡不處理。

## 使用者環境的規模

- 原作安裝 `D:\KoikatuSunshine`：BepInEx 外掛 123 個，`UserData/Studio/scene` 有 **7,656 個場景 PNG**。
- `UserData/Studio/pose` 有姿勢檔；`abdata/studio*` 有 148 個原版 Studio 資源檔（道具、動畫、地圖清單），另有 `abdata/map`。
- OK 基礎庫（`base_v1`）目前只收角色資源：
  - 原版 `abdata/studio*`（除了 `studio/base/00`）與 `abdata/map` 都沒轉。
  - mod 帶的 Studio 道具約 1.7 萬筆被順帶轉了，但只用角色的查詢組，沒有道具清單與道具專用查詢，不能直接當道具用。
- OK 卡能播的動畫來自 `Content/IndependentResources/PairedAnimations`（3.2 GB，24 個原始 Unity bundle）：
  - 包含 `studio/anime/00`、`01` 與 H 動畫；`studio_k/anime` 與 mod 動畫都不在裡面。
  - 這也是「專案不再有 Unity 檔」目標還沒完成的一塊。

## 原作 Studio 功能 × 現況

狀態：✅ 已有且可用於 OK 卡　◐ 部分　✗ 沒有

### 場景與物件
| 原作功能 | 現況 | 狀態 |
|---|---|---|
| 場景存讀（`【KStudio】` PNG，ObjectInfo 樹） | 只有自訂 `OKStudioScene`（`Saved/Studio/*.okstudio.json`），固定 2 個角色，沒有 KStudio 解析器 | ✗ |
| 物件樹：角色、道具、燈光、相機、資料夾、路徑，父子關係 | 只有女、男兩個角色 | ✗ |
| 拖放／選取／複製／刪除物件 | — | ✗ |
| 移動旋轉縮放把手（Guide Object） | `UOKStudioGuide`（W/E/R、格線），只動角色根節點 | ◐ |
| 道具（`abdata/studio/info` 的 ItemList、道具顏色／花紋、道具 FK） | 沒有清單解析，道具模型也不在基礎庫 | ✗ |
| 地圖（Studio map、地圖光照） | 沒有；只有自製 UE 關卡（CrowdRoom、StreetSocial） | ✗ |
| 燈光物件（方向光、點光、聚光；角色燈） | `SetupStudioView` 只有固定的 3 盞方向光 | ✗ |
| 相機物件、切換、存讀相機位置 | 一個繞行相機，存進場景 | ◐ |

### 角色
| 原作功能 | 現況 | 狀態 |
|---|---|---|
| 多角色 | 引擎端每張卡一個 `AKKPreviewActor`，10 個已驗證互不干擾；Studio UI 只開 2 個 | ◐ |
| FK 姿勢（逐骨旋轉，KKS_FKIK 同時 FK+IK） | 只有編輯器裡 OKAnimation 的骨骼編輯模式；遊戲內沒有 | ◐ |
| IK 姿勢（手腳、AdvIK 的肩膀／身體 IK） | Rust 有完整的 FinalIK FullBodyBipedIK 移植，但只給 H／捏人用，沒有使用者操作 | ◐ |
| 姿勢存讀（`UserData/Studio/pose`） | 編輯器的 `UOKPoseLibrary`；不讀 KK 姿勢檔 | ◐ |
| 手指形狀 | `SetHandShape`（左右手選擇） | ✅ |
| 視線、脖子朝向（前方／跟隨鏡頭／動畫／固定） | 視線與頭部看鏡頭有；沒有 KK 的脖子模式組 | ◐ |
| 表情（眉、眼、嘴、開口、眨眼） | `SetFacePattern` 等都有，捏人 UI 可用 | ✅ |
| 衣服狀態（9 部位 穿／半脫／脫） | 捏人 UI 可用，但不存進場景 | ◐ |
| 飾品顯示（Accessory_States） | 只帶在 `.okclothes` 資料裡，沒有執行期切換 | ✗ |
| 汗、淚、臉紅、體液（siru） | 淚永遠隱藏、臉紅只套卡上的值、體液沒有 | ✗ |
| 換卡、換衣服、換服裝組 | OK 卡編輯器與捏人 UI 都有 | ✅ |
| ABMX 骨骼修改 | 捏人 UI 有 1,192 個控制 | ✅ |
| 懷孕（PregnancyPlus） | 有 | ✅ |
| SkinEffects、ClothingBlendShape、ClothColliders | 沒有（BlendShape 只有低階 API） | ✗ |

### 動畫
| 原作功能 | 現況 | 狀態 |
|---|---|---|
| Studio 動畫清單與播放（含選項參數、速度、Yure、PV） | 已對照原作驗證；OK 卡只能用 PairedAnimations 裡的 `studio/anime/00/01` 與 H | ◐ |
| H 動畫、雙人對位 | 有（互動場景、雙人播放） | ✅ |
| mod 動畫、`studio_k` 動畫 | OK 卡無法播 | ✗ |
| 動畫轉 OK 動畫（.okanim） | OKAnimation 外掛完整，但約 2.5 萬筆的 KK→OK 批次庫目前不在磁碟上 | ◐ |
| 影片動作（GVHMR） | 有 | ✅ |

### 時間軸（Timeline 外掛）
| 原作功能 | 現況 | 狀態 |
|---|---|---|
| 任意屬性的關鍵影格（骨骼、表情、衣服、相機、燈光、道具） | 遊戲內時間軸只 key 角色根節點；編輯器的 OKAnimation 能 key 骨骼 | ◐ |
| 曲線編輯、插值 | 編輯器有；遊戲內只有線性／平滑／常數 | ◐ |
| 相機路徑 | 沒有 | ✗ |
| 讀原作 Timeline 資料 | 沒有 | ✗ |

### 材質與畫面
| 原作功能 | 現況 | 狀態 |
|---|---|---|
| MaterialEditor（逐材質換著色器、改值、換貼圖） | 讀卡時完整套用；UI 只有原始數值編輯 | ◐ |
| 基礎庫模型的材質編輯 | OKModel 編輯器（含另存新檔） | ✅ |
| 畫面風格（KK 色階／HG） | 全域切換 | ◐ |
| 後製（PostProcessingEffect：bloom、DOF、色調…） | 固定的 Studio 後製 | ✗ |
| 截圖（Screencap：解析度、透明背景） | 只有 MCP 截圖與卡片四視圖 | ◐ |
| 影片輸出（VideoExport） | 沒有 | ✗ |

### 物理
| 原作功能 | 現況 | 狀態 |
|---|---|---|
| 動態骨骼、碰撞體（KKS_Colliders） | 已對照原作驗證 | ✅ |
| 進階次級物理（PhysX／Jolt／MuJoCo） | 有，只在一對一互動開啟 | ◐ |

## 缺口總結
1. 沒有通用的場景物件樹，也沒有 KStudio 場景解析器（7,656 個場景讀不進來）。
2. Studio 道具、地圖、Studio／mod 動畫不在 OK 基礎庫，動畫還依賴原始 Unity bundle。
3. 遊戲內沒有 FK／IK 擺姿與姿勢檔。
4. 沒有燈光、相機、道具物件；沒有相機路徑、影片輸出、後製設定。
5. 狀態控制不足：飾品顯示、淚／汗／體液、SkinEffects、ClothColliders。
6. 時間軸只 key 兩個角色的根節點。

## 建議的外掛分工

| 外掛 | 內容 | 依賴 |
|---|---|---|
| **OKStudio**（Runtime） | 場景物件樹（角色、道具、燈光、相機、資料夾、路徑）、OK 場景格式 `.okscene`（JSON + 縮圖 PNG）、選取與把手、遊戲內 Studio UI | KKUnityBundle、OKAnimation |
| **OKStudioLibrary**（Rust + 轉換器） | 基礎庫擴充：Studio 道具清單與道具查詢組、地圖、Studio／`studio_k`／mod 動畫轉成 OK 動畫（取代 PairedAnimations 的 Unity bundle） | ok-extract |
| **OKStudioImport** | 讀 KStudio 場景 PNG：角色轉 OK 卡（`remake_ok_card`）、道具／燈光／相機／時間軸資料轉 `.okscene` | OKStudio、OKStudioLibrary |
| **OKStudioPose** | 遊戲內 FK／IK 擺姿（Rust FinalIK）、手指、脖子與視線模式、KK 姿勢檔讀寫 | OKStudio |
| **OKStudioTimeline** | 任意屬性關鍵影格、曲線、相機路徑；可轉出 `.okanim` 與 Level Sequence | OKStudio、OKAnimation |
| **OKStudioRender** | 燈光物件、後製設定、截圖（解析度、透明背景）、影片輸出（Movie Render Queue） | OKStudio |
| **OKStudioState** | 飾品顯示、淚／汗／臉紅／體液、SkinEffects、ClothingBlendShape、ClothColliders；MaterialEditor UI | KKUnityBundle |

## 建議順序
1. OKStudio 物件樹與 `.okscene`（其他外掛都掛在它上面）。
2. OKStudioLibrary：Studio 道具與地圖進基礎庫、動畫轉 OK 動畫。
3. OKStudioPose。
4. OKStudioImport（讀 KStudio 場景，用 7,656 個場景做驗收）。
5. OKStudioTimeline。
6. OKStudioRender。
7. OKStudioState。

## 需要使用者決定
1. **Studio 主體放在哪裡**：遊戲內（執行期 UMG，像原作）、Unreal 編輯器內，或兩者都要（共用物件樹，兩套 UI）。
2. **KStudio 場景相容程度**：只匯入（讀進來轉成 `.okscene`），還是也要能存回 KStudio 格式。
3. **Studio 道具與地圖進基礎庫的範圍**：原版全部、mod 全部，或只收場景實際用到的（從 7,656 個場景統計）。
4. **動畫**：是否這次一併把 PairedAnimations 的 Unity bundle 換成 OK 動畫（完成「專案不再有 Unity 檔」的最後一塊）。

## 決定（2026-10-16，使用者）
1. Studio 物件樹共用，遊戲內與 Unreal 編輯器各一套 UI。
2. KStudio 場景只匯入（轉成 `.okscene`），不存回原作格式。
3. 原版 Studio 道具與地圖全部進基礎庫；mod 道具只收 7,656 個場景實際用到的。
4. PairedAnimations 的 Unity bundle 這次一併換成 OK 動畫。

## 階段
| 階段 | 內容 | 驗收 |
|---|---|---|
| S1 | OKStudio 外掛：物件樹（資料夾、角色、道具、燈光、相機，父子）、`.okscene` 存讀、`AOKSceneActor` 在世界生成場景；Unreal 編輯器 UI（大綱、屬性、預覽、新增／刪除／改父物件、存讀） | `OKStudio.EditorSelfTest`：兩張 OK 卡、一個基礎庫模型道具、燈光、相機、資料夾子物件；存讀一致；預覽角色與道具有部位、子物件世界座標正確 |
| S2 | 遊戲內 Studio UI 改用物件樹（取代目前只有兩個角色的 OKStudioScene） | 遊戲內自測：新增角色／道具／燈光、存讀 |
| S3 | OKStudioLibrary：原版 Studio 道具清單與道具查詢組、地圖進基礎庫；道具瀏覽 | 原版道具全部可在場景生成 |
| S4 | OKStudioPose：遊戲內 FK／IK、脖子與視線模式、KK 姿勢檔 | 姿勢存讀一致、IK 到達 |
| S5 | OKStudioImport：KStudio 場景 PNG → `.okscene`（角色轉 OK 卡、mod 道具按需轉入） | 7,656 個場景的匯入統計與抽樣比對 |
| S6 | 動畫：Studio／`studio_k`／H／mod 動畫轉 OK 動畫，移除 PairedAnimations | OK 卡播放與原作對照 |
| S7 | OKStudioTimeline、OKStudioRender、OKStudioState | 各自自測 |
