# TTS 聲線一致性與低延遲優化計畫

日期：2026-10-22。狀態：P0／P1 的伺服器部分已實作並在本機測過；P2 的 Base 固定聲線已接進伺服器並建好全部聲線的 profile，使用者試聽確認聲線一致（見 13.6）；2026-10-23 已部署到 `Build/ok-tts`，下次啟動遊戲即使用 Base（尚未在遊戲中實測）。

## 1. 目標與決策

使用者需求：同一角色說不同台詞時，聲音不要經常變成另一個人；同時維持即時對話所需的速度。

推薦主線是 **VoiceDesign 離線建立聲音 → 固定參考音訊與逐字稿 → 1.7B Base 複製聲線 → Rust/CUDA 串流播放**。並行評估的候選是 0.6B Base，但必須以同一組音質、聲線與遊戲負載測試決定是否採用。此處的「並行評估」指比較方案，不要求多個代理或同時佔用 GPU。

先修正現有快取、排隊與觀測問題，再接入 Base。固定 seed、降低 temperature、加強音色文字描述只能作為對照或緩解，不能視為聲紋固定方案。聲音複製也不保證完全不漂移，仍需跨台詞驗收。

不以加快播放速度、縮短台詞、取消自然停頓，冒充推論速度改善。

## 2. 範圍與限制

- 主要範圍：`Tools/ok-tts`、StreetSocial 一般對話與自由對話的 TTS 呼叫、播放、快取及診斷。
- 保留 UE 播放、嘴型、對話完成判定與重播紀錄的契約。
- 工具以 Rust 實作，專案編排腳本使用 Lua；不新增 Python 或 PowerShell 腳本。既有 Python 參考程式可作為數值對照來源，不成為正式執行依賴。
- 不修改 `StreetSocialCity/`，不覆蓋其他 session 的變更，不停止另一份 checkout 的語音服務。
- 參考聲音、角色私有資料、測試錄音與模型權重只存本機，未經確認不納入版控；共用程式、資料格式與不含私有音訊的報告可提交。
- 這份計畫沒有承諾新模型速度、實作天數或聲音品質已達標。

## 3. 已確認現況

### 3.1 推論與聲音來源

| 項目 | 原始碼與現況 |
|---|---|
| 模型 | `src/engine.rs`：`Qwen/Qwen3-TTS-12Hz-1.7B-VoiceDesign` |
| 執行方式 | Rust、cudarc、cuBLAS、NVRTC；code predictor 已使用 CUDA graph |
| 聲音條件 | `voices.json` 與 `src/text.rs`：性格、年齡、音色描述與 seed |
| 固定 seed | `speak` 與 `speak_stream` 均強制使用 `voice.seed`，已不是逐句隨機 seed |
| 取樣 | talker temperature 0.65、top-k 30；predictor 0.9、50 |
| 角色識別 | 遊戲傳送 personality 與全域 TTS age，沒有獨立的角色聲音 profile ID |
| 語言 | 由文字推斷中／英文；目前日文字符也進中文分支，不能視為完整多語支援 |
| 推論隔離 | `Mutex<Option<Engine>>` 序列化合成，保護共用 GPU/KV 狀態 |

VoiceDesign 的文字條件並非固定 speaker embedding；不同台詞獨立合成是聲線漂移的主要候選原因。這是根據架構提出的判斷，尚未以人工盲聽和聲紋指標完成因果驗證。

### 3.2 串流與快取

- 自由對話已呼叫 `/synthesize_stream`，不是尚未串流。
- 每累積 4 個 codec frame 解碼一次，對應約 320 ms 音訊內容；這不是 320 ms 的計算時間。
- UE 起播門檻為 4,800 samples／24 kHz，即 200 ms 音訊內容；這也不是固定再等待 200 ms 牆鐘時間。
- 一般腳本對話走 `/synthesize`，等整句 OGG 完成後播放。
- 自由對話串流完成後再呼叫 `/synthesize` 取得重播檔，取得檔案後才要求下一片段。
- `/synthesize_stream` 會寫快取，但進入時沒有命中快取就直接讀取播放的分支。
- `/synthesize` 先取得引擎鎖，才檢查檔案快取；命中快取也可能被另一句推論擋住。
- 串流在引擎鎖範圍內完成 OGG 編碼與落盤，有縮短鎖持有時間的空間。
- `decode_tail` 每次帶入最多 25 個左側 context frames，存在重複解碼成本；不能未驗證就刪除 context。
- 現有 `first_audio` 計時起點位於取得引擎鎖之後，沒有反映排隊、HTTP、UE 起播與 LLM 等待。
- 非串流失敗會退回內建錄音，可能造成另一種明顯的聲音切換；需與模型音色漂移分開統計。

### 3.3 現有速度證據

來源為 `Build/ok-tts/logs/ok-tts.log` 的 2026-10-22 執行紀錄，硬體為 RTX 5090。以下是案例，並非完整分布或新測試結果。

| 案例 | first_audio | 生成時間 | 音訊長度 |
|---|---:|---:|---:|
| 短句「你想吃什么呢？」 | 0.11 s | 0.30 s | 0.88 s |
| 較長片段「还有红豆汤圆，甜甜的会暖起来。」 | 0.16 s | 1.39 s | 2.48 s |
| 慢速片段「哼，那干嘛非找她，。」 | 0.34 s | 3.52 s | 2.46 s |

最近多筆短句 first_audio 約 0.10～0.19 秒，但也有 0.67 秒案例。慢速片段的生成時間超過音訊長度，顯示持續供音仍需驗收。不能僅憑這些紀錄斷言原因就是 GPU 競爭。

## 4. 驗收指標

以下是待測目標，不是已達成數字。暖機與冷啟動分開，快取命中與未命中分開。

