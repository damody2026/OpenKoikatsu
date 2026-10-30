# 街區 1 對 1：照原作 H 場景改造計畫（2026-10-30，E 線 e-galgame）

使用者要求（2026-10-30）：
- 1 對 1 互動照原作新增五種經驗，互動結果寫進女主記憶。
- 原作 H 場景的互動功能全部做進去：當場換衣服、換地點、換姿勢、切速度，每個姿勢有不同的狀態、表情、對話。
- 對話用 TTS；沒有台詞的 H 短語音（喘息、短反應）用原作 H 語音。
- 左側是測試用 UI（Gallery 的 `UOKCharacterController` 面板），要改成像原作那樣專門的 1 對 1 互動 UI。
- 參考 `D:\KoikatuSunshine\kks_decomp\Assembly-CSharp`（`HSceneProc.cs`、`HFlag.cs`、`HSprite.cs`、`HVoiceCtrl.cs`、`HandCtrl.cs`、`SaveData\Heroine.cs`）。

## 1. 原作要點（decomp 行號）

### 經驗
- `Heroine.HExperience`（`Heroine.cs:277`）：hCount==0 → 初めて；`hAreaExps[1..5]` 任一 <100（[2] 加 countKokanH、[3] 加 countAnalH）→ 不慣れ；lewdness==100 → 淫乱；其他 → 慣れ。
- 五個部位經驗 `hAreaExps[1..5]`：胸、秘部、アナル、尻、乳首（[0] 是接吻）。愛撫部位對應 `HAibu.GetAreaExperience`：mouth→0、胸（手）→1、秘部→2、肛門→3、臀→4、胸（非手道具）→5。
- 場景中累計 `HFlag.count.selectAreas[6]`；結束 `EndProc`（`HSceneProc.cs:3220-3378`）：AddHCount、破處／後庭初次、isKiss、countNamaInsert、countHoushi、`hAreaExps[0] += selectAreas[0]`。
- `CalcParameter`（`:3440-3595`）＋結果畫面（`HSceneResult.cs:526-559`）：`hAreaExps[1..5] += selectAreas[1..5]*0.1`；奉仕經驗每次射精 15/10/5/1；秘部／肛門 `countKokanH`/`countAnalH`：有插入 5，每次高潮再 15/10/5/1；好感與淫亂度依結束類型隨機加。
- 經驗影響：姿勢門檻（`isExperience` 需慣れ、`stateRestriction`）、生插入許可（`NamaInsertCheck`）、肛門許可機率 = hAreaExps[3]+countAnalH、語音欄位（四級各一欄，未經驗部位強制第 0 欄）、眼神／視線、結束劇情、拒絕（速度 ≥70% 且部位經驗 0 且 denial.aibu 為 false）。

### 模式與狀態
- 愛撫 aibu：Idle、M_/A_/S_Touch→Idle、K_Touch→K_Loop、Dislikes、Orgasm_Start→Loop→A。
- 奉仕 houshi：Idle、WLoop↔SLoop、OLoop（男 ≥70）、IN_Start→IN_Loop→Oral_Idle→Drink/Vomit、M_OUT_Start→OUT_A。子分類：手、口、胸。
- 插入 sonyu（肛門加 `A_`）：Idle→Insert→InsertIdle→WLoop↔SLoop→OLoop→M_IN/M_OUT/WF_/SF_/WS_/SS_ 開始→IN_A/OUT_A/Pull→Drop。保險套切換。
- 兩條量表（女／男 0–100，可鎖），速度（點擊／滾輪加速、自然衰減），自動（おまかせ）。

### UI（`HSprite`）
- 右側：女／男量表、速度條、動作泡泡（插入／肛門／內外／喝／吐／再來）。
- 動作選單：愛撫／奉仕（手、口、胸）／插入，各姿勢 New 標記、需換地點標記。
- 主選單：女方服裝（各部位狀態輪替、全穿／半脫／全脫、飾品、換裝）、男方服裝、燈光、移動軸、換地點、結束。

### 語音
- 喘息 breath（依狀態、量表、速度、是否觸摸）、短喘 shortbreath（觸摸部位）、台詞 voice（動作碼 0xx/1xx/2xx/3xx）。
- 本專案：台詞改由 TTS 產生（AI 依狀態寫一句），喘息與短喘用原作 H 語音（`KKSpeech::CollectHBreath`）。

