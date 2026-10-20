# Stage357：普通語音套用實際嘴型部件

本輪補驗 Stage356 未確認的角色網格接線；未新增卡別修補，沒有改語音／嘴型產品邏輯，也未改 Rust SESSION243 / CLI353 / CACHE108。依使用者確認，遊戲男角無聲，現有男卡介面維持不播女聲。沒有實作性愛語音連動。

## 驗證方式

`KKSpeechDisplay.spec.cpp` 使用原普通話題 sun_tk_32_000.ogg 解碼及48kHz generator包絡，載入兩個 KK_226176 outfit0 實際角色。每個取樣點由 Actor.Tick(0) 驅動 SpeechOpen，之後 RefreshSkinnedDisplay，與同一角色現有手動嘴型通道的實際網格／權重／可見性比較。第一及最後嘴型各45點，共90點；這是共用嘴型接線測試，卡片只是fixture，不加卡片條件。

manual通道不是獨立原生oracle；Stage356的 NativeWeights / SharedDeformation 另以遊戲fixture確認FBS既有計算。Stage356實際裝置Playback確認PCM消耗、結束及pitch速度；本輪專門隔離renderer接線，測試世界無AudioDevice、以生成器直接取樣，未冒稱同時聲畫錄影。

## 最終結果

BuildR3 Succeeded6.34秒。CPUR3與GPUR3各TestCompleted Success/actualexit0：

|模式|GPU身體|CPU嘴型slot|GPU嘴型slot|pattern／step|頂點觀測|語音對manual最大誤差|相對rest最大移動|
|---|---:|---:|---:|---|---:|---:|---:|
|CPU|0|5|0|2／90|563040|0 cm|2.235378925 cm|
|GPU／混合|1|5|0|2／90|563040|0 cm|2.237906234 cm|

兩actor隔離、mouth可見性、非有限值檢查、停止恢復manual .2、一般姿勢刷新保留嘴型皆通過。全CPU與GPU／混合模式是分別對自己的manual參考，不是跨模式全頂點parity。身體模式有實際可見GPU body component正控制，不能只憑環境變數判斷。嘴型slot的source skel_kind皆0，按既有LoadSession條件使用CPU是預期；沒有實際GPU mouth slot，因此本輪 **GPU mouth morph RHI／最終像素未驗收**。不能將GPU身體模式PASS寫成GPU嘴型PASS。

在相同最終DLL上重跑 Stage356 六項普通語音／profile／FBS回歸，使用Speech356UE_R8新tag；六項全部Success/actualexit0；1160檔再解碼通過。三pitch 1/.94/1.06實際7.546559/8.027159/7.110938秒，對應預期7.532063/8.012834/7.105721秒。結果見Audit357與log。聲畫時差、Unity最終output包絡逐樣本parity、wavInfoData、音素識別／隨機嘴寬與完整ADV腳本排程仍不屬於這次驗收，沒有全面無bug承諾。

## 重跑

1. 先建置 OpenKoikatsuEditor，建置失敗不得繼續用舊DLL驗收。
2. `Tools/run_speech_display357.ps1 -Cpu -Tag <新tag>`。
3. 上一程序結束後，`Tools/run_speech_display357.ps1 -Tag <另一新tag>`。
4. `Tools/run_general_speech356.ps1 -Tag <新tag>`：明列六項headless tests，新增RenderedMouth須由full-RHI renderer runner執行，不能加入NullRHI prefix。

兩個renderer測試串行，保存log/stdout/stderr與actual exit，runner同時檢查TestCompleted Success及來源是否比DLL更新。保留Stage356 Audit及所有歷史失敗，新增Saved/Tests/SpeechDisplay357/Audit.json；不要重封歷史seal。
