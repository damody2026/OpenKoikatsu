# 一對一GPU塗層與IK共用資料驗證（2026-10-07）

## 範圍與狀態

Tests清理完成後繼續場景優化。GPU塗層預設仍為0，原CPU路徑保留；runtime IK不可變setup共用已接入正常雙人播放。全部互動120FPS與乾動畫<=6ms尚未完成。

使用APNG角色與共用資源，不需角色特化離線資產。C001測試卡改用`Content/CharacterCards/CrowdAPNG/C001.png`，與原output卡manifest SHA256完全相同：`6ac5a2390a45127ae50ebdc2ced8e99ce0475f764951834405ce31db95dfd5ea`。男角仍為`Saved/TestCards/InteractionMaleAPNGR65.png`。

## 實作

- RuntimeCoating kernel讀取已發布的當幀GPU位置，使用runtime頂點與face-edge bindings，寫入原FOutput並經pack/copy發布到mutable mesh streams。不在正常路徑GPU readback。
- 固定offset與smooth normals保留既有geometry更新頻率；mask／film／BVH與附著水滴物理頻率不變。
- 多surface合併RDG，來源private GPU frame先flush。來源epoch、目的epoch、generation、當幀來源、buffer用途／大小、finite affine及bindings皆驗證。
- 延後batch flush再次驗證weak component、epoch與proxy pointer。來源未發布是Retryable而非永久拒絕，原CPU輸出保留，8幀後可重試；硬拒絕在身份改變後恢復。
- GpuCoatingSparseGather=1：完成GPU readiness／epoch／receipt檢查後才決定是否完整收集wet vertices。GPU成功只收集附著水滴頂點；fallback沿用原TSet次序與first-face位置，不改數值結果。0保留完整收集再丟棄oracle。
- IK setup cache以immutable Arc source snapshot identity及motion/state精確比對、最多16項，持有source避免地址重用。每幀原資源revision与角色狀態驗證保留；solver只clone自己修改的MotionIkData，config與source不被修改。

## 驗證

- SharedSetupUnitR2：snapshot reuse／replacement／不同state／missing state／invalid binding／error／mutation isolation／eviction PASS。
- SharedSetupPairOracleR1：actual男女APNG兩姿勢64幀；oracle每幀重建setup、優化端共用setup。完整骨架、shaped world/rest、reference與私有particles exact PASS。
- SharedSetupRustBuildR1、SharedSetupEditorBuildR1、SparseGatherBuildR1成功。
- SparseGatherRegressionR1八項PASS：GpuCoatingProxy、GpuStreamProxy、GPU.RuntimeCoating、GPU.RuntimeDeform、Paint.Scene、MaskParity、Film、Speech.RenderedMouth。
- RuntimeCoating真GPU四幀129頂點readback：最大位置誤差1.0400565e-6cm，tangent7.78430948e-8；負／非均勻scale、shear、degenerate edge與invalid input驗證。
- SparseGatherEyesR1：functional_pass1/exit0/hash stable；五張近拍本人逐張確認中立、注視、閉眼、睜眼、動畫播放眼睛正常。external1，畫面stat FPS不作性能驗收。
- SparseGatherPairPaintR1：functional_pass1/exit0/hash stable，兩角色水槍與live head/mouth/belly通過；12次姿勢切換max-frame41.426ms，最高click-to-play19.467ms。兩角色當幀query位置最大誤差0.000006644cm。external1，只作功能證據。
- SparseGatherNavigationR1：functional_pass1/exit0/hash stable，editor_and_interaction_separate1。external1，只作功能證據。

## 性能證據

BatchAllControlsVisibleR1為先前批次版無trace隔離量測：exit0/external0/isolated1/hash stable，乾6.280296ms，男噴10.452025ms，男停射濕面10.748009ms。仍未達標。

SparseGatherAllControlsVisibleR1入口失敗：原output卡已不在舊路徑，未執行有效量測。R2卡hash精確相同、十階段完成，但後半段另Unreal程序出現，external1/isolated0；所有FPS數字不得當優化收益或正式驗收。

