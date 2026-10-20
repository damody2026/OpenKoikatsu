# Stage384：雙人切換、粒子生命週期與骨骼查找

2026-10-03。正式專案 BuildR7 actual 0；DLL 已更新，重開 Unreal 使用。

## 根因與通用修正

Stage383 已將控制器／曲線解析移到背景，但實際雙人 API 測試仍有約 5 秒提交。不能把名稱快取或 worker 成功當成切換不卡。

分段測量 Perf384PairR3：女角 `SetGameAnimation` 的 neutral 2361 ms、select 1 ms、frame 2357 ms。原呼叫先 `SetPoseMode(0)` 完整重做 Shape/T-pose 與 DynamicBone，再 `set_anim_clip` 清除 private state，重新做每個鏈的 240 步暖機。進入另一場景時 collider flags 又清除 state。

1. `SetGameAnimation` 先核對片段；沒有 pending shape edit 時保留已載入的形體與 rigid neutral reference，直接設定 Animator。真正 `bShapeDirty` 仍走原 `SetPoseMode(0)` 更新。無效片段不重置舊播放。
2. `set_anim_clip` 保留 private DynamicBone 粒子歷史。體型、服裝、明確資源 reset 仍保留原重建路徑。不是刪掉首次載入所需的暖機，也沒有降低物理更新頻率。
3. 普通 hair/skirt collider flags 更新時，在既有 chain 上重新綁定 colliders，保留粒子；保留 authored freeze axis，解除 override 時還原原始值。Ver02 胸部的既有設定重建流程不變。`AttachedChain` 是未持久化的 runtime 型別，沒有修改 APNG/cache wire layout。
4. 配對 MotionIK／共用 IK 設定也在 Gallery 曲線 worker 準備；只捕捉 immutable root/state/motion 字串。session、角色自己的 solver、UObject 和提交仍在遊戲執行緒。配對資料 cache 只在 lookup/insert 持鎖，解析新資料時不鎖住正在播放的配對；保留原檔案 metadata invalidation。
5. `skel_export::refresh_pose` 每次建立一次 ASCII-insensitive 名稱索引，保留 body→head→head_full 優先、原 HashMap 同名第一項。每幀重建，不快取角色可變 TRS、不共用不同角色的骨骼。

依據：反編譯 `Studio/OCIChar.cs:1018` 切換 controller／Animator.Play；`DynamicBone.cs:126,173` 的 Start/OnEnable；插件 `KKS_Colliders/KK_Plugins/Colliders.cs:214,359` 更新普通 colliders／freeze axis，`UpdateBreastDB` 的 Ver02 重建則保持。上述來源位於 `C:/portable/KoikatuSunshine/kks_decomp/`。

## 實測與範圍

實際 March_7th outfit0 + ill_male_02 outfit0，667／681 bones，使用原生配對 metadata 的前兩個雙人選項和共同 controller state；不按卡名／姿勢名修改 runtime。

| API 測量 | 舊提交 Perf384PairR1 | 最終 Perf384PairR5 |
| --- | ---: | ---: |
| 第一個配對提交 | 5413.992 ms | 369.098 ms |
| 第二個配對提交 | 5226.971 ms | 93.045 ms |
| 第一個配對 30 frames 平均 CPU 更新 | 46.435 ms | 47.106 ms |
| 第二個配對 30 frames 平均 CPU 更新 | 57.818 ms | 47.603 ms |

最終第二個選項背景準備仍需約 12.9 秒，但舊配對在此期間前進 **240 frames**，最大單次舊配對 CPU 更新 **115.793 ms**。首選項準備約 20.6 秒；測試中首選項尚無舊配對。冷解析仍耗時，不宣稱載入時間消失。

測試明確分開 prepare、Start commit、Advance 更新。這是正式 API CPU 計時，不是滑鼠點擊的完整 Gallery callback 延遲、GPU 首呈現或整體 FPS；Gallery 仍有相機 framing／語音工作。不同輪次負載有波動：本輪單角色 quiet 量測亦約 27.7–38.2 ms，因此不把單次差異當穩定提升比例，也不宣稱所有姿勢皆零停頓。

## 驗證

- BuildR7、Perf384PairR5、Perf384ControlsR2：actual 0，Unreal 測試需另有 Automation Success。
- NativePlayback：兩卡骨骼／walk camera、dynamic proxy、原始 clone/OBJ 幾何與 world tolerance 對照；兩次原生配對選取各 30 frames，共同時鐘；背景準備期間舊配對 Advance。
- ParticlesR2：installed 真實女卡，clip 切換保留 private state；無效片段保留時間與狀態；普通 floor collider flag 改變、delta 0 的粒子 pos/prev 完全相同；explicit reset 仍清掉 state。
- IndexFinalR2：source priority、同名／ASCII case、非 ASCII 字元、缺失名稱、新 pose 值與空來源；結果直接比原 `.find(eq_ignore_ascii_case)`。
- MotionFinalR2：截斷、過大、負數及惡意 MotionIK bytes 拒絕。
- KksMouse：七種滑鼠組合、owner/gesture/clock，以及 Stop/Pause/Replay/Refresh 的 generation 取消。

保留 R1–R4 中間結果：只準備 IK 的 R2/R3 仍約 5–6 秒；R4 後續降到 89.543 ms，但第一個配對仍 2546.029 ms，不能當最終。E262 紀錄這些誤判與命令錯誤。最終資料／snapshot：`Saved/Tests/AnimationPerf384/R1.audit.json`，原 Stage383 的封存資料原樣核對。