| 面向 | 建議門檻／判定 |
|---|---|
| 暖機未命中起播 | 遊戲固定負載、單一前景說話者：文字片段備妥到實際音訊消費開始 P95 ≤ 500 ms |
| 快取命中起播 | 相同條件 P95 ≤ 150 ms；另列 OGG 解碼與檔案 IO 成本 |
| 持續合成 | RTF＝生成時間／音訊長度，P95 < 1.0，目標 ≤ 0.7 |
| 播放完整性 | 標準測試集中無因缺資料造成的非預期斷音、重播、漏片段或跨角色串音 |
| 聲線一致性 | 每個試點 profile 至少 20 句，盲聽至少 95% 判為同一說話者，明顯換人樣本逐筆修正 |
| 發音與自然度 | 與現況配對盲聽無明顯退步；ASR 漏字、重複字只作輔助診斷 |
| 遊戲效能 | 固定場景與設定，啟用 TTS 的 frame time P95 相對同條件現況退步 ≤ 5%；另記 GPU 記憶體峰值 |
| 取消 | 取消後不再播放舊句；已取消推論於下一個有界檢查點停止，記錄實際取消延遲 |
| 故障處理 | 明確記錄失敗、取消、截斷、fallback；不得以 HTTP 200 或非空 PCM 直接認定完整成功 |

小樣本 P95 僅作開發參考；最終每個候選方案每個主要負載情境至少 100 次請求。暖機後重複至少三輪，避免挑選最好的一輪。聲紋相似度沒有跨模型通用合格值，先用人工標註校準，不預先硬訂 cosine 門檻。

## 5. 目標架構

```text
建立聲音（離線）
VoiceDesign / 合適的既有參考音訊
  → 人工挑選乾淨、聲線穩定的片段與正確逐字稿
  → Base 所需的 speaker embedding / reference codec tokens
  → 有版本的本機 voice profile

對話執行（線上）
角色穩定 ID → voice profile ID + revision
  → 台詞正規化、明確語言、語意片段
  → 快取查詢 → 命中直接供音
              → 未命中進入可取消的推論佇列
  → 常駐 Base 引擎 + 預先載入的聲音條件
  → PCM 串流 → UE 緩衝、播放、嘴型
  → 非推論關鍵路徑保存重播檔與狀態
```

VoiceDesign 不必與 Base 同時常駐 GPU。參考特徵可在建立 profile 時預算，執行時載入，以減少與遊戲競爭。仍須計入 reference tokens 帶來的 prefill 成本；重用聲音條件不代表推論開銷為零。

### 5.1 聲音 profile

規劃新增 `voice_profile_id`，與 personality 解耦：性格用來選預設 profile 或描述說話方式，不再作為唯一聲音身分。不要使用顯示名稱或角色列表索引當永久 ID。

Profile 至少保存：schema version、profile ID、revision、模型 ID 與權重 revision／hash、參考音訊 hash、逐字稿、語言、speaker embedding／reference tokens 格式與形狀、前處理版本、預設取樣設定及核准狀態。

- 優先試完整 reference audio + transcript 的條件方式；若比較 speaker embedding only，必須獨立評估速度與一致性。
- 不隨機輪換參考片段，不以改 seed 產生每句不同的「新聲音」。
- 先固定一套基準聲線；情緒、年齡與跨語言變體另作驗證，避免重新引入換人問題。
- Base 的風格控制能力不能直接假定等同 VoiceDesign 的 instruction；不可盲目沿用舊 prompt。

### 5.2 Base 與 Rust 相容性

現有 `talker.rs` 有固定 `HIDDEN=2048` 與 VoiceDesign prompt 組裝。接 1.7B Base 前要核對權重名稱、特殊 token、speaker 條件插入、reference text/code 對齊、位置編碼、prefill 和輸出格式。

0.6B 另需從模型 config 驅動維度、層數及 kernel 形狀檢查；不得將 1.7B 常數原封不動套用。新增 Base 路徑先保留 VoiceDesign 對照，拒絕不相容權重並回報具體原因。

## 6. 分階段實作

### P0：建立可重現基準與端到端觀測

- [ ] 固定目前執行檔、原始碼 revision、模型 revision、voices hash、取樣參數與遊戲 stage。
- [ ] 為請求加入 request ID、conversation generation、character/profile ID、片段序號與 cache key。
- [ ] 記錄文字備妥、請求送出、server receive、queue enter／leave、prefill 完成、first PCM、UE 收到、首次實際消費 PCM、生成結束與 cache ready。
- [ ] 用各程序 monotonic clock 計算本地 duration，再以 request ID 串接；不直接相減不同程序的 monotonic timestamp。
- [ ] 記錄音訊 underrun、fallback、取消、截斷、重試與 GPU／frame time。
- [ ] 設立固定語料、負載腳本、JSON 結果與試聽索引，工具以 Rust／Lua 實作。

退出條件：可區分 LLM 等待、TTS 排隊、模型推論和 UE 起播成本，且同一測試能重複執行。

### P1：現有引擎的快取、排隊與生命週期

- [ ] 將不可變 voice metadata 與合成引擎鎖分離，快取命中不等待 GPU 推論鎖。
- [ ] 串流支援快取命中：明確選擇讀 PCM sidecar 或解碼 OGG 為 PCM；以延遲、容量及播放一致性測試選定。
- [ ] 相同 cache key 的併發請求去重，避免重算與同名暫存檔衝突。
- [ ] 完成 PCM 後釋放推論資源，由有界後處理工作保存 OGG；不得讓背景工作無限制堆積。
- [ ] 用明確的 pending／ready／failed 狀態協調串流完成與重播檔可用，避免檔案尚未寫好時 `/synthesize` 又重算。
- [ ] 下一句預取不再依賴上一句 OGG 落盤，但保持發話順序與有界預取量。
- [ ] 加入可取消、可觀測的排隊；過期請求在取得引擎前和合成中都能退出。
- [ ] 設定背壓及緩衝上限；慢客戶端不得累積無上限 PCM。

維持單一引擎可變狀態隔離，不直接解除 Mutex 讓多個請求同時修改 KV cache。前景已開始的語音避免被背景預取打斷。

退出條件：快取命中不觸發模型生成、不被長句合成阻塞；取消後無舊音訊；重播檔與串流完成狀態可靠。

### P2：固定聲線試點

