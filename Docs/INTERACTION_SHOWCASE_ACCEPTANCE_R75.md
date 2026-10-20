# R75 來源索引串流實作與驗收

目前新路徑的逐值與功能驗收通過；平均每幀 ≤8 ms／實際 Present 120 FPS 尚未達標。新路徑沒有性能收益，預設關閉；不將排程等待下降當作全幀改善。水槍依使用者指示排除驗收。

## 實作

- 通用 blocking `UpdateGeneratedStreamsBySourceRanges` API：先驗證所有 section requests，再執行 caller 提供的來源工作；來源 job 完成自己範圍的 canonical 轉換後，立即產生對應 section 的互不重疊 draw 範圍。
- 保留同幀 CPU canonical／section vertices、既有法線與 tangent 精確打包、UV／color、bounds、眼睛／morph／物理／shadow；不建立卡片特化的預計算資產。
- compact draw 用排序來源索引的 lower-bound 找到範圍；不符合 private source 條件的資料仍走既有轉換。request 被拒絕時先補完 deferred canonical 來源，再走原 fallback。
- 排程參與者有界，預設最多 8（含 GT），保留 serial／parallel、粒度與動態派工開關供比較。`ok.Animation.SourceIndexedStreams=0` 為最終預設；實驗 ON 不代表採用。
- 新增 serial／parallel、13 disjoint source jobs、4 components 的 GPU packed streams／bounds／duplicate request rejection 回歸，以及 runtime indexed draw／normal rotation exact parity。

## 最終編譯與回歸

四模組 `InteractionBuildR75Final.log`：Result Succeeded，16.39 s。`PairRegressionR75Final`：13/13 PASS；退出碼 0、四 DLL 前後 hash 穩定、外部 UE=0。

## 最終 binary 的 opt-in 正確性驗收

`PairEyesR75Parity`：APNG、normal RHI、visible 1280×720、SkipPaint；SourceIndexedStreams=1、SourceIndexedParity=1、NormalMotionParity=1。

- `audit_completed=1`、`functional_pass=1`；APNG entry 6200.590 ms（<10 s）；first prepared pair commit 79.361 ms。
- 實際 log：`OK source indexed parity vertices=71185 source_jobs=195`。這是實際 private 來源工作，並非只有 API unit test；檢查開啟期間沒有 parity assertion。
- 54 種嘴型、語音活動、行走兩個循環、paired shared clock／pause／replay、live head／mouth／belly、男角替換與失敗保留、返回／重新進入、stable shadow depth pixel oracle 通過。
- 12 次已準備姿勢切換全部通過既有 300 ms 門檻。五張 PairEyes screenshots 已逐張檢查，虹膜、閉眼／重開、tracking 及播放幾何正確；記錄 `eye-visual-review.json`。截圖 stat overlay 不作 FPS 證據。
- 退出碼 0、四 DLL hash 穩定、外部 UE=0；沒有 leader-map ensure、fatal 或 Unhandled Exception。`120fps_present_verified=0`；開啟 parity 會額外重算參考資料，這輪不作性能證據。

## 效能實驗與採用決定

| 實驗 | quiet 全幀 ms（兩階段） | 實際 Present FPS（兩階段） | 結論 |
|---|---|---|---|
| R75 1024／balanced ON | 9.934021 / 9.878292 | 100.664264 / 101.228523 | 較同 binary OFF 慢 |
| 同 binary OFF | 9.404828 / 9.544249 | 106.331495 / 104.775510 | 不達 8 ms／120 FPS |
| R75Scheduling 256／Unbalanced | 17.253484 / 12.883458 | 57.950775 / 77.619617 | 未採用；phase1 最大 653.626 ms，根因未證實 |
| R75Bounded 最多 8 參與者 ON | 10.286079 / 10.818219 | 97.220070 / 92.436360 | GT wait 降至約 0.048 ms，全幀仍未改善 |

上述實驗均保留正常 RHI、external UE=0、各自四 DLL hash 穩定的證據。只有前兩行是完全同 binary A/B；後續修改排程後重編，不能冒稱同 binary。external UE=0 不代表沒有其他 OS 排程噪音。

源码複核確認 baseline 已每 canonical source 轉換一次，且共用真正重複 draw 的 normal rotation。R75 只是修正 R74 experimental direct draw 的重複轉換，並沒有比實際 baseline 減少全部 draw 生成／packing／CPU 寫入／bounds 工作。等待改善不能取代 wall-time 與 Present 驗證。

