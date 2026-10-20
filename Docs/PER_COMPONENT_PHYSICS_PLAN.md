# 分元件物理計畫（2026-10-18）

目標：
1. 身體與穿戴物拆成「物理元件」，每個元件各自選解算器（Verlet／原作 DynamicBone＋碰撞／PhysX／Kawaii／Chaos…）與重力、柔軟度。
2. 新版 OK 卡可以逐元件指定物理。
3. 原作轉來、沒有指定的卡，依元件類別套用預設表，不同元件用不同物理（例如瀏海用原作 DynamicBone，裙子用 PhysX）。

## 現況（調查結果）

- 鏈沒有類別。可用的訊號：
  - 部件 `role`（`unity.rs` `role_for_category`）：`hair_b/f/s/o`、`clothes_top/bot/bra/shorts/gloves/panst/socks/shoes`、`accessory`。
  - `source_property`（如 `outfit0.accessory20`）。
  - ver02 的 `Chain.kind`（0/1 胸、2/3 臀）。
  - 根粒子旗標：2 body、8 outer、16 skin。
- 分組方式：
  - 頭髮、飾品：每個部件一個 `AttachedDynamicSet`。
  - 衣服：全部併成一個 `clothes_dynamics`。
  - 胸臀：獨立的 `BodyDynamicSet`。
- 解算器選擇是全域的：
  - 外掛 `SolveJobs` 每個 batch 選一個後端（`OKPhysXSecondary.Backend`）。
  - `Enabled 0` 時整個退回 Rust 的 Verlet（`dbone.rs`、`ver02.rs`）。
- 傳到外掛的 ABI（`OkPxChainStep` / `FOKPxParticle`）沒有空欄位。
- OK 卡（`okLb` JSON + `okLc` pack）沒有自己的物理欄位。卡上只有原作的 `noShake`、`bustSoftness`、`bustWeight`。DynamicBoneEditor 外掛資料有跟著帶，但解算器沒讀。

## 設計

### 1. 物理元件類別 `PhysicsComponent`（Rust，`dbone.rs` 新增）

每條鏈在建立時決定一個類別，依序判斷：

| 類別 | 判斷 |
|---|---|
| `bust` / `hip` | ver02 `kind` 0–1 / 2–3 |
| `hair_front` `hair_back` `hair_side` `hair_other` | 部件 role `hair_f/b/s/o` |
| `skirt` | `clothes_bot` |
| `skin_tight` | 旗標 16（貼身：絲襪、緊身褲） |
| `clothes` | 其他 `clothes_*` |
| `tail` | 飾品，且掛點或骨名含 tail／尻尾／しっぽ（名單放在設定檔，可增修） |
| `accessory` | 其他飾品 |

- 類別存在 `DBone`，`describe_chains` 一併印出，方便查。
- 衣服目前併成一組。拆的時候每條鏈保留來源 role，不必拆 set（分派以鏈為單位，見第 3 節）。

### 2. 解算器指定：三層覆寫

最終解算器 = 卡片指定 > 元件預設表 > 全域。

- **全域**：`OKPhysXSecondary.Backend` 新增 `-1 = 依元件`，並設為預設。0–6 仍是「全部強制用這個」，留給比較與量測。
- **元件預設表**：`Content/Config/physics_components.toml`（資料放 Content；Saved 只放可重建的東西）。每個類別寫 `solver`、`weight`、`hardness`，tail 的骨名名單也放這裡。初始值見第 5 節，以量測決定。
- **卡片指定**：見第 4 節。
- 解析在 Rust 做（`ok-rules` 的規則精神：邏輯在 Rust，UE 只呈現）。外掛只收到每條鏈已決定的解算器。

### 3. 每條鏈分派

- **ABI**：用根粒子 `flags` 的第 8–11 位元放「解算器 + 1」（0 = 沿用全域），不改結構大小。`px.rs` 與 `OKPhysXSecondaryAbi.h` 同步加常數 `FLAG_SOLVER_SHIFT = 8`。
- **外掛 `SolveJobs`**：
  1. 依每個 job 的解算器分組。
  2. 每組用自己的 `bPhysX` / `bProjectOnly` 做 `PrepareJob`，各呼叫一次 step。
  3. Chaos、Jolt、MuJoCo 的快取 key 用 `BatchKey` 混入解算器編號。
  4. 重置與 `GLastSolver` 改成每組各記一份。
  5. PhysX 組沿用該 batch 的場景。
