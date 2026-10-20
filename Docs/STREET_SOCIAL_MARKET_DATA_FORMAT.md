# StreetSocial 股市資料格式（A 股、美股）

資料放在 `Content/IndependentResources/MarketData/`，這個資料夾**不進 git**（已列在 `.gitignore`）。要用 `Tools/build_ashare_data.lua` 和 `Tools/build_us_data.lua` 重新產生。遊戲端由 Rust（`ok_mind.dll`）讀取。

## 目錄

```
MarketData/
  cn/
    manifest.json        版本、來源、產生時間、期間、每檔列數與 sha256
    stocks.json          股票清單
    calendar.json        交易日陣列 ["2010-01-04", ...]
    index/<code>.okbar   指數日線（上證 sh.000001、深成 sz.399001、創業板 sz.399006、滬深300 sh.000300、科創50 sh.000688）
    daily/<code>.okbar   個股日線，code 例如 sh.600519、sz.300750、bj.430047
  us/
    manifest.json
    stocks.json
    calendar.json
    index/<symbol>.okbar  SPY、QQQ、DIA、IWM（ETF 當作指數）；^GSPC、^IXIC、^DJI（如果抓得到）
    daily/<symbol>.okbar  symbol 裡的 "." 和 "/" 換成 "-"，例如 BRK-B
  fx/
    usd_twd.okfx          每日匯率（1 USD 兌多少 TWD）
    cny_twd.okfx          每日匯率（1 CNY 兌多少 TWD）
```

## `.okbar`（zstd 壓縮）

整個檔案是**一個 zstd frame**，解壓後依序是 little-endian 的 header 和固定長度的資料列。

**Header（16 bytes）：**

| 偏移 | 型別 | 欄位 | 說明 |
|---|---|---|---|
| 0 | char[4] | magic | `OKBR` |
| 4 | u16 | version | 1 |
| 6 | u16 | market | 1 = CN，2 = US |
| 8 | u32 | rows | 資料列數 |
| 12 | u32 | price_scale | 1000，價格以「元或美元 × 1000」的整數存放 |

**每列 64 bytes：**

| 偏移 | 型別 | 欄位 | 說明 |
|---|---|---|---|
| 0 | i32 | date | YYYYMMDD（交易所當地日期） |
| 4 | i32 | open | **不復權**原始價 × 1000 |
| 8 | i32 | high | 同上 |
| 12 | i32 | low | 同上 |
| 16 | i32 | close | 同上 |
| 20 | i32 | preclose | 前收盤，交易所公布的值（除權後已調整）；US 用前一列 close 除以當日 split_ratio |
| 24 | i64 | volume | 股數 |
| 32 | f64 | amount | 成交額（CNY 或 USD）；US 沒有這筆資料時用 close × volume 估算 |
| 40 | f32 | split_ratio | 當天生效的拆股或送股倍數，例如 4 拆 1 寫 4.0；沒有就是 1.0。CN 的「10 送 5」寫 1.5 |
| 44 | f32 | dividend | 當天除息的每股現金股利（原幣） |
| 48 | i32 | limit_up | 漲停價 × 1000（CN）；US 填 0 |
| 52 | i32 | limit_down | 跌停價 × 1000（CN）；US 填 0 |
| 56 | u8 | flags | bit0 停牌、bit1 ST 或 *ST、bit2 上市首日、bit3 這列由第二來源補齊 |
| 57 | u8[7] | reserved | 填 0 |

**規則：**

- **只存有交易的日子，停牌除外。** CN 停牌日也要存一列：flags bit0 = 1，OHLC 等於前收，volume = 0。US 沒有成交的日子不存。
- **價格一律存不復權的原始價。** 復權交給 Rust 用 split_ratio 和 dividend 計算，所以遊戲裡看到的是當年真實的價格（例如 2010 年的 AAPL 大約 250 美元）。
- **CN 漲跌停價：** 依當日前收、板塊、ST 狀態、上市首日規則計算，四捨五入到 0.01 元：
  - 主板 ±10%（ST ±5%）
  - 創業板：2020-08-24 起 ±20%，之前 ±10%
  - 科創板 ±20%
  - 北交所 ±30%
  - 新股首日：主板是 44% 和 −36%；創業板、科創板改制後前 5 日不設漲跌停，填 0
  - 計算出的值必須和真實的漲停或跌停收盤一致，驗證時要抽查。

