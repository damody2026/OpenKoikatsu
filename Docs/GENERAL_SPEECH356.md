# Stage356：一般角色對話語音

已實作一般角色說話及音量嘴型。預設來源為 `C:/portable/KoikatuSunshine/voice_extract`；沒有實作性愛動作與呻吟／色情台詞的連動。未修改既有配對播放或 Rust 物理邏輯，SESSION243 / CLI353 / CACHE108 不變。

## 使用

載入角色後，在動作欄的一般語音區選取一句話，按「說話」；「停止語音」停止目前角色。切換角色時依卡片性格重載列表。可修改語音根目錄再按「載入語音」。索引或音檔缺少時顯示錯誤；不替換成其他性格。目前一般話題語料是女角性格；原生 Player.voiceNo 會優先查 Voice.infoTable[-100]，此索引未提供對應男角一般台詞，因此男卡介面顯示沒有語音，避免套用女性性格。只使用 comm_lines.json 的 TopicTalkCommon_ / sun_tk_ 一般话題記錄及精確的角色性格 bundle。

## 通用實作與來源

- `KKSpeech` 按性格載入索引、以 Unreal Vorbis 解碼器讀取 OGG。單次解碼後由有限長度 ISoundGenerator 播放，播放速度不依賴動畫 phase。壓縮檔上限16MiB，PCM上限64MiB；支援單／雙聲道、8–96kHz，解碼錯誤會回報。
- 每個 AKKPreviewActor 擁有獨立 AudioComponent、SoundWave、PCM 及原子 meter。新請求先驗證／解碼；失敗保留原播放。替換建立新的 component，完成回呼只處理自己的 component，避免舊回呼清掉新語音。停止、EndPlay、Destroyed、重載會清理。
- 讀取 ChaFileParameter.voiceRate，預設0.5，pitch 使用來源 Lerp(0.94,1.06,voiceRate)。無效 rate 回預設、超範圍夾限。
- Wave 與生成器採裝置取樣率，原PCM用線性插值轉換；避免44.1／22.05kHz直接當48kHz播放，也避免 Wave rate 和 generator resample 重複修正速度。
- `FBSAssist.AudioAssist` 的 RMS fallback：1024個第一聲道樣本，RMS×3後夾0–1；下降20/80、上升50/50，previous保存原始夾限音量。停止時不更新 previous；重新載入角色／語音設定才重置。語音播放期間由 EffectiveFaceRate(0) 接入既有 CPU/GPU 嘴型／口腔顯示，結束恢復手動嘴型設定。
- Gallery 顯示開始、停止、自然結束及錯誤狀態。

反編譯來源位於 `C:/portable/KoikatuSunshine/kks_decomp/Managed/CharaStudio_Data/Assembly-CSharp`：`ADV.Commands.Base/Voice.cs`、`ChaControl.cs`、`FBSAssist/AudioAssist.cs`、`ChaFileParameter.cs`。一般ADV使用卡片voiceNo/pitch、播放後 SetLipSync；本輪移植對應的一般手動對話與 RMS fallback，不是完整ADV腳本執行器。

## 驗證

最終 BuildR9 成功（5.57秒）；Speech356UE_R7 六項 Success，actual exit0：

1. CatalogCorpus：40種性格，每種29句，共1160 OGG全部解碼，179,543,515個原PCM幀。明細 Saved/Tests/Speech356/Corpus.csv。
2. Core：索引／失敗處理、真OGG、mono/stereo、44.1/48kHz一秒及半秒seek、第一聲道RMS、有限EOF、獨立meter、來源平滑公式。
3. Playback：實際48kHz WASAPI裝置消耗PCM，嘴型變動、EffectiveFaceRate接入、手動值恢復；自然結束、失敗請求保持舊播放、替換、停止、其他actor隔離與Destroy清理。
4. SpeechProfile：缺少rate默认0.5；-2/0/.25/1/2讀取、夾限及性格保持。
5. NativeWeights：既有原生嘴型權重回歸。
6. SharedDeformation：既有共用變形回歸。

同一7.532063秒一般對話的實際播放時間：pitch1.00為7.554564秒（預期7.532063）；.94為8.028035秒（預期8.012834）；1.06為7.118719秒（預期7.105721）。每次device生成361,540幀。測試使用普通話題 sun_tk_32_000，未載入互動場景。編輯器 latent 測試音源設為 UI sound 以避免非PIE暫停策略；產品音源保留一般 game audio 暫停語意。

重跑 `Tools/run_general_speech356.ps1 -Tag <新tag>`；不要加 -nosound。原失敗建置／測試保留，詳 ERROR_LOG E234。Corpus.csv為可重跑明細，重跑前保留既有封存檔；不覆寫歷史驗收 seals。

## 實際限制

meter 是生成器轉換後、進入 mixer 前的1024樣本，並非 Unity AudioSource.GetOutputData 最終輸出；裝置／音效／pitch重取樣後的包絡不保證逐樣本一致。音訊預取也可能造成幾十毫秒嘴型提前。本輪沒有 wavInfoData 預錄包絡、音素識別、完整遊戲ADV自動對話排程或實際角色畫面逐幀聲畫對照，因此不宣稱完整原生嘴型 parity 或全面零bug。所有1160音檔確認可解碼，實際装置端到端測試使用代表性普通對話。
