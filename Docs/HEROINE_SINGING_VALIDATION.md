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
- **鏡頭與站姿**：演唱時（開場白、歌、結尾）改用專屬鏡頭（`SongCameraShot`，2026-10-24）：從她正面拍頭與肩，25 秒內由 2.1 m 推近到 1.6 m，左右擺 12 度，切換時平滑移過去。站姿沿用她的站立待機並面向他；演唱期間取消她的手勢動作，只有嘴型跟著歌聲動。站立待機本身是有小動作的 conversation_* 動畫，使用者要求先不處理動畫（2026-10-24）。交談時 galgame 演出模組也會設定鏡頭，演唱時改由 `SongCameraShot` 接管。
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

## 8. 原聲本身的修音感（2026-10-23）

- **症狀**：lover_night 換聲後電子音仍明顯。
- **量測**：問題出在 HeartMuLa 產生的原人聲，它本身就像硬修音。指標 snap 是持續音落在半音 ±10 cent 內的比例，自然人聲約 0.2。

  | 音訊 | snap | flat |
  |---|---:|---:|
  | lover_night 原人聲 | 0.62 | 0.30 |
  | 換聲（soft_f0） | 0.50 | 0.21 |
  | 去修音 scoop 40／drift 12 | 0.42 | 0.19 |
  | 去修音 scoop 60／drift 20 | 0.36 | 0.19 |
  | 角色 TTS 參考音 | 0.23 | 0.05 |

- **改法**：換聲前先改原人聲的音高曲線（`seedvc::deautotune_f0`），以參數控制：
  - `scoop_cents`：每個音從低多少 cent 滑上來（60 ms）。
  - `drift_cents`：持續音的緩慢飄移幅度（每 250 ms 換一個目標，頭尾淡入淡出）。
- **預設**：scoop 60、drift 20；0／0 為關閉。換聲版本 `seedvc-rust-2`，舊歌下次會重新換聲。scoop 60／drift 20 版本經 ASR 轉寫，歌詞完整。
- **單獨換聲指令**：加 `--scoop 60 --drift 20`。試聽檔在 `Saved/StreetSocial/Songs/ab-lover/`。

### 8.1 再平順一點（2026-10-23）

- **回饋**：使用者覺得 scoop 60／drift 20 有改善，但仍有電子音。
- **量測**：換聲後的持續音音高幾乎凍住。flat 指標（相鄰幀變化小於 2 cent 的比例）約 0.2，TTS 參考音只有 0.05。drift 每 250 ms 才換一次目標，幀間變化仍太小。
- **改法**：新增 `jitter_cents`，每 50 ms 換一個目標的小幅擺動。`glide_ms` 是平均濾波，會抹掉 jitter，兩者不要同時用。

  | 版本 | snap | flat |
  |---|---:|---:|
  | scoop 60／drift 20 | 0.36 | 0.19 |
  | + glide 100 | 0.36 | 0.21 |
  | scoop 80／drift 30／glide 80／vibrato 15 | 0.30 | 0.19 |
  | scoop 80／drift 30／jitter 8／vibrato 15 | 0.29 | 0.17 |
  | scoop 80／drift 30／jitter 15／vibrato 15（預設，第 9 版） | 0.29 | 0.16 |

- **預設**：`vibrato_cents` 15、`scoop_cents` 80、`drift_cents` 30、`jitter_cents` 15。使用者試聽後選定第 9 版（2026-10-24），換聲版本 `seedvc-rust-5`。之前曾短暫改成不開顫音（`seedvc-rust-4`）。jitter 8 與 15 兩版的 ASR 轉寫都與歌詞一致。
- **剩下的限制**：Seed-VC 輸出會把加進去的音高變化又拉平一部分（輸入加 30 cent 飄移，flat 只從 0.19 降到 0.16）。換聲後高頻（8 kHz 以上）只剩原聲的五分之一，聲音偏乾淨、偏合成。這部分要從音色模型處理，不是調音高參數能解決的。

