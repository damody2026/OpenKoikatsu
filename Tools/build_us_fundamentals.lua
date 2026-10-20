-- Quarterly fundamentals for US stocks from SEC EDGAR XBRL -> market.sqlite table `fundamentals` (market 'us').
-- Also fills symbols.cik and symbols.industry (SEC SIC description). Dev tool, not shipped.
--
-- Sources (data.sec.gov JSON APIs; descriptive User-Agent WITHOUT any contact details, <= ~6 requests/s total):
--   * ticker -> CIK: thetrading.tools identity.json (a copy of SEC company_tickers_exchange.json, cached by
--     build_us_data.lua in <raw us>/lists/ttt_identity.json). www.sec.gov/files/company_tickers.json itself
--     answers 403 to a User-Agent without contact info, so it is not used.
--   * https://data.sec.gov/api/xbrl/companyfacts/CIK##########.json   (filtered to the tags below before caching)
--   * https://data.sec.gov/submissions/CIK##########.json              (sic, sicDescription, tickers, fiscalYearEnd)
--
-- Conventions (same as the CN rows): values are CUMULATIVE fiscal year-to-date. period = 'Q1' (3 months),
-- 'H1' (6 months), 'Q3' (9 months), 'FY' (12 months), counted from the company's fiscal-year start, so
-- period_end is the company's own fiscal period end (e.g. Apple Q1 ends late December).
-- available = the 'filed' date of the earliest 10-Q/10-K (or 20-F/40-F) that reported the period; values are
-- taken from that first filing (as originally reported, restatements ignored).
--
-- Steps
--   Tools\lua\lua.exe Tools/build_us_fundamentals.lua fetch --raw <dir> --identity <ttt_identity.json> [--stocks stocks.json]
--   Tools\lua\lua.exe Tools/build_us_fundamentals.lua load  --raw <dir> --identity <ttt_identity.json> --db <market.sqlite>
--   Tools\lua\lua.exe Tools/build_us_fundamentals.lua validate --db <market.sqlite>
-- `fetch` is resumable; `load` deletes and re-inserts only market 'us' fundamentals rows (idempotent).
local ok = require "ok"
local json, path, kit, util = ok.json, ok.path, ok.kit, ok.util
local md = require "market_data_common"

local M = {}
M.MD = path.join(ok.root, "Content/IndependentResources/MarketData")
local UA = { ["User-Agent"] = "OpenKoikatsu-market-data/1.0 (game research)" }
local CF_URL = "https://data.sec.gov/api/xbrl/companyfacts/CIK%010d.json"
local SUB_URL = "https://data.sec.gov/submissions/CIK%010d.json"
local FIRST_END = 20090101
local O = json.object
local NULL = json.null

