# 女主唱歌：實作與驗收紀錄

日期：2026-10-23。對應計畫：[HEROINE_SINGING_IMPLEMENTATION_PLAN.md](HEROINE_SINGING_IMPLEMENTATION_PLAN.md)。

**目前狀態**：P0～P3 已完成，並在遊戲中（沙盒、SongPick03）實際跑過完整流程。三個模型階段仍是呼叫上游 Python 入口的原型；依使用者要求「測試可以 Python，最後遊戲成品要用 Rust」，原生 Rust 推論是下一階段的主要工作（見最後一節）。音色品質尚未經使用者評分，不能標為通過。

## 1. 架構與檔案

| 部分 | 位置 | 說明 |
|---|---|---|
| 聲線匯出 | `Tools/ok-tts` `voice-export` | 唯讀；卡內 `okVo` 優先，否則用性格預設 profile，與對話合成同一優先序；輸出 24 kHz 參考音與 `voice.json`（embedding tag、參考音雜湊） |
| 歌曲工作服務 | `Tools/ok-song`（Rust） | `doctor`／`generate`／`serve`／`status`／`cancel`／`mix`；HTTP `/health`、`/jobs`、`/jobs/{id}`、`/jobs/{id}/cancel` |
| 模型階段 | `ok-song/src/backend.rs` | 產歌 HeartMuLa-oss-3B、分離 Demucs `htdemucs_ft`、換聲 Seed-VC（F0 條件歌唱模型，不移調）；目前都是上游 Python 入口，`native_rust_inference=false` |
| 混音與嘴型 | `ok-song/src/audio.rs`（Rust） | 多相位 sinc 重取樣、混音、BS.1770 響度（−16 LUFS）、4 倍超取樣真峰值（≤ −1 dBTP）、10 ms 人聲 RMS 包絡、OGG |
| 快取 | `Saved/StreetSocial/Songs/` | 歌曲與分軌由所有女主共用；換聲與混音依聲線分開；`DONE` 標記與 SHA-256 驗證才算命中；LRU 容量上限 |
| 規則 | `Tools/ok-rules/src/song.rs` | `song_decide`（`Relation::liking` 跨階段排名、門檻、冷卻）、`song_lyrics`（依階段與時段選模板） |
| 遊戲事件 | `Source/OpenKoikatsu/OKStreetSocialSong.inl`、`OKStreetSocialSongState.h` | 準備 → 就緒 → 邀請 → 開場白 → 唱歌 → 結尾；面板開關、邀請按鈕、歌詞字幕、跳過 |
| 播放 | `OKCrowdRuntimeScene` `OKSpeakCrowdRuntimeSong` | 播放混音，嘴型依音訊渲染時鐘讀人聲包絡（不用混音 RMS），音高固定 1 |
| 啟動 | `run_street_social_development.lua` | `Build/ok-song/ok-song.exe` 以 base+20 埠隨遊戲啟動、`--parent-pid`；`OK_SONG_ENDPOINT` |
| 外部環境（本機） | `E:\OkSong` | `envs/heartmula`、`envs/seedvc`（各自 torch 2.10 cu128）、`seed-vc`（51383ef）、FFmpeg 8.1 共享版、HF 快取；HeartMuLa 在 `E:\HeartMuLa`（5858ca8） |

## 2. 重跑指令

```bash
Build/ok-song/ok-song.exe doctor
Tools/ok-song/target/release/ok-song.exe generate --request E:/OkSong/test/req-march7.json
Tools/lua/lua.exe Tools/run_street_social_development.lua -Control -Sandbox VoiceTest -PortBase 48060 -RulesDll <自建 ok_rules.dll>
```

遊戲中測試（控制埠 48070）：`OK_CONTROL_PORT=48070 Tools/lua/lua.exe Tools/street_play.lua song state|prepare [女主編號]|play|later|decline|skip|cancel|enable|disable`。指定女主的 `prepare N` 會標示 `override`，正式自動事件不使用。

## 3. 實測結果（RTX 5090，另一個遊戲同時在跑）

**離線（P1）**

| 階段 | 時間 | 備註 |
|---|---:|---|
| HeartMuLa 產生 30 秒歌曲 | 187 s（首次含載入） | 4 行副歌時模型在 15 秒處自行結束；加一段主歌後達 30 秒 |
| Demucs 分離 | 36 s（首次含下載 67 s） | 人聲與伴奏長度一致 |
| Seed-VC 換聲 | 53～99 s | 實際計算 RTF 0.34～0.53，其餘是每次重新載入模型 |
| Rust 混音＋包絡＋OGG | 0.5～1.8 s | 響度 −16.0 LUFS，與 ffmpeg EBU R128 量測一致 |

