# Runtime 私有蒙皮來源版本驗收

日期：2026-10-07。範圍：衣髮/配件 immutable rest source、版本限定 staged palette API；尚未啟用實际 APNG 衣髮 GPU caller。

## 實作

`AccessoryRuntime` 來源改為跨模組唯讀。第一次取得有效來源時驗證原始位置、方向、權重有限值及 authored bone index 範圍；相同來源與 palette 槽數的後續查詢直接使用快取。每次仍檢查當前骨骼矩陣與 post correction 有限值。

版本是 native 模組 runtime 唯一、非零的 u64，不使用指標或檔名，不写入 APNG/snapshot。clone、deserialize、來源替換/資源抽離及 palette 槽數改變清除/重建快取；回到舊槽數不能使舊版本復活。一般姿態/速度/placement 不改變原始頂點與骨骼槽來源。

新增 C/UE API `PrivateSkinSourceRevision`、`PrivateSkinStagesRevision`，保留舊 API。讀取拒絕過期版本，沿用有限次容量重試與原子輸出；Close/move 保持 native 所屬權。

本 API 回傳的 source view 仍只可在有效 session 的目前呼叫期間使用。GPU caller 必須先複製來源到 owned buffer，再保存 revision；下一幀傳版本限定的矩陣更新。這不是允許在 native session free 後使用 Handle 的 lease。

## Native 實際驗收

- 生命週期單元測試通過：20 次暖讀仍僅一次來源掃描、clone/序列化還原/換來源/換槽數版本失效、非法 source 快取、signed influences、未加權槽與非有限值拒絕。
- 女性 C001 APNG：111 part checks、133,440 頂點、37 FFI parts、296 次版本讀取；每來源/槽數只掃描一次，與原始 renderer 最大誤差 0。
- 男性 InteractionMaleAPNGR65 APNG：102 part checks、174,294 頂點、34 FFI parts、272 次版本讀取；每來源/槽數只掃描一次，最大誤差 0。
- 原始 renderer 總計 307,734 頂點；568 次暖讀沒有重掃高模權重。來源替換與 clone 版本不同、錯誤輸出保留驗證通過。
- Null/零 revision/負 index API 拒絕測試通過。
- 女性既有一個來源缺少完整方向串流（3 frame checks）；不移除幾何，GPU caller 仍須用已驗證的方向來源或保留 CPU。

證據：`Saved/InteractionPrivateSourceRevisionUnit.log`、`Saved/InteractionPrivateRevisionFemale.log`、`Saved/InteractionPrivateRevisionMale.log`、`Saved/InteractionPrivateRevisionInvalid.log`。既有 60 個 Rust warnings 非此功能失敗。

## 整合驗收

Unreal 編譯與回歸已完成，結果如下。完整雙人場景驗收另於完成後追加。不得將本快取的成功當成實際衣髮 GPU 啟用或整體 120fps 達標。

Unreal 編譯成功：`Saved/InteractionPrivateRevisionBuild.log`，Result: Succeeded，74.32 秒。完整回歸 `Saved/Tests/InteractionShowcase/PrivateSourceRevisionRegression`：23/23 PASS、exit 0、四模組 SHA256 穩定、external Unreal 0。

`LiveNativePalette` 在實際女性/男性 APNG 的三次 native 動畫更新取得 111/102 staged publications；來源 revision 在播放過程穩定。錯誤版本、ownership move、Close 保留 staged 輸出/拒絕失效 session 全部通過。staged 對原始 flatten 的最大誤差分別 5.62796578e-6 / 6.16132803e-6 cm。

GPU 核心與串流回歸通過：19,907 頂點 LocalVertexFactory packing byte_mismatches=0；resident immutable input 僅一次上傳、三次 palette 更新；實際 scene proxy GPU/CPU raster oracle 通過。


## 最後完整雙人場景驗收（目前 native/原 renderer 路徑）

`Saved/Tests/InteractionShowcase/PairEyesSpeedPrivateRevisionFinal`：audit_completed=1、functional_pass=1、exit 0、四 DLL hashes stable、external Unreal 0。水槍 SkipPaint。

- APNG entry 5,974.985ms、first ready walking 7,840.792ms；10 秒 entry 與 walking 兩輪繼續播放 PASS。
- First ready pair 72.340ms；12 次 ready switches 最大 click-to-play 18.303ms、最大 frame 31.269ms，300ms 切換門檻 PASS。
- 13 個速度階段（0.25/0.5/1/2/4 倍、continuous、pause）相鄰 CPU/frame budget 全部 PASS；68 次 UI requests，callback 最大 0.040401ms。
- 54 嘴型、語音/取消/替換、男女 shared clock、切換/暫停/重播、head/mouth/belly、male replacement/失敗保留、返回、既有 paired shadow depth oracle PASS。
- 五張 PairEyes（Neutral/Tracking/Closed/Reopened/Playing）逐張 view_image，虹膜/highlight、tracking、閉眼、恢復睜眼與播放眼睛正常，stat fps 可見。SHA256 與判讀保存在 eye-visual-review.json。

尚未達標：playing speed phases whole frame mean 9.628789–10.079022ms，pause 8.464134ms；混合平均 6.570ms 不能代表 steady pair playback。背景最大幀 1,278.791ms >300ms。120fps_present_verified=0。actual APNG private GPU route 仍未啟用，故不宣稱本階段已有 FPS 收益。

本輪目前來源版本功能實作與驗收成功；完整性能目標仍需接入 GPU/按需 query/bounds/發布協調後量測。
