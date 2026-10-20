# StreetSocial TTS 人聲

日期：2026-10-10。

## 做了什麼

StreetSocial 的對話可以選擇兩種人聲：

- **內建**：原本的錄音（預設）。
- **TTS**：把這句中文台詞送給本機的 TTS 伺服器 `ok-tts`（Rust，CUDA），伺服器合成 OGG，遊戲照常播放。

遊戲中按 **V** 或點對話面板的「V 人聲：內建／TTS」切換。下一句開始生效，選擇存在 `Saved/StreetSocial/voice-settings.json`，下次啟動沿用。

模型與聲音提示沿用 `E:\TTS\STATUS.md`：`Qwen/Qwen3-TTS-12Hz-1.7B-VoiceDesign`，talker temperature 0.65、top_k 30、repetition 1.05；code predictor 0.9／50；提示表（42 個性格 × 年紀）從 `E:\TTS\gen_ages.py` 匯出到 `Tools/ok-tts/voices.json`。

## 遊戲流程

1. `QueueNode` 一律先準備好內建錄音。
2. TTS 模式且語音是中文時，另外 POST 到 `http://127.0.0.1:47860/synthesize`：台詞（`[H名]`、`[P名]` 已換成名字）、性格編號、年紀、輸出資料夾。
3. 伺服器拿掉「」『』♡♪ 等符號、繁轉簡、套該性格的提示，寫 `Saved/StreetSocial/TTSCache/<sha1>.ogg`（先寫暫存再改名），回傳路徑。同一句同一聲音第二次直接用快取；同一句每次合成結果相同。
4. 遊戲把這個檔案登記成語音，取代這句的錄音序列，再走原本的播放路徑。嘴型、句子完成判定、存檔都不變。
5. 伺服器沒開、回錯誤、或超過 `tts_timeout_seconds`（預設 15 秒）時，這句用內建錄音，面板顯示原因。

日文語音模式（L）一律用內建錄音。為什麼由遊戲播放、不讓外部程式直接播：外部播放的話，嘴型和「語音播完才算這句完成、才存好感」都會斷掉。介面用本機 HTTP 而不是 MCP：呼叫端只有遊戲，UE 內建 HTTP 模組即可。

## ok-tts（Rust）

`Tools/ok-tts`，不需要 Python 也不需要 CUDA toolkit，只需要 NVIDIA 驅動。

- `cudarc`（動態載入 driver／cuBLAS／NVRTC）。kernel 在 `src/kernels.cu`，啟動時用 NVRTC 編譯；bf16 自己處理，不依賴 CUDA 標頭。
- talker（28 層）與 code predictor（5 層）bf16，捨入時機照 PyTorch；單句無 padding，M-RoPE 退化成一般 RoPE。
- codec 解碼器 f32，`[C, T]` 排列；causal conv 拆成 K 次位移 GEMM，transposed conv 是 GEMM＋overlap-add。
- 取樣在 GPU 上（`sample_bf16`），token 留在顯存；code predictor 一個 frame 的 15 步錄成一個 CUDA graph 重播。主機每 4 個 frame 才查一次 EOS。
- tokenizer：`tokenizers` crate 用 `vocab.json`／`merges.txt` 組 Qwen2 byte-level BPE，`<|im_start|>`／`<|im_end|>` 手動插入。

指令：

```
ok-tts serve  [--port 47860] [--model <snapshot>] [--voices voices.json]
ok-tts say    --text 文字 [--personality 00] [--age 18] [--out x.ogg|x.wav] [--greedy] [--runs N]
ok-tts parity --reference <dump_reference.py 的輸出>
```

模型位置：`--model`、`OK_TTS_MODEL`，或預設的 `E:\TTS\hf-cache\...\snapshots\<rev>`。

### 發布

`Tools/ok-tts/package.ps1` → `Build/ok-tts/`：`ok-tts.exe`、`voices.json`、`ok-tts-serve.cmd`、`cuda/`（cublas64_12、cublasLt64_12、nvrtc64_120_0、nvrtc-builtins，NVIDIA 可再散布的 DLL；預設從 `CUDA_PATH\bin` 或 PyTorch 附帶的同名檔複製）、`package.json`（SHA256）。只留 `C:\WINDOWS\system32` 的 PATH 下實測可跑。