聲紋輔助指標（Qwen Base speaker encoder）：三月七、雷電將軍、胡桃的參考音，各自都與「自己的」換聲結果最相似（原始 cosine 0.981～0.984），與原歌手（0.937～0.948）及其他女主較低。只是輔助，最後要人工試聽。

試聽檔案（本機）：`Saved/StreetSocial/Songs/prototype-p1/`（原歌、原人聲、伴奏、各女主的說話參考音、換聲人聲、成品）。

**服務（P2）**：同一請求再送 0 秒命中快取；換女主只重做換聲（53 s）；同 request_id 同內容回同一工作，不同內容 409；生成中取消會結束子程序、狀態 cancelled、不發布半成品。

**遊戲（P3，沙盒 SongPick03）**

| 項目 | 結果 |
|---|---|
| 選角 | `song_decide` 排名第一為貞德·Alter（戀人，liking 99.8）；未指定女主的準備選中她 |
| 準備 | 歌曲與分軌已快取時 71～152 秒（只換聲＋混音）；完全命中時 0 秒 |
| 邀請 | 只在和她交談、沒有台詞／觸碰／來電時出現；面板（換聲音按鈕下方）與底部對話框都顯示 |
| 唱歌 | 開場白（她的聲線）→ 30 秒歌曲 → 結尾台詞；嘴型隨人聲包絡變化（取樣 0.18～0.93）；底部字幕顯示歌詞 |
| 等一下／下次吧 | 回到就緒；等一下 5 分鐘、下次吧 20 分鐘後才再邀請；婉拒不扣好感 |
| 中斷 | 唱到一半離開對話：歌聲立即停止、回到就緒並保留歌曲 |
| 跳過 | 進入結尾台詞後回到閒置 |
| 自動事件門檻 | 開關打開後，因今日已唱過，規則回 `sung_today`，不再準備 |
| 存檔 | `songs.json`（存檔 ID、開關、當日次數、最近事件）；已加入存檔欄位的複製清單 |

## 4. 已知限制與未做項目

- **模型階段仍是 Python 原型**（HeartMuLa、Demucs、Seed-VC），需本機 `E:\OkSong` 環境；原生 Rust 推論尚未完成，是正式交付前的必要工作。
- **音色品質未經使用者評分**（計畫要求音色、自然度、清晰度各 ≥ 4／5）。
- **上游產歌入口沒有 seed**：可重現性靠階段快取保證。
- **歌詞模板**：目前依關係階段與時段，從 3 份固定歌詞中選擇；計畫中「填入共同經歷」的模板尚未做。
- **鏡頭**：沿用交談中的鏡頭，沒有專屬的演唱鏡頭與站立動作。
- **效能**：沒有做計畫中「遊戲負載下背景產歌時 frame time」的正式量測；目前一個實例一次只跑一個重型工作，不會和 TTS 同時搶 GPU 推論鎖，但會和遊戲共用 GPU。
- **預設關閉**：要在面板打開「女主唱歌」，或用控制通道測試。
- 開著房間麥克風時，喇叭聲可能被當成插話而打斷（和她平常說話相同）；測試時用 `street_play.lua mic off`。

## 5. 原生 Rust 推論的路線

| 階段 | 結構 | 可沿用 | 估計 |
|---|---|---|---|
| HeartMuLa | Llama-3.2 3B 主幹（28 層、3072 維、GQA 24/8、llama3 RoPE 縮放）＋ 300M 解碼器（3 層）＋ 8 組碼本、CFG（batch 2） | ok-tts 的 CUDA kernel（bf16 GEMM、RMSNorm、RoPE、KV cache、CUDA graph、取樣） | 主幹與解碼器 2～3 天（含逐層數值核對） |
| HeartCodec | 24 層 flow-matching transformer（ada_norm_single）＋ RVQ ＋ 卷積上取樣到 48 kHz | 同上 | 2～3 天 |
| Demucs htdemucs_ft | 4 個模型的 bag，每個是時頻雙分支 transformer | — | 3～4 天 |
| Seed-VC | Whisper-base 編碼器、CAMPPlus、RMVPE（F0）、DiT（CFM 30 步）、BigVGAN 44 kHz | Whisper／卷積 kernel 需新寫 | 5 天以上 |
