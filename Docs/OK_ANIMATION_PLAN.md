# OK 動畫系統規畫

> 2026-10-15 自 OK_Demo（`E:\PhysX\UE5.8\Samples\OK_Demo`，已退役）移入本專案。路徑與參數已改為本專案的名稱：OK_Demo → OpenKoikatsu、`Scripts/*` → `Tools/*`、`-Demo*` → `-Pair*`。整合計畫見 `Docs/PHYSX_ENGINE_PORT_PLAN.md`。

目標：任何來源的動作，都轉成一種**與角色無關**的「OK 動畫」；任何角色卡都能用「角色卡骨架 + OK 動畫」播放，並能大量匯入外部動作。

和原作 KK 的逐項差異見第 9 節；和 Unreal 原生動畫系統的差異與區分規則見第 10 節。

## 0. 實作狀態（2026-10-14）

### 0.1 程式位置

| 層 | 位置 | 內容 |
|---|---|---|
| Rust 核心 | `Tools/ok-extract/src/okanim/` | `mod.rs` 格式（`OKANIM02` + 版本 2 + LZ4(bincode)）、Spec（anim / group / blend / sequence）、Player；`convert.rs` KK clip／任意骨架 → OK、rest 自動對齊 `align_rest`、接觸偵測；`solve.rs` 群組求解（寫姿勢 → 配位 → 雙骨 IK → 腳鎖地與踏步 → 角色收尾）；`ffi.rs` C ABI；`tests.rs` 單元測試（8 個） |
| 橋接 | `Plugins/KKUnityBundle/.../KKOkAnimation.h/.cpp` | `FOkAnimBridge`（轉換、資訊、編輯、取樣、群組封裝）、`FOkAnimPlayer`；其他模組一律經過這裡，不得另外連結 `ok_extract.lib` |
| 角色 | `AKKPreviewActor` | `PoseMode 3`：自帶單人 OK player；`PoseMode 4`：由外部（群組播放器）每幀寫姿勢，角色只做臉、眼、陰影 |
| 外掛 Runtime | `Plugins/OKAnimation/Source/OKAnimation` | 資產 `UOKAnimSequence`、`UOKGroupAnimSequence`、`UOKBlendSpace`、`UOKAnimMontage`、`UOKSkeletonFamily`、`UOKRetargetProfile`、`UOKAnimationSettings`；`UOKAnimationComponent`、`AOKGroupPlayer`、`UOKContactNotifyState`、`UOKAnimationLibrary`、`FAnimNode_OKPlayer`、`FOKUnrealSkeleton`（Unreal 骨架 ↔ OK）；控制端口測試指令 `OKAnim.*` |
| 外掛 Graph | `.../OKAnimationGraph` | `UAnimGraphNode_OKPlayer`（AnimBP 節點「OK Player」） |
| 外掛 Editor | `.../OKAnimationEditor` | 匯入工廠（.okanim / .okgroup）、新增資產工廠、資產定義（分類 Animation › OK Animation）、OK 動畫編輯器、內容瀏覽器右鍵動作、`OKBatchConvert` commandlet |

### 0.2 與原規畫不同的決定（皆為通用解）

- **重定向不依賴 IK Rig / IK Retargeter**：改用 `UOKRetargetProfile`（骨名對應）+ Rust `align_rest`（rest 自動對齊到家族 rest，等同 IK Retargeter 的 auto-align）。內建預設：UE5 Mannequin、UE4 Mannequin、3ds Max Biped、Mixamo、KK；骨名比對忽略大小寫、分隔符與 `Bip001`／`mixamorig` 前綴。未對應的中間骨頭會併入下一根有對應的骨頭。好處：同一套程式同時用在「Unreal → OK」與「OK → Unreal 網格」，執行期也能用，不需要編輯器資產。
- **Montage 用通用的 `sequence` spec**：時間軸上的區段，每段可以是任何 OK 資產（動畫、群組、BlendSpace），後一段淡入時前面按權重淡出。
- **Sequencer**：不另做 MovieScene track；`UOKAnimationComponent::SequenceTime` 是 `Interp` 屬性，Sequencer 的一般 float 軌就能逐幀驅動（setter 會立刻擺出該時間的姿勢）。
- **AnimGraph 只有一個節點 `OK Player`**：因為 spec 統一，單一節點就能播動畫、BlendSpace（X/Y 腳位）、Montage 與群組中的某個角色。Unreal 網格上不解接觸（沒有 OK 角色身體），接觸求解只在 OK 角色上做。
- **接觸帶逐幀 offset 軌跡**（`track`）：同體型完全重現原動作，不同體型依目標 hips 高度縮放；偵測用「效應點到身體骨段表面」的距離。
- **在 Unreal 工具裡編輯**：OK 動畫「展開成 AnimSequence」（骨頭、曲線、接觸變成 `OK Contact` notify）→ 用 Unreal 原生工具修改 → 「寫回」。OK 資產本身仍是唯一真實資料。

### 0.3 使用方式

- 遊戲中（控制端口 `/exec?cmd=`）：`OKAnim.Play`、`OKAnim.Validate [n]`、`OKAnim.PlayPair`、`OKAnim.Blend <x> <狀態> [<y> <狀態>]`、`OKAnim.Montage <淡入秒> <狀態>...`、`OKAnim.Param <名> <值>`、`OKAnim.Shape <female|male> <索引> <值>`、`OKAnim.PlayUnreal <AnimSequence>`、`OKAnim.BatchKK [來源|*] [篩選|*] [上限] [資料夾]`、`OKAnim.SaveFamily`、`OKAnim.DumpUnreal`、`OKAnim.Report [reset]`、`OKAnim.Stop`。
- 大量匯入：遊戲中 `OKAnim.BatchKK` 輸出 .okanim → `UnrealEditor-Cmd OpenKoikatsu.uproject -run=OKBatchConvert -Files=<資料夾> -Dest=/Game/OKAnimation/KK`；Unreal／FBX 動作先用 Unreal 匯入成 AnimSequence，再 `-run=OKBatchConvert -Source=/Game/Mocap -Dest=/Game/OKAnimation [-Profile=...]`，或在內容瀏覽器右鍵「Convert to OK Animation」。
- 編輯器自我測試：`-run=OKBatchConvert -SelfTest`。

### 0.4 驗收數據（2026-10-14）

| 項目 | 結果 |
|---|---|
| KK clip → OK → 同角色播放（正常位 A_SLoop，12 個時間點） | 259 根動畫骨與原生**最大 0.000°**；20 根受物理動態影響的骨頭自動分開報告 |
| KK 動作批次轉換 | 1680 個全部成功，62 MB（平均 37 KB），約 46 ms／個 |
| KK 雙人 → OK 群組 | 偵測 12 個接觸（男手在女大腿、胯部、骨盆、雙腳踩地），轉換 0.28 s；同體型接觸誤差平均 0.1–0.3 cm |
| 體型自適應（男角身高 0 / 1） | 自動配位（各自移動約 3–4 cm、高個子屈膝），滿權重接觸誤差 1–3 cm |
| Unreal（UE4 Mannequin 持槍走路）→ OK → KK 女角 | 52 根骨，29 ms；離線逐段比對骨頭方向**誤差 0.0°** |
| 1D / 2D BlendSpace、Montage（3 段、0.3 s 淡入） | 遊戲中播放正常；Montage 長度與預期一致 |
| 編輯器 commandlet 自我測試 | 10/10 通過；展開成 AnimSequence ↔ 寫回往返最大 0.056°、hips 0.03 mm；接觸 notify 往返保留 |
| OK 動畫編輯器 | 序列（骨架樹、預覽、時間軸、工具列）與群組（spec 大綱）都能開啟 |

### 0.45 Roadmap（`OK_ANIMATION_ROADMAP.md` R1–R6）完成後的新增功能與驗收（2026-10-14）

- 動作庫：`Saved/OKAnim/KK/<female|male>/`（女 13821、男 11243 個，Rust 背景多執行緒批次 `OKAnim.BatchKK all * 100000 sex=male`，
  `ok.Anim.BatchThreads`）、`Saved/OKAnim/Pairs/`（3342 個雙人群組，`ok.Interaction.PairBatch 1` + `/pose?filter=*`）；遊戲內 `OKAnim.Library [關鍵字]`。
