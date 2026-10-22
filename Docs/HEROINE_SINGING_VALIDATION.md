# 女主唱歌：實作與驗收紀錄

日期：2026-10-23。對應計畫：[HEROINE_SINGING_IMPLEMENTATION_PLAN.md](HEROINE_SINGING_IMPLEMENTATION_PLAN.md)。

**目前狀態**：P0～P3 完成，三個模型階段（產歌、分離、換聲）都已改成原生 Rust + CUDA，遊戲執行時不需要 Python。每個模型都和上游 Python 逐層比對過數值（第 5 節），並在遊戲中實際跑過完整流程（沙盒、Conquest182 stage + 原生 ok-song）。音色品質尚未經使用者評分，不能標為通過。

## 1. 架構與檔案

| 部分 | 位置 | 說明 |
|---|---|---|
| 聲線匯出 | `Tools/ok-tts` `voice-export` | 唯讀；卡內 `okVo` 優先，否則用性格預設 profile，與對話合成同一優先序；輸出 24 kHz 參考音與 `voice.json`（embedding tag、參考音雜湊） |
| 歌曲工作服務 | `Tools/ok-song`（Rust） | `doctor`／`generate`／`serve`／`status`／`cancel`／`mix`；HTTP `/health`、`/jobs`、`/jobs/{id}`、`/jobs/{id}/cancel` |
| 產歌 | `ok-song/src/native/mula.rs`、`codec.rs` | backend `heartmula-rust`：HeartMuLa-oss-3B（Llama-3.2 3B 主幹＋3 層解碼器，bf16，CFG 3、top-k 50），HeartCodec（流匹配 10 步＋scalar 解碼器，f32，48 kHz）；種子由歌詞、標籤與長度決定 |
| 分離 | `ok-song/src/native/demucs.rs` | backend `demucs-rust`：htdemucs_ft 四個模型各取自己的音源，伴奏 = 鼓＋貝斯＋其他；7.8 秒分段、25 % 重疊、三角權重；不做隨機位移，結果可重現 |
| 換聲 | `ok-song/src/native/seedvc.rs` 與 `whisper.rs`、`campplus.rs`、`rmvpe.rs`、`dit.rs`、`bigvgan.rs` | backend `seedvc-rust`：Seed-VC F0 條件 44 kHz 模型（Whisper-small 內容特徵、CAMPPlus 聲紋、RMVPE 音高、長度調節器、DiT 流匹配 30 步 CFG 0.7、BigVGAN v2），不移調 |
| 權重準備 | `ok-song prepare-models` | 以 Rust 讀 PyTorch 檢查點（zip＋pickle）轉成 f32 safetensors；Whisper-small 直接複製 |
| GPU 層 | `ok-tts/src/gpu.rs`（共用）＋ `ok-song/src/native/*.cu` | cuBLAS GEMM、NVRTC kernel；ok-song 在 ok-tts 的 kernel 之後附加自己的 |
| 混音與嘴型 | `ok-song/src/audio.rs` | 多相位 sinc 重取樣、混音、BS.1770 響度（−16 LUFS）、4 倍超取樣真峰值（≤ −1 dBTP）、10 ms 人聲 RMS 包絡、OGG |
| 快取 | `Saved/StreetSocial/Songs/` | 歌曲與分軌由所有女主共用；換聲與混音依聲線分開；`DONE` 標記與 SHA-256 驗證才算命中；LRU 容量上限；原生版的修訂號（`heartmula-rust-1` 等）在快取鍵內 |
| 規則 | `Tools/ok-rules/src/song.rs` | `song_decide`（`Relation::liking` 跨階段排名、門檻、冷卻）、`song_lyrics`（依階段與時段選模板） |
| 遊戲事件 | `Source/OpenKoikatsu/OKStreetSocialSong.inl`、`OKStreetSocialSongState.h` | 準備 → 就緒 → 邀請 → 開場白 → 唱歌 → 結尾；面板開關、邀請按鈕、歌詞字幕、跳過 |
| 播放 | `OKCrowdRuntimeScene` `OKSpeakCrowdRuntimeSong` | 播放混音，嘴型依音訊渲染時鐘讀人聲包絡（不用混音 RMS），音高固定 1 |
| 啟動 | `run_street_social_development.lua` | `Build/ok-song/ok-song.exe` 以 base+20 埠隨遊戲啟動、`--parent-pid`；`OK_SONG_ENDPOINT`；沙盒記錄 `running.json`，一般啟動不會關掉執行中沙盒的伺服器 |