local function tags_of(tax, list) local t = {}; for i, x in ipairs(list) do t[i] = { tax, x } end; return t end
-- metric -> ordered tag candidates (taxonomy, tag)
local DUR_ORDER = { "revenue", "net_income", "eps", "operating_cf", "dividend_ps" }
local DUR = {
  revenue = util.extend(tags_of("us-gaap", { "Revenues", "RevenueFromContractWithCustomerExcludingAssessedTax", "RevenueFromContractWithCustomerIncludingAssessedTax",
    "SalesRevenueNet", "SalesRevenueGoodsNet", "SalesRevenueServicesNet", "RevenuesNetOfInterestExpense",
    "TotalRevenuesAndOtherIncome", "RegulatedAndUnregulatedOperatingRevenue", "ElectricUtilityRevenue",
    "OilAndGasRevenue", "RealEstateRevenueNet", "OperatingLeasesIncomeStatementLeaseRevenue",
    "InterestAndDividendIncomeOperating" }), { { "ifrs-full", "Revenue" } }),
  net_income = { { "us-gaap", "NetIncomeLoss" }, { "us-gaap", "NetIncomeLossAvailableToCommonStockholdersBasic" },
    { "us-gaap", "ProfitLoss" }, { "ifrs-full", "ProfitLossAttributableToOwnersOfParent" }, { "ifrs-full", "ProfitLoss" } },
  eps = { { "us-gaap", "EarningsPerShareBasic" }, { "us-gaap", "EarningsPerShareBasicAndDiluted" },
    { "ifrs-full", "BasicEarningsLossPerShare" } },
  operating_cf = { { "us-gaap", "NetCashProvidedByUsedInOperatingActivities" },
    { "us-gaap", "NetCashProvidedByUsedInOperatingActivitiesContinuingOperations" },
    { "ifrs-full", "CashFlowsFromUsedInOperatingActivities" } },
  dividend_ps = { { "us-gaap", "CommonStockDividendsPerShareDeclared" }, { "us-gaap", "CommonStockDividendsPerShareCashPaid" } },
}
local INST_ORDER = { "equity", "total_assets", "shares_bs" }
local INST = {
  equity = { { "us-gaap", "StockholdersEquity" }, { "us-gaap", "StockholdersEquityIncludingPortionAttributableToNoncontrollingInterest" },
    { "ifrs-full", "EquityAttributableToOwnersOfParent" }, { "ifrs-full", "Equity" } },
  total_assets = { { "us-gaap", "Assets" }, { "ifrs-full", "Assets" } },
  shares_bs = { { "us-gaap", "CommonStockSharesOutstanding" } },
}
local DEI_SHARES = { "dei", "EntityCommonStockSharesOutstanding" }
local FORMS = util.set({ "10-Q", "10-K", "20-F", "40-F", "10-KT", "10-QT" })
local KEEP_TAGS = {}
for _, m in ipairs(DUR_ORDER) do for _, t in ipairs(DUR[m]) do KEEP_TAGS[t[1] .. "\0" .. t[2]] = true end end
for _, m in ipairs(INST_ORDER) do for _, t in ipairs(INST[m]) do KEEP_TAGS[t[1] .. "\0" .. t[2]] = true end end
KEEP_TAGS[DEI_SHARES[1] .. "\0" .. DEI_SHARES[2]] = true

M.LOG = nil
local function log(msg)
  local line = os.date("%Y-%m-%d %H:%M:%S") .. " " .. msg
  print(line); io.stdout:flush()
  if M.LOG then path.append_text(M.LOG, line .. "\n", { newline = "\n" }) end
end

local function norm(code) return (code:upper():gsub("%.", "-"):gsub("/", "-")) end
local function nz(v) if v == nil or v == NULL then return nil end return v end

