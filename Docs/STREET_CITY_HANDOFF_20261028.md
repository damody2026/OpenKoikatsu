# 街道城市（StreetSocialCity）三腳架裝飾：完整交接（2026-10-28）

這份文件寫給沒看過之前對話的新 session，讀完就能接手。**使用者指示：這份文件和所有街道城市的檔案一律不 commit，只留在本機。**

---

## 1. 範圍與邊界

| 項目 | 位置 |
|---|---|
| UE 專案 | `E:\StreetSocialCityProduction`（使用者另一個專案；地圖 `UniversityCity`） |
| 引擎 | E 引擎 `E:\PhysX\UE5.8` |
| C 端看到的地圖 | `C:\portable\OpenKoikatsu\StreetSocialCity\Content\UniversityCity\Maps\UniversityCity.umap`（腳本用它算 SHA；實際指向 E 槽的專案） |
| 工具 | `C:\portable\OpenKoikatsu\Tools\*street_city*`（Lua；embedded Python 給 UE 和 Blender 用） |
| 產物 | `C:\portable\OpenKoikatsu\output\street-social-city-production\` |
| 打包版 | `E:\StreetSocialCityProduction\motel-packages\<時間>`（cook 中間檔放 E 槽，成功後自動刪除） |
| 進度文件 | `Docs/STREET_CITY_IMMERSIVE_LIFE_PLAN_20261024.md`（第 10 節）、`Docs/STREET_CITY_MOTEL_ROOM_FIDELITY_PLAN_20261025.md`、`Docs/STREET_CITY_MOTEL_ROOM_SPEC_20261025.md`、`Docs/STREET_CITY_SHOP_STREET_SPEC_20261025.md`、錯誤紀錄 `Docs/STREET_ERRORS.md` |

不要動的東西：
- OpenKoikatsu 主專案的遊戲程式（`Source/`、`Plugins/`、`Tools/ok-*`）。
- `C:\portable\OpenKoikatsu\StreetSocialCity\`（AGENTS.md 說的是 C 端那份，不進版控）。
- 別的 session 的遊戲和 PID。只關自己開的程序。
- `E:\OpenKoikatsu`（E 組的 checkout）。

---

## 2. 使用者的決定與上限

- **一律不 commit、不 `git add`**，包括這份文件。
- **Tripo：**
  - 旅館這輪上限 100 件：10-27 上午 50 件，下午使用者追加「還可以再生成50個」，**已經用完**。
  - 商店街另有上限 100 件，還沒用。
  - 每件最多 30 點，用使用者的設定：H3.1、2K、PBR 開、Remove Lighting 開、Triangle 270K、**Ultra Mesh 關**。Ultra Mesh 開著會變 40 點；同一個帳號其他人可能把設定改掉，送出前要看按鈕上顯示 30。
  - 重新生成或補件都要使用者本人在這個對話裡同意；別的 session 轉述的不算。
  - **10-28 23:4x 使用者本人的新決定：**
    - 「你可以把剩下的tripo點數全部用完，用於增加整個世界的視覺完整性」，原本 100 件的上限取消；
    - 「哪個面數低就用哪個」：每件比較 Blender 整理和 remesh 後的面數，用比較低的；
    - 「你先盤點完整缺口 我明天再幫你接claude in chrome」。
    - 盤點已完成；**一件都還沒送出**，要等使用者接上 Claude in Chrome，並先確認點數餘額。詳見 §9。
- **Tripo 下載：** Claude 控制的 Chrome 分頁發出的下載一律不會落地，也不會出現在 Chrome 的下載記錄。要請使用者在自己的分頁做 Batch Export（每次最多 10 件，GLB、2k）。
- **remesh 規則（10-27 使用者決定）：**
  - 每個模型比較 remesh 前（整理後）和 remesh 後的面數，**面數沒有變少就用沒 remesh 的模型**，舊的批次也一樣。
  - 某一類模型有 9 成以上 remesh 後變多，就不再對這類跑 remesh。
  - 家具 v2 有 94%、v3 有 100% 變多，所以**家具不再 remesh**。v1 小擺件只有 18%，照常 remesh。
- **記憶體：** 單一程序超過 60 GB 就停；整合期間壓在 40 GB。Editor 用 d3d12 開這張地圖要約 70 GB，所以實拍一律用打包版。
- **E 組量 FPS 或錄 demo 時：** 立刻停掉自己所有的 Unreal、cook、UAT、Blender、remesh，回覆對方，等「測試結束」或「錄影結束」。E 引擎上跑 cook／UAT 前，先確認沒有別人在跑。
- **24 小時無人值守：** 自己判斷，要做通用解，錯誤寫進 `Docs/STREET_ERRORS.md`（在最新版本的檔尾追加，不要從舊副本覆寫）。卡住時每 10 分鐘檢查一次，不要前景 sleep。

---

## 3. 架構

```
規格 JSON / 掃描結果 ──► 建模器（Blender，Lua 包 embedded Python）──► glb + build.json
                                                                     │
原生工作程序（UE Editor nullrhi，Tools/street_city_motel_native.lua serve）
  ◄── job（Tools/street_city_motel_job.lua <mode> <payload.lua>）：掃描、安裝、匯入、audit、routes、physics_plan
                                                                     │
驗收 Tools/run_street_city_motel_acceptance.lua：
  [裝房間] → audit → routes → physics_plan → quit → cook（repackage）→ [打包版實拍] → 鏡面遷移 → 快速物理 → 完整實走