`-python` backend（上游 Python 入口）仍保留在同一介面後面，只用來對照。

## 2. 部署與重跑指令

本機部署：`Build/ok-song/ok-song.exe`、`Build/ok-song/cuda/`（cuBLAS、NVRTC DLL，與 ok-tts 相同）、`Build/ok-song/ok-song.json`（內容同 `Tools/ok-song/ok-song.example.json`）。權重：`E:/HeartMuLa/ckpt`（HeartMuLa、HeartCodec、tokenizer）、HTDemucs-ft safetensors（Hugging Face 快取）、`E:/OkSong/models-rs`（由下列指令產生）。舊版保留為 `ok-song.before-native.exe`、`ok-song.replaced-*.exe` 與 `ok-song.before-*.json`。

```bash
Build/ok-song/ok-song.exe prepare-models --seedvc E:/OkSong/seed-vc/checkpoints --out E:/OkSong/models-rs
Build/ok-song/ok-song.exe doctor
Build/ok-song/ok-song.exe generate --request E:/OkSong/test/req-march7.json
Tools/lua/lua.exe Tools/run_street_social_development.lua -Control -Sandbox VoiceTest -PortBase 48060 -RulesDll <自建 ok_rules.dll>
```

遊戲中測試（控制埠 48070）：`OK_CONTROL_PORT=48070 Tools/lua/lua.exe Tools/street_play.lua song state|prepare [女主編號]|play|later|decline|skip|cancel|enable|disable`。指定女主的 `prepare N` 會標示 `override`，正式自動事件不使用。

數值核對（需要上游 Python 輸出的參考檔，Python 只在開發時使用）：`ok-song mula-check`、`codec-check`、`demucs-check`、`seedvc-check --parts mel,voc,lr,est,cfm,sem,style,f0,pipe,conv`。

## 3. 實測結果（RTX 5090）

**原生版，單一 30 秒歌（離線，另一個遊戲同時在跑）**

| 階段 | 原生 Rust | Python 原型 |
|---|---:|---:|
| 產歌（HeartMuLa token＋HeartCodec） | 35 s（token 12 s，每幀 32 ms；解碼 7.6 s；其餘為載入） | 187 s |
| 分離（htdemucs_ft 四模型） | 11 s（計算 7 s） | 24～36 s |
| 換聲（Seed-VC） | 17 s（計算 11 s） | 53～99 s |
| 混音＋包絡＋OGG | 1 s | 同左（本來就是 Rust） |

**品質指標**

- 歌詞：Qwen3-ASR 辨識原生產歌分離出的人聲，與 Python 產歌的結果幾乎相同，歌詞完整唱出；換聲後仍可辨識（少數字錯，例如「晚風」聽成「狂風」）。
- 聲紋（Qwen Base speaker encoder，修正 float WAV 讀取後）：原生換聲對自己的參考聲線 0.987，對另外兩位女主 0.979／0.971，對原歌手 0.969；原生與 Python 換聲（同輸入）彼此 0.999。輔助指標，最後要人工試聽。

**遊戲（沙盒、Conquest182 stage、原生 ok-song）**