## `stocks.json`

```json
[{"code":"sh.600519","symbol":"600519","name":"贵州茅台","name_zh_tw":"貴州茅臺","board":"sh_main|sz_main|chinext|star|bse|nyse|nasdaq|amex|arca|other",
  "list_date":"2001-08-27","delist_date":null,"is_index":false,"st_ranges":[["2015-01-01","2015-06-30"]],"rows":4000,"first":"20100104","last":"20261009"}]
```

## `.okfx`（zstd 壓縮）

**Header（12 bytes）：** `OKFX`、u16 version=1、u16 reserved、u32 rows。

**每列 12 bytes：** i32 date、f64 rate。

沒有資料的日子（假日）由 Rust 往前取最近一筆。

## 已知限制

- **US 存活者偏差：** 免費來源大多只有目前仍上市的股票，期間內下市的股票（例如 2010 年後被併購或破產的公司）可能缺漏。manifest 要記錄缺漏原因。
- **US 價格基準：** US 的 `.okbar` 仍是 thetrading.tools 的**拆股調整後價格**（`split_ratio` 都是 1.0、`dividend` 都是 0，manifest 寫 `price_basis: split_adjusted_thetrading_tools`）。
  - `market.sqlite` 裡的 US 日線由 `Tools/build_us_actions.lua load` 改寫成**當年實際成交價**（`symbols.price_basis = raw`），並填入拆股和現金股利；來源、規則和例外清單記在 meta `manifest.us_actions`。
  - 拆股和股利來自 Yahoo Finance chart API（`events=div,split`）。`split` 填在除權日，合股小於 1；`div` 是除息日每股現金股利，已換算回當時的股數。少數分拆（spin-off）被 Yahoo 記成拆股，例如 APTV 2017 的 1.193。
  - 價格：兩個來源每日漲跌一致時，用 Yahoo 未四捨五入的價格換算回原始價，所以拆股多次的股票，早年的價格也不會因為 0.01 的四捨五入而失真。一致性不足的 32 檔仍用 thetrading.tools 的價格乘上拆股倍數。
  - Yahoo 抓不到的 14 檔標成 `raw_no_action_source`：保留原本的價格，沒有拆股或股利資料。
  - thetrading.tools 對部分股票另外扣掉了特別股利。Yahoo 的價格沒有扣，所以 sqlite 用的是 Yahoo 的價格。
  - 少數小型股的合股日期和價格跳動對不上，清單在 `suspect_splits`。
  - **重建注意：** 重跑 `build_market_sqlite.lua` 會用 `.okbar` 蓋回拆股調整價，之後要再跑一次 `build_us_actions.lua load`。
- **US 財報：** `Tools/build_us_fundamentals.lua` 從 SEC EDGAR XBRL（data.sec.gov companyfacts）寫入 `fundamentals`，說明記在 meta `manifest.us_fundamentals`。
  - 數值是**公司自己會計年度**的年初累計值：`Q1` 3 個月、`H1` 6 個月、`Q3` 9 個月、`FY` 12 個月。`period_end` 是公司的會計期末，不一定是日曆季末。
  - `available` 是第一份報告這一期的 10-Q 或 10-K 的申報日，數值也取那一份的原始版本，不採用之後的重編。
  - 每股數字（`eps`、`eps_ttm`、`dividend_ps`、`bvps`、`total_shares`）以申報當時的股數為準。遇到之後的拆股，遊戲要自己用 `split` 換算。
  - 外國公司的 20-F 可能用其他幣別，以 `currency` 欄為準。
- **US 存活者偏差（續）：** 已試過 Yahoo、Stooq、Nasdaq、MarketWatch、stockanalysis，都找不到免費又可靠的下市股票日線，所以仍沒有補上。

## SQLite（`market.sqlite`）

遊戲優先讀 `MarketData/market.sqlite`，沒有這個檔案時才讀上面的 `.okbar`、`.okfx`。

- **產生：** `Tools/build_market_sqlite.lua` 把 `cn/`、`us/`、`fx/` 轉進資料庫。
  - 重跑時會保留其他腳本寫進去的表格，例如財報。
  - 先寫到 `market.sqlite.building`，完成後才換掉舊檔，所以中途失敗不會留下壞掉的資料庫。
