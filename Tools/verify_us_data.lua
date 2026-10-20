-- Verify Content/IndependentResources/MarketData/us/ against
-- Docs/STREET_SOCIAL_MARKET_DATA_FORMAT.md (dev tool).
--
-- Checks: single zstd frame, header, row size, sha256/rows vs manifest and stocks.json,
-- dates ascending and inside calendar.json, calendar vs the NYSE holiday rules (2010-2026, incl. the
-- special closures), OHLC sanity, preclose chain, split_ratio/dividend/limit fields, continuity across
-- well-known split dates (data is split-adjusted, so no jump may appear), and a spot check of well-known
-- 2010-01-04 split-adjusted closes.
--
--     Tools\lua\lua.exe Tools/verify_us_data.lua [--dir PATH]
-- Exit code 0 = all hard checks passed.
local ok = require "ok"
local path, json, util = ok.path, ok.json, ok.util
local md = require "market_data_common"

local DEFAULT = path.join(ok.root, "Content/IndependentResources/MarketData/us")

-- Well-known split dates (effective first post-split session). Source is split-adjusted,
-- so close(d)/close(d-1) must stay a normal daily move.
local SPLITS = { { "AAPL", 20140609, 7 }, { "AAPL", 20200831, 4 }, { "NVDA", 20210720, 4 }, { "NVDA", 20240610, 10 },
  { "TSLA", 20200831, 5 }, { "TSLA", 20220825, 3 }, { "AMZN", 20220606, 20 }, { "GOOGL", 20220718, 20 },
  { "WMT", 20240226, 3 }, { "AVGO", 20240715, 10 } }
-- Approximate 2010-01-04 closes, raw price / later split factors (reference values from
-- public historical quotes, memorised; tolerance 2%).
local SPOT_2010 = { { "AAPL", 214.01 / 28 }, { "MSFT", 30.95 }, { "AMZN", 133.90 / 20 }, { "JPM", 42.85 }, { "XOM", 69.15 },
  { "WMT", 54.23 / 3 }, { "KO", 57.04 / 2 }, { "INTC", 20.88 }, { "CSCO", 24.69 }, { "JNJ", 64.68 },
  { "PG", 61.12 }, { "IBM", 132.45 }, { "SPY", 113.33 }, { "QQQ", 46.42 },
  { "NVDA", 18.49 / 40 } } -- NVDA 2021 4:1 and 2024 10:1

-- ---------------------------------------------------------------- NYSE sessions (pandas_market_calendars rules)
local function wd(n) return (md.ymd_days(n) + 3) % 7 end -- 0 = Monday
local function nth_weekday(y, m, weekday, k) -- k-th weekday (k = -1: last) of month
  if k > 0 then
    local first = y * 10000 + m * 100 + 1
    local off = (weekday - wd(first)) % 7
    return md.add_days(first, off + 7 * (k - 1))
  end
  local nm = m == 12 and (y + 1) * 10000 + 101 or y * 10000 + (m + 1) * 100 + 1
  local last = md.add_days(nm, -1)
  return md.add_days(last, -((wd(last) - weekday) % 7))
end
local function easter(y) -- Anonymous Gregorian algorithm
  local a, b, c = y % 19, y // 100, y % 100
  local d, e = b // 4, b % 4
  local f = (b + 8) // 25
  local g = (b - f + 1) // 3
  local h = (19 * a + b - d - g + 15) % 30
  local i, k = c // 4, c % 4
  local l = (32 + 2 * e + 2 * i - h - k) % 7
  local m = (a + 11 * h + 22 * l) // 451
  local month = (h + l - 7 * m + 114) // 31
  local day = (h + l - 7 * m + 114) % 31 + 1
  return y * 10000 + month * 100 + day
end
local function observed(n) -- Saturday -> Friday, Sunday -> Monday
  local w = wd(n)
  if w == 5 then return md.add_days(n, -1) elseif w == 6 then return md.add_days(n, 1) end
  return n
