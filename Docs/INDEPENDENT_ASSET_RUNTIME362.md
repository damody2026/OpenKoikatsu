# Stage362：獨立骨架資源的元件使用驗證

本輪沒有發現需要修改的匯入器缺陷，新增通用驗證而非角色補償。最新資產仍為 `Content/IndependentImports/Stage361AllR1`：九套服裝、1,667 個資產，其中 99 個 Skeletal Mesh。原有匯入資產與 Stage361 Audit 不改寫。

`OpenKoikatsu.ImportedAssets.SkeletalUsage` 在只帶 KKImportedAssets runtime 插件與保存資產的隔離專案執行，不載入 KKUnityBundle 卡片解析器，不帶卡片或 IndependentResources pack。建立元件前直接核對所有骨架材質保存的 SkeletalMesh／MorphTargets usage flags，避免 Editor 自動修補掩蓋匯入問題。

99 個 USkeletalMeshComponent 註冊後讀取有效、有限的蒙皮頂點。對 72 個含非零 morph 的模型，依資產資料選取最大位置 delta 的目標，設定權重 1 後確認頂點移動，清除後逐頂點精確回復 baseline。另對 99 個 UPoseableMeshComponent 將根骨在 ComponentSpace 平移 10 公分，確認頂點移動、有限值與還原一致。這是根骨權重／元件更新的驗證，未測各肢體獨立旋轉、完整動畫控制器或每個 morph 的逐一播放；Stage361 另有全部保存 delta 比對。

使用 Unreal 的 SetMorphTarget、SetBoneTransformByName 與 GetCPUSkinnedVertices，沒有另寫蒙皮公式或卡名特例。即使啟動實際 RHI，GetCPUSkinnedVertices 會切換 CPU 蒙皮後讀回，因此本結果不證明 GPU morph buffer 或原作畫面像素一致。既有 AKKImportedCharacterActor 仍組裝 Static snapshot；本測試手動建立骨架元件，未新增完整動態角色 Actor。

```powershell
./Tools/run_skeletal_assets362.ps1 -Tag MyFreshTag
```

Runner 要求無其他 Editor、來源時間戳不得晚於部署 DLL、使用新資料夾與 log，並同時核對 actual exit 及 Automation Success。FinalR1 為加入 DLL 檢查後的最終執行；結果及 SHA256 見 `Saved/Tests/AssetImport362/Audit.json`。

事故保留：UsageRedR1 原先用於檢驗材質缺旗標假設，實際 Success／actual0，不能把名稱當紅案。BuildR2 actual6，TArray<TObjectPtr<UMorphTarget>> 不可用 auto* 迭代，改 const auto&／Get()；BuildR4 actual0（5.10 秒）。SkinR1 的 morph 回復與 SkinR2 的根骨回復均 Success／actual0。曾猜錯 Material.h 的目錄，實際在 Engine/Public/Materials；後續先 rg --files 定位。此類編譯與定位錯誤不算產品缺陷。

Stage361 Windows Cook 已驗收，因本輪產品／資產不變，不重跑或改封存舊 Cook。原始任意 KKS 卡免資源還原、私有 rig、完整 controller、整個應用 Shipping、安裝包、原作 pixels 及 Stage355 的 279 個未通過案例仍未驗收。
