-- Build the StreetSocial China A-share daily dataset.
--
-- Format: Docs/STREET_SOCIAL_MARKET_DATA_FORMAT.md
-- Output: Content/IndependentResources/MarketData/cn/  (git-ignored)
--
-- Sources
--   * BaoStock  - SH/SZ stock list (incl. delisted), trade calendar, unadjusted daily
--                 bars (adjustflag=3), dividend / bonus-share records, 4 indices
--                 (BaoStock's TCP protocol is spoken by `ok-luakit baostock`).
--   * AKShare-equivalent calls (Tools/market_akshare.lua) - Beijing Stock Exchange stocks (Eastmoney daily
--                 bars + Sina volumes + Eastmoney dividend plans) and the STAR 50 index (Sina).
--
-- Every network response is cached under --raw (default: a scratch dir) so a crash or
-- Ctrl-C can resume without re-downloading.  Downloaded data is only parsed as
-- numbers/strings, never executed.
--
-- Steps (run all with `all`, or one by one):
--   meta      stock list + trade calendar
--   sse       SH/SZ daily bars + dividends (parallel BaoStock sessions)
--   bj        Beijing Stock Exchange via AKShare-equivalent calls
--   index     indices
--   pack      write .okbar / stocks.json / calendar.json / manifest.json
--
-- Dev tool:  Tools\lua\lua.exe Tools/build_ashare_data.lua all
local ok = require "ok"
local json, path, kit, util, proc = ok.json, ok.path, ok.kit, ok.util, ok.proc
local md = require "market_data_common"

local M = {}
M.DEFAULT_OUT = path.join(ok.root, "Content/IndependentResources/MarketData/cn")
local function default_raw()
  local base = os.getenv("OK_MARKET_RAW") or path.join(os.getenv("TEMP") or "/tmp", "ok_market_raw")
  return path.join(base, "cn")
end

local START_DATE = "2010-01-01"
local KFIELDS = "date,open,high,low,close,preclose,volume,amount,tradestatus,isST"
local IFIELDS = "date,open,high,low,close,preclose,volume,amount"
local INDICES = {
  { "sh.000001", "000001", "上证综合指数", "baostock" },
  { "sz.399001", "399001", "深证成份指数", "baostock" },
  { "sz.399006", "399006", "创业板指数", "baostock" },
  { "sh.000300", "000300", "沪深300指数", "baostock" },
  { "sh.000688", "000688", "上证科创板50成份指数", "akshare_sina" },
}

-- Rule dates
local CHINEXT_REFORM = "2020-08-24"      -- ChiNext ±20 %, IPO first 5 days unlimited
local MAIN_REGISTRATION = "2023-04-10"   -- main-board registration IPOs: first 5 days unlimited
local IPO_44_RULE = "2014-01-01"         -- main/ChiNext first day +44 % / -36 %
local MAIN_ST_10PCT = "2026-07-06"       -- main-board ST/*ST limit 5 % -> 10 % (verified from data: last 5 % hits
                                         -- 2026-07-03, first >5 % moves 2026-07-06)
local O = json.object
local NULL = json.null
local log = md.log

local function split_fields(s) return util.split(s, ",") end

-- ---------------------------------------------------------------- cache helpers
local function write_json(p, obj, indent)
  md.write_atomic(p, json.encode(obj, { ensure_ascii = false, indent = indent }))
end
local write_rows_gz, read_rows_gz = md.write_rows_gz, md.read_rows_gz

-- ---------------------------------------------------------------- baostock (ok-luakit baostock)
-- jobs: {{fn=..., kw=json.object, out=file}} -> runs them on `workers` BaoStock sessions; returns the summary.
local function baostock_run(jobs, workers, label)
  if #jobs == 0 then return { done = 0, errors = 0, aborted = false } end
  local spec = path.temp(".bsjobs")
  local list = {}
  for i, j in ipairs(jobs) do list[i] = O({ { "fn", j.fn }, { "kw", j.kw }, { "out", path.norm(j.out) } }) end
  path.write_bytes(spec, json.encode({ jobs = list }, { ensure_ascii = false }))
  local res = proc.run({ kit.exe(), "baostock", spec, "--workers", tostring(workers), "--label", label },
    { stdout_only = true, env = { OK_TZ_OFFSET = tostring(math.floor(md.tz_offset())) } })
  path.remove(spec, true)
  if not res.ok then error("ok-luakit baostock failed (" .. res.code .. ")") end
  return json.decode(res.stdout)
end
-- One query (BS().query) in a fresh session: fields, rows or error.
local function bs_query(fn, kw)
  local out = path.temp(".bs.json")
  baostock_run({ { fn = fn, kw = kw, out = out } }, 1, "baostock")
  local r = path.exists(out) and ok.read_json(out) or { error = "no result" }
  path.remove(out, true)
  if r.error then error(r.error, 0) end
  return r.fields, r.rows
end

-- ---------------------------------------------------------------- step: meta
local function step_meta(raw, end_)
  local today = os.date("%Y-%m-%d")
  local _, cal = bs_query("query_trade_dates", O({ { "start_date", START_DATE }, { "end_date", today } }))
  local days = {}
  for _, r in ipairs(cal) do if r[2] == "1" then days[#days + 1] = r[1] end end
  if end_ == nil then
    -- last trading day strictly before today (today's session may not be closed/published)
    for _, d in ipairs(days) do if d < today then end_ = d end end
  end
  local kept = {}
  for _, d in ipairs(days) do if d <= end_ then kept[#kept + 1] = d end end
  days = kept
  local fields, basic = bs_query("query_stock_basic", O())
  write_json(path.join(raw, "meta", "calendar.json"), days)
  write_json(path.join(raw, "meta", "stock_basic.json"), O({ { "fields", fields }, { "rows", basic }, { "end", end_ },
    { "fetched", os.date("%Y-%m-%dT%H:%M:%S") } }))
  log(("meta: %d trading days %s..%s, %d securities"):format(#days, days[1], days[#days], #basic))
end

local function load_meta(raw)
  return ok.read_json(path.join(raw, "meta", "calendar.json")), ok.read_json(path.join(raw, "meta", "stock_basic.json"))
end

local function startswith_any(s, list) for _, p in ipairs(list) do if util.startswith(s, p) then return true end end return false end
local function board_of(code)
  local parts = util.split(code, ".")
  local ex, n = parts[1], parts[2]
  if ex == "sh" then
    if startswith_any(n, { "688", "689" }) then return "star" end
    if startswith_any(n, { "600", "601", "603", "605" }) then return "sh_main" end
    return nil -- 900xxx B shares etc.
  end
  if ex == "sz" then
    if startswith_any(n, { "300", "301", "302" }) then return "chinext" end
    if startswith_any(n, { "000", "001", "002", "003", "004" }) then return "sz_main" end
    return nil -- 200xxx B shares
  end
  if ex == "bj" then return "bse" end
  return nil
end
M.board_of = board_of

local function sse_universe(raw)
  local _, sb = load_meta(raw)
  local end_ = sb["end"]
  local out = {}
  for _, row in ipairs(sb.rows) do
    local code, name, ipo, outd, typ, status = table.unpack(row, 1, 6)
    local b = typ == "1" and board_of(code) or nil
    if b and not (util.truthy(outd) and outd < START_DATE) and not (not util.truthy(ipo) or ipo > end_) then
      out[#out + 1] = { code = code, name = name, ipo = ipo, out = util.truthy(outd) and outd or nil, status = status, board = b }
    end
  end
  return out
end

-- ---------------------------------------------------------------- step: sse
local function detect_ex_years(rows)
  local years = {}
  for i = 2, #rows do
    local pc = md.pyfloat(util.truthy(rows[i][6]) and rows[i][6] or "0")
    local prev = md.pyfloat(util.truthy(rows[i - 1][5]) and rows[i - 1][5] or "0")
    if pc and prev then
      if pc > 0 and prev > 0 and math.abs(pc - prev) >= 0.005 then
        years[rows[i][1]:sub(1, 4)] = true
        years[rows[i - 1][1]:sub(1, 4)] = true -- ex-date may sit inside a suspension gap
      end
    end
  end
  local out = {}
  for y in pairs(years) do out[#out + 1] = y end
  table.sort(out)
  return out
end
M.detect_ex_years = detect_ex_years

local function step_sse(raw, workers, limit)
  local uni = sse_universe(raw)
  local end_ = ok.read_json(path.join(raw, "meta", "stock_basic.json"))["end"]
  -- delisted first: no other free source has them
  uni = util.sorted(uni, function(u) return { u.status == "1" and 1 or 0, u.code } end)
  local todo = {}
  for _, u in ipairs(uni) do
    if not (path.exists(path.join(raw, "k", u.code .. ".csv.gz")) and path.exists(path.join(raw, "div", u.code .. ".json"))) then
      todo[#todo + 1] = u.code
    end
  end
  if limit then todo = util.slice(todo, 1, limit) end
  log(("sse: universe %d, to fetch %d with %d workers"):format(#uni, #todo, workers))
  local errors = O()
  local tmp = path.temp(".sse")
  path.mkdir(tmp, true)
  -- phase 1: k-line bars
  local jobs = {}
  for i, code in ipairs(todo) do
    if not path.exists(path.join(raw, "k", code .. ".csv.gz")) then
      jobs[#jobs + 1] = { code = code, fn = "query_history_k_data_plus", out = tmp .. "/k" .. i .. ".json",
        kw = O({ { "code", code }, { "fields", KFIELDS }, { "start_date", START_DATE }, { "end_date", end_ }, { "frequency", "d" }, { "adjustflag", "3" } }) }
    end
  end
  local res = baostock_run(jobs, workers, "sse bars")
  for _, j in ipairs(jobs) do
    if path.exists(j.out) then
      local r = ok.read_json(j.out)
      if r.error then errors[j.code] = r.error else write_rows_gz(path.join(raw, "k", j.code .. ".csv.gz"), split_fields(KFIELDS), r.rows) end
      path.remove(j.out, true)
    end
  end
  -- phase 2: dividend records for the years with an ex-rights preclose jump
  if not res.aborted then
    jobs = {}
    local by_code = {}
    for _, code in ipairs(todo) do
      local kpath, dpath = path.join(raw, "k", code .. ".csv.gz"), path.join(raw, "div", code .. ".json")
      if path.exists(kpath) and not path.exists(dpath) and errors[code] == nil then
        local krows = {}
        for _, r in ipairs(read_rows_gz(kpath)) do
          local row = {}
          for k, f in ipairs(split_fields(KFIELDS)) do row[k] = r[f] end
          krows[#krows + 1] = row
        end
        local years = detect_ex_years(krows)
        by_code[code] = years
        if #years == 0 then write_json(dpath, O()) end
        for _, y in ipairs(years) do
          jobs[#jobs + 1] = { code = code, year = y, fn = "query_dividend_data", out = tmp .. "/d_" .. code .. "_" .. y .. ".json",
            kw = O({ { "code", code }, { "year", y }, { "yearType", "operate" } }) }
        end
      end
    end
    baostock_run(jobs, workers, "sse dividends")
    local got = {}
    for _, j in ipairs(jobs) do
      got[j.code] = got[j.code] or {}
      got[j.code][j.year] = path.exists(j.out) and ok.read_json(j.out) or { error = "not run" }
      path.remove(j.out, true)
    end
    for _, code in ipairs(todo) do
      local years = by_code[code]
      if years and #years > 0 then
        local div, err = O(), nil
        for _, y in ipairs(years) do
          local r = got[code][y]
          if r.error then err = r.error; break end
          local recs = {}
          for _, row in ipairs(r.rows) do
            local d = O()
            for k, f in ipairs(r.fields) do if row[k] ~= nil then d[f] = row[k] end end
            recs[#recs + 1] = d
          end
          div[y] = recs
        end
        if err then errors[code] = err else write_json(path.join(raw, "div", code .. ".json"), div) end
      end
    end
  else
    log("sse: BaoStock blacklisted this IP - aborting; resume later (cache is kept)")
  end
  path.rmtree(tmp)
  write_json(path.join(raw, "meta", "sse_errors.json"), errors, 1)
  log(("sse: finished, %d errors"):format(#json.keys(errors)))
end

-- ---------------------------------------------------------------- step: bj (AKShare-equivalent)
local function retry(fn, tries, base)
  tries, base = tries or 6, base or 3.0
  local last
  for i = 0, tries - 1 do
    local okv, v = pcall(fn)
    if okv then return v end
    last = v
    ok.time.sleep(base * 2 ^ i + math.random() * 2)
  end
  error(last, 0)
end
local function err_repr(e) return util.usub(tostring(e), 1, 300) end

local function step_bj(raw, sleep)
  local ak = require "market_akshare"
  local end_ = ok.read_json(path.join(raw, "meta", "stock_basic.json"))["end"]
  local lpath = path.join(raw, "bj", "list.csv.gz")
  if not path.exists(lpath) then
    local f = retry(function() return ak.stock_info_bj_name_code() end)
    write_rows_gz(lpath, f.columns, f.rows)
  end
  local lst = read_rows_gz(lpath)
  local errors = O()
  local s, e = START_DATE:gsub("-", ""), end_:gsub("-", "")
  local em_dead = 0
  for n, it in ipairs(lst) do
    local sym = it["证券代码"]
    -- sina = primary bars (shares volume); em = optional (gives exchange preclose via change);
    -- fhps = dividend / bonus plans with ex-dates.
    local jobs = {
      { "sina", 5, function() return ak.stock_zh_a_daily("bj" .. sym, s, e) end },
      { "em", em_dead > 0 and 1 or 2, function() return ak.stock_zh_a_hist(sym, s, e) end },
      { "fhps", 4, function() return ak.stock_fhps_detail_em(sym) end },
    }
    for _, job in ipairs(jobs) do
      local tag, tries, fn = job[1], job[2], job[3]
      local p = path.join(raw, "bj", tag, sym .. ".csv.gz")
      if not (path.exists(p) or (tag == "em" and em_dead >= 5)) then
        local okv, f = pcall(retry, fn, tries, 2.0)
        if okv then
          write_rows_gz(p, f.columns, f.rows)
          if tag == "em" then em_dead = 0 end
        else
          errors[sym .. ":" .. tag] = err_repr(f)
          if tag == "em" then em_dead = em_dead + 1 end -- Eastmoney blocks bursts; after 5 straight failures stop asking
          if tag == "fhps" then write_rows_gz(p, { "error" }, {}) end -- dividend plan unavailable; equivalent factor fallback
        end
        ok.time.sleep(sleep + math.random() * sleep)
      end
    end
    if n % 25 == 0 then log(("bj: %d/%d, errors %d"):format(n, #lst, #json.keys(errors))) end
  end
  write_json(path.join(raw, "meta", "bj_errors.json"), errors, 1)
  log(("bj: finished %d stocks, %d errors"):format(#lst, #json.keys(errors)))
end

-- ---------------------------------------------------------------- step: sina (fallback for SH/SZ)
-- Second source for SH/SZ when BaoStock is unreachable: Sina unadjusted daily bars plus Sina
-- dividend (分红) and rights-issue (配股) history.  Rows built from it get flags bit3.
local function step_sina(raw, sleep, only_missing, reverse)
  if only_missing == nil then only_missing = true end
  local ak = require "market_akshare"
  local end_ = ok.read_json(path.join(raw, "meta", "stock_basic.json"))["end"]
  local uni = sse_universe(raw)
  local todo = {}
  for _, u in ipairs(uni) do
    if not (only_missing and path.exists(path.join(raw, "k", u.code .. ".csv.gz"))) then todo[#todo + 1] = u end
  end
  -- listed first: Sina rarely has delisted symbols
  todo = util.sorted(todo, function(u) return { u.status ~= "1" and 1 or 0, u.code } end)
  if reverse then todo = util.reverse(todo) end -- a second process can walk the list from the other end
  local s, e = START_DATE:gsub("-", ""), end_:gsub("-", "")
  local errors, consec = O(), 0
  log(("sina: %d SH/SZ stocks without BaoStock bars"):format(#todo))
  for n, u in ipairs(todo) do
    local code = u.code
    local sym = util.split(code, ".")[2]
    local jobs = {
      { "k", function() return ak.stock_zh_a_daily((code:gsub("%.", "")), s, e) end },
      { "fh", function() return ak.stock_history_dividend_detail(sym, "分红") end },
      { "pg", function() return ak.stock_history_dividend_detail(sym, "配股") end },
    }
    for _, job in ipairs(jobs) do
      local tag, fn = job[1], job[2]
      local p = path.join(raw, "sina", tag, code .. ".csv.gz")
      if not path.exists(p) and not (tag ~= "k" and not path.exists(path.join(raw, "sina", "k", code .. ".csv.gz"))) then
        local okv, f = pcall(retry, fn, tag == "k" and 2 or 3, 2.0)
        if okv then
          write_rows_gz(p, f.columns, f.rows)
          consec = 0
        else
          local msg = err_repr(f)
          errors[code .. ":" .. tag] = msg
          -- "no data" style answers are permanent: write an empty marker so resume skips them
          local low = msg:lower()
          if tag ~= "k" or low:find("decode", 1, true) or low:find("keyerror", 1, true) or low:find("empty", 1, true) then
            write_rows_gz(p, { "error" }, {})
          else
            consec = consec + 1
          end
        end
        ok.time.sleep(sleep + math.random() * sleep)
      end
    end
    if consec >= 8 then
      log("sina: 8 consecutive network failures, backing off 10 min")
      ok.time.sleep(600)
      consec = 0
    end
    if n % 50 == 0 then log(("sina: %d/%d, errors %d"):format(n, #todo, #json.keys(errors))) end
  end
  write_json(path.join(raw, "meta", reverse and "sina_errors_rev.json" or "sina_errors.json"), errors, 1)
  log(("sina: finished %d, %d errors"):format(#todo, #json.keys(errors)))
end

-- ---------------------------------------------------------------- pack helpers
local function fnum(s, default)
  default = default or 0.0
  if s == nil or s == NULL or s == "" or s == "nan" or s == "None" or s == "NaT" then return default end
  local v = md.pyfloat(s)
  if v == nil or v ~= v then return default end
  return v
end
M.fnum = fnum

local function repair_ohlc(bars, code, stats)
  -- Sina occasionally returns o/h/l = 0 with a valid close (e.g. several STAR stocks on 2024-11-06).
  for _, b in ipairs(bars) do
    if b.open <= 0 or b.high <= 0 or b.low <= 0 then
      local c = b.close
      b.open = b.open > 0 and b.open or c
      b.high = math.max(b.high, b.open, c)
      local lo
      for _, x in ipairs({ b.low, b.open, c }) do if x > 0 and (lo == nil or x < lo) then lo = x end end
      b.low = lo
      stats.repaired_ohlc = stats.repaired_ohlc or {}
      table.insert(stats.repaired_ohlc, code .. "@" .. b.date)
    end
  end
end

local function sorted_keys(t) local k = {}; for x in pairs(t) do k[#k + 1] = x end; table.sort(k); return k end
local function bar(t) return t end

local function sina_bars(raw, code, stats)
  local kp = path.join(raw, "sina", "k", code .. ".csv.gz")
  if not path.exists(kp) then return nil end
  local krows = read_rows_gz(kp)
  if #krows == 0 or krows[1].date == nil then return nil end
  local ev, rights = {}, {}
  local fp, pp = path.join(raw, "sina", "fh", code .. ".csv.gz"), path.join(raw, "sina", "pg", code .. ".csv.gz")
  if path.exists(fp) then
    for _, r in ipairs(read_rows_gz(fp)) do
      local d = (util.truthy(r["除权除息日"]) and r["除权除息日"] or ""):sub(1, 10)
      local skip = #d ~= 10 or not d:sub(1, 1):match("%d") or (util.truthy(r["进度"]) and r["进度"] ~= "实施")
      if not skip then
        local split = 1.0 + (fnum(r["送股"]) + fnum(r["转增"])) / 10.0
        local cash = fnum(r["派息"]) / 10.0
        if split ~= 1.0 or cash ~= 0 then
          local s0, c0 = 1.0, 0.0
          if ev[d] then s0, c0 = ev[d][1], ev[d][2] end
          ev[d] = { s0 * split, c0 + cash }
        end
      end
    end
  end
  if path.exists(pp) then
    for _, r in ipairs(read_rows_gz(pp)) do
      local d = (util.truthy(r["除权日"]) and r["除权日"] or ""):sub(1, 10)
      if #d == 10 and d:sub(1, 1):match("%d") and fnum(r["配股方案"]) > 0 then
        rights[d] = { fnum(r["配股方案"]) / 10.0, fnum(r["配股价格"]) }
      end
    end
  end
  local bars = {}
  for _, r in ipairs(krows) do
    local b = { date = r.date:sub(1, 10), open = fnum(r.open), high = fnum(r.high), low = fnum(r.low), close = fnum(r.close),
      preclose = 0.0, volume = md.trunc(fnum(r.volume)), amount = fnum(r.amount), susp = false, st = false, second = true }
    if b.date >= START_DATE and b.close > 0 then bars[#bars + 1] = b end
  end
  repair_ohlc(bars, code, stats)
  bars = util.sorted(bars, function(b) return b.date end)
  local evd, rd = sorted_keys(ev), sorted_keys(rights)
  for i, b in ipairs(bars) do
    if i == 1 then
      b.preclose, b.pc_unknown = b.open, true
    else
      local pv = bars[i - 1]
      local split, cash, rr, rp = 1.0, 0.0, 0.0, 0.0
      for _, d in ipairs(evd) do
        if pv.date < d and d <= b.date then split = split * ev[d][1]; cash = cash + ev[d][2] end
      end
      for _, d in ipairs(rd) do
        if pv.date < d and d <= b.date then
          rr, rp = rights[d][1], rights[d][2]
          stats.sina_rights_events = (stats.sina_rights_events or 0) + 1
        end
      end
      b.preclose = util.round((pv.close - cash + rr * rp) / (split + rr), 2)
    end
  end
  stats.sina_stocks = (stats.sina_stocks or 0) + 1
  return bars, ev
end

-- ---------------------------------------------------------------- step: index
local function step_index(raw)
  local ak
  local end_ = ok.read_json(path.join(raw, "meta", "stock_basic.json"))["end"]
  local bs_dead = false
  for _, ix in ipairs(INDICES) do
    local code, src = ix[1], ix[4]
    local p = path.join(raw, "index", code .. ".csv.gz")
    if not path.exists(p) then
      local done = false
      if src == "baostock" and not bs_dead then
        local okv, err = pcall(function()
          local _, rows = bs_query("query_history_k_data_plus", O({ { "code", code }, { "fields", IFIELDS }, { "start_date", START_DATE },
            { "end_date", end_ }, { "frequency", "d" }, { "adjustflag", "3" } }))
          write_rows_gz(p, split_fields(IFIELDS), rows)
        end)
        if okv then
          log(("index %s fetched (baostock)"):format(code))
          done = true
        else
          log(("index: baostock unavailable (%s); using Sina fallback"):format(tostring(err)))
          bs_dead = true
        end
      end
      if not done then
        -- Sina: OHLC + volume, no amount / preclose (preclose = previous close)
        local p2 = src ~= "baostock" and p or path.join(raw, "index_sina", code .. ".csv.gz")
        if not path.exists(p2) then
          ak = ak or require "market_akshare"
          local f = retry(function() return ak.stock_zh_index_daily((code:gsub("%.", ""))) end)
          write_rows_gz(p2, f.columns, f.rows)
          log(("index %s fetched (sina)"):format(code))
        end
      end
    end
  end
end

-- ---------------------------------------------------------------- pack helpers
local function to_milli(x) return math.tointeger(math.floor(x * 1000 + 0.5)) end
local function cents(x) return math.tointeger(math.floor(x * 100 + 0.5)) end

-- Return (up, down) in milli-yuan; pct in whole percent; exchange rounding = half-up to 0.01.
local function limit_pair(pc_cents, up_pct, down_pct)
  local up = (pc_cents * (100 + up_pct) * 2 + 100) // 200
  local dn = (pc_cents * (100 - down_pct) * 2 + 100) // 200
  return up * 10, dn * 10
end

local function band_pct(board, date, is_st)
  if board == "sh_main" or board == "sz_main" then
    if is_st and date < MAIN_ST_10PCT then return 5 end
    return 10
  end
  if board == "chinext" then
    if date >= CHINEXT_REFORM then return 20 end
    return is_st and 5 or 10
  end
  if board == "star" then return 20 end
  if board == "bse" then return 30 end
  return 10
end

-- k = 0-based index of trading day since listing. Returns override or nil.
-- {"none",0,0} => no limit (store 0), {"pct",up,down} => explicit.
local function ipo_rule(board, list_date, k)
  if board == "star" then return k < 5 and { "none", 0, 0 } or nil end
  if board == "bse" then return k == 0 and { "none", 0, 0 } or nil end
  if board == "chinext" then
    if list_date >= CHINEXT_REFORM then return k < 5 and { "none", 0, 0 } or nil end
    if k == 0 then return list_date >= IPO_44_RULE and { "pct", 44, 36 } or { "none", 0, 0 } end
    return nil
  end
  if board == "sh_main" or board == "sz_main" then
    if list_date >= MAIN_REGISTRATION then return k < 5 and { "none", 0, 0 } or nil end
    if k == 0 then return list_date >= IPO_44_RULE and { "pct", 44, 36 } or { "none", 0, 0 } end
  end
  return nil
end

M.ZSTD_LEVEL = math.tointeger(tonumber(os.getenv("OK_ZSTD_LEVEL") or "19"))

local function okbar_body(rows)
  local buf = { string.pack(md.HDR, "OKBR", 1, 1, #rows, 1000) }
  for _, r in ipairs(rows) do buf[#buf + 1] = md.pack_row(r) end
  return table.concat(buf)
end

local function addlist(stats, key, v) stats[key] = stats[key] or {}; table.insert(stats[key], v) end

-- bars: {date, open, high, low, close, preclose, volume, amount, susp, st, second?, pc_unknown?}
-- events: date -> {split_ratio, dividend} from dividend records.
local function build_rows(bars, board, list_date, cal, calidx, fill_to_end, events, stats, code)
  local inside = {}
  for _, b in ipairs(bars) do if calidx[b.date] then inside[#inside + 1] = b end end
  bars = util.sorted(inside, function(b) return b.date end)
  if #bars == 0 then return {} end
  -- fill calendar gaps as suspended rows
  local by_date = {}
  for _, b in ipairs(bars) do by_date[b.date] = b end
  local i0 = calidx[bars[1].date]
  local i1 = fill_to_end and #cal or calidx[bars[#bars].date]
  local full = {}
  local prev
  for k = i0, i1 do
    local d = cal[k]
    local b = by_date[d]
    if b == nil then
      stats.filled_suspended = (stats.filled_suspended or 0) + 1
      local pc = prev.close
      b = { date = d, open = pc, high = pc, low = pc, close = pc, preclose = pc, volume = 0, amount = 0.0, susp = true,
        st = prev.st, second = prev.second or false }
    end
    full[#full + 1] = b
    prev = b
  end
  local out = {}
  local k_trade = 0
  local ev_dates = sorted_keys(events)
  local used_ev = {}
  local pend_split, pend_div = 1.0, 0.0
  prev = nil
  for _, b in ipairs(full) do
    local d = b.date
    local pc = b.preclose
    if b.susp then
      if pc <= 0 and prev ~= nil then pc = prev.close end
      b.open, b.high, b.low, b.close = pc, pc, pc, pc
      b.volume, b.amount = 0, 0.0
    end
    if pc <= 0 then pc = prev and prev.close or b.open end
    local split, divd = 1.0, 0.0
    if prev ~= nil then
      local pclose = prev.close
      -- dividend events dated in (prev.date, d]
      for _, ed in ipairs(ev_dates) do
        if not used_ev[ed] and prev.date < ed and ed <= d then
          pend_split = pend_split * events[ed][1]
          pend_div = pend_div + events[ed][2]
          used_ev[ed] = true
        end
      end
      split, divd = pend_split, pend_div
      if b.susp and math.abs(pc - pclose) < 0.005 and (split ~= 1.0 or divd ~= 0.0) then
        -- ex-date inside a suspension: the exchange preclose only moves on resumption -> carry
        split, divd = 1.0, 0.0
      else
        pend_split, pend_div = 1.0, 0.0
      end
      if math.abs(pc - pclose) >= 0.005 then
        local expected = (pclose - divd) / split
        if split == 1.0 and divd == 0.0 then
          -- no record: rights issue or missing record -> equivalent split factor
          split = pclose / pc
          addlist(stats, "equivalent", ("%s@%s:%.2f->%.2f"):format(code, d, pclose, pc))
        elseif math.abs(expected - pc) > math.max(0.015, 0.003 * pc) then
          -- record doesn't fully explain the exchange preclose (e.g. rights issue on same day)
          split = (pclose - divd) / pc
          addlist(stats, "residual", ("%s@%s:exp%.3f pc%.2f"):format(code, d, expected, pc))
        end
      elseif split ~= 1.0 or divd ~= 0.0 then
        -- record with no price effect (tiny dividend rounding); keep only if price effect plausible
        if math.abs((pclose - divd) / split - pc) > 0.01 then
          addlist(stats, "ignored_event", code .. "@" .. d)
          split, divd = 1.0, 0.0
        end
      end
    end
    local first_day = list_date ~= nil and d == list_date
    local k
    if list_date ~= nil and d >= list_date and not b.susp then k = k_trade else k = 99 end
    local rule = (util.truthy(list_date) and list_date >= cal[1] and k < 5) and ipo_rule(board, list_date, k) or nil
    local lu, ld
    if b.pc_unknown then
      lu, ld = 0, 0 -- first bar from a source without preclose (issue price unknown)
    elseif rule == nil then
      local p = band_pct(board, d, b.st)
      lu, ld = limit_pair(cents(pc), p, p)
    elseif rule[1] == "none" then
      lu, ld = 0, 0
    else
      lu, ld = limit_pair(cents(pc), rule[2], rule[3])
    end
    if board == "bse" and (lu ~= 0 or ld ~= 0) then
      -- BSE: limit price may not exceed ±30 % -> up rounds down, down rounds up (verified vs closes)
      local pcc = cents(pc)
      local p = band_pct(board, d, b.st)
      lu = (pcc * (100 + p)) // 100 * 10
      ld = -((-pcc * (100 - p)) // 100) * 10
    end
    if (lu ~= 0 or ld ~= 0) and not b.susp and (to_milli(b.high) > lu or to_milli(b.low) < ld) then
      -- Traded outside the computed band => the exchange imposed no limit that day
      -- (share-reform / restructuring resumption, delisting-consolidation first day, relisting...).
      lu, ld = 0, 0
      addlist(stats, "unlimited_days", code .. "@" .. d)
    end
    if not b.susp and util.truthy(list_date) and d >= list_date then k_trade = k_trade + 1 end
    local flags = (b.susp and 1 or 0) | (b.st and 2 or 0) | (first_day and 4 or 0) | (b.second and 8 or 0)
    out[#out + 1] = { math.tointeger(tonumber((d:gsub("-", "")))), to_milli(b.open), to_milli(b.high), to_milli(b.low),
      to_milli(b.close), to_milli(pc), md.trunc(b.volume), b.amount + 0.0, split + 0.0, divd + 0.0, lu, ld, flags }
    prev = b
  end
  return out
end
M.build_rows = build_rows

local function st_ranges(rows)
  local out, cur = {}, nil
  for _, r in ipairs(rows) do
    local d = tostring(r[1])
    d = d:sub(1, 4) .. "-" .. d:sub(5, 6) .. "-" .. d:sub(7)
    if r[13] & 2 ~= 0 then
      if cur == nil then cur = { d, d } else cur[2] = d end
    elseif cur ~= nil then
      out[#out + 1] = cur
      cur = nil
    end
  end
  if cur then out[#out + 1] = cur end
  return out
end

local function sse_events(div)
  local ev = {}
  for _, y in ipairs(json.keys(div)) do
    for _, r in ipairs(div[y]) do
      local d = util.truthy(r.dividOperateDate) and r.dividOperateDate or ""
      if d ~= "" then
        local cash = fnum(r.dividCashPsBeforeTax)
        local split = 1.0 + fnum(r.dividStocksPs) + fnum(r.dividReserveToStockPs)
        if not (cash == 0 and split == 1.0) then
          local s0, c0 = 1.0, 0.0
          if ev[d] then s0, c0 = ev[d][1], ev[d][2] end
          if ev[d] and not (s0 == split and c0 == cash) then ev[d] = { s0 * split, c0 + cash } else ev[d] = { split, cash } end
        end
      end
    end
  end
  return ev
end

-- ---------------------------------------------------------------- step: pack
local function step_pack(raw, out)
  local cal, sb = load_meta(raw)
  local calidx = {}
  for i, d in ipairs(cal) do if calidx[d] == nil then calidx[d] = i end end
  local end_ = sb["end"]
  path.mkdir(path.join(out, "daily"), true)
  path.mkdir(path.join(out, "index"), true)
  local stocks, files, missing = {}, O(), {}
  local stats = {}
  local zw = md.zstd_writer(M.ZSTD_LEVEL)
  local names_tw = {} -- s2tw conversions, resolved in one luakit call at the end
  local function emit(rel, rows, entry)
    local fentry = O({ { "rows", #rows }, { "sha256", "" }, { "bytes", 0 } })
    files[rel] = fentry
    zw:add(path.join(out, rel), okbar_body(rows), function(info) fentry.sha256 = info.sha256; fentry.bytes = info.bytes end)
    entry.rows = #rows; entry.first = tostring(rows[1][1]); entry.last = tostring(rows[#rows][1])
    stocks[#stocks + 1] = entry
  end
  local function tw(name)
    local e = { name = name }
    names_tw[#names_tw + 1] = e
    return e
  end

  -- --- SH/SZ
  local uni = sse_universe(raw)
  for n, u in ipairs(uni) do
    local code = u.code
    local kp, dp = path.join(raw, "k", code .. ".csv.gz"), path.join(raw, "div", code .. ".json")
    local source = "baostock"
    local bars, ev
    local skip = false
    if path.exists(kp) then
      bars = {}
      for _, r in ipairs(read_rows_gz(kp)) do
        if util.truthy(r.date) then
          bars[#bars + 1] = { date = r.date, open = fnum(r.open), high = fnum(r.high), low = fnum(r.low), close = fnum(r.close),
            preclose = fnum(r.preclose), volume = md.trunc(fnum(r.volume)), amount = fnum(r.amount),
            susp = r.tradestatus ~= "1", st = r.isST == "1" }
        end
      end
      ev = path.exists(dp) and sse_events(ok.read_json(dp)) or {}
      if not path.exists(dp) then addlist(stats, "no_dividend_cache", code) end
    else
      local sb_, sev = sina_bars(raw, code, stats)
      if sb_ == nil then
        missing[#missing + 1] = O({ { "code", code }, { "name", u.name }, { "reason", u.status ~= "1"
          and "BaoStock unavailable (IP blacklisted during build) and Sina has no data" or "BaoStock unavailable and Sina fetch failed" } })
        skip = true
      else
        bars, ev = sb_, sev
        source = "sina(second source: preclose derived, ST unknown)"
      end
    end
    if not skip and #bars == 0 then
      missing[#missing + 1] = O({ { "code", code }, { "name", u.name }, { "reason", source .. " returned no bars 2010+" } })
      skip = true
    end
    if not skip then
      local delisted = u.status ~= "1" or (u.out ~= nil and u.out <= end_)
      local rows = build_rows(bars, u.board, u.ipo, cal, calidx, not delisted, ev, stats, code)
      if #rows == 0 then
        missing[#missing + 1] = O({ { "code", code }, { "name", u.name }, { "reason", "no bars inside calendar" } })
      else
        local twe = tw(u.name)
        local entry = O({ { "code", code }, { "symbol", util.split(code, ".")[2] }, { "name", u.name }, { "name_zh_tw", twe },
          { "board", u.board }, { "list_date", u.ipo }, { "delist_date", delisted and (u.out or NULL) or NULL },
          { "is_index", false }, { "st_ranges", st_ranges(rows) }, { "source", source } })
        emit("daily/" .. code .. ".okbar", rows, entry)
      end
    end
    if n % 500 == 0 then log(("pack: sse %d/%d"):format(n, #uni)) end
  end

  -- --- BJ
  local lp = path.join(raw, "bj", "list.csv.gz")
  if path.exists(lp) then
    for _, it in ipairs(read_rows_gz(lp)) do
      local sym = it["证券代码"]
      local code = "bj." .. sym
      local name = it["证券简称"]
      local ep, sp, fp = path.join(raw, "bj", "em", sym .. ".csv.gz"), path.join(raw, "bj", "sina", sym .. ".csv.gz"), path.join(raw, "bj", "fhps", sym .. ".csv.gz")
      repeat
        if not path.exists(sp) and not path.exists(ep) then
          missing[#missing + 1] = O({ { "code", code }, { "name", name }, { "reason", "akshare sina+eastmoney daily not fetched" } })
          break
        end
        local em, any_em = {}, false
        if path.exists(ep) then
          for _, r in ipairs(read_rows_gz(ep)) do em[r["日期"]:sub(1, 10)] = r; any_em = true end
        end
        local is_st = util.contains(name, "ST")
        local bars = {}
        if path.exists(sp) then
          for _, r in ipairs(read_rows_gz(sp)) do
            bars[#bars + 1] = { date = r.date:sub(1, 10), open = fnum(r.open), high = fnum(r.high), low = fnum(r.low), close = fnum(r.close),
              preclose = 0.0, volume = md.trunc(fnum(r.volume)), amount = fnum(r.amount), susp = false, st = is_st }
          end
        else
          for _, d in ipairs(sorted_keys(em)) do
            local r = em[d]
            bars[#bars + 1] = { date = d, open = fnum(r["开盘"]), high = fnum(r["最高"]), low = fnum(r["最低"]), close = fnum(r["收盘"]),
              preclose = 0.0, volume = md.trunc(fnum(r["成交量"]) * 100), amount = fnum(r["成交额"]), susp = false, st = is_st }
          end
        end
        if #bars == 0 then
          missing[#missing + 1] = O({ { "code", code }, { "name", name }, { "reason", "akshare returned no bars" } })
          break
        end
        local ev = {}
        if path.exists(fp) then
          for _, r in ipairs(read_rows_gz(fp)) do
            local d = (util.truthy(r["除权除息日"]) and r["除权除息日"] or ""):sub(1, 10)
            if d ~= "" and d ~= "NaT" and d ~= "nan" then
              local split = 1.0 + fnum(r["送转股份-送转总比例"]) / 10.0
              local cash = fnum(r["现金分红-现金分红比例"]) / 10.0
              if split ~= 1.0 or cash ~= 0.0 then ev[d] = { split, cash } end
            end
          end
        end
        -- preclose: exchange value from Eastmoney (close - change) when available, else derived from the
        -- previous close with the exchange ex-rights formula round((prev - cash) / split, 2).
        -- Sina also returns earlier NEEQ (OTC) history; keep only BSE / NEEQ-select trading from the
        -- listing date on, and drop zero-price no-trade quotes.
        local ld_ = (util.truthy(it["上市日期"]) and it["上市日期"] or ""):sub(1, 10)
        local kept = {}
        for _, b in ipairs(bars) do if (ld_ == "" or b.date >= ld_) and b.close > 0 then kept[#kept + 1] = b end end
        bars = kept
        repair_ohlc(bars, code, stats)
        if #bars == 0 then
          missing[#missing + 1] = O({ { "code", code }, { "name", name }, { "reason", "no bars after listing date" } })
          break
        end
        bars = util.sorted(bars, function(b) return b.date end)
        local evd = sorted_keys(ev)
        for i, b in ipairs(bars) do
          local r = em[b.date]
          local chg = r and r["涨跌额"]
          if r ~= nil and chg ~= nil and chg ~= "" and chg ~= "nan" then
            b.preclose = util.round(fnum(r["收盘"]) - fnum(chg), 2)
            stats.bse_preclose_em = (stats.bse_preclose_em or 0) + 1
          elseif i == 1 then
            b.preclose, b.pc_unknown = b.open, true
          else
            local pv = bars[i - 1]
            local sp_, cash_ = 1.0, 0.0
            for _, d_ in ipairs(evd) do
              if pv.date < d_ and d_ <= b.date then sp_ = sp_ * ev[d_][1]; cash_ = cash_ + ev[d_][2] end
            end
            b.preclose = util.round((pv.close - cash_) / sp_, 2)
            stats.bse_preclose_derived = (stats.bse_preclose_derived or 0) + 1
          end
        end
        local list_date = ld_ ~= "" and ld_ or nil
        local rows = build_rows(bars, "bse", list_date, cal, calidx, true, ev, stats, code)
        if #rows == 0 then
          missing[#missing + 1] = O({ { "code", code }, { "name", name }, { "reason", "no bars inside calendar" } })
          break
        end
        local entry = O({ { "code", code }, { "symbol", sym }, { "name", name }, { "name_zh_tw", tw(name) }, { "board", "bse" },
          { "list_date", list_date or NULL }, { "delist_date", NULL }, { "is_index", false }, { "st_ranges", st_ranges(rows) },
          { "source", "akshare(sina" .. (any_em and "+eastmoney" or "") .. ")" } })
        emit("daily/" .. code .. ".okbar", rows, entry)
      until true
    end
  else
    missing[#missing + 1] = O({ { "code", "bj.*" }, { "name", "" }, { "reason", "BSE list not fetched" } })
  end

  -- --- indices
  for _, ix in ipairs(INDICES) do
    local code, sym, name, src = ix[1], ix[2], ix[3], ix[4]
    local p = path.join(raw, "index", code .. ".csv.gz")
    local isrc = src == "baostock" and "baostock" or "akshare(sina, no amount)"
    if not path.exists(p) then
      p = path.join(raw, "index_sina", code .. ".csv.gz")
      isrc = "akshare(sina fallback, no amount)"
    end
    if not path.exists(p) then
      missing[#missing + 1] = O({ { "code", code }, { "name", name }, { "reason", "index not fetched" } })
    else
      local rows = {}
      local prev_close
      for _, r in ipairs(read_rows_gz(p)) do
        local d = r.date:sub(1, 10)
        if not (d < cal[1] or d > end_) then
          local o, h, l, c = fnum(r.open), fnum(r.high), fnum(r.low), fnum(r.close)
          -- source index values carry 3-4 decimals and are occasionally off by 0.001 vs each other
          h, l = math.max(h, o, c), math.min(l, o, c)
          local pc = fnum(r.preclose)
          if pc == 0 then pc = util.truthy(prev_close) and prev_close or o end
          rows[#rows + 1] = { math.tointeger(tonumber((d:gsub("-", "")))), to_milli(o), to_milli(h), to_milli(l), to_milli(c), to_milli(pc),
            md.trunc(fnum(r.volume)), fnum(r.amount), 1.0, 0.0, 0, 0, 0 }
          prev_close = c
        end
      end
      emit("index/" .. code .. ".okbar", rows, O({ { "code", code }, { "symbol", sym }, { "name", name }, { "name_zh_tw", tw(name) },
        { "board", "other" }, { "list_date", NULL }, { "delist_date", NULL }, { "is_index", true }, { "st_ranges", {} }, { "source", isrc } }))
    end
  end
  zw:close()
  -- OpenCC s2tw for every name (one luakit call)
  local srcs = {}
  for i, e in ipairs(names_tw) do srcs[i] = e.name end
  local conv = kit.zhconv("s2tw", srcs)
  local map = {}
  for i, e in ipairs(names_tw) do map[e] = conv[i] end
  for _, s in ipairs(stocks) do s.name_zh_tw = map[s.name_zh_tw] end

  write_json(path.join(out, "calendar.json"), cal)
  write_json(path.join(out, "stocks.json"), stocks)
  local eq = stats.equivalent or {}
  local by_board = O()
  local n_stocks, n_idx = 0, 0
  for _, s in ipairs(stocks) do
    if not s.is_index then
      n_stocks = n_stocks + 1
      local k = s.board
      by_board[k] = by_board[k] or O({ { "listed", 0 }, { "delisted", 0 } })
      local key = util.truthy(s.delist_date ~= NULL and s.delist_date) and "delisted" or "listed"
      by_board[k][key] = by_board[k][key] + 1
    else
      n_idx = n_idx + 1
    end
  end
  local total_rows, total_bytes = 0, 0
  for _, k in ipairs(json.keys(files)) do total_rows = total_rows + files[k].rows; total_bytes = total_bytes + files[k].bytes end
  local function sl(t, n) return util.slice(t or {}, 1, n) end
  local manifest = O({
    { "format", "STREET_SOCIAL_MARKET_DATA_FORMAT v1" },
    { "market", "CN" },
    { "version", 1 },
    { "generated", md.utc_iso() },
    { "period", O({ { "start", cal[1] }, { "end", end_ }, { "trading_days", #cal } }) },
    { "sources", O({
      { "sh_sz_stocks", "BaoStock query_history_k_data_plus adjustflag=3 (unadjusted), query_dividend_data, query_stock_basic (incl. delisted), query_trade_dates" },
      { "sh_sz_fallback", "for stocks BaoStock could not serve: AKShare stock_zh_a_daily (Sina unadjusted) + stock_history_dividend_detail 分红/配股; preclose = round((prev - cash + r*price)/(1 + bonus + r), 2); ST unknown (bit1 = 0); rows flagged bit3" },
      { "sh_sz_fallback_stocks", stats.sina_stocks or 0 },
      { "bse_stocks", "AKShare stock_zh_a_daily (Sina, unadjusted OHLC, volume in shares, amount); stock_zh_a_hist (Eastmoney) preclose = close - change where reachable, otherwise preclose = round((prev_close - cash)/split, 2); stock_fhps_detail_em (dividends/bonus)" },
      { "bse_preclose_rows", O({ { "eastmoney", stats.bse_preclose_em or 0 }, { "derived", stats.bse_preclose_derived or 0 } }) },
      { "indices", "BaoStock (sh.000001, sz.399001, sz.399006, sh.000300); AKShare stock_zh_index_daily (Sina) for sh.000688, amount=0" },
      { "stock_list_fetched", sb.fetched == nil and NULL or sb.fetched },
    }) },
    { "rules", O({
      { "limit_rounding", "half-up to 0.01 CNY on exchange preclose" },
      { "main_board", "±10%, ST ±5% until " .. MAIN_ST_10PCT .. " then ±10%" },
      { "chinext", "±20% from " .. CHINEXT_REFORM .. ", before ±10% (ST ±5%)" },
      { "star", "±20%" }, { "bse", "±30%, up limit rounded down / down limit rounded up to 0.01 (matches BSE closes)" },
      { "ipo", "main/ChiNext first day +44%/-36% (list_date >= 2014-01-01; earlier = 0); STAR, ChiNext (listed >= 2020-08-24), main board (listed >= 2023-04-10): first 5 trading days 0; BSE first day 0" },
      { "unlimited_days", "days whose high/low lie outside the computed band had no exchange limit (share-reform/restructuring resumption, delisting-consolidation first day, relisting); limits stored as 0 and listed in notes.unlimited_days" },
      { "suspended", "row kept, OHLC = preclose, volume 0, flags bit0" },
      { "split_dividend", "from dividend records on ex-date; when the exchange preclose changes without a record (rights issue etc.) split_ratio = prev_close/preclose (equivalent factor)" },
    }) },
    { "counts", O({ { "stocks", n_stocks }, { "indices", n_idx }, { "by_board", by_board }, { "rows", total_rows }, { "bytes", total_bytes } }) },
    { "missing", missing },
    { "notes", O({
      { "bse_history", "BSE stocks come from AKShare current list only; delisted BSE/NEEQ-select stocks are not included. BSE ST history unavailable (flag from current name)." },
      { "filled_suspended_rows", stats.filled_suspended or 0 },
      { "equivalent_split_events", #eq },
      { "equivalent_split_examples", sl(eq, 200) },
      { "residual_events", sl(stats.residual, 200) },
      { "repaired_ohlc_rows", stats.repaired_ohlc or {} },
      { "unlimited_days_count", #(stats.unlimited_days or {}) },
      { "unlimited_days", stats.unlimited_days or {} },
      { "ignored_events", sl(stats.ignored_event, 200) },
    }) },
    { "files", files },
  })
  write_json(path.join(out, "manifest.json"), manifest, 1)
  log(("pack: %s, missing %d"):format(util.repr(manifest.counts), #missing))
end
M.step_pack = step_pack

local function main()
  local ap = ok.args.parser("Build the StreetSocial China A-share daily dataset (see the header of Tools/build_ashare_data.lua)")
  ap:arg("steps", { nargs = "+", choices = { "meta", "sse", "sina", "bj", "index", "pack", "all" } })
  ap:opt("--raw", { default = default_raw(), help = "raw download cache (resume)" })
  ap:opt("--out", { default = M.DEFAULT_OUT })
  ap:opt("--end", { default = nil, help = "last trading day YYYY-MM-DD (default: last closed day)" })
  ap:opt("--workers", { type = "int", default = 2, help = "parallel BaoStock sessions; >~4 got the IP blacklisted (error 10001011)" })
  ap:opt("--limit", { type = "int", default = nil, help = "debug: only fetch N stocks" })
  ap:opt("--sleep", { type = "float", default = 0.6, help = "AKShare politeness delay (s)" })
  ap:flag("--reverse", { help = "sina step: walk the stock list backwards" })
  local a = ap:parse(arg)
  local steps = util.contains_value(a.steps, "all") and { "meta", "sse", "bj", "index", "pack" } or a.steps
  path.mkdir(a.raw, true)
  for _, s in ipairs(steps) do
    if s == "meta" then
      if not (path.exists(path.join(a.raw, "meta", "stock_basic.json")) and a["end"] == nil and not util.contains_value(a.steps, "meta")) then
        step_meta(a.raw, a["end"])
      end
    elseif s == "sse" then step_sse(a.raw, a.workers, a.limit)
    elseif s == "sina" then step_sina(a.raw, a.sleep, true, a.reverse)
    elseif s == "bj" then step_bj(a.raw, a.sleep)
    elseif s == "index" then step_index(a.raw)
    elseif s == "pack" then step_pack(a.raw, a.out) end
  end
end
M.main = main

if ok.is_main() then ok.main(main) end
return M
