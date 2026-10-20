# Runtime palette 單次匯出驗收

## 實作範圍
FOkSession::SkinPalette 保存每個成功讀取 part 的容量 hint；首次 count+copy，後續直接 copy 當前 palette。接受較小的實際數量；容量不足或讀取失敗時 count+copy 重試一次，最多三個 native 呼叫。全部輸出先寫暫存，成功才發布；錯誤保留呼叫者前一份矩陣／source view。Close／Open 清除 hints，move 隨 session 所有權轉移。native 每次完整驗證，不保存 borrowed source pointer 或 pose，不需要 APNG 專用資產。

現有 CPU 表情／嘴型更新 ApplyMouth 也使用此讀取介面。新 bridge 同樣受益，但 private 服裝／頭髮仍未整體搬至 GPU。這不是 source-generation lease，也尚未消除 native 每次的 immutable vertex validation；不可宣稱完整 O(bones) 或由此直接達到 120FPS。

## 編譯與回歸
Saved/InteractionPaletteCapacityBuild.log 明確 Result Succeeded，51.38秒。PaletteCapacityRegression：19/19 Success、exit0、四個 DLL hash 穩定、external Unreal0。

新增 OpenKoikatsu.Animation.PaletteReadCapacity：首次兩個 native 呼叫、暖機後一個、增長最多三個、縮小一個且裁切長度；故意污染暫存的失敗仍保留呼叫端舊值；失敗清除 hint，空／超上限數量拒絕且不 copy，恢復來源後能重新成功。

其餘包含實際男女 APNG 三時間點 live native 矩陣、完整 affine／signed／零scale／carrier refresh／source bounds、原始高模資料保留、語音與54嘴型原公式等回歸。本輪没有角色專用預計算檔。

## 完整場景驗收
PairEyesSpeedPaletteCapacity：audit_completed1／functional_pass1。APNG 進場6157.493ms，首個 ready walking8136.117ms，walking 兩輪後仍移動。首個 ready pair72.062ms；12次 ready switch 最大 click25.711ms／最大幀44.165ms，原300ms門檻通過。13個播放速度階段與暫停全部原CPU／frame門檻PASS，68次調速請求、callback最大0.053000ms。54嘴型／語音、shared clock、camera head／eyes／belly、male replace及失敗保留、return與shadow depth oracle通過。

五張眼睛原 PNG 已用 view_image 逐張檢查：瞳孔／高光正常，閉合／重新開啟正常，播放中眼睛存在，stat fps 顯示；eye-visual-review.json 已保存。此為既有 renderer 的功能驗收，不是新 private GPU renderer 像素 oracle。

混合階段平均6.757ms不能當穩態120FPS證據；最大1245.183ms仍保留，不能宣稱所有背景載入都在300ms內。run-evidence：exit0／四DLLstable／external Unreal0。此 binary 同時包含當前 workspace 的其他變更，不用前後 binary 的 FPS 差值歸因本項改善。

水槍使用 SkipPaint，功能保留但本輪不量測。120FPS 仍須正常 RHI 的穩態 Present／全幀報告驗證，不能沿用上一個 binary 或混合階段平均。

## 最新穩態效能與剩餘工作
PairProfilePaletteCapacity 已完成、exit0／四DLLstable／external Unreal0；profile模式 functional_pass0 是模式欄位，不是本輪功能測試失敗。entry6172.651ms，首個 ready pair222.764ms。quiet兩段各15秒，1525／1533 samples，全幀9.892095／9.841691ms，實際Present101.093925／101.611047FPS；Advance6.924931／6.886356ms、GT9.780665／9.733908ms、GPU1.369588／1.364593ms、wait0.110922／0.107841ms。最大幀28.772298／13.014399ms。

8ms／120FPS仍未通過。第三段有verbose記錄，全幀13.396480ms，不用作quiet基準或120FPS證據。三輪驗收的四個DLL hash全部一致；本輪 build／audit 已結束。此次容量hint僅省掉重複palette匯出，沒有解決主要private CPU skin／normal-motion draw generation／packing／upload。

下一步保留原material／UV0..3／顏色／draw indices與法線旋轉公式，將private immutable source及同幀native矩陣接入GPU頂點生成或等價skinning。KKStreamMeshProxy使用原FLocalVertexFactory及既有material，若採compute輸出position／packed tangents可避免重建Crowd缺漏recipe，但必須新建可UAV寫入buffer、正確barrier／draw順序／bounds／ray與paint按需查詢，通過same-pose圖像oracle後才停止native CPU geometry。此為待實作事項，沒有在此驗收中宣稱完成。