end
local SPECIAL_CLOSED = { [20121029] = true, [20121030] = true, [20181205] = true, [20250109] = true }
local function nyse_holidays(y)
  local h = {}
  local ny = y * 10000 + 101
  if wd(ny) == 6 then h[md.add_days(ny, 1)] = true elseif wd(ny) ~= 5 then h[ny] = true end -- Saturday: not observed
  h[nth_weekday(y, 1, 0, 3)] = true   -- Martin Luther King Jr. Day
  h[nth_weekday(y, 2, 0, 3)] = true   -- Washington's Birthday
  h[md.add_days(easter(y), -2)] = true -- Good Friday
  h[nth_weekday(y, 5, 0, -1)] = true  -- Memorial Day
  if y >= 2022 then h[observed(y * 10000 + 619)] = true end -- Juneteenth
  h[observed(y * 10000 + 704)] = true -- Independence Day
  h[nth_weekday(y, 9, 0, 1)] = true   -- Labor Day
  h[nth_weekday(y, 11, 3, 4)] = true  -- Thanksgiving
  h[observed(y * 10000 + 1225)] = true -- Christmas
  return h
end
local function nyse_sessions(a, b)
  local out, hol = {}, {}
  for z = md.ymd_days(a), md.ymd_days(b) do
    local n = md.days_ymd(z)
    local y = n // 10000
    hol[y] = hol[y] or nyse_holidays(y)
    if wd(n) < 5 and not hol[y][n] and not SPECIAL_CLOSED[n] then out[#out + 1] = n end
  end
  return out
end

local function fmt(x, spec) return string.format(spec, x) end

local function main()
  local ap = ok.args.parser()
  ap:opt("--dir", { default = DEFAULT })
  local a = ap:parse(arg)
  local root = a.dir
  local err, warn, info = {}, {}, {}
  local man = ok.read_json(path.join(root, "manifest.json"))
  local stocks = ok.read_json(path.join(root, "stocks.json"))
  local cal = ok.read_json(path.join(root, "calendar.json"))

  -- ---- calendar
  local cal_i, seen, sorted_unique = {}, {}, true
  for i, x in ipairs(cal) do
    cal_i[i] = math.tointeger(tonumber((x:gsub("-", ""))))
    if seen[cal_i[i]] or (i > 1 and cal_i[i] <= cal_i[i - 1]) then sorted_unique = false end
    seen[cal_i[i]] = true
  end
  if not sorted_unique then err[#err + 1] = "calendar not strictly ascending/unique" end
  do
    local a0 = math.tointeger(tonumber((cal[1]:gsub("-", ""))))
    local b0 = math.tointeger(tonumber((cal[#cal]:gsub("-", ""))))
    local nyse = nyse_sessions(a0, b0)
    local nset, miss, extra = util.set(nyse), {}, {}
    for _, d in ipairs(nyse) do if not seen[d] then miss[#miss + 1] = d end end
    for _, d in ipairs(cal_i) do if not nset[d] then extra[#extra + 1] = d end end
    table.sort(miss); table.sort(extra)
    if #miss > 0 or #extra > 0 then
      warn[#warn + 1] = ("calendar vs NYSE schedule: missing %s extra %s"):format(util.repr(util.slice(miss, 1, 10)), util.repr(util.slice(extra, 1, 10)))
    end
    info[#info + 1] = ("calendar matches the NYSE holiday rules: %s (%d sessions %s..%s)"):format(
      (#miss == 0 and #extra == 0) and "True" or "False", #cal_i, cal[1], cal[#cal])
  end
  local cal_set = seen

  -- ---- files
  local by_code = {}
  for _, s in ipairs(stocks) do
    local key = (s.is_index and "index/" or "daily/") .. s.code .. ".okbar"
    if by_code[key] then err[#err + 1] = "duplicate stocks.json entry " .. key end
    by_code[key] = s
  end
  local files = man.files
  local fset = {}
  for _, k in ipairs(json.keys(files)) do fset[k] = true end
  local disk = {}
  for _, sub in ipairs({ "daily", "index" }) do
    for _, p in ipairs(path.glob(path.join(root, sub), "*.okbar")) do disk[sub .. "/" .. path.name(p)] = true end
  end
  local function diff(x, y) local t = {}; for k in pairs(x) do if not y[k] then t[#t + 1] = k end end; table.sort(t); return t end
  local d1, d2 = diff(disk, fset), diff(fset, disk)
  if #d1 > 0 or #d2 > 0 then
    err[#err + 1] = ("disk vs manifest mismatch: extra %s missing %s"):format(util.repr(util.slice(d1, 1, 5)), util.repr(util.slice(d2, 1, 5)))
  end
  local x1, x2 = diff(by_code, fset), diff(fset, by_code)
  if #x1 > 0 or #x2 > 0 then
    local sym = util.extend(x1, x2); table.sort(sym)
    err[#err + 1] = ("stocks.json vs manifest mismatch: %s"):format(util.repr(util.slice(sym, 1, 5)))
  end
  local total_rows, total_bytes = 0, 0
  local series = {}
  local want = {}
  for _, s in ipairs(SPLITS) do want[s[1]] = true end
  for _, s in ipairs(SPOT_2010) do want[s[1]] = true end
  local bad_ohlc = 0
  local rels = json.keys(files)
  table.sort(rels)
  local paths = {}
  for i, rel in ipairs(rels) do paths[i] = path.join(root, rel) end
  md.zstd_each(paths, function(i, zi, data)
    local rel = rels[i]
    local meta = files[rel]
    if zi.error then
      err[#err + 1] = rel .. ": " .. (zi.eof == false and "zstd frame not terminated" or zi.error)
      return
    end
    if zi.trailing and zi.trailing > 0 then err[#err + 1] = rel .. ": trailing data after first zstd frame"; return end
    total_bytes = total_bytes + zi.bytes
    if zi.sha256 ~= meta.sha256 then err[#err + 1] = rel .. ": sha256 mismatch" end
    local rows, h = md.okbar_parse(data, true)
    local n = h.rows
    if h.magic ~= "OKBR" or h.version ~= 1 or h.market ~= 2 or h.scale ~= 1000 then
      err[#err + 1] = ("%s: bad header b'%s' %d %d %d"):format(rel, h.magic, h.version, h.market, h.scale)
    end
    if #data ~= 16 + 64 * n then
      err[#err + 1] = ("%s: size %d != 16+64*%d"):format(rel, #data, n)
      return
    end
    local st = by_code[rel] or {}
    if n ~= meta.rows or n ~= st.rows or tostring(rows[1][1]) ~= st.first or tostring(rows[#rows][1]) ~= st.last then
      err[#err + 1] = rel .. ": rows/first/last mismatch with manifest/stocks.json"
    end
    total_rows = total_rows + n
    local prev_d, prev_c
    local zero7 = string.rep("\0", 7)
    for k, r in ipairs(rows) do
      local d, o, hh, l, c, pc, v, _, sr, dv, lu, ld, fl, res = table.unpack(r, 1, 14)
      if prev_d ~= nil and d <= prev_d then err[#err + 1] = ("%s: dates not ascending at %d"):format(rel, d); break end
      if not cal_set[d] then err[#err + 1] = ("%s: date %d not in calendar"):format(rel, d); break end
      if math.min(o, hh, l, c) <= 0 or hh < math.max(o, c, l) or l > math.min(o, c, hh) then
        bad_ohlc = bad_ohlc + 1
        if bad_ohlc <= 10 then err[#err + 1] = ("%s: OHLC insane at %d: %d %d %d %d"):format(rel, d, o, hh, l, c) end
      end
      if sr ~= 1.0 or dv ~= 0.0 or lu ~= 0 or ld ~= 0 or res ~= zero7 then
        err[#err + 1] = ("%s: split/dividend/limit/reserved not default at %d"):format(rel, d); break
      end
      if v < 0 or (v == 0 and not util.truthy(st.is_index)) then err[#err + 1] = ("%s: volume %d at %d"):format(rel, v, d); break end
      if prev_c ~= nil and pc ~= prev_c then err[#err + 1] = ("%s: preclose %d != prev close %d at %d"):format(rel, pc, prev_c, d); break end
      if fl & ~0x04 ~= 0 or (fl & 0x04 ~= 0 and k ~= 1) then err[#err + 1] = ("%s: unexpected flags %d at %d"):format(rel, fl, d); break end
      prev_d, prev_c = d, c
    end
    local sym = st.symbol
    if sym and want[sym] then
      local m = {}
      for _, r in ipairs(rows) do m[r[1]] = r end
      series[sym] = m
    end
  end)

  -- ---- split continuity
  for _, sp in ipairs(SPLITS) do
    local sym, d, ratio = sp[1], sp[2], sp[3]
    local s = series[sym]
    if not s then
      warn[#warn + 1] = "split check: " .. sym .. " missing"
    elseif not s[d] then
      err[#err + 1] = ("split check: %s has no row on %d"):format(sym, d)
    else
      local ds = {}
      for k in pairs(s) do ds[#ds + 1] = k end
      table.sort(ds)
      local idx = util.index_of(ds, d)
      local prev = ds[idx - 1] or ds[#ds] -- python ds[-1] when d is the first row
      local c0, o1, c1 = s[prev][5], s[d][2], s[d][5]
      local jump = c1 / c0 - 1
      local gap = o1 / c0
      local good = math.abs(jump) < 0.25 and math.abs(gap - 1 / ratio) > 0.2
      local t = good and info or err
      t[#t + 1] = ("split %s %d %d:1  prev close %s -> open %s close %s (%s%%) %s"):format(sym, d, ratio, fmt(c0 / 1000, "%.3f"),
        fmt(o1 / 1000, "%.3f"), fmt(c1 / 1000, "%.3f"), fmt(jump * 100, "%+.1f"), good and "continuous" or "DISCONTINUITY")
    end
  end

  -- ---- spot check
  local hit = 0
  for _, sp in ipairs(SPOT_2010) do
    local sym, ref = sp[1], sp[2]
    local s = series[sym]
    if not s or not s[20100104] then
      warn[#warn + 1] = "spot: " .. sym .. " no 2010-01-04 row"
    else
      local got = s[20100104][5] / 1000
      local dev = got / ref - 1
      if math.abs(dev) <= 0.02 then hit = hit + 1
      else
        warn[#warn + 1] = ("spot: %s 2010-01-04 close %s vs ref %s (%s%%)"):format(sym, fmt(got, "%.3f"), fmt(ref, "%.3f"), fmt(dev * 100, "%+.1f"))
      end
    end
  end
  info[#info + 1] = ("spot check 2010-01-04 closes within 2%%: %d/%d (reference values memorised from public quotes, not a fetched page)"):format(hit, #SPOT_2010)

  local jumps = man.extreme_daily_moves_gt60pct or {}
  local nd, ni = 0, 0
  for _, k in ipairs(rels) do if util.startswith(k, "daily/") then nd = nd + 1 elseif util.startswith(k, "index/") then ni = ni + 1 end end
  print(("files %d (daily %d, index %d); rows %d; compressed %s MB; calendar %d"):format(#rels, nd, ni, total_rows,
    fmt(total_bytes / 1e6, "%.1f"), #cal))
  print("symbols with >60% daily close moves (review list in manifest): " .. util.str(jumps.symbols))
  for _, m in ipairs(info) do print("INFO  " .. m) end
  for _, m in ipairs(warn) do print("WARN  " .. m) end
  for i = 1, math.min(50, #err) do print("ERROR " .. err[i]) end
  print(("RESULT: %s (%d errors, %d warnings)"):format(#err == 0 and "PASS" or "FAIL", #err, #warn))
  return #err > 0 and 1 or 0
end

ok.main(main)