- **Rust Verlet 也可以逐鏈**：解算器是 Verlet 的鏈不放進 `px_batch`，留在 Rust 的 `update_particles2`。同一 set 內混用時，Verlet 鏈先更新，外掛鏈後更新（兩者互不碰撞，與現在一致）。
- **碰撞不變**：身體代理、對方身體、卡片碰撞體對所有解算器一樣。Verlet 鏈沒有對方碰撞，這是原作行為。表上要標清楚，量測時另列。

### 4. OK 卡的物理段

- 新 PNG chunk `okPh`：JSON，帶 `format` 與 `version`。獨立於 `okLb`，改物理不必重算 library digest。
- 範例：

```json
{"format":"ok-physics","version":1,
 "parts":{
  "hair.parts1":{"solver":"dynbone","weight":4,"hardness":6},
  "outfit0.clothes.bot":{"solver":"physx"},
  "outfit0.accessory20":{"solver":"physx","chains":{"cf_J_Tail_00":{"solver":"verlet"}}}
 }}
```

- 鍵用現有的 `source_property`，衣服用 `outfitN.clothes.<slot>`。`chains` 可再以根骨名逐鏈覆寫。
- 解算器用名稱，不用數字：`verlet`、`dynbone`、`physx`、`kawaii`、`chaos`、`jolt`、`mujoco`。不認得或 Shipping 沒有的名稱退回元件預設，並記 log。
- 讀寫：`oklib.rs` 新增 `read_physics` / `write_physics`。
  - `strip_ok_chunks` 與 `remake_ok_card` 要保留 `okPh`，重轉卡不能洗掉使用者設定。
  - `.okclothes` 帶一份自己衣物的物理段，換裝時跟著走。
  - 改格式要升 `SESSION_CACHE_REVISION`，避免讀到舊快取。
- 原作卡的 `noShake` 仍優先（不動），`bustSoftness` / `bustWeight` 照舊作用在 bust/hip。
- 之後可選：把 DynamicBoneEditor 外掛資料轉成 `okPh` 的參數覆寫（第二階段，不在第一版）。

### 5. 原作卡的預設（2026-10-18 使用者定案）

| 類別 | 預設 |
|---|---|
| hair_front / back / side / other | dynbone |
| bust / hip | dynbone |
| 其他（skirt、skin_tight、clothes、tail、accessory） | physx |

量測只用來確認沒有變差（第 6 階段），不再用來決定預設。

## 介面

- 一對一場景的物理選單新增第一項「依元件（卡片／預設）」，並設為預設。其餘選項仍是全部強制，用來比較。
- 角色編輯器各部件（頭髮、衣服、飾品）的屬性面板加「物理」下拉＋重力／柔軟度。
  - 未設定時顯示「預設（xxx）」。
  - 存檔寫進 `okPh`。

## 分階段

1. **類別**：`PhysicsComponent` 判斷、`describe_chains` 輸出、單元測試（各 role、ver02 kind、tail 名單）。
2. **分派**：ABI 旗標位元、外掛按組分派、Rust Verlet 逐鏈、`Backend -1`。用「全部同一解算器」對照，確認結果與現在相同。
3. **預設表**：`physics_components.toml` 載入與熱重載；按類別量測的工具（Lua）；量測後定案預設。
4. **卡片**：`okPh` 讀寫、保留、`.okclothes`；Rust 測試涵蓋解析、覆寫順序、未知名稱退回、重轉保留。
5. **介面**：一對一選單「依元件」與編輯器各部件物理設定。
6. **驗收**：C050（瀏海、龍尾巴）＋另外兩張卡（長裙、絲襪），一對一全姿勢量測，與目前預設（全部 dynbone）比較不得變差。

## 風險

- 同一飾品的多個網格部件各自模擬同一條鏈（見 PHYSX_ERRORS「瀏海打架」）。解算器必須按部件鍵決定，不可按網格決定，否則同一束頭髮會分家。
- 不同解算器的鏈之間沒有互相碰撞（現在也沒有）。例如裙子與長髮用不同解算器時，若需要互碰，要另外規劃。
- Shipping 沒有 Jolt、MuJoCo。卡片指定它們時退回元件預設，不能失敗。

## 實作狀態（2026-10-18，第一版）

已完成：
- `OKPhysXSecondary.Backend` 預設 -1（依元件）。0–6 仍可強制全部用同一個；一對一選單第一項是「依元件（卡片／預設）」。
- 元件類型：`px::PhysicsComponent`，共四類：
  - Hair：`hair_*`。
  - Body：ver02 胸臀。
  - Clothes：`clothes_*`。
  - Accessory：其他。
  - 尾巴、裙子、貼身衣物的預設都是 PhysX，所以第一版不再細分。
