-- Validate the StreetSocial A-share dataset built by build_ashare_data.lua (+ FX files).
--
-- Checks
--   * .okbar: single zstd frame, header OKBR/v1/market 1/price_scale 1000, size = 16 + rows*64
--   * dates strictly ascending and inside calendar.json
--   * high >= max(open, close), low <= min(open, close)
--   * close inside [limit_down, limit_up] whenever limits are nonzero
--   * suspended rows (flags bit0): volume 0 and OHLC == preclose
--   * stocks.json / manifest.json agree with the files (rows, first/last, sha256)
--   * limit rounding heuristic: closes one tick under the computed limit with high == close
--   * cross-check: random sample of >= 50 SH/SZ stocks x several dates, BaoStock-derived close
--     vs AKShare-equivalent (Eastmoney, fallback Sina) unadjusted close, tolerance 0.01
--   * BSE: Eastmoney-derived close vs cached Sina close
--   * index and FX sanity
--
-- Usage: Tools\lua\lua.exe Tools/verify_ashare_data.lua [--data DIR] [--raw DIR] [--sample 60] [--dates 5] [--seed 1]
local ok = require "ok"
local json, path, kit, util = ok.json, ok.path, ok.kit, ok.util
local md = require "market_data_common"

local DEFAULT_DATA = path.join(ok.root, "Content/IndependentResources/MarketData")
local function default_raw()
  local base = os.getenv("OK_MARKET_RAW") or path.join(os.getenv("TEMP") or "/tmp", "ok_market_raw")
  return path.join(base, "cn")
end

local Report = {}
Report.__index = Report
local function new_report() return setmetatable({ counts = {}, examples = {} }, Report) end
function Report:add(key, example, n)
  self.counts[key] = (self.counts[key] or 0) + (n or 1)
  if example ~= nil then
    local ex = self.examples[key] or {}
    self.examples[key] = ex
    if #ex < 8 then ex[#ex + 1] = example end
  end
end

local PACK = "<i4i4B" -- compact row kept per stock: day, close, flags
local function pyf(x) return util.num(x + 0.0) end