- [ ] 挑選 3 個差異明顯的成人角色聲線作試點，保留現有對照樣本。
- [ ] 各準備一段乾淨參考音訊與正確逐字稿；長度先比較約 3、6、10 秒，依品質與 prefill 成本選擇。
- [ ] 接入 1.7B Base、聲音條件生成／載入、profile 格式驗證。
- [ ] 以官方實作輸出的中間資料核對 speaker 條件與 prompt；數值容差按階段設定，不能要求所有 bf16 自回歸結果逐位元相同。
- [ ] 實作角色到 profile 的持久映射、缺失 profile 診斷和相容舊 personality 請求的預設映射。
- [ ] 完成每角色至少 20 句盲聽與跨重啟一致性比較。

退出條件：同聲線不同台詞的穩定度明顯改善，發音無顯著退步，且取得完整延遲分布。

### P3：保持快的串流與切句

- [ ] 依 P0 profiling 判定 talker、predictor、prefill、codec、傳輸各自占比，再決定優化項目。
- [ ] 首包維持小批量，後續評估較大 decode 批量，減少重複左側 context 解碼；比較 4／8／12 frames，不直接套用最大值。
- [ ] 若 codec 仍是瓶頸，再研究持續解碼狀態；必須驗證卷積邊界、接縫與整句對照。
- [ ] 評估固定聲音條件的 embedding／prefill prefix 快取；確認文字與位置條件不變才可重用 KV prefix。
- [ ] 只有 profiling 顯示值得時才補 talker CUDA graph，驗證位置更新、EOS、動態長度與 graph cache 生命週期。
- [ ] 自由對話採語意完整片段，避免不必要的 2～5 字拆段與「，。」等正規化產物；保留「好」「嗯」等合理短答。
- [ ] 切句兼顧首段延遲與後續流暢度：首段可較短，後段可適度合併，不為等完整段落而拖慢首次出聲。
- [ ] 一般腳本對話共用串流播放能力，保留嘴型、取消、完成判定與存檔順序。

退出條件：達到起播與 RTF 門檻，無可感知接縫；短句改善不是靠延後所有首句。

### P4：0.6B 對照與部署選擇

- [ ] 參數化模型維度並接入 0.6B Base，沿用同一組 reference、語料、負載與測試。
- [ ] 比較未命中 first audible P50／P95、RTF、顯存峰值、遊戲 frame time、聲線盲聽、發音自然度。
- [ ] 0.6B 達到聲線與發音門檻，且速度或顯存收益足夠才採用；否則維持 1.7B。
- [ ] 不在每句間自動切換不同模型，以免聲線和韻律再次改變。

退出條件：形成有測量證據的模型決策，而非只依參數量選型。

### P5：整合、發布與回歸

- [ ] 所有試點通過後才擴展至其餘 profile，不先批量產生全聲線。
- [ ] 執行完整測試矩陣、發布新的 ok-tts 套件及必要的新遊戲 stage，保存 hashes 與報告。
- [ ] 使用新 tag；更換引擎建置時使用新的 base-dir。不遞迴刪除帶 junction 的 `.stage`。
- [ ] 啟動器只處理本 checkout 的服務與連接埠，驗證另一條開發線不受影響。
- [ ] 更新 TTS 使用與診斷文件，實際錯誤與修正記入 `Docs/STREET_ERRORS.md`，保留其他 session 紀錄。

## 7. API、快取與故障契約

新欄位規劃如下，名稱與版本需在實作時固定並測相容性：

| 欄位／能力 | 用途 |
|---|---|
| `voice_profile_id`、`voice_revision` | 固定角色聲音與可追溯版本 |
| `language` | 明確指定語言，避免混合文字誤判 |
| `request_id`、`generation`、`segment_index` | 取消、排序與跨程序診斷 |
| capabilities／protocol version | 客戶端判斷串流、profile 與完成狀態能力 |
| synthesis completion status | 分辨完整結束、取消與中途失敗，避免 200 回應後截斷被當成功 |

快取鍵應包含：schema／實作版本、模型權重 revision、profile revision、reference hash、語言、清理後文字、取樣參數、seed、音訊後處理版本。不能只用穩定 MODEL_ID 字串代表所有權重或設定。

快取寫入採唯一暫存名與原子發布，處理 Windows 已存在目標、重複寫入、損壞及不完整檔案。快取版本改變後不誤用舊聲線；舊快取保留供回退，清理另行限定目錄與規則。

PCM 協定需明確標示 endian、sample rate、channels。目前標頭 `audio/L16` 與程式註明的 little-endian PCM 存在語意差異，須在協定版本中釐清並保留舊客戶端相容性。

串流與快取重播的靜音裁切、增益、限幅須一致或差異受控。現有 `peak_normalize` 僅在 peak 超過 0.99 時縮小，不是完整的響度正規化。不得對每個小 chunk 各自放大，造成音量抽動。

故障時不要默默切換另一種聲音。UI／日誌明示 fallback；已播一半的片段不得重新從頭重試造成重複。一般對話原有內建錄音備援可保留為明確政策；自由對話無相同內容的錄音時應保留字幕並結束或跳過該片段。

## 8. 測試矩陣

| 維度 | 案例 |
|---|---|
| 台詞 | 2～5 字合理短答、10～20 字、40～80 字、多句、問句、激動與平靜內容 |
| 文字 | 繁簡中文、數字、姓名、英文、混合文字、裝飾符號、異常連續標點 |
| 語言 | 中文先達標；英文與跨語言分開報告；日文未接妥前不得宣稱支援 |
| 聲音 | 同角色連續多句、跨重啟、角色 A→B→A、相同性格但不同 profile |
| 快取 | 冷快取、熱快取、生成中同 key、profile 改版、損壞檔、落盤失敗 |
| 負載 | GPU 空閒、固定街區遊戲負載、較重渲染、背景預取、多個排隊請求 |
| 生命週期 | 新對話替換舊對話、切角色、取消排隊、取消生成、客戶端斷線、服務重啟 |
| 音訊 | EOS、長句截斷、靜音、爆音、接縫、stream/cache 重播、慢消費者背壓 |
| 遊戲 | 一般對話、自由對話、男聲、嘴型、重播、跳過、存檔完成判定 |

