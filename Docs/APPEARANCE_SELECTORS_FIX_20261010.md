# 圖片式外觀選擇修復

## 新增驗收門檻（使用者 2026-10-10 指定）

依 UI 類型順序，每種類型連續選十個不同素材，再進下一種類型；逐筆驗證套用結果。每次點選到新角色畫面可顯示必須小於 300 ms。資料欄位測試、縮圖載入、平均時間、背景工作成功都不能代替這項驗收。冷載入與已有資源的切換分開記錄；不得把點選後的準備時間排除在外。

接續工作順序：

1. 增加可持續寫入逐筆結果的全類型 GUI 順序驗收，記錄真實按鈕 callback 至顯示提交完成的時間、素材識別及保存結果；數量不足十個的類型明確列出。
2. 取得目前逐筆基準，區分來源解析、卡片輸出／讀回、session 打開、材質／模型建立與 render readiness。
3. 將即時顯示更新與保存資源工作分開，以有版本與失效規則的 runtime 資源準備、局部替換和工作合併避免重建整個角色。保留保存一致性及失敗回復，不能只提前改 UI 選取色。
4. 重新跑同一套完整序列；任何筆超時或套用失敗均列為未通過，直到符合門檻。

## 計畫

1. 重現眉型點選，分別驗證請求、卡片欄位、來源材質、獨立資源與實際預覽；不能只以縮圖或 ID 變更判定成功。
2. 盤點所有有範例圖的選擇器，檢查來源識別、材質覆寫清理、換裝範圍與失敗時保留目前角色的共用流程。
3. 修正根因，保留原始卡與無關編輯；驗證原始素材、模組素材、重置與保存後重新載入。
4. 編譯與執行測試，記錄實際通過項目及尚未驗證範圍。

## 初步證據

- 使用者的眉型請求 73760E064EE51A2BC304D3AC65B08170 已輸出 `.source.png`，但當次程式關閉前沒有輸出完成卡或 report，不能視為成功。
- 同一輪髮色請求的 native_request_ms = 210213.803，完整重建約 210 秒；UI 的「正在套用」不能當作即時套用成功。
- 現有驗收僅抽查眉型及上眼線，不能代表所有圖片式選擇器可用。

## 已實作的共用修正

- 眉型／上下眼線／鼻型的直接貼圖選擇改為更新所有 outfit 的內容定址貼圖，保留模型、骨架、權重、法線貼圖與其他素材。仍使用來源材質的原生取圖與著色流程；不支援的材質程式回到完整來源重建，不按卡片名稱特判。
- 服裝花紋、徽章、領口等更新範圍依欄位登錄表的 Coordinate 區塊判斷，避免因名稱不是 clothes 開頭就重建全部 outfit。
- 上眼睛高光切換清除原生 `_overtex1` 的衝突覆寫，保留下高光與無關貼圖。
- 左右眼漸層依 PupilGradient1／2 精確尋找來源，避免另一眼的模組覆蓋目前眼睛。
- 舊版 APNG 補回外觀來源 metadata 時保留目前骨骼／懷孕編輯狀態，包括「原本不存在」的 modifier。避免換眉毛意外恢復原始卡的 ABMX，導致服裝變形。
- 新增可直接執行與檢視的 `ok-appearance` 診斷程式，GUI 驗收加入 modifier 不變檢查。

## 資料層驗收（2026-10-10）

- 14 個 appearance 測試通過，0 失敗。300 個圖片選擇欄位各執行套用與重置，共 600 次交易。
- 目錄取得 38,739 筆素材；53 種分類各抽一張真實縮圖，以 32／21 分頁取得，0 遺失。這不代表逐一驗證所有素材。
- 原請求的完整重建需 291.407 秒；新版直接貼圖路徑不重建任何模型，最近一次七套服裝的來源寫入至完成卡片為 2.815 秒（不含 GUI 後續重新載入）。
- 同一眉型與完整原生來源重建結果逐套比對，七套貼圖一致；其他模型、權重、bind pose 與貼圖保留。
- C001 七套服裝換眉型前後，rest／實際 posed 頂點位置及可見狀態完全一致。
- 證據：`Saved/Tests/AppearanceSelectors20261010/appearance-tests-r3.log`、`geometry-1791563729359486800/acceptance.json`、`delta-1791563729377758400/acceptance.json`、`thumbnails-0.report.json`、`thumbnails-1.report.json`。

## 畫面驗收狀態

- R1 的八項 GUI 自動檢查雖通過，但畫面顯示服裝位置異常，因此不接受該輪作為完成證據。已修復 metadata 回填的 modifier 問題，並強化檢查。
- R2 八項 GUI 驗收已通過，人工檢視模組眼線套用後的圖片確認服裝位置正常。這是原功能修復證據，不代表新增的 300 ms 門檻通過。
- 已經產生的舊版測試輸出不會被自動覆寫；最終驗收從原始 Content APNG 開始。

## 300 ms 工作進度（未完成）

- 新增 `ok.AuditEditorAppearanceSweep`／`run_editor_acceptance.ps1 -Mode AppearanceSweep`。預設按 UI 類型順序各選前十個不同素材，逐筆記錄從真實按鈕 callback 到 render fence 完成及後續畫面幀的時間，並等待最終材質完成。每筆核對預覽存在、選取來源及保存卡片的欄位／resolver idempotence。
- `OK_APPEARANCE_SWEEP_LIMIT`／`OK_APPEARANCE_SWEEP_START` 僅供有明確標記的診斷子集合；存在時不得宣告完整驗收成功。
- 冷頭型第一筆實測 526821.870 ms，保存及來源核對成功，效能失敗。已停止此輪診斷並保留報告，沒有宣稱全類型通過。
- 原生眼睛的獨立上下高光貼圖納入 delta 更新；七套服裝的眼睛 RT、高光與 UV 已和完整原生來源比對通過。
- 原本七次重包的 global texture 修改改成一次 batch transaction；batch／serial 等價與重複 index 拒絕測試通過。
- 新增 native texture-only transaction 與既有 renderer 的 MID 貼圖替換，保留 live animation／gaze／骨架／物理，不重建模型及 shader；目前正在編譯與 GUI 計時驗證。
- 最新資料層 appearance 測試 15 項通過（包括 600 個套用／重置／再次套用 idempotence 交易），portable_outfits 4 項通過；兩項七套服裝眉型／geometry 安裝環境比對亦再次通過。
- 仍須移除點選關鍵路徑中的完整卡片保存／讀回與非局部資源準備，並完成全部類型十筆的嚴格 GUI 驗收。現有資料不能證明小於 300 ms。

## 2026-10-10 後續基準與架構驗證

- 真實素材目錄按 UI 類型順序連續交易，每種十個不同素材；300 種、3,000 筆全部通過欄位／resolver 再套用 idempotence 核對，結果持續帶入下一筆與下一種類型。證據 `Saved/Tests/AppearanceSelectors20261010/ten-per-type-metadata.json`、`ten-per-type-metadata.log`。此項明確為 data_layer_only，未驗證 renderer／GUI 速度。
- GUI HotTextureR4 十筆眉型全部成功套用／保存／來源核對且 incremental_preview=true。從點選到 render ready 為 4058.772–20934.336 ms，全數未達 300 ms；game-thread 貼圖提交為 10.043–48.703 ms。不可把提交時間當作完整切換時間。證據 `Saved/Tests/EditorAcceptance/AppearanceSweep20261010HotTextureR4/report.json`。
- HotTextureR3 新增 worker 錯把 APNG 送回原作擷取路徑，Session 準備 68171.886 ms，且與 portable 模型 layout 不符。已改空 GameRoot 讀取 APNG，R4 十筆增量提交實際成功。
- progressive readiness 會主動提交尚未使用的 parent 編譯工作；GUI sweep 等待當前 actor 最終材質與 render readiness，不以全域未顯示資源編譯當作其完成條件。Sweep 加入 stage／busy 進度、初始報告與每類型首／末筆 screenshot。
- 新增 native `appearance/live_head.rs` runtime context：初始化時取得 APNG 的材質定義及來源 metadata；每次 candidate 只產生局部貼圖與欄位變更，準備失敗不改目前角色。提交前核對版本與角色 shape/layout；過期 candidate 拒絕。Checkpoint 捕獲完整累積修改及版本，以一次 batch transaction 寫回所有 outfit。
- MainAlpha 使用原生 shader texture table，因此同時更新 png 與 shader_textures:_MainTex；原生材質的 MainTex 改為可替換 MID 參數。只放寬 shader_kind 而不更新原生輸入並不能修復顯示。
- 新 runtime context 尚未接到 GUI 點選／背景 checkpoint 佇列。需要先完成 native 七類各十筆、live animation／geometry 保留、過期與錯誤 shape 拒絕、checkpoint 重新載入一致性，再接 GUI。不得以尚未接入的候選準備時間宣稱 UI 達標。
- 仍須完成全部 300 類型的實際 GUI 十筆切換與每筆 <300 ms；head／composed skin／hair／clothes／accessory geometry 需要依來源程式設計局部準備，不能採卡片專用預製資源或以只改 UI 選取狀態代替。

## 順序 GUI 測試與第二輪 CPU 修正（未完成）