## 後續架構與風險

Native private skin 完成 worker 上同步產生 display packets 的兩階段 GT prepare／publish 架構，已列入計畫但**尚未實作**。須保留 exact math、canonical 查詢、影子更新順序及角色替換的 callback lifetime；不得跳過眼睛、物理或降低畫質換取達標。

R74 一次 Renderer `SetLooseParameters` crash 保留於錯誤紀錄。後續未重現不代表已證實修復，不能將 follower teardown 的不同問題當成它的根因。

預設路徑速度初輪 `PairEyesSpeedR75Final` 未通過：4倍 phase7 advance 9.352392 ms／frame 13.209806 ms，相鄰基準 7.669850／10.878113 ms，超過原 1.15×+0.5 ms 門檻；其餘倍率通過。共享時鐘倍率正確、69 callback 最大0.041198 ms，但整輪 `audit_completed=0`，不能當成完整成功。外部 UE=0、hash穩定、沒有 ensure/fatal。保留失敗，不放寬門檻；沒有足夠證據歸因 OS 噪音。程式檢查每幀僅一次 paired frame，Speed只改elapsed，物理使用未乘倍率的delta。

## 預設路徑同 binary 完整功能複測

`PairEyesSpeedR75FinalRetry`：audit_completed=1、functional_pass=1。與 parity 及初輪失敗測試的四 DLL hashes 完全相同，前後穩定；退出0、externalUE0、performance_evidence_isolated=1。

| 項目 | 實測 |
|---|---|
| APNG → 可操作場景 | 6099.416 ms（<10 s） |
| first prepared pair commit | 69.321 ms |
| 12 次 prepared 切換 click-to-play 最大 | 19.729 ms |
| 切換 frame stall 最大 | 36.000 ms（<300 ms） |
| 播放速度 | 13 phases，全原 adjacent CPU／frame budget PASS |
| 速度控制回呼 | 69 次，最大0.050500 ms |
| 4倍速 phase7 | clock4.000000；CPU7.320260、frame10.442578 ms；相鄰基準7.785101／11.048011，PASS |

54嘴型／語音、walking loop two cycles、共享時鐘／pause／restart、眼睛與頭部控制、live mouth／belly、男角替換／失敗保留、返回與重入、shadow pixel oracle 全通過。五張本輪眼睛 PNG 逐張人工檢查正常，已保存 eye-visual-review.json；無 ensure／fatal／Unhandled Exception。

前次4倍階段失敗此次未重現，**仍保留失敗，不宣稱根因已確定或問題已修復**。速度功能和本輪門檻通過，不等於每次執行無量測波動。`120fps_present_verified=0`；水槍 skipped。

## 最終同 binary 正常 RHI 全幀量測

`PairProfileR75Final`：visible 1280×720、APNG、正常 RHI、無 trace、預設 SourceIndexedStreams=0；audit_completed=1、pair_profile_completed=1。profile 模式的 functional_pass=0 是模式定義，完整功能另由上述功能測試驗證。

| quiet 階段 | 全幀平均 ms | PairAdvance ms | 實際 Present FPS | GPU ms | GT wait ms | 最大幀 ms |
|---|---:|---:|---:|---:|---:|---:|
| 0（1599 samples） | 9.433605 | 6.623726 | 106.005521 | 1.450102 | 0.095781 | 15.862402 |
| 1（1582 samples） | 9.479136 | 6.654700 | 105.498206 | 1.382820 | 0.098475 | 11.248000 |

APNG entry5973.835 ms、first pair187.623 ms。externalUE=0、performance_evidence_isolated=1、四 DLL前後hash穩定；與最終 regression、parity、full retry 的四 hashes 全部相同。沒有 ensure／fatal／Unhandled Exception。兩 quiet 階段 paint hits 均0；第三階段為額外 native 日誌診斷，不作穩態 FPS 證據。

**當前功能與切換門檻通過；平均≤8 ms／實際120 FPS 未通過。** 不能用PairAdvance6.62ms、截圖statFPS或診斷phase代表全幀達標。此版沒有採用性能退步的新路徑；後续 native completion observer 架構仍未實作。

計畫、實作錯誤及失敗實驗分別保存於 `INTERACTION_SHOWCASE_PLAN.md`、`INTERACTION_SHOWCASE_ERRORS.md`；完整原始證據位於 `Saved/Tests/InteractionShowcase` 對應 tags。
