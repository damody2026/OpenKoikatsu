# 街頭規則搬到 Rust（ok-rules）— 2026-10-13

## 目的
規則判斷（誰在什麼條件下怎麼變）從 UE C++ 搬到 `Tools/ok-rules`（`ok_rules.dll`，C ABI），C++ 只負責收集事實、套用結果、存檔與呈現。
好處：`cargo test` 不開 UE 就能驗證規則；改數值不用重編（tuning 熱重載）；改規則只要 `cargo build` 幾秒，遊戲執行中就能換上新 DLL。

## 設計
- **Rust 不保存狀態**：存檔仍在 `FOKStreetStore`。每次呼叫帶齊需要的事實，回傳「要寫什麼、發生了什麼」。所以 DLL 可以隨時替換，也沒有存檔遷移。
- **介面**：`okr_eval(handle, request_json)`，用 `rule` 分派（沒有 `rule` 時才看 `kind`，方便手寫請求）。請求自己的資料可以用 `kind`（傳聞種類、活動種類），不會影響分派。新系統只加新的規則名，ABI 不變。
- **數值**：`Content/IndependentResources/StreetSocialData/rules/*.toml`。檔案不在時用編進 DLL 的同一份（`include_str!`），stage 出去的版本規則一樣。
- **DLL 不存在或打不開**：畫面顯示錯誤，5 秒後關閉遊戲（`OKStreetRulesFatal`）。不保留 C++ 備援，避免兩份規則分歧。
- **FFI 安全**：每個入口 `catch_unwind`，release `panic = "unwind"`；`debug_panic` 測試確認 panic 只會變成錯誤字串。

## 檔案
| 檔案 | 內容 |
|---|---|
| `Tools/ok-rules/src/lib.rs` | C ABI、`Rules`（open / eval / reload） |
| `Tools/ok-rules/src/tuning.rs` | 讀 tuning 檔，缺檔用內建 |
| `Tools/ok-rules/src/bonds.rs` | 女主彼此好感度與性向的規則與測試 |
| `Tools/ok-rules/include/ok_rules.h` | C 標頭（ABI 版本 1） |
| `Content/.../StreetSocialData/rules/bonds.toml` | 好感度數值 |
| `Source/OpenKoikatsu/OKStreetRules.h/.cpp` | UE 載入器、熱重載、console 指令、致命錯誤畫面 |
| `Source/OpenKoikatsu/OKStreetSocialBonds.inl` | 收集事實 → `okr_eval` → 寫回 store |

## 好感度（第一個系統）
| request `kind` | 用途 |
|---|---|
| `bonds_defaults` | 起始好感、結算間隔 |
| `bonds_tick` | 每 15 遊戲分鐘：同地點同活動、一起跟著他 → 好感變化與階段轉換 |
| `bond_event` | 單次事件（`encounter`、`encounter_rivals`、`stood_up`、`bluff_game`，數值在 toml `[events]`） |
| `bond_mood` | 要不要一起行動：0 可以、1 勉強（女友）、2 拒絕 |

## 開發流程
- `StreetSocial.cmd` / `run_street_social_development.lua` 啟動時自動 `cargo build --release`（已是最新就不做事），複製 DLL 到 `Saved/StreetSocial/Build/ok-rules/` 再載入，所以遊戲執行中 cargo 仍可重編。
- 改數值：存 `bonds.toml` → console `ok.StreetRules.Reload`，或 `Tools\lua\lua.exe Tools/street_play.lua rules reload`。
- 改規則：`cargo test` → `cargo build --release` → 同樣的 reload，會換上新 DLL（複製成 `reload/ok_rules-<時間>.dll` 後載入）。
- 直接問規則：`Tools\lua\lua.exe Tools/street_play.lua rules eval '{"kind":"bond_mood",...}'`（只回答，不套用）。
- 狀態：`ok.StreetRules.Status` 或 `street_play.lua rules`。

## 新增一個規則系統
1. `Tools/ok-rules/src/<system>.rs`：資料結構、規則函式、`eval(kind, request)`、單元測試。
2. tuning 放 `StreetSocialData/rules/<system>.toml`，在 `tuning.rs` 加上 `include_str!` 內建版。
3. `lib.rs` 的 `Rules::eval` 依 `kind` 分派到新模組。
4. C++：收集事實組 request → `S.Rules->Eval` → 寫回 store。不要在 C++ 留第二份規則。

## 量測
| 項目 | 搬遷前（UE C++） | 搬遷後 |
|---|---|---|
| 改一條好感度規則到可執行 | UE 增量重編 97 秒 + stage + 重啟遊戲 | （待量） |
| 改一個數值 | 同上 | 存檔 + reload，不重啟 |

## 驗證紀錄
（待補）

## 全面搬遷（2026-10-13 起：遊戲邏輯盡量都在 Rust，Unreal C++ 只做呈現）

共用基礎：`relation.rs`（關係欄位雙方同名，只回傳變動）、`rng.rs`（UE `FRandomStream` 逐位相同，有 seed 的結果不變；原本沒有 seed 的擲骰改由 C++ 傳 `seed`/`roll`）、`effects.rs`（效果清單：現金、銀行、過帳、公司錢包、關係、記憶、紀錄、傳聞、訊息、日誌，C++ `ApplyRulesEffects` 依序重播；會寫入存檔的傳聞與紀錄在候選存檔提交後才執行）。

| 批次 | 內容 | 狀態 |
|---|---|---|
| 0 | 共用基礎 | 完成 |
| 1 | 女主互動（閘門、同意、反應、紀錄、傳聞）、地點規範與人潮、邀約與主動靠近、偏見與心情、模型關係增減驗證、議價、金錢壓力 | 完成，已編譯 |
| 2 | 換日：女主數值與關係、個人金錢、銀行與學期學費、托付與創業、公司與不動產（含女主的公司、薪資稅） | 完成，已編譯 |
| 3 | 女主每日行程（日程、各自的早晨、跑腿、朋友見面、提早出門、雨天、接送、急需插入、點餐、天氣與節日、營業時間） | 完成，已編譯 |
| 4 | 校園（科系表、課表、出席、考試、獎學金、讀書、退學與重考、系辦） | 完成，已編譯 |
| 5 | 社交模擬（約定、劇情模式與 foil、傳聞與吃醋、偶遇、撞見、主動找他、導演、偏見行動、自己傳訊、接電話、生日） | 完成，已編譯（自動成為朋友併入 7） |
| 6 | 活動結算、結帳與口味、送禮、借錢、跑腿、朋友聚會、打工、私密之後、吹牛、刮刮卡、預言、女主投資與建議、破產與父親 | 完成，已編譯 |
| 7 | 對話核心（台詞目錄與 rules.json 由 Rust 讀、條件、可說的台詞、話題分支、告別、記錄效果、好感上限、成為朋友與告白、邀約、收集話題、自動成為朋友、反省） | 完成，已編譯 |
| AI | C++ 不再直接呼叫 AI：自由對話（含無 ok-mind 時的直連備援，已移除）、判讀、群聊、她傳的訊息都改成 ok-mind（Rust）的 chat 工作，結果在 TickMind 依工作 id 交回 | 完成，已編譯 |

保留在 C++：每幀執行的路徑（行人移動、對話對象選擇、用餐進度）、帳本核對、介面、存檔讀寫、語音（ASR/TTS 本地服務）、提示詞的組字（把場景事實寫成文字）。模型呼叫只經 ok-mind（Rust）。