## 9. HeartMuLa 參考音訊槽位實驗（2026-10-23）

- **背景**：HeartMuLa 的輸入在 tags 與歌詞之間有一個 MuQ-MuLan 參考嵌入（512 維，經 `muq_linear` 進 backbone）。官方 pipeline 對 `ref_audio` 直接報 `NotImplementedError`，一律填零。
- **做法**：
  - 嵌入用 Python 的 `muq` 套件（`OpenMuQ/MuQ-MuLan-large`）算，屬於實驗用。
  - Rust 端 `mula::Prompt.muq` 接受外部嵌入，`song::GenerateOptions.muq` 傳入，CFG 的無條件列照上游換成 unconditional embedding。
  - 新增指令 `ok-song song-generate --muq EMBED.f32` 與 `ok-song separate`。遊戲流程仍填零，沒有改動。
- **條件**：lover_night 歌詞與 tags 不變，三個種子（原曲種子、2、3）× 三種嵌入（零、角色 TTS 參考音、文字「soft breathy natural female vocal…」）。零嵌入＋原曲種子重現了原曲。

  | 分離出的原人聲 | 種子 1007… | 種子 2 | 種子 3 | 平均 snap |
  |---|---:|---:|---:|---:|
  | 零（現況） | 0.62 | 0.54 | 0.59 | 0.58 |
  | 角色參考音嵌入 | 0.43 | 0.40 | 0.51 | 0.45 |
  | 文字嵌入 | 0.51 | 0.53 | 0.52 | 0.52 |

- **結果**：
  - 參考音嵌入不會讓歌聲變成她的音色。聲紋相似度（ok-tts speaker-sim）九首都是 0.67，與零嵌入相同。
  - 修音感明顯下降，歌詞 ASR 轉寫完整。
  - 這些人聲再用現行換聲預設（scoop 80／drift 30／jitter 15）處理後，snap 0.23～0.29、flat 0.13。原本的 lover_night 是 0.28／0.17。TTS 參考音是 0.23／0.05。
- **使用者試聽**：覺得效果更差，不採用；遊戲流程維持零嵌入。
- **未做**：要進遊戲流程，需要每個聲線的 MuQ-MuLan 嵌入。目前只能用 Python 計算，成品須改成 Rust（移植 MuQ-MuLan），或預先算好嵌入檔隨聲線存放。試聽檔在 `Saved/StreetSocial/Songs/ab-muq/`。

## 10. YingMusic-SVC 試跑（2026-10-23）

