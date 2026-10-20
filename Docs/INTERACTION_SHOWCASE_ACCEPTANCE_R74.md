# R74 當前功能驗收與效能結果

當前功能已驗收通過；平均 ≤8 ms／120 FPS 尚未達標，不標記整體效能目標完成。水槍依使用者指示跳過。

## 當前實作

- 增加 blocking generated-stream source job API 與 exact CPU/GPU 回歸；unsupported/duplicate requests 在 callback 前拒絕。
- 實作 source conversion 與 GPU packing 批次融合及 parity，但同 binary A/B 沒有收益，`ok.Animation.FusedSourceStreams=0` 為預設。禁止把實驗稱為性能改善。
- 修正 snapshot bypass/re-enable 的過期姿勢判定：A→停用時更新 B→再啟用 A 必須重新提交。
- 修正角色清理順序：先銷毀骨架／嘴型 follower，再移除 persistent leader asset，避免男角替換／返回場景的 LeaderBoneMap ensure。
- Audit launcher 在驗收失敗 throw 前保存四 DLL after hashes、退出碼及外部 UE/isolation evidence，避免丟失診斷資訊。

## 最終當前 binary 驗收

四模組 Build `InteractionBuildR74TeardownR3.log` PASS，16.29s。`PairRegressionR74TeardownR3` 13/13 PASS，包含實際註冊／附掛的 follower teardown、重複清理、source snapshot mode transition、GPU exact quantization/UV/color/bounds、語音、嘴型、角色選取與鏡頭。

完整 APNG + PairEyes + PairSpeed + SkipPaint：`Saved/Tests/InteractionShowcase/PairEyesSpeedR74TeardownFinal/report.txt`：

| 項目 | 結果 |
|---|---|
| audit_completed / functional_pass | 1 / 1 |
| APNG → 可操作場景 | 6529.626 ms（<10s） |
| first prepared pair commit | 77.668 ms |
| 12 次已準備姿勢切換 click-to-play 最慢 | 19.573 ms |
| 切換最大 frame stall | 33.230 ms（<300ms） |
| 嘴型 / 實際語音觀測 | 54 種 / PASS |
| 行走超過兩週期仍循環 | PASS |
| 13 個速度階段 CPU/full-frame 相鄰 baseline 門檻 | 全部 PASS |
| 68 次速度調整 callback 最慢 | 0.062600 ms |
| 男角替換與失敗保留、shared clock、pause/restart | PASS |
| live eyes/head/mouth/belly、return/reentry | PASS |
| stable deferred shadow depth pixel oracle | PASS |
| 五張雙人眼睛 normal RHI 截圖人工驗證 | PASS |
| 水槍 | watergun_validation_skipped=1 |

最後 runtime log 無 LeaderBoneMap ensure、fatal 或 Unhandled Exception。功能測試載入期間觀測到外部材質烘焙 UE，因此不使用混合功能階段的 FPS／平均作 120 FPS 證據；截圖與 oracle 的同步 readback 也不是穩態 frame budget。

## 最終隔離正常 RHI 效能

`Saved/Tests/InteractionShowcase/PairProfileR74TeardownFinal/report.txt`，同一最終 binary、無 trace、可見 1280×720、雙人完整原功能、paint hits 0：

| 項目 | 姿勢 0 | 姿勢 1 |
|---|---:|---:|
| 全幀平均 ms | 9.569357 | 9.572967 |
| 實際 Present FPS | 104.501234 | 104.458581 |
| PairAdvance 平均 ms | 6.671164 | 6.691910 |
| GPU 平均 ms | 1.365668 | 1.364470 |
| 最大 frame ms | 17.618399 | 19.726001 |

APNG entry 5953.430ms、first prepared pair205.979ms。`external_unreal_observed=0`、`performance_evidence_isolated=1`、`module_hashes_stable=1`；功能與 profile 四個 DLL hashes 完全相同。`120fps_present_verified=0`。當前瓶頸仍屬 CPU；不能把 CPU callback 時間、混合單人/雙人平均或瞬間 screenshot FPS 當達標。

## 保留的失敗與未完成項目

- R74 fusion ON 10.243/10.239ms；同 binary OFF10.051/10.147ms，未採用 ON 作預設。
- opt-in parity 第一次完整 speed audit 在 phase7 出現持續 CPU/full-frame 升高，測試失敗保留；後續 default 全階段 PASS 不覆寫或聲稱其根因已證明。
- CacheBoundary profile 曾一次 Renderer SetLooseParameters access violation，後續 retry及兩輪最終 runtime 未重現。follower teardown ensure 是另一个已定位並修正的問題，不宣稱該修正已證明消除 Renderer crash；crash log/dump 留存待追蹤。
- 維持 runtime APNG 資料、共用資源與背景準備；未建立角色特化預計算資源。未準備的姿勢仍需背景準備，300ms 切換結果限於已準備姿勢。
- 下一個待實作通用方向：source-indexed jobs 共用一次 canonical 轉換與 normal quaternion 給所有 section draw，再 exact 打包及彙總 bounds；具體 API、依賴與驗收計畫見 INTERACTION_SHOWCASE_PLAN.md。

所有編譯／測試設定錯誤、崩潰及失敗量測記錄於 INTERACTION_SHOWCASE_ERRORS.md；根因 trace 分析保留於 INTERACTION_PAIR_PROFILE_ROOT_CAUSE.md。
