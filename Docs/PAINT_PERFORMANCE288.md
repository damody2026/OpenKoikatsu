# 水槍碰撞準備續修（E288）

## 通用改動

原本每個120Hz物理子步、每顆packet各建立一份忽略人物的世界碰撞名單；現在同子步共用同一份 FCollisionQueryParams。每子步重新讀取有效component，不長期保留失效指標；碰撞查詢、命中順序、最早命中及世界／皮膚的比較不變。Sweep仍在GameThread，不改成異步World查詢。

原本BVH葉節點每次packet查詢都重新由三個世界座標構造FBox。現在每次動畫幾何Refit建立FaceBounds，一面一格、各工作只寫自己的格；BVH葉節點也由相同三角形盒子合成。查詢重用包圍盒並沿用Radius+.001的容差及原本精確SweepTriangle。仍使用實際動畫／Morph／scale後座標；沒有使用renderer的可能過期Bounds或PhysicsAsset替代皮膚三角形。

新增約每面sizeof(FBox)的快取；87k面約數MiB，代價是記憶體及每次Refit的盒子更新。單純查詢加速不是完整幀加速，也不代表消除了其他動畫／覆蓋／粒子成本。

## 驗證

Unreal 5.8.3，Sweep288ModuleBuildR2專案模組編譯／連結成功；Sweep288RegressionR1四項Success：Camera.KksMouse、Paint.Film、Paint.MaskParity、Paint.Scene。Scene增加位移、旋轉、非等比縮放後的256條隨機射線，快取葉節點拒絕與完整三角形掃掠的最早命中相同。既有動畫附著、Morph、GPU遮罩清除／讀回、多材質／多人、隱藏／顯示與30/60/120Hz守恆檢查仍通過。

新增MeasureSweepPair：在同一份凍結幾何上，對每個可見有油漆的surface最多取4條確定種子的射線，交替比較現算FBox及快取FBox，各8次；比較命中有無、surface、triangle、time與重心座標。計時在普通性能報告完成之後，不含Frame平均，不推進油漆或動畫。此測量只涵蓋手工表面查詢，不含World Sweep或名單準備，不能拿來宣稱全Ballistic或FPS倍數。

## 實際卡片：0987

各暖機2秒、噴射8秒、停止8秒；KKS March及空遊戲根目錄的離線APNG KK_346095，真正動畫持續變形68.667／66.184cm。質量誤差約1e-12，全部valid1／exit0。

| 測試 | 噴射整幀平均ms | 停止整幀平均ms | 停止水槍Actor CPU平均ms | 同射線 原／快取 查詢ms | 不一致數 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Sweep288NativeAnimatedR1 | 41.087 | 47.607 | 18.650 | 1.493／1.415 | 0 |
| Sweep288PortableAnimatedR1 | 24.680 | 28.204 | 13.613 | 1.021／1.005 | 0 |

查詢的同資料改善約5.2%／1.6%，幅度有限；新舊整幀輪次受到濕面／液滴／排程變動影響，不能用跨輪差當成此改動的獨立倍數。仍未證明所有動畫60FPS。

完整報告也保留其它階段的慢值：本轮March同資料遮罩原／批次5.174／5.524ms，新排程在此份資料更慢；APNG為7.457／4.542ms。這證明E287批次方案不是每張卡每種濕面分布都保證更快；不刪除這個反例或用最佳輪掩蓋。遮罩逐像素差仍零，活體油漆量差零。

## 錯誤及交付

Sweep288ModuleBuildR1新增測試變數Face遮蔽外層Face，UE將C4456當錯誤；已改TestFace並在R2重新編譯。新增測試應避免與既有大函式的區域變數同名。

normal DLL與0987相同，匹配依賴及來源／报告保存ModuleSnapshots/0987/verification.json。CardReaderDevelopment.cmd重啟使用；沒有關閉另一專案Editor。

DLL SHA256: 8cf5c2c30de931f67e117f2a60d3921f5406545486e246846585dee9ac85797d