local function check_files(cn, rep)
  local cal = ok.read_json(path.join(cn, "calendar.json"))
  local calset = {}
  for _, d in ipairs(cal) do calset[math.tointeger(tonumber((d:gsub("-", ""))))] = true end
  local stocks = ok.read_json(path.join(cn, "stocks.json"))
  local manifest = ok.read_json(path.join(cn, "manifest.json"))
  local files = manifest.files or {}
  local total_rows = 0
  local data = {}
  local rels, paths = {}, {}
  for i, s in ipairs(stocks) do
    rels[i] = (s.is_index and "index" or "daily") .. "/" .. s.code .. ".okbar"
    paths[i] = path.join(cn, rels[i])
  end
  local exists = {}
  for i, p in ipairs(paths) do exists[i] = path.exists(p) end
  local present = {}
  for i, p in ipairs(paths) do if exists[i] then present[#present + 1] = i end end
  local plist = {}
  for k, i in ipairs(present) do plist[k] = paths[i] end
  local function handle(i, item)
    local s = stocks[i]
    local rel = rels[i]
    local err, rows
    if not exists[i] then
      err = ("[Errno 2] No such file or directory: %s"):format(util.repr((paths[i]:gsub("/", "\\"))))
    elseif item.info.error then
      err = item.info.error
    else
      local raw = item.data
      if item.info.content_size ~= nil and item.info.content_size ~= json.null and item.info.content_size ~= #raw then
        err = "frame content size mismatch"
      else
        local magic, ver, market, n, scale = string.unpack(md.HDR, raw)
        if magic ~= "OKBR" or ver ~= 1 or market ~= 1 or scale ~= 1000 then
          err = ("bad header b'%s' v%d m%d s%d"):format(magic, ver, market, scale)
        elseif #raw ~= 16 + n * 64 then
          err = ("size %d != 16+%d*64"):format(#raw, n)
        else
          rows = (md.okbar_parse(raw))
        end
      end
    end
    if err then
      rep:add("FAIL file_unreadable", rel .. ": " .. err)
    else
      local compact = {}
      for k, r in ipairs(rows) do compact[k] = string.pack(PACK, r[1], r[5], r[13]) end
      data[s.code] = table.concat(compact)
      total_rows = total_rows + #rows
      local mf = files[rel]
      if not mf or mf.rows ~= #rows or mf.sha256 ~= item.info.sha256 then rep:add("FAIL manifest_mismatch", rel) end
      if s.rows ~= #rows or s.first ~= tostring(rows[1][1]) or s.last ~= tostring(rows[#rows][1]) then
        rep:add("FAIL stocks_json_mismatch", rel)
      end
      local prev = 0
      for _, r in ipairs(rows) do
        local d, o, h, l, c, pc, vol, _, split, div, lu, ld, fl = table.unpack(r, 1, 13)
        local tag = s.code .. "@" .. d
        if d <= prev then rep:add("FAIL date_not_ascending", tag) end
        prev = d
        if not calset[d] then rep:add("FAIL date_not_in_calendar", tag) end
        if h < math.max(o, c) or l > math.min(o, c) or l > h then
          rep:add("FAIL ohlc_inconsistent", ("%s o%d h%d l%d c%d"):format(tag, o, h, l, c))
        end
        if c <= 0 then rep:add("FAIL nonpositive_close", tag) end
        if not s.is_index then
          if fl & 1 ~= 0 then
            if vol ~= 0 then rep:add("FAIL suspended_volume_nonzero", tag) end
            if not (o == h and h == l and l == c and c == pc) then rep:add("FAIL suspended_ohlc_not_preclose", tag) end
          end
          if lu ~= 0 or ld ~= 0 then
            if c > lu or c < ld then rep:add("FAIL close_outside_limits", ("%s c%d [%d,%d] st%d"):format(tag, c, ld, lu, fl & 2)) end
            if h > lu or l < ld then rep:add("WARN high_low_outside_limits", ("%s h%d l%d [%d,%d]"):format(tag, h, l, ld, lu)) end
            if fl & 1 == 0 and h == c and c == lu - 10 then
              rep:add("WARN close_one_tick_below_limit_up(high==close)", ("%s c%d lu%d pc%d"):format(tag, c, lu, pc))
            end
            if fl & 1 == 0 and l == c and c == ld + 10 then
              rep:add("WARN close_one_tick_above_limit_down(low==close)", ("%s c%d ld%d pc%d"):format(tag, c, ld, pc))
            end
            if c == lu and fl & 1 == 0 then rep:add("info limit_up_closes") end
            if c == ld and fl & 1 == 0 then rep:add("info limit_down_closes") end
          end
          if split <= 0 or split > 20 then rep:add("WARN odd_split_ratio", tag .. " " .. pyf(split)) end
          if split ~= 1.0 then rep:add("info split_rows") end
          if div ~= 0 then rep:add("info dividend_rows") end
          if fl & 1 ~= 0 then rep:add("info suspended_rows") end
        end
      end
    end
    end
  local done = 0
  local function upto(limit) -- stocks without a file, in order
    while done + 1 < limit do
      done = done + 1
      if not exists[done] then handle(done, nil) end
    end
  end
  md.zstd_each(plist, function(k, info, raw)
    local i = present[k]
    upto(i)
    handle(i, { info = info, data = raw })
    done = i
  end)
  upto(#stocks + 1)
  rep:add("info total_rows", nil, total_rows)
  return cal, stocks, manifest, data
end

local function rows_of(blob)
  local out = {}
  local n = #blob // 9
  for k = 0, n - 1 do
    local d, c, fl = string.unpack(PACK, blob, k * 9 + 1)
    out[k + 1] = { d, c, fl }
  end
  return out
end

local function check_indices(stocks, data, rep)
  for _, s in ipairs(stocks) do
    if s.is_index then
      local blob = data[s.code]
      if not blob or #blob == 0 then
        rep:add("FAIL index_missing", s.code)
      else
        local rows = rows_of(blob)
        for k = 1, #rows - 1 do
          local a, b = rows[k], rows[k + 1]
          if math.abs(b[2] / a[2] - 1) > 0.12 then rep:add("WARN index_jump_gt_12pct", s.code .. "@" .. b[1]) end
        end
        local lo, hi = math.huge, -math.huge
        for _, r in ipairs(rows) do lo = math.min(lo, r[2]); hi = math.max(hi, r[2]) end
        lo, hi = lo / 1000, hi / 1000
        print(("  index %-10s rows=%5d %d..%d close range %.1f..%.1f"):format(s.code, #rows, rows[1][1], rows[#rows][1], lo, hi))
        if s.code == "sh.000001" and not (1500 < lo and hi < 7000) then rep:add("FAIL index_sh000001_range", pyf(lo) .. ".." .. pyf(hi)) end
      end
    end
  end
end

local function check_fx(fxdir, rep)
  local bounds = { { "usd_twd", 25, 40 }, { "cny_twd", 3.5, 6.0 } }
  for _, b in ipairs(bounds) do
    local name, lo, hi = b[1], b[2], b[3]
    local p = path.join(fxdir, name .. ".okfx")
    if not path.exists(p) then
      rep:add("FAIL fx_missing", name)
    else
      local data = kit.zstd_decompress_bytes(path.read_bytes(p))
      local magic, ver, _, n = string.unpack("<c4I2I2I4", data)
      if magic ~= "OKFX" or ver ~= 1 or #data ~= 12 + 12 * n then
        rep:add("FAIL fx_header", name)
      else
        local rows = {}
        for i = 0, n - 1 do local d, r = string.unpack("<i4d", data, 13 + 12 * i); rows[i + 1] = { d, r } end
        for k = 1, #rows - 1 do if rows[k + 1][1] <= rows[k][1] then rep:add("FAIL fx_dates", name); break end end
        local bad = {}
        for _, r in ipairs(rows) do if not (lo < r[2] and r[2] < hi) then bad[#bad + 1] = r end end
        if #bad > 0 then
          local parts = {}
          for k = 1, math.min(3, #bad) do parts[k] = ("(%d, %s)"):format(bad[k][1], pyf(bad[k][2])) end
          rep:add("FAIL fx_range", name .. " [" .. table.concat(parts, ", ") .. "]")
        end
        local mn, mx = math.huge, -math.huge
        for _, r in ipairs(rows) do mn = math.min(mn, r[2]); mx = math.max(mx, r[2]) end
        print(("  fx %s: %d rows %d..%d rate %.4f..%.4f last %.4f"):format(name, n, rows[1][1], rows[#rows][1], mn, mx, rows[#rows][2]))
      end
    end
  end
end

local function ak_closes(ak, code)
  local sym = util.split(code, ".")[2]
  local function closes(f, dcol, ccol)
    local di, ci
    for i, c in ipairs(f.columns) do if c == dcol then di = i elseif c == ccol then ci = i end end
    local out = {}
    for _, r in ipairs(f.rows) do out[math.tointeger(tonumber((r[di]:sub(1, 10):gsub("-", ""))))] = md.pyfloat(r[ci]) end
    return out
  end
  for attempt = 0, 0 do
    local okv, f = pcall(ak.stock_zh_a_hist, sym, "20100101", "20991231")
    if okv then
      if #f.rows > 0 then return closes(f, "日期", "收盘"), "eastmoney" end
    else
      ok.time.sleep(3 * (attempt + 1))
    end
  end
  for attempt = 0, 2 do
    local okv, f = pcall(ak.stock_zh_a_daily, (code:gsub("%.", "")), "20100101", "20991231")
    if okv then
      if #f.rows > 0 then return closes(f, "date", "close"), "sina" end
    else
      ok.time.sleep(3 * (attempt + 1))
    end
  end
  return nil, nil
end

local function cross_check(stocks, data, rep, n_stocks, n_dates, seed, sleep)
  local ak = require "market_akshare"
  local rnd = md.PyRandom(seed)
  local pool = {}
  for _, s in ipairs(stocks) do
    if not s.is_index and s.board ~= "bse" and data[s.code] and (s.source or "baostock") == "baostock" then pool[#pool + 1] = s end
  end
  -- n_stocks listed stocks (AKShare sources rarely keep delisted symbols) + a few delisted extras
  local delisted, listed = {}, {}
  for _, s in ipairs(pool) do
    if util.truthy(s.delist_date ~= json.null and s.delist_date) then delisted[#delisted + 1] = s else listed[#listed + 1] = s end
  end
  local sample = rnd.sample(listed, math.min(#listed, n_stocks))
  util.extend(sample, rnd.sample(delisted, math.min(#delisted, 5)))
  local checked, good, stocks_ok = 0, 0, 0
  local srcs, sorder = {}, {}
  for _, s in ipairs(sample) do
    local rows = {}
    for _, r in ipairs(rows_of(data[s.code])) do if r[3] & 1 == 0 then rows[#rows + 1] = r end end
    if #rows > 0 then
      local closes, src = ak_closes(ak, s.code)
      ok.time.sleep(sleep)
      if closes == nil then
        rep:add("WARN crosscheck_unavailable", s.code)
      else
        if not srcs[src] then srcs[src] = 0; sorder[#sorder + 1] = src end
        srcs[src] = srcs[src] + 1
        local picks = rnd.sample(rows, math.min(n_dates, #rows))
        local stock_ok = true
        for _, r in ipairs(picks) do
          local ref = closes[r[1]]
          if ref == nil then
            rep:add("WARN crosscheck_date_absent_in_akshare", s.code .. "@" .. r[1])
          else
            checked = checked + 1
            if math.abs(r[2] / 1000 - ref) <= 0.01 + 1e-9 then good = good + 1
            else
              stock_ok = false
              rep:add("FAIL crosscheck_close_mismatch", ("%s@%d bs=%s ak=%s"):format(s.code, r[1], pyf(r[2] / 1000), pyf(ref)))
            end
          end
        end
        if stock_ok then stocks_ok = stocks_ok + 1 end
      end
    end
  end
  local parts, total = {}, 0
  for _, k in ipairs(sorder) do parts[#parts + 1] = util.repr(k) .. ": " .. srcs[k]; total = total + srcs[k] end
  print(("  cross-check: %d stocks sampled, %d fetched {%s}, %d closes compared, %d within 0.01, %d stocks fully matching"):format(
    #sample, total, table.concat(parts, ", "), checked, good, stocks_ok))
  rep:add("info crosscheck_compared", nil, checked)
  rep:add("info crosscheck_matched", nil, good)
end

local function cached_sina_check(stocks, data, raw, rep)
  local n, cmp_, bad = 0, 0, 0
  local bad_stocks, nbad_stocks = {}, 0
  for _, s in ipairs(stocks) do
    if not s.is_index and s.source == "baostock" and data[s.code] then
      local p = path.join(raw, "sina", "k", s.code .. ".csv.gz")
      if path.exists(p) then
        local rows = md.read_rows_gz(p)
        if #rows > 0 and rows[1].close ~= nil then
          local ref = {}
          for _, r in ipairs(rows) do ref[math.tointeger(tonumber((r.date:sub(1, 10):gsub("-", ""))))] = md.pyfloat(r.close) end
          n = n + 1
          for _, r in ipairs(rows_of(data[s.code])) do
            if r[3] & 1 == 0 and ref[r[1]] ~= nil then
              cmp_ = cmp_ + 1
              if math.abs(r[2] / 1000 - ref[r[1]]) > 0.01 + 1e-9 then
                bad = bad + 1
                if not bad_stocks[s.code] then bad_stocks[s.code] = true; nbad_stocks = nbad_stocks + 1 end
                rep:add("WARN cached_sina_close_differs", ("%s@%d bs=%s sina=%s"):format(s.code, r[1], pyf(r[2] / 1000), pyf(ref[r[1]])))
              end
            end
          end
        end
      end
    end
  end
  local pct = cmp_ > 0 and 100.0 * (cmp_ - bad) / cmp_ or 0
  print(("  BaoStock vs cached AKShare/Sina: %d stocks, %d closes, %d differ >0.01 (%.4f%% match) in %d stocks"):format(n, cmp_, bad, pct, nbad_stocks))
  rep:add("info cached_sina_compared", nil, cmp_)
end

local function bse_check(stocks, data, raw, rep)
  local n, cmp_, bad = 0, 0, 0
  for _, s in ipairs(stocks) do
    if s.board == "bse" and data[s.code] then
      local p = path.join(raw, "bj", "em", s.symbol .. ".csv.gz")
      if path.exists(p) then
        local ref = {}
        for _, r in ipairs(md.read_rows_gz(p)) do ref[math.tointeger(tonumber((r["日期"]:sub(1, 10):gsub("-", ""))))] = md.pyfloat(r["收盘"]) end
        n = n + 1
        for _, r in ipairs(rows_of(data[s.code])) do
          if r[3] & 1 == 0 and ref[r[1]] ~= nil then
            cmp_ = cmp_ + 1
            if math.abs(r[2] / 1000 - ref[r[1]]) > 0.01 + 1e-9 then
              bad = bad + 1
              rep:add("FAIL bse_sina_vs_eastmoney_close", ("%s@%d sina=%s em=%s"):format(s.code, r[1], pyf(r[2] / 1000), pyf(ref[r[1]])))
            end
          end
        end
      end
    end
  end
  print(("  BSE: %d stocks with Eastmoney cache, %d closes Sina(okbar) vs Eastmoney compared, %d mismatches"):format(n, cmp_, bad))
end

local function main()
  local ap = ok.args.parser("Validate the StreetSocial A-share dataset")
  ap:opt("--data", { default = DEFAULT_DATA })
  ap:opt("--raw", { default = default_raw() })
  ap:opt("--sample", { type = "int", default = 60 })
  ap:opt("--dates", { type = "int", default = 5 })
  ap:opt("--seed", { type = "int", default = 20261011 })
  ap:opt("--sleep", { type = "float", default = 1.0 })
  ap:flag("--no-network", { help = "skip the AKShare cross-check" })
  local a = ap:parse(arg)
  local rep = new_report()
  local cn = path.join(a.data, "cn")
  print("[files]")
  local cal, stocks, _, data = check_files(cn, rep)
  local ns, by = {}, {}
  for _, s in ipairs(stocks) do
    if not s.is_index then
      ns[#ns + 1] = s
      local k = s.board .. "\0" .. (util.truthy(s.delist_date ~= json.null and s.delist_date) and "delisted" or "listed")
      by[k] = (by[k] or 0) + 1
    end
  end
  local size = 0
  path.walk(cn, function(dir, _, files) for _, f in ipairs(files) do size = size + (path.size(path.join(dir, f)) or 0) end end)
  print(("  calendar %s..%s (%d days); stocks %d; indices %d; size %.1f MB"):format(cal[1], cal[#cal], #cal, #ns, #stocks - #ns, size / 1e6))
  local keys = {}
  for k in pairs(by) do keys[#keys + 1] = k end
  table.sort(keys)
  for _, k in ipairs(keys) do
    local b, st = k:match("^(.-)%z(.*)$")
    print(("    %-8s %-9s %d"):format(b, st, by[k]))
  end
  local src = {}
  for _, s in ipairs(stocks) do local k = s.source or "?"; src[k] = (src[k] or 0) + 1 end
  local skeys = {}
  for k in pairs(src) do skeys[#skeys + 1] = k end
  table.sort(skeys)
  for _, k in ipairs(skeys) do print(("    source %s: %d"):format(k, src[k])) end
  print("[indices]")
  check_indices(stocks, data, rep)
  print("[fx]")
  check_fx(path.join(a.data, "fx"), rep)
  print("[bse]")
  bse_check(stocks, data, a.raw, rep)
  print("[cached second source]")
  cached_sina_check(stocks, data, a.raw, rep)
  if not a.no_network then
    print("[cross-check BaoStock vs AKShare]")
    cross_check(stocks, data, rep, a.sample, a.dates, a.seed, a.sleep)
  end
  print("[summary]")
  local fails = 0
  local ckeys = {}
  for k in pairs(rep.counts) do ckeys[#ckeys + 1] = k end
  table.sort(ckeys)
  for _, k in ipairs(ckeys) do
    print(("  %s: %d"):format(k, rep.counts[k]))
    for _, ex in ipairs(rep.examples[k] or {}) do print("      e.g. " .. ex) end
    if util.startswith(k, "FAIL") then fails = fails + rep.counts[k] end
  end
  print("RESULT: " .. (fails == 0 and "PASS" or ("FAIL (%d hard failures)"):format(fails)))
  return fails == 0 and 0 or 1
end

ok.main(main)