## 2. 現有可用部分（E 線）
- `AKKPairedPlayback`：雙人共用時鐘、`SetSpeed` 0–4、狀態切換（重新 Start，無混合）。
- `h_playback.rs` `Pair`：id、category（`AnimationInfo_00..07`）、name、layers、sources；目前只露出 `_01`/`_02`。
- `KKSpeech::CollectHBreath` / `AKKPreviewActor::BeginHBreath`：原作喘息＋嘴型；條件固定為初次欄位。
- 衣服：9 部位 × 0–3 狀態（`ClothesStates`），全脫即時，其他會整個重載。
- 觸碰：`FOKBodyInteraction` 只有 7 個粗區域。
- 結果：`result.json` 的 `actions`、`FinishPrivate` 寫記憶、`intimacy_after` 固定加值。

## 3. 實作順序
1. **五種經驗規則**（`Tools/ok-rules/src/hexp.rs`）：`h_session_end` 依本次統計更新 `ext.hexp.<女主>`，回傳經驗等級前後、記憶事件（初次、破處、部位熟練、等級變化）、淫亂度與好感；`h_experience` 查目前等級與門檻（生插入、肛門機率、拒絕）。所有進入 1 對 1 的路徑結束時都呼叫。
2. **場景統計**：`UOKCharacterController` 記錄模式、姿勢、狀態轉換、速度、觸碰部位秒數、高潮／射精、插入（秘部／肛門／生）、接吻，寫進 `result.json` 的 `h_counts`。
3. **專用 1 對 1 UI**（取代左側測試面板）：右側量表與速度、動作選單（愛撫／奉仕三類／插入，依經驗門檻篩選）、狀態按鈕（插入、肛門、內／外、喝／吐、再來、拔出）、服裝選單（部位輪替、全穿／半脫／全脫、換裝）、換地點（同房間的位置／回街上選房間）、結束。
4. **狀態機**：依模式自動推進（量表、速度、點擊），取代手選狀態下拉。
5. **表情與視線**：依狀態、量表、經驗套表情（眼、眉、嘴、紅暈）與看向。
6. **語音**：喘息依狀態／量表／速度／經驗欄位；台詞事件（開始、換姿勢、插入、高潮、結束）以 TTS 說出由 AI 依狀態寫的一句。
7. **記憶**：結束時把姿勢、初次、等級變化寫進她的記憶（MindObserve），讓之後的對話知道。
8. **測試**：不同女主、不同關係階段（朋友／戀人）、不同經驗等級逐一進入 1 對 1，各模式各姿勢跑一遍，判斷合理性並以通用解修正，記在 `Docs/STREET_ERRORS.md`。

## 4. 進度
- 2026-10-30：計畫建立。
- 2026-10-30：第 1 步完成：`Tools/ok-rules/src/hexp.rs`（`h_experience`、`h_session_end`，ext.hexp.<女主>，四級經驗、初次與部位熟練寫進記憶）；acts.toml 加 `h_first`/`h_area`/`h_level`。
- 2026-10-30：原作姿勢表 `Content/IndependentResources/StreetSocialData/h_poses.json`（`ok-hposes D:/KoikatuSunshine <輸出>`，140 個姿勢的模式、奉仕種類、肛門、經驗門檻、服裝）。
- 2026-10-30：第 2、3、4 步第一版：`Source/OpenKoikatsu/OKHScene.h/.cpp`（`UOKHScenePanel`）。街區進 1 對 1 時（`AOKInteractionMode::BeginHScene`）用它取代左側測試面板（`ok.HScene.TestPanel 1` 可叫回）。奉仕手／口／胸、插入分頁列出姿勢（需要「習慣了」的姿勢鎖住），奉仕與插入的原作狀態流程、兩條量表、速度、強弱、射在裡面／外面／嘴裡、吞下／吐出、保險套（她不准生插入時要你戴）、後庭（每場問一次，機率照經驗）、服裝部位切換；結束時 `h` 結果交給 `FinishPrivate` 呼叫 `h_session_end`。尚未：愛撫（部位觸碰經驗）、表情、TTS 台詞、換地點。
