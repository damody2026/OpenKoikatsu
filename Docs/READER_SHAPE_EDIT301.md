# E301 讀卡編輯：身體參數一致性與平均16ms

2026-10-05。使用者最後確認以平均16ms為標準，非每次最大16ms。範圍為既有CPU讀卡預覽，保留左邊滑桿、原APNG及原網格解析度。

## 根因與通用修正

GUI OnShapeChanged → MarkShapeDirty → ApplyPoseFrame 原先呼叫完整 apply_shape；每次重建body/clothes/private DynamicBone狀態，重新240步暖機。Shape301BaselineR1 C001九次平均480.207600ms、最大504.718803ms。這不是正常Maker參數更新。

ChaControl.cs 的 LateUpdateForce 將 ResetDynamicBoneAll、UpdateShapeBody、UpdateShapeFace分開。UpdateShapeBody約5170行只更新shape keys/mask，不要求重啟物理。因此新增 apply_shape_live（靜止即時更新）及 prepare_shape_live（動畫同幀準備基準），保留粒子歷史，更新乳房reference/collider尺寸；動畫的正常frame只求解一次物理，完整載入/匯出仍使用原apply_shape。

E299 CPU rigid 跟隨錯把已蒙皮頭部當成剛性物件，改身高後再次套頭骨矩陣，造成錯位。現在只有skel_kind0世界烘焙剛性物件接收跟隨；kind1/2/3使用原生蒙皮後的頂點，kind4使用私有骨架。真正a_n_arm_L/R手部飾品跟隨保留，沒有卡名特例。

## 驗收定義

- 從GUI OnShapeChanged發出44×6次參數變更，保留所有shape keys。
- shape_update_mean_ms包含ApplyPoseFrame中shape基準、正常動畫、蒙皮及程序網格發布；max與event→完成latency分開報告，不能把平均當每次延遲。
- 檢查最終GUI参数與原生一致、CPU已蒙皮元件相對actor無額外rigid變換、source→draw映射與真實配件socket。
- 原生fixture比較所有44項的body/head形變基準與完整重建，保留private particle history；測試UV接縫重複頂點、下一個動畫幀有限值及接縫。
- 靜止編輯和動畫編輯分別實測；正式編譯後檢查影像。尚在執行最終驗收，不提前宣稱完成。

## 中間結果與錯誤保留

Shape301Native C001/C010/C018 R1各44項通過，接縫1275/1518/1353組，最大間距0/0.000000179/0.000000119m。C001單次native峰值18.2265ms不當平均承諾。

GuiR1平均15.010499ms，GuiR2平均13.698085ms，但舊測試把根BodyMesh的actor落地平移當額外變換，兩輪valid0；改以ComponentToWorld相對ActorTransform檢查。

GuiR3平均16.636912ms，超過門檻，valid0。不可挑比較快的R2冒稱最終成功。進一步刪除同幀重複物理解算。

NativeC001R2以「dt0刷新再正常動畫」當staged解算oracle失敗，body物理結果不同；錯誤oracle把多一次物理解算視為正確。修正驗收為對照原始shape基準、prepare不改private粒子、正常frame有限值及接縫。沒有放寬接縫門檻（0.0005m），不要求保留已確認的重複step。

## E301 補充：非均勻比例下的真正飾品跟隨

GuiR5平均更新已低於16ms（C00114.528142/C01014.827282ms），但C010 socket仍偏1.613833cm，因此valid0。Rigid跟隨需要完整骨骼矩陣；FTransform只能保存TRS，遇到非均勻父縮放會丟失shear。

CPU現在分解精確Delta為可用元件Transform C及殘差R=Delta*C逆，draw頂點使用P*R，再由元件乘C，得到完整P*Delta。僅殘差改變才更新compact draw資料；一般TRS仍只更新元件。法線用逆轉置，切線從第一次殘差更新前的原始draw切線取值，不反覆累積變形。實際socket驗收使用section頂點+元件對照完整native骨骼矩陣，不能只比較同一個近似Transform。

保留CPU world-baked geometry載入時neutral，切姿勢/ApplyShapes不得把改過比例的骨骼重抓為neutral。GPU維持原路徑。

動畫prepare也略過隨即會被正常Animator重算的曲線/ABMX姿勢，但保留shape基準及乳房reference設定；最終三卡原生fixtureFinal2通過。細節日誌改為OK_SHAPE_DIAGNOSTICS=1，缺資源及錯誤仍照常記錄。

## E301 正式驗收（R6）

Shape301BuildR6成功。Shape301GuiR6／Shape301TposeR1均valid1，6張1600×1000影像檢查valid；C010動畫、C001靜止影像已實際檢視。每張各44項×6值=264次GUI變更，兩模式合計1584次。

|卡|動畫中參數更新平均ms|動畫／編輯整幀平均ms|靜止參數更新平均ms|
|---|---:|---:|---:|
|C001|11.231619|13.021234|8.500582|
|C010|14.797194|14.573108|12.349155|
|C018|8.302480|9.575403|7.179766|

三張都低於平均16ms。最大單次更新動畫中52.749/95.465/100.771ms、靜止22.703/111.866/19.105ms，保留尖峰，不宣稱每次16ms。event→完成延遲也獨立列於report。

body/head/full-head形變與完整重建對照、private history、animated接縫和有限值，Shape301NativeC001/C010/C018Final2均通過。接縫1275/1518/1353組；native最大接縫0/0.000000179/0.000000119m。GUI CPU已蒙皮無額外變換、draw mapping誤差0；C010八個arm rigid由1.613833cm降至0.000083333cm，靜止0.000153173cm。

本段完成身體即時編輯與CPU裂開／飾品比例修正。使用者後續將平均16ms擴大到讀卡模式所有載入完成後操作與水槍；新E302正在加噴射中／停噴／半脫全脫穿回後五秒播放驗收，不以本段冒稱擴大範圍已全部完成。APNG格式／原始高模未降低、不需重製。
