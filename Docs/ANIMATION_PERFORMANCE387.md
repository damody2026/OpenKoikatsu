# Stage387：配對 IK 集中發布骨架矩陣

2026-10-03，正式 BuildR2 actual0／Succeeded，DLL已更新。

## 根因與修正

檢查剩餘約44ms成本時，發現 `IkSkeleton::set_world_pos/set_world_rot` 每次呼叫前flush dirty會重建所有bone worlds及名稱。但目前IK的world_pos、world_rot、parent及local getter／setter都從最新的tfs直接算Transform階層，不使用這份發布矩陣。完整solver內反覆重建的是給外部renderer的資料，並非後續IK必需的pose輸入。

新增內部 `IkSkeleton::new_batched`，僅配對solver使用。保留原 `new()` 的中途flush契約，不改其他maker／普通IK callers。配對solver的局部pos／rot／scale及各次live getter計算完全相同；暫存proxy移除後原 `body.rebuild_worlds()` 統一發布最终矩陣，隨後FrameCorrect／export refresh保持原順序。每個角色有自己的persistent solver和mutable body，沒有跨角色cache、不減少迭代、不降低IK或物理／陰影頻率。

依据 `C:/portable/KoikatuSunshine/kks_decomp/Managed/KoikatsuSunshine_Data/Assembly-CSharp-firstpass/RootMotion.FinalIK/IKMappingLimb.cs:158,166` 的ReadPose／WritePose及逐Transform寫入，以及既有Rust getter保持Unity local→world的計算順序。此修正只改中途發布時機，沒有更動浮點乘法結合順序、contact／形體規則或wire／ABI。

## 驗證方式

- IkR2：9項FinalIK tests，包括交錯local／world讀寫、名稱ASCII大小寫／缺失、parent hierarchy、一般／負／零縮放。batched與原new每次live讀取、最終locals／worlds exact。
- ContactsR1：另兩張真卡KK_226176＋KK_224305，兩種原生配對category的Idle contact，兩角色都從相同persistent solver起點clone獨立舊solver；link_ik／update後逐項pos／rot／scale、world矩陣、names exact。
- NativePlayback的兩個配對各加一次delta0 oracle，放在30frames性能量測以外。預設不複製舊solver；`OK_ANIM_SKIN_PARITY=1`診斷開啟時以valid0／1輸出，避免FFI assert／unwind。cfg(test)才assert。
- 原新solverstate及時鐘保持自己的歷史；舊solver oracle只用clone。native測試的delta0核對不推進動畫時鐘，放在性能區間外。

初輪Perf387PairR1沿用舊fixture取第一個排序共同state，選到S_Drink；其MotionIK state不存在，沒有執行solver，故沒有IK parity日志。actual0／AutomationSuccess只代表原先clock／geometry契約通過，不能作IK性能驗收。此前Stage384–386同類44ms數值亦不代表active IK成本。新Perf387PairR2改成原生共同Idle，要求四筆男女exact oracle，並以預設關閉的 `OK_ANIM_IK_LEGACY=1` 在同DLL、同state量測原中途flush。旗標只供診斷，測後還原；新舊30frames相同time區間，但粒子歷史與執行順序不同。保留全部舊log，不把不同workload當提升比例。

## 最終結果

BuildR2／IkFinalR1 actual0；ContactsR1 actual0、兩category各8 active contacts；Perf387PairR2 actual0／AutomationSuccess，四筆release IK oracle exact（角色tfs613／627，包含暫時proxy）。原walk、667／681匯出骨骼、幾何及matrix oracle亦通過。

| active Idle API測量 | 第一個選項 | 第二個選項 |
| --- | ---: | ---: |
| batched 30frames CPU平均 | 46.691ms | 46.609ms |
| 同DLL原flush 30frames CPU平均 | 50.256ms | 49.976ms |
| 原生pair首次／後續commit | 1281.467ms | 88.928ms |
| 背景prepare | 12.956秒 | 2.252秒 |

此輪active IK更新約少7%，只是這兩次同DLL的API CPU觀察，不當全場景FPS或所有動作固定比例。首次active IK commit仍約1.28秒；其SetGameAnimation女189.582ms／男102.907ms以外還有配對首次初始化等工作，尚未分段證明哪一項主因，不能只猜factory就宣稱已解。後續commit88.928ms。第二次背景prepare時舊配對更新37frames，max107.160ms。

單角色quiet25.297ms／双walk36.934ms來自未啟用IK的初輪R1，只作該workload參考，不與active Idle當同一測量。首輪R1及舊非active-IK性能記錄全保留。

Perf387ControlsR1 actual0／AutomationSuccess：七mask／owner／gesture／clock／generation cancel回歸通過。

這是API CPU計時與數值回歸，不是GPU／場景FPS、硬體GUIclick、全卡／所有姿勢的無bug認證。性能測試不包含oracle成本，不把不同輪次負載差異當固定比例保証。

失敗與界線見ERROR_LOG E265；源碼diff、成功及失敗的完整日志／receipt位於 `Saved/Tests/AnimationPerf387/`。fresh R1 audit／snapshot核對Stage386全部303項旧證據不變。