- 遊戲內指令（新增）：`OKAnim.Library`／`LibraryPlay`、`RootMotion <in_place|root_motion|none> [travel=]`、`LivePlay`、`Matrix`、`WalkTest`、
  `Trio`、`GroupBlend`、`Posture`、`ParallelFinish`、`Profile [reset]`；CVar `ok.Anim.AsyncPair`（預設 1）、`ok.Anim.WalkClip`。
- commandlet：`-SelfTest`（39 項全通過）、`-Score`（26744 個動作評分 → score.json／review.txt）、`-Fbx=<檔或資料夾>`。
- 編輯器分頁：Contacts、Poses、Montage Sections、Skeleton Family、OK Assets；工具列 Modifiers（含時間工具）、Record、Load Cards、
  Edit in Sequencer；Preview Settings › Retarget／Cards／RoleHeight。編輯器自我測試：序列 21 項、群組 23 項全通過。
- 匯入：.bvh（CMU 骨名）、.fbx（`bImportFbxAsOK` 開啟時拖曳即轉）；右鍵 Montage／BlendSpace／LevelSequence「Convert to OK Animation」。

| 驗收 | 結果 |
|---|---|
| 體型矩陣（侍奉，雙方 4×4 身高） | 伙伴接觸最差 14.3 → **0.0 cm**（承載肢體 IK、伙伴優先、IK 兩輪，錯誤 #87） |
| 體型矩陣（後背位） | 最差 3.5 cm（衝突的身體接觸，配位已取折衷） |
| 根運動 | root_motion 3.1 s 前進 175 cm；in_place 0 cm |
| 走路混入 | 起始偏離 0.5 hips 高：權重 0.85 → 0；一般播放 0 |
| 三人群組 | 27 接觸，第三人走入後最大 5.3 cm |
| 即時重載 | 改檔後播放中動畫自動換新（1 次重載） |
| FBX／BVH／Unreal 資產 | FBX 往返 0.000°；BVH 往返 0.000°、手肘 90.0°；Montage／BlendSpace／Additive／LevelSequence 全部可轉 |
| 效能（雙人 OK 播放） | GT 10.2 → 7.2–8.4 ms（約 120–139 FPS） |

### 0.5 OK 動畫編輯器的 Animate 模式（Maya 式 K 動作）

開啟任一 `UOKAnimSequence`（內容瀏覽器 › Animation › OK Animation › OK Animation Sequence 可直接新增「rest 姿勢、61 格」的空白動畫）：