- `AppearanceSweep20261010HeadTypesR1` 已實際依眉型、上眼線、下眼線、眼白、上高光、下高光、鼻型順序各選十筆，共 70 筆。全部 preview/selection 與保存欄位／來源核對成功，全部 incremental_preview=true；但 click-to-render-ready 為 3324.954–14804.070 ms，70 筆效能均失敗。這是診斷子集合，不是全部類型驗收。
- 每類首／末筆已留圖，但圖片實際只有 888×500；不能把啟動參數 1600×1000 當作實際解析度。後續 sweep 在開始前要求 viewport 達到指定尺寸，逐筆記錄實際 width/height，啟動器加 ForceRes。等待編譯與實測證明。
- Native runtime context R11 與 R12 均完成七類各十筆、動畫／geometry 保留、過期 candidate／錯誤 shape 拒絕、累積 checkpoint 保存重載一致性。R11 同時有 GUI／編譯負載，不能直接將整輪時間差歸因於程式修正。
- 眼白 create_eyew 僅依賴來源紅色 byte 與固定顏色，改成 256 項 LUT，保留逐項原公式運算順序。六組色彩空間／顏色測試組合、256 種紅色與四種來源 alpha，逐像素等於原 scalar 公式；`eye-white-lut-tests-r12.log` 通過。R12 native 準備眼白平均 675.37 ms、最慢 2234.18 ms，仍未達標。
- 純眼白／RGB tint 轉換新增以完整來源 bytes 與所有參數為 key 的 runtime cache，最多 128 項／64 MiB，計算在 mutex 外進行；用於重複 outfit／左右眼共用結果，不能以卡片或選取 ID 作快取鍵。另將白色 tint 的免轉換判斷移到 PNG 解碼之前。
- MainAlpha 若先前被證明 opaque，新貼圖出現透明度則不能沿用原本 depth-writing specialization。增量更新會拒絕並要求完整材質替換；此時不宣稱 300 ms 通過。新增拒絕與不修改 live state 的測試。
- R18 native 驗證已通過：七類各十筆連續準備／提交、動畫與 geometry 保留、stale／錯誤 shape 拒絕；累積 checkpoint 重載目前 outfit 一致，並另外逐一核對全部七套的 renderer 語意欄位與資源 content id，與各套獨立解析／套用完全相同。原本誤判的 outfit 4 差異來自飾品 HashSet 的順序，修正驗收後單次 face 解析通過比對，沒有放寬真正的有序資料核對。
- R18 appearance 23 項、PNG cache 2 項、眼白公式 2 項、portable_outfits 4 項全部通過。`editor-build-cache-r18.log` 編譯成功，啟動 DryRun 通過；GUI `AppearanceSweep20261010HeadTypesR3` 進行中。尚未接入 runtime context 即時交易／背景 checkpoint，不能宣告小於 300 ms。

## 原作眼白 RT 尺寸根因

- 原作 `C:/portable/KoikatuSunshine/kks_decomp/Assembly-CSharp/ChaControl.cs:10751` 的 InitBaseCustomTextureEtc 設定 `num = hiPoly ? 512 : 256`，EyeW 與左右 Eye 建立相同尺寸；`CustomTextureCreate.cs:186` 以 Graphics.Blit 寫入 createTex，並開啟 sRGBWrite。
- 原本眼白 CPU 路徑直接以來源 PNG 寬高輸出；高解析 mod source 因而重算／重編碼遠超原作 RT 的像素數。新的 production 路徑固定依 high/low 模式輸出 512／256，取樣紅色先依來源色彩空間解碼，再雙線性插值、以 linear 顏色 lerp、編碼 sRGB；來源 alpha 仍不控制輸出 alpha。
- Red channel 的解碼用 256 項表，固定顏色的線性值移到迴圈外；cache key 包含完整來源、全部顏色、source_linear 與 RT 尺寸。不是按卡片 ID 降質或把烘焙後圖片任意縮小。
- 新測試對兩種尺寸／兩種來源色彩空間的每個輸出像素，與獨立四點取樣及原始 shader 公式比對；待 R19 實測與 GUI 眼白十筆確認。完整 GUI <300 ms 仍未達成。
- R19／R20 的尺寸及取樣公式三項測試通過；R20 再加入來源上下翻轉的逐像素一致性，兩個方向均完全相同。眼白改用來源 row index 翻轉，避免先編碼整張高解析翻轉 PNG 再解碼。
- R21 移除眼白的重複來源解析：優先取得一次具 row-order provenance 的來源，失敗才使用既有 texture-pool fallback；iris 及其他 head source 路徑維持原有解析。正在跑公式／七類連續提交／七套 checkpoint 驗證與編譯，尚無新版 GUI <300 ms 證據。
- R3 GUI 已完成七類各十筆：70/70 套用、保存來源及增量 renderer 成功，實際全為 1600×1000；端到端 2582.069–13350.900 ms，0/70 達到 300 ms。資料在 `AppearanceSweep20261010HeadTypesR3/report.json`，按類別彙整為 `AppearanceSelectors20261010/head-gui-r3-summary.json`。不可把這輪稱為效能驗收通過或完整 300 類型驗收。

## 後續實作順序

1. 完成目前 R3 七類各十筆的實際 GUI 比對，記錄實際解析度、每筆端到端耗時與首／末筆畫面；保持二進位不變。
2. 將 runtime candidate 接到 native FFI 與既有 actor 的 texture transaction。不得把完整檔案保存／讀回／Session::Open 留在即時點選關鍵路徑。
3. 以版本化 working-card transaction 管理連續點選；提交前寫入可恢復的操作 journal，checkpoint 背景合併且只發布對應版本。過期結果不得覆蓋新選取，失敗不得清掉尚未保存操作。
4. 存檔、換人物／服裝、切換到 geometry／合成貼圖參數及關閉場景，必須使用相同 working revision 或等待對應 checkpoint。不能只對眉型開快速分支而讓其他編輯讀到舊卡片。
5. 依來源程式提供局部 face/body composition 與 head/hair/clothes/accessory geometry preparation，保留其他 renderer／rig／shape；所有資源由載入 APNG 與共用來源在 runtime 準備。
6. 最終從原始 Content APNG 開始，依完整 UI 類型順序各選十個不同來源；逐筆功能、保存／重載及 <300 ms 全數通過才完成。3,000 筆 metadata 驗證與七類診斷不能替代完整 GUI 驗收。

## R21／R22 實測與後續增量 API（未完成）

- R21 native 眼白公式 3 項、appearance 23 項、outfit 4 項、PNG cache 2 項通過；七類各十筆 live preparation 與全部七套 checkpoint 語意／資源比對通過（47.63 秒）。目前 GUI 仍沒有使用此 runtime context。
- `editor-build-eye-rt-r22.log` 成功。R4 GUI 眼白十筆全部套用、保存來源及增量 renderer 成功，實際 1600×1000；十筆均超過 300 ms。最後一筆 11199.686 ms，其中 native request 9507.790 ms、提交 15.999 ms。證據 `Saved/Tests/EditorAcceptance/AppearanceSweep20261010EyeWhiteR4/report.json`；不可宣稱效能通過。
- 來源追蹤發現眼白取得 PNG 後，又透過 face_base_color_space 再載入／解析來源 bundle。改為解析時攜帶同一 Texture2D 的 import color space；loose Sideloader PNG 保持 sRGB 預設，只有 texture-pool fallback 使用舊查詢。須重新驗證與量測，不能把刪除第二次 IO 當作完整達標。
- 新增 additive C ABI `appearance/live_head_ffi.rs`：owned context／candidate／checkpoint，candidate metadata 與像素採 caller-owned buffer query/copy，提交前檢查 revision／layout，checkpoint 捕獲後可在獨立 worker export。尚在編譯／測試，未接 GUI；不能提前解除 busy 或讓其他編輯讀到舊卡片。下一步仍須可恢復 journal、版本化 working state 與 GUI／保存整合。

## R24 native 與 Unreal 增量端點

- R24 native 編譯成功；C ABI 無效 handles／buffer 測試 1 項、真實 C001 owned candidate／checkpoint 生命週期 1 項通過。後者核對 query/copy、過小 buffer 不寫入、驗證不消耗 candidate、成功只消耗一次、stale 拒絕且保留 candidate、動畫時鐘保留，以及 context 釋放後仍可 export checkpoint 並重載同樣貼圖。證據 `ffi-tests-r24.log`、`ffi-installed-r24.log`。
- R24 七類各十筆、全部七套 checkpoint 比對通過（49.25 秒）；眼白逐 byte／512及256 RT／雙方向公式 3 項通過。`appearance::tests` 本版 16 通過、5 ignored；PNG cache 2 項、portable_outfits::tests 2 項通過、3 ignored。未啟用的 ignored 測試不可算通過。
- Unreal 新增 `FOkAppearanceContext/Candidate/Checkpoint` RAII 包裝，以及 actor `ApplyHeadTextureCandidate`。這條端點只取得 delta 像素，保留 sampler／上下眼高光；所有 renderer／貼圖檢查完成後才提交 native，再更新 MID。不讀完整 APNG／不打開新 Session。正在建置，尚未接入正式 GUI、未有新版端到端速度證據。
- R4 眼白十筆完整統計為 2822.793–11700.696 ms，平均 6864.825 ms。另完成 1600×1000 正面／側面臉部近照並目視檢查，未見明顯眼白／瞳孔倒置：`Saved/Tests/AppearanceSelectors20261010/EyeWhiteR4CloseupR2/`。視覺來源為 R22 GUI 保存的卡片，不能拿來證明 R24 尚未接入的 GUI 路徑。

- 新增 `ok.AuditAppearanceCandidates`（runner `AppearanceCandidates`）：以真實 actor 驗證同七類各十筆，包含 worker queue、native prepare、UE texture allocation／MID commit、render fence＋兩幀，再核對累積 checkpoint 重載貼圖。報告固定 diagnostic_subset=true、uses_ui_callback=false、valid=false、complete=false；這個診斷不能代替完整 GUI 圖片 callback 驗收。正要編譯／實測。

## R25 actor 診斷與 R26 metadata 改善

- R25 Unreal 建置／啟動檢查通過。`AppearanceCandidates20261010R1` 七類各十筆 70/70 真實 actor 顯示提交成功、1600×1000，最後累積 checkpoint export／重載所有貼圖 CRC 相同，二進位 hashes 在診斷前後未變。只有 9/70 低於 300 ms；仍是 uses_ui_callback=false 的端點診斷。runner 因報告刻意保持 valid=false 而拒絕當作正式驗收成功，符合防誤報設計。彙整 `candidate-actor-r25-summary.json`。
- R25 actor 準備到顯示平均：眉型 553.223 ms、上眼線 750.854、下眼線 333.969、眼白 1956.054、上高光 397.562、下高光 403.319、鼻型 1400.533；不可把 native-only 平均當作 GUI 時間。已加入 worker prepare／actor commit／queue+render wait 分項，以及逐張貼圖尺寸與準備時間，用下一輪確認差距。
- R26 合併 KKEx 的 resolver 與 MaterialEditor conflict 處理，對同一個解碼 tree 依序修改，最後只編碼一次；避免中途把含大型 opaque texture blobs 的整個 KKEx 編碼再解碼。全部 300 targets × apply/reset 的 fused 與舊兩階段輸出逐 byte 相同測試通過，另保留 unknown plugin／opaque texture dictionary。
- R26 appearance 17 項、真實 C ABI lifecycle、七類各十筆＋七套 checkpoint 語意及資源核對全部通過（42.55 秒）。這輪 native metadata patch 平均約 61–67 ms；眉型 candidate 平均 125.081 ms，但上眼線平均 371.138 ms、眼白最慢 2419.937 ms，仍有冷來源成本。不能宣告小於 300 ms。

