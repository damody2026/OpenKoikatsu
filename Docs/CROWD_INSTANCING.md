# 100 人 Optimize for Instancing 實驗

已完成 100 人的建置開關與四輪 A/B。**本次未量到效能收益：開啟的兩輪平均每幀成本比關閉增加約 8.5%。** 預設保持 PackedRig；開啟版本保留供測試。Development 120 FPS 尚未達成。

| 狀態 | 平均每幀 ms | 約 FPS | P95 ms | GT ms | RT ms | GPU ms | samples |
|---|---:|---:|---:|---:|---:|---:|---:|
| 關閉 1 | 40.36 | 24.8 | 47.74 | 16.02 | 40.03 | 8.05 | 868 |
| 開啟 1 | 45.22 | 22.1 | 54.33 | 17.12 | 44.64 | 8.24 | 774 |
| 開啟 2 | 38.66 | 25.9 | 40.96 | 14.76 | 38.34 | 8.26 | 906 |
| 關閉 2 | 36.97 | 27.1 | 41.69 | 14.41 | 36.68 | 7.75 | 948 |

兩輪每幀平均：關閉 38.66 ms（約 25.9 FPS），開啟 41.94 ms（約 23.8 FPS）。每輪都有 100 人載入並持續走動；關閉為 0/505 meshes 開啟，開啟為 505/505。全部 LOD 0，皆為 100 個 CPU pose evaluators，原材質與 section 不變。四次均未達 120 FPS。

兩組配對中開啟都較慢，但開啟的兩次重測相差約 17%，關閉相差約 9%；背景負載、動畫相位與 cache 有變動。因此結論限於「本次沒有可證明的收益」，不能把 8.5% 當成所有機器與場景的固定退步。RT 約 37–45 ms，這個選項沒有解決目前的主要渲染提交成本。

四輪均使用 project module `UnrealEditor-OpenKoikatsu-0764.dll`，SHA256 `E4AFCA618C8253A15EB2FAA71887AE68AFFDA149B0C58EBEF86AF4A4073DCB9A`。Renderer 的 SHA256 與各輪完整結果在 `Saved/Crowd/InstancingABR2.json`；各輪 process exit 為 0。正式取樣中沒有 owned 匯入、編譯或資產建置工作。

本次只改 SkeletalMesh 各 LOD 的 `bOptimizeForInstancing`，不改 CPU pose 求值、GPU 蒙皮、動畫、骨架、材質、section、貼圖、陰影、解析度或更新頻率。不等同 GPU pose 遷移，也不會把 100 個不同網格合成一個 draw。

已建立 C000–C099 的 `InstancingBonesR1` 版本：505 個 mesh、10,882,117 個渲染頂點。建置後的頂點位置、法線／切線、骨骼索引對應及權重與 PackedRig 一致，材質與 section 保持一致。所有原資產保留；completion manifest 在各資源驗證／保存後才發佈。`Saved/Crowd/InstancingPrepared100R2.json` 記錄全部 100 張結果。

遊戲可用 `CrowdRoom-Instancing.cmd` 開啟；原 `CrowdRoom.cmd` 與未指定實驗版本的啟動保持既有行為。亦可執行：

```powershell
./Tools/run_crowd_room.ps1 -Mode Play -Quality InstancingBonesR1 -TaskWorkers 8 -ParallelEndFrameUpdates 1
```

## 測試方法

`Tools/test_crowd_instancing.ps1` 使用同一個 `D:/UE5.8/CrowdRuntimeStable` runtime，依 Off1／On1／On2／Off2 執行。每次全部載入 100 人後暖機 10 秒，45 秒時退出；約 35 秒用於取樣。固定 Development Editor `-game`、1600×1000、8 TaskGraph workers、async end-frame mode 1、RHI width 8、VSync off、不限制 FPS，無詳細 trace。正式取樣期間沒有本 task 的資產建置工作。

每輪都要求 selected=loaded=animated=100、failed=0、至少 120 個 warm samples、gpu_pose=false，以及 mesh 開关狀態與要求一致。原畫質、LOD 與動畫更新頻率不變。外部其他 Editor 保持開啟，背景負載與動畫相位存在變動；因此使用兩次重測，不能拿過往最佳單次結果做這次對照。

結果將保存於 `Saved/Crowd/InstancingABR2.json` 與各輪 BenchmarkInstancingABR2_*.json／log／launch.json／png。

## 骨骼資料的解讀

建置檢查記錄 section bone references：PackedRig 284,007，開啟版本 9,644,695。這個總數是各 section 的 metadata 重複數，不能當作 GPU 常駐容量或每幀上傳量。UE 5.8 的 `SkeletalRenderGPUSkin.cpp` 支援共用 `FUnifiedBoneBuffer`；`SkinningSceneExtensionProxy.cpp` 在 unified mode 只採第一個有效 section 的 bone map。實際效果須看 CPU／Render Thread／GPU 的量測。

單人畫面確認完整角色，既有的過亮材質問題仍存在。GPU pose 的 GpuMatrixR3 路徑尚未通過幾何插值門檻，仍是另一個 opt-in 實驗，未納入本次開關測試。

錯誤與排查記錄見 `Docs/CROWD_GPU_POSE_ERRORS.md`。