| 功能 | 操作 |
|---|---|
| 選骨 | 視窗中點關節（藍點；選取黃色、本格有 key 為紅色），Ctrl/Shift 多選；左側骨架列表點名稱；Up/Down 往父/子骨走 |
| 擺姿勢 | E 旋轉、W 移動（Gizmo；Ctrl+\` 切世界/局部座標）。旋轉任何骨頭；移動 hips 會平移身體；移動其他骨頭（手、腳…）用雙骨 IK |
| 設 key | Auto Key（預設開，擺了就 key）；關閉時擺的姿勢在換格時還原，按 S 才 key。S 全部、Shift+E 只 key 旋轉、Shift+W 只 key 位移；工具列 Key All |
| 時間軸 | 點/拖曳換格；點菱形選 key（Shift 加選）、拖曳移動 key、Del 刪除、右鍵選單；`,` `.` 上/下一個 key，Alt+`,` Alt+`.` 上/下一格，Alt+V 播放，Home/End |
| Channel Box | 選取骨頭的 Rotate X/Y/Z（度）與 hips 的 Translate X/Y/Z，可直接輸入或拖曳；本格有 key 的通道顯示紅色 |
| Graph Editor | Unreal 曲線編輯器：選取骨頭的每個通道一條曲線，可改切線、內插（預設 spline、自動切線） |
| 其他 | Ctrl+C / Ctrl+V 複製貼上姿勢（貼的是最終姿勢，任何格都一樣）、Delete Key、Collapse Layer（把圖層烘進 base）、Clear Layer；全部支援 Undo/Redo |
| Dope Sheet | 每根有 key 的骨頭一列；點列名選骨、點/Shift 選 key、拖曳逐骨搬移、Del 或右鍵刪除 |
| 鏡像 | Mirror Pose：選取骨頭的對側取鏡像姿勢；沒選取時整個姿勢左右互換（`_L/_R`、`.L/.R`、`Left/Right` 自動配對，hips 位置也鏡像） |
| 切線 | Spline / Linear / Stepped：本格（選取骨頭，未選取時全部）的 key 內插方式；Stepped 用於 blocking |
| 長度 | 時間軸旁的 Frames 欄位：加長（最後一格延長）或縮短（超出的 key 移除） |
| Ghost / Trail | 前後 N 格的骨架殘影（藍＝前、綠＝後）；選取骨頭整段動畫的運動軌跡（每格一點、目前格放大） |
| 群組預覽 | 群組動畫的其他角色同時顯示在同一個預覽網格上 |
| BlendSpace 面板 | 軸範圍與樣本點；拖曳十字游標即時預覽混合，Shift+拖曳移動樣本（可 Undo） |
| 骨架族預覽網格 | 工具列 Family Preview Mesh：由骨架族 rest 自動產生「每根骨頭一段方柱」的預覽網格並設為預設，KK 骨架族的動作可直接 K（不需角色模型） |

設計：key 存在 `UOKAnimSequence::KeyLayer`（每骨 6 條 `FRichCurve`），疊在 base 動作上（類似 Maya 動畫圖層）；base 存在 `SourceData`，播放用的 `Data` 是 base + 圖層烘焙結果。所以對 KK／Unreal 轉來的動作做微調不會破壞原始 mocap，Clear Layer 即可還原。

Gizmo 的數學（任意骨架、任意對齊都成立）：預覽網格上骨頭的世界旋轉 `W' = Δ·R'`（R' 為對齊後的預覽 rest），動畫來源空間的世界旋轉 `S = Δ·A = W'·R'⁻¹·A`（A 為軌道的 rest_world）；在世界空間轉 G，等於在局部旋轉後面乘上 `S⁻¹·G·S`。hips 位移同理換算到「hips 父骨 rest 空間、以 hips 高度為單位」。

自我測試（`OKAnim.OpenEditor <資產>` 後 `OKAnim.EditorSelfTest [exit]`，結果寫到 `Saved/OKAnim/editor_selftest.txt`）實際操作編輯器：
Gizmo 轉 30° 後骨頭世界旋轉誤差 0.000°、自動 key、Channel Box 輸入、hips 平移誤差 0 cm、手 IK 到達目標誤差 0 cm、Undo 還原 0 cm、時間軸搬移 key、複製貼上姿勢 0.000°、Collapse 前後 0.0000°——11/11 通過。
commandlet 自我測試的「展開→寫回」往返誤差也降為 0（改讀原始 key）。

尚未自動化驗證：AnimBP 中 `OK Player` 節點（與編輯器預覽共用同一條取樣路徑，已驗證）。

```
KK 動畫（Unity clip / 控制器狀態） ─┐
Unreal 動畫資產（任意骨架）        ─┤
Unreal 標準骨架動畫（Mannequin…）  ─┤                 ┌─► OK 動畫 (.okanim v2) ─► 任何角色卡骨架播放
3ds Max 骨架（Biped / CAT）         ─┼─► OK 骨架 ────┤
外部動作（Mixamo、BVH mocap…）     ─┘                 └─► 在 Unreal 編輯（Control Rig / Sequencer）
```

---

## 1. 兩個核心定義

### 1.1 OK 骨架（標準、與角色無關）

- **骨骼**：沿用 KK 的動畫層骨骼 `cf_j_*`（hips、spine01–03、neck、head、肩、上臂、前臂、手、五指各三節、大腿、小腿、腳、腳趾），另加 `cf_pv_*` IK 目標骨。
  - 不含體型骨（`cf_s_*`、`cf_d_*`、`cf_hit_*`）、頭髮骨和配件骨。
- **rest pose**：KK 原生、未套體型（shape 0.5）的 T-pose。男女共用同一套骨名，各有一份 rest。
- **規格檔**：存成一份 JSON，內容是每根骨的名稱、父骨、rest local TRS（Unity 座標），以及一個**骨架版本雜湊**。OK 動畫用這個雜湊確認相容性。
- **UE 資產**：同一份規格產生 `USkeleton`，男女各一，放在 `/Game/OK/Skeleton`，另附一個預覽網格。這份資產用於 UE 端編輯和重定向。
- **為什麼可行**：所有角色卡的 `cf_j_*` 階層和 rest 旋轉都相同。體型只改 `cf_s_*`、`cf_n_height` 等骨，所以 `cf_j_*` 的旋轉可以直接通用。

### 1.2 OK 動畫（.okanim v2）

**最大的優勢：不綁定任何一副骨架。** 同一段 OK 動畫可以套用在各種 OK 骨架上：
- 女性、男性。
- 每張角色卡套用體型後的骨架。
- 模組身體（多出或缺少部分骨頭）。
- 未來版本的 OK 骨架。

為了做到這點，有四條規則：

1. **依骨名對應，不依索引**：每條軌記錄骨名（`cf_j_*`），播放時用名字找目標骨架上的骨頭。
   - 目標骨架缺的骨頭直接略過。
   - 目標骨架多出的骨頭維持它自己的 rest。
   - 名稱不同的骨頭用別名表對應（例如模組身體的骨名）。
2. **存「相對 rest 的旋轉差」，不存絕對局部旋轉**：
   - 每格存的是「這根骨頭相對 OK 標準 rest，在骨架共同座標系下轉了多少」。
   - 播放時套到目標骨架**自己的 rest** 上。
   - 男女 rest 不同、模組身體的骨頭軸向不同，都能得到相同的動作。
3. **位移正規化**：
   - 骨盆位移除以標準骨盆高度，播放時乘回目標骨架的骨盆高度。
   - 根運動同理，以腿長正規化，避免小個子角色滑步。
4. **接觸點用「語意」描述**：例如「左手接觸對方右肩，偏移 3 cm」，不寫死世界座標。播放時依目標骨架的實際比例解算 IK。

檔頭不再用「骨架雜湊必須一致」，改記錄**骨架家族與 OK 標準 rest 版本**：只要是同一家族（`cf_j_*` 階層）就能播放；版本不同時，由 rest 差異自動換算。

延伸現有的 OKCLIP01，格式為自訂二進位加 LZ4 壓縮，和動畫快取同一套做法。

| 區塊 | 內容 |
|---|---|
| 檔頭 | 魔數、版本、骨架家族、OK 標準 rest 版本、fps、格數、循環旗標、根運動模式（原地／帶位移） |
| 骨骼軌 | 每條軌：骨名 + 相對 OK 標準 rest 的旋轉差（四元數，Unity 座標） |
| 骨盆軌 | hips 位置，**以「骨盆高度」正規化**，播放時乘上角色卡自己的骨盆高度 |
| 根運動 | 角色整體位移、轉向（可選） |
| 接觸軌 | 手腳著地、扶物、雙人接觸點的開始／結束與權重曲線；播放時用 IK 修正到角色卡實際比例 |
| 表情軌 | 表情、視線、嘴型曲線（可選） |
| 中繼資料 | 名稱、分類、標籤、來源、授權、雙人動作的角色 A／B 標記 |

---

## 2. 四條轉換管線

### 2.1 KK 動畫 → OK 動畫（批次，Rust CLI）

- **輸入**：
  - 遊戲動畫資源（單人約 1.3 萬個 clip、99 個雙人動作）。
  - 現成的解碼快取（`Saved/OkAnimCache`）。
- **流程**：
  1. 在 OK 骨架（未套體型）上，用現有 Rust 取樣器逐格取樣控制器狀態。混合樹的參數（例如身高）可以取固定值，也可以每個身高變體各輸出一份。
  2. 只寫 `cf_j_*` 旋轉、正規化後的骨盆位移和 `cf_pv_*`。
  3. 雙人動作：兩個角色各自一條軌，相對位置記在根運動。遊戲的 MotionIK 資料轉成接觸軌。
- **驗證**：同一張卡分別用「原生播放」和「OK 動畫播放」，逐格比對骨骼位置，誤差應小於 1 mm。
- **優點**：轉完後執行時不必再解析 Unity 資料包，可以徹底取代載入慢的問題。

### 2.2 KK 骨架 → OK 骨架

- 骨名、階層相同，對應是一對一，不需要重定向。
- **工作內容**：寫出 OK 骨架規格 JSON。可以重用現有的 `--dump-rest-skeleton`，加上骨骼篩選。
- **角色卡骨架**等於 OK 骨架加上該卡的體型，這部分由現有的 shape 系統負責。

### 2.3 Unreal 標準骨架 → OK 骨架（UE IK Retargeter）

> **實作改變**（見 0.2）：最後沒有用 IK Rig／IK Retargeter，改為 `UOKRetargetProfile` 骨名對應 + rest 自動對齊（Rust `align_rest`），執行期與編輯器共用。以下為原規畫，保留作為比較。

- **建一份 OK IK Rig**，定義這些鏈：
  - 脊椎、頸、頭。
  - 左右手臂、左右腿。
  - 五指各一條。
- **來源骨架**：UE5 Mannequin（Manny/Quinn）、UE4 Mannequin 和 MetaHuman 都有現成的 IK Rig，直接建立 Retargeter 資產即可。
- **rest pose 差異**：A-pose 對 T-pose 在 Retargeter 的「重定向姿勢」裡對齊。KK 左右手臂軸向是鏡像的，要在鏈設定中確認。
- **輸出**：OK 骨架上的 UAnimSequence，再用 2.5 的匯出器轉成 `.okanim`。

### 2.4 3ds Max 骨架 → OK 骨架

- 建議**統一走 FBX → UE**，不另外寫 MaxScript 匯出器，這樣所有外部來源共用同一條重定向管線。
  - 3ds Max（Biped `Bip01 …`、CAT）匯出 FBX，匯入 UE 時自動建立 Skeleton。
- **骨名範本**：預先做好 Biped、CAT、Mixamo、MMD、BVH（CMU）的 IK Rig 範本，依骨名特徵自動選用。
- **BVH mocap**：先用命令列轉成 FBX，例如 Blender 背景模式，再進同一條管線。

### 2.5 Unreal 編輯 → OK 動畫（匯出器）

- 在 OK 骨架上用 **Control Rig** 做 FK 加四肢 IK，在 **Sequencer** 製作，烘成 UAnimSequence。
- 編輯器工具逐格讀骨骼 local transform，轉成 Unity 座標（轉換函式現成），呼叫 Rust 的 `build_dense_clip`（新增一個 FFI 開口）寫出 `.okanim`。
- 預覽時可以選一張角色卡，用現有的 Rust 播放路徑即時顯示「卡骨架 + 這段動畫」。

### 2.6 Unreal 動畫資產 → OK 動畫

對象是任何已經在 Unreal 裡的動畫，不論綁在哪副骨架上，例如：
- 商城或 Fab 動作包、UE5 Mannequin 和 MetaHuman 的動畫、Lyra 範例。
- 專案自己做的動畫。
- Live Link 動捕錄製的結果。

| 輸入類型 | 處理 |
|---|---|
| UAnimSequence | 直接處理，見下方流程 |
| AnimMontage | 依 section 或整段烘成 AnimSequence 再處理；section 名稱寫進中繼資料 |
| Level Sequence（Sequencer） | 用 Sequencer 的「烘焙動畫」輸出 AnimSequence；鏡頭等非骨架軌忽略 |
| Control Rig 動畫 | 先烘成 AnimSequence |
| BlendSpace | 依取樣點各烘一段，或指定參數值烘一段；混合參數寫進中繼資料 |
| Additive 動畫 | 依它的 base pose 先套回完整姿勢再處理，或標記為 additive 另存 |
| Live Link / Take Recorder 錄製 | 錄製結果本身就是 AnimSequence，直接處理 |

**流程（UE 編輯器批次工具，可和第 3 節共用）：**

1. **判斷來源骨架**
   - 已知骨架（Mannequin、MetaHuman、Mixamo 骨名、Biped 骨名等）：直接套用對應的 IK Rig 範本。
   - 未知骨架：用 UE 的自動角色化功能（IK Rig 的自動建立重定向鏈）先產生草稿 IK Rig。草稿存起來，人工確認一次後就能重複使用。
2. **重定向到 OK 骨架**
   - 用 IK Retargeter 批次輸出 OK 骨架上的 AnimSequence。
   - 來源若是 A-pose，在重定向姿勢裡對齊 OK 的 T-pose。
3. **根運動**
   - 依來源動畫的 root motion 設定，決定輸出「原地」或「帶位移」，寫進檔頭。
4. **曲線**
   - 動畫曲線（例如腳著地曲線 `FootLock`、表情 morph 曲線）依名稱對照表轉成接觸軌或表情軌。
   - 對照不到的保留在中繼資料，不丟棄。
5. **匯出**：用 2.5 的匯出器寫成 `.okanim`，記錄來源資產路徑、原骨架和授權。

**另一條路：直接播放。** 修好現有的 `SetUnrealAnimation` 路徑後，UE 動畫也能不轉檔、直接在角色卡上播放，適合邊做邊看的預覽。目前有兩個問題要先處理：
- 這條路徑只在編輯器內有效。
- 疑似有「身高縮放兩次」的問題，需要實測。

正式內容建議仍轉成 `.okanim`，播放、驗證和打包都只走同一條路。

### 2.7 在 Unreal Editor 編輯 OK 動畫與相關參數

原則：**`.okanim`（與骨架無關的資料）才是本體**。Unreal 的 AnimSequence 只是「把它展開到某一副 OK 骨架上」的**編輯視圖**，用完轉回去，不會把動畫綁死在某副骨架上。

- **Unreal 資產 `UOKAnimation`**：直接存與骨架無關的 OK 動畫資料，以及中繼資料和參數。這是內容瀏覽器裡看到、被遊戲引用的那個資產。
- **開始編輯時**：選一副 OK 骨架，可以是女性、男性、某張角色卡套用體型後的骨架，或模組身體。系統依那副骨架的 rest 展開成一個暫時的 AnimSequence，連同接觸 Notify 和曲線，交給 Unreal 原生編輯器。
- **存檔時**：把 AnimSequence 換算回「相對標準 rest 的旋轉差」和正規化位移，寫回 `UOKAnimation`。所以在女性骨架上改的動作，男性和其他角色卡一樣能用。
- **換一副骨架編輯也可以**：同一段動畫可以先在女性骨架上調手，再在男性骨架上檢查，資料始終只有一份。

骨骼、曲線、接觸點都用 Unreal 原生的動畫編輯器、Sequencer 和 Control Rig 編輯，不重做一套編輯器。

**資料在 Unreal 裡的對應：**

| OK 動畫內容 | 在 Unreal 裡的形式 | 用什麼編輯 |
|---|---|---|
| 骨骼旋轉、骨盆位移 | 編輯時展開到所選 OK 骨架上的暫時 AnimSequence 骨骼軌 | 動畫編輯器、Sequencer + Control Rig（FK／IK） |
| 根運動 | AnimSequence 的 root motion | 同上 |
| 接觸軌（手腳著地、扶物、雙人接觸） | 自訂 `OKContact` AnimNotifyState：作用端（左手、右腳…）、目標（地面、自己某骨、對方某骨）、偏移、權重曲線、依體型插值 | 動畫編輯器的 Notify 軌，拖拉起訖時間 |
| 表情、視線、嘴型 | 動畫曲線（依 OK 命名規則） | 動畫編輯器的曲線編輯器 |
| 中繼資料與播放參數 | 掛在資產上的 `UOKAnimationMeta`（AssetUserData） | Details 面板 |
| 多人互動動作 | `UOKGroupAnimSequence`：1～N 段 OK 動畫（與骨架無關）+ 正規化的相對根位置 + 接觸圖 | 多人編輯模式或 Sequencer（各角色槽任選 OK 骨架同場編輯） |

**可編輯的參數（`UOKAnimationMeta`）：**
- **基本**：名稱、分類、標籤、來源、授權。
- **播放**：循環、速度、淡入淡出時間、根運動模式（原地／帶位移）。
- **體型適應**：骨盆位移的正規化方式、各肢體 IK 開關與權重、腳是否鎖地、地面高度。
- **雙人**：角色 A／B 的身高對齊方式、接觸優先順序。
- **全域預設值**：在專案設定（`UOKAnimationSettings`）中調整，單一動畫可以覆寫。

**OK 動畫專用編輯器面板（補足原生編輯器沒有的部分）：**
- **角色卡預覽**：選一張或多張角色卡，用執行時同一條 Rust 管線即時播放「卡骨架 + 這段動畫」。包含體型、IK 修正和頭髮衣服物理，所見即所得。
- **體型比較**：同時並排顯示多張體型差異大的卡，直接看手腳有沒有到位、有沒有穿模。
- **驗證**：一鍵跑關節角度、滑步、穿模檢查，結果標在時間軸上，點一下跳到出問題的那一格。
- **按鈕**：「在 Sequencer 中用 Control Rig 編輯」、「匯出 .okanim」、「從 .okanim 重新匯入」。

**來回轉換：**
- **匯入**：`.okanim` → 產生 AnimSequence、接觸 Notify、曲線和 Meta。所有來源轉出來的 OK 動畫都能拿回 Unreal 修改，包括 KK 動作和外部大量匯入的動作。
- **匯出**：存檔時自動重新產生 `.okanim`，也可以手動匯出。
- **即時同步**：遊戲或 PIE 執行中修改資產並存檔後，執行時重新載入該動畫，不必重開遊戲。

---

## 3. 大量匯入外部動作（批次工具）

用一個 UE commandlet `OKImportMotions` 串起整條流程，可在背景執行：

```
資料夾（FBX / BVH）
  → 依骨名自動辨識骨架種類（Mannequin、Mixamo、Biped、CAT、MMD、CMU…）
  → 套對應的 IK Rig 範本 → 批次重定向到 OK 骨架
  → 自動整理：去掉 T-pose 起始格、偵測循環點、原地化（可選）、自動偵測腳著地 → 接觸軌
  → 驗證：用 3～5 張體型差異大的卡實際播放
  → 輸出 .okanim + 目錄 JSON（分類、標籤、長度、來源、授權）