## R27 分項 profile 與 R28 source ownership

- R27 建置／啟動檢查通過；`AppearanceCandidates20261010R2` 70/70 真實 actor 提交與 checkpoint 重載 CRC 核對成功，35/70 <300 ms。上／下高光各 10/10 達標，其餘類別仍有超時。仍是端點診斷，沒有宣稱 GUI callback 或全部類型驗收成功。
- R27 平均 worker／actor commit（ms）：眉型 429.169／7.822，上眼線 773.719／9.683，下眼線 333.861／5.406，眼白 1740.071／6.923，上高光 217.315／6.132，下高光 226.589／5.400，鼻型 598.869／5.061。主要瓶頸在 source preparation。彙整 `candidate-actor-r27-summary.json`，原報告保留四段 native timings、素材 PNG bytes、viewport；log 保留 UE 貼圖尺寸／準備時間。
- `unity::BUNDLE_CACHE` 與 `ParsedBundle` 的 RefCell cache 原本是 thread-local。UE pool 每筆可用不同執行緒，造成同來源在不同 worker 重複解析。R28 C ABI context 改成固定 `ok-appearance-source` 執行緒負責初始化與來源準備；不以 unsafe Send/Sync 共用 RefCell，不加全域 parser 大鎖。版本／live 提交仍使用同一 context mutex，checkpoint 捕獲後獨立保存，釋放時送 Stop 並 join source owner。
- R28 編譯、無效 handle／buffer 測試與 installed C ABI lifecycle 通過。測試從兩個不同 caller thread 同時準備，核對回傳的 preparation_thread 相同且不同於主測試執行緒；stale 拒絕、live 動畫保留、context 釋放後 checkpoint 保存重載仍通過。Unreal R29 正在建置，待以相同 70 筆 actor 診斷確認。

## R29 固定來源執行緒實測與 Texture2D 索引

- R29 建置及啟動檢查成功；AppearanceCandidates20261010R3 同序七類各十筆共 70 筆，全部 actor 提交及累積 checkpoint 重載 CRC 通過。45/70 <300 ms：眉型 3、上眼線 0、下眼線 10、眼白 5、上高光 10、下高光 10、鼻型 7。所有 preparation_thread 均為同一 ThreadId(12)。這仍是 uses_ui_callback=false 的診斷，正式 GUI 及全部類型驗收未完成。
- R29 上眼線 warm resolve 仍約 261–321 ms，第一筆約 1028 ms。ParsedBundle 的 decode/color-space/sampler 原本各自循序建立前方全部 Texture2D UnityObject，包括大型像素資料；新增每個 parsed bundle 的大小寫不敏感 object location 索引，保留重名第一筆語意，後續只解析選中的 Texture2D。正在以原始素材的線性查詢 oracle 比對 PNG、color space、sampler，並重新跑連續切換／七套保存一致性。
- with_serialized 初始化後改採唯讀 borrow，允許 mesh walk 內的巢狀唯讀貼圖查詢；沒有跨執行緒共用 RefCell。R30 編譯及測試尚在進行，不能預報效能改善。

## R30／R31 索引回歸與實際 actor 結果

- R30 原始三種 bundle 各十張貼圖、大小寫查詢的 PNG／import color space／sampler 與舊線性解析完全相同；巢狀空 bundle 查詢通過。appearance 17 項、owned C ABI lifecycle、七類七十筆及全部七套 checkpoint 比對均通過；runtime-head-preparation-r30.log 完整測試 33.65 秒。Native 數值與並行建置負載不可直接當作單一優化因果證明。
- R31 Unreal 建置成功；AppearanceCandidates20261010R4 70/70 actor 提交、1600×1000、checkpoint 重載 CRC 成功，44/70 <300 ms。眉型 0、上眼線 0、下眼線 9、眼白 6、上高光 10、下高光 10、鼻型 9。上眼線平均 627.34 ms、眉型 348.95 ms；仍然不達標，不把單次總通過數變動當作改善證明。詳見 candidate-actor-r31-summary.json。
- R32 把同一 Texture2D 的 import color space／sampling 一併保存在索引，避免查 metadata 又重建含像素資料的 UnityObject。原始素材 30 張線性 oracle 與巢狀查詢通過。R34 正加入 residue decode/encode、eye source、detail source 的可選分項 profile，以釐清 resolve 段的實際瓶頸；正式 GUI 接入與完整驗收仍未完成。

## R34 細分 profile：上眼線大型來源複製

- R34 七類各十筆、動畫／geometry 保留、七套 checkpoint 再次通過（36.20 秒）；profile 開啟的耗時不能當最終 speed acceptance。
- 精確分項顯示 residue 約 341 KB，decode 約 0.6–1.5 ms、encode 約 0.2–0.5 ms，並非主要瓶頸。上眼線兩個圖層各自 detail_source warm 約 100–110 ms。原始 mt_eyeline_up_00.unity3d 實際 148,128,157 bytes。
- direct_catalog_png 對每個候選名稱執行 exact_png_from_bytes(b.clone(),name)，即使頭型 suffix 不存在也複製整個 148 MB。新增借用 exact_png_from_ref，保留 owned API 相容性；候選只查 parsed cache，cache miss 才取得 owned 解析資料。仍保留 source provenance、head suffix/base 順序及原作 row flip。
- 正在 R36 編譯／重跑同序 native 與實際 actor；尚未宣稱上眼線或全部 GUI <300 ms。新增 read/lookup/transform 分項以確認剩餘成本。

## R36 拿掉候選複製後的 profile

- R36 原始素材 oracle 及同序七十筆／全部七套 checkpoint 測試通過（27.41 秒）。上眼線 warm direct_catalog 每圖層約 45–54 ms，其中 read 約 38–46 ms、lookup 約 6 ms；首次 parse 仍約 202 ms。整份來源反覆複製已移除，但仍每個 renderer 讀取同一 148 MB 檔案。
- R38 增加 canonical path＋size/mtime 的 runtime 索引，命中時只向既有 bounded parser cache 取得 Arc；索引本身不持有 parsed bundle，不會延長 parser eviction 後的像素資料生命週期。檔案消失不回傳 stale bundle，size/mtime 改變重新讀取，既有 clear_bundle_cache 一併清索引。不是卡片特化預製資源。
- owned parse_bundle_cached 現在 cache miss 直接取得 Vec 所有權，避免先 borrow 再複製整份 bytes。direct_catalog 對同一 parsed source 按原有次序查 exact texture，PNG override priority／flip 不變。R38 編譯與失效／eviction／原始輸出 oracle 待完成。

## R39 回歸完成與 R40 實測

- R39 source path cache 測試通過：相同檔案／canonical alias 重用，大小改變失效，同大小但 mtime 改變失效，parser cache 淘汰後重新建立，檔案刪除不回傳 stale，clear_bundle_cache 一併清 path index。原始三 bundle 各十張 PNG／色彩空間／sampler oracle 通過。
- appearance 17 項、owned C ABI 跨 caller thread／stale／動畫／context 釋放後 checkpoint 保存重載、七類各十筆與全部七套 renderer 語意／資源核對全部通過。證據 appearance-tests-r39.log、ffi-installed-r39.log、runtime-head-profile-r39.log（25.28 秒，含 profile logging）。
- 上眼線 warm direct source: read 約 0.17–0.29 ms、lookup 約 6 ms、含 flip 總計約 7–9 ms；cold 第一個 source parse 仍約 193 ms＋52 ms 初次索引／貼圖。不能跳過 cold case 後宣稱全部 <300 ms。
- R40 Unreal 建置成功（65.51 秒），DryRun 成功。AppearanceCandidates20261010R5 正以同序七十筆 actual actor 測試。正式 GUI 尚保留 full-card export 關鍵路徑，仍需 working-state／保存一致性整合；不宣稱正式 UI 完成。
- GUI sweep 明確記錄 uses_ui_callback=true 及 required_selections_per_target=10；任何類型不足十筆立即判未完成。runner 另外記錄全部 project plugin manifest 指向的現存 DLL hashes，防止其他渲染／卡片模組在測試期間變更卻仍當成同版本證據。

## R40 實際 actor 結果與背景優先順序

- AppearanceCandidates20261010R5 70/70 提交與 checkpoint 重載 CRC 通過，54/70 <300 ms；下眼線／上高光／下高光／鼻型各 10/10，眼白 9/10、上眼線 4/10、眉型 1/10。上眼線平均 338.81 ms、max 1012.38；cold mod 眼白 max 2646.04。不能當作全部成功。
- log 與逐筆 timings 顯示前 16 筆同時建立未顯示的服裝預熱 actor；其 native open、貼圖分配及材質編譯與前景競爭。眉型 metadata/card parse 早期約 125–220／80–200 ms，上眼線後四筆恢復約 73–84／53–56 ms。不能把這種工作負載差異歸因於貼圖索引本身。
- TickClothingPreviews 原本只在 bAppearanceBusy 暫停，使用者瀏覽 appearance panel（或內部端點診斷）時便繼續建立隱藏 actor。改為 panel 可見、縮圖載入或 property transaction 時暫停，保留 pending job/queue，關閉後繼續；不刪除預熱功能、不只在測試模式關閉。待 R41 編譯與 actor／服裝預熱回歸確認。

## R41 前景優先順序回歸

- R41 建置成功。AppearanceCandidates20261010R6 70/70 actor 提交及 checkpoint 重載 CRC 通過，66/70 <300 ms。眉型 10/10 平均 154.43 ms；上眼線 9/10 平均 197.91 ms（cold 448.44）；下眼線 10/10；眼白 8/10（cold mod 1952.78、另筆 325.48）；上高光 9/10（312.15）；下高光、鼻型各 10/10。仍是未完成的端點診斷。
- log 只有開啟面板前已派發的一筆服裝 worker，面板可見時沒有繼續建立新隱藏 actor。R42 增加實際 OnCloseAppearance 後等待 background cached actor 的驗證，並逐幀確認 active textures CRC 保持不變；不能以永久關閉服裝預熱換取快數字。R42 建置成功，R7 進行中。
- 冷 mod 眼白使用一般 catalog 來源 [moderchan]charamaker support MOD 的 CMS_NT_00 bundle（33,621,699 bytes，zip 29 entries），需繼續拆分 bundle 讀取／解析／PNG 與 RT bake，不能假定只是 8K 圖片或 GPU 不足。正補 source/bake 分項。

