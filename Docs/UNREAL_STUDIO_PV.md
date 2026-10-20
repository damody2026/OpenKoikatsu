# Unreal Studio PV 手腳複製（stage295）

## 來源與根因

目前播放已有 Animator、手型、ExpressionBone、ABMX、身體／衣物物理與 GPU palette，但缺少 Studio `PVCopy`。來源是 `studio/base/00.unity3d` 中實際 body prefab 的 `Preparation._pvCopy`，不是 `PVCopy.Reset` 裡的示範骨名。

反編譯依據位於 `C:/portable/KoikatuSunshine/kks_decomp/Managed/CharaStudio_Data/Assembly-CSharp/`：

- `Studio/PVCopy.cs`：LateUpdate observable 的四組序列複製。
- `IllusionUtility.SetUtility/TransformCopy.cs`：先 localScale，再 world position，再 world rotation。
- `Studio/OCIChar.cs`：LoadAnime 的 H／normal PV 初始化，以及 ActiveKinematicMode 的 FK 閘門。
- `Studio/Info.cs`：HAnimeLoadInfo 的四個 PV 欄位使用 bool.TryParse，缺失或無效為 false。

## 共用實作

`Tools/ok-extract/src/unity.rs::studio_pv_config` 沿 Preparation→PVCopy 的序列化 PPtr 取得 pv／bone GameObject 陣列，轉為 prefab 相對 hierarchy paths。檔案作用域保留，不把 external FileID 的 PathID 當本檔資產。`pv.rs` 只接受唯一相對路徑匹配；沒有卡名、臉型、骨名例外或位置常數。

Rig 與既有 EyeLook 共用 Sideloader 的 `studio/base/00.unity3d` override 選擇；補齊實際 body prefab 的缺失節點。世界位置使用完整 parent matrix 反算；世界 rotation 使用 parent quaternion，避免由非等比 scale／shear matrix 分解旋轉。目的節點先設 scale，保留 source setter 的次序。

PV 進入 apply_shape 與 finish_animator_pose 的共用 Studio 流程，位於手型之後、ExpressionBone／ABMX 之前；external Animator locals 使用同一路徑。後續身體／衣物物理、GPU palette、私人髮與飾品仍由既有共用刷新流程处理。

Source PV flags 與 FK eligibility 是 transient session state。普通 Studio LoadAnime 設四個 true；H 使用目錄 options 第4..7項；FK 暫停複製並保留 mask，停用 FK 後恢復。這與普通動畫保留 Yure 設定的規則不同。Cache 保存 binding config，重新開卡恢復 mask15／FKfalse；pipeline141，decoded cache90。

## API 與 Unreal

- C ABI 新增 `set_studio_animation_pv_profile`，在來源 graph、options、Yure 全部驗證後一起寫入 PV mask。bit0..3 對應序列化 slots，不能用固定骨名解讀 mod 綁定。
- `set_studio_pv(mask,fk)` 僅控制 eligibility；沒有宣稱它提供完整 FK solver。invalid mask／FK 拒絕且保留狀態。
- `studio_pv_flag` 回傳實際開關；`studio_pv_bone` 提供 source／destination 在共同 palette 的索引。
- `UKKAnimationCatalog::StudioPVMask` 讀已匯入 Unreal 的 options，依 bool.TryParse 語意處理。Gallery／PreviewActor SetStudioAnimation 自動傳入來源 mask。直接使用舊 API 的呼叫者沒有提供 PV 資料，維持 mask15 的相容預設；實際 H row 請使用 Catalog mask。
- FOkSession 提供 SetStudioPV、StudioPVFlag、StudioPVBone。真 RHI audit 核八個來源／目的節點、body palette 的座標轉換與全部 GPU followers；不把 NullRHI getter 当實際顯示證據。

Palette 的 integer handle 屬於建立它的 Session；同一張卡的兩次載入也不能互用索引。對照需以各自 serialized slot／骨名取得 local index，再送入各自 external pose入口。既有Yure對照也已改為 name join；BodyDynamics／ExternalBodyDynamics使用同一Session，不存在這個跨Session問題。

## 獨立原生對照