- **目的**：使用者要求遊戲在 16 GB 顯卡上跑得動，找比 Seed-VC 更好的指定音色換聲。
- **模型**：[YingMusic-SVC](https://github.com/GiantAILab/YingMusic-SVC)，程式 MIT，權重 0.73 GB。
  - 以 Seed-VC 為基礎，DiT 設定相同（768 維、17 層、44.1 kHz BigVGAN、whisper-small、256 格 F0）。
  - 推論多了 F0 感知的音色殘差（`use_style_residual`）與自動移調。
  - 論文中的 RVC 音色轉換器只在訓練時使用。
- **條件**：
  - Python 試跑（`E:/OkSong/envs/ymsvc`，torch cu128）。
  - lover_night 的分離人聲加她的 TTS 參考音，fp16，CFG 0.7。
  - 關閉自動移調（`semi_tone_shift=0`），否則會和原伴奏不同調。
- **結果**：

  | 版本 | 聲紋相似度（對她的參考音） | snap | flat | 8 kHz 以上 |
  |---|---:|---:|---:|---:|
  | 分離出的原人聲 | 0.671 | 0.62 | 0.30 | 0.649 |
  | 現行 Seed-VC（Rust，去修音預設） | 0.684 | 0.28 | 0.17 | 0.128 |
  | YingMusic-SVC 30 步 | **0.885** | 0.44 | 0.19 | 0.258 |
  | YingMusic-SVC 50 步 | **0.887** | 0.44 | 0.17 | 0.300 |

  - **音色**：聲紋相似度大幅提高。現行 Seed-VC 的換聲在這個量測上幾乎沒有往她靠。
  - **高頻**：高頻約為現行的兩倍。
  - **修音感**：YingMusic 沒有套用我們的去修音，所以 snap 比現行高。
  - **歌詞**：50 步的 ASR 轉寫完整；30 步有一句錯成「人生我們兩顆」。
- **資源**：
  - 模型載入後 2.1 GB，推論峰值 2.7 GB（PyTorch allocated），reserved 約 3.9 GB。
  - RTX 5090 上 30 秒歌曲：30 步 13.7 秒、50 步 18.4 秒，不含載入 18 秒。
- **試聽**：`Saved/StreetSocial/Songs/ab-ying/`（`ying-s30-song.ogg`、`ying-s50-song.ogg`、`current-seedvc-song.ogg`）。
- **使用者試聽（2026-10-24）**：覺得效果太差，不移植。換聲維持 Seed-VC 加第 9 版的去修音設定。聲紋相似度高不代表聽感好，選模型要以試聽為準。

## 11. 顯存預算（目標：16 GB 顯卡，2026-10-24）

- **量法**：
  - WDDM 下 nvidia-smi 看不到各程序的用量；全卡總量相減又會被其他程式的增減干擾。曾把換聲誤量成 13.7 GB，實際約 3.4 GB。
  - 因此改用程式內統計：`OK_GPU_MEM=1` 時，`native::mem_log` 印出存活中的 GPU 緩衝、峰值與記憶體池保留量（`gpu::mem_stats`、`Gpu::pool_reserved`）。
  - 遊戲與服務的用量取自 Windows 計數器 `\GPU Process Memory(*)\Dedicated Usage`。
- **同時常駐的程序**（C 機，遊戲在跑時）：遊戲 6.3 GB、ok-tts 4.9 GB、ok-asr 2.7 GB，合計約 13.9 GB。
- **ok-song 各階段**（依序執行，同時只有一個模型在顯卡上）：

  | 階段 | 改前 | 改後 | 改法 |
  |---|---:|---:|---|
  | HeartMuLa 生成 | 7.1 GB | 4.4 GB | 主幹 28 層的矩陣改成 int8，每列一個縮放；解碼用 int8 GEMV（`gemv_i8_bf16`），提示預填時展開成 bf16 給 cuBLAS。小解碼器維持 bf16 |
  | HeartCodec 解碼 | 6.1 GB（池 7.3） | 3.5 GB（池 5.0） | transformer 區塊的矩陣改存 fp16，每次 GEMM 前展開成 f32 |
  | Demucs 分離 | 約 1.7～3 GB | 不變 | |
  | Seed-VC 換聲 | 2.8 GB（池 2.85） | 不變 | |

- **品質**：
  - **HeartCodec fp16**：同一組 token 與種子下，和 f32 版的相關係數 0.99998（SNR 43 dB）。生成版本 `heartmula-rust-1` 沒有改。
  - **HeartMuLa int8**：bf16 與 int8 各用兩個種子生成。四首分離出的人聲經 ASR 轉寫，歌詞都完整；修音感與高頻指標落在不同種子本身的變化範圍內。
  - 試聽檔在 `Saved/StreetSocial/Songs/ab-vram/`。
- **速度**：生成與解碼時間沒有明顯差異（GPU 同時被遊戲使用，單次量測波動大）。
- **設定**：`generate.int8` 預設 true，並寫進範例設定。它在取樣參數的快取鍵內，所以舊歌下次會整首重新生成。
- **常駐程序也降了**：
  - ok-tts：talker 改 int8，文字嵌入表移到主記憶體，4.5→3.0 GB（見 TTS_OPTIMIZATION_PLAN_20261022.md 第 14 節）。
  - ok-asr：音訊編碼器矩陣改以 bf16 存放、每次 GEMM 前展開成 f32（checkpoint 本來就是 bf16，結果位元相同），詞嵌入表移到主記憶體，2.7→2.1 GB。20 個檔案轉寫完全相同，速度不變。
  - 合計：遊戲 6.3 + TTS 3.0 + ASR 2.1 = 11.4 GB，加上 ok-song 峰值 4.4 GB 約 15.8 GB。16 GB 卡上剛好放得下，但沒有餘裕；Windows 桌面本身也會用掉數百 MB。遊戲在 16 GB 卡上的實際用量可能比 5090 上少（貼圖串流池會依顯存調整），要在 16 GB 卡上實測才知道。

## 12. 歌舞劇式演出（2026-10-24）

- **使用者要求**：唱歌時也要做動作，最好像迪士尼歌舞劇。另外希望是有沉浸感的戀愛遊戲，不合理的地方要修正。
- **編舞**：由 ok-rules 的 `song_choreo` 決定，C++ 只負責照時間軸播放。
  - **樂句**：從歌曲的嘴型包絡找出唱的樂句。若樂句數量少於歌詞行數的 3/4（連唱、很少換氣），就依行數把歌聲區間平均切開，每個分界點移到前後 0.5 秒內最小聲的位置。
  - **主歌與副歌**：主歌的單位是 [主歌] 每行以空白切成的半句，其餘是副歌。
  - **時間軸**：
    - 前奏：輕快的待機動作。
    - 主歌：每句換一個姿勢或開心手勢，不重複。
    - 副歌：跳舞。
    - 最後一個音前 2.5 秒：決勝姿勢。
    - 開場台詞時害羞，結尾台詞時開心。
    - 每個動作至少維持 4 秒。
  - **用到的片段**：都是動作庫（`lib_*`）裡的站姿片段，例如舞蹈 1／2、啦啦隊舞、擺姿勢、開心情緒動作、決勝姿勢。抒情歌與輕快歌（依歌詞模板的 tags）使用不同組合。
- **播放的限制與做法**：
  - 每個角色只有一個串流動作槽，片段播放中不能裝下一個；片段要先準備 0.6～1.7 秒。
  - 所以目前片段一開始播，就先預熱下一個；到了切換點先放掉目前的片段，預熱好的那個一裝上就開播。中間會短暫經過站立待機。
  - 時間以實際的歌曲播放秒數為準（`OKGetCrowdRuntimeSpeechPlayback`）。
  - 新增 `OKReleaseCrowdRuntimeAction`：只放掉動作，不停止說話。原本的 `OKResumeCrowdRuntimeBehavior` 會一併取消說話聲，用它切換動作時歌會被中斷。
- **鏡頭**：
  - 從她正面偏 35 度、遠離男主的那一側拍。她面向男主，正對著拍會拍到男主的背。
  - 前奏、副歌、結尾姿勢拍全身（4.8 m）；主歌拍半身（2.6 m）；開場與結尾台詞拍頭肩（2.0 m）。
  - 距離與高度平滑過渡。
- **沉浸感修正**：
  - **台詞順序**：她提議時開口問「我練了一首歌，想唱給你聽，你要聽嗎？」（語音）；你答應後她說「那我開始囉，不要笑我喔」。原本是答應之後她才再問一次要不要聽。
  - **她會記得**：唱完寫入她的記憶（重要度 0.7）；被你拒絕也會記下（0.35）。之後聊天時她會記得。
  - **字幕語言**：遊戲是簡體中文時，歌詞字幕與面板改顯示簡體，段落標成主歌／副歌。原本一律是繁體。
- **實測**（沙盒，雷電將軍，SongDance10）：
  - 開場害羞 → 前奏 → 主歌手勢 → 副歌跳舞 → 結尾決勝姿勢 → 收尾開心，依序播出，歌完整唱完。
  - 偶爾某個片段在下一個切換點前還沒裝好會被略過，例如副歌第二個舞步。
- **限制**：她站在原地，沒有走位。換動作時會短暫回到站立待機。
- **地點合理性**（追加）：她只在唱歌會覺得自在的地方提議。
  - 規則 `heroine_place_ease` 以地點規範表的舉動 `sing`（大聲、人多時會不自在）計算尷尬度，再依她的害羞、大方程度與關係判斷反應；只有「自然」或「樂在其中」才提議。
  - 例如圖書館上午不唱、公園晚上可以；不自在時 45 秒後再看。
  - 規則測試 `singing_to_him_fits_a_quiet_park_evening_not_a_library_morning`。
- **卡拉 OK 字幕**（追加）：唱歌時字幕一次只顯示正在唱的那一句（♪ 句子 ♪），開唱前顯示 ♪。原本整首歌詞在字幕框越積越長，最後被截斷。
  - 每句的開始時間來自同一套樂句切分（song_choreo 的 `lines`）：換氣數剛好等於歌詞單位數時照換氣，否則在歌聲區間平均切開並對齊最小聲處。
  - 字幕依遊戲語言顯示繁體或簡體。
- **前奏**（追加）：第一句在 1.5 秒之後才開唱時才放前奏動作。這批歌常常一開始就唱第一個字，那時直接從主歌手勢開始。

## 13. 整合後驗收與麥克風修正（2026-10-25）

- 整合版（Gest15）上的驗收發現：歌開始約 10 秒就結束。原因是即時麥克風的「插話」機制把歌當成一般回覆停掉了；房間裡的人聲（正式遊玩時也可能是喇叭放出的歌聲）觸發了插話。
- 修正（`OKStreetSocialLiveTalk.inl`）：她唱歌期間（`SongOwnsVoice`），麥克風和男主說話時一樣靜音，插話也不會停歌。
- 重新驗收（SongMic01，沙盒 VoiceTest，雷電將軍）：邀請 → 接受 → 開場動作 → 5 段歌中動作 → 收尾 → 唱完，整首約 24 秒都有唱完；期間 log 顯示 "mic off while she sings"，卡拉 OK 字幕正常。她說話用的是卡片裡的本人聲線（面板顯示「她的角色卡里存有专属声音」）。
<<<<<<< HEAD

## 14. 完整歌曲、里程碑唱歌、跳過與重播（2026-10-25，E 線）

使用者要求：
- 歌曲改成約 1 分半。
- 可以跳過，也可以重播回憶裡的歌。
- 成為戀人時唱一次；之後戀人好感到 100、150 各唱一次；再來每 500 句唱一次。

### 歌曲長度與歌詞（ok-rules `song.rs`、ok-song `job.rs`）
- 每張歌詞都寫成完整一首：主歌 → 副歌 → 主歌 → 副歌 → 副歌。重複的副歌也逐行寫出，字幕才能逐行對上。
- 本來還有一段橋段，但模型唱完要 100 秒以上，90 秒時還在最後一段副歌，所以拿掉了。
- HeartMuLa 唱完歌詞也不會停，會一直唱到長度上限，所以歌的長度就是上限 90 秒。mix 最後 2 秒一律淡出（`ok-song audio::mix`），不會切在字中間。
- 歌詞只寫繁體顯示稿。給模型的稿（簡體，`[Verse]`／`[Chorus]`／`[Bridge]`）由 `model_text` 用 zhconv 轉出，不用再維護兩份。
- `max_duration_ms` 改為 90000（`SONG_MS`）。ok-song 的上限從 60 秒放寬到 120 秒，歌詞字數上限從 400 改為 640。
- 一般歌詞三張（evening_walk／sunny_day／lover_night）加長。另外新增四張里程碑歌詞：
  - confession：成為戀人
  - favor100：越來越喜歡
  - favor150：一直在一起
  - lines：說不完的話
- 「兩人去過的地方」只替換第一段主歌，其餘段落保留。confession 和 favor150 保留原本寫好的詞，不替換。
- 編舞（`song_choreo`）改成按段落順序：第 i 個樂句對應歌詞第 i 個單位的段落（主歌、副歌、橋段）。主歌和橋段用手勢，副歌跳舞。

### 觸發（rules `song_milestone`）
1. 成為戀人（`stage=lover`、尚未唱過 confession）：立刻到期。
2. 戀人好感 ≥100：favor100。
3. 好感 ≥149.5（戀人上限 150）：favor150。
4. 三首都唱過後，和她說了 500 句（他打的或說的話、她回的每一句都算，含電話文字）：lines。唱完後歸零，再數 500 句。
5. 每次只處理一首：到期的優先；多首同時到期時，先處理順序在前的里程碑。
- 接近時先做歌，到期才邀請。一首 90 秒的歌要做好幾分鐘，所以接近時（朋友且好感度 liking ≥60、戀人好感 85／135、400 句）就先送 ok-song 做，等到期才開口。
- 「唱過」記在完成的那一刻。被打斷或拒絕都不算唱過，之後會再邀。
- 自動唱歌預設改為開啟。songs.json schema 2；舊的 schema 1 存檔把「關」當成預設值，不是玩家選的，所以讀進來一律視為開啟。

### 跳過與重播
- 跳過：開場白、唱歌中、收尾那句話都能跳過。
- 重播：每首唱完的歌都會保存（歌曲 ogg、嘴型 `*.mouth.json`、歌詞），記在 songs.json 的 `memories`。
- 和她說話時，右側面板「回憶的歌」列出她最近五首。按「再聽一次」，她會用同一段音檔和舞台演出再唱一次，開場說「那首歌啊……好，我再唱一次給你聽。」。
- 重播不算里程碑，也不會讓句數歸零。被打斷就直接結束，不會再邀。
- 控制通道 `/song`：
  - `{"action":"replay","girl":-1}`：讓目前對象唱她最近的一首。
  - `{"action":"set_relation","girl":N,"stage":"lover","favor":100,"lines":0}`：測試用，設定她的階段、好感與句數。
  - 狀態回應會帶 `milestone`、`progress`、`memories`。

### 啟動器
- E 的 `local-instance.json` 是 `voice=false`（自己不開 ok-tts／ok-asr）。這時 `OK_SONG_ENDPOINT` 也和 TTS／ASR 一樣，可以由環境變數指定別處的 ok-song。

### 穩定性
- ok-song 的工作若在某個階段 panic（例如找不到 CUDA DLL），只把那個工作記為 failed，工作執行緒繼續處理下一個。
- 遊戲端製作超過一小時就記為失敗，里程碑留著，之後再做一次。

### 驗收（2026-10-25，E 沙盒 SongE，SongE04 stage，雷電將軍）
- 新遊戲預設開啟唱歌。設成戀人（好感 10）後，confession 立刻到期：做歌約 1.5～2.5 分鐘 → 對話中邀請 → 接受 → 唱完（含開場與收尾約 110 秒）。之後 progress 記為 `done: confession`，句數歸零，memories 多一首。
- 重播：她唱同一首（約 114 秒）。memories 數量不變，也不算新的里程碑。
- 好感 90：favor100 先做好（`due=false`），30 秒內不邀請。改成 100 後才邀請；唱到一半按跳過 → 收尾 → `done: confession,favor100`。
- 好感 150：favor150 到期。好感 120、420 句、favor150 還沒唱：不觸發任何歌（每 500 句要等三首里程碑都唱過）。
- 句數：一次自由對話後，句數從 420 變成 425（他 1 句、她 4 句）。
- 長度：上限 100 秒時，第 100 秒還在唱，且 mix 最後一秒從 -18 dB 淡到 -41 dB。因此上限定為 90 秒並保留淡出。
=======
- 2026-10-25 整合版 Int1025C 最終驗收（沙盒 VoiceTest，雷電將軍）：
  - 打字台詞：男主先用 m12 說出，期間麥克風關閉；他說完後，她的回覆語音和對話框才接上。
  - 唱歌：邀請 → 開場 → 5 段歌中動作 → 收尾 → 唱完，整首約 28 秒，期間 "mic off while she sings"，沒有被打斷。
  - 她的語音用卡片裡的本人聲線（協調者在遊戲內確認）。
>>>>>>> origin/master
