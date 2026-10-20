-- US corporate actions (splits, cash dividends) + as-traded (raw) prices for the StreetSocial market DB.
-- Dev tool, not shipped. Format: Docs/STREET_SOCIAL_MARKET_DATA_FORMAT.md.
--
-- Source: Yahoo Finance chart API (public JSON, no key):
--   https://query2.finance.yahoo.com/v8/finance/chart/<SYM>?period1=..&period2=..&interval=1d&events=div,split
--   * quote OHLC are split-adjusted (NOT dividend-adjusted), unrounded (float32), volume split-adjusted;
--   * events.splits = {date, numerator, denominator}; events.dividends = {date, amount} with the amount
--     split-adjusted like the prices.
-- Requests carry a generic browser User-Agent only (no personal information), one connection at a time
-- with a minimum gap between request starts and exponential backoff. Fetching is resumable (one cached
-- file per symbol).
--
-- Steps:
--   Tools\lua\lua.exe Tools/build_us_actions.lua fetch --raw <cache dir> --stocks <MarketData/us/stocks.json> [--extra syms.txt]
--   Tools\lua\lua.exe Tools/build_us_actions.lua load  --raw <cache dir> --db <MarketData/market.sqlite>
--   Tools\lua\lua.exe Tools/build_us_actions.lua validate --db <MarketData/market.sqlite>
--
-- `load` is idempotent: it recomputes every US bar from the thetrading.tools split-adjusted .okbar rows
-- (MarketData/us/daily|index/*.okbar, the same files build_market_sqlite.lua loads) plus the cached Yahoo data,
-- so it never compounds conversions. It touches only market 'us' rows and meta 'manifest.us_actions'.
-- Re-run it after every build_market_sqlite.lua rebuild (that rebuild reloads the split-adjusted .okbar rows).
local ok = require "ok"
local json, path, kit, util = ok.json, ok.path, ok.kit, ok.util
local md = require "market_data_common"

local M = {}
M.MD = path.join(ok.root, "Content/IndependentResources/MarketData")
local UA = { ["User-Agent"] = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0 Safari/537.36",
  ["Accept"] = "application/json" }
local CHART = "https://query2.finance.yahoo.com/v8/finance/chart/%s"
local P1 = 1259625600 -- 2009-12-01T00:00:00Z
local I32_MAX = 2 ^ 31 - 1
local O = json.object
local log_ = md.log

local function log(raw, msg)
  local line = os.date("%Y-%m-%d %H:%M:%S") .. " " .. msg
  print(line); io.stdout:flush()
  path.append_text(path.join(raw, "actions.log"), line .. "\n", { newline = "\n" })
end

local function g(x) return string.format("%g", x) end

-- ---------------------------------------------------------------- fetch
local function fetch_one(sym, out, p2, throttle)
  local delay, st = 5.0, -1
  for _ = 1, 6 do
    throttle()
    local r = kit.http_request(CHART:format(kit.urlencode(sym)), {
      params = { { "period1", P1 }, { "period2", p2 }, { "interval", "1d" }, { "events", "div,split" }, { "includeAdjustedClose", "false" } },
      headers = UA, timeout = 40 })
    st = r.status
    if st == 200 then
      path.write_bytes(out, md.zstd_compress(r.body, 10))
      return "ok"
    end
    if st == 400 or st == 404 or st == 422 then return "http" .. st end
    ok.time.sleep(delay + math.random()) -- 429 / 5xx / network: back off
    delay = math.min(delay * 2, 300)
  end
  return "fail" .. st
end

local function counter_repr(status)
  local c, order = {}, {}
  for _, k in ipairs(json.keys(status)) do
    local v = status[k]
    if not c[v] then c[v] = 0; order[#order + 1] = v end
    c[v] = c[v] + 1
  end
  local idx = {}
  for i, v in ipairs(order) do idx[v] = i end
  table.sort(order, function(a, b) if c[a] ~= c[b] then return c[a] > c[b] end return idx[a] < idx[b] end)
  local parts = {}
  for _, v in ipairs(order) do parts[#parts + 1] = util.repr(v) .. ": " .. c[v] end
  return "Counter({" .. table.concat(parts, ", ") .. "})"
end

local function cmd_fetch(a)
  local raw = a.raw
  local yd = path.join(raw, "yahoo")
  path.mkdir(yd, true)
  local stocks = ok.read_json(a.stocks)
  local syms = {}
  for _, s in ipairs(stocks) do syms[#syms + 1] = s.code end
  if a.extra then
    for x in path.read_text(a.extra):gmatch("%S+") do syms[#syms + 1] = x end
  end
  local stf = path.join(raw, "fetch_status.json")
  local status = path.exists(stf) and ok.read_json(stf) or json.object()
  local todo = {}
  for _, s in ipairs(util.unique(syms)) do
    if not path.exists(path.join(yd, s .. ".json.zst")) and not (util.startswith(status[s] or "", "http4") and not a.retry_missing) then
      todo[#todo + 1] = s
    end
  end
  local p2 = math.floor(ok.time.time())
  log(raw, ("fetch: %d symbols, %d to do"):format(#syms, #todo))
  local last = 0.0
  local function throttle()
    local d = last + a.gap - ok.time.time()
    if d > 0 then ok.time.sleep(d) end
    last = ok.time.time()
  end
  for done, sym in ipairs(todo) do
    local res = fetch_one(sym, path.join(yd, sym .. ".json.zst"), p2, throttle)
    status[sym] = res
    if done % 100 == 0 or util.startswith(res, "fail") then
      path.write_text(stf, json.encode(status))
      log(raw, ("fetch %d/%d last=%s:%s"):format(done, #todo, sym, res))
    end
  end
  path.write_text(stf, json.encode(status))
  log(raw, "fetch done: " .. counter_repr(status))
end

-- ---------------------------------------------------------------- parse
-- datetime.fromtimestamp(ts, America/New_York) -> YYYYMMDD (US DST rules since 2007).
local function ny_day(ts)
  ts = math.tointeger(ts) or md.trunc(ts)
  local z = ts // 86400
  local y = md.days_ymd(z) // 10000
  local function nth_sunday(month, k)
    local first = md.ymd_days(y * 10000 + month * 100 + 1)
    local wd = (first + 3) % 7 -- 0 = Monday
    return first + (6 - wd) % 7 + 7 * (k - 1)
  end
  local dst_start = nth_sunday(3, 2) * 86400 + 7 * 3600 -- 02:00 EST
  local dst_end = nth_sunday(11, 1) * 86400 + 6 * 3600 -- 02:00 EDT
  local off = (ts >= dst_start and ts < dst_end) and -4 * 3600 or -5 * 3600
  return md.days_ymd((ts + off) // 86400)
end
M.ny_day = ny_day

local function nz(v) if v == nil or v == json.null then return nil end return v end
local function pyfloat(v) -- float(x or 0)
  if not util.truthy(v) then return 0.0 end
  if type(v) == "number" then return v + 0.0 end
  return md.pyfloat(v) or error("could not convert string to float: " .. util.repr(v))
end

local function load_yahoo(p)
  local j = json.decode(kit.zstd_decompress_bytes(path.read_bytes(p)))
  local chart = nz(j.chart) or {}
  local res = nz(chart.result)
  if not util.truthy(res) then return nil end
  local r = res[1]
  local ev = nz(r.events) or {}
  local splits = {}
  local sp = nz(ev.splits) or {}
  for _, k in ipairs(json.keys(sp)) do
    local s = sp[k]
    local n, d = pyfloat(s.numerator), pyfloat(s.denominator)
    if n > 0 and d > 0 and math.abs(n / d - 1) > 1e-9 then splits[#splits + 1] = { ny_day(s.date), n / d } end
  end
  local divs = {}
  local dv = nz(ev.dividends) or {}
  for _, k in ipairs(json.keys(dv)) do
    local s = dv[k]
    local amt = pyfloat(s.amount)
    if amt > 0 then divs[#divs + 1] = { ny_day(s.date), amt } end
  end
  local bars, border = {}, {}
  local ts = nz(r.timestamp) or {}
  local ind = nz(r.indicators) or {}
  local q = (nz(ind.quote) or { {} })[1]
  local function col(k) return nz(q[k]) or {} end
  local o, h, l, c, v = col("open"), col("high"), col("low"), col("close"), col("volume")
  local n_o, n_h, n_l, n_c, n_v = #o, #h, #l, #c, #v
  for i, t in ipairs(ts) do
    if i <= n_o and i <= n_h and i <= n_l and i <= n_c and i <= n_v then
      local ro, rh, rl, rc, rv = nz(o[i]), nz(h[i]), nz(l[i]), nz(c[i]), nz(v[i])
      if ro ~= nil and rh ~= nil and rl ~= nil and rc ~= nil and math.min(ro, rh, rl, rc) > 0 then
        local day = ny_day(t)
        if bars[day] == nil then border[#border + 1] = day end
        bars[day] = { ro, rh, rl, rc, util.truthy(rv) and rv or 0 }
      end
    end
  end
  local meta = nz(r.meta) or {}
  return { splits = util.sorted(splits), divs = util.sorted(divs), bars = bars, border = border,
    currency = nz(meta.currency), type = nz(meta.instrumentType) }
end
M.load_yahoo = load_yahoo

local function okbar_path(code)
  local name = code:gsub("%.", "-"):gsub("/", "-")
  for _, sub in ipairs({ "daily", "index" }) do
    local p = path.join(M.MD, "us", sub, name .. ".okbar")
    if path.exists(p) then return p end
  end
  return nil
end

-- raw USD -> int x1000, rounded to the cent (to 0.0001 below 1 USD, i.e. 0.001 at x1000 scale).
local function rnd_price(x)
  if x >= 1.0 then return util.round(x * 100) * 10 end
  return math.max(1, util.round(x * 1000))
end

local NICE = { 1 / 100, 1 / 50, 1 / 40, 1 / 35, 1 / 30, 1 / 25, 1 / 20, 1 / 15, 1 / 12, 1 / 10, 1 / 8, 1 / 6,
  1 / 5, 1 / 4, 1 / 3, 1 / 2, 2 / 3, 1.5, 2, 3, 4, 5, 6, 8, 10, 15, 20 }

-- ---------------------------------------------------------------- load
local function cmd_load(a)
  local raw, dbp = a.raw, a.db
  if path.exists(dbp .. ".building") or path.exists(path.with_suffix(dbp, ".sqlite.building")) then
    io.stderr:write("market.sqlite is being rebuilt (.building exists); try later\n")
    os.exit(1)
  end
  local function sql(ops) return kit.sqlite(dbp, util.extend({ { sql = "PRAGMA synchronous=OFF" } }, ops)) end
  local cal = {}
  for _, r in ipairs(kit.sql_rows(dbp, "SELECT day FROM calendar WHERE market='us' ORDER BY day")) do cal[#cal + 1] = r[1] end
  local calset = util.set(cal)
  local syms = kit.sql_rows(dbp, "SELECT id, code FROM symbols WHERE market='us' ORDER BY code")
  if a.only then
    local only = util.set(util.split(a.only, ","))
    syms = util.filter(syms, function(x) return only[x[2]] end)
  end
  local KEYS = { "symbols", "yahoo_found", "yahoo_missing", "with_splits", "with_divs", "split_events", "div_events",
    "split_events_outside_rows", "yahoo_ohlc_symbols", "yahoo_only_rows_added", "inconsistent", "coarse_symbols",
    "suspect_splits", "suspect_divs", "non_usd", "overflow", "ttt_unadjusted_splits", "yahoo_unadjusted_splits",
    "rows_from_yahoo", "inferred_splits" }
  local LISTS = util.set({ "yahoo_missing", "inconsistent", "coarse_symbols", "suspect_splits", "suspect_divs", "non_usd",
    "overflow", "ttt_unadjusted_splits", "yahoo_unadjusted_splits", "inferred_splits" })
  local st = {}
  for _, k in ipairs(KEYS) do st[k] = LISTS[k] and {} or 0 end
  st.symbols = #syms
  local log10 = math.log
  local t0 = ok.time.time()
  local pending = {}
  local function flush()
    if #pending > 0 then
      table.insert(pending, 1, { sql = "BEGIN" })
      pending[#pending + 1] = { sql = "COMMIT" }
      sql(pending)
      pending = {}
    end
  end
  for idx, sy in ipairs(syms) do
    local n = idx - 1
    local sid, code = sy[1], sy[2]
    local p = okbar_path(code)
    if p then
      repeat
        local base = md.read_okbar(p) -- split-adjusted thetrading.tools rows
        local yp = path.join(raw, "yahoo", code .. ".json.zst")
        local y = path.exists(yp) and load_yahoo(yp) or nil
        if y == nil then
          table.insert(st.yahoo_missing, code)
          y = { splits = {}, divs = {}, bars = {}, border = {}, currency = nil }
        else
          st.yahoo_found = st.yahoo_found + 1
          if util.truthy(y.currency) and y.currency ~= "USD" then table.insert(st.non_usd, code .. ":" .. y.currency) end
        end
        local splits = {}
        for _, s in ipairs(y.splits) do if s[1] >= 20100101 then splits[#splits + 1] = s end end
        local divs = y.divs
        local tdays, tclose, topen = {}, {}, {}
        for i, r in ipairs(base) do tdays[i] = r[1]; tclose[r[1]] = r[5] / 1000.0; topen[r[1]] = r[2] / 1000.0 end
        local ybars = y.bars
        local ydays = {}
        for _, d in ipairs(y.border) do ydays[#ydays + 1] = d end
        table.sort(ydays)

        local function adjusted_in(days_, opn, cls, sd, ratio)
          if math.abs(log10(ratio)) < 0.2 then return true end
          local prev_last, nxt_first
          for _, d in ipairs(days_) do
            if d < sd then prev_last = d elseif nxt_first == nil then nxt_first = d end
          end
          if not prev_last or not nxt_first then return true end
          local j = opn(nxt_first) / cls(prev_last)
          if j <= 0 then return true end
          return math.abs(log10(j)) <= math.abs(log10(j) + log10(ratio))
        end
        local t_adj, y_adj, t_set, y_set = {}, {}, {}, {}
        for k, s in ipairs(splits) do
          if adjusted_in(tdays, function(d) return topen[d] end, function(d) return tclose[d] end, s[1], s[2]) then
            t_adj[#t_adj + 1] = s; t_set[k] = true
          end
          if adjusted_in(ydays, function(d) return ybars[d][1] end, function(d) return ybars[d][4] end, s[1], s[2]) then
            y_adj[#y_adj + 1] = s; y_set[k] = true
          end
        end
        local function missing_str(set)
          local parts = {}
          for k, s in ipairs(splits) do
            local inside = set[k]
            if not inside then -- python: (sd, r) not in t_adj (value equality)
              for kk in pairs(set) do if splits[kk][1] == s[1] and splits[kk][2] == s[2] then inside = true end end
            end
            if not inside then parts[#parts + 1] = s[1] .. ":" .. g(s[2]) end
          end
          return code .. ":" .. table.concat(parts, ",")
        end
        if #t_adj ~= #splits then table.insert(st.ttt_unadjusted_splits, missing_str(t_set)) end
        if #y_adj ~= #splits then table.insert(st.yahoo_unadjusted_splits, missing_str(y_set)) end

        local function mkfactor(lst)
          return function(d)
            local x = 1.0
            for _, s in ipairs(lst) do if s[1] > d then x = x * s[2] end end
            return x
          end
        end
        local tf, yf = mkfactor(t_adj), mkfactor(y_adj)
        local factor = yf -- for dividends (Yahoo amounts follow Yahoo's own adjustment)

        -- Agreement test on raw day-to-day close changes. thetrading.tools subtracts special dividends
        -- additively (a step constant), which leaves daily changes intact, so same security => same changes.
        local use_yahoo = false
        local pairs_, agree = 0, 0
        local prev
        for _, d in ipairs(tdays) do
          local yb = ybars[d]
          if not yb then prev = nil
          else
            local T, Y = tclose[d] * tf(d), yb[4] * yf(d)
            if prev then
              local pT, pY, pf = prev[1], prev[2], prev[3]
              local tol = 0.0105 * (tf(d) + pf) + 0.003 * Y
              pairs_ = pairs_ + 1
              local good = math.abs((T - pT) - (Y - pY)) <= tol -- additive adjustments keep absolute changes
              if not good and math.min(T, pT, Y, pY) > 0 then -- multiplicative ones (spin-off factors) keep returns
                good = math.abs(log10(T / pT) - log10(Y / pY)) <= 0.01 + 0.0105 * (tf(d) / T + pf / pT)
              end
              if good then agree = agree + 1 end
            end
            prev = { T, Y, tf(d) }
          end
        end
        if pairs_ > 0 then
          local frac = agree / pairs_
          use_yahoo = frac >= 0.9
          if not use_yahoo then table.insert(st.inconsistent, ("%s:agree=%.3f/%d"):format(code, frac, pairs_)) end
        end
        local rows = {}
        local offset = 0.0 -- Yahoo raw - ttt raw at the last overlapping day (for ttt-only rows)
        for _, r in ipairs(base) do
          local d = r[1]
          local f = tf(d)
          local yb = use_yahoo and ybars[d] or nil
          local tr = { r[2] / 1000.0 * f, r[3] / 1000.0 * f, r[4] / 1000.0 * f, r[5] / 1000.0 * f }
          local vol = util.round(r[7] / f)
          local o, h, l, c
          if yb then
            local gg = yf(d)
            local yr = { yb[1] * gg, yb[2] * gg, yb[3] * gg, yb[4] * gg }
            offset = yr[4] - tr[4]
            local close_enough = f == 1.0
            if close_enough then
              for k = 1, 4 do if not (math.abs(tr[k] - yr[k]) <= 0.011) then close_enough = false end end
            end
            if close_enough then
              o, h, l, c = rnd_price(tr[1]), rnd_price(tr[2]), rnd_price(tr[3]), rnd_price(tr[4]) -- exact cents from the source
            else
              o, h, l, c = rnd_price(yr[1]), rnd_price(yr[2]), rnd_price(yr[3]), rnd_price(yr[4])
              st.rows_from_yahoo = st.rows_from_yahoo + 1
            end
          else
            local off = use_yahoo and offset or 0.0
            local function adj(x) return rnd_price(math.max(x + off, 0.0001)) end
            o, h, l, c = adj(tr[1]), adj(tr[2]), adj(tr[3]), adj(tr[4])
          end
          rows[d] = { o, h, l, c, vol, r[13] }
        end
        local days = {}
        for d in pairs(rows) do days[#days + 1] = d end
        table.sort(days)
        if use_yahoo then
          st.yahoo_ohlc_symbols = st.yahoo_ohlc_symbols + 1
          local lo, hi = days[1] or 0, days[#days] or 0
          for _, d in ipairs(y.border) do
            local yb = ybars[d]
            -- only fill gaps inside the thetrading.tools range: outside it Yahoo may show another company
            -- that used the ticker before (e.g. AA before the 2016 Alcoa split-up)
            if not (rows[d] or not calset[d] or yb[5] <= 0 or d < lo or d > hi) then
              local gg = yf(d)
              rows[d] = { rnd_price(yb[1] * gg), rnd_price(yb[2] * gg), rnd_price(yb[3] * gg), rnd_price(yb[4] * gg),
                util.round(yb[5] / gg), 8 } -- bit3: filled from second source
              st.yahoo_only_rows_added = st.yahoo_only_rows_added + 1
            end
          end
        elseif #days > 0 then
          local mx = -math.huge
          for _, d in ipairs(days) do mx = math.max(mx, tf(d)) end
          if mx > 5 then table.insert(st.coarse_symbols, code) end
        end
        -- Splits Yahoo does not list but thetrading.tools adjusted for: the ttt/Yahoo price ratio steps by r
        -- on a day when Yahoo's own raw price jumps by ~1/r and no dividend is paid nearby.
        local inferred = {}
        if use_yahoo then
          local q = {}
          for _, d in ipairs(tdays) do
            if ybars[d] then q[#q + 1] = { d, tclose[d] * tf(d) / (ybars[d][4] * yf(d)) } end
          end
          local known = {}
          for _, s in ipairs(splits) do known[#known + 1] = s[1] end
          local dvd = {}
          for _, x in ipairs(divs) do dvd[#dvd + 1] = x[1] end
          for i = 1, #q - 6 do -- python range(1, len(q) - 5), 0-based
            local d, qc = q[i + 1][1], q[i + 1][2]
            local qp = q[i][2]
            local step = qc / qp
            if not (math.abs(log10(step)) < 0.25) then
              local win = {}
              for k = i + 1, math.min(#q, i + 6) do win[#win + 1] = q[k][2] end
              table.sort(win)
              local after = win[4]
              local bw = {}
              for k = math.max(0, i - 6) + 1, i do bw[#bw + 1] = q[k][2] end
              table.sort(bw)
              local before = bw[#bw // 2 + 1]
              if not (math.abs(log10(after / qc)) > 0.05 or math.abs(log10(before / qp)) > 0.05) then
                local pd_ = q[i][1]
                local j = ybars[d][1] * yf(d) / (ybars[pd_][4] * yf(pd_))
                if not (math.abs(log10(j) + log10(step)) > 0.15) then
                  local near = false
                  for _, k in ipairs(known) do if math.abs(k - d) <= 3 then near = true end end
                  for _, k in ipairs(dvd) do if (0 <= d - k and d - k <= 7) or (0 <= k - d and k - d <= 3) then near = true end end
                  if not near then
                    local rr = util.round(step, 4)
                    for _, nr in ipairs(NICE) do
                      if math.abs(log10(step / nr)) < 0.06 then rr = nr; break end
                    end
                    inferred[#inferred + 1] = { d, rr }
                  end
                end
              end
            end
          end
          if #inferred > 0 then
            local parts = {}
            for _, x in ipairs(inferred) do parts[#parts + 1] = x[1] .. ":" .. g(x[2]) end
            table.insert(st.inferred_splits, code .. ":" .. table.concat(parts, ","))
          end
        end
        days = {}
        for d in pairs(rows) do days[#days + 1] = d end
        table.sort(days)
        if #days == 0 then break end
        local function first_ge(x) for _, d in ipairs(days) do if d >= x then return d end end return nil end
        -- split / dividend on rows (an event on a non-row day moves to the next row)
        local split_on, div_on, n_split, n_div = {}, {}, 0, 0
        for _, s in ipairs(util.extend(util.slice(splits), inferred)) do
          local sd, r = s[1], s[2]
          local tgt = first_ge(sd)
          if tgt == nil or sd < days[1] then
            st.split_events_outside_rows = st.split_events_outside_rows + 1
          else
            if tgt ~= sd then st.split_events_outside_rows = st.split_events_outside_rows + 1 end
            if split_on[tgt] == nil then n_split = n_split + 1 end
            split_on[tgt] = (split_on[tgt] or 1.0) * r
          end
        end
        for _, x in ipairs(divs) do
          local dd, amt = x[1], x[2]
          if not (dd < days[1] or dd > days[#days]) then
            local tgt = first_ge(dd)
            if tgt ~= nil then
              if div_on[tgt] == nil then n_div = n_div + 1 end
              div_on[tgt] = (div_on[tgt] or 0.0) + amt * factor(dd)
            end
          end
        end
        st.split_events = st.split_events + n_split
        st.div_events = st.div_events + n_div
        if n_split > 0 then st.with_splits = st.with_splits + 1 end
        if n_div > 0 then st.with_divs = st.with_divs + 1 end
        local out = {}
        local prev_c
        local overflow = false
        for _, d in ipairs(days) do
          local o, h, l, c, vol, fl = table.unpack(rows[d])
          h, l = math.max(h, o, c), math.min(l, o, c)
          local sp = split_on[d] or 1.0
          local pc = prev_c == nil and o or math.max(1, util.round(prev_c / sp))
          if sp ~= 1.0 and prev_c ~= nil then
            local jump = o / (prev_c / sp)
            if not (0.5 < jump and jump < 2.0) then
              table.insert(st.suspect_splits, ("%s:%d:%s:open/pc=%.2f"):format(code, d, g(sp), jump))
            end
          end
          local dv = util.round(div_on[d] or 0.0, 6)
          if dv ~= 0 and util.truthy(prev_c) and dv * 1000 > 0.5 * prev_c then
            table.insert(st.suspect_divs, ("%s:%d:%s:pc=%s"):format(code, d, g(dv), g(prev_c / 1000)))
          end
          if math.max(o, h, l, c, pc) > I32_MAX then
            table.insert(st.overflow, code)
            overflow = true
            break
          end
          out[#out + 1] = { sid, d, o, h, l, c, pc, vol, util.round(c / 1000.0 * vol, 2), sp + 0.0, dv, 0, 0, fl }
          prev_c = c
        end
        if not overflow then
          pending[#pending + 1] = { sql = "DELETE FROM bars WHERE sid=?", params = { sid } }
          pending[#pending + 1] = { sql = "INSERT INTO bars VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)", many = out }
          local basis = (next(ybars) ~= nil or #y.splits > 0) and "raw" or "raw_no_action_source"
          pending[#pending + 1] = { sql = "UPDATE symbols SET price_basis=?, first=?, last=? WHERE id=?", params = { basis, days[1], days[#days], sid } }
        end
        if n % 250 == 0 then
          flush()
          print(("load %d/%d %.0fs"):format(n, #syms, ok.time.time() - t0)); io.stdout:flush()
        end
      until true
    end
  end
  flush()
  local summary = O()
  for _, k in ipairs(KEYS) do summary[k] = LISTS[k] and #st[k] or st[k] end
  local man = O({
    { "source", O({ { "name", "Yahoo Finance chart API (v8, events=div,split)" },
      { "url_template", CHART:format("{SYMBOL}") .. "?period1=1259625600&period2=<now>&interval=1d&events=div,split" },
      { "fetched", os.date("%Y-%m-%d") } }) },
    { "price_basis", "raw (as traded): price = split-adjusted price x product of split ratios with ex-date after the row; volume = adjusted volume / same factor; rounded to 0.01 USD (0.0001 below 1 USD)" },
    { "ohlcv_source", "thetrading.tools rows define the trading days and volume; prices come from Yahoo (unrounded, converted to raw with Yahoo splits) whenever the two series agree on >= 90% of raw day-to-day close changes, keeping the exact thetrading.tools cents where they already equal the raw price. Needed because thetrading.tools prices are split-adjusted, rounded to 0.01 and for some symbols also reduced by special dividends (additive step). Rows Yahoo has inside the thetrading.tools range but that source lacks are added with flags bit3. Symbols failing the test keep thetrading.tools prices x split factor (listed in inconsistent_with_thetrading_tools)." },
    { "split_detection", "a Yahoo split is applied to a series only if that series shows no ~1/ratio jump on the ex-date (splits a series was not adjusted for are listed below); the split column always records it" },
    { "split", "ratio effective on the ex-date (4-for-1 = 4.0, 1-for-10 reverse = 0.1); event on a non-trading day of the symbol is put on its next row" },
    { "div", "cash dividend per share on the ex-date in as-traded shares (Yahoo amount is split-adjusted; multiplied back)" },
    { "preclose", "previous raw close / that day split ratio (dividends not subtracted); first row = own open" },
    { "amount", "raw close x raw volume (estimate)" },
    { "price_basis_values", O({ { "raw", "converted with Yahoo actions" },
      { "raw_no_action_source", "Yahoo had no data for the symbol; thetrading.tools values kept, correct only if no split happened since 2010" } }) },
    { "summary", summary },
    { "yahoo_missing", st.yahoo_missing },
    { "inconsistent_with_thetrading_tools", st.inconsistent },
    { "coarse_symbols_still_from_rounded_source", st.coarse_symbols },
    { "suspect_splits", st.suspect_splits },
    { "splits_not_in_thetrading_tools_adjustment", st.ttt_unadjusted_splits },
    { "splits_not_in_yahoo_adjustment", st.yahoo_unadjusted_splits },
    { "inferred_splits", st.inferred_splits },
    { "inferred_splits_rule", "split missing from Yahoo events but present in thetrading.tools adjustment: ttt/Yahoo ratio steps by r and persists, Yahoo raw open jumps by ~1/r, no dividend within days; r snapped to a common ratio" },
    { "suspect_divs", util.slice(st.suspect_divs, 1, 500) },
    { "non_usd_quote", st.non_usd },
    { "overflow_skipped", st.overflow },
  })
  sql({ { sql = "INSERT OR REPLACE INTO meta VALUES(?,?)", params = { "manifest.us_actions", json.encode(man, { ensure_ascii = false }) } } })
  log(raw, "load done: " .. json.encode(summary))
end
M.cmd_load = cmd_load

local ANCHORS = { -- (code, day, field, expected raw USD, tolerance) -- known as-traded closes / events
  { "AAPL", 20100104, "c", 214.01, 0.02 }, { "AAPL", 20140606, "c", 645.57, 0.05 }, { "AAPL", 20200828, "c", 499.23, 0.05 },
  { "AAPL", 20140609, "split", 7.0, 0 }, { "AAPL", 20200831, "split", 4.0, 0 },
  { "NVDA", 20100104, "c", 18.49, 0.05 }, { "NVDA", 20210720, "split", 4.0, 0 }, { "NVDA", 20240610, "split", 10.0, 0 },
  { "NVDA", 20240607, "c", 1208.88, 0.1 },
  { "TSLA", 20200828, "c", 2213.40, 0.5 }, { "TSLA", 20200831, "split", 5.0, 0 }, { "TSLA", 20220825, "split", 3.0, 0 },
  { "GOOGL", 20220715, "c", 2235.55, 0.5 }, { "GOOGL", 20220718, "split", 20.0, 0 },
  { "AMZN", 20220603, "c", 2447.00, 0.5 }, { "AMZN", 20220606, "split", 20.0, 0 },
}
local DIV_YEARS = { { "AAPL", 2021, 0.865 }, { "KO", 2019, 1.60 }, { "JNJ", 2019, 3.75 }, { "MSFT", 2019, 1.89 }, { "XOM", 2019, 3.43 } }

-- Python repr of fetchall() results (list of tuples).
local function rows_repr(rows)
  local parts = {}
  for i, r in ipairs(rows) do
    local cells = {}
    for k = 1, #r do cells[k] = util.repr(r[k] == json.null and nil or r[k]) end
    parts[i] = "(" .. table.concat(cells, ", ") .. (#r == 1 and ",)" or ")")
  end
  return "[" .. table.concat(parts, ", ") .. "]"
end
M.rows_repr = rows_repr

local function cmd_validate(a)
  local db = a.db
  local function q(s, ...) return kit.sql_rows("file:" .. db .. "?mode=ro", s, { ... }, { readonly = true }) end
  local base = "FROM bars b JOIN symbols s ON s.id=b.sid WHERE s.market='us'"
  local good_n, bad_n = 0, 0
  for _, an in ipairs(ANCHORS) do
    local code, day, fld, exp, tol = table.unpack(an)
    local r = q(("SELECT %s %s AND s.code=? AND b.day=?"):format(fld, base), code, day)
    local got = r[1] and r[1][1]
    if got == json.null then got = nil end
    if got ~= nil and fld ~= "split" then got = got / 1000.0 end
    local good = got ~= nil and math.abs(got - exp) <= tol
    if good then good_n = good_n + 1 else bad_n = bad_n + 1 end
    print(("%s %s %d %s got=%s expected=%s"):format(good and "OK " or "BAD", code, day, fld, util.str(got), util.str(exp)))
  end
  for _, dy in ipairs(DIV_YEARS) do
    local code, yr, exp = table.unpack(dy)
    local got = q(("SELECT round(sum(div),4) %s AND s.code=? AND b.day BETWEEN ? AND ?"):format(base), code, yr * 10000, yr * 10000 + 9999)[1][1]
    if got == json.null then got = nil end
    local good = got ~= nil and math.abs(got - exp) < 0.02
    if good then good_n = good_n + 1 else bad_n = bad_n + 1 end
    print(("%s %s dividends %d sum=%s expected=%s"):format(good and "OK " or "BAD", code, yr, util.str(got), util.str(exp)))
  end
  print(("anchors: %d ok, %d bad"):format(good_n, bad_n))
  print("basis " .. rows_repr(q("SELECT price_basis, count(*) FROM symbols WHERE market='us' GROUP BY 1")))
  print("split rows " .. rows_repr(q(("SELECT count(*), sum(split>1), sum(split<1) %s AND split<>1"):format(base))))
  print("div rows " .. rows_repr(q(("SELECT count(*), count(DISTINCT sid) %s AND div>0"):format(base))))
  -- pc consistency: pc == round(prev c / split)
  print("rows " .. rows_repr(q(("SELECT count(*) %s"):format(base))))
end

local function main()
  local cmd = arg[1]
  local rest = table.move(arg, 2, #arg, 1, {})
  local ap = ok.args.parser()
  if cmd == "fetch" then
    ap:opt("--raw", { required = true })
    ap:opt("--stocks", { default = path.join(M.MD, "us", "stocks.json") })
    ap:opt("--extra", { default = nil, help = "file with extra symbols to probe (whitespace separated)" })
    ap:opt("--gap", { type = "float", default = 0.6, help = "min seconds between request starts" })
    ap:flag("--retry-missing")
    cmd_fetch(ap:parse(rest))
  elseif cmd == "load" then
    ap:opt("--raw", { required = true })
    ap:opt("--db", { default = path.join(M.MD, "market.sqlite") })
    ap:opt("--only", { default = nil, help = "comma-separated codes (testing; manifest then covers only these)" })
    cmd_load(ap:parse(rest))
  elseif cmd == "validate" then
    ap:opt("--db", { default = path.join(M.MD, "market.sqlite") })
    cmd_validate(ap:parse(rest))
  else
    io.stderr:write("usage: build_us_actions.lua {fetch,load,validate} ...\n")
    os.exit(2)
  end
end
M.main = main

if ok.is_main() then ok.main(main) end
return M