`Tools/GamePoseProbe/Probe.cs` 取得實際 OCIChar.Preparation.pvCopy 的 GameObject 陣列與已編譯 `<Start>` callback，直接執行原版 callback；沒有在 probe 內重寫 CopyPosRotScl。使用實際 LoadAnime／ActiveKinematicMode 的切換序列，擾動 PV 位置／旋轉／scale 和目的 parent 的非等比 scale，保存 before hierarchy 與 after local/world TRS。

五卡各15案例：normal15→H12→FK0→退出FK回H12→normal15，每階段三個姿勢。Rust 獨立重建 hierarchy，核對实际 GameObject binding、local TRS、world position；停用 slots 必須與 before 完全相同。另將全部節點改為任意名稱，確認相對綁定與計算相同。

## 重現

1. `Tools/GamePoseProbe/build.ps1`
2. `Tools/GamePoseProbe/run_pv_native.ps1 -Cards All`
3. 在 `C:/portable/OpenKoikatsu/Tools/ok-extract` 執行 `cargo test --release --lib pv_ -- --ignored --nocapture --test-threads=1`
4. Unreal Build.bat 編譯 OpenKoikatsuEditor Win64 Development。
5. `Tools/GamePoseProbe/run_pv.ps1 -Mode Tests`
6. `Tools/GamePoseProbe/run_pv.ps1 -Mode Shots -Blink open -Cards All -Pose OneD`
7. `Tools/lua/lua.exe Tools/GamePoseProbe/verify_pv.lua`

原生 runner 驗證 completion／array counts，只結束自己啟動且 exe path 相符的 game，finally 移回暫時 probe。重跑會保存舊 fixture，不依賴過期 completion。

## 驗證範圍

原生 PV callback 與來源 policy、改名／非等比 parent 案例是獨立來源證據。Session／UE 控制測試核共用入口與 palette，RHI 核真正 displayed bones、髮／飾品附著與眉眼回歸。這些不等於整個 Unity 自動 LateUpdate 排程的逐幀認證，也不等於完整 FK／IK、全部 mod controllers、full H gameplay 或 shader pixel 等價。External GameObject PPtr 與奇異 parent matrix 尚未作來源認證；不靠同檔 ID 猜測或 identity inverse 掩蓋。下巴輪廓依使用者指示延期。

最終數字、接受 log／PNG hashes 由 `Saved/Logs/Studio295Audit.json` 與本節後續驗收紀錄保存。

## 最終驗收（2026-09-29，已完成）

- 原生五卡75frames／180copied／120held／180positive；local/world/quaternion最大3.5762787e-7，門檻2e-5不變。任意節點改名後結果完全相同。
- 最新Rust共用入口300frames、720positive、10atomic；normal與cache reset、四組綁定皆PASS。最新legacy135frames／81000骨TRS／2929257附件vertices max0，405atomic PASS。新增的第600個body transform是實際Studio rig中的Look At Target，非漏算比較項。
- Production接受build29.12秒；僅修測試的後續build分別7.13／4.58／PV接受版／Yure接受版5.11秒。18項core success之中Yure又按獨立Session name join補驗；最終採17項原core＋PV300frames＋Yure450frames，共19種測試全通過。初始PV失敗及兩次FK對照失敗保留，不宣稱第一次queue19全綠。
- 五卡15RHI PNG900×1080；四組PV×source/destination、424 follower samples，palette坐標誤差最大2.252e-6cm，完整follower transform一致。71髮／飾品attachment audits max0cm；15fit≤.92、灰背景與gaze／highlight／world-target EyeLook gates全部PASS。全部總覽與Alice原圖已目視；沒有新增眉毛／側髮／配件錯位。
- 圖在Saved/DebugShots/Studio295_OneD_open，僅15PNG＋README。全身為固定world背視角，portrait依head取景；row r00001390，H Stop_Idle t=.4、source PV0011與Yure0011。正式Saved/Screenshots的stage286 Tpose15图逐檔hash保持。
- 統一verify_pv.py PASS；Studio295Audit.json保存native／source／logs／PNG／ContactSheet SHA256，Studio295Verified.log為正式輸出。錯誤完整E-151。所有本輪owned game／UE已結束，probe不留game plugins。

PV來源複製已完成；完整FK／IK pose solver、option items、Studio scene、多weighted／additive／Directional／Mirror／Timing／transitions／CrossFade、所有mods與shader pixel等價仍是獨立工作，下巴依指示延期。