- 解算器放在根粒子旗標第 8–11 位元（`FLAG_SOLVER_SHIFT`，值為解算器＋1）。外掛 `SolveJobs` 依此分組，每組呼叫一次 `SolveJobsWith`。
- OK 卡 `okPh`（`okphysics.rs`）：
  - 鍵：`hair.back|front|side|other`、飾品的卡片屬性（`outfit0.accessory20`）、`clothes`、`body`。
  - 卡片開啟時讀取，不進 session 快取。
  - `strip_ok_chunks`、`library_card` 都會保留它，所以重轉卡不會洗掉。
- 命令列：`ok-extract . --okphysics <card> [key=solver ...]`（`key=` 清除該設定）。
- 稽核：`bySolver=解算器:步數/pops/jitter`，前綴 `b` 是胸臀。
- 角色編輯器「顏色／飾品詳細編輯」（2026-10-18）：
  - 列表最前面是「物理／…」下拉（`kind: "choice"`，鍵 `physics.<okPh 鍵>`）。
  - 列出四個頭髮部位、身體、衣物（全部），以及本套服裝有放東西的飾品欄（含額外飾品欄 20 以後，判斷依據是有綁定骨架）。
  - 第一個選項是「預設（目前預設的解算器）」，選它會移除該鍵。
  - 按「套用並更新角色」寫進卡片的 `okPh`。
  - 程式在 `appearance/properties/physics.rs`。舊卡（非 OK 卡）重建預覽 PNG 時，`export_edited` 會把 `okPh` 補回。
  - GUI 驗收：`run_editor_acceptance.ps1 -Mode Properties`，項目為 `physics_solver_uses_choice_with_default` 與 `physics_choice_saved_in_card`。
- 飾品逐鏈分類（2026-10-18，`PhysicsComponent::of_accessory_chain`、`AttachedDynamicSet::classify_chains`）：
  - 依據是 102 張卡的普查（Demo＋CrowdAPNG）。
  - 戴在頭上的算頭髮：掛點是 `a_n_head*`、`a_n_hair*`、`a_n_earrings*`、`cf_J_FaceUp*` 等，包含假髮、雙馬尾、耳朵、耳環。
    - 原作卡常把整頭頭髮做成飾品（C050 `Yvonne_hair`、C000 假髮）。之前這些會落到 PhysX，瀏海又會被推開。
  - 不在頭上，而且骨頭或網格名稱含 tail／sippo／shippo／尻尾／しっぽ／尾巴／weiba／尾：算尾巴。
    - 頭上叫 tail 的是髮束。twintail、ponytail、hair 一律不算尾巴。
  - 其他算一般飾品，例如緞帶、項鍊、掛繩。
  - 同一個飾品可以同時有頭髮鏈和尾巴鏈。
  - 每條鏈各自決定解算器：卡片指定的部件優先，否則用該類型的預設。頭髮用 DynamicBone，尾巴和其他用 PhysX。
- 尾巴彈性（`OKPhysXSecondary.TailBounce`，預設 1）：
  - 一對一場景的物理區有「尾巴彈性」滑桿，範圍 0–2。
  - ok-extract 在尾巴鏈的根粒子設旗標 32。外掛在任何解算器讀取之前調整尾巴粒子：拉回原形的彈性 ×(1+1.5×值)，阻尼 ×(1−0.5×值)，擺動範圍不動。
  - 原作尾巴很鬆（C050：彈性 0.09、剛性 0.10），會被臀部拖著甩，甚至翻到頭上。加強拉回、減少阻尼後，尾巴會保持自己的形狀，甩出去再彈回來。
  - 0 等於卡片原設定。
  - 對所有解算器都有效：PhysX 把阻尼當線性阻尼、彈性當彈簧。
  - 稽核新增 `tail:步數/pops/jitter swing=平均偏離 max=最大偏離`。
- 卡片鍵：session 記的是完整屬性（`outfit0.accessory1.ChaFileAccessory.PartsInfo.id`），`part_key` 會正規化成 `outfit0.accessory1`。之前卡片對飾品的指定其實沒有生效。

尚未做：
- 逐部件的重力、柔軟度（仍是全域 CVar）。
- 衣服逐件指定（目前 `clothes` 一個鍵管全部）。
- Rust Verlet 逐鏈（Verlet 仍只能全域 `Enabled 0`）。
- `.okclothes` 帶物理段。