| 項目 | 結果 |
|---|---|
| 準備 | 歌與分軌已快取時只做換聲＋混音：53 s（GPU 同時跑兩個遊戲與兩組語音服務）；完全命中時 5 s 內就緒 |
| 唱歌 | 貞德·Alter（戀人）邀請 → 開場白 → 30 秒歌 → 結尾台詞 → 閒置；嘴型取樣 0.01～0.99 隨人聲包絡變化；底部字幕與面板顯示歌詞；畫面 90 FPS |
| 先前驗過（Python 版時） | 等一下／下次吧、中斷、跳過、自動事件門檻、存檔欄位，流程未改 |

## 4. 已知限制與未做項目

- **音色品質未經使用者評分**（計畫要求音色、自然度、清晰度各 ≥ 4／5）。
- **數值差異**：原生版與 Python 的差異來自 bf16 取捨（HeartMuLa，同分時 token 選擇不同）、Python 卷積用 TF32、重取樣器不同（ok-song 的多相位 sinc 對 librosa／torchaudio）、Whisper 在 Python 以 fp16 執行。各模型對同輸入都在 cos ≥ 0.9999 內（第 5 節），但隨機取樣讓整首歌不會逐位元相同。
- **伴奏**：與 Python 一樣是鼓＋貝斯＋其他三個模型的輸出，不是「混音減人聲」。
- **權重來源**：HeartMuLa、HeartCodec、Demucs 直接讀上游 safetensors；Seed-VC 四個 `.pth` 需先跑 `prepare-models`。下載權重本身仍要自行準備。
- **歌詞模板**：依關係階段與時段從 3 份固定歌詞中選擇；計畫中「填入共同經歷」的模板尚未做。
- **鏡頭**：沿用交談中的鏡頭，沒有專屬的演唱鏡頭與站立動作。
- **效能**：沒有做計畫中「遊戲負載下背景產歌時 frame time」的正式量測；目前一個實例一次只跑一個重型工作。每個階段各自建立 CUDA context 並在結束時釋放，HeartMuLa 約占 8 GB VRAM（載入 HeartCodec 前先釋放）。
- **預設關閉**：要在面板打開「女主唱歌」，或用控制通道測試。
- 開著房間麥克風時，喇叭聲可能被當成插話而打斷；測試時用 `street_play.lua mic off`。

## 5. 原生移植的數值核對

參考值由上游 Python 在同一輸入上輸出（開發用暫存腳本，不納入版控）。cos 為整個張量的餘弦相似度。

| 模型 | 核對點 | 結果 |
|---|---|---|
| HeartMuLa | 輸入嵌入、主幹 28 層每層輸出、最終輸出 | cos ≥ 0.999996 |
| HeartMuLa | 12 幀 teacher forcing 的 8 個碼本 token | 96 個中 88 個相同；不同處的引導後 logit 差為 0（bf16 同分）或 1 個 ulp |
| HeartCodec | 第一個 transformer block、估計器輸出、兩段 latent、波形 | 0.99999999、0.99999997、0.99999995、0.99998 |
| Demucs | 4 層編碼器、時域編碼器、cross-transformer、4 層解碼器 | 全部 ≥ 0.9999997 |
| Demucs | 整首四個音源 | 人聲 0.9999994，其他 ≥ 0.99992 |
| Seed-VC 梅爾 | 44.1 kHz log-mel | 0.9999999999997 |
| BigVGAN | 7 秒音訊 | 0.99994 |
| Whisper-small | 特徵、編碼器（對 Python f32） | 0.9999999999999、0.99999999 |
| CAMPPlus | kaldi fbank、192 維聲紋 | 0.999999999998、0.9999995 |
| RMVPE | F0 | 最大誤差 0.25 cent，清濁判斷 713 幀全同 |
| 長度調節器、DiT | 條件、估計器輸出、30 步 CFM | 0.99999998、0.9999999999997、0.9999999999996 |
| Seed-VC 串接 | 第一塊梅爾、第一塊波形 | 0.9999999999996、0.99997 |
| 權重轉換 | 以 `prepare-models` 產生的權重重跑上列 Seed-VC 項目 | 與 Python 轉出的權重結果相同 |