```

驗證以分數排序，分數低的列入待人工檢查清單，不擋整批。項目包括：
- 關節角度是否超過限制。
- 腳滑步。
- 自己穿模，用現有的身體碰撞膠囊檢查。
- 手指翻折。

---

## 4. 執行時：角色卡骨架 + OK 動畫

統一由 Rust 播放，現有的 `.okanim` 與 SharedAnimation 路徑擴充即可，打包版也能用：

1. 取樣 `.okanim`，寫入 `cf_j_*` 的 local 旋轉。
2. 骨盆位移乘上「卡的骨盆高度 ÷ OK 骨架骨盆高度」。
3. 套用體型（shape、ABMX）。
4. 依接觸軌做 IK 修正：
   - 腳：著地時鎖定腳的位置，避免滑步和穿地。
   - 手：扶物、碰身體時把手放回接觸點。
   - 雙人：依兩人身高對齊根位置，再用接觸軌解 IK。可重用現有的全身 IK 求解器和接觸機制。
5. 頭部看向、DynamicBone、物理等後處理照舊。

---

## 5. 主要難點與對策

| 難點 | 對策 |
|---|---|
| rest pose 不同（A／T pose、骨頭軸向不同） | 一律以「相對 rest 的旋轉差」轉換，Retargeter 用重定向姿勢對齊 |
| 座標系（Unity 左手 Y-up、UE 右手 Z-up、Max Z-up） | 只在邊界轉換一次，內部統一用 Unity 座標（和現有 Rust 一致） |
| 體型差異造成手腳不到位、穿模 | 骨盆正規化，加上接觸軌 IK 修正 |
| 手指（KK 三節、各來源數量不同） | IK Rig 手指鏈。來源缺手指時用預設手型 |
| 雙人動作 | 雙軌、相對根運動、接觸軌。依身高對齊，再解 IK |
| 大量匯入的品質 | 自動驗證與評分，低分項目人工檢查 |
| 授權 | 中繼資料記錄來源和授權，目錄可篩選 |

---

## 6. 分階段計畫

| 階段 | 內容 | 產出 | 估計 |
|---|---|---|---|
| P0 | 規格 | OK 骨架 JSON（男女）、.okanim v2 格式、骨架雜湊 | 2–3 天 |
| P1 | KK → OK 批次轉換（Rust CLI）與逐格比對驗證 | 全部 KK 動作的 .okanim | 3–5 天 |
| P2 | 執行時播放器統一（含骨盆正規化、腳部 IK） | 任何卡播放任何 OK 動畫 | 1 週 |
| P3 | UE 端：OK 骨架資產、預覽網格、Control Rig；`OKContact` Notify、`UOKAnimationMeta`、`UOKGroupAnimSequence`；.okanim 雙向轉換與存檔自動匯出；OK 動畫編輯面板（角色卡預覽、體型比較、驗證）；執行中即時重新載入 | 在 Unreal Editor 編輯 OK 動畫與參數 | 2.5–3.5 週 |
| P4 | 重定向管線：IK Rig 範本（Mannequin、MetaHuman、Mixamo、Biped、CAT、MMD、BVH）+ 未知骨架自動角色化 + 批次 commandlet；Unreal 動畫資產（Sequence、Montage、Level Sequence、BlendSpace、Additive）的烘焙與轉換 | 大量匯入工具、Unreal 動畫 → OK 動畫 | 1.5–2.5 週 |
| P5 | 1～N 人互動（7.4 節）：`UOKGroupAnimSequence`、接觸自動偵測、群體配位、姿勢適應、全身 IK 交替解算、程序化踏步與走路混合、體型組合矩陣驗證 | 任何體型組合的多人互動動作 | 4–6 週 |
| P6 | 自動驗證、目錄、遊戲內瀏覽與搜尋 | 品質報告、動作庫 UI | 1 週 |
| P7 | OK 元件套組其餘部分：`UOKAnimMontage`、`UOKBlendSpace`（1D／2D、單人與群組、相位同步、接觸混合、KK 混合樹轉換）、動畫圖節點、Sequencer 軌、縮圖與資產動作 | 完整的 OK 動畫元件 | 4–6 週 |
| P8 | OK 動畫編輯器完整版（7.3 節）：五種模式、全部面板、FK／IK 工具、姿勢庫、洋蔥皮、錄製、修改器 | Persona 等級的 OK 動畫編輯器 | 6–10 週 |

P3 會先做編輯器的最小版本（動畫模式：預覽視窗、時間軸、骨架樹、Details、驗證、Control Rig 展開與寫回），讓編輯流程先能用；P8 再擴充成完整版。

`UOKAnimSequence`、`UOKSkeletonFamily`、`UOKRetargetProfile`、`UOKAnimationComponent`、編輯器和匯入器屬於 P2～P4 的骨幹，隨那幾個階段一起做；P7 是在骨幹穩定後補齊的進階元件。

建議順序：**P0 → P1 → P2**。先讓「KK 動畫 → OK 動畫 → 任何卡播放」完整跑通，用逐格比對確保和原生播放一致。這同時解決動作載入慢的問題，也替後面所有外部來源打好播放基礎。接著 **P4（大量匯入）** 和 **P3（UE 編輯）** 可以並行，最後做 **P5 雙人**。

---

## 7. OK 元件套組

一整套與骨架無關的 OK 元件。命名統一用 `OK` 前綴，放在新的 `OKAnimation`（執行時）與 `OKAnimationEditor`（編輯器）兩個模組。

### 7.1 設計關鍵：`UOKAnimSequence` 不繼承 `UAnimSequence`

Unreal 的 `UAnimSequence`，以及所有 `UAnimationAsset`，都**必須指定一個 `USkeleton`**，繼承它就等於又綁回某副骨架。所以：

- `UOKAnimSequence` 是**獨立的資產類別**，繼承 `UObject`，存與骨架無關的資料（骨名軌、相對 rest 的旋轉差、正規化位移、接觸、曲線、參數）。
- 和 Unreal 動畫系統的連接，交給下面的**轉接元件**：動畫圖節點、Sequencer 軌、編輯用的暫時 AnimSequence。在任何 OK 骨架上，都是播放當下才換算。

### 7.2 元件清單

**資產（內容瀏覽器中可見、可編輯）**

| 元件 | 用途 |
|---|---|
| `UOKAnimSequence` | 單段 OK 動畫。對應 `.okanim`，可雙向匯入匯出 |
| `UOKGroupAnimSequence` | 1～N 人互動動畫：每個角色槽一段 OK 動畫 + 錨點與正規化相對位置 + 接觸圖 + 適應策略（7.4 節）；雙人是 N=2 |
| `UOKAnimMontage` | 多段 OK 動畫串接、分段、分支（對應 Unreal 的 Montage 概念），例如前戲 → 循環 → 高潮 |
| `UOKBlendSpace` | 1D／2D 混合空間：依參數混合多段 OK 動畫，樣本可以是單人 `UOKAnimSequence`，也可以是 N 人 `UOKGroupAnimSequence`（7.5 節） |
| `UOKSkeletonFamily` | 骨架家族定義：標準骨名、階層、標準 rest、別名表、IK 鏈。取代「骨架雜湊」 |
| `UOKRetargetProfile` | 外部骨架 → OK 的對應：骨名對應表 + 是否對齊 rest；內建 UE5／UE4 Mannequin、Biped、Mixamo、KK 預設（實作見 0.2） |
| `UOKAnimationSettings` | 專案設定：全域預設的 IK 權重、腳鎖地、正規化方式、驗證用的角色卡清單 |

**執行時元件**

| 元件 | 用途 |
|---|---|
| `UOKAnimationComponent` | 掛在角色上的播放元件：播放、混合、淡入淡出、Montage 分支、速度。後端可選 Rust 角色卡管線或 Unreal 骨架網格 |
| `FAnimNode_OKSequencePlayer` | 動畫圖節點：把 `UOKAnimSequence` 即時套到這個網格所用的任何 OK 骨架上，讓一般 Unreal 骨架網格也能直接播 OK 動畫 |
| `FAnimNode_OKBlendSpacePlayer` | 同上，播放 `UOKBlendSpace` |
| `FAnimNode_OKContactIK` | 依接觸軌做手腳和雙人 IK 修正，可單獨放在任何動畫圖裡 |
| `UOKContactNotifyState` | 接觸區段標記：作用端、目標、偏移、權重、依體型插值 |
| `UOKAnimationLibrary` | Blueprint 函式庫：播放、查詢長度和曲線、取樣某一格姿勢、套用到角色卡 |

**Sequencer 與編輯器**

| 元件 | 用途 |
|---|---|
| ~~`UMovieSceneOKAnimationTrack`~~ | 實作改為 `UOKAnimationComponent::SequenceTime`（Interp 屬性，Sequencer 一般 float 軌驅動）；群組用 `AOKGroupPlayer::SetTime` |
| OKAnimSequence 編輯器 | 自訂資產編輯器，內容包括：<br>• 時間軸：骨骼、接觸、曲線、事件<br>• Details：參數<br>• 預覽：任選一張或多張角色卡、任選 OK 骨架並排比較<br>• 驗證面板：關節角度、滑步、穿模，問題標在時間軸上<br>• 「展開到所選骨架，用 Control Rig 編輯」與「寫回」 |
| 匯入器／Factory | `.okanim`、FBX、BVH 拖進內容瀏覽器時自動判斷骨架並轉成 `UOKAnimSequence`；資料夾批次匯入走第 3 節的流程 |
| 縮圖與資產動作 | 內容瀏覽器縮圖用預設角色卡擺出代表姿勢；右鍵選單提供「驗證」、「重新匯出 .okanim」、「在 Sequencer 開啟」 |

### 7.3 OK 動畫編輯器（參考 Unreal Persona 的一整套 OK 版）

操作方式、版面、快捷鍵都比照 Unreal 的動畫編輯器（Persona），讓會用 Unreal 的人不必重新學。差別在於編輯的對象是**與骨架無關的 OK 資產**，預覽對象是**任選的角色卡或 OK 骨架**。

**五種編輯模式**（右上角切換，比照 Persona 的 Skeleton／Mesh／Animation／Blueprint 模式）

| 模式 | 對應 Unreal | 編輯對象 |
|---|---|---|
| 骨架家族 | Skeleton Editor | `UOKSkeletonFamily`：骨名、階層、標準 rest、別名表、IK 鏈、左右鏡像對應 |
| 動畫 | Animation Editor | `UOKAnimSequence` |
| Montage | Montage Editor | `UOKAnimMontage`：分段、分支、串接 |
| BlendSpace | BlendSpace Editor | `UOKBlendSpace`：樣本點與參數 |
| 多人（1～N） | （Unreal 沒有） | `UOKGroupAnimSequence`：多角色同場、各槽選角色卡、身高滑桿、接觸圖、自動移動預覽 |

**面板**（比照 Persona 的分頁，可拖拉停靠）

| 面板 | 內容 |
|---|---|
| 預覽視窗 | 角色卡預覽（Rust 管線，含體型與頭髮衣服物理），可切換成 OK 骨架網格；骨骼顯示、接觸點與 IK 目標顯示、地面、背景、光源 |
| 時間軸 | 比照 Persona：骨骼軌、接觸軌（`OKContact`）、事件軌、曲線軌、根運動軌；縮放、範圍選取、播放速度、循環 |
| 骨架樹 | 依骨架家族顯示 `cf_j_*` 骨頭，選取後在視窗出現旋轉工具；顯示這段動畫有沒有這根骨頭的軌 |
| 曲線編輯器 | 骨骼旋轉、位移、表情曲線、接觸權重 |
| Details | 資產參數（`UOKAnimationMeta`）、所選骨頭或關鍵格的數值 |
| 預覽場景設定 | 選角色卡（可多選並排比較）、選 OK 骨架、物理開關、播放速度、頭部看向、背景 |
| 資產瀏覽器 | 列出 OK 動畫，可篩選分類、標籤、來源，雙擊切換、拖到時間軸做混合預覽 |
| 驗證 | 關節角度、滑步、穿模、接觸不到位；結果標在時間軸上，點擊跳到該格 |
| 重定向 | 比照 Retarget Manager：來源骨架、`UOKRetargetProfile`、重定向姿勢對齊、即時預覽 |

**編輯工具**

- **FK**：在視窗直接轉骨頭（以骨架家族的共同座標系操作，不受各骨架軸向影響），自動打關鍵格。
- **IK**：手腳、頭、骨盆的 IK 控制點，可用內建簡易 IK，或切換到 Control Rig 編輯模式。
- **姿勢**：複製／貼上姿勢、左右鏡像（自動處理 KK 左右軸向鏡像）、姿勢庫（存常用手型、表情）。
- **時間**：裁切、插入或刪除格、重新取樣 fps、改變速度、循環接縫自動平滑。
- **洋蔥皮與殘影**：顯示前後幾格的姿勢，方便調動作節奏。
- **錄製**：用 Live Link 動捕或 Control Rig 即時操作直接錄成 OK 動畫。
- **復原／重做**：全部動作走 Unreal 的交易系統，和一般編輯器一致。

**OK 動畫修改器**（比照 Unreal 的 Animation Modifier，可批次套用在多個資產上）

- **自動偵測接觸**：依腳的高度與速度，自動產生腳著地區段。
- **根運動擷取**：原地 ↔ 帶位移互轉。
- **左右鏡像**：產生左右相反的版本。
- **平滑化**：去除動捕抖動。
- **循環修正**：讓首尾銜接。
- **重新正規化**：修正骨盆高度與腿長比例。

**實作方式**

- **編輯器框架**：用 Unreal 公開的資產編輯器框架（`FWorkflowCentricApplication`）搭建，模式與分頁可擴充。
- **時間軸和曲線**：採用 Unreal 公開的 Sequencer 時間軸元件和 Curve Editor 模組，不從頭寫。
- **預覽視窗**：用 `FAdvancedPreviewScene` 放入角色卡預覽演員，或 OK 骨架網格。
- **Control Rig**：需要時把動畫展開成暫時 AnimSequence，交給 Control Rig 編輯模式，完成後寫回 `UOKAnimSequence`。
- **限制**：Persona 內部的時間軸和骨架樹多數不對外公開，不能直接重用，所以這幾個面板需要自己實作，但外觀和操作比照 Persona。

### 7.4 1～N 人互動動作與體型自動適應

OK 骨架的身高、比例差異很大（矮小到高大都有），而互動動作要讓手、身體、嘴等接觸點在任何體型組合下都對得上。因此「雙人動作」一般化為 **N 人群組動作**，並加上自動適應的解算器。

**資產：`UOKGroupAnimSequence`**（取代 `UOKPairAnimSequence`，雙人只是 N=2 的特例）

**基礎：沿用原本 KK 雙人互動的做法，延伸到 N 人。** 原本的雙人動作（`h_playback.rs` 的 MotionIK 流程）已經能在不同體型間對上接觸點，運作方式是：
- 每個動畫狀態定義「哪個作用端（手、腳、骨盆…）追蹤對方的哪根骨頭」。
- 偏移量依雙方體型參數，在「小」和「大」兩組數值之間插值（`correct_shape_values`）。
- 每格先凍結雙方姿勢，以對方的實際骨頭位置當 IK 目標，再用全身 IK（`IKSolverFullBodyBiped`）解算。
- 身高參數同時驅動混合樹，選擇身高變體的動作。

OK 版保留同一套機制和手感，只做這些延伸：
- **一般化到 N 人**：對方可以是任一個角色槽，也可以是地面或物件。
- **資料格式改成 OK 格式**：與骨架無關，也能自己在編輯器建立、自動偵測，不再只能用遊戲原生資料。KK 原有的 MotionIK 資料會直接轉成同樣的接觸格式，所以原本的雙人動作轉成 OK 後行為不變。
- **補上原本沒有的部分**：群體配位、姿勢適應策略、程序化踏步。這些用來處理身高差距比原作更大的組合，以及三人以上的配位。

| 內容 | 說明 |
|---|---|
| 角色槽（Role） | 1～N 個，每個槽一段與骨架無關的 OK 動畫，加上可選條件：身高範圍、性別、必要骨骼 |
| 錨點與相對位置 | 以共同錨點描述各角色根位置，例如場景原點或某角色的骨盆。位置依身高正規化，不存絕對座標 |
| 接觸圖 | 角色之間、角色與地面／物件之間的接觸：作用端、目標、偏移、起訖時間、權重曲線、優先順序、容許誤差 |
| 適應策略 | 每個接觸由誰配合（例如 A 不動、B 配合）、配合方式的偏好與上限（見下） |

**接觸的設定方式**
- **自動偵測**：分析來源動畫（KK 雙人動作、動捕、外部動作），找出不同角色身體碰撞膠囊之間「距離很近且持續一段時間」的部位，自動產生接觸軌與起訖時間。KK 的 MotionIK 資料直接轉成接觸。
- **手動設定**：在編輯器中選一個角色的作用端，再點另一個角色的目標部位，拖出起訖區間，設定權重和優先順序。
- **語意化**：接觸寫成「A 的右手 → B 的左肩，偏移 3 cm」，不寫世界座標，所以換任何體型都成立。

**執行時的自動適應（每幀，依實際 OK 骨架大小）**

1. **正規化播放**：每個角色依自己的骨盆高度與腿長，縮放骨盆和根位移。
2. **群體配位**：以最小平方法同時調整每個角色的根位置與朝向，讓所有接觸點的距離誤差最小。
   - 依優先順序加權。
   - 遵守地面限制，腳不可穿地。
   - 遵守「誰配合誰」的設定。
3. **姿勢適應**：根位置調整不夠的部分，依各接觸的策略依序處理，每項都有角度上限：
   - **移動**：整個人前後左右移動。
   - **蹲低／踮腳**：降低或抬高骨盆，膝蓋用 IK 跟著彎。
   - **前傾／後仰**：脊椎彎曲。
   - **伸手**：手臂 IK。
   - **低頭／抬頭**：頸部。
4. **全身 IK**：每個角色用現有的全身 IK 求解器打到接觸目標，並遵守關節限制。互相接觸的角色交替解算幾次直到收斂，依「誰配合誰」的權重分配移動量。
5. **自動移動（locomotion）**：配位後如果角色的腳需要移動，啟用程序化踏步：
   - 腳維持鎖地，直到偏移超過門檻。
   - 再以弧線踏到新位置，並依距離決定步幅與步數。
   - 需要長距離移動時（例如跟隨或走近另一個角色），從 OK 動作庫混合走路動畫。
6. **物理**：最後跑頭髮、衣服、胸部物理，以及前面做的衣服碰撞層。

**驗證**
- 對每個群組動作跑「體型組合矩陣」，例如每個角色取 140、155、170、185 cm 四種身高。
- 統計接觸誤差、穿模、滑步、關節角度。
- 標出哪些組合需要人工調整策略。

**編輯器**
- 7.3 節的「雙人」模式改為 **「多人（1～N）」** 模式。
- 預覽場景可為每個角色槽各選一張角色卡，用滑桿即時改身高，看接觸與自動移動的結果。
- 接觸圖以節點和連線顯示，可直接在視窗點選建立接觸。

### 7.5 1D／2D BlendSpace（單人與 N 人群組動作都支援）

KK 原本就用混合樹讓同一個動作依參數變化，例如依身高選變體、依強度改節奏（控制器裡的 1D 與 2D Cartesian 混合）。OK 版用 `UOKBlendSpace` 承接，並擴充到群組動作。

**樣本與參數**
- **1D**：一個參數軸，例如速度、強度、身高比例。
- **2D**：兩個參數軸，例如「強度 × 身高差」、「速度 × 方向」，樣本放在格點或任意位置，以三角化混合（比照 Unreal BlendSpace）。
- **樣本類型**：
  - 單人：`UOKAnimSequence`。
  - 群組：`UOKGroupAnimSequence`。同一個 BlendSpace 的所有群組樣本必須有相同的角色槽數量和相容的接觸圖，也就是相同的接觸配對，偏移和時間可以不同。
- **參數來源**：
  - 遊戲或 Blueprint 設定。
  - **自動綁定體型**：例如「身高差」軸自動取兩個角色的實際身高比，和 KK 用身高參數選動作變體的做法一樣。
- **KK 轉換**：KK 控制器的 1D 和 2D 混合樹直接轉成 `UOKBlendSpace`，樣本與參數範圍照原作。

**混合方式**
- **時間對齊**：循環長度不同的樣本，以相位同步（比照 Unreal 的 Sync Marker，例如每個抽送循環的起點），避免混合時節奏錯亂。
- **姿勢混合**：在與骨架無關的空間裡混合：
  - 骨頭旋轉差用四元數插值。
  - 正規化位移和根運動線性插值。
  - 混合完才套到目標 OK 骨架，所以任何體型都能用。
- **群組動作的接觸混合**：
  - 各樣本的接觸偏移、權重曲線、起訖時間依混合權重插值。
  - 某個接觸只出現在部分樣本時，權重依比例淡入淡出。
  - 角色的相對位置也一併插值。
- **順序**：先混合，再做 7.4 節的群體配位、姿勢適應、全身 IK 和自動移動。接觸永遠以「混合後的姿勢」解算，不會因為混合而對不上。

**編輯器**
- BlendSpace 模式比照 Unreal：1D 軸或 2D 平面上拖放樣本，拖動預覽點即時看混合結果。
- 群組 BlendSpace 會為每個角色槽各選一張角色卡預覽。
- 顯示「接觸相容性」檢查，不相容的樣本會被標出來。
- 可以把參數軸綁到預覽角色的身高滑桿，直接看身高差如何影響混合和接觸。

**執行時**
- `UOKAnimationComponent` 與動畫圖節點 `FAnimNode_OKBlendSpacePlayer` 都支援單人和群組 BlendSpace。
- 群組 BlendSpace 由一個「群組播放器」統一驅動所有參與角色，確保大家用同一組混合權重和相位。
### 7.6 播放路徑

```
UOKAnimSequence
  ├─► Rust 角色卡管線（體型、ABMX、頭髮衣服物理）── 遊戲中的角色卡
  └─► FAnimNode_OKSequencePlayer ── 任何使用 OK 骨架家族的 Unreal 骨架網格（預覽、其他專案、過場）