local function ticker_map(identity_path)
  local j = ok.read_json(identity_path)
  local iss = {}
  for _, i in ipairs(j.issuers) do iss[i.id] = i end
  local m = {}
  for _, s in ipairs(j.securities) do
    local i = iss[s.issuer_id]
    if i and util.truthy(nz(i.cik)) then
      local names, seen = {}, {}
      local function addn(t) if t ~= nil and t ~= NULL and not seen[t] then seen[t] = true; names[#names + 1] = t end end
      addn(s.ticker)
      for _, a in ipairs(nz(s.aliases) or {}) do addn(a) end
      for _, h in ipairs(nz(s.ticker_history) or {}) do addn(h.ticker) end
      for _, t in ipairs(names) do
        if util.truthy(t) then
          local k = norm(t)
          -- prefer active, primary securities when a ticker is reused
          local pri = { util.truthy(s.active) and 1 or 0, util.truthy(s.primary_for_issuer) and 1 or 0 }
          if not m[k] or util.compare(pri, m[k][2]) > 0 then
            m[k] = { math.tointeger(tonumber(tostring(i.cik))) or i.cik, pri }
          end
        end
      end
    end
  end
  local out = {}
  for k, v in pairs(m) do out[k] = v[1] end
  return out
end
M.ticker_map = ticker_map

-- ---------------------------------------------------------------- fetch
local function filter_facts(b)
  local j = json.decode(b)
  local facts = O()
  local out = O({ { "cik", nz(j.cik) or NULL }, { "entityName", nz(j.entityName) or NULL }, { "facts", facts } })
  local jf = nz(j.facts) or {}
  for _, tax in ipairs(json.keys(jf)) do
    local tags = jf[tax]
    for _, tag in ipairs(json.keys(tags)) do
      if KEEP_TAGS[tax .. "\0" .. tag] then
        facts[tax] = facts[tax] or O()
        facts[tax][tag] = O({ { "units", tags[tag].units or O() } })
      end
    end
  end
  return out
end
M.filter_facts = filter_facts

local function cmd_fetch(a)
  local raw = a.raw
  path.mkdir(path.join(raw, "facts"), true)
  path.mkdir(path.join(raw, "subs"), true)
  local tm = ticker_map(a.identity)
  local stocks = ok.read_json(a.stocks)
  local set = {}
  for _, s in ipairs(stocks) do local c = tm[norm(s.code)]; if c then set[c] = true end end
  local ciks = {}
  for c in pairs(set) do ciks[#ciks + 1] = c end
  table.sort(ciks)
  log(("fetch: %d symbols, %d CIKs mapped"):format(#stocks, #ciks))
  local last = 0.0
  local function get(url)
    local delay, st = 10.0, -1
    for _ = 1, 5 do
      local d = last + a.gap - ok.time.time()
      if d > 0 then ok.time.sleep(d) end
      last = ok.time.time()
      local r = kit.http_request(url, { headers = UA, timeout = 60 })
      st = r.status
      if st == 200 then return r.body end
      if st == 404 then return nil end
      if st == 403 then error({ refused = url }) end
      ok.time.sleep(delay + math.random())
      delay = math.min(delay * 2, 300)
    end
    error(("giving up %s (%d)"):format(url, st), 0)
  end
  local refused = false
  for n, cik in ipairs(ciks) do
    local res = {}
    local okv, err = pcall(function()
      local fp = path.join(raw, "facts", cik .. ".json.zst")
      if not path.exists(fp) and not path.exists(path.join(raw, "facts", cik .. ".none")) then
        local b = get(CF_URL:format(cik))
        if b == nil then
          path.write_text(path.join(raw, "facts", cik .. ".none"), "404")
          res[#res + 1] = "facts404"
        else
          path.write_bytes(fp, md.zstd_compress(json.encode(filter_facts(b)), 10))
          res[#res + 1] = "facts"
        end
      end
      local sp = path.join(raw, "subs", cik .. ".json")
      if not path.exists(sp) then
        local b = get(SUB_URL:format(cik))
        if b ~= nil then
          local j = json.decode(b)
          local keep = O()
          for _, k in ipairs({ "cik", "name", "sic", "sicDescription", "tickers", "exchanges", "fiscalYearEnd", "entityType",
            "stateOfIncorporation", "formerNames" }) do
            local v = j[k]
            if v == nil then v = NULL end
            keep[k] = v
          end
          path.write_text(sp, json.encode(keep))
          res[#res + 1] = "subs"
        end
      end
    end)
    local msg
    if not okv then
      if type(err) == "table" and err.refused then
        refused = true
        msg = "REFUSED " .. err.refused
      else error(err, 0) end
    else
      msg = #res > 0 and table.concat(res, "+") or "cached"
    end
    if n % 200 == 0 or msg:find("REFUSED", 1, true) then log(("fetch %d/%d cik=%s %s"):format(n, #ciks, cik, msg)) end
    if refused then break end
  end
  if refused then
    log("SEC refused requests (403) -> stopped; no contact details were added")
    os.exit(2)
  end
  log("fetch done")
end

-- ---------------------------------------------------------------- build
local function ymd(s) return math.tointeger(tonumber((s:gsub("-", "")))) end
local function dnum(s) return ok.time.days_from_civil(tonumber(s:sub(1, 4)), tonumber(s:sub(6, 7)), tonumber(s:sub(9, 10))) end
local function idate(n) return md.ymd_days(n) end

local function classify(days)
  if 80 <= days and days <= 100 then return "Q1" end
  if 170 <= days and days <= 195 then return "H1" end
  if 260 <= days and days <= 285 then return "Q3" end
  if 350 <= days and days <= 380 then return "FY" end
  return nil
end

local function rstrip_chars(s, chars)
  while #s > 0 and chars:find(s:sub(-1), 1, true) do s = s:sub(1, -2) end
  return s
end

-- entries of one unit -> ordered {keys = {...}, best = {key -> cand}} keeping the earliest filing per (start, end).
local function first_report(entries)
  local best, keys = {}, {}
  for _, e in ipairs(entries) do
    local form = nz(e.form)
    if form and (FORMS[form] or FORMS[rstrip_chars(form, "/A")]) then
      local key = (nz(e.start) or "\0none") .. "|" .. e["end"]
      local cand = { e.filed, util.endswith(form, "/A") and 1 or 0, e.val, e.accn, form, start = nz(e.start), ["end"] = e["end"] }
      local b = best[key]
      if not b then keys[#keys + 1] = key; best[key] = cand
      elseif util.compare({ cand[1], cand[2] }, { b[1], b[2] }) < 0 then best[key] = cand end
    end
  end
  return keys, best
end

local function pick_unit(units, money)
  local ukeys = json.keys(units)
  if money then
    if units.USD ~= nil then return "USD", units.USD end
    for _, u in ipairs(ukeys) do
      if utf8.len(u) == 3 and u:match("^%a+$") then return u, units[u] end
    end
  else
    for _, u in ipairs({ "USD/shares", "shares" }) do if units[u] ~= nil then return u, units[u] end end
    for _, u in ipairs(ukeys) do if util.endswith(u, "/shares") then return u, units[u] end end
  end
  return nil, nil
end

local function build_company(facts)
  local F = nz(facts.facts) or {}
  local function tag(t) return (nz(F[t[1]]) or {})[t[2]] end
  -- duration metrics: per (start,end) earliest filing, first tag in priority order that has the period
  local dur = {}
  for _, metric in ipairs(DUR_ORDER) do
    dur[metric] = {}
    for _, t in ipairs(DUR[metric]) do
      local v = tag(t)
      if util.truthy(v) then
        local unit, ents = pick_unit(v.units, metric == "revenue" or metric == "net_income" or metric == "operating_cf")
        if util.truthy(ents) then
          local keys, best = first_report(ents)
          for _, k in ipairs(keys) do
            local c = best[k]
            if util.truthy(c.start) and not dur[metric][k] then
              dur[metric][k] = { c[3], c[1], c[4], util.split(unit, "/")[1], t[2], start = c.start, ["end"] = c["end"] }
            end
          end
        end
      end
    end
  end
  -- every reported EPS value per period (later filings restate comparatives, e.g. after a split)
  local eps_hist, eps_keys = {}, {}
  for _, t in ipairs(DUR.eps) do
    local v = tag(t)
    if util.truthy(v) then
      local _, ents = pick_unit(v.units, false)
      for _, e in ipairs(ents or {}) do
        if util.truthy(nz(e.start)) and FORMS[util.split(nz(e.form) or "", "/")[1]] then
          local k = e.start .. "|" .. e["end"]
          if not eps_hist[k] then eps_hist[k] = {}; eps_keys[#eps_keys + 1] = { k, e.start, e["end"] } end
          table.insert(eps_hist[k], { e.filed, e.val + 0.0 })
        end
      end
      if util.truthy(ents) then break end
    end
  end
  for k, l in pairs(eps_hist) do eps_hist[k] = util.sorted(l) end
  local function eps_asof(key, filed)
    local best
    for _, fv in ipairs(eps_hist[key] or {}) do if fv[1] <= filed then best = fv[2] end end
    return best
  end
  local inst = {}
  for _, metric in ipairs(INST_ORDER) do
    inst[metric] = {}
    for _, t in ipairs(INST[metric]) do
      local v = tag(t)
      if util.truthy(v) then
        local unit, ents = pick_unit(v.units, metric ~= "shares_bs")
        if util.truthy(ents) then
          local keys, best = first_report(ents)
          for _, k in ipairs(keys) do
            local c = best[k]
            if inst[metric][c["end"]] == nil then inst[metric][c["end"]] = { c[3], c[1], c[4], unit } end
          end
        end
      end
    end
  end
  local dei = {}
  local dv = tag(DEI_SHARES)
  if util.truthy(dv) and dv.units.shares ~= nil then
    for _, e in ipairs(dv.units.shares) do
      dei[e.accn] = dei[e.accn] or {}
      dei[e.accn][e["end"]] = e.val
    end
  end
  -- candidate periods = (start,end) of revenue / net income / eps
  local keyset, keys = {}, {}
  for _, m in ipairs({ "net_income", "revenue", "eps" }) do
    for k, x in pairs(dur[m]) do
      if not keyset[k] then keyset[k] = { x.start, x["end"] }; keys[#keys + 1] = k end
    end
  end
  table.sort(keys) -- (python iterates a set here; sorted keeps the result deterministic)
  local fy_starts = {}
  for _, k in ipairs(keys) do
    local s, e = keyset[k][1], keyset[k][2]
    local c = classify(dnum(e) - dnum(s) + 1)
    if c == "FY" or c == "H1" or c == "Q3" then fy_starts[s] = true end
  end
  local rows, rkeys = {}, {}
  local skipped, skorder = {}, {}
  for _, key in ipairs(keys) do
    local s, e = keyset[key][1], keyset[key][2]
    local per = classify(dnum(e) - dnum(s) + 1)
    local skip = not per or ymd(e) < FIRST_END
    if not skip and per == "Q1" and not fy_starts[s] then
      -- 3-month fact that does not start a fiscal year -> a Q2/Q3/Q4 single quarter, not YTD
      -- (Q1 also accepted when the previous FY ended the day before)
      local near = false
      for x in pairs(fy_starts) do if math.abs(dnum(s) - dnum(x)) <= 3 then near = true; break end end
      if not near then
        local prev_fy = false
        for _, k2 in ipairs(keys) do
          local ks, ke = keyset[k2][1], keyset[k2][2]
          if util.truthy(ke) and math.abs(dnum(s) - dnum(ke) - 1) <= 3 and classify(dnum(ke) - dnum(ks) + 1) == "FY" then prev_fy = true; break end
        end
        if not prev_fy then skip = true end
      end
    end
    if not skip then
      local got = {}
      for _, m in ipairs(DUR_ORDER) do got[m] = dur[m][key] end
      local filed, accn_pair
      for _, m in ipairs(DUR_ORDER) do
        local x = got[m]
        if x then
          if filed == nil or x[2] < filed then filed = x[2] end
          if accn_pair == nil or util.compare({ x[2], x[3] }, accn_pair) < 0 then accn_pair = { x[2], x[3] } end
        end
      end
      local accn = accn_pair[2]
      local cur = got.revenue or got.net_income or got.operating_cf
      local currency = cur and cur[4] or "USD"
      local lag = dnum(filed) - dnum(e)
      if lag <= 0 or lag > (per == "FY" and 400 or 200) then
        -- first seen as a comparative in a later filing (original pre-dates XBRL) or bogus date
        if not skipped[per] then skipped[per] = 0; skorder[#skorder + 1] = per end
        skipped[per] = skipped[per] + 1
      else
        local tagsm = {}
        for _, m in ipairs(DUR_ORDER) do if got[m] then tagsm[m] = got[m][5] end end
        local r = { key = key, filed = filed, start = s, period_end = ymd(e), period = per, available = ymd(filed),
          currency = currency, accn = accn, tags = tagsm }
        for _, m in ipairs(DUR_ORDER) do r[m] = got[m] and (got[m][1] + 0.0) or nil end
        for _, m in ipairs(INST_ORDER) do local x = inst[m][e]; r[m] = x and (x[1] + 0.0) or nil end
        -- shares: cover-page shares of the filing that first reported the period (sum of classes), else balance sheet
        local sh
        if dei[accn] then
          local vals, lastk = dei[accn], nil
          for k in pairs(vals) do if lastk == nil or k > lastk then lastk = k end end
          sh = vals[lastk] + 0.0
        end
        if not util.truthy(sh) and util.truthy(r.shares_bs) then sh = r.shares_bs end
        r.total_shares = sh
        local k = r.period_end .. "|" .. per
        if not rows[k] then rkeys[#rkeys + 1] = k; rows[k] = r
        elseif r.available < rows[k].available then rows[k] = r end
      end
    end
  end
  -- eps_ttm, bvps, roe
  for _, k in ipairs(rkeys) do
    local r = rows[k]
    if util.truthy(r.equity) and util.truthy(r.total_shares) then r.bvps = r.equity / r.total_shares else r.bvps = nil end
    if r.net_income ~= nil and util.truthy(r.equity) and r.equity > 0 then r.roe = r.net_income / r.equity * 100.0 else r.roe = nil end
    r.eps_ttm = nil
    if r.eps ~= nil then
      if r.period == "FY" then r.eps_ttm = r.eps
      else
        local sd, pe = dnum(r.start), idate(r.period_end)
        -- previous fiscal year = FY period ending the day before this period's fiscal-year start;
        -- same period last year = same length, ending ~1 year earlier. Values as known when this period was
        -- filed (later filings restate comparatives, e.g. for splits), so the three EPS are consistent.
        local fy_eps, prev_eps
        for _, ek in ipairs(eps_keys) do
          local ks, ke = ek[2], ek[3]
          local kd = dnum(ke) - dnum(ks) + 1
          if fy_eps == nil and classify(kd) == "FY" and math.abs(sd - dnum(ke) - 1) <= 7 then
            fy_eps = eps_asof(ek[1], r.filed)
          elseif prev_eps == nil and classify(kd) == r.period and 355 <= pe - dnum(ke) and pe - dnum(ke) <= 375 then
            prev_eps = eps_asof(ek[1], r.filed)
          end
        end
        if fy_eps ~= nil and prev_eps ~= nil then r.eps_ttm = util.round(r.eps + fy_eps - prev_eps, 6) end
      end
    end
  end
  local out = {}
  for _, k in ipairs(rkeys) do out[#out + 1] = rows[k] end
  out = util.sorted(out, function(r) return { r.period_end, r.period } end)
  if #out > 0 then
    local sk = O()
    for _, p in ipairs(skorder) do sk[p] = skipped[p] end
    out[1].skipped_late = sk
  end
  return out
end
M.build_company = build_company

-- Counter with first-insertion key order (dict(Counter) / its repr).
local function counter()
  local c = { order = {}, v = {} }
  function c:add(k, n)
    if self.v[k] == nil then self.v[k] = 0; self.order[#self.order + 1] = k end
    self.v[k] = self.v[k] + (n or 1)
  end
  function c:obj() local o = O(); for _, k in ipairs(self.order) do o[k] = self.v[k] end; return o end
  return c
end

local function cmd_load(a)
  local raw, dbp = a.raw, a.db
  if path.exists(path.with_suffix(dbp, ".sqlite.building")) then
    io.stderr:write("market.sqlite is being rebuilt (.building exists); try later\n")
    os.exit(1)
  end
  local tm = ticker_map(a.identity)
  local syms = kit.sql_rows(dbp, "SELECT id, code, etf, is_index FROM symbols WHERE market='us'", {}, { timeout = 120 })
  local out, stats = {}, counter()
  local gaps = { no_cik = {}, no_facts = {}, no_rows = {} }
  local ops = { { sql = "BEGIN" }, { sql = "UPDATE symbols SET cik=NULL, industry=NULL WHERE market='us'" } }
  local cache_cik, cache_rows
  for _, sy in ipairs(syms) do
    local sid, code = sy[1], sy[2]
    local cik = tm[norm(code)]
    if not cik then
      table.insert(gaps.no_cik, code)
    else
      local sp = path.join(raw, "subs", cik .. ".json")
      local ind
      if path.exists(sp) then
        local j = ok.read_json(sp)
        ind = util.truthy(nz(j.sicDescription)) and j.sicDescription or nil
        if util.truthy(nz(j.tickers)) then
          local found = false
          for _, t in ipairs(j.tickers) do if norm(t) == norm(code) then found = true end end
          if not found then stats:add("ticker_not_in_submissions") end
        end
      end
      ops[#ops + 1] = { sql = "UPDATE symbols SET cik=?, industry=? WHERE id=?", params = { cik, ind or NULL, sid } }
      stats:add("cik_set")
      stats:add("industry_set", ind and 1 or 0)
      local fp = path.join(raw, "facts", cik .. ".json.zst")
      if not path.exists(fp) then
        table.insert(gaps.no_facts, code)
      else
        if cache_cik ~= cik then
          local facts = json.decode(kit.zstd_decompress_bytes(path.read_bytes(fp)))
          cache_cik, cache_rows = cik, build_company(facts) -- one-entry cache (share classes are adjacent rarely; keep memory low)
        end
        local rows = cache_rows
        if #rows == 0 then
          table.insert(gaps.no_rows, code)
        else
          stats:add("symbols_with_rows")
          for _, r in ipairs(rows) do
            local tk = util.sorted_keys(r.tags)
            local parts = {}
            for _, k in ipairs(tk) do parts[#parts + 1] = k .. "=" .. r.tags[k] end
            local src = "sec-xbrl " .. r.accn .. " " .. table.concat(parts, ",")
            local function v(x) if x == nil then return NULL end return x end
            out[#out + 1] = { sid, r.period_end, r.period, r.available, util.truthy(r.currency) and r.currency or "USD", v(r.revenue),
              v(r.net_income), v(r.eps), v(r.eps_ttm), v(r.bvps), v(r.roe), v(r.total_shares), v(r.equity), v(r.total_assets),
              v(r.operating_cf), v(r.dividend_ps), src }
            stats:add("rows_" .. r.period)
          end
        end
      end
    end
  end
  ops[#ops + 1] = { sql = "DELETE FROM fundamentals WHERE sid IN (SELECT id FROM symbols WHERE market='us')" }
  ops[#ops + 1] = { sql = "INSERT OR REPLACE INTO fundamentals VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)", many = out }
  local tags = O()
  for _, m in ipairs(DUR_ORDER) do local l = {}; for i, t in ipairs(DUR[m]) do l[i] = t[2] end; tags[m] = l end
  for _, m in ipairs(INST_ORDER) do local l = {}; for i, t in ipairs(INST[m]) do l[i] = t[2] end; tags[m] = l end
  local gap_counts = O({ { "no_cik", #gaps.no_cik }, { "no_facts", #gaps.no_facts }, { "no_rows", #gaps.no_rows } })
  local man = O({
    { "sources", O({
      { "ticker_to_cik", "SEC company_tickers_exchange.json via thetrading.tools identity.json (2026-10-08 snapshot)" },
      { "facts", "https://data.sec.gov/api/xbrl/companyfacts/CIK##########.json (us-gaap / ifrs-full / dei, non-dimensional facts)" },
      { "industry", "https://data.sec.gov/submissions/CIK##########.json sicDescription -> symbols.industry" } }) },
    { "convention", "values are CUMULATIVE fiscal year-to-date: Q1 = 3 months, H1 = 6 months, Q3 = 9 months, FY = 12 months from the company's own fiscal-year start; period_end is the company's fiscal period end (not calendar quarter)" },
    { "available", "'filed' date of the earliest 10-Q/10-K/20-F/40-F (amendments only if nothing earlier) reporting the period; values are as first reported" },
    { "tags", tags },
    { "derived", "total_shares = dei:EntityCommonStockSharesOutstanding of the first filing (cover date, all classes summed) else us-gaap:CommonStockSharesOutstanding at period end; bvps = equity / total_shares; roe = net_income / period-end equity x 100 (not annualised, equity > 0 only); eps_ttm = cumulative EPS + last FY EPS - same period last year (FY: = EPS); dividend_ps = declared (else paid) per share, cumulative" },
    { "currency", "reporting currency of the money facts (USD for almost all; foreign 20-F filers may be EUR, JPY, ...); eps in the same currency" },
    { "source_column", "'sec-xbrl <accession> metric=tag,...'" },
    { "stats", stats:obj() },
    { "gaps", gap_counts },
    { "gap_symbols", O({ { "no_cik", gaps.no_cik }, { "no_facts", gaps.no_facts }, { "no_rows", gaps.no_rows } }) },
  })
  ops[#ops + 1] = { sql = "INSERT OR REPLACE INTO meta VALUES(?,?)", params = { "manifest.us_fundamentals", json.encode(man, { ensure_ascii = false }) } }
  ops[#ops + 1] = { sql = "COMMIT" }
  kit.sqlite(dbp, ops, { timeout = 120 })
  log(("load: %d rows; stats %s; gaps %s"):format(#out, util.repr(stats:obj()), util.repr(gap_counts)))
end
M.cmd_load = cmd_load

local function rows_repr(rows)
  local parts = {}
  for i, r in ipairs(rows) do
    local cells = {}
    for k = 1, #r do cells[k] = util.repr(r[k] == NULL and nil or r[k]) end
    parts[i] = "(" .. table.concat(cells, ", ") .. (#r == 1 and ",)" or ")")
  end
  return "[" .. table.concat(parts, ", ") .. "]"
end

local function cmd_validate(a)
  local db = "file:" .. a.db .. "?mode=ro"
  local function q(s, ...) return kit.sql_rows(db, s, { ... }, { readonly = true }) end
  local base = "FROM fundamentals f JOIN symbols s ON s.id=f.sid WHERE s.market='us'"
  print("rows by period " .. rows_repr(q(("SELECT period, count(*) %s GROUP BY period"):format(base))))
  print("symbols " .. rows_repr(q(("SELECT count(DISTINCT sid) %s"):format(base))))
  for _, pl in ipairs({ { "Q1", 120 }, { "H1", 120 }, { "Q3", 120 }, { "FY", 200 } }) do
    local per, lim = pl[1], pl[2]
    local lags = {}
    for _, r in ipairs(q(("SELECT period_end, available %s AND f.period=?"):format(base), per)) do
      lags[#lags + 1] = md.ymd_days(r[2]) - md.ymd_days(r[1])
    end
    table.sort(lags)
    if #lags > 0 then
      local neg, over = 0, 0
      for _, x in ipairs(lags) do if x <= 0 then neg = neg + 1 end; if x > lim then over = over + 1 end end
      print(("%s lag days median %d p99 %d neg %d >%d %d"):format(per, lags[#lags // 2 + 1], lags[math.floor(#lags * .99) + 1], neg, lim, over))
    end
  end
  local r = q(("SELECT f.eps, f.net_income, f.total_shares %s AND f.eps IS NOT NULL AND f.net_income IS NOT NULL AND f.total_shares>0 AND f.currency='USD' AND abs(f.net_income)>1e7"):format(base))
  local good = 0
  for _, x in ipairs(r) do if math.abs(x[1] * x[3] - x[2]) <= 0.15 * math.abs(x[2]) then good = good + 1 end end
  print(("eps x shares ~ net income within 15%%: %d/%d"):format(good, #r))
  for _, code in ipairs({ "AAPL", "MSFT", "NVDA", "JPM", "KO", "TSLA" }) do
    print(code .. " " .. rows_repr(q(("SELECT f.period_end, f.period, f.available, f.revenue/1e6, f.net_income/1e6, f.eps, f.eps_ttm, f.total_shares/1e6, round(f.roe,2), f.dividend_ps %s AND s.code=? ORDER BY f.period_end DESC LIMIT 5"):format(base), code)))
  end
end

local function main()
  local cmd = arg[1]
  local rest = table.move(arg, 2, #arg, 1, {})
  local ap = ok.args.parser()
  local a
  if cmd == "fetch" then
    ap:opt("--raw", { required = true })
    ap:opt("--identity", { required = true })
    ap:opt("--stocks", { default = path.join(M.MD, "us", "stocks.json") })
    ap:opt("--gap", { type = "float", default = 0.17, help = "seconds between SEC requests (0.17 -> < 6/s)" })
    a = ap:parse(rest)
  elseif cmd == "load" then
    ap:opt("--raw", { required = true })
    ap:opt("--identity", { required = true })
    ap:opt("--db", { default = path.join(M.MD, "market.sqlite") })
    a = ap:parse(rest)
  elseif cmd == "validate" then
    ap:opt("--db", { default = path.join(M.MD, "market.sqlite") })
    a = ap:parse(rest)
  else
    io.stderr:write("usage: build_us_fundamentals.lua {fetch,load,validate} ...\n")
    os.exit(2)
  end
  if a.raw then
    path.mkdir(a.raw, true)
    M.LOG = path.join(a.raw, "sec.log")
  end
  if cmd == "fetch" then cmd_fetch(a) elseif cmd == "load" then cmd_load(a) else cmd_validate(a) end
end
M.main = main

if ok.is_main() then ok.main(main) end
return M
