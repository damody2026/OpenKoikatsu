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

遊戲中測試（控制埠 48070）：在 `OK_CONTROL_PORT=48070` 下用 ok-game-mcp 的 `game_call {path: "/song", body: {action: "state|prepare|play|later|decline|skip|cancel|enable|disable", girl: 女主編號}}`（[STREET_SOCIAL_GAME_MCP.md](STREET_SOCIAL_GAME_MCP.md)）。指定女主的 `prepare N` 會標示 `override`，正式自動事件不使用。

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
| 背景產歌時的幀時間 | 沙盒遊戲（另有攻略線遊戲同時執行）從遊戲日誌的時間戳與幀編號計算：產歌前 30 秒平均 8.4 ms（p95 11.5）；產歌中三個 3～4 分鐘區段平均 7.3／6.6／6.3 ms（p95 8.3／8.0／8.6，p99 ≤ 11 ms）。沒有因背景產歌掉幀 |
| 全新歌（無快取） | 同上條件：產歌 590 s（HeartMuLa 549 s，兩個遊戲都不限幀率全速渲染）、分離 15 s、換聲 31 s；之後已改為每幀一個 CUDA graph |
| 先前驗過（Python 版時） | 等一下／下次吧、中斷、跳過、自動事件門檻、存檔欄位，流程未改 |

## 4. 已知限制與未做項目

- **音色品質未經使用者評分**（計畫要求音色、自然度、清晰度各 ≥ 4／5）。
- **數值差異**：原生版與 Python 的差異來自 bf16 取捨（HeartMuLa，同分時 token 選擇不同）、Python 卷積用 TF32、重取樣器不同（ok-song 的多相位 sinc 對 librosa／torchaudio）、Whisper 在 Python 以 fp16 執行。各模型對同輸入都在 cos ≥ 0.9999 內（第 5 節），但隨機取樣讓整首歌不會逐位元相同。
- **伴奏**：與 Python 一樣是鼓＋貝斯＋其他三個模型的輸出，不是「混音減人聲」。
- **權重來源**：HeartMuLa、HeartCodec、Demucs 直接讀上游 safetensors；Seed-VC 四個 `.pth` 需先跑 `prepare-models`。下載權重本身仍要自行準備。
- **歌詞模板**：副歌依關係階段與時段從 3 份固定歌詞中選；主歌依她和他的共同經歷填入（見第 6 節）。地點的歌詞句子是 17 個固定地點的手寫對照，不在表上的地點不會入歌；LLM 自動寫詞未做。
- **VRAM**：兩個遊戲加 ok-song 同時執行時，遊戲曾顯示顯示記憶體超出預算 1.2 GB、畫面掉到 20 FPS；單一遊戲時沒有這個警告。
- **鏡頭**：沿用交談中的鏡頭，沒有專屬的演唱鏡頭與站立動作。
- **效能與 GPU 分享**：一個實例一次只跑一個重型工作；每個階段各自建立 CUDA context 並在結束時釋放，HeartMuLa 約占 8 GB VRAM（載入 HeartCodec 前先釋放）。遊戲的幀時間不受背景產歌影響（第 3 節），反過來是產歌被遊戲搶走 GPU 時間：HeartMuLa 逐幀生成在閒置時每幀 32 ms，一個遊戲同時執行時 127 ms（每幀錄成 CUDA graph 後；之前 190 ms），兩個不限幀率的遊戲同時全速渲染時會到每幀數秒。分離、換聲與 HeartCodec 解碼是大批次運算，只慢 1.5～3 倍。正式遊戲只有一個實例，30 秒歌估計 1.5～2 分鐘完成；在對話前預先準備，不阻塞遊戲。
- **預設關閉**：要在面板打開「女主唱歌」，或用控制通道測試。
- 開著房間麥克風時，喇叭聲可能被當成插話而打斷；測試時用 `game_call {path: "/mic", body: {ignore: true}}`。

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

## 6. 依共同經歷寫主歌（2026-10-23）

- **流程**：準備歌曲時，遊戲用 ok-mind 的唯讀 `memories` 查詢取得她的記憶，只留以玩家為實體的紀錄（kind、place、importance、t），交給 `song_lyrics`。不用 `recall`，因為它會更新記憶的「最近被想起」時間。
- **選擇**：規則只取一起做過的種類（activity、outing、date、treat、talk），依重要度、再依時間排序，挑出最多兩個不同、且在地點表上的地點。有兩個地點時主歌寫兩段；只有一個時，後兩句用固定句子。沒有符合的記憶時沿用原本的固定歌詞。
- **模板 id**：例如 `sunny_day+cafe`，id 會跟著地點變；歌詞本身也在快取鍵內，所以記憶改變時會產生新歌，同樣的記憶則重用舊歌。
- **實測（沙盒，`SongMem01` stage）**：
  - 貞德·Alter 在沙盒沒有記憶檔，沿用固定歌詞 `evening_walk`。
  - 刻晴有一起去咖啡廳的記憶，模板為 `sunny_day+cafe`，主歌是「還記得咖啡廳的那天／咖啡香留在窗邊／你說過的每一句話／我都悄悄記在心上」。
  - 換聲後的人聲以 Qwen3-ASR 辨識，與歌詞幾乎一字不差；在遊戲裡完整唱完，面板與字幕顯示新歌詞，嘴型跟著動。
  - 當時攻略線遊戲與沙盒遊戲都在全速渲染，產歌花了 1550 秒；沙盒限到 30 FPS 後，分離與換聲共 50 秒。

## 7. 換聲的電子音（2026-10-23）

- **症狀**：使用者試聽時覺得歌聲有電子音。逐段比對後，分離出的原人聲正常，換聲後才有。
- **量測**：
  - 原生換聲與 Python 換聲在同一輸入上的音高曲線幾乎相同（細部音高相關 0.958 對 0.970），不是移植造成的。
  - Seed-VC 把音高分成 256 格（約每 20 cent 一格）再取嵌入，滑音與顫音會變成階梯。
- **改法**：
  - `soft_f0`：音高位置在時間上線性內插，再混合相鄰兩格的嵌入，不再吸附到單一格。
  - CFG 從 0.7 降到 0.4，擴散步數從 30 提到 50。
  - 由使用者在五個試聽版本中選定。另有顫音／滑音選項（`vibrato_cents`、`glide_ms`），預設關閉。
- **設定**：`ok-song.example.json` 的 convert 改為 `"diffusion_steps": 50, "inference_cfg_rate": 0.4, "soft_f0": true`。這些參數在快取鍵內，舊歌下次會重新換聲（歌與分軌仍沿用快取）。
- **單獨換聲指令**：`ok-song convert --vocal V.wav --reference R.wav --out O.wav [--soft-f0] [--cfg 0.4] [--steps 50] [--vibrato N] [--glide MS]`