```

- **原生工作程序：** `Tools/lua/lua.exe Tools/street_city_motel_native.lua serve`。等到 `motel-v1/serve-ready.json` 的 status 是 `passed` 才算就緒。job 名稱 `x` 會呼叫 payload 裡的 `motel_x()`。
- **主要 payload：**
  - `street_city_motel_sample_payload.lua`：`rooms_install`（裝旅館房間）。
  - `street_city_motel_audit_payload.lua`、`street_city_motel_routes_payload.lua`：audit、routes、physics_plan。
  - `street_city_shop_survey_payload.lua`：`office_scan`（書桌，清單在 `shop-survey-v1/office-want.json`）、`city_probe`、`city_room_views`。
  - `street_city_dorm_decor_payload.lua`：`dorm_decor_install`、`office_decor_install`、`classroom_decor_install`、`office_fit_scan`、`office_fit_install`。
  - `street_city_motel_pool_ramp_payload.lua`：`pool_ramps`（泳池隱形斜坡）、`ramp_probe`（射線和膠囊掃掠診斷）。
  - `street_city_motel_furniture_payload.lua`：`furniture_import`（只匯入 Editor）。
  - `street_city_motel_decor_payload.lua`：`decor_import`（v1 小擺件）。
- **旅館房間：**
  - 規格在 `motel-v1/room-specs/batch-NN.json`（57 間）。
  - 建模器 `build_street_city_motel_rooms_v2.lua <room id...>`，log 在 `motel-v1/sample-rooms/build-v2.log`；看 `FAILED_ROUTE []`（必須是空的）和 `SKIP`。
  - Tripo 家具登記表 `motel-v1/sample-rooms/furniture/receipts.json`；規格裡座椅、吊燈、桌子寫 `key`，或在 extras 放 `{type:'furniture',key}`。
- **Tripo 家具流程**（`OK_TRIPO_SET=motel-furniture-tripo-v2|v3`）：
  1. `collect_street_city_motel_furniture_tripo.lua`（設 `OK_TRIPO_SINCE=<unix 時間>`）：依專案名稱從 Downloads 收集。同名的用 `model-ids.json` 的 `duplicate_sha256` 分辨；重複匯出的用底色貼圖加頂點數判定為同一個模型。
  2. `prepare_..._tripo.lua [k/n]`：依 plan 尺寸縮放，≤35k 面，貼圖 ≤2K。
  3. remesh（家具已不再跑）：`remesh_street_city_motel_tripo_decor.lua`，用 `OK_TRIPO_SET`、`OK_REMESH_RUNGS` 選要試的參數組。
  4. `register_..._tripo.lua`：定向（椅背規則、細節較多的一面朝前，搭配 `overrides.json` 的 `rz`、`rx`、`ry`、`scale`、`category`），逐件選面數較少的模型，渲染 `oriented/sheet.png` 人工確認，登記進 receipts.json。
  5. `apply_..._tripo.lua`：依房號寫進規格。第一次執行會備份成 `batch-NN.before-furniture-v2.json`；要重套時先從備份還原，再依序套 v2、v3。
  6. 重建房間 → `furniture_import` → 驗收 `--rooms`。
- **整層擺設：**
  - 辦公樓和住宅：`office_fit_scan` → `build_street_city_office_fit.lua`（`OK_FIT_ONLY=<前綴>` 只重做部分樓層）→ `office_fit_install`。
  - 書桌小物：`office_scan` → `build_street_city_office_decor.lua` → `office_decor_install`。

---

## 4. 怎麼執行與判斷通過

| 步驟 | 指令（在 `C:\portable\OpenKoikatsu`） | 判斷 |
|---|---|---|
| 完整驗收 | `Tools\lua\lua.exe Tools\run_street_city_motel_acceptance.lua [--reuse] [--from-cook] [--rooms id...] [--capture v2 id...]` | log 最後是 `ACCEPTANCE DONE` |
| 物理結果 | `output/.../motel-v1/physics-latest.json` | `status=passed`、`unique_completed_routes=79/79`、`walking_step_assists=0` |
| 打包版實拍（旅館） | 驗收的 `--capture v2 <ids>` → `motel-v1/runtime-views-<時間>/` | 人工看圖 |
| 打包版實拍（樓層） | `$env:OK_SHOP_CAPTURE_PACKAGE='1'; Tools\lua\lua.exe Tools\capture_street_city_shop_survey.lua --views <views.json>`，再用 `Tools\lua\lua.exe Tools\sheet_street_city_shop_survey.lua <資料夾> floors` | `captures.json` 的 missing 為 0 |

- 驗收選項：
  - `--reuse`：沿用已就緒的工作程序。
  - `--from-cook`：地圖 SHA 和上次 physics_plan 相同時，直接從 cook 開始。
- 驗收的保護：
  - 裝房間每 10 間一批；工作程序超過 40 GB 會自動 quit 再重開。
  - 有別人在跑 UAT 時會每 10 分鐘重新檢查。
- 樓層視點：從 `shop-survey-v1/city-room-views.json` 依 id 前綴篩出，每層 6 個視點。
- 遊戲執行檔、pak、地圖的 hash 在實走前後都會比對（`*-launch.json`）。

---

## 5. 已完成並驗收

| 項目 | 驗收時的 package／地圖 SHA | 實走 |
|---|---|---|
| 商店街 96 間改造、94 間入口 | 20261026-073046／2f9ca1764f7c；入口實走 20261026-083621 | 79/79；入口 94/94、0 輔助 |
| 旅館 57 間量產房（燈帶、招牌件、浴缸、自動補家具） | 20261026-172556／1c42021a88b5 | 79/79 |
| 宿舍 D1～D4 擺設、行政樓 AD 書桌、泳池斜坡第一版 | 20261027-061633／7097d7deef00 | 79/79 |
| 教室掛件（10 棟 50 層） | 20261027-103959／04672d07a723 | 79/79、125 筆 |
| 圖書館 LI 書桌閱讀用品（160 張） | 20261027-121154／a29ca48f55b9 | 79/79、126 筆 |
| 旅館 Tripo 家具 100 件、辦公樓 City_O01～O03 18 層、書桌放寬（411 張） | 20261027-193538／f02a2d242180 | 79/79、124 筆 |
| 302 改用 sample `purple_chesterfield`（v3 第 10 件 Tripo 做成兩張沙發，plan 標了 `replaced_by`） | 20261027-210950／56796e48bf14 | 79/79、124 筆 |

- 實拍總覽：
  - 旅館 57 間：`motel-v1/runtime-views-20261027-194430/sheet-living-0..1.jpg`。
  - 辦公樓：`shop-survey-v1/captures-20261027-204844/floors-01..04.jpg`。
  - 教室：`captures-20261027-113438`。
  - 圖書館：`captures-20261027-125422`。
- Editor 資產：`/Game/UniversityCity/MotelFurnitureTripoV2`、`V3`、`MotelDecorTripoV1`（122 件，其中 22 件改用整理後的模型）。

---

## 6. 已經裝進地圖、還沒驗收（目前地圖 673366e28df2…）

- **住宅 City_R1、R2 共 10 層：** office-fit 的住宅模式。房間往牆邊多算 0.4 m，門口淨空 0.9 m。每層約 15 間臥室、10 組客廳、3 組餐桌、78 盆植物，只放在空著的地方。
- **City_CV（5 層）、TR（2 層）書桌小物：** 共 188 張桌子。
- **泳池斜坡上端延伸到平台高度：** 8 道斜坡的上端都在 1847 cm，膠囊掃掠都暢通。
- 上一次完整實走（地圖 bd54e1a680b6，修正前）在 201 第 24 段卡住，原因見第 8 節。

**下一步：**
1. 確認 E 引擎上沒有別人的 Editor 或 UAT，E 組也沒在測，再執行 `Tools\lua\lua.exe Tools\run_street_city_motel_acceptance.lua`（完整流程）。目標：79/79、0 次輔助跨步。
2. 通過後，用 `city-room-views.json` 篩出 `City_R*`、`City_CV*`、`City_TR*` 的視點，用打包版實拍，再拼總覽檢查。
3. 結果寫進 `STREET_CITY_IMMERSIVE_LIFE_PLAN_20261024.md` 第 10 節（目前寫到 10.8）。

---

## 7. 還沒做的工作

- 住宅擺設仍然偏少：公寓大多是約 3.5 m 的小房間，餐桌常因為門口淨空放不下。可以再針對公寓格局（廚房、浴室）細分。
- Campus_SP 體育館：房間大多空著，只有書架和洗手台。
- 旅館（來自規格與 fidelity 文件）：
  - wishlist 中還沒有對應模型的項目，例如愛心卡座、床幔、牆飾、鞦韆床這類，目前是程序件。
  - Tripo 上限已經用完，要再生成需要使用者同意。
- 商店街：Tripo 上限 100 件還沒用（`STREET_CITY_SHOP_STREET_FIX_PLAN_20261025.md` 有 60～80 件的候選）。用之前要依規則逐件確認。
- 其他館舍的辦公室：可以把樓層加進 `office-want.json`，沿用 `office_scan`。

---

## 8. 踩過的坑（詳細在 `Docs/STREET_ERRORS.md`）

1. **hash 崩潰被誤判成失敗：** 完整實走已經通過，但事後 hash 時 ok-luakit 回傳 0xC0000142，整支腳本記成失敗。現在 `run_street_city_motel_physics.lua` 的 hash 會重試。失敗時先看原生回報（`physics-<時間>.json`），不要一律重跑。
2. **驗收開頭的輸入檢查偶爾失敗：** W 前進或 Shift 加速沒過。驗收腳本只會針對這種失敗自動重跑一次，原樣重跑就會過。
3. **cook 中間檔：** 放在 E 槽，成功後自動刪除。D 槽空間很少。被中斷的 cook 會留下空的 `motel-packages/<時間>`，確認沒有 junction 後可以刪。
4. **記憶體爆量：** 同一個工作程序做匯入加裝房間，漲到 59.6 GB。現在每批之後超過 40 GB 會自動重開。重開時的 log 檔名要每次不同，否則會撞上舊程序還開著的檔（已修）。
5. **第 27 件的專案不見：** Tripo 顯示「Project not found」，點數自動退回。依使用者指示重新生成（e9528749），`model-ids.json` 有記錄。
6. **Tripo 下載：** Claude 控制的分頁下載不會落地；本機接收端會被 CSP 擋下，這屬於安全機制，不要繞過。批次匯出的檔名用專案名稱，檔案裡沒有 ID。重新匯出時，同一個模型每次的位元組都不同。
7. **PowerShell 5.1 時間戳：** `Get-Date -UFormat %s` 會多 8 小時，要用 `[DateTimeOffset]::new(<時間>).ToUnixTimeSeconds()`。
8. **remesh 面數上限：** 小擺件的參數組套到家具上，會因面數超過上限被拒。instant-meshes-rs 在 `-f 42000` 會崩潰（現在會自動降 5%、10% 重試）。工具崩潰的報告不能被快取沿用（已修）。
9. **泳池第 24 段卡住：** 真正原因是斜坡上端比平台低 4.5 cm、又差 8 cm 才到平台邊緣，平台的角露在斜坡上，膠囊會架在角上懸空。已改成斜坡延伸到平台高度。另外：完整實走期間不要跑 Blender 或 remesh，CPU 卡頓會讓它更容易發生。
10. **Tripo 模型的朝向：** 批次匯出的模型會轉 90°。鏡子常是側面朝前，立鏡、燭台會被誤當成掛牆或吊燈，一定要看 `oriented/sheet.png`，再用 `overrides.json` 修正。
11. **家具伸進走道：** 太深的沙發往後退；深度超過 1.1 m 的椅子改用自動擺放。同一間房的第二盞檯燈用 `side_lamp2`。
12. **UE payload：** 用 `exec` 建 `import_one` 時，它會複製當下的 globals，所以要先設好 `PREFIX`、`KEEP`、`save` 再建。

---

## 9. 2026-10-29 進度（c-street-city，08:55 因 C⇔E 整合暫停）

### 9.1 旅館房間驗收

| 輪次 | 內容 | 結果 |
|---|---|---|
| 第 8 輪 | 201、205、207、311 | 通過，地圖 `97b11930c01f…` |
| 第 9 輪 | 57 間全部重建：P6 規格、鏡面改暖灰半金屬、主燈與吊燈錯開 1.1 m | 通過：package `20261029-023245`，地圖 `e85c91c9…`，完整實走 79/79、0 次輔助跨步 |
| 第 10 輪 | P7 規格（11 間）、浴室主燈 2500→1500 lm | 通過：package `20261029-045539`，地圖 `be18c8c8ebbc…`，79/79、0 次輔助跨步，實拍 `motel-v1/runtime-views-20261029-050259/` |
| 第 11 輪 | P7 修訂（11 間）、實拍前等 300 frame | **未完成**：已安裝、打包（package `20261029-072840`）、實拍（`runtime-views-20261029-073431/`）、快速物理（physics-20261029-082751.json）；08:47 開始的完整實走在 08:55 停掉 |

- **slot1 與 `city.lock` 已放掉，queue 沒有登記**。
- 每輪的審查結果與修正寫在 `STREET_CITY_MOTEL_ROOM_SPEC_20261025.md` §12.3、§12.4。
- 下一輪實拍要傳 `OK_MOTEL_CAPTURE_SETTLE=300`：遊戲開著自動曝光，原本切鏡頭後約 100 frame 就拍，浴室特寫會曝白。

### 9.2 下一步

1. 用 scratchpad 的 `review.lua` 看第 11 輪的實拍（`runtime-views-20261029-073431/`），確認 11 間的修正與浴室曝光。
   - 地圖裡已經是第 11 輪的內容，但還沒跑完整實走。
2. 重排一次驗收，至少要跑完完整實走：
   - 先在 `Saved/UnrealSlots/queue/` 登記，照順序拿 slot；
   - 跑 `run_street_city_motel_acceptance.lua --reuse ...`。
3. 還沒修的：
   - 406 拱門做成圓角；
   - 510 改成真正的下沉浴缸（要新元件）；
   - 513 天花吊圈；
   - 312 沙發顏色（那張是 Tripo 模型）；
   - 402、313、315、506 的浴室，要等曝光收斂後再判斷。

### 9.3 全世界視覺缺口與 Tripo

- 盤點：`Docs/STREET_CITY_WORLD_GAP_INVENTORY_20261029.md`。
  - 城內實拍 193 張，室外 Z1–Z10 與室內的缺口；
  - 第一批 108 件，有剩再做 29 件。
- 生成清單：`output/street-social-city-production/world-tripo-v1/plan.json`，共 137 件，每件有 prompt、尺寸、面數上限 `tris`，status 是 `planned_not_submitted`。
- 等使用者接上 Chrome 後的流程：
  1. 看點數餘額；確認設定 H3.1、2K、PBR、Remove Lighting、Triangle、Ultra Mesh 關，按鈕要顯示 30；
  2. 依 batch1 順序送出；
  3. 請使用者在自己的分頁每 10 件做一次 Batch Export；
  4. `OK_TRIPO_SET=world-tripo-v1` 收集 → `prepare_street_city_motel_furniture_tripo.lua`（照每件的 `tris` 上限減面）→ remesh → 取面數低的 → LOD → 安裝；
  5. 驗收後用 `world-gap-v1/gap-views.json` 重拍對照。
- 不需要 Tripo、可以先做的：世界邊緣、岸線、草地材質等非模型問題（盤點文件 §3）。
## 10. 2026-10-29 上午（接手的 c-street-city）

### 10.1 Tripo：82 件已送出，點數用完
- 使用者本人在本 session 說：「現在你可以使用tripo了 把剩下的點數全部用完吧」。
- 10:46～11:01 依 `world-tripo-v1/plan.json` 的順序送出第 1～82 件，點數 2485 → 25（不夠再生一件），設定照 §2。
  - 第 83 件之後的 55 件標成 `not_submitted_credits_exhausted`。
- 記錄：
  - `world-tripo-v1/submitted.tsv`：序號、key、模型 ID、Tripo 專案名稱；第 43～82 件已有名稱，第 1～42 件等生成完成後再從 Assets 頁讀。
  - `plan.json` 每件也寫了 `tripo_model_id`。
- **下一步要使用者做**：在自己的 Chrome 分頁做 Batch Export（每次 10 件，GLB、2k），共 9 次。
- 之後收集：設 `OK_TRIPO_SET=world-tripo-v1`、`OK_TRIPO_SINCE`=10-29 10:46 的 unix 時間。
  - `model-ids.json` 要用 `submitted.tsv` 產生。
  - 同名的件（例如兩件 beach umbrella）依 §3 用 duplicate_sha256 區分。

### 10.2 旅館第 12 輪
- 第 11 輪實拍的審查（scratchpad `review11/`）：
  - 曝光等了 300 frame，浴缸內側仍整片白。
  - 原因是 `M_SpaWater` 不透明度只有 0.38，加上白色材質的反照率 0.92～0.95 截頂，詳見 STREET_ERRORS。
- 修正：

| 範圍 | 修正 |
|---|---|
| 全部房間 | 浴缸水改用獨立槽 `S_TubWater`，安裝時給每間 `MI_PoolWater_<id>_tub`（不透明度 0.72，`bath.water` 可覆寫）；`mat()` 把非發光材質的反照率上限壓到 0.85 |
| 建模器 | 新增 `portal`（圓角平板門框：直柱、圓角塊、楣，三件分開，讓路線檢查的包圍盒不擋門洞）；浴缸新增 `sunken`；吊燈新增 `halo` |
| 406 | 改用 `portal` |
| 510 | 改成 `sunken` |
| 513 | 霓虹改琥珀色、emit 1.5；吊圈改成天花 halo |
| 312 | 天花霓虹 emit 3；沙發改珊瑚紅 |

- 57 間已重建，`FAILED_ROUTE []`；驗收 `acceptance-r12-*.log` 進行中（`--rooms` 57 間、`--capture v2`、`OK_MOTEL_CAPTURE_SETTLE=300`）。

### 10.3 世界邊緣（不需要 Tripo）
- `Tools/street_city_world_edge_payload.lua`：
  - `world_edge_probe`（唯讀）：地面聯集、四邊每 20 m 的地面高度、霧、天光設定 → `world-edge-v1/probe.json`。
  - 探測結果：城市地面是 G01～G16，範圍 0～110000 cm 的正方形，邊緣高度 1000～1198 cm；**地圖裡沒有 ExponentialHeightFog**。
- `Tools/build_street_city_world_edge.lua`：遠景地面 skirt。
  - 北、東、南三側往外 4 km；150 m 內是平地，往外是 35～80 m 的草丘；西側是海，不延伸。
  - 2,796 個三角形，NoCollision、不投影。
- `world_edge_install`：
  - 材質用 `M_GrassWorldTriplanar_v2`；
  - 匯入時 Y 軸被鏡像，用 actor 的 mirror_y 抵消，並核對放好後的邊界；
  - 地圖 46b72161… → 82114d72…。
- 還沒做：
  - 霧（地圖沒有霧，夜景會受影響，等看過實拍再決定）；
  - 草地塊的色差（`MI_Detail_b6a245…` 等多個材質）；
  - 岸線。

### 10.4 第 12 輪結果、第 13 輪、草地與岸線探測（13:3x）
- **第 12 輪**：完整驗收通過（package `20261029-112318`，地圖 `5e44178a…`，79/79 條、0 次輔助跨步）。結果寫在規格文件 §12.5。
- **第 13 輪**：
  - 浴缸水改成 (.01,.07,.09)、不透明度 0.85；
  - 平台浴缸的 LED 環 emit 改成 2.5；
  - 程序貼圖套上 0.85 的反照率上限（雜湊加 `|cap85`，360 張全部重新產生）。
  - 57 間已重建、`FAILED_ROUTE []`；13:32 開始完整驗收（`acceptance-r13-133210.log`）。
- **草地**（`world-edge-v1/grass-probe.json`）：
  - `M_GroundNatural` 底下真正的草坪只有 `MI_Detail_b6a245…`（meadow_grass）；其他實例是碎石、柏油、沙。
  - 色塊來自草坪實例和 `Tripo-ground-campus`／`Tripo-ground-urban` 這兩種 Tripo 烘焙地面並排。後者的貼圖自帶不同的綠，也可能含步道。
  - 要統一，需要先分出 Tripo 地面中哪些槽是純草地。還沒做。
- **岸線**（`world-edge-v1/shore-probe.json`）：
  - 西側海面是 `Activity_ACTSPACE-04-351` 平面，從 x=0 開始，z=1088 cm；緊鄰的陸地（G01～G04，Tripo-ground-urban）在 x=1 m 處只有 z=1000 cm，比水面低 0.88 m。
  - 下一步：把探測擴充成記錄 x=0～40 m 的陸地剖面，再做一條連續的沙坡（沙用 `MI_Detail_c6f565…`），蓋住水面附近的階梯立面。

### 10.5 第 13 輪通過、第 14 輪排隊（15:1x）
- 第 13 輪完整驗收通過（79/79、0 assists），slot1 與 city.lock 已在 15:10 前放掉。
- 浴缸水改成不透明材質、506 地板改米色大理石（規格文件 §12.6）。下一步：拿到名額後，`run_street_city_motel_acceptance.lua --reuse`（全部 57 間重裝，讓 MI_TubWater 生效）＋完整驗收；同一個 worker 先跑 `motel_world_shore_probe`（擴充後含 40 m 陸地剖面）。
- 排隊檔：`Saved/UnrealSlots/queue/c-street-city.txt`。
- Tripo 82 件仍等使用者在自己的 Chrome 做 Batch Export，Downloads 目前沒有新檔。
### 10.6 第 14 輪通過、海岸修正待跑（10-30 07:3x）
- 第 14 輪：57 間全部重裝（含不透明浴缸水 M_MotelTubWaterOpaque、506 米色大理石），audit／routes／cook（包 `E:\StreetSocialCityProduction\motel-packages` 最新一份）、quick physics、full walk 都通過：`physics-20261030-065550.json` status=passed、walking_step_assists=0、126 條路線。`acceptance-r14b.log`。
- 注意：`--reuse` 不帶 `--rooms` 不會安裝房間（STREET_ERRORS 已記）；這輪帶了全部 57 個 id。
- 浴缸水還沒實拍審圖（驗收不含 runtime capture），下次拿名額時用 `--capture` 拍。
- 海洋（`world-edge-v1/ocean-probe.json`）：西邊有兩張 platform_LOD0 海面，`Activity_ACTSPACE-04-351`（有碰撞，頂 z=1100）與 `-345`（無碰撞，頂 z=1500），沙灘 G01～G04 是 z=1000 直到 x=0。
- 已寫 `motel_world_shore_fix`（`Tools/street_city_world_edge_payload.lua`）：備份地圖，把可見的海面頂降到 z=995，隱藏的只記錄；輸出 `world-edge-v1/shore-fix-*.json`。還沒跑；跑完要重跑驗收與實拍。
- slot1、city.lock 已在 07:3x 放掉；queue 已重新登記（最低優先）。
### 10.7 海岸修正與第 15 輪（10-30 08:05～09:13）
- `motel_world_shore_fix` 已跑：`Activity_ACTSPACE-04-351` 頂從 z=1100 降到 995（沙灘 1000 以下 5 cm）；`-345` 在遊戲中隱藏，只記錄。地圖 8eda5059… → 1982c93c…，備份 `world-edge-v1/UniversityCity-before-shore-fix-8eda5059dba7b407.umap`，紀錄 `world-edge-v1/shore-fix-*.json`。
- 第 15 輪完整驗收（`acceptance-r15.log`）：cook、實拍 290 張（`motel-v1/runtime-views-20261030-081543`）、quick physics、full walk 都過：`physics-20261030-084203.json` passed、0 assists、126 路線。
- 浴缸水審圖：58 張 bath 視角都是有水的藍綠水面，不再像空碗；浴室曝光正常。
- 海岸還沒有室外實拍（旅館實拍不含西岸），下次用 world gap 視點重拍確認。
- slot1、city.lock 在 09:13 已放。
- （10-30 11:0x）依「通用解」要求，`motel_world_shore_fix` 改成全圖規則：凡材質含 ocean、且範圍超出城市地面聯集的 StaticMeshActor 都算海面，可見者頂面設為「邊界地面最低高度（probe.json 四邊取樣）− 5 cm」，已符合就不動（寫 shore-fix-noop.json）。不再寫死 actor 名稱。目前四邊最低都是 z=1000，所以結果與 10-30 08:05 那次相同。

- （10-30 11:5x）西岸實拍（`shop-survey-v1/captures-20261030-113903`）顯示 08:05 的海面下降是錯的：z=1000 是海床，城內 G??_Water 水面在 1100，外海降到 995 造成 1 m 水面落差。規則已改成外海＝城內共同水面（1100），待重跑。A03 的方塊階梯應是水域地圖 1 m 格的海床邊緣透過淺水看到的，要修在 `prepare_street_city_flat_campus.lua` 的海床生成（平滑坡）上。

### 10.8 地面色塊分析（10-30 12:0x）
- 海面已依規則回到城內共同水面（`shore-fix` 1982c93c… → 7213ac7f…，外海頂 z=1100）；之後的驗收 `acceptance-r16.log`。
- 地面貼圖：G01～G16 的 Tripo master（每塊一個 `/Game/UniversityCity/Ground/Source_*/Tripo-ground-*`）各自用 `basecolor-2k` 等 TextureObject，來源是 `E:\StreetSocialCityProduction\world-ground-v1\assets\<kind>\`（grass 89,123,34／forest 78,70,49／sand 206,187,163／campus 195,184,169／urban 142,139,131／riverbank 127,124,115，sRGB 平均）；M_GroundNatural 的草坪 MI 用 meadow（86,115,49）。`world-edge-v1/ground-tex-probe.json`、`ground-master-probe.json`。
- 對照 `world-gap-v1/sheets/gap-08.jpg`：草地本身顏色一致；「方塊狀色差」是草地與碎石／鋪面交界沿著 2.5 m 地面網格走成直角階梯（例如 G071）。通用修法要在地面 chunk 生成（world-ground-v1 的 chunk 建置）讓種類交界沿平滑等值線切開或做混合帶，而不是逐塊調色。尚未動手。
### 10.9 第 16 輪實走完成與邊緣重拍（10-30 13:5x）
- 第 16 輪（海面回復後，包 20261030-115259、地圖 7213ac7f）實走分 4 段全部通過：79/79 路線、0 assists（`physics-20261030-125044／131220／132354／133554.json`）。`run_street_city_motel_physics.lua` 新增 `slice <k> <n>`（分段 soak 基數 310 秒）與 pak 雜湊快取。
- 邊緣／岸線重拍 20 張（`shop-survey-v1/captures-20261030-134730`）：
  - 世界邊緣已看不到切邊（G084、G088、G104、G150 地平線是草地）。
  - 城內淺海與外海已同高，交界幾乎看不出來（G016、G020）。
  - 仍有：A01～A04 沙灘外緣是階梯狀（沙與水的種類交界走 2.5 m 格）；A13、G086、G148 草地有大塊矩形色差（深淺不同的草地材質相鄰）。
- 下一步：`world_ground_master_probe` 補記每個 Tripo master 的貼圖來源（判斷 grass／forest 等種類），再訂「草地類 slot 統一外觀」的規則。
- （10-30 14:3x）視點地面探測（`world-edge-v1/view-ground-probe.json`）：G086、G148、A13 的深色矩形是 `Outdoor_annex_ground`（及公園、球場等室外地面）各自的 `M_GroundNatural` 實例，參數和城區草坪 MI 不同。新增規則 `motel_world_grass_unify`：以城區 chunk 上最多 slot 使用的草地 MI 為準，把全圖用同一張草地貼圖的其他 M_GroundNatural 實例的 scalar／vector 參數同步過去（只改資產、記錄舊值可回復）。待跑，跑後要 cook 與重拍。

### 10.10 草地統一（10-30 14:2x～14:4x）
- `motel_world_grass_unify` 已跑（`world-edge-v1/grass-unify-*.json` 記錄舊值）：凡 base color 名含 grass／meadow／lawn、且使用者都不是運動設施（stadium／track／court／field）的 M_GroundNatural 實例，貼圖與參數都改成城區草坪 `MI_Detail_b6a245…`（meadow）。改了 6 個：`Outdoor_annex_ground` 兩個、`Outdoor_active_park`、`Outdoor_reading_park`、`World_PK_0102`、`World_PK_0110`。
- 新包 `motel-packages\20261030-142934`（只 cook，沒實走；只改材質不影響碰撞）。重拍 `shop-survey-v1/captures-20261030-143620`：A13 的大片色帶消失；G086、G148 腳邊仍有一塊深色，是 `Outdoor_stadium` 的場地（運動設施，規則刻意保留）。
- 中遠景有一條偏白的霧帶（G086、G101），屬盤點 §3 第 5 項（光照／霧），尚未處理。
### 10.11 遠景霧試調（10-30 15:0x～15:3x）—— 結論：撤回
- `motel_world_atmosphere`（讀寫 `CityAtmosphere`，目標值在 `world-edge-v1/atmosphere-target.json`）把 Mie 0.003996→0.0016、aerial perspective 距離倍率 1.0→0.6，地圖 7213ac7f → f9f5a30f（備份 `UniversityCity-before-atmosphere-7213ac7f2671…`），包 20261030-150409。
- 前後對照（`captures-20261030-143620` vs `captures-20261030-152220`）：G086、G101 中景那片偏白其實是草地朝太陽方向的反光，不是大氣霧，改大氣幾乎沒差；空拍 A13 反而變淡。所以把目標值改回原值（0.003996／1.0），下次拿名額時套回去並 cook、實走。
- 中景偏白若要處理，應看 M_GroundNatural 草地的粗糙度／高光，而不是大氣。
### 10.12 暫停（10-30 19:0x，協調者要整合 C／E／F）
- 地圖目前 8d055ec6（海面回到 1100、草地統一、大氣已撤回原值），包 `motel-packages\20261030-163106`。
- 這張地圖的實走：分段 0、1、2 已過（各 20/20、0 assists），分段 3（19 條）兩次在 normal input smoke 失敗（Shift／W），都是 3 個 Unreal 同時在跑的時候，同一包的前三段沒問題，判斷是負載造成。紀錄 `motel-v1/walk-8d055ec6-done.txt`。
- 恢復後第一步：在機器較空（最多 1 個其他 Unreal）時跑 `Tools/run_street_city_motel_physics.lua package 0 slice 3 4`；若地圖已被整合改動，就重跑 `run_street_city_motel_acceptance.lua --no-walk`（現在會先做鏡面遷移）再分 4 段實走。
- 之後的工作：沙灘外緣階梯（A01～A04，要改 world-ground chunk 生成的種類交界）、草地朝陽反光（M_GroundNatural 粗糙度）、Tripo 82 件等使用者 Batch Export。
- 10-30 19:4x 恢復：整合後地圖仍是 8d055ec6、包 20261030-163106，不必重 cook；只差實走分段 3/4。使用者正在用 slot1、slot2，等機器較空再跑。

### 10.13 海岸線從源頭修（10-30 19:5x 起，協調者指示）
- 查明：water mask（`world-field.npz`）本身的海岸是平滑曲線；階梯來自地形——flat campus 把水域頂點壓到 1000、陸地 1200，網格 2.5 m，之後 immersion ground sheet 把陸地頂面換成分區多邊形，陸地頂面的邊緣成了階梯。
- 通用解（不逐格補）：
  1. `motel_world_coast_edge_probe`（唯讀）：沿西岸每 1 m 量陸地頂面真正的邊界（穿過水面與小物件）。
  2. `Tools/build_street_city_coast_strip.lua`（Blender，一次一個）：取 ±12 m 內最外側的陸地邊界再平滑（只往海側移，保證蓋住突出的階梯），做一條沙灘帶：陸側 6 m 高出地面 1 cm，海側 6 m 斜坡從 12.01 m 降到 10.60 m，水線（11.00 m）變成平滑曲線；河口處斷開不跨河。
  3. `motel_world_coast_strip_install`：匯入、用 G01～G04 的沙灘 MI、複雜碰撞可走、與邊緣 skirt 同樣以頂點比對方向，存圖前備份。
  4. 之後 `run_street_city_motel_acceptance.lua --no-walk`（含鏡面遷移）＋實走 4 段＋重拍 A01～A04。
- （10-30 20:2x）不等名額先做完離線部分：
  - 海岸邊界改從 flat-campus 頂點離線算（ground sheet 不動高度，所以陸地邊界就是 2.5 m 格上 1200 的最外側點），存 `world-edge-v1/coast-edge-offline.json`；沙灘帶已建好（`coast-strip/build.json`：8,464 三角形、2 段、最大外移 4.0 m，第一次建時河口旁外移 44 m，已加上限）。
  - 海灘道具 `Tools/build_street_city_beach_props.lua`（Blender）：陽傘兩色各 80 面、躺椅 364 面、救生塔 536 面；規則是沿同一條平滑海岸線每 24 m 一組（陽傘＋兩張躺椅），每 260 m 一座救生塔，河口前後 15 m 不放；共 130 個擺放點（`beach-props/placements.json`）。安裝 job `motel_world_beach_props_install`：落到實際地面、NoCollision、可重跑取代。
  - 說明：water mask 精度不是原因（mask 本身平滑），重新產生 FlatCampus 網格也不會改變 ground sheet 的頂面，所以沒有重跑 flat campus。
### 10.14 海岸安裝完成、公園小物件（Blender，10-30～10-31）

- 沙灘帶與海灘道具已裝進地圖：探測時實測和離線邊界平均差 0.53 m、最大 4.5 m，在可接受範圍內。地圖現為 b2c3f528…，打包 `20261030-215917`（`--from-cook --no-walk`，鏡面遷移已跑）。分段實走和 A01–A04、edge-recheck 重拍還沒做，由等待腳本在只有自己排隊、有一個 slot 空著時分段跑（每段之間有人排隊就讓出 slot）。
- 新增 `Tools/build_street_city_park_props.lua`（Blender）：把 Tripo 計畫裡點數用完沒送出的 11 件戶外小物件，依 `world-tripo-v1/plan.json` 的 size_m／tris 用低面數做出來，輸出到 `world-blender-v1/park-props/<key>.glb` 和 `build.json`。
  - 11 件：picnic_table、campus_map、signpost、notice_board、drinking_fountain、lifebuoy_post、beach_shower、aframe_sign、banner_pole、track_hurdle、bike_shelter。
  - 檢查規則：每軸尺寸都在計畫的 ±15% 內、模型底部貼齊地面、面數不超過預算。11 件全數通過（72～784 面），預覽圖 `park-props/contact.png`。
  - 還沒安裝：擺放規則（依區域：Z1 沙灘、Z2／Z5 草坪與步道旁、Z3 商店街、Z8 跑道）要讀地圖的路網和地面高度，所以要另寫一個原生 install job，做法比照 `motel_world_beach_props_install`。- 10-31 00:25 新地圖的實走分段 0/4 通過（`physics-20261031-002508.json`），之後因為 F 排隊，就讓出 slot。
- 公園小物件的安裝流程已經寫好，但還沒跑：
  - `motel_world_anchor_probe`（唯讀）：讀出地圖上已裝好的長椅、燈、車架、回收站、導盲磚、路緣石、鋪面每個 instance 的世界座標，寫進 `world-blender-v1/anchors.json`。
  - `Tools/place_street_city_park_props.lua`（離線）：依各類規則擺放（見檔頭），選點順序取彼此最遠的點，讓物件分散。用假資料試過，164 件。
  - `motel_world_park_props_install`：每個網格的軸向用 probe 頂點對準；地面缺失、是水面、高低差超過 15 cm，或被其他 actor 擋到的點就跳過。只是外觀物件（NoCollision）。
- 因為裝公園小物件會改地圖，所以把它和實走排成同一條：探測 → 擺放 → 安裝 → cook，再分 4 段實走、重拍。實走的進度從 0 重新算。
### 10.15 地標批次（2026-10-31，Blender，尚未裝進地圖）

- `Tools/build_street_city_landmarks.lua`：報攤、咖啡座 a/b、廣場噴水池、涼亭、遊樂設施 a（滑梯塔）/b（盪鞦韆）、海堤、燈塔、鐘塔、舞台，共 11 件，依 world-tripo-v1/plan.json 的 size_m／tris 製作，全數通過尺寸檢查（84～600 三角面）。輸出 `output/street-social-city-production/world-blender-v1/landmarks/`（build.json、contact.png）。
- 待做：擺放規則（地標要依 zone 與空地找位置，不像公園小物能靠街道錨點）與 native install job；海堤要沿海岸線連續排列。

- 2026-10-31 10:36 重拍（`shop-survey-v1/captures-20261031-103656`，20/20）：沙灘帶、步道、公園小物都在。A03 河口仍有問題：沙灘到河口是直角切斷，河水與海水交界出現深藍方格（兩片水面或地面網格露出），下一步要查河口的水面／地面 actor 並補過渡。

### 10.16 河口、地標擺放、公園小物找空位（2026-10-31）

- 重拍 20 張逐張檢查結果在協調者筆記 `c-street-city-recheck-20261031.md`（地面網格接縫、沙灘階梯、河岸、世界邊緣空地、道路斷頭）。
- 河口：舊沙灘帶（`build_street_city_coast_strip.lua`）是逐列沿 x 的一維剖面，遇河口轉向就切斷。新增唯讀探測 `motel_world_shore_grid_probe`（x0−30 m～x0+300 m、全 y，1 m 網格，記地面頂 z 與是否在水面下，輸出 `world-edge-v1/shore-grid.json`），下一步依這張網格在 Blender 以等高線生成沿水岸的沙灘帶與淺海床。
- 公園小物安裝：重疊或不平時，在物件自身座標 3 m 內依距離找最近空位（每 75 cm 一格），已放的同批物件也算障礙；單車棚允許蓋在車架與單車上。紀錄多 `moved_cm`。
- 地標：`Tools/place_street_city_landmarks.lua`（區域 5 m 網格取樣，鋪面／草坪／海岸三類地面規則，最遠點排序，含備用位置；`limits` 限制每種實裝數），安裝 job `motel_world_landmarks_install`（與公園小物共用 `_blender_props_install`）。錨點與步道查詢抽成 `Tools/street_city_anchors.lua`。地標目前 NoCollision（只做外觀），之後要加碰撞時需一起重跑實走。海堤待海岸網格。
- 安裝結果（10-31）：公園小物 177/179、地標 31 件；沙灘帶 v2（`Tools/build_street_city_shore_strip.lua`，依 shore-grid 等高線，河口不再直角切斷）與沙色水底。

### 10.17 外海深淺方格（2026-11-01）

- 沙片從高出地面 1 cm 改成 15 cm 之後，方格完全沒變，可見不是 z-fighting。新增唯讀探測 `world_seabed_layers_probe`（A03 外海 x 80～135 m、y 700～830 m，逐層記錄 actor、高度、材質，輸出 `world-edge-v1/seabed-layers.json`）。結果：沙片沿 x 合併成長條時取整條最高格的高度，碰到近岸台階格（約 1040 cm）的列整列被抬到 1055 cm，其他列在 1015 cm，兩種水深形成深淺方格。改成同高度的格子才合併、各用自己的高度後重建並安裝。
- 驗收：4 段實走全通過（physics-20261101-034313、035531、041452 與第 0 段）；重拍 `shop-survey-v1/captures-20261101-042558`（20/20）中，A02／A03／A04 外海已均勻，沒有方格。
- 剩下：A03 河口右側的水面邊緣仍有小段階梯狀的水面輪廓（河道水面網格本身的邊），之後和河口海堤一起處理。
- 河口（11-01 下午）：
  - 海床改成一整片連續高度面（離沙灘陸地越遠越深，最深到 1015 cm；2 m 格，約 8.2 萬三角面），取代淺灘斜坡和逐格沙片。
  - 河道兩岸（對岸 80 m 內）不鋪沙灘，改成連續灰石海堤 `WorldEdge_Seawall`（`seawall.glb`，1602 三角面，有碰撞）。檢查圖 `world-edge-v1/coast-strip/seabed-review.png`。
  - 河口仍有的階梯水色，原因是 G??_Water 網格帶著從 z 0 角點拉出的斜面扇。新增 `Tools/build_street_city_water_plane.lua`，以 `world_water_plane_install` 裝成一整片 z 1100 的 `WorldEdge_Water`（820 三角面），原 G??_Water 隱藏。詳見 `Docs/STREET_ERRORS.md`。
  - 驗收：4 段實走全通過，重拍 `shop-survey-v1/captures-20261101-102245`，A03 河口和外海連成同一種水色，海堤清楚，G113 內河正常。
  - 新的唯讀探測：`world_seabed_layers_probe`（區域可由 `seabed-layers-region.json` 指定）、`world_point_actors_probe`、`world_water_owner_probe`（逐點列出覆蓋的水面三角形與 z）。
  - 後續（11-01 中午，已完成）：
    - **內陸水域**：新增唯讀探測 `world_water_bed_probe`，依 `water-plane/mask.json` 量全城 200,415 個水格底下的地面。沙片建置在 x ≥ 299 m 鋪岸坡剖面的海床（10,940 個 2 m 方塊），A13 內陸湖的綠色方塊已消失。
    - **燈塔旁白色方塊**：是 G04 原地面在沙灘線外側殘留的小塊陸地（1110～1198 cm）。改成通則：沙灘線外側高出海床的殘留地面，一律用沙丘蓋住（原高 +15 cm，每公尺降 25 cm）。
    - **沒用到的水面複本**：`world_water_copy_cleanup` 刪掉了 G01、G02 兩個沒人引用的 `_Top` 複本。
    - **驗收**：4 段實走全部通過，重拍見 `shop-survey-v1/captures-20261101-114953`。
  - 內陸護岸（11-01 下午）：
    - `world_bank_probe` 量出內陸岸邊幾乎都是約 1 m 的垂直落差（37,951 個岸帶格裡只有 102 格是斜坡），G099 岸上的草皮直接切到水面。
    - 沙片建置沿水面遮罩（模糊 1.5 格後取 0.5 等值線）在 x ≥ 299 m 鋪連續灰石護岸，剖面朝水內，底 1070 cm、頂 1210 cm、寬 0.6 m；併入 `WorldEdge_Seawall`（共 12,318 三角面，有碰撞），內陸 1,789 個轉折點。
    - 已安裝並 cook 通過（ACC 0）。實走第 0–2 段已通過（`motel-v1/physics-20261101-193849`、`-200049`、`-203016`，`walk-coast-next.txt`=3）；第 3 段與重拍尚未完成（11-01 21:00 整合暫停），恢復後從第 3 段接著跑再重拍 `edge-recheck-views.json`，看 G099／G113／A04／A17 岸邊。
- 背景鏈（scratchpad `bed_chain.txt`）：實走與重拍每 30 秒看佇列，有更高優先的人在等就停掉自己的程序樹、release，之後重跑該段；安裝 job 失敗即停。
- 實走偶發失敗（10-31 16:42、19:15）：發生在泳池入水台階往下的第一階，不是旅館樓梯，屬於「懸空」判定的餘裕太小（站立高差約 120.5 cm，上限 120 cm），詳見 `Docs/STREET_ERRORS.md`。要修得改 StreetSocialCity 的 `CityEnvironmentImmersionAudit.cpp`，依規則不動，失敗就重跑。
