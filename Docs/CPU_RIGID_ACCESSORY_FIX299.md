# E299：CPU預覽剛性配件掛接

2026-10-05，使用者指出 `Anim298PerfR3/C010_Flow5s.png` 兩側裝飾留在T-pose位置。

## 遊戲來源與根因

`C:/portable/KoikatuSunshine/kks_decomp/Assembly-CSharp/ChaControl.cs:2373`：`ChangeAccessoryParent` 將配件 `SetParent(referenceInfo.transform, worldPositionStays:false)`；局部位移／旋轉由後续SetAccessoryPos/Rot控制。`ChaReference.cs:278` 將 `a_n_arm_L` 對應到真實骨架參考物件。必須跟隨卡片指定parent，不可依網格名稱或圖片猜成手腕。

C010原卡KK_110449.png、outfit0：Choker_03的8個submesh指定 `a_n_arm_L/R`（每個48,376來源頂點），maidband指定 `a_n_wrist_L/R`。資料已有正確rigid_follow_bone，APNG本身不必重製。

CPU模式沒有BodySkel，舊CaptureRigidFollowerNeutral與UpdateRigidFollowers直接return。剛性配件頂點已bake到初始世界空間，因此停在T-pose位置；source到draw的局部頂點相等檢查抓不到元件未跟隨的錯誤。

## 通用實作

在載入時，CPU用既有 `Session.BoneWorldMatrix` 記錄每個實際參考骨骼的矩陣。動畫每幀只查詢每個socket一次，以UE行向量慣例 `neutral.Inverse() * current` 更新整個ProceduralMeshComponent的相對變換。保留原卡局部擺放與材質，無須重新上傳大型裝飾幾何。獨立DynamicBone／slot attached配件不重複套用矩陣；有BodySkel的路徑維持原骨架跟隨。

中間嘗試過native skin逐頂點delta：真實fixture通過，但會使48k頂點的裝飾每幀改變／上傳。此方案已撤回，最終使用元件變換。兩份臨時Rustfixture日誌保留為調查證據，不冒稱最終版本回歸。

讀卡驗收新增CPU配件的元件變換後位置與原生骨骼矩陣比較，並列出配件數、手臂配件數、位置誤差與實際移動距離。

## 驗證

- `Accessory299BuildR1.log`：正式Editor編譯Succeeded。
- `Accessory299AllOutfitsR1/report.txt`：C010全部7套服裝、每套5秒walk，valid1；7張影像檢查valid。outfit0剛性配件47個／手臂分區8個，socket誤差0.000019813cm、最大移動56.270049cm。全outfit最大誤差0.000026106cm。載入0.984–2.259秒。
- 圖片 `Accessory299AllOutfitsR1/C010_outfit00_Worn.png` 已實際查看，兩側裝飾不再留在空中。
- `Accessory299FlowR1/report.txt`：20秒walk＋3秒噴射／15秒停噴，valid1，全部3張圖片檢查valid。47個剛性分區／8個手臂分區誤差0.000127057cm；半脫／全脫／穿回、GUI存卡、離線重載通過。clothing_coatings=1、守恆誤差0，peak packets13／impact FX16，粒子上限512。
- C010本輪actor9.717930ms、乾燥整幀12.435480ms、停噴整幀12.984968ms。不是全專案13ms保證。
- `Accessory299OtherCardsR1/report.txt`：C001（三月七）與C018（不知火舞）各5秒walk，valid1、2張圖valid；socket誤差0.000000932／0.000001533cm。整幀14.322250／9.305622ms，C001仍未13ms。
- 最終plugin SHA256：307C87B1253ACE4BA20D472601EE108197C11F2E17DC94A360CFE0C8A4DE65EB。
- 未遍歷100卡／所有動畫／所有姿勢；現有遊戲socket查詢與矩陣處理為共用邏輯。