## R42／R43 精確結果

- R7 70/70 提交與 checkpoint CRC 通過，68/70 <300 ms；background_clothing_resumed=true。冷上眼線455.32 ms、冷眼白2060.56 ms仍超時，不能視為完整 GUI 驗收。
- R43 native 七類各十筆及七套 checkpoint 通過25.29秒。CMS_NT_00 冷 source read30.561 ms、總1345.897 ms；PNG實際512×512、白眼合成13.697 ms。瓶頸不是大尺寸 RT bake。R44繼續拆分 bundle載入／解壓／serialized初始化／Texture2D catalog，以證據決定修正。

## R44 冷來源根因與 R47 修正

- R44 native 同序七十筆／七套 checkpoint 通過24.40秒。CMS_NT_00 bundle load494.415 ms、extract155.477 ms、serialized初始化371.583 ms、catalog總599.757 ms（含初始化）；共有2879 Texture2D。
- BundleLoadOptions 預設 load_assets=true，先解析 embedded serialized files，但 ParsedBundle 又重新解析。R47讓 ParsedBundle只載入container/block，保留 public loader既有asset行為；檔案改為shared immutable bytes供serialized parser直接持有，避免serialized初始化再複製整份資料。現有stream讀取API仍兼容Vec來源。
- R45/R46編譯指出shared consumers的型別／test模組路徑漏改，已修正；R47編譯驗證中，未部署。正式GUI與全部類型速度仍未完成。

- R47編譯、原始30張PNG／color space／sampler oracle，以及同序七十筆與七套checkpoint通過25.84秒；但CMS冷source仍1339.169 ms：serialized初始化0.110 ms，整包load532.272 ms、extract442.011 ms、catalog267.748 ms。不得將單階段改善冒充整體達標。
- R48改為保留compressed container/file view，stream只請求經overflow與file-range檢查的需要區間；.resS/.resource不當作SerializedFile全讀。UnityFS由既有lazy block cache解壓，legacy使用原有extract path，外部Vec stream仍保留。編譯驗證中。

- R48／R49未部署：lazy nodes/files差異及resource flags造成來源失敗。R50保留全部directory entries並強化獨立eager oracle：原始三bundle＋CMS模組，各十張PNG／CS／sampler必須成功一致，檔案range首尾與越界／overflow檢查；另跑真實head source與checkpoint。

## R50／R51驗證與正式介面接續計畫

- R50獨立eager oracle：原始三bundle＋CMS模組40張實際PNG、CS、sampler及stream首尾／越界／overflow通過4.79秒。七類十筆及七套checkpoint通過24.30秒；appearance25、unity43、dbone37、reference3測試通過，owned FFI保存重載4.56秒通過。R51 Unreal build成功61.58秒，未修改engine原始碼。
- R8 actual actor 70/70套用、checkpoint、關閉後服裝背景恢復通過；63/70<300ms，冷眼白1462.77ms。CMS native冷來源875.655ms（serialized_init822.855ms），warm約30–45ms。不可把部分來源改善視為全部達標。
- R51正跑實際GUI七類依序各十筆；uses_ui_callback=true，此子集合不是全300類驗收。
- 正式快路徑需把每次顯示交易與APNG重包分開，但保存／reload仍須取得相同revision：先加入不可變deferred preview source到FKKCharacterFile，讓所有SaveWithEdits呼叫共用保存屏障，保存失敗不得清除pending snapshot或覆寫原卡；用fixture驗證保存後資源與pending typed edits並存。再接入owned context/candidate及compact metadata，加入持久化journal／nav及geometry reload屏障，最後才擴展剩餘geometry／composed-skin類型與完整GUI測試。不得只在UI提前解除busy而遺失保存一致性。

- 正式GUI HeadTypesR5七類依序各十筆：70/70套用與保存來源核對、0/70<300ms；2403.05–5798.40ms、平均3120.68ms。證據head-gui-r51-summary.json，明確為診斷子集合。
- R52/R53加入deferred preview共用保存屏障與candidate raw ABI。SaveWithEdits只更新資源preview，保留current raw/typed edits，失敗保留pinned revision；SaveUnchanged仍保存serialized參數baseline，也必須先取得同一preview。fixture測試包含retry、write failure、invalid PNG、card copy、typed edit、unchanged語意。實際actor診斷的checkpoint將經FKKCardWriter保存再重載CRC。尚未接正式UI，編譯／驗證中。
- 候選raw實際43719849 bytes，不能稱為小型compact metadata；大型KKEx必須納入即時資料更新成本與後續去重設計。

- R54 native ABI buffer拒絕與owned candidate/checkpoint lifecycle通過（5.16秒）；候選raw與保存checkpoint全block一致，commit後handle不可讀。R53共用型別編譯發現fixture TSharedRef比較錯誤，已修正，重建待完成。
- FOKAppearanceDeferredPreview具體橋接已加入：immutable native checkpoint由shared owner持有，以mutex合併多個save caller的資源輸出，成功後快取同revision PNG；只刪除此call建立的精確temporary filename，失敗保持checkpoint可重試。actual actor audit改由candidate raw讀取卡片、附加deferred source、SaveWithEdits、reload並核對Custom/KKEx與renderer CRC。仍未接正式UI。

- R57重建成功。R58明確四個Unreal automation tests全部執行並通過（3 success、1包含預期write-failure warning的success、0fail/0notRun）：DeferredPreviewSave（含UScriptStruct copy、失敗retry、invalid PNG、SaveUnchanged語意、typed edits）、EmbeddedPngSave、ShapeRoundTrip、AppearanceCardTransaction。report位於Saved/Tests/AppearanceSelectors20261010/DeferredPreviewR58/report/index.json。
- actual actor R9開始用實際FKKCardWriter/deferred preview完成checkpoint保存與重載CRC，不再只直接native Export。正式UI仍未接快路徑，不能宣稱全部300類或<300ms。

- R9 actual actor 70/70套用、正式CardWriter checkpoint保存重載、background clothing恢復通過；68/70 <300ms，冷眼白1274.68ms、上眼線328.29ms仍超時。仍是uses_ui_callback=false診斷子集合，未完成正式GUI全類驗收。
- R59計畫：Texture2D查詢只解析版本化SerializedFile metadata，透過已驗證的bundle range讀取每個header與選中object；不全讀含大量inline圖片的SerializedFile。保留legacy fallback、原始TypeTree/ref_types/byte order及first-match順序。用獨立eager oracle核對完整catalog、40張PNG/CS/sampler與實際冷來源，再決定部署。

- R60 sparse Texture2D完整catalog順序/尺寸/名稱、40張實際PNG/CS/sampler/ranges oracle通過2.61秒；70筆runtime與7套checkpoint通過22.97秒。CMS冷來源205.040ms（catalog159.718ms），由R50的875.655ms下降，仍需實際actor/GUI量測。
- R61保存一致性：Context持有baseline APNG bytes及各套outfit snapshot；每筆candidate準備全outfit的global head變更，checkpoint只repack已準備資源，不再重讀base path或重新解析game/mod來源。先驗證全outfit獨立oracle、後續commit/context釋放不改變舊checkpoint，再處理新增準備成本。

- R61 pinned全outfit快照七十筆、獨立七套oracle、後續commit/context釋放後舊checkpoint byte-exact一致及禁止覆寫輸出通過28.96秒。R62開始消除各outfit重複解壓同一zip bundle：以canonical archive path、entry、size/mtime索引現有bounded parser cache，變更／刪除／eviction均失效；miss採full-content key，保留legacy fallback。加入archive lifecycle與真實CMS PNG/CS oracle。

- R64正式GUI整合計畫：catalog背景建立owned Context；已驗證七類由candidate準備全outfit→precommit checkpoint→寫完整APNG journal→讀卡合併pending typed edits→正式writer成功→actor commit。避免每次全renderer session重建；所有navigation/save既有Entrypath直接指向已保存資源，不引入stale/deferred路徑。source program/layout/shape不兼容則保持live原狀後走完整交易。尚待編譯與真實UI驗證；其餘geometry/composed-skin類型還未達速度要求。

- R64同輸入bake共用七十筆及七套獨立oracle通過22.69秒；precommit candidate checkpoint與postcommit checkpoint完全一致、context釋放後有效、stale/consumed候選禁止capture的FFI驗證通過4.72秒。unity44與appearance25項回歸通過。warm平均各類約129–166ms；冷眼白345.30ms、鼻線330.34ms仍超時，不能宣稱全部<300ms。
- R65 production GUI已接七類owned candidate +durable APNG before commit，尚在建置，正式點選結果待驗證。新增Tools/test_appearance_card_regressions.ps1，明確設置真卡副本所需兩個env並驗證5項test計數、failed/notRun及binary hashes，避免再次把fixture/跳過誤認真卡驗收。

## R65 實際介面結果與 R66 修正計畫

- 正式 GUI HeadTypesR6 依序七類各十筆，70/70 套用與保存來源核對成功，0/70 <300ms，完整數據 head-gui-r65-summary.json。這只是七類子集合，完整300類各十筆仍未完成。
- 每次 full APNG journal 約108MB；例：prepare150.82ms、journal426.32ms、read289.02ms、save484.72ms、actor10.53ms。完整APNG每次先落盤不是可達標的互動架構，必須改為 pinned revision、精簡持久化journal與正式保存／reload屏障，不能只提前解除busy。
- Expanded 真實輸出卡回歸五項全執行通過；KKEx 40,548,003 bytes，獨立full parser 5.027ms、selected parser0.053ms，resolver payload byte-exact。C001 baseline本身沒有KKEx，不能用先前baseline測試當作大型KKEx證據。
- R66 先修正可獨立驗證的根因：卡片分類改讀正式marker與block table，不對100MB任意資源多次掃字串；header解析借用immutable payload view避免兩份大型暫存copy；每個head patch清除不再被references使用的歷史unique資源；完整geometry交易成功後重建owned Context，避免第一類切換後所有後續類型退回legacy。驗證後繼續處理互動交易架構與完整GUI驗收。