- **補資料：** 直接往表格新增或更新資料列即可，不必重新打包整個資料集。
  - 補完要跑 `cargo test --release`，其中有一項會檢查資料庫和 `.okbar` 逐筆一致。

**日期與價格：** 日期一律是整數 `YYYYMMDD`，價格是「元或美元 × 1000」的整數，和 `.okbar` 相同。

| 表格 | 內容 |
|---|---|
| `meta(key, value)` | `schema_version`、`built_at`、各來源的 manifest（`manifest.cn`、`manifest.us`、`manifest.cn_fundamentals`…，JSON） |
| `symbols(id, market, code, symbol, name, name_zh_tw, board, list_date, delist_date, is_index, etf, first, last, st_ranges, industry, cik, price_basis)` | 股票清單；`market` 是 `cn` 或 `us`；`price_basis` 是 `raw`（當年實際成交價，遊戲自己處理拆股和股利）或來源的調整方式 |
| `calendar(market, day)` | 交易日 |
| `bars(sid, day, o, h, l, c, pc, vol, amount, split, div, lu, ld, flags)` | 日線，欄位意義同 `.okbar`；`sid` 對應 `symbols.id` |
| `fx(pair, day, rate)` | `usd_twd`、`cny_twd` |
| `fundamentals(sid, period_end, period, available, currency, revenue, net_income, eps, eps_ttm, bvps, roe, total_shares, equity, total_assets, operating_cf, dividend_ps, source)` | 財報 |

**`fundamentals` 的規則：**

- **累計值：** 每期是當年度的累計值（年初到該期），和中國財報的慣例相同。
- **`period`：** `Q1`、`H1`（美股可寫 `Q2`）、`Q3`、`FY`。
- **`available`：** 遊戲裡最早能看到這份財報的日期。
  - 有實際公告日就用公告日。
  - 沒有就用法定期限：一季報 04-30、半年報 08-31、三季報 10-31、年報隔年 04-30。
  - 遊戲只會顯示 `available` 不晚於當天的財報。
- **金額單位：** 一律是元或美元，不用萬元、億元。

**遊戲端：**

- **報價：** `quote` 會附上 `fundamentals`，內容是最新一期財報，加上用當下股價算出的 `pe_ttm`、`pb`、`market_cap`、`dividend_yield`。
- **歷史財報：** 新增查詢 `fundamentals`，回傳歷年各期財報。
- **日 K、週 K、月 K：** 以畫面上最後一根 K 線為基準做「前復權」，拆股和除息不會在 K 線上留下缺口。
  - 分時圖、報價和下單仍然用當天的實際價格。
- **拆股、合股、股利：** A 股和美股（`price_basis = raw`）都會在除權日調整持股或入帳。
  - 美股的合股倍數小於 1。
  - 放空的部位遇到除息要付股利。

## 不得露出遊戲日期之後的資料

遊戲只能顯示「遊戲當天以前」就已經存在、已經公開的東西。新增查詢或畫面時都要遵守下面這些規則，`cargo test` 裡的 `nothing_after_the_game_day_is_shown` 會檢查。

| 項目 | 規則 |
|---|---|
| 股票本身 | 遊戲當天還沒上市的股票不存在：報價回 `not_listed`，搜尋、自選股、漲跌排行、熱度列都不會出現它的代號或名稱 |
| 已下市的股票 | 搜尋不再列出；已持有的部位仍顯示最後收盤 |
| 價格 | 只用遊戲當下已收盤的 K 線，加上正在交易那根的「到目前為止」部分（成交量、成交額都按經過的比例） |
| 前復權 | 只用畫面上已出現的 K 線計算，不參考之後的除權事件 |
| 財報 | `available` 不晚於遊戲當天才顯示；拆股或送轉後、新財報公布前，每股數字用拆股倍數換算 |
| 名稱與代號 | 照 `names` 表顯示當年的名稱和代號，例如 2012 年的 Google 顯示 GOOG。沒有歷史資料時，A 股依當天的 ST 狀態加上或去掉「ST」 |
| 例外 | 「筆記本：前世記得的事」本來就是重生者記得的未來，可以列出未來的比賽結果、指數走勢和股票漲幅 |