盲聽時隱藏模型／方案名稱並隨機排序，分開問「是否同一個人」「發音是否正確」「是否自然」，避免把語氣變化直接判成換人。ASR 與 speaker embedding 指標只作輔助；音量、語速或波形相似不代表聲紋一致。

## 9. 主要修改位置

| 路徑 | 預計責任 |
|---|---|
| `Tools/ok-tts/src/engine.rs` | 模型變體、profile 條件、串流引擎 |
| `Tools/ok-tts/src/talker.rs` | Base prompt、維度參數化、取樣及可能的 graph 優化 |
| `Tools/ok-tts/src/server.rs` | 快取命中、去重、排隊、取消、完成狀態與觀測 |
| `Tools/ok-tts/src/text.rs` | 語言、文字正規化、舊 voice 映射 |
| `Tools/ok-tts/src/codec.rs` | 串流批量、context 成本與接縫驗證 |
| `Tools/ok-tts/src/audio.rs` | PCM／OGG 契約、裁切、限幅與原子寫入 |
| `Tools/ok-tts/src/main.rs` | 模型／profile 設定、診斷及基準測試入口 |
| `Source/OpenKoikatsu/OKStreetSocialVoice.inl` | 一般對話請求、profile 與錯誤政策 |
| `Source/OpenKoikatsu/OKStreetSocialScene.cpp` | 自由對話片段、串流、預取、取消與起播指標 |
| `Source/OpenKoikatsu/OKCrowdRuntimeScene.cpp` | 實際 PCM 消費、underrun 與播放生命週期 |

新增 profile／scheduler 模組依責任拆分，避免繼續把所有邏輯集中到 server 或 scene 大檔。

## 10. 風險與回退

| 風險 | 控制與回退 |
|---|---|
| Base 聲線較穩但 prefill 變慢 | 預算參考條件、量測 prefix 重用；未達標不替換預設 |
| 0.6B 發音／聲線退步 | 保留 1.7B 為正式候選，不以速度掩蓋品質問題 |
| 參考音訊含噪音或錯誤逐字稿 | 離線檢查與人工核准，profile hash 固定來源 |
| 小批量 codec 負載高，大批量又延遲 | 首包與後續分開策略，依起播與 underrun 選擇 |
| 併發修改共享 KV／GPU 狀態 | 明確引擎所有權；只將 metadata、IO、後處理移出鎖 |
| 模型切換佔滿顯存 | 不同時常駐 VoiceDesign 和 Base；顯存不足明確失敗 |
| 新快取／協定破壞舊客戶端 | 版本化、能力協商、舊端點相容測試 |
| 自動 fallback 換人 | 明確設定與記錄，不悄悄更換 profile 或模型 |

發布保留上一版執行檔、設定、profile revision 與 stage。以明確 backend/profile 設定切回舊版，不修改角色卡，不覆蓋舊快取。正式切換前先完成試點聲線與使用者試聽；技術測試通過不等於使用者已接受聲音。

## 11. 完成交付

- [ ] Rust/Lua 實作與必要 UE 整合。
- [ ] 有版本的 profile schema 與本機建置／驗證工具。
- [ ] 現況、1.7B Base、0.6B 候選的相同語料比較報告。
- [ ] 端到端延遲、RTF、顯存、遊戲 frame time、盲聽與故障測試結果。
- [ ] 可試聽且能追溯 profile／模型／文字的本機樣本索引。
- [ ] 套件版本、檔案 hash、部署方式與可操作的回退步驟。
- [ ] 所有聲線未驗證前，文件明確列出支援與未驗證範圍。

## 12. 參考依據

本機原始碼與 2026-10-22 日誌是現況依據；`Docs/STREET_SOCIAL_TTS_VOICE.md` 和 `E:/TTS/STATUS.md` 的 2026-10-10 資料僅作歷史背景，不覆蓋現有 Rust 串流實作。