## R66 驗證與 R67 互動／保存屏障

- R66 module-only build成功41.89秒，未修改engine；full target嘗試被-NoEngineChanges拒絕，已改回限定專案模組並保留失敗log。
- 原生七類70筆／全outfit獨立oracle／不再累積無references的unique資源通過22.53秒。大型108MB卡七項Unreal tests全執行通過（6success+1預期失敗路徑warning，0fail/0notRun）。read65.572ms，KKEx40.5MB selected0.053ms且resolver完整一致。
- R67 採一般編輯器的未另存工作狀態：每筆互動持有candidate metadata與不可變全outfit checkpoint；click只準備／讀metadata／提交renderer，不同步重寫108MB APNG。這是未保存編輯，不宣稱程式崩潰後可恢復；先前提出每click durable journal是內部設計選項，非使用者要求。正式SaveWithEdits、APNG擷取前、reload、服裝切換、關閉外觀面板、properties catalog、進入互動場景均先通過同一非同步保存屏障，成功才更新工作檔案，失敗保留pending revision供重試。
- 新head revision使所有舊服裝display cache失效，避免換裝還原舊眉毛／眼睛；關閉面板保存後用正式資源signature恢復服裝預熱。
- GUI sweep仍量真OnClicked到render fence＋兩幀；之後額外走正式保存屏障、記錄save_barrier_ms、驗證保存來源。未把APNG保存成本混稱互動切換時間，也沒有跳過保存驗證。編譯及實際GUI測試待完成。

- R67 build成功215.53秒。正式GUI HeadTypesR7：70/70套用與正式保存屏障／來源核對通過，只有3/70<300ms。眉型平均374.70ms，上眼線395.55、下眼線415.37、眼白415.83、上高光419.38、下高光386.09、鼻型352.39。未完成速度及全類型驗收。
- R68 計畫：保留選取頁的widgets與textures，僅更新選取背景色，避免每筆重建32縮圖；KKEx使用borrowed MessagePack tree及按map位置的sparse overlay，只materialize resolver與需修改的MaterialEditor清單，opaque images/unknown plugin資料只在最終輸出寫一次。用原有完整decode/edit/encode作獨立oracle，涵蓋所有registry targets、duplicate fields、EC resolver、unknown extension/extra version entries，並跑真卡70筆及全outfit保存重載後再部署。

- R69 borrowed sparse KKEx overlay編譯成功；所有registry target對完整decode/edit/encode oracle byte-exact通過1.00秒（包含重複MaterialTexturePropertyList、未知Ext、大型opaque plugin、EC resolver、缺resolver及empty KKEx）。真卡70筆／全outfit／checkpoint通過19.34秒；metadata patch各類約36–38ms，由約56–74ms下降。
- R70進一步將MaterialEditor.TextureDictionary改用borrowed parser，直接建立每個PNG的shared owner，移除先全dict owned再複製PNG的額外大型buffer；新增duplicate id、non-PNG/missing id、scope、sampler property key與shared ownership測試，等待編譯。

- R70：borrowed MaterialEditor字典測試通過；appearance26項通過（8項installed測試明確ignored），真卡70筆與全outfit保存／獨立oracle通過16.54秒，owned FFI checkpoint/stale/跨thread/lifetime通過4.22秒。R71部署後重跑正式GUI七類70筆，並新增逐筆native_preparation分項，不能用native數字宣稱正式UI已達標。

- R71 module build成功66.05秒。正式GUI HeadTypesR8：70/70套用與保存驗證通過，59/70<300ms。超時中cold上眼線texture208.08ms、眼白345.23ms、鼻型285.92ms；另有大型metadata/parse成本波動（最高105/121ms）。已保留每筆native_preparation與獨立保存時間，不隱藏cold案例。
- R72計畫：patch區塊先借用original slice，只把改動區塊改為owned；計算block table後直接一次寫入預先保留的output，不再建立整份payload再複製。UnityFS range利用累積block starts二分搜尋，只走相交block；獨立eager oracle增加每個實際壓縮block邊界讀取與0-byte/end/overflow案例。runner保存native stdout/stderr，下一輪收集實際GUI cold來源分項。

- R72B 原生回歸五組全部執行通過：appearance26、真實texture range/catalog oracle1（含block跨界）、runtime七類70筆與全outfit/checkpoint1、owned FFI1、borrowed dictionary1。runtime測試總耗時232.40秒（前輪16.54秒），曾觀察測試等待而非忙算，最後自行完成；未將其誤判deadlock，也未停止其他使用者程序。整輪總時間不是每筆選取耗時證據。下一輪R73實際GUI加入selected snapshot/worker queue與原生source profile，定位冷來源長尾。尚未達完整驗收。

- R73 build83.54秒成功；HeadTypesR9真UI70/70套用與保存來源正確，50/70<300ms，平均278.405ms、min178.335ms、max587.522ms。queue約0.04–0.06ms，無worker排隊瓶頸；selected大卡snapshot約25–57ms。冷來源catalog744筆162ms、CMS2879筆216–231ms，上眼線直接素材包讀取110ms+lookup166ms。不能用平均或前輪較好數字宣稱通過。
- R75/R76計畫與實作：KKEx只借用MaterialEditor.TextureDictionary binary，其他欄位、plugin順序/重複keys、四bytes前綴與legacy String皆保留；原始字典只於每個PNG建立shared owner時複製。合成oracle涵蓋array/direct map/1-wrapper/data-wrapper及duplicate plugin，還原dict後整個Value完全一致，texturebindings與legacy完全一致（R75測試1項0.04s通過）。
- 冷Texture2D精確查找另建只讀m_Name的versioned TypeTree index，不先解出全包所有sampler與尺寸；查到才讀該張metadata。精確unique名前綴優先於fuzzy排名；duplicate names保留完整舊catalog排序fallback。完整catalog API仍保留原行為。增加實際bundle PNG/CS/sampler oracle，明確確認cold exact不建立全catalog，再部署GUI測量。尚待R76建置。

- R77 build78.64秒成功。HeadTypesR10 70/70套用及保存正確、66/70<300ms，平均204.198ms、最小133.935ms、最大558.339ms。仍失敗上眼線0/5、眼白5、鼻型7。snapshot複製已接近0；冷m_Name index仍要讀取分散的壓縮區塊（CMS2879筆182ms），不能宣稱完整達標。
- R78：參照Unity AssetBundle.LoadAsset使用正式m_Container→PPtr→Texture2D。用容器路徑建立index，拒絕外部PPtr、非Texture2D與歧義名稱，讀取所選物件header確認實際m_Name才採用；無法匹配維持versioned name scan／舊完整catalog fallback。增加所有原始貼圖identity/CS索引對照、cold named/fuzzy獨立eager oracle，以及原生測試擴充左右pupil/gradation共11類110筆，等待結果再啟用UE快路徑。
- 完整驗收產物：C槽剩餘約203GB，而3000次108MB保存可超過300GB。GUI sweep在每類十筆全部核對後保留首筆/末筆/首個失敗，其餘保留MD5、bytes、render CRC、來源驗證、timings；僅刪除本run新生成且驗證成功的明確owned GUID.png，路徑必須恰為Saved/AppearanceEditor，並排除當前工作卡與輸入卡。非遞迴，不清理任何既有使用者檔案，報告明確標示output_retained。
- 剩餘範圍不能漏：頭型/skin composed textures、hair與clothing/accessory geometry仍走完整交易。後續需把完整APNG保存與renderer重建分離，保留未變renderer/GPU buffers，採runtime來源與bounded預備資源；各類必須真正UI十筆測完，不能以head子集合或metadata-only替代300類驗收。

- R79原始GUI仍是每click後保存方式：70/70套用與保存，存在非固定多筆wall-time長尾，結果head-gui-r79-summary.json，未宣稱通過。檢查程序僅剩使用者GoldenCharacterMCP（不停止），free RAM89.3GB，不能歸因RAM不足。原生m_Container／原始所有Texture2D identity及named/fuzzy oracle通過16.34秒，11類110筆+各outfit保存通過54.80秒；新增pupil/gradation原生平均約596–636ms，texture compose為主要成本。
- R80 iris：byte→linear使用256格原公式float結果查表；pupil兩組顏色移至pixel loop外；保留bilinear運算順序及linear→sRGB公式。純iris transform使用已有64MB/128項LRU，key包含source PNG、optional gradient/highlight bytes與presence、所有用到的顏色／位置／blend／scale／source-linear參數，不依角色或outfit名稱。新增凍結舊CPU實作，檢查全部byte範圍的雙線性sample bit-exact，四種linear/layers組合512RT PNG逐byte相等，並比對cold/warm cache。等待回歸。
- R81 GUI驗收修正：嚴格先連續click十筆，再保存這十筆。每筆於真render fence+兩幀後capture完整metadata+immutable checkpoint；十筆時間量完才依序用正式writer寫出各自revision並verify來源，最後一筆另走正式MaterializeAppearance屏障。避免原harness在每兩個click中間寫100MB APNG而改變連續編輯負載，沒有排除click內準備工作，也沒有省略任何筆的保存驗證。snapshot/verify失敗立刻明確停止，剩餘不當作通過；bounded十筆快照驗證後釋放，仍採限定owned中間輸出清理。

## R82 重複色彩空間探測的整包 I/O

- face_base_color_space 改走既有依路徑／zip來源識別的 ParsedBundle cache，不再每次為確認色彩空間重讀整包與複製 Vec。仍逐byte核對實際 PNG，無法核對時保持原有 fallback，不猜測 gamma。
- Texture2D 完整目錄成功建立時，若名稱確定不存在，避免再 eager 解壓相同包；不完整目錄及 decode 失敗仍走 legacy fallback。負查詢僅在單個 immutable ParsedBundle 中 bounded 128 項，不跨來源沿用。
- 新增實際來源 oracle：face、iris、eye white、eyebrow，各比對原圖／垂直翻轉／不相符 PNG，包含 cold/warm cache，全部與凍結舊演算法一致。例 iris cold probe 247.525→1.301ms。這是查詢分項，不能當作 GUI 切換已達標。
- R82 native 編譯成功 81秒；appearance 26、iris bit-exact 1、實際 Texture2D oracle 1、色彩空間 oracle 1 已執行通過。十一類110筆及 FFI 生命周期正在驗證；正式 UI 與其餘類型仍未驗收。
- R82 七組 native 回歸全部通過；十一類110筆加全outfit／checkpoint通過33.54秒。pupil0/1平均278.264/283.625ms、gradation0/1平均270.860/287.011ms，最大仍479.966ms，不滿足嚴格<300ms。
- R83 module-only build成功73.56秒；大型真卡七項回歸全部執行通過。正式GUI連續十筆後逐revision保存的新harness正在執行，不以原生測試取代正式點選。
- R84後續根因：通用 named_png_from_bundle、zip PNG load、sampler lookup仍先整包read再做hash cache，導致相同高光／漸層每眼重讀。改用既有來源path／zip cached ParsedBundle，保持override先後、fuzzy排序與缺來源fallback；完整原始bundle oracle新增path/zip cold/warm及missing/fuzzy查詢逐byte比對。尚未編譯或部署，不影響正在執行的R83 DLL驗收。