```

兩條路共用同一套取樣與換算程式（Rust 實作，透過 FFI 給 C++ 用），確保結果一致。

---

## 8. 現有可重用的程式

| 用途 | 位置 |
|---|---|
| 動畫取樣 | Rust 取樣器、解碼快取 `anim/disk_cache.rs` |
| 寫出 dense clip | `anim/shared.rs` 的 `build_dense_clip`（目前只在 crate 內使用） |
| 共用動畫的資產、匯入與播放 | `UKKSharedAnimation`、`KKSharedAnimationImporter`、`KKSharedAnimationDriver`，現有 75 個 `.okanim` 已能跨卡播放 |
| UE 動畫餵進 Rust | `SetUnrealAnimation` → `OKRetargetUnrealPose`（目前只在編輯器內有效，有疑似身高縮放兩次的問題待驗證） |
| 座標轉換 | `OKUnityLocalToUnreal` / `OKUnrealLocalToUnity` |
| 全身 IK、接觸修正 | `IKSolverFullBodyBiped`、`point_contact.rs`、雙人 MotionIK 求解流程 |
| 驗證 | `video_motion/validate.rs`（關節限制等檢查） |

需要新增：
- OK 骨架資產。
- 啟用 ControlRig、IKRig、LevelSequenceEditor 外掛（專案預設停用所有引擎外掛）。
- IK Rig 範本。
- 匯出器與批次 commandlet。
- .okanim v2 的接觸軌和表情軌。
- 雙人接觸格式。
---

## 9. 與 KK（Koikatsu）的差異

整理 OK 系統（本規畫加上本專案已完成的物理部分）和原作 KK 的差異。「已完成」表示本專案目前已經做到，「規畫」表示本文件的計畫內容。

### 9.1 動畫

| 項目 | KK | OK | 狀態 |
|---|---|---|---|
| 動畫格式 | Unity AnimationClip（含 Humanoid 肌肉曲線），存在壓縮資料包內 | `.okanim`：自訂格式加 LZ4，存骨名與相對 rest 的旋轉差、正規化位移 | 規畫 |
| 與骨架的關係 | 綁 Unity Avatar 與 KK 骨架，靠控制器和 Avatar 套到角色 | **不綁骨架**：依骨名與骨架家族套用到任何 OK 骨架，包括男女、不同體型、模組身體、未來版本 | 規畫 |
| 動作來源 | 只有遊戲內建和模組提供的動作 | KK 動作、Unreal 動畫資產、Mannequin／MetaHuman、3ds Max（Biped／CAT）、Mixamo、BVH 動捕、自己在編輯器製作 | 規畫 |
| 大量匯入 | 無，每個動作都要在 Unity 製作後打包 | 整個資料夾批次重定向、自動整理、自動驗證，輸出動作目錄 | 規畫 |
| 編輯 | 遊戲內只能選動作和調速度；製作要靠 Unity 外部工具 | Unreal 內完整的 OK 動畫編輯器（比照 Persona）：FK／IK、Control Rig、曲線、接觸、驗證、多卡並排預覽 | 規畫 |
| 載入速度 | 每次解析 Unity 資料包（本專案原本的後背位要 72 秒） | 先播第一個階段、其餘平行準備、解碼結果存成快取，第二次後約 1 秒 | 已完成 |
| 混合 | 控制器內的 1D／2D 混合樹，參數主要是身高 | 1D／2D BlendSpace，單人與群組動作都支援，相位同步，參數可自動綁體型 | 規畫 |

### 9.2 互動動作

| 項目 | KK | OK | 狀態 |
|---|---|---|---|
| 人數 | 雙人（部分場景 3 人，各自有專用資料） | 1～N 人，通用的群組動作資產 | 規畫 |
| 接觸資料 | MotionIK：遊戲原生資料，只能用遊戲裡已有的動作 | 語意化接觸圖（A 的右手 → B 的左肩），可自動偵測、在編輯器建立；KK 的 MotionIK 原樣轉入 | 規畫 |
| 體型適應 | 偏移量在「小」和「大」兩組值之間依體型插值，再用全身 IK | 保留同一套機制，再加上群體配位、姿勢適應（移動、蹲低踮腳、前傾、伸手）、程序化踏步與走路混合，可處理更大的身高差 | 規畫 |
| 驗證 | 無自動驗證 | 身高組合矩陣自動驗證：接觸誤差、穿模、滑步、關節角度 | 規畫 |

### 9.3 物理與外觀

| 項目 | KK | OK | 狀態 |
|---|---|---|---|
| 頭髮、衣服、胸部 | DynamicBone（Verlet），只撞角色卡設定的少數碰撞體 | 可在執行中切換 PhysX 5.11、KawaiiPhysics、Chaos、原始 Verlet；軟約束；骨長、碰撞、接觸全程檢查 | 已完成 |
| 身體碰撞 | 少量手動擺放的碰撞體 | 全身碰撞膠囊，依動畫姿勢自動縮放寬容度；長骨頭以整段檢查 | 已完成 |
| 衣服碰撞 | 頭髮常穿過衣服 | 依實際衣服網格自動加厚碰撞層，頭髮與飾品撞衣服表面，衣服撞皮膚 | 已完成（男角衣服尚未算出厚度） |
| 雙人互相碰撞 | 頭髮衣服不撞對方身體 | 頭髮衣服會撞對方（以對方的衣服表面為準） | 已完成 |
| 仰頭時頭髮穿身 | 常見 | 逐粒子寬容度與整段骨頭檢查，實測最深 1.2 cm、0 次明顯穿模 | 已完成 |
| 切換動作 | 直接切換，頭髮可能卡在身體裡 | 切換時先用 Chaos 跑 0.5 秒把頭髮彈出 | 已完成 |
| 柔軟度與重量 | 只有角色卡內的 DynamicBone 參數 | 遊戲內滑桿：硬度 1～10、重量 1～10 | 已完成 |
| 面向鏡頭 | 頭部可看向鏡頭 | 以臉的實際朝向計算最短旋轉，後仰 70°、低頭 45°、左右 65°，雙人一起 | 已完成 |
| 抗鋸齒 | 遊戲預設 | 非時間性：2 倍超取樣 + SMAA，頭髮擺動不殘影 | 已完成 |

### 9.4 平台與工具

| 項目 | KK | OK | 狀態 |
|---|---|---|---|
| 引擎 | Unity 5.6 | Unreal Engine 5.8（PhysX 5.11 分支） | 已完成 |
| 執行時調參 | 無 | 本機 HTTP 控制端口：即時改參數、量測、切換動作，不必重開遊戲 | 已完成 |
| 品質量測 | 無 | 穿模、抖動、伸長、面向鏡頭誤差等稽核數據，可逐項比較物理系統 | 已完成 |
---

## 10. 與 Unreal 原生動畫系統的差異

OK 元件刻意和 Unreal 的動畫概念對應，以降低學習成本，但**資料與行為不同**。本節是區分兩者的依據。

### 10.1 核心差別

| 項目 | Unreal 原生 | OK |
|---|---|---|
| 動畫與骨架的關係 | 動畫資產必須綁一個 `USkeleton`，換骨架要先重定向成新資產 | 動畫**不綁骨架**，依骨名與骨架家族在播放當下換算到任何 OK 骨架 |
| 骨頭資料 | 每骨的局部變換（位置、旋轉、縮放） | 每骨相對**標準 rest** 的旋轉差、正規化的骨盆與根位移；不存縮放（體型由角色卡負責） |
| 體型 | 不同體型就是不同骨架或網格，各自一套動畫，或在執行時重定向 | 體型是角色卡的屬性（shape、ABMX），同一段 OK 動畫自動適應 |
| 互動動作 | 沒有內建的多人互動資產，一般靠 Montage 同步或 Motion Warping 自行組合 | `UOKGroupAnimSequence`：1～N 人、語意化接觸圖、依體型自動配位與 IK |
| 接觸與 IK | 靠動畫圖節點和 Control Rig 自行搭建 | 接觸是動畫資料的一部分，播放器自動解算，可在編輯器設定和驗證 |
| 次級動態（頭髮、衣服） | Chaos Cloth、Rigid Body 節點、AnimDynamics | KK 的 DynamicBone 鏈，由 PhysX／Kawaii／Chaos／Verlet 解算（可切換），含衣服碰撞層與穿模稽核 |
| 角色來源 | 自己的骨架網格 | KK 角色卡，由 Rust 管線即時組裝並蒙皮 |
| 播放後端 | 動畫圖與 `USkeletalMeshComponent` | 主要是 Rust 角色卡管線；另有動畫圖節點，可在 OK 骨架家族的 Unreal 網格上播放 |

### 10.2 元件對照（同名概念、不同東西）

| OK 元件 | 對應的 Unreal 概念 | 差別 | 兩者怎麼接 |
|---|---|---|---|
| `UOKAnimSequence` | `UAnimSequence` | 不繼承它、不綁 `USkeleton`；資料是旋轉差與正規化位移 | 編輯時展開成暫時的 `UAnimSequence`，存檔時換算回來；Unreal 動畫可轉成 OK |
| `UOKGroupAnimSequence` | 無 | Unreal 沒有多人互動資產 | 在 Sequencer 中以多個角色同場編輯 |
| `UOKAnimMontage` | `UAnimMontage` | 段落裡放的是 OK 動畫；支援群組動作 | 匯入 Unreal Montage 時依段落轉換 |
| `UOKBlendSpace` | `UBlendSpace`（1D／2D） | 樣本與骨架無關；可放群組動作並混合接觸；參數可自動綁體型 | 匯入 Unreal BlendSpace 時依樣本點轉換 |
| `UOKSkeletonFamily` | `USkeleton` | 只定義骨名、階層、標準 rest、別名、IK 鏈；**不是**任何網格的骨架 | 由它產生 OK 骨架家族的 `USkeleton`，供編輯與 Unreal 網格播放 |
| `UOKRetargetProfile` | IK Rig／IK Retargeter | 骨名對應 + rest 自動對齊；目標固定是 OK 骨架家族 | 不使用 IK Rig；同一份對應雙向使用（Unreal → OK、OK → Unreal 網格），執行期可用 |
| `UOKContactNotifyState` | `UAnimNotifyState` | 繼承 Unreal 的 NotifyState，但內容是語意化接觸，由 OK 播放器解算 | 編輯時就是一般的 Notify 軌 |
| `UOKAnimationComponent` | `USkeletalMeshComponent`＋`UAnimInstance` | 播放對象是角色卡（Rust 管線）或 OK 骨架網格 | 可和動畫圖並存 |
| `FAnimNode_OKSequencePlayer` 等節點 | `FAnimNode_SequencePlayer` 等 | 播放的是 OK 資產，播放當下換算到目前的 OK 骨架 | 就是一般的動畫圖節點 |
| OK 動畫編輯器 | Persona（Animation Editor） | 外觀與操作比照 Persona，但是獨立的編輯器；預覽對象是角色卡，多一個多人模式 | 需要細修骨骼時，展開到 Control Rig 編輯模式 |
| `UOKAnimationComponent.SequenceTime` | Sequencer 的 Skeletal Animation 軌 | 用一般 float 軌驅動 OK 播放時間 | 和其他 Sequencer 軌放在同一個 Level Sequence |
| `UOKAnimationMeta` | AnimSequence 的屬性 | 存 OK 專用參數（正規化、IK 權重、腳鎖地、接觸策略、授權） | 以 AssetUserData 掛在資產上 |

### 10.3 直接使用、不另做的 Unreal 功能

- **Control Rig**：FK／IK 操作與烘焙。
- **Sequencer**：過場、多人同場編輯、錄製（Take Recorder）。
- ~~IK Rig／IK Retargeter~~：實作改用 OK 自己的重定向（見 0.2）；IK Rig 仍可在 Unreal 內把任意動作先重定向到 Mannequin 再轉 OK。
- **Curve Editor**：曲線編輯，嵌在 OK 編輯器內。
- **Live Link**：動捕輸入。
- **FBX 匯入器**：外部動作的入口。
- **交易系統（Undo／Redo）**、**內容瀏覽器**、**資產註冊**。

### 10.4 避免混用的規則

1. **命名**：OK 的類別一律用 `OK` 前綴（`UOK…`、`FAnimNode_OK…`）；Unreal 原生資產不改名、不加前綴。
2. **模組與資料夾**：
   - 程式放在 `OKAnimation`（執行時）和 `OKAnimationEditor`（編輯器）模組。
   - 資產放在 `/Game/OK/`，其中骨架家族放 `Skeleton`、OK 動畫放 `Animations`、重定向設定放 `Retarget`。
   - Unreal 原生動畫維持在原本位置，作為「來源」。
3. **誰是本體**：
   - OK 動畫的本體是 `UOKAnimSequence`，也就是 `.okanim`。
   - 編輯時展開的 `UAnimSequence` 是**暫時**的，存在 `/Game/OK/_Transient` 或記憶體中，不提交、不被遊戲引用。
   - 遊戲只引用 OK 資產。
4. **轉換方向明確**：
   - Unreal 原生 → OK：匯入、重定向，是單向轉換，來源保留不動。
   - OK → Unreal 原生：只在「展開編輯」時暫時產生，或使用者明確選擇「匯出為 Unreal 動畫」時才產生，並標記為衍生物。
5. **骨架**：OK 骨架家族產生的 `USkeleton`，只用在 OK 編輯與 OK 網格播放；不要把其他角色的動畫直接綁上去，一律先走重定向轉成 OK。