同binary CPU/GPU正式A/B仍待完成。完整trace根因與正式數據另見INTERACTION_CROWD_PROFILE_COMPARISON.md；錯誤見INTERACTION_SHOWCASE_ERRORS.md。

## 本輪收尾紀錄

最新同binary CPU基準`InteractionSparseGatherCpuOracleVisibleR1`：exit0/external0/isolated1/hash stable。十階段mean ms／Present FPS如下：

| 階段 | mean ms | Present FPS |
|---|---:|---:|
| 乾1x | 6.734965 | 148.361587 |
| 乾4x | 6.702134 | 149.116624 |
| camera | 6.879735 | 145.359643 |
| mouth | 6.942796 | 144.034094 |
| voice | 6.767896 | 147.671124 |
| belly | 7.553968 | 132.392124 |
| 女噴全控制 | 9.567583 | 104.501520 |
| 男噴全控制 | 11.231009 | 89.057669 |
| 男停射濕面全控制 | 11.906603 | 83.982083 |
| clear | 6.740484 | 148.364344 |

GPU版`SparseGatherAllControlsVisibleR3`完成但clear階段出現external UnrealEditor-Cmd，整段isolated0，不能與CPU基準宣告正式A/B收益。`R4`在入口即有另一遊戲及Cargo工作，已核對自己owned receipt／PID／start time／完整log commandline後中止，只停止自己的測試程序。功能驗證已完成，benchmark隔離連續被外部工作打破；沒有停止外部程序或修改其他任務。

GPU預設維持0；native IK setup共用已正常啟用，GPU sparse gather預設1只在明確启用GPU coating且來源已acknowledged時使用。所有新演算法功能與數值驗證通過，但全部120FPS目標尚未達成。下一項是取得無外部遊戲／編譯的GPU正式量測，然後依最新trace處理剩餘native dynamics／shape／display與濕面成本；不得因benchmark阻礙而標記目標完成。

## FrozenTransform同binary正式重驗與trace
CpuOracleVisibleR1／GpuAllControlsVisibleR1：exit0/external0/isolated1，前後與兩run的4DLL SHA256完全相同。CPU0乾6.395026ms、4x6.583707、女噴9.399699、男噴11.297596、男濕10.888418、clear9.138070。GPU1乾6.528177、4x6.340613、女噴9.356963、男噴10.962800、男濕10.906378、clear6.323396。乾<=6ms與全8ms/120FPS仍FAIL。
男濕CPU/GPU coating scope分別1.207527/.983899ms，但GPU charts3658/178912texels、CPU2598/124848，workload不同，不能斷言完整FPS收益。GPU request21898/published21885/hardreject0。預設GPU coating仍0。
FrozenTransformGpuControlsTraceR1已完整完成／external0/isolated1/hash stable。新all-thread event export含Dry499pairs998roles、Wet297pairs594roles、Clear490pairs980roles。乾PairAdvance3.99792／NativeContacts.50729／PrivateGpuPaletteBatch.42631ms；濕PairAdvance5.15116／LiveShape.77139／PaintTick2.27651／Coating.72357／SparseQuery.02457／NativeContacts.55996／PaletteBatch.41246；clear PairAdvance4.06606／PaintTick.00132。clear昂貴異常未在GPUtrace重現，不能歸因CPU塗層。新trace只是定位，不能取代formal無tracePresent驗收。資料Saved/Crowd/Profile-FrozenControls{Dry,Wet,Clear}R1-Root.json。
FrozenTransform位元oracle、actual APNG64幀／FinalIK九項／八項UE回歸（含54 mouths／2430steps）已PASS；其Contacts整scope沒有已證實收益。HeadInput與SharedBodyIndex候選未證實根因，已撤回，錯誤記錄SHOWCASE_ERRORS。下一步新增NativeContacts與Palette細分trace，再定位可消除的當幀重複工作，不減少physics／IK／wetmask／render品質。