## R83 GUI sequential batch verification

- HeadTypesR12 七類70筆已實際連續點選，70/70套用與獨立保存來源正確，69/70<300ms；平均188.556947ms、最小120.127ms、最大364.490502ms。唯一超時為上眼線第0筆，native準備304.976ms，其中texture_resolve269.461ms。不是全部達標。
- 每類已先連選十筆再核對十個 pinned revisions；正式第十筆保存屏障正確。保留14個首／末輸出（失敗是首筆），56個中間owned測試輸出已限定刪除，報告仍完整保留MD5、bytes、timings及驗證結果；保留檔存在性檢查通過。
- LeftEyeR83 pupil0／gradation0 兩類20筆套用／保存均通過，但20/20速度超標，第一pupil744ms，其餘約400–480ms。正式GUI的額外native texture preparation不能從總時間排除。R84先消除通用素材／sampler整包重讀，另加iris_inputs／iris_color_space／iris_bake分項釐清剩餘冷成本。RightEyeR83正在驗證。
- RightEyeR83 20/20套用及保存來源正確，0/20<300ms，平均443.403ms、最大671.886ms；左眼平均476.533ms、最大744.164ms。R83合計110筆全功能正確，僅69筆速度達標，完整結果head-gui-r83-summary.json。不可作完整驗收。
- R84 native編譯成功79秒；新增來源cache回歸（path／zip entry、同長度修改、mtime變更、parser eviction、來源刪除），並開啟iris分項profile。正在執行八組原生回歸，尚未部署。

## R84/R85 實際結果與 R86 通用來源所有權修正

- R84 八組原生回歸全部通過；110筆＋全outfit／checkpoint17.05秒。各類平均45–86ms、最大206.234ms。iris_inputs約5–7ms、冷color_space34ms／warm<1ms、冷bake約8ms，證實主要收益來自素材與sampler免重讀。
- R85 module-only build75.88秒成功。正式連續點選：七類70/70功能及保存、68/70<300ms，平均193.224ms／max372.012ms；左眼20/20功能及保存、18/20<300ms，平均241.602ms／max471.174ms；右眼20/20功能及保存、19/20<300ms，平均229.675ms／max515.960ms。合計110/110功能，105/110速度，仍未通過全類型驗收。
- R86 檢查指出 SharedBytes::from_vec 把整包Vec轉Arc<[u8]>會重新分配複製，改以Arc<Vec<u8>>保留既有immutable allocation。舊from_arc與data_arc相容API維持；需要Arc slice的legacy data_arc才按原有相容約定複製。新增allocation identity、range views與原owner釋放後存活驗證，並維持所有實際原始貼圖／range／來源oracle。
- 另修正path parser cache使用legacy sampled content key的漏洞：相同長度且修改位於未取樣位置，原本可能取回舊parser。pathcache改以canonical source與完整bytes identity，新增相同sample但內部byte變更回歸；mtime／刪除／eviction檢查仍保留。這是正確性修正，不以較弱來源判定交換速度。
- R86 原生編譯中，尚未測試或部署；其餘289類仍需通用局部geometry／skin material交易與完整順序GUI驗收。
- R86 八組原生回歸全部通過；新增三個來源／所有權單元測試通過，Unity擴展單元45 passed／0 failed／10明確ignored。真實110筆及保存17.53秒通過。原生cold pupil0 272.974ms仍偏高，正式GUI不能據此判定通過。
- R87 將path／zip來源key與raw sampled-byte key分成不同enum分支。每次實際讀入新來源都給單調owned generation，只有canonical path／entry／size／mtime索引可重用仍在bounded cache的parser。資料異動、檔案消失、eviction均重新讀取及產生新parser，不需要每次reload再hash整包；也不可能因sample bytes相同命中舊來源。R86的完整bytes hash實作由此取代，來源失效約束未減弱。搭配SharedBytes免複製後重新編譯回歸。

## R88 冷啟動與 R89 shared source initialization

- R87 八組回歸全部通過，native110筆17.00秒。R88 module-only build74.85秒成功。冷啟動直接進pupil0／gradation0正式UI20/20套用與保存正確，19/20<300ms，第一次pupil仍438.013ms；其餘約188–275ms。SharedBytes免複製使bundle_parse本身由111MB約25ms→0.369ms，但第一次讀包及目錄準備仍在click內，不能宣稱通過。
- R89 runtime Context 初始化時，在其穩定source worker建立全部vanilla head texture families的只讀目錄（由完整catalog分類406..414取得並去重，不依角色ID、前十個項目或測試旗標），包括相同source的override provider及共用face create來源。僅建立Texture2D名稱／sampler／CS目錄，不先產生任何角色PNG或選擇結果；點選仍做實際source解碼、compose及commit。來源path／entry／mtime失效規則不變。
- 初始化成本輸出APPEARANCE_SOURCE_READY，families/providers/initialization_ms與character_prebakes=0；正式編輯面板啟用必須等Context ready，不將這個成本隱藏成點選已完成。角色APNG與共用資源以外不依賴預先特化檔案。此版本尚待編譯、完整來源回歸與正式UI冷啟動量測。

## 後續通用 geometry 交易工作（尚未實作完成）

1. 先以既有exact single-slot extractor處理200個accessory selectors：分離native prepare與APNG pack/write，candidate持有新增renderer資源、移除的validated part_origin、保留renderer索引映射及immutable checkpoint。不可靠mesh名稱猜刪除對象。
2. UE將單一renderer建立流程抽出，新的GPU buffers／runtime material bindings準備完成後一次publish；未改body／head／hair／clothes components與動畫／solver狀態維持。保留舊renderer直到候選完整驗證成功，失敗不更新選取或live session。
3. 舊APNG缺exact accessory_context／provenance時，要由原始來源及APNG中的rig／shape／material inputs建立runtime來源context，不能把舊卡永久留在210秒全角色重建fallback。原始來源識別與shape依賴不符必須明確重建或報錯，不猜資料。
4. 將同一交易擴充到cloth／hair／head geometry與skin/composed素材；來源program相同的情況更新runtime parameters／texture bindings，避免shader重新編譯。Head rig變更要另外驗證保留parts的骨骼映射，不可以只換外觀mesh。
5. 所有300個target依registry/UI順序各十筆正式測完；包括每筆display、保存來源與<300ms，任何失敗仍列未完成。當前十一類的成功不能替代剩餘289類。

## R90 實際冷啟動與 R91 下一步

- R89 八組原生回歸通過；shared source runtime初始化284.315ms、9 families／14 providers，character_prebakes=0。R90 module-only build成功73.20秒，未修改引擎。
- 正式冷啟動LeftEyeR90：20/20套用及各revision保存來源正確，19/20<300ms。首次pupil244.626ms，第二筆375.376ms仍失敗（metadata parse119.477ms、texture resolve191.591ms），不可宣稱全部通過。保留失敗樣本與全部timings。
- R91 避免跨outfit命中bake cache前仍反覆resolve／deserialize相同material definition，以及命中後重複clone兩次mesh；key包含immutable material resource ID及完整residue material overrides，cache僅存活單次prepare，不跨來源／revision套用。
- 飾品新增純runtime prepare_accessory_snapshot：由exact context／provenance提取單slot，持有新parts、候選snapshot、保留舊索引映射及新增連續range；不pack、不寫APNG、不publish。正式writer使用相同準備函式，保存步驟在後。新增映射、移除／空候選與prepared對正式保存結果逐byte對照。尚待編譯驗證及UE局部renderer接線；不能當作飾品已達標。
- R90 HeadTypesR14 70/70套用與保存，67/70<300ms。失敗眉毛302.018ms、眼白310.907ms／1209.753ms；1.21秒案例parse654.367ms、texture487.426ms，不能直接推定都是演算法忙算。R91加入來源執行緒GetThreadTimes CPU分項，與wall-time並列；從不扣除等待或改寫<300ms判定。
- R90 RightEye20/20功能與保存、20/20<300ms，平均219.939ms、max271.084ms；十一類合計110/110功能、106/110速度，完整結果head-gui-r90-summary.json，仍不足全300類驗收。
- R91另將direct_catalog_png的mod zip分支改用archive／entry／mtime source cache，保持exact名字與loose PNG優先順序，避免該殘餘路徑重讀整包或沿用sampled bytes舊parser。
- R92固定來源重建成功69秒，唯一exe SHA256 46B86A2BCF73B6532024F9737285C9F6AC787B2E04E762A4E511C2C2A2D7BBFC。八組原生回歸全部通過，十一類110筆及全outfit／checkpoint25.22秒；新增同名renderer／provenance／保留索引映射、空新增及原全局狀態回歸1項通過3.99秒。exact single-slot對正式保存結果的原始資源整合測試正在執行，尚未部署UE。
- R92原始source單slot整合測試1項通過35.69秒，包含runtime prepare無寫檔、候選與正式保存內容一致、add/reset／legacy明確fallback、其他6個outfits不變；正式保存交易691.002ms，這仍不是<300ms，pending UE局部publish。
- UE renderer準備迴圈抽成BuildSessionPartRange，明確接受Source session及FPartBuildOutput輸出集合。既有完整載入仍使用同一builder；移出的迴圈只重導向來源／輸出identifiers，保留native shader／eye／morph／skinning／outline行為，range guard拒絕非法範圍。尚待module build與實際載入驗證；候選隱藏及原子publish尚未接線，不能當作飾品快路徑完成。
- R95八組原生回歸全部通過，跨outfit準備一次／distinct source program的獨立JSON斷言通過；十一類native平均40–80ms、max127.445ms，pupil約69–70ms，material resolves每次1–2，恢復跨outfit重用。native exe SHA256 4A8780EAEEEDBF0057332AB71AD1709E5545781C04DB7FF8B9D2A09994F3E202。正式UE尚未驗證，R96 module-only build進行中。
- 目前catalog availability盤點39384筆／53個categories，各category均至少十項（catalog-category-inventory-r96.json）。這僅確認候選數量，不代表可套用、速度或3000次正式UI驗收通過。
- 局部geometry publish仍需同步native evaluator的part索引、clothing/accessory dynamics、UE morph slots／component tags與私有骨骼bindings。不能只替換parts陣列或暫時換live Session來建renderer；新骨骼缺相容mapping時不得發布候選。
- BuildSessionPartRange目前只完成來源／輸出明確分離，未完成staging。後續必須：candidate不得重用live BodyMesh／HeadMesh；新component在Register前隱藏並停tick，保留原始visibility供commit還原；資源集合與rooted textures持有候選ownership，失敗只銷毀候選；native part map／private skeleton相容與renderer完整性先驗證，再一次publish；不暫時交換actor.Session，不先銷毀舊renderer。
- R96共用header影響38個actions，module-only build成功322.01秒；R97只修正新增C4457變數遮蔽，3 actions成功23.63秒。未修改引擎來源。部署後啟動LeftEyeR97正式冷啟動20筆，收集wall／來源thread CPU與material_resolves，尚無結果。
- LeftEyeR97正式20筆：功能／保存全通過，17/20<300ms；平均270.494ms、max413.878ms。首次398.857ms，另332.669／413.878ms。來源計數每筆2，快取沒有退步。失敗第2筆parse wall180.636ms／thread CPU15.625ms，texture wall144.687ms／CPU156.25ms（Windows thread CPU有15.625ms取樣粒度，短區間可跨quantum）。等待不扣除，不修改驗收門檻。
- 啟動同DLL、同卡、相同20筆的R98NoProfile對照，僅取消詳細APPEARANCE_PROFILE log；完整per-click timings／CPU／render fence與保存驗證仍保留，用來判定熱路徑逐項stderr輸出的干擾。尚無結果。

