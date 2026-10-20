# Stage363：保存骨架的非根骨旋轉與權重公式

本輪沒有發現匯入器產品缺陷，只新增通用驗證。最新資產仍為 Stage361AllR1，沒有卡別修正、資產覆寫或重封歷史 Audit。

Stage362 的根骨平移不能證明非根骨旋轉及混合權重正確。本輪 `OpenKoikatsu.ImportedAssets.JointUsage` 在無卡、資源 pack、KKUnityBundle 解析器的隔離專案，對每個 Skeletal Mesh 從實際 LOD0 的 section BoneMap 和量化權重選取總權重最大的非根骨，不以角色、部位或圖片指定骨頭。根骨唯一有權重的模型另列 root_only_models，不能假稱測過非根骨。

對選中骨頭在 ComponentSpace 的 Z 軸旋轉 0.35 radians。取得元件更新後的骨矩陣，以 Unreal 來源邏輯計算：每個骨頭的 inverse reference matrix × current component-space matrix，再以 section BoneMap 解析索引、weight/65535 加權頂點位置。此標量公式與實際 GetCPUSkinnedVertices SIMD 讀回比對。骨索引越界、非有限值、沒有移動或還原後不同均使測試失敗。計算使用保存的 Unreal render 資料，不是 KKS 原生 dump 的獨立 oracle。

浮點誤差預先固定為 `64 * FLT_EPSILON * (1 + sum(absmax(weighted_term)))` 公分，涵蓋標量與 SIMD 累加的捨入差異；沒有看到失敗後放寬門檻。還原骨頭後逐頂點位置要求精確等於 baseline。這不是位置原始 bit parity 驗收。

BuildR1 actual0，5.16 秒。DetachedJointR1 Automation Success／actual0：99 個模型、root_only_models=0、130,797 個 render 頂點，最大位置差 0.0000457764 公分，旋轉有位移且還原一致。沒有新的编譯或產品錯誤。結果及來源、binary、log、1,667 個原資產／隔離副本雜湊保存於 `Saved/Tests/AssetImport363/Audit.json`。

```powershell
./Tools/run_joint_assets363.ps1 -Tag MyFreshTag
```

沿用過期 DLL 檢查、無其他 Editor、新 evidence tag、actual exit／Automation Success 雙重 gate。元件讀回仍為 CPU；未涵蓋所有骨頭／旋轉軸、GPU pixels、完整動畫 controller、私有 rig、原始任意 KKS 卡、完整應用 Shipping 或 Stage355 的 279 個未通過案例。產品 Actor 仍使用 Static snapshot，沒有以本測試冒稱新增完整動態角色功能。