啟動器：設定是 TTS 而伺服器沒回應時，`StreetSocial.cmd` 會最小化啟動 `Build/ok-tts/ok-tts-serve.cmd`（沒有時改用 `E:\TTS\start_tts_server.cmd`，它現在也轉呼叫 Rust 版）。舊的 Python 伺服器只留作對照：`E:\TTS\tts_server.py`／`start_tts_server_python.cmd`。

### 與 Python 的對照（`ok-tts parity`）

參考資料由 `Tools/ok-tts/reference/dump_reference.py`、`dump_predictor.py` 在 `E:\TTS\.venv-triton` 產生（只在開發時用）。

| 項目 | 結果 |
|---|---|
| tokenizer 三個案例 | 完全相同 |
| talker 輸入 embedding、trailing text | 逐位元相同 |
| prefill 最後一格 logits | 相對誤差 0.7%，argmax 相同 |
| code predictor（同一輸入 teacher forcing，15 步） | 相對誤差約 0.5%，argmax 14/15 相同（不同的那步是近乎平手） |
| codec（同一組 code） | 各段 0.5–2%，最終聲波 1%（Python 端是 bf16） |
| greedy 整句 | 50 frame vs 52 frame。bf16 平手處分岔，所以不要求序列相同 |

### 速度（RTX 5090，暖機後）

| 情況 | 結果 |
|---|---|
| 27 字、6.96 秒音訊，GPU 空閒 | 0.94 秒（0.035 秒／字） |
| 7 字、1.04 秒音訊，GPU 空閒 | 0.18 秒 |
| 遊戲同時以 1080p、150% 渲染，19 字、5.68 秒音訊 | 1.63 秒 |
| 冷啟動到 READY（含暖機一句） | 約 7 秒 |

對照：Python Triton 版平均 0.087 秒／字（STATUS.md）；改用 GPU 取樣與 CUDA graph 前，Rust 版在遊戲負載下一度要 6.3 秒。

## 遊戲端檔案

| 檔案 | 作用 |
|---|---|
| `Source/OpenKoikatsu/OKStreetSocialVoice.inl` | 設定讀寫、切換、健康檢查、請求與退回內建 |
| `Source/OpenKoikatsu/OKStreetSocialScene.cpp` | `QueueNode` 送出請求；Tick 等待回覆或逾時；V 鍵與按鈕；`-StreetSocialTtsSmoke` |
| `Source/OpenKoikatsu/OKCrowdRuntimeScene.cpp` | 語音登記多接受 `generated_voice_root` 底下的 OGG |
| `Tools/prepare_street_social_selection.lua` | manifest 加 `generated_voice_root`＝`Saved/StreetSocial/TTSCache` |
| `Tools/run_street_social_development.lua` | 設定是 TTS 而伺服器沒回應時自動啟動 |

`voice-settings.json`：

```json
{
  "voice_mode": "tts",
  "tts_endpoint": "http://127.0.0.1:47860",
  "tts_age": 18,
  "tts_timeout_seconds": 15,
  "tts_server_command": "C:\\portable\\OpenKoikatsu\\Build\\ok-tts\\ok-tts-serve.cmd"
}
```

`tts_age` 可用 12、18、21、25、35、45。`tts_server_command` 只有啟動器讀，可省略。端點只接受 `127.0.0.1`／`localhost`。

## 驗證

- 遊戲執行檔 `StreetTts01.stage`（SHA256 `58c225be…1eed`）：五輪街區驗收（Contacts、Acceptance、Presentation、Dialogue、Touch）passed、0 錯誤；三張卡（C019、C044、C004）畫面比對 passed；已用 `publish_street_social_appearance.py` 發布 receipt，`StreetSocial.cmd -ValidateOnly` 通過。自測不讀使用者設定，TTS 在自測中停用。
- `-StreetSocialTtsSmoke`（強制 TTS、自動和 1 號女角交談）：Python 伺服器與 Rust 伺服器各跑過；都在 1–2.5 秒內換上 TTS 語音、播完、`mouth_peak` 0.47–0.63。Rust 版 OGG（vorbis_rs）UE 解碼正常。
- 試聽頁：`E:\TTS\samples\rust\index.html`（C00–C10，18 歲，旁邊放舊 Python 版同一檔對照）。

## 還沒確認

- 沒有人工試聽 Rust 版的聲音；判斷只靠上面的數值對照和各性格語速差。
- 只測了單句；長句（>300 frame，24 秒以上）走 codec 分段路徑，沒有實測。
- talker 的逐步解碼還沒錄成 CUDA graph（位置每步不同）。