## R98 對照與 R99 MaterialEditor 資源所有權

- R98NoProfile相同DLL／同卡20筆功能與保存皆正確、18/20<300ms；首次552.227ms（texture wall370.545ms／CPU93.75ms）、第二筆326.938ms（parse wall139.825ms／CPU31.25ms）。關閉詳細log有降低後續耗時，但不是完整根因，不能據此宣稱通過。
- R99以每個runtime editing Context擁有的bounded PngDictionaryCache重用MaterialEditor PNG owners。cache identity逐byte核對整份dictionary，不用sampled hash；bindings與其他plugins仍每次解析，dictionary改動就重建，duplicate-ID最後有效PNG與legacy String語意保持。
- 預算包含dictionary bytes＋實際owned PNG bytes，128MiB／8項LRU，超限正常解析但不留cache。Context runtime初始化先建立owners，成本獨立輸出APPEARANCE_DICTIONARY_READY；之後head選取僅重建binding map，保持所有保存metadata原樣。
- 新增每筆dictionary hit／miss telemetry，實際110筆需確認選取沒有變更dictionary仍重用；待cache生命周期／異動／LRU回歸及正式GUI驗收。其餘289類尚未完成，不當作全部完成。

- R99c fixed fixture compile; all nine native regression groups passed, including dictionary ownership/invalidation/LRU, installed-source oracles, 110 runtime selections/all-outfits/checkpoint (21.14s), and owned FFI lifecycle (4.87s). Actual GUI remains unverified; building module-only R100 for production measurement.

- R100 module-only build succeeded62.23s; seven large-card UE regression cases passed (each expected test state=Success, no failed/notRun). Production LeftEyeR10020/20 actual UI/render/save and <300ms passed; first pupil241.595ms, max272.207ms (gradation first). Runtime dictionary retained77,108,261bytes/init148.102ms; shared source directories456.813ms. Diagnostic subset only, full acceptance remains incomplete.

- HeadTypesR15 production UI70/70 display/save successful,69/70 <300ms. Upper eyeline item1 failed377.089ms: native334.278ms, card parse275.626ms/thread CPU15.625ms, dictionaryhit1/miss0. All other69 passed; still failure under strict wall-time. Evidence head-r100-summary.json. No waits subtracted and no rerun used to erase failure.

- RightEyeR100 actualUI20/20 display/save/<300ms passed, average154.375ms/max238.765ms. R100 total110 functional/save,109 speed passes, one upper-eyeline377.089ms retained. R101 hoists per-texture OK_ME_TEX_DIAG environment lookup out of binding loop (404 entries here), makes informational card parse counts opt-in through OK_DIAG/APPEARANCE_PROFILE, and adds JSON-only per-block decode/apply/drop timings for cached runtime parser. This removes synchronous routine stderr from production parsing and exposes stalls without adding hot-path log I/O; not yet evidence the 377ms stall is fixed.

- R101 nine native regression groups passed, runtime110/all-outfit checkpoint20.15s and ownedFFI4.63s. Building R102 module-only; no shader/engine source edits. JSON card_parse_phases reports each decoded block as [decode_ms,apply_ms,release_ms] without synchronous stderr.

- R102 module build58.96s succeeded. HeadTypesR16 UI70/70 display/save,69/70 speed; eyebrow item1 failed321.685ms with texture249.519ms/CPU31.25ms, parse12.072ms (KKEx decode0.404/apply10.993/release0.199ms). Original upper-eyeline failure no longer reproduced, but a new source-phase wait remains and is retained. R103 adds thread-local transaction timing capture with nested/panic-safe ownership and JSON stage totals/max/calls: bundle identity/read, loose PNG override, texture decode, flip, tint, diagnostics, native eye/detail resolver. No disk/log I/O in timing collection; diagnostics are measured to distinguish blocking stderr from data preparation.

- R103 native nine groups passed (27 regular appearance tests including nested/panic/thread capture ownership). R104 additionally waits for requested screenshot completion before the next audit click/save, verifies its file and records audit_screenshot_ms; timing starts at each real button callback as before. This corrects visual evidence ownership and test-only capture overlap, not a production warmup or relaxed deadline. Comparing R104 structured stages and all original failed rows will determine whether capture I/O was responsible.

- R105 native foundation for legacy APNG runtime upgrade: extract_session_from_info accepts owned, current parsed CardInfo and executes the exact existing source pipeline, without temporary PNG or SQL filename cache. The path wrapper still validates/parses before any output work and delegates to the same implementation. Accessory context/provenance emission depends on card metadata, no longer on a source filename. Expanded installed single-slot regression compares source renderers, geometry/UV/skin/material inputs and original context assets against the established path source, then exercises runtime prepare and formal save. This entry point is not yet wired to legacy context upgrade or UE partial publish; no claim of accessory deadline success. Do not compile until the R104 GUI performance run completes.

- R104 HeadTypesR17 70/70 display/save,69/70 speed; mean114.105ms,max462.919ms. Definitive stall: source_stages detail.log356.780ms, while bundle0.393/override0.159/texture6.248/flip1.221/tint1.339ms. Screenshot capture guard correctly saved all14 images; no screenshot wait overlaps the next click. R105 makes the three normal head-source success logs opt-in (OK_DIAG/APPEARANCE_PROFILE), retaining ERROR messages and structured no-I/O timings. Earlier blanket CPU/wall inferences are superseded by this measured log-write cause. Runtime extraction-from-info foundation is included, not yet wired to geometry updates.

- R107 installed single-slot integration passed 36.60s: fresh path extraction and filename-free parsed metadata extraction agree exactly on renderer geometry, bind poses, UVs, skin, material inputs and original accessory context; runtime preparation and formal saved result match, other six outfits unchanged. The previous SQL-cache baseline mismatch is isolated from this input-path equivalence contract; legacy cache compatibility still needs its own investigation. Single-slot formal export625.380ms remains over300ms and is not a GUI pass. R105 nine native groups already passed; deploying the measured head log-blocking fix next, then rerunning all110 actual UI head choices.

## R108 部署與下一段具體接線順序

- Module-only build succeeded62.13s; deployed R105 production log opt-in fix and filename-free source foundation. No engine source modification. HeadTypesR18 actual UI70 selection audit running; then left/right eye20 each under same binary hashes. Native/metadata success does not substitute UI acceptance.
- Geometry next: separate per-part reference-space export from global skeleton creation, so a slot candidate bakes only its new renderers against the unchanged live rig. Validate the entire retained-index map and all per-part arrays before native mutation; retain existing SessionPart/runtime dynamics and existing global bone identities. Remap export arrays, UE SessionIndex, mouth/morph references and component tags together.
- Legacy APNG upgrade must not infer accessory slots from mesh names. For metadata with recoverable installed sources, filename-free full extraction can supply exact accessory context and source-property provenance. A legacy upgrade can replace the whole accessory set once (identified by serialized roles), retaining body/head/hair/clothes; all subsequent selections use exact-slot deltas. Prepare this initialization before enabling selectors, and publish its matching renderers/checkpoint together. This is a design awaiting implementation and installed-asset parity validation, not a completed fast path.
- Do not invoke the full extraction pipeline concurrently without auditing global expression flags. Existing process-global eyes/mouth/brow extraction policy must be isolated or the initialization must execute at an explicitly exclusive initialization barrier. Background work cannot silently alter a live character's evaluator policy.
- Full registry-order3000 UI run remains the final acceptance condition. Current diagnostic subsets must retain complete=false and must not be combined into a purported full-order pass.

- R108 HeadTypesR18 actual UI70/70 display/save/<300ms; mean103.773637ms,max159.496501ms. LeftEyeR108 actual UI20/20 display/save/<300ms; mean127.699495ms,max179.698098ms. RightEyeR108 running. These are diagnostic subsets only; complete=false remains correct.
- R109 source work underway: share exact per-part reference-space bake math between complete export and new-renderer candidates; prepare_part_exports accepts only the requested new parts plus existing global palette. replace_part_exports validates all five array counts and the complete contiguous retained map before mutation, moves retained buffers (preserving allocation identity), and preserves the live global skeleton. Added meaningful invalid-map/no-partial-mutation, retained allocation, reset, and installed-source full-vs-part-local parity tests. Not compiled/deployed yet; do not run heavy tests during the R108 GUI performance audit. UE/native geometry transaction wiring remains pending.

