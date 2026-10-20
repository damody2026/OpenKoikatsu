# Stage383：動畫切換與即時播放性能

2026-10-03。正式專案已編譯更新，重開 Unreal 使用。

## 改動

1. 控制器名稱目錄改成不可變共用快取，依 game root、來源描述與 raw/state 類型分開。count 直接取長度，index 直接取名稱，不重做 filter/nth 或每次 canonicalize。名稱、排序、同步層過濾與來源限制維持原規則。Restart 用精確 membership 查詢，先驗證雙方，不再各列一次整份名稱。
2. Gallery 切換先背景準備控制器與片段，工作執行緒只接觸不可變資源，不持有 session 或操作 UObject。準備遍歷全部可達 blend-tree 葉節點及活動／基礎層，避免另一個身高或參數值在套用時才拆新片段。原本的雙角色播放器保持運作，兩方準備好才回遊戲執行緒套用。
3. 完成回呼核對 request generation、角色弱參照與目前選項。停止、暫停、重播、更新目錄及介面關閉會取消待切換請求；過期結果不套用。取消或失敗會更新準備提示。
4. 即時飾品蒙皮原本每幀完整複製 renderer，再產生 OBJ 字串，最後只取 positions/normals/tangents 並丟棄 OBJ。新增 `skin_mesh_geometry` 與原 `skin_mesh` 共用計算核心；播放只複製必需的幾何、權重與 binds，直接取幾何結果。原 CLI／匯出 `skin_mesh` 仍產生 OBJ。
5. LBS 的 `posed * bind` 在位置、法線與切線各通道內每骨骼計算一次，不每頂點重乘。平滑迭代數為零時直接返回，不建立無用的鄰接表。AssignedWeights 的同名骨骼查詢與世界矩陣使用函式內快取，保留原第一個同名匹配；每次呼叫重建快取，不共用角色可變姿勢。
6. 每個角色顯示刷新只讀取一次骨骼陣列，供相同共用骨架元件使用。依 Engine `PoseableMeshComponent.cpp:53` 的 RefreshBoneTransforms 更新 component space、bounds、children 與 dynamic data；取消每幀重建 render proxy、重複 Fill 與無需重新配置時的 AllocateTransformData。

未降低物理更新頻率、刪除碰撞／IK、停用陰影或修改動畫時鐘。沒有卡名或特定姿勢特判。

## 測量與驗證

| 測項 | 結果 |
| --- | --- |
| 最終正式 BuildR16 | actual 0，Succeeded |
| Perf383ControlsR3 | actual 0、Automation Success；七種滑鼠組合與 Stop/Pause/Replay/Refresh 的請求取消回歸 |
| CatalogueFinalR1 | actual 0；3,696 states、三次索引列舉 1.271 ms，名稱與原資料完全相同；cold controller 14,766 ms |
| SkinFinalR1 | actual 0；位置／法線與原逐頂點矩陣公式完全相同，包含負權重、零權重、越界骨骼、bind 數量不相等 |
| TangentFinalR1 | actual 0；方向、位置 palette、切線 handedness 回歸通過 |
| Perf383NativeR12 | actual 0、Automation Success；實際 March_7th 卡原版 walk、667 骨骼、proxy 不重建、worker 準備與精確 membership 查詢通過 |
| 原完整 clone/OBJ 對照 | 37 runtime renderers、44,480 頂點的 positions/normals 完全相同 |
| 原 fresh-world 矩陣對照 | 6 組檢查，最大 abs 差 0.000000238，低於明訂 0.000001 界線 |

原 Perf383NativeR1 單角色 20 ticks 平均 **119.479 ms**；最終 Perf383NativeR12 關閉額外分段 profiling 的 20 ticks 平均 **37.006 ms**，本輪約降低 **69%**。這是角色 Tick 的 CPU 耗時，不是整个場景 FPS、GPU 呈現時間或實際滑鼠點擊的雙角色切換延遲。兩輪 phase／系統負載不同，不能當固定性能保證。

分段測量指出主要成本在飾品更新；身體／衣服物理各約低於 1 ms，臉部與陰影亦非這輪主要成本。移除 OBJ 前，單個 skin 約 2–3 ms；移除後幾何核心多為 0.02–0.05 ms。保留全部中間測試，沒有把初期只有名稱快取、119–191 ms 的結果宣稱為播放問題已解決。

背景解析仍需時間，快取只在同程序內。遊戲執行緒保留角色套用、物理初始化、顯示刷新與語音等既有提交工作，因此不宣稱所有切換完全零停頓；新增日誌區分 catalogue worker、clip worker 與 game-thread commit 耗時，方便對實際遊玩紀錄繼續核對。

## 紀錄

- `Saved/Tests/AnimationPerf383/metrics.json`：上述量測與原生 parity 統計。
- `Saved/Tests/AnimationPerf383/`：Rust tests、正式 build receipts/logs、原遊玩日誌複本。
- `Saved/Logs/CameraControls382Perf383*.log`／`.stderr`：Unreal 驗證與原生分段資料。
- `OK_ANIM_PROFILE=1` 可啟用診斷分段；`OK_ANIM_SKIN_PARITY=1` 啟用完整 clone/OBJ 與 world 對照。預設不啟用。對照失敗輸出 valid=0，由驗證程序檢查，不在 FFI 裡直接 panic。
- `Docs/ERROR_LOG.md` E261 保留編譯錯誤與 Perf383NativeR7 的嚴格浮點 assert 事故。原 Stage382 封存原樣核對；本輪封存 `Saved/Tests/AnimationPerf383/R1.audit.json`。
