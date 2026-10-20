# 身體開合控制與來源驗證（Stage347）

## 範圍與來源

使用實際安裝的三張卡與模型來源，不以卡名選擇修補分支。所有私密部位驗證均為骨骼／頂點數值，沒有產生截圖。

- KK_226176：SAC.innie2，9080 頂點，來源包含 `vagina.open` blend shape。
- Alice_keiyuu：SAC.clit，8899 頂點，來源包含同名 blend shape。
- 菲比 v1.1：BP.profundis，9823 頂點，32 個相關骨骼、13 個普通 DynamicBone，沒有開合 blend shape。

原生 Studio 使用停用的獨立 SkinnedMeshRenderer 複本 BakeMesh，以 0、25、50、75、100、0% 六次控制量測，未修改原角色。兩個 SAC 來源最大位移分別為 0.009326805 與 0.0115425233 公尺；線性誤差最大 9.027586e-8 公尺，回到 0% 的復原誤差為 0。原生探針已撤出；runner 的 delayed quit 最後終止自有 Studio，exit -1 不當作遊戲正常結束。

## 共通修正

1. 替換身體載入後保留來源普通 DynamicBone。按 Transform 名稱重綁 root、component、exclusions、not-roll、collider PPtr；碰撞器使用隔離 ID，經現有 per-chain collider 通道保留，避免衣服載入流程清除身體原生參照。
2. Unreal 為所有含來源 morph 的部件建立可控制 renderer。批次 RegisterMorphTarget(false) 後呼叫 InitMorphTargets，建立引擎查找索引。
3. `GetPartMorphNames`／`SetPartMorphWeight` 使用部件索引與來源名稱。權重限 0–100%，非有限值、未知名稱及無效部件拒絕；actor 個別保存，卸載清除。
4. CPU morph delta 依當前 LBS palette 轉換，GPU 使用既有 reference-space morph 路徑。沒有卡別固定偏移或新增持久化 schema。
5. CPU morph 身體每次姿勢刷新都重建當前 base；有部件控制權重時，刷新後重新套用 morph，避免開合只在滑動權重時生效。

Blueprint 可直接呼叫上述函式。Console：`ok.SetPartMorph <part-index> <source-morph-name> <percent> [actor-name]`。必須先查部件索引與來源名稱，不能對所有身體假設相同 morph。

預設 renderer 仍使用 GPU；`OK_CPU_SKIN=1` 明確選用 CPU renderer，載入不再覆蓋該選擇。回歸必須斷言實際 Comp 類型。首輪標為 CPU 的程序實際被 Open 強制切成 GPU，已撤回，其日誌保留為 `UECPUFalsePositive.log`，不列入 CPU 證據。

## 驗證與界線

Rust 三項實際來源測試通過：13 個身體元件在 Transform ID 全部改變後仍保留；120 幀產生 3120 次相關骨骼變動且無非有限值；live/cache 皆保留 23 個身體加衣服元件；CPU 位移隨 90 度姿勢旋轉的最大誤差 2.0236257e-9 公尺。

Unreal 最終 CPU／GPU 結果以 `Saved/Tests/BodyArticulation347/Acceptance.json` 與對應實際程序日誌為準。GPU 檢查名稱查找、來源頂點 payload、morph batch/RHI 初始化、六個權重；CPU 檢查實際 section 頂點位移與閉合復原。兩路徑均檢查第二個 actor 不受影響與卸載清理。

真正 CPU 最終量測 KK_226176 最大 0.932681433 公分、Alice 最大 1.154252291 公分，與原生 BakeMesh 最大位移對上（驗收容差 1e-6 公尺）。六個权重均在姿勢刷新後驗證，0% 完全復原；實際程序 exit0。先前 false-positive 的 0 公分結果不算此量測。

**BP 自動接觸開合仍未驗收。** 反編譯 `KKS_Studio_BetterPenetration/Core_BetterPenetration/CollisionAgent.cs` 包含接觸目標、碰撞器及拉骨控制；普通 DynamicBone 的次級運動不是其完整替代。OpenKoikatsu 尚無該插件的完整互動驅動。這次補回物理元件不能宣稱所有 BP 身體或雙人動作都可自動開合，也不認證完整私密部位像素／outline parity。

後續驗收必須包含來源 `Initialize` 的目標／骨骼解析、`ResetKokanParticles`／`EnableKokanParticles` 的狀態切換、`PullKokanBones` 與回復路徑、配對角色之實際碰撞器綁定，以及原生對照的接觸／離開數值曲線。不能以只移動 root 的現有 120 幀測試替代。也不能將 SAC 的單一線性 morph 強套到無此 morph 的 BP 模型。

版本：CACHE108、SESSION238、CLI348。先前 Stage346 SHA 封存屬歷史版本，不能拿來驗收本次修改。
