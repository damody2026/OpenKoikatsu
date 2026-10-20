# Stage355：負縮放下的原生碰撞器尺度

SESSION243 / CLI353 / CACHE108。修正共用 `transform_lossy_scale` 的旋轉移除算法：負祖先尺度會反射後代旋轉，不能直接使用普通 quaternion 乘積。沿階層保存各軸符號，局部 quaternion 的 xyz 分別乘以 `signY*signZ`、`signX*signZ`、`signX*signY`，再組合。用此旋轉投影世界矩陣取得 lossyScale。此修改用於 component 尺度與 authored/native/generated/implanted collider 半徑，沒有新增磁碟欄位、卡別補丁或碰撞 epsilon。

原生內部 Transform 算法不是反編譯 C# 的一部分；此反射公式由實際 `Transform.rotation/lossyScale` 數值支持。`DynamicBoneCollider.Collide` 的半徑、軸向、內外邊界與平方距離判斷仍依反編譯程式碼，inside 仍使用來源的半徑加 particleRadius，不擅自改為減法。

## 原生對照

探針無 renderer、無卡片、無動作，只直接呼叫原生 collider。NativeR1/R2九種縮放，各162案/7290輸入；NativeR3加入 identity Transform，180案/8100輸入，涵蓋XYZ、inside/outside、height=0/radius/0.45、三種particleRadius、膠囊低端/內段/高端，以及單/雙/三負軸、全零/單零軸。每案均有非零原生碰撞反應。Native擷取均完整但延遲Quit，owned cleanup actualexit=-1，非乾淨exit0。

RedR2為cfg(test)忽略反射的負控制：尺度最大差0.400527894，穩定樣本最大碰撞差56.073990mm，測試失敗exit101。最終GreenR2尺度差3.576279e-7、旋轉quaternion component差1.192093e-7、端點世界位置差0.105367µm。**7821個穩定樣本**最大碰撞差0.240736µm，小於2µm門檻。其中18案identity全部810個輸入，包括零距離/中心線，max0.074506µm，未排除其中任何樣本。

## 必須保留的未通過範圍

**全部8100個輸入尚未通過：279個複合Transform的outside近零距離樣本未完成座標一致性驗收，原始最大碰撞差0.773254633m。** 原生OutsideSphere/OutsideCapsule以`distanceSquared > 0`判斷，沒有epsilon；TransformPoint端點約1e-7m的浮點差，在中心附近可改變正規化方向或跨越zero分支。不能以小端點誤差冒稱大輸出差正常，也不能把這279筆抹掉。

報告同時保存raw max、stable max/count、near-axis count與`all_samples_passed=false`；test的passed只代表明定的穩定尺度/碰撞子集合通過。分類使用原生端點與輸入的距離，outside且距離<2µm單獨報告；這不是產品epsilon或放寬子集合誤差門檻。RustR1、RedR2、GreenR1、GreenR2全部保留。一般world_rot_of及鏡像角色完整動態流程沒有因這次lossyScale estimator修正而宣稱原生逐幀全通過。

後續應擷取原生localToWorldMatrix、TransformPoint及Matrix4x4.MultiplyPoint3x4中間值，釐清世界矩陣/位置浮點順序；保留零距離分支，不加卡別位置偏移、epsilon或修圖。衣服穿插、不同世界Transform配對及整體scene零bug仍未驗收。

## 回歸與部署

dbone 29PASS/13ignored；既有九案動態骨鏈1620幀native parity通過，維持原20µm/quat2e-5門檻。原生飾品16觀測PASS。四卡neutral walk共960幀PASS413.30秒/actualexit0；兩靜態卡物理粒子0，兩女物理卡各48共享粒子，23,040逐幀位置有限且另一instance不變，實卡private garment物理粒子0。

Editor build43.83秒Succeeded、CLI build32.32秒，actualexit0，LIB/EXE/Editor DLL檢出353 marker。實卡擷取結束後串行UE GPU及CPU各ClothedActorScaleIsolation Success/actualexit0，各99衣服元件bounds觀測max0cm、實際renderer及雙actor隔離通過。三Animator本輪未重跑，沿用Stage353驗收。所有owned程序退出、探針撤出。

Audit.json與HANDOFF3 §47封存有限通過範圍，同時保留全部碰撞樣本未通過旗標與279筆近中心線差異；不得用stable subset passed將整體未通過項目標完成。
