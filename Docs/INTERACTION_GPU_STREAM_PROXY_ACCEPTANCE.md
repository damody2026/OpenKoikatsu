# GPU stream scene proxy 發布驗收

## 本輪實作

`UKKAffineProceduralMeshComponent::SubmitGpuStreams` 接收 owned source／draw mapping／palette／post correction，game thread 移交 render thread，非阻塞 receipt 在所有結果寫完後才發布 atomic status。只有 install 包含完整 source，更新只含 palette；不持有 native 借用指標，不在工作執行緒操作 UObject／FFI。

scene proxy 使用 resident deform＋pack，再以 RDG GPU-to-GPU copy 寫入原 stock position／tangent buffers。沒有 CPU pack 或正式 readback，沒有換掉原 LocalVertexFactory／typed SRV／material／UV／color／indices；GPU copy 後轉為 vertex／graphics SRV 可讀。保留原光柵 draw／shadow 路由。ray tracing 尚未驗證時明確拒絕 GPU publication，維持 CPU renderer。

API 限定 ready、collision-free、animated proxy。caller 提供 finite conservative local bounds；提交前擴張至舊 bounds＋新 bounds，拒絕或 stale 工作不能縮掉目前有效幀。此 API 不生成 CPU query vertices；實際角色停用 CPU geometry 前，仍必須由來源／query owner 整合 bounds 推導與 CPU 按需 skin。

process-wide GT-only proxy epoch 隔離不同 actor components、proxy rebuild；resident generation 隔離來源替換與 CPU takeover。CPU section update 使 GPU resident generation 失效，後來的舊 palette 不能覆蓋 CPU 新幀。失敗保留最後有效 GPU buffers／immutable streams。receipt 的 Generation 在 Published 時是成功來源 generation；GT admission 拒絕時只能報告 request／目前 proxy 身分，不能當作 RT state 查詢介面。

## 編譯與完整回歸

`Saved/InteractionGpuStreamProxyFinalBuild.log`：Result Succeeded、8.07秒。直接 module dependency 與 plugin descriptor dependency 都已加入。`GpuStreamProxyFullRegression`：23/23 Success、exit0、四個 DLL hashes stable、external Unreal0；包含原 stream upload、shared／live native affine palette、GPU deform／residency／19,907 packing vertices、mouth／speech 等回歸。

## 實際 scene proxy／raster 證據

`OpenKoikatsu.AnimationCatalog.GpuStreamProxy` 以實際 registered component 建立3幀、13,000頂點（position／tangent超過64KiB）。原 LocalVertexFactory buffers 的 GPU readback 驗證 position／packed tangent、四组UV、color、indices 一致，material pointer 保留。GPU 更新不走 CPU vertex regeneration；CPU query vertices 保持原值。

測試涵蓋：finite conservative bounds 包含每幀可見頂點；非法 palette、錯誤 generation／proxy epoch 不覆蓋最後幀；不同 character component 的 epoch 不同且不能接收另一個角色 source；CPU takeover 正常、拒絕舊 generation、重新安裝後正常；proxy rebuild 改 epoch、恢復原 CPU authored streams、拒絕重建前工作。

actual raster capture 256×256，**65,536 pixels、578 visible pixels、GPU／CPU pixel_mismatches=0**。same-pose CPU reference 由原 stream renderer 產生，不是複製 GPU 模擬結果。原圖 `Saved/Tests/GpuStreamProxy/GPU.png`、`CPU.png` 已逐張 view_image 確認：灰色三角形可見、畫面一致，並非空畫面相等。這證明新 API 的 GPU output 確實由原 LocalVertexFactory 消費並繪出。

此 raster fixture 使用共用 default surface material；不能代替 APNG 衣髮／outline／toon material／shadow 的正式新路徑圖像驗收。新衣髮 shadow oracle 尚未完成，原路由保留不等於已測完所有新角色畫面。

## 剩餘場景整合

APNG 實際角色尚未呼叫新 publication API。下一步需 native immutable source lease／generation（避免每幀全 source validation）、private part source／secondary draw capture、同 palette conservative bounds、CPU 按需 query、背景準備與 receipt 成功後原子停止該 part CPU generation；仍保留 unsupported eyes／mouth／face morph 的原路徑與全部部件。

所有 source runtime 從 APNG／共用資源建立，不依賴角色專用 prebake。尚未宣稱120FPS、8ms或所有背景卡頓300ms達標。水槍功能維持，本輪不量測。

## 本版最後完整 APNG 功能驗收

`PairEyesSpeedGpuProxyFinal`：audit_completed=1、functional_pass=1、exit0、四個 DLL hashes stable、external Unreal0。APNG entry 6111.411ms、first ready walking8076.627ms、walking兩輪仍播放；first ready pair71.039ms。12次ready switches最大click-to-play17.216ms、最大frame33.401ms，300ms切換門檻PASS。

13速度階段（含pause／continuous）原CPU／frame budget全部PASS，68次調速callback最大0.059899ms。54嘴型、語音與取消／替換、shared clock、head／eyes／belly、male replacement及失敗保留、return、原paired shadow depth oracle，完整功能PASS。五張原PairEyes PNG已逐張view_image確認虹膜／highlight／眼球、閉眼與重新睜眼及播放正常，eye-visual-review.json保存；stat fps可見。

混合階段mean6.711ms、最大1314.143ms；播放speed phases whole mean9.75–10.18ms，pause8.619236ms。背景最大幀仍未达300ms，8ms／120FPS未完成；不把混合平均或paused截圖133–137FPS當穩態播放證明。原APNG scene仍未使用新private GPU route，本輪不宣稱FPS收益；新proxy圖像oracle與APNG原route功能驗收是分開的證據。水槍SkipPaint。