- R108 RightEye actual UI20/20 display/save/<300ms; mean123.997280ms,max173.219997ms. Combined eleven head targets110/110 functional/save/speed, max179.698098ms. Canonical path/hash comparison confirms all three runs used identical DLLs; head-r108-summary.json retains full_acceptance=false and required_full_selections=3000. R104 failed evidence remains; fixed R108 results do not erase historical failures. Full3000 still pending.

- R109 reference export refactor compiled. Six regular skel-export tests passed (four installed ignored tests correctly remained ignored),76.48s; installed fresh-source/single-slot plus full-vs-part-local all-renderer export parity passed34.64s. Native nine-group followup running on immutable appearance-r109.exe. New exact-slot native publication code is subsequent R111 source, not included in those R109 results.
- R111 native replace_accessory_parts validates outfit/count, exact property removals, all retained indices, new-part property, prepared live rig and all export arrays before mutation; retained SessionPart/private dynamics and all global rig state stay owned by the original session. Installed regression now checks invalid publication leaves original buffers intact, valid publication keeps retained position/reference buffers and animation clock, and its new arrays match a complete export. Not yet compiled or connected to UE; no accessory GUI performance pass claim.

- R109 all nine native regression groups passed; unique executable SHA25692E18B2DF883684348B0BF8603ED5D1DC645DE3F9AD23C00CC88C885A4C0520D. R111 compile succeeded68s; part-map atomicity unit passed, installed native publication/integration passed36.17s. Retained native part/reference buffer identity and animation time are preserved; published reference arrays match a complete export. Still no UE geometry connection or deadline evidence.
- R113 adds Context geometry preparation, revision validation, rig-rest/shape/layout compatibility, deferred all-outfit checkpoint and exact-slot commit. New source/renderer session is owned by the candidate; raw metadata and snapshots change only after validated native publication. Resource resolution prioritizes candidate-owned unique assets over original APNG shared resources, so second and subsequent slot edits work without intermediate writes. Legacy missing provenance returns an explicit upgrade requirement; this is not the final legacy solution and is not yet exposed through UI. Expanded installed test covers prepare-without-publish/write, stale revision rejection, pinned checkpoint parity, six untouched outfits and reset. Await compilation/tests before any deployment.

- R113 compiled71s but installed Context integration failed39.35s at shape compatibility. No deployment. R115 shares APNG editable-shape restoration (sex/count/finite checks, coordinate-local ABMX, bust settings, apply_shape/skin_all) with geometry candidates before reference export; adds cached parser mode access to avoid reparsing owned PNG dictionaries for the display-mode CardInfo. Saved/source snapshots remain unchanged; candidate gets exactly the APNG display policy. Root and rejected alternative (dropping shape guards) recorded in CHARACTER_EDITOR_ERRORS.md. Await new regression.

- R115 compile succeeded74s; installed source/context/native publication/checkpoint/reset integration passed42.28s. Shared APNG shape normalization resolved the legitimate source/display mismatch while retaining all shape and rig guards. Nine native groups and installed all-outfit APNG integrity regression running before deployment. Formal snapshot export639.838ms is still not a <300ms GUI result; UE geometry staging/FFI and legacy upgrade remain required.

- R117 compiled and actual C001 APNG regression passed: 7/7 outfits, source unchanged, 1 test passed in4.37s. R115 nine-group summary is valid. Historical100-card reader integrity remains unavailable due missing output fixture; wrong zero-test filter is not a pass. Latest post-R117 source contains geometry FFI worker/type skeleton only, uncompiled and not deployed. User requested a handoff; complete state, evidence, pending wiring and commands are recorded in Docs/APPEARANCE_SELECTORS_HANDOFF_20261010.md. Full3000 UI acceptance remains incomplete.

## R119–R123 geometry C ABI、決定性序列化、legacy 對照

- R119 完成 additive geometry C ABI（`live_head_ffi.rs`）：`ok_appearance_prepare_geometry/geometry_free/geometry_info/geometry_card/geometry_stage/geometry_checkpoint/geometry_commit`。沿用 Context 穩定 source worker；stage 為 preview bytes roundtrip 的獨立 owned OkSession（新零件+global rig，無 source_property，只供 UE staging）；commit 先完整借用驗證（stale/shape/rig/map/provenance/export counts）後才 take，可預見失敗保留 candidate。`replace_accessory_parts` 拆出借用式 `validate_accessory_replacement`，`SkelExport::validate_part_replacement` 亦同；PreparedGeometry 的 per-part export 改存 `exports`，source 只留 global rig。
- R119/R120 installed single-slot integration 失敗於新 FFI checkpoint 比對：worker thread 準備的 accessory_model／material 資源 ID 與主執行緒不同，JSON 語意相同、bytes 長度相同。根因：`MeshMaterial.colors/floats/tex_wraps`、`Skeleton.tfs`、`AccessoryRuntime` HashSet、`DBoneSet.colliders`、`SessionBlob.body_keys/head_keys`、`Rig.links`、`HandShapes.keys`、`ListEntry.extra` 以 HashMap 迭代順序序列化，content-addressed ID 依 hash seed／cache 來源而變。R122 新增 `ordered.rs` serialize_with（排序，wire format 不變、舊資料可讀），單元測試驗證 seed 無關與相容。
- R122：ordered unit、FFI null/invalid、single-slot+geometry C ABI lifecycle（added=2 retained=34；stage parity：幾何/skin/bind/材質/rig/reference export 全等，validation 不消耗、shape 改變拒絕保留、stale 拒絕保留、checkpoint 脫離 context 後輸出與準備 snapshot 一致）、C001 七 outfit APNG、owned head FFI 全通過；NativeR122b 九組回歸全通過（appearance 27）。`run_appearance_native_regressions.ps1` 在 Windows PowerShell 5.1 因 native stderr 重導成 terminating error（NativeR122 目錄保留失敗），改為該呼叫暫用 Continue、仍以 exit code+明確 test count 判定。
- UE：`FOkAppearanceGeometryCandidate` owned wrapper、`FOkAppearanceContext::PrepareGeometry/StageGeometry/Capture/CommitGeometry`；`BuildSessionPartRange` 新增 stage mode（唯一 `Stage_` 名稱、不 RegisterComponent、session index 加 added_range.start、socket attach 延後、不重用 BodyMesh/HeadMesh）；`AKKPreviewActor::ApplyAccessoryGeometryCandidate` 驗證→stage clone→未註冊建置→native commit→移除槽位元件/重映射 retained index、tag、mouth slot、morph weight、vagina morph→註冊、socket、pose、InitAnim。新 rigid neutral socket 需求直接拒絕走完整路徑（避免動畫中重抓 neutral）。UI 經 `OK_APPEARANCE_GEOMETRY=1` gate，未驗證實畫面前不預設啟用。單檔編譯檢查未出現新錯誤（既有 unity include 依賴錯誤除外），完整 module link 待執行。
- R123 legacy 對照：C001 outfit0 filename-free 來源萃取 63.0s，57 parts vs 儲存 55 parts；含 body 在內沒有任何 renderer 資源 byte-exact 相同（C001 由不同 pipeline 狀態輸出）。故無法以 parity 為舊零件補 provenance；可行方案為初始化階段以 context+逐槽 extract_slot 一次替換整組 legacy accessory，之後 exact-slot delta。來源萃取 63s 仍需 context-only 早退／init barrier。

## R124–R132 飾品 exact-slot 幾何實際 UI 打通與加速

- 實際 UI 診斷（registry index 113 = accessory1_124，十個不同項目，`OK_APPEARANCE_GEOMETRY=1`，fixture `Saved/Tests/AppearanceSelectors20261010/fixtures/editor-geometry-r126.png`）：
  - R123/R124：全部 fallback；原因 `extraction dependencies changed`。新增 `Context::mismatch` 診斷（只在失敗路徑）定位為 env_sig：editor 執行期間反覆切換 `OK_UE_ANIM`/`OK_HEAD_GPUSKIN` 等 process 環境，Context worker 讀到的值與 export 時不同。
  - R126：新增 `profile.rs` thread-scoped 環境覆寫（同 `request_clothes_states` 模式），`prepare_geometry` 以 portable export profile（五個開關=1、clothes [0;9]）執行，不再 set_var 全域環境；7/10 走幾何路徑，UE publish 19–59ms；3 筆因新 rigid socket 無 neutral 而 fallback。
  - R127：load 時同一時點記錄 leader 全骨 neutral（`RigidNeutralAllBones`），新飾品沿用不重抓；10/10 幾何路徑，530–820ms。
  - R128：`references::begin_load` 每次全樹 stat（~220ms）改由 source thread 上的目錄變更通知（catalog_watch）把關，未變更才沿用 epoch；262–465ms。
  - R130：Context 快取「已套顯示 shape 的 globals」template（key=無 parts globals bytes+全部顯示 shape 輸入 SHA256），hit/miss 都由同一 bytes 解碼；插入 parts 後重放 shape policy 的 renderer 尾段（reset accessories→update(0)→skin_all）。測試證明 miss/hit/未快取三者 renderer、export、rig 完全相同（第一版漏重放 dynamic accessory，差 9.7cm，已由 parity 測試擋下）。200–429ms，8/10<300。
  - R132：同一 slot 的 mesh/color/assignment 三次重取 bundle bytes，color 另做完整解壓解析。改為 thread-local 4 筆 bundle source memo（隨 entry memo / source epoch 清除）+ `ParsedBundle::accessory_color_use`（與原 byte 版共用 `accessory_color_use_in`）。80 個已安裝 vanilla/mod 飾品新舊結果逐欄相同。native prepare 68–213ms；UI 137–385ms，平均 228.6ms，8/10<300，10/10 顯示／保存正確。未過：item0 384.7ms（首次 template miss+冷 bundle），item2 310.9ms。
- 仍待：首次點擊冷成本、push_meshes/meshes 尖峰；C001 legacy provenance 升級（R123 證明不能 parity 補標）；`OK_APPEARANCE_GEOMETRY` gate 待 C001 可用與速度達標後移除；其餘 289 個 target 與 3000 次完整依序驗收。
