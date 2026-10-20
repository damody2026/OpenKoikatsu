# Stage353：一般 DynamicBone 原生逐幀對照與通用修正

產品版本為 SESSION241 / CLI351 / CACHE108。沒有新增 persistent schema 欄位；版本更新使舊姿勢／抽取 cache 失效。修改共用 DynamicBone 路徑，無卡片名、圖片名、動作或時間點補償。

## 修正與來源

1. `DynamicBone.UpdateDynamicBones` 讀 component 的 `Abs(Transform.lossyScale.x)`。舊實作取世界矩陣欄向量長度，非均勻祖先加上旋轉時混入剪切。原生五案的中間值對照支持將世界矩陣軸投影到 composed Transform.rotation 軸；非均勻案的 scale 差從0.000424266降至浮點誤差範圍。共用 `transform_lossy_scale` 適用 persistent、one-shot 和 legacy component scale。
2. `DynamicBoneCollider.OutsideCapsule/InsideCapsule` 內段使用 `position += offset * ((reach-distance)/distance)`，端點與球使用 `center + offset*(reach/distance)`，並先比較平方距離。舊統一 nearestPoint 公式實數等價，但 f32 觸碰輸入不同。共用碰撞函式依反編譯保留來源分支與順序。
3. DB 的世界位置沿階層先縮放局部點，再旋轉／平移，與Stage350原生 TransformPoint控制一致。世界矩陣 basis保留完整剪切，只改translation計算順序。137原生Transform初始位置診斷的最大差由0.357647µm降至0.122878µm。

來源：`C:/portable/KoikatuSunshine/kks_decomp/Managed/CharaStudio_Data/Assembly-CSharp/DynamicBone.cs` 與 `DynamicBoneCollider.cs`。Unity內部lossyScale與位置運算的浮點順序由實際原生數值支持，不將五案推論冒充所有Unity Transform組合已認證。

## 已完成數值控制

原生探針產生無Renderer、無卡片、无場景動作的骨架。自由骨鏈、球形碰撞、膠囊碰撞、固定Z軸、非均勻缩放各180幀，連續保留歷史；倍率1/.6/1.8/1，同時移動／旋轉component。900幀、3600粒子位置及前一位置觀測，比對完整六節點輸入／輸出。

RustR2/R3為真正負控制：非均勻particle max28.864622µm / world25.873582µm / quaternion component6.365776e-5，超過原20µm/quat2e-5門檻。GreenR3/R4為最終三項修正的PASS：particle/previous max7.365852µm、world1.309947µm、local0.246730µm、quat1.403689e-5，門檻不變。R4另斷言每幀6節點/4粒子，以及所有輸出local位置、旋轉、縮放有限，避免NaN被max運算忽略。Native球形碰撞與自由鏈的最大差0.190528333m，是非零碰撞正控制。

既有Accessory318NativeR3控制最初有一幀quat2.235174e-5超標。cfg(test)舊欄長度算法得到完全同一失敗，且歷史報告也記此值，證明不是本次新退化。只改膠囊運算後仍2.217293e-5；加入通用世界位置運算後AccessoryGreenR2 PASS，16個卡／mode／時間點觀測、max world0.796712µm / quat8.713454e-6。它是原生Transform／已量測粒子係數的solver對照，不是完整卡片解析／所有curve驗收。

dbone SuiteR2 29PASS/0FAIL/12ignored，0.70秒，包含剪切scale負控制與既有cache、重綁、私有槽物理、碰撞、instance隔離。RustR1的型別編譯失敗不算solver負控制。NativeR1/R2資料均完整，但Quit延遲，核對自有PID/exe後終止actualexit=-1；runnerexit0只是資料完整。探針已撤出。

## 部署與實卡驗證

Editor buildR1 Succeeded43.32秒、CLI buildR1 38.56秒，actualexit皆0。最終InstalledR1四卡960幀PASS286.20秒/actualexit0：男女兩卡衣服粒子0僅作靜態控制；Alice與March_7th各48共享粒子，480物理幀/23,040粒子位置觀測、nonfinite0、另一session不變。實卡private衣服粒子0，不宣稱已覆蓋該路徑的實卡物理。

部署後UE GPU四項Success/actualexit0：三項Animator回歸、ClothedActorScaleIsolation（實際GPU renderer、99衣服元件bounds觀測max0cm、兩actor隔離）。CPU同項元件控制也Success/actualexit0，斷言實際CPU renderer、99觀測max0cm。LIB/CLI EXE/Editor DLL須全部檢出351 marker；Audit.json保存source、反編譯、fixtures、負/正控制、實卡、build、UE log及部署binary SHA。所有owned程序已退出、無native probe、無圖片；不覆寫Stage350–352歷史封存。

## 未驗收範圍

這些控制沒有衣服網格穿插檢查、配對場景正常性或全面零bug證明。原生生成案例只驗ordinary DynamicBone的outside球／膠囊、固定軸和縮放；inside邊界、旋轉非均勻外部碰撞器的lossyScale.z，以及Ver01/Ver02逐幀原生parity需要各自來源控制。已有unit控制不是這些範圍的原生驗收，沒有刪除不同Actor Transform配對guard。