- [Qwen3-TTS 官方模型與功能表](https://github.com/QwenLM/Qwen3-TTS#released-models-description-and-download)：1.7B／0.6B Base、VoiceDesign、串流能力。
- [官方 Voice Design then Clone 流程](https://github.com/QwenLM/Qwen3-TTS#voice-design-then-clone)：先設計參考聲音，再建立可重用的 clone prompt。
- [官方推論實作](https://github.com/QwenLM/Qwen3-TTS/blob/main/qwen_tts/inference/qwen3_tts_model.py)：voice clone prompt 與推論契約的核對來源。
- [官方技術報告](https://arxiv.org/abs/2601.15621)：模型架構與研究評估。官方最低延遲宣稱不能當作本機遊戲整合已達成的結果。

實作時應固定參考程式與模型的 commit／revision，避免跟隨 main 變更導致測試不可重現。

## 13. 實作紀錄（2026-10-22，第一輪）

本輪做的是 P0／P1 中伺服器能單獨完成的部分，加上自由對話的 UE 請求欄位。**聲線一致性（P2）還沒動**，玩家聽到的聲音仍是 VoiceDesign 逐句合成，換人問題依舊存在。

### 13.1 已實作

| 項目 | 位置 | 內容 |
|---|---|---|
| 快取命中不等 GPU | `server.rs` | 伺服器自己載入一份 `voices.json`；選聲線、算 cache key、讀快取都不經引擎鎖。模型載入期間也能回答已快取的句子與 `/voice`。 |
| 串流快取命中 | `server.rs`、`audio.rs` | `/synthesize_stream` 命中時解碼 OGG 成 PCM 一次送出，與重播檔是同一份裁切後的音訊；選 OGG 解碼而不另存 PCM sidecar，因為快取只需一種檔案，解碼成本實測 10～20 ms。 |
| 同句去重與 pending 狀態 | `server.rs` | 以 cache 檔路徑為鍵的 in-flight 表（pending → ready／failed，Condvar 通知）。串流中同一句的 `/synthesize` 等那份檔案，不再重算；擁有者失敗或取消時，等待者自己接手。 |
| OGG 移出引擎鎖 | `server.rs` | 串流先結束 HTTP body、釋放引擎，再編碼與原子寫檔；寫檔數量受同時請求數限制，沒有額外背景佇列。 |
| 原子寫檔 | `audio.rs` | 暫存檔名每次唯一；Windows 目標檔被開啟而無法取代時，保留現有檔案。 |
| 取消 | `server.rs` | 請求可帶 `channel`、`generation`；`POST /cancel {"channel","below"}` 之後，該 channel 較舊 generation 的請求在取得引擎時與每個串流片段之間停止，不寫半成品快取。 |
| 請求狀態與觀測 | `server.rs` | `request_id`（可由客戶端給）、`segment_index`；每個請求一行 `REQ` 紀錄：`queue_enter／queue_leave／first_pcm／generated／cache_ready／end`（同一程序的 monotonic 時間，以伺服器收到請求為零點），狀態分 `complete／cancelled／hangup／failed`，cache 分 `hit／joined／miss`。`GET /requests[?id=]` 查最近 256 筆。`/health` 回報 `protocol: 2`、capabilities 與 PCM 格式（s16le、24 kHz、mono；`Content-Type` 為相容舊客戶端仍是 `audio/L16`，另加 `X-Sample-Format: s16le`）。 |
| 文字正規化 | `text.rs` | 結尾或句末標點前的停頓符號移除（「找她，。」→「找她。」、「沒聽見，」→「沒聽見。」），附單元測試。受影響的句子 cache key 會變，第一次會重新生成。 |
| 串流批量參數 | `engine.rs`、`main.rs` | 首段固定 4 frames，後續段由 `serve --stream-later-frames N`（預設 4，不變）或請求欄位 `stream_later_frames` 指定，供比較用。 |
| 基準測試 | `bench.rs`（`ok-tts bench`） | Rust HTTP/1.1 客戶端，與遊戲一樣收 chunked 串流；記錄首包、結束、伺服器紀錄，並以 UE 的 4,800 samples 起播門檻模擬播放，算出起播時間與斷音秒數。`--fresh` 每輪換快取目錄量未命中，`--later-frames 4,8,12` 每句輪流跑各設定，輸出 JSON。 |
| UE 自由對話 | `OKStreetSocialScene.cpp` | 請求帶 `request_id`、`channel`（每次遊戲執行一個 GUID，避免重啟後被舊的取消門檻擋掉）、`generation`、`segment_index`；新回覆開始時對舊 generation 呼叫 `/cancel`；第 i 句串流完成就開始第 i+1 句，不再等第 i 句的檔案回應；重播檔路徑改以計數判定全部回齊。舊伺服器會忽略新欄位，`/cancel` 對舊伺服器是 404，不影響播放。 |

`CACHE_VERSION` 與 cache key 組成本輪沒有改（模型、聲線、取樣都沒變），既有快取繼續有效。第 7 節要求的 key 加入權重 revision、取樣參數等，留到換 Base 時一併改版。

### 13.2 測量（本機 RTX 5090，遊戲同時在跑，非正式驗收）

都是開發參考：每組 48 次以下，未達第 4 節「每情境至少 100 次」的要求；GPU 同時有遊戲與另一個 ok-tts，負載會起伏。原始 JSON 在當次 session 的暫存目錄，未納入版控。

| 情境 | 舊版（`Build/ok-tts`，faa4f4bc…） | 新版 |
|---|---:|---:|
| 長句生成中，另一句已快取的 `/synthesize` | 2.547 s（等引擎鎖） | 0.006 s |
| 長句生成中，串流一句已快取的句子 | 首包 0.105 s，但整句重新生成（2.434 s） | 首包 0.018 s，讀快取 |
| 串流中同一句的 `/synthesize` | 等引擎鎖後讀檔 | 等串流結束後 30 ms 的 OGG 落盤，不重算 |
| 取消（3 個請求，0.4 s 後 `/cancel`） | 無此功能 | 生成中的請求約 0.1 s 內停止，兩個排隊請求取得引擎後立即退出，未留快取 |

後續批量比較（16 句 × 3 輪 × 每句輪流 4／8／12，皆未命中）：

| later frames | 首包 P50／P95 | 模擬起播 P50／P95 | RTF P50／P95 | 斷音 |
|---:|---:|---:|---:|---:|
| 4 | 0.102／0.119 s | 0.186／0.325 s | 0.287／0.331 | 0 |
| 8 | 0.103／0.112 s | 0.251／0.413 s | 0.261／0.314 | 0 |
| 12 | 0.102／0.116 s | 0.319／0.347 s | 0.256／0.301 | 0 |

較大的後續批量只省約一成生成時間，卻讓起播變晚：首段 4 frames 經前導靜音裁切後常不足 0.2 s，要等第二段，第二段越大越晚到。**決定維持 4**。同一天較重負載時另一次 48 句（只跑 4）：首包 P95 0.600 s、RTF P95 1.59、斷音 P95 0.35 s／最大 1.13 s，說明持續供音在 GPU 競爭下仍不夠，這是 P3 要處理的主要風險，而非批量大小。

### 13.3 未完成與下一步

- UE 改動（含自由對話每句「請求→開始播放」「回覆→開始播放」的 `Free talk: TTS line N plays ...` 日誌）只做了編譯確認，見 13.5；尚未打包新 stage、沒有在遊戲裡實際對話驗收；`Build/ok-tts` 的正式執行檔沒有替換（遊戲當時正使用它）。部署需要在無人使用時換 exe 並重新產生 `package.json` 的 hash。
- 固定語料：`Tools/ok-tts/bench/corpus_zh.txt`（第 8 節文字維度）。
- 一般腳本對話（`OKStreetSocialVoice.inl`）仍走 `/synthesize` 整句，沒有串流、沒有新欄位與延遲日誌。自由對話的延遲日誌尚未在遊戲裡跑過。
- P2 只做了 x-vector 試驗（見 13.4），還沒有 profile 格式、沒有接進伺服器。ICL 模式（參考音訊＋逐字稿）需要 speech tokenizer 的 `encoder.*`（225 個張量，參考音訊轉 codec tokens），尚未實作。

### 13.4 P2 試驗：Base 1.7B x-vector 複製

官方 `generate` 在 `x_vector_only_mode` 下只用 speaker embedding，不需要參考 codec tokens：prefill 的 codec 列變成 `think, think_bos, 語言, think_eos, 說話者, pad, bos`，instruct 為空。本輪據此實作：

- `speaker.rs`：Base 的 `speaker_encoder.*`（ECAPA-TDNN，76 個張量）在 CPU 以 f32 執行；mel 依官方 `mel_spectrogram`（n_fft 1024、hop 256、128 帶 Slaney、0～12 kHz、反射填補）。輸出 2048 維，與 talker hidden 相同。附 FFT 與濾波器的單元測試。**尚未與官方 Python 的中間值逐項核對**（下一輪先做，dump 工具放在既有 `reference/`）。
- `talker.rs` `build_prompt` 可選擇插入 speaker 列；`engine.rs` `speak_clone`。Base 的 talker／code predictor／codec 張量與 VoiceDesign 同名同形，codec 檔逐位元組相同，直接用現有載入。
- `ok-tts consistency`：同一聲線先由 VoiceDesign 說一句參考句與 16 句語料，再由 Base 以參考句的 embedding 說同樣 16 句；另請其他聲線（含男聲 m0）各說 3 句作「別人」對照。`ok-tts speaker-sim` 比對任意 WAV（`檔名#1`／`#2` 取前／後半）。

自我檢查（聲線 08，`speaker-sim`）：同一段錄音的前後半，原始 cosine 0.988、扣除全體平均後 +0.34；男聲對參考 0.936～0.941／−0.36～−0.44。編碼器有鑑別力，但原始 cosine 全都擠在 0.93～0.99，只有扣平均後的值可讀。

三個聲線的結果（扣平均後的 cosine，數字越高越像參考句的說話者）：

| 聲線 | VoiceDesign 各句→參考 | Base 複製各句→參考 | VoiceDesign 句間 | Base 句間 | 其他女聲→參考 | 男聲 m0→參考 |
|---|---:|---:|---:|---:|---:|---:|
| 08 | −0.10 | +0.30 | +0.05 | +0.24 | −0.08～−0.23 | −0.16 |
| 00 | −0.08 | +0.30 | +0.02 | +0.22 | −0.06（08） | −0.22 |
| 03 | −0.05 | +0.19 | +0.05 | +0.19 | −0.04（08） | −0.13 |

解讀：VoiceDesign 同一聲線的不同台詞，對參考句的相似度和「另一個女聲」差不多，句與句之間也幾乎不相關——與「換句就像換人」的症狀一致，支持第 3.1 節的推測。Base 複製的台詞明顯更接近參考、彼此也更一致。**這只是輔助指標**：編碼器未經官方數值核對、同一模型的編碼器評自己的條件有循環偏差、每聲線只有 16 句。Base 單句生成比 VoiceDesign 慢（本輪 RTF 平均 0.53～3.1，含首句 CUDA graph 擷取與 GPU 競爭，未分開量），延遲要另測。

試聽樣本與 JSON（本機、不進版控）：`Saved/TTS/Consistency-20261022/`（聲線 08，`ref.wav`、`design_NN.wav`、`base_NN.wav`、`other_*.wav`，另含 bench JSON）、`Saved/TTS/Consistency-20261022-p00/`、`-p03/`。

下一輪建議順序：官方數值核對 speaker encoder → 盲聽 08／00／03 的 design 與 base 樣本 → 若通過，定 profile 格式（參考音訊 hash、embedding、模型 revision），伺服器加 Base 後端（不與 VoiceDesign 同時常駐）與新 cache key → 量 Base 的串流首包與 RTF。

### 13.5 UE 編譯確認

以 `D:/PhysX/UE5.8` 建置 C checkout 的 `OpenKoikatsu Win64 Development` 遊戲目標：`OKStreetSocialScene.cpp`（含本輪全部改動）編譯通過，沒有新的警告。整個目標沒有建完：`Plugins/OKStudio` 的 `OKSceneDocument.h` 在遊戲目標下有既有的 C4430／C2143 錯誤，與本輪改動無關，需要負責 OK Studio 的那條線處理後才能產出遊戲執行檔。打包 StreetSocial 走 Interaction host（`rebuild_street_social_runtime.lua`），不受這個遊戲目標影響，但本輪沒有打包。

### 13.6 Base 後端接進伺服器（2026-10-23）

使用者試聽 13.4 的樣本後確認：Base 複製的句子聽起來都是同一個人，明顯比 VoiceDesign 好。因此照 P2 做成可部署的後端。

- **聲音 profile**（`profile.rs`，`ok-tts profile`）：每個聲線（女聲性格 × 年齡 18／21／25／35／45／12，加男聲 m0～m9）由 VoiceDesign 說同一句參考句（`profile::DEFAULT_REF_TEXT`），存成 `<key>@<age>.wav`，再用 Base speaker encoder 算出 embedding 存成 `<key>@<age>.json`（schema 1：instruct／seed／參考文字／參考 WAV 的 sha1、模型 snapshot、encoder 版本、embedding）。設計或參考句變了才重做，`--force` 全部重做。profile 放在 exe 旁的 `profiles/`（`Build/` 不進版控）。本機已建好 262 份（`Build/ok-tts/profiles/`，約 106 MB），參考錄音 6～12 秒。
- **後端選擇**（`serve`）：`profiles/` 有 profile 且找得到 Base 模型時預設用 Base；`--backend design` 強制回到 VoiceDesign，`--backend base` 強制 Base。只載入其中一個模型，不同時佔兩份顯存。Base 下的聲線沒有 profile 時回明確錯誤，不改用其他聲音（一般對話照原政策退回內建錄音，自由對話只剩字幕）。
- **cache key**：Base 為 `ok-tts-base-1|Base 模型 ID|embedding 的 sha1 前 16 碼|seed|文字`，與 VoiceDesign 的快取分開；profile 重做後不會重播舊聲音。舊的 VoiceDesign 快取保留，切回 `--backend design` 時仍可用。
- `/health` 回報 `backend`、`profiles`、模型 ID；`/voice` 回報該聲線的 profile id 與是否存在。
- 回退：把 `profiles/` 改名，或啟動時加 `--backend design`。

延遲（同一份 `bench/corpus_zh.txt` 19 句，未命中串流，遊戲同時在跑）：

| 後端 | 次數 | 首包 P50／P95 | 模擬起播 P50／P95 | RTF P50／P95 | 斷音 |
|---|---:|---:|---:|---:|---:|
| Base | 54 | 0.099／0.201 s | 0.137／0.279 s | 0.155／0.234 | 0 |
| VoiceDesign | 54 | 0.093／0.103 s | 0.169／0.261 s | 0.262／0.352 | 0 |
| Base（第二次，兩個伺服器同時載入） | 36 | 0.167／0.320 s | 0.223／0.452 s | 0.259／0.382 | 0 |

Base 沒有比 VoiceDesign 慢；第一次量到 RTF 較低（不用帶聲音描述，prefill 較短），但第二次負載較重時與 VoiceDesign 相當，還不能說「比較快」。

部署（2026-10-23）：`Build/ok-tts/ok-tts.exe` 換成本版（以 78796ab8 建置，sha256 前 16 碼 bd4eabf009d35bd2）；舊版改名為 `ok-tts.before-base.exe`（faa4f4bc…），舊 `package.json` 備份為 `package.before-base.json`，新的 `package.json` 記錄全部檔案雜湊與 profile 數。部署時舊版正被另一個 session 的遊戲使用：Windows 允許將執行中的 exe 改名，那個程序繼續跑舊版，下次啟動遊戲才換成 Base。以部署位置的新 exe 不帶參數啟動驗證過：自動選 Base、載入 262 份 profile、無缺漏、串流正常。回退：把 `profiles/` 改名，或把 `ok-tts.before-base.exe` 改回 `ok-tts.exe`。

尚未完成：還沒在遊戲裡實際對話確認。speaker encoder 仍未與官方數值逐項核對（使用者試聽已確認效果）。英文台詞在 Base 下用中文參考句的聲紋說，沒有另外驗證。

### 13.7 壓縮的聲音檔、執行中換聲音、聲音存進角色卡（2026-10-23）

使用者要求：profile 預設壓縮；玩家可在遊戲中重新產生最符合角色描述的聲音並固定存檔；聲音可以存在角色卡裡。

**`.okvoice` 格式**（`profile.rs`）：一個檔案包含中繼資料 JSON（schema 2：id、來源 design／runtime、聲音描述原文、seed、參考句、參考音訊 sha1、模型 snapshot、encoder 版本、建立時間）、bf16 聲紋（2048 維，4 KB；talker 本來就以 bf16 使用，存 bf16 沒有損失）、OGG 參考錄音。schema 1 的 `<id>.json`＋`<id>.wav` 在執行 `ok-tts profile` 時自動轉換並刪除舊檔。本機 262 份從 106 MB 降到 18 MB（每份約 60 KB，其中約 55 KB 是參考錄音，保留它是為了能試聽與日後重算聲紋）。玩家在遊戲中選定的 profile（來源 runtime）不會被 `ok-tts profile` 覆蓋，除非加 `--force`。

**角色卡**（`cardvoice.rs`）：同一份 `.okvoice` 位元組放在 PNG 的 `okVo` 區塊（輔助、私有、可安全複製，與 `okPv`／`okLc` 等同類），插在 IEND 前；影像不重新編碼，其他區塊與 IEND 之後的原作卡資料不變，寫入前先讀回驗證。重做 OK 卡時只會移除 `okLb`／`okLc`／`okPv`，所以 `okVo` 會被保留。移除聲音後，卡片與原檔逐位元組相同（已在 APNG 卡 `CrowdAPNG/C009.png` 的副本上驗證）。CLI：`ok-tts card-voice --card X.png [--set v.okvoice | --remove]`。

**伺服器**（`voicemgr.rs`，Base 後端）：
- 合成請求可帶 `card`（她的卡片絕對路徑）；卡裡有聲音就用它，否則用性格預設 profile。卡片依修改時間快取，讀不到時記錄並退回預設。快取鍵以實際使用的聲紋區分。`/requests` 的紀錄有 `voice: card|profile|design`。
- `POST /voice/candidates`：VoiceDesign 只在這次呼叫中與 Base 一起載入，用（可自訂的）描述與新的 seed 產生 1～6 個參考錄音，算出聲紋，再由 Base 用每個候選說一句預覽，輸出 OGG。2 個候選約 19 秒，期間其他合成會等候。
- `POST /voice/fix`：把候選（或目前的預設聲音）存進角色卡，及／或設為該性格的預設（舊檔保留為 `.okvoice.prev`），立即生效。`GET /voice/card`、`POST /voice/card/remove`。

**修正的顯存洩漏**：`Engine` 的欄位依宣告順序釋放，`gpu`（stream）排在第一個，之後模型權重要在已銷毀的 stream 上 `free_async`，失敗被忽略，卸下的引擎整份模型（約 6 GB）留在顯存。改為 `gpu` 最後釋放（`Gpu` 內部也改成 cuBLAS 與 workspace 先於 stream 釋放）。修正前每次產生候選後多佔約 6.3 GB，修正後第一次約 +0.7 GB，之後不再增加（背景有遊戲與 Editor，數字有雜訊）。這個問題也影響 `ok-tts profile` 與 `consistency`（先後載入兩個模型）。

**遊戲**（`OKStreetSocialVoicePick.inl`）：女角的語音請求（腳本台詞與自由對話）帶上她的卡片路徑（`HighCards[i]`）。右側語音面板在她的聲音描述下方顯示「卡裡是否有專屬聲音」，並有「換聲音…」：可編輯的聲音描述（預設為性格描述）、產生 3 個候選、每個候選可「試聽」（她用候選聲音說預覽句）、「聽參考句」、「存進她的角色卡」、「設為這個性格的預設」；另有「把目前的聲音存進她的角色卡」與「還原為性格預設聲音」。介面字串在三種語言的 `ui_text.toml`。玩家角色的聲音仍由男聲選單決定，不讀他的卡。

尚未完成：遊戲端只做了編譯確認，沒有打包 stage、沒有在遊戲中實際操作；伺服器端流程（候選、存卡、用卡合成、存預設、移除）已用測試卡副本驗證。

### 13.8 遊戲中實測（2026-10-23）

以新 stage `VoicePick02`（D: 引擎、`FreeTalk01HostD`）在沙盒中實際操作（`run_street_social_development.lua -Sandbox VoiceTest -PortBase 48060`，用卡片副本與沙盒存檔，與另一個 session 正在跑的 Conquest 遊戲互不影響），經控制通道（`street_play.lua`，`OK_CONTROL_PORT=48070`）驗證：

| 項目 | 結果 |
|---|---|
| 自由對話串流 | 她的每句帶卡片路徑；卡裡沒有聲音時 `voice: profile`，串流後的檔案請求直接命中快取 |
| 換聲音面板 | 顯示「她目前用这个性格的默认声音」；「换声音…」→ 可編輯描述 →「生成 3 个候选声音」約 38 秒；每個候選有試聽、聽參考句、存卡、設預設 |
| 試聽 | 她用候選聲音說預覽句（`mic off` 下；開著房間麥克風時，喇叭或環境聲可能被當成插話而中斷預覽，與她平常說話的行為相同） |
| 存進她的角色卡 | 寫入卡片副本（+56 KB），原卡不變；之後她的自由對話與腳本台詞都是 `voice: card`；玩家台詞仍用男聲選單（m0） |
| 重新啟動遊戲 | 存了聲音的卡片正常載入，她仍用卡裡的聲音，好感與記憶都在（修正身分 bug 之後，見下） |
| 設為性格預設 | 寫入 `08@18.okvoice` 並保留 `.prev`；測完已還原成原檔 |
| 還原為性格預設聲音 | 卡片與原檔逐位元組相同，她改回 `voice: profile` |
| 自訂描述 | 「低沉、略帶沙啞、成熟冷靜」描述產生 1 個候選約 16 秒 |

**實測中發現並修正的問題**
- **存了聲音後她變成陌生人**：角色 ID 是 `apng-` 加整個卡片檔的 sha256，加入 `okVo` 後 ID 改變，重開遊戲時她叫「March 7th」、好感 0。改為計算 ID 時去掉 `okVo` 區塊（`prepare_street_social_selection.lua` 的 `card_identity`）；因為移除該區塊後與原卡逐位元組相同，存了聲音的卡片 ID 與原本一致，舊存檔不需遷移。`verify_street_social_contacts.lua` 改用同一個函式。
- **狀態訊息看不到**：訊息原本在候選清單最後，面板太長時被擠到畫面外；改放在「换声音…」按鈕下方。

**沙盒模式**（`run_street_social_development.lua -Sandbox NAME [-PortBase N]`）：同一個 checkout 已有遊戲在跑時，另開一個不干擾它的遊戲。使用自己的連接埠（預設 48060 起），存檔、語音設定與自由對話紀錄放在 `Saved/StreetSocial/Sandbox/NAME/`（第一次從主存檔複製），不停掉別人的 ok-tts／ok-asr，也不重建 `Tools/ok-rules`（只複製現有 DLL），不做存檔 ID 遷移。

**給使用者測試**：`StreetSocial.cmd` 會跑最新的 stage（`VoicePick02`）與已部署的 `Build/ok-tts`。對女角按 E 後，在右側面板她的 TTS 提示下方找「换声音…」。存進角色卡會改寫真正的卡片檔（只加 `okVo` 區塊，不影響身分與存檔）；「还原为性格默认声音」可恢復原檔。

## 14. talker int8 權重（2026-10-24）

- **目的**：讓遊戲在 16 GB 顯卡上能和 TTS、ASR、產歌同時執行。量到的常駐顯存：遊戲 6.3 GB、ok-tts 4.5～4.9 GB、ok-asr 2.7 GB。
- **做法**：
  - 28 層 talker 的 qkv、o、gate/up、down 矩陣改成 int8，每列一個 f32 縮放（對稱量化，縮放 = max |w| / 127）。
  - 程式碼放在共用的 `gpu::W`（ok-song 的 HeartMuLa 也用它）。
  - 逐幀解碼（列數 ≤ 4）用 `gemv_i8_bf16`：一個 block 先把輸入向量放進 shared memory，16 個 warp 各算一個輸出。
  - 預填時先展開成 bf16，再交給 cuBLAS。
  - 5 層的 code predictor 與嵌入表維持 bf16。
  - 環境變數 `OK_TTS_WEIGHTS=bf16` 可切回原本的 bf16（parity 比對要用 bf16）。
- **結果**（RTX 5090，`ok-tts bench`，語料 `bench/corpus_zh.txt`）：

  | 項目 | bf16 | int8 |
  |---|---:|---:|
  | 常駐顯存（Windows GPU Process Memory） | 4.54 GB | 3.52 GB |
  | 串流 RTF p50（GPU 較空閒時交替三組） | 0.353／0.338／0.339 | 0.336／0.338／0.343 |
  | 首段音訊 p50 | 0.24～0.30 s | 0.25～0.35 s |

  - GPU 被其他遊戲占用時，量測波動很大。曾經量到 int8 慢 15%，改成交替量測後兩者相同。
  - `ok-tts gemv-bench` 在 CUDA graph 內量單一矩陣，int8 大多比 bf16 快，最多快 5 倍。
- **品質**：
  - 同一句、同一固定種子下，bf16 與 int8 的聲紋相似度 0.995。bf16 自己重跑是 1.000，因為有固定種子。
  - 18 句 ASR 轉寫有 15 句相同；其餘 3 句只差語氣詞或標點，例如「不要了」對「不要啦」。
- **部署**：`Build/ok-tts/ok-tts.exe` 已更新。正在執行的服務在下次重啟後才會換成新版。
