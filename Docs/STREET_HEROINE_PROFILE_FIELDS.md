# 街區女主人設欄位（satiety_max、traits.yandere、心情）

2026-10-27，C 線（R3-268、R3-269）。E 線整合時照這份格式。

## 放在哪裡

每位女主一份 `Content/IndependentResources/StreetSocialData/Profiles/apng-<sha256>.json`（ok-mind `Profile`，`Tools/ok-mind/src/profile.rs`）。遊戲第一次用到時複製到存檔的 `Saved/StreetSocial/mind/heroines/`。

下面兩個欄位屬於「人設」，不是存檔狀態：載入時一律以 Content 那份為準，舊存檔沒有這兩個欄位也會套用（`Profiles::load`）。

## satiety_max（頂層，整數）

```json
"satiety_max": 150,
```

- 她的胃有多大：範圍 50～1000，一般人 100。沒寫就是 100。
- 遊戲裡的飽足度仍存 0～100，意思是「胃裝了幾成」。所以各處門檻（92 飽、115 撐太飽、45／35／20 餓…）對每個人都成立，不用另外改。
- 食物給的是絕對量。換算成她的飽足度要乘上 `100 / satiety_max`：大胃王吃同一份，飽足漲得比較少。消化速度（每小時下降多少）每個人都一樣，是百分比。
- 換算的地方：
  - Rust：`needs.rs advance_with`（她自己的用餐時段）、`plan.rs meal`（她自己買的飯）、`shop.rs order_too_full`（點這道會不會太撐）。
  - C++：`FeedHer`、同桌用餐、`TooFullFor`、菜單上「吃不下」的標記。
- 面板顯示絕對值：`飽足 {目前}/{上限}`，目前 ＝ 飽足度 × 上限 ÷ 100。
- 目前的值：

  | 女主 | satiety_max |
  |---|---|
  | 貞德 | 160 |
  | 惠惠 | 150 |
  | 貞德·Alter | 120 |
  | 刻晴 | 110 |
  | 安可 | 90 |
  | 爱丽丝 | 80 |
  | 其他人 | 100 |

## traits.yandere（traits 底下，布林）

```json
"traits": { ..., "yandere": true }
```

- 病嬌。沒寫就是 false，存檔時 false 不會寫出來。
- 卡片本身的 Koikatsu 性格是 24（病嬌）時也算，前提是 request 的 profile 帶了 `"personality": 24`（ok-rules `traits.rs`）。
- 作用（R3-268，`social.rs call_cancel`）：已經是戀人時，他打電話取消約會，她不扣好感，而是直接走來找他，進入 1 對 1（`YandereComes` → `BeginPrivate`）。
- 目前沒有女主標成病嬌。

## 心情（存檔狀態，不是人設）

- `FOKStreetRelation.MoodLevel` / `MoodMinute`，和 Rust `Relation.mood_level` / `mood_minute` 對應。
- 範圍 -100～100，0 是平常。會慢慢回到 0，半衰期 360 遊戲分鐘（`relation.rs mood_at`）。
- 存檔位置：關係紀錄的 `"mood"` 物件裡的 `level`、`level_minute`。同一個物件原本就存了短期情緒：`emotion`、`intensity`、`minute`。
- 分級（`mood_band`）：

  | 分級 | 範圍 |
  |---|---|
  | low | ≤ -45 |
  | down | -45 ～ -15 |
  | usual | -15 ～ 15 |
  | good | 15 ～ 45 |
  | high | ≥ 45 |

- 給 AI 的方式：每輪狀態訊息的 needs 那段後面接 `ui.mind.spirits_status`。平常（usual）不寫，也不放進快取的靜態前綴。
- 右側面板：`ui.scene.stats_line` 的 `{mood}`。
- 目前會改變心情的事件：惠惠的爆裂魔法（`social.rs explosion_cast`）。
  - 成功率依施法前的飽足度：0 時 5%，80 以上 95%，中間線性。
  - 放完飽足清 0。
  - 成功心情 +30，失敗 -30。
  - 她的反應由 AI 決定：系統提示是 `ui.photos.explosion_came_off_prompt` 和 `ui.photos.explosion_fizzled_prompt`。
