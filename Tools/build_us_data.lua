-- Build the StreetSocial US daily market dataset (dev tool, not shipped).
--
-- Format: Docs/STREET_SOCIAL_MARKET_DATA_FORMAT.md
-- Output: Content/IndependentResources/MarketData/us/   (git-ignored)
--
-- Single data source: thetrading.tools per-symbol CSV
--   https://www.thetrading.tools/data/csv/<SYMBOL>.csv
--   columns: date,open,high,low,close,volume  (split-adjusted, NOT dividend-adjusted,
--   prices rounded to 0.01, volume split-adjusted). There is no adjusted-close column.
--   Free for personal / non-commercial research use (see https://www.thetrading.tools/terms);
--   fetched politely (few workers, small delay, generic User-Agent, no personal info).
--
-- Because the source is split-adjusted and has no corporate-action feed, the .okbar
-- price fields hold the split-adjusted prices x1000, split_ratio = 1.0 and dividend = 0
-- for every row (manifest: price_basis = split_adjusted_thetrading_tools,
-- dividends = not_modeled).
--
-- Candidate symbols (the source has no public symbol list, so we probe):
--   * NASDAQ Trader symbol directory (nasdaqlisted.txt / otherlisted.txt), filtered to
--     common stock / ADR / ETF-type issues (warrants, rights, units, preferreds, notes,
--     test issues dropped);
--   * thetrading.tools' own SEC identity file (/data/companies/identity.json), incl.
--     inactive securities;
--   * tickers removed from the S&P 500 / 400 / 600 since 2010 (Wikipedia change tables),
--     used only as candidate names to probe on thetrading.tools.
--
-- Steps (all resumable; raw cache lives outside the repo):
--   Tools\lua\lua.exe Tools/build_us_data.lua lists   # fetch candidate lists
--   Tools\lua\lua.exe Tools/build_us_data.lua fetch   # download CSVs (cached; 404s remembered)
--   Tools\lua\lua.exe Tools/build_us_data.lua pack    # write .okbar / json files
--   Tools\lua\lua.exe Tools/build_us_data.lua all
local ok = require "ok"
local json, path, kit, util = ok.json, ok.path, ok.kit, ok.util
local md = require "market_data_common"
local html = require "market_html"

local M = {}
M.OUT = path.join(ok.root, "Content/IndependentResources/MarketData/us")
M.RAW_DEFAULT = "C:/Users/Damody/AppData/Local/Temp/claude/C--portable-OpenKoikatsu/4c6d3560-751d-4631-a03d-094df739a76c/scratchpad/market_raw/us"
local UA = { ["User-Agent"] = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) market-data-research/1.0" }
local TTT_CSV = "https://www.thetrading.tools/data/csv/{}.csv"
local TTT_IDENTITY = "https://www.thetrading.tools/data/companies/identity.json"
local NASDAQ_DIR = "https://www.nasdaqtrader.com/dynamic/symdir/%s.txt"
local WIKI_IDX = "https://en.wikipedia.org/w/index.php"
-- The S&P 500 article dropped its change table in 2026; this revision (2026-02-24) still has it.
local SP500_CHANGES_REVID = 1340186477

local START = 20100101
local END = 20261009 -- latest closed NYSE session at build time (2026-10-11 is a Sunday)
local INDEX_ETFS = { "SPY", "QQQ", "DIA", "IWM" }
local INDEX_CARET = { "^GSPC", "^IXIC", "^DJI" } -- not offered by thetrading.tools; probed and recorded
local I32_MAX = 2 ^ 31 - 1

-- Traditional Chinese names for well-known companies only (others -> null).
local ZH_TW = {
  AAPL = "蘋果", MSFT = "微軟", GOOGL = "Alphabet（谷歌）A 股", GOOG = "Alphabet（谷歌）C 股",
  AMZN = "亞馬遜", META = "Meta（臉書）", NVDA = "輝達", TSLA = "特斯拉", NFLX = "網飛",
  INTC = "英特爾", AMD = "超微", QCOM = "高通", AVGO = "博通", TSM = "台積電 ADR",
  ASML = "艾司摩爾", MU = "美光", TXN = "德州儀器", IBM = "IBM", ORCL = "甲骨文",
  CSCO = "思科", ADBE = "Adobe", CRM = "Salesforce", PYPL = "PayPal", UBER = "優步",
  ABNB = "Airbnb", SBUX = "星巴克", MCD = "麥當勞", KO = "可口可樂", PEP = "百事",
  WMT = "沃爾瑪", COST = "好市多", TGT = "目標百貨", HD = "家得寶", NKE = "耐吉",
  DIS = "迪士尼", JPM = "摩根大通", BAC = "美國銀行", WFC = "富國銀行", C = "花旗",
  GS = "高盛", MS = "摩根士丹利", V = "Visa", MA = "萬事達卡", AXP = "美國運通",
  ["BRK.A"] = "波克夏 A 股", ["BRK.B"] = "波克夏 B 股", JNJ = "嬌生", PFE = "輝瑞", MRK = "默克",
  LLY = "禮來", ABBV = "艾伯維", UNH = "聯合健康", MRNA = "莫德納", XOM = "埃克森美孚",
  CVX = "雪佛龍", BA = "波音", GE = "奇異", F = "福特", GM = "通用汽車", TM = "豐田 ADR",
  SONY = "索尼 ADR", BABA = "阿里巴巴 ADR", JD = "京東 ADR", PDD = "拼多多 ADR",
  BIDU = "百度 ADR", NIO = "蔚來 ADR", T = "AT&T", VZ = "威瑞森", TMUS = "T-Mobile 美國",
  CMCSA = "康卡斯特", PG = "寶僑", UPS = "優比速", FDX = "聯邦快遞", CAT = "開拓重工",
  MMM = "3M", HON = "漢威聯合", LMT = "洛克希德馬丁", RTX = "雷神技術", SPOT = "Spotify",
  SHOP = "Shopify", SNAP = "Snap", PLTR = "Palantir", COIN = "Coinbase", ARM = "安謀 ADR",
  DELL = "戴爾", HPQ = "惠普", LULU = "露露檸檬", CMG = "Chipotle", BKNG = "Booking",
  EBAY = "eBay", ZM = "Zoom", RBLX = "Roblox", EA = "藝電", TTWO = "Take-Two",
  SPY = "標普 500 ETF", QQQ = "那斯達克 100 ETF", DIA = "道瓊工業 ETF", IWM = "羅素 2000 ETF",
}

local log = md.log
local function O(pairs_list) return json.object(pairs_list) end

local function fname(sym) return (sym:gsub("%.", "-"):gsub("/", "-")) end
M.fname = fname

-- ----------------------------------------------------------------------------------- lists
-- NONCOMMON / UNITS regexes (re.I) with frontier patterns for \b.
local function word(s, w) return s:find("%f[%w_]" .. w .. "%f[^%w_]") ~= nil end
local function noncommon(name)
  local s = name:lower()
  return word(s, "warrants?") or word(s, "rights?") or word(s, "preferred") or word(s, "pfd") or word(s, "notes?")
    or word(s, "debentures?") or word(s, "subordinated") or word(s, "baby bonds?") or s:find("%d%s*%%") ~= nil
    or word(s, "perpetual") or s:find("%f[%w_]depositary shares?,? each representing a 1/") ~= nil
end
local function is_common_or_etf(name, etf)
  if etf then return true end
  if noncommon(name) then return false end
  local s = name:lower()
  if word(s, "units?") and not (s:find("common units", 1, true) or s:find("limited partnership units", 1, true)
      or s:find("partnership units", 1, true)) then
    return false
  end
  return true
end

local KINDS = { "Common Stock", "Ordinary Shares", "Ordinary Share", "Common Shares", "Common Share" }
-- (Class [A-Z] )?(Common Stock|Ordinary Shares?|Common Shares?) at position i -> end index or nil
local function kind_at(n, i)
  if n:sub(i, i + 5) == "Class " and n:sub(i + 6, i + 6):match("%u") and n:sub(i + 7, i + 7) == " " then
    local e = kind_at(n, i + 8)
    if e then return e end
  end
  for _, k in ipairs(KINDS) do
    if n:sub(i, i + #k - 1) == k then return i + #k - 1 end
  end
  return nil
end
local function clean_name(n)
  n = util.strip(n)
  -- \s+-\s+(Class [A-Z] )?(Common Stock|Ordinary Shares?|Common Shares?).*$
  local init = 1
  while true do
    local a, b = n:find("%s+%-%s+", init)
    if not a then break end
    if kind_at(n, b + 1) then n = n:sub(1, a - 1); break end
    init = a + 1
  end
  -- \s+(Class [A-Z] )?(Common Stock|Ordinary Shares?|Common Shares?)\s*$
  init = 1
  while true do
    local a, b = n:find("%s+", init)
    if not a then break end
    local e = kind_at(n, b + 1)
    local function tail_ok(x)
      return x and n:sub(x + 1):match("^%s*$") ~= nil
    end
    -- "Shares?" may also stop before a trailing "s"
    local hit = tail_ok(e)
    if not hit and e then
      for _, k in ipairs(KINDS) do
        local p = b + 1
        if n:sub(p, p + 5) == "Class " then p = p + 8 end
        if n:sub(p, p + #k - 1) == k and tail_ok(p + #k - 1) then hit = true end
      end
    end
    if hit then n = n:sub(1, a - 1); break end
    init = a + 1
  end
  local s = util.strip(n)
  return s ~= "" and s or n
end
M.clean_name = clean_name
M.is_common_or_etf = is_common_or_etf

local function http_get(url, params, tries, timeout)
  tries = tries or 5
  for i = 0, tries - 1 do
    local r = kit.http_request(url, { params = params, headers = UA, timeout = timeout or 60 })
    local e
    if r.status == -1 then e = r.error
    elseif r.status == 429 or r.status == 500 or r.status == 502 or r.status == 503 or r.status == 504 then e = "HTTP " .. r.status end
    if not e then return r end
    if i == tries - 1 then error(e, 0) end
    local wait = 2 ^ i * 3
    log(("retry %s in %ds: %s"):format(url, math.tointeger(wait), e))
    ok.time.sleep(wait)
  end
end
M.http = http_get
local function raise_for_status(r, url)
  if r.status >= 400 then error(("%d Error for url: %s"):format(r.status, url), 0) end
end

local function wiki_removed(raw)
  local out = {}
  local pages = { { "sp500", { { "oldid", SP500_CHANGES_REVID } } }, { "sp400", { { "title", "List_of_S&P_400_companies" } } },
    { "sp600", { { "title", "List_of_S&P_600_companies" } } } }
  for _, pg in ipairs(pages) do
    local tag, params = pg[1], pg[2]
    local text = M.http(WIKI_IDX, params).body
    path.write_text(path.join(raw, "lists", "wiki_" .. tag .. ".html"), text)
    for _, t in ipairs(html.read_html(text)) do
      local cols = {}
      for i, c in ipairs(t.columns) do cols[i] = type(c) == "table" and table.concat(c, " ") or util.str(c) end
      local ri, di, si
      for i, c in ipairs(cols) do
        if not ri and c:find("Removed", 1, true) and (c:find("Ticker", 1, true) or c:find("Symbol", 1, true)) then ri = i end
        if not di and (util.startswith(c, "Date") or c:find("Effective", 1, true)) then di = i end
        if not si and c:find("Removed", 1, true) and c:find("Security", 1, true) then si = i end
      end
      if ri and di then
        for _, row in ipairs(t.rows) do
          local tick = util.strip(html.str(row[ri], t.dtypes[ri])):upper()
          if tick ~= "" and tick ~= "NAN" and #tick <= 8 and tick:match("^%u[%u%d%.%-]*$") then
            local d = html.parse_date(html.str(row[di], t.dtypes[di]))
            if d and d // 10000 >= 2010 then
              local name = si and util.strip(html.str(row[si], t.dtypes[si])) or ""
              out[#out + 1] = O({ { "symbol", (tick:gsub("%-", ".")) }, { "removed", md.iso(d) },
                { "name", name == "nan" and json.null or name }, { "list", tag } })
            end
          end
        end
      end
    end
  end
  return out
end
M.wiki_removed = wiki_removed

local function cmd_lists(raw)
  path.mkdir(path.join(raw, "lists"), true)
  for _, f in ipairs({ "nasdaqlisted", "otherlisted" }) do
    local url = NASDAQ_DIR:format(f)
    local r = M.http(url)
    raise_for_status(r, url)
    path.write_text(path.join(raw, "lists", f .. ".txt"), r.body)
  end
  local r = M.http(TTT_IDENTITY)
  raise_for_status(r, TTT_IDENTITY)
  path.write_text(path.join(raw, "lists", "ttt_identity.json"), r.body)

  local cands, order = {}, {}
  local function add(sym, kw)
    sym = util.strip(sym):upper()
    if sym == "" or sym:find("$", 1, true) or sym:find(" ", 1, true) then return end
    local c = cands[sym]
    if not c then
      c = O({ { "symbol", sym }, { "name", json.null }, { "board", json.null }, { "etf", false }, { "src", {} } })
      cands[sym] = c
      order[#order + 1] = sym
    end
    for _, kv in ipairs(kw) do
      local k, v = kv[1], kv[2]
      if k == "src" then table.insert(c.src, v)
      elseif v ~= nil and v ~= json.null and (c[k] == nil or c[k] == json.null or c[k] == false) then c[k] = v end
    end
  end

  local excluded = 0
  local lines = md.splitlines(path.read_text(path.join(raw, "lists", "nasdaqlisted.txt")))
  for k = 2, #lines do
    local ln = lines[k]
    local p = util.split(ln, "|")
    if not (#p < 8 or util.startswith(ln, "File Creation")) then
      local sym, name, test, etf, nxt = p[1], p[2], p[4], p[7], p[8]
      if not (test == "Y" or nxt == "Y") then
        if not is_common_or_etf(name, etf == "Y") then excluded = excluded + 1
        else add(sym, { { "name", clean_name(name) }, { "board", "nasdaq" }, { "etf", etf == "Y" }, { "src", "nasdaqlisted" } }) end
      end
    end
  end
  local exch = { N = "nyse", A = "amex", P = "arca", Z = "other", V = "other" }
  lines = md.splitlines(path.read_text(path.join(raw, "lists", "otherlisted.txt")))
  for k = 2, #lines do
    local ln = lines[k]
    local p = util.split(ln, "|")
    if not (#p < 8 or util.startswith(ln, "File Creation")) then
      local sym, name, ex, etf, test = p[1], p[2], p[3], p[5], p[7]
      if not (test == "Y" or sym:find("$", 1, true)) then
        local suffix = false
        for _, s in ipairs({ "W", "WS", "U", "R", "RT", "WI" }) do if util.endswith(sym, "." .. s) then suffix = true end end
        if suffix or not is_common_or_etf(name, etf == "Y") then excluded = excluded + 1
        else add(sym, { { "name", clean_name(name) }, { "board", exch[ex] or "other" }, { "etf", etf == "Y" }, { "src", "otherlisted" } }) end
      end
    end
  end
  local n_dir = #order

  local ident = ok.read_json(path.join(raw, "lists", "ttt_identity.json"))
  local exmap = { NYSE = "nyse", Nasdaq = "nasdaq", ["NYSE American"] = "amex", ["NYSE Arca"] = "arca", CBOE = "other" }
  local main_ex = util.set({ "NYSE", "Nasdaq", "NYSE American", "NYSE Arca" })
  for _, s in ipairs(ident.securities or {}) do
    local t = ((s.ticker ~= json.null and s.ticker) or ""):gsub("%-", ".")
    if t ~= "" then
      local skip = false
      if not cands[t] then
        if t:match("%.P%u*$") or t:match("%.W%u*$") or t:match("%.U$") or t:match("%.R%u*$")
            or (utf8.len(t) == 5 and ("WUR"):find(t:sub(-1), 1, true)) then
          skip = true
        elseif util.truthy(s.active) and not main_ex[s.exchange] then
          skip = true
        end
      end
      if not skip then
        local name = s.security_name
        if name == json.null then name = nil end
        local board = exmap[s.exchange] or "other"
        if not util.truthy(s.active) and not cands[t] then
          add(t, { { "name", name }, { "board", board }, { "src", "ttt_identity_inactive" } })
        else
          add(t, { { "name", name }, { "board", board }, { "src", "ttt_identity" } })
        end
      end
    end
  end
  local n_ident = #order - n_dir

  local removed = wiki_removed(raw)
  local removed_by_sym, rorder = {}, {}
  for _, r in ipairs(removed) do
    if not removed_by_sym[r.symbol] then removed_by_sym[r.symbol] = r; rorder[#rorder + 1] = r.symbol end
  end
  local n_before = #order
  for _, sym in ipairs(rorder) do
    local r = removed_by_sym[sym]
    if not cands[sym] then
      add(sym, { { "name", r.name }, { "board", "other" }, { "src", "wiki_" .. r.list .. "_removed" } })
      cands[sym].removed = r.removed
    end
  end
  for _, t in ipairs(INDEX_ETFS) do add(t, { { "src", "index_etf" } }) end
  local list = {}
  local syms = util.set_list(cands)
  table.sort(syms)
  for i, s in ipairs(syms) do list[i] = cands[s] end
  local out = O({
    { "generated_at", md.utc_iso() },
    { "counts", O({ { "nasdaq_trader_kept", n_dir }, { "nasdaq_trader_excluded_noncommon", excluded },
      { "ttt_identity_extra", n_ident }, { "wiki_removed_rows", #removed },
      { "wiki_removed_unique", #rorder }, { "wiki_removed_not_listed", #order - n_before },
      { "total", #order } }) },
    { "candidates", list },
  })
  path.write_text(path.join(raw, "candidates.json"), json.encode(out, { indent = 1, ensure_ascii = false }))
  log("candidates", util.repr(out.counts))
end
M.cmd_lists = cmd_lists

-- ----------------------------------------------------------------------------------- fetch
local function cmd_fetch(raw, workers, delay, retry, limit)
  local cdir = path.join(raw, "ttt")
  path.mkdir(cdir, true)
  local stp = path.join(raw, "fetch_status.json")
  local status = path.exists(stp) and ok.read_json(stp) or json.object()
  local cands = ok.read_json(path.join(raw, "candidates.json")).candidates
  local syms = {}
  for _, c in ipairs(cands) do syms[#syms + 1] = c.symbol end
  for _, s in ipairs(INDEX_CARET) do syms[#syms + 1] = s end
  local todo = {}
  for _, s in ipairs(syms) do
    local f = path.join(cdir, fname(s) .. ".csv")
    local st = status[s] and status[s].status
    if not (path.exists(f) and st == "ok") and not (st == "404" and not retry) then
      if st == "error" or st == nil or retry then todo[#todo + 1] = s end
    end
  end
  if limit then todo = util.slice(todo, 1, limit) end
  log(("%d candidates, %d to fetch"):format(#syms, #todo))
  local done = 0
  local function finish(sym, res)
    status[sym] = res
    done = done + 1
    if done % 200 == 0 then
      path.write_text(stp, json.encode(status, { indent = 0 }))
      local n = 0
      for _, k in ipairs(json.keys(status)) do if status[k].status == "ok" then n = n + 1 end end
      log(("%d/%d done, ok total %d"):format(done, #todo, n))
    end
  end
  -- Requests go out in curl --parallel batches of `workers` connections; each worker still waits `delay`
  -- per request on average. 429 / 5xx / network failures are retried like http_get (3, 6, 12, 24 s).
  local batch = workers * 10
  local tmpdir = path.temp(".ttt")
  path.mkdir(tmpdir, true)
  local i = 1
  while i <= #todo do
    local group = util.slice(todo, i, i + batch - 1)
    local t0 = md.utc_iso()
    local pending, attempt, results = group, 0, {}
    while #pending > 0 do
      local reqs = {}
      for k, sym in ipairs(pending) do
        reqs[k] = { url = TTT_CSV:gsub("{}", (kit.urlencode(sym):gsub("%%", "%%%%"))), out = tmpdir .. "/" .. k .. ".body", sym = sym }
      end
      local st = kit.http_batch(reqs, { parallel = workers, headers = UA, timeout = 90 })
      local again = {}
      for _, r in ipairs(reqs) do
        local code = st[r.out]
        if (code == -1 or code == 429 or (code >= 500 and code <= 504 and code ~= 501)) and attempt < 4 then
          again[#again + 1] = r.sym
        else
          results[r.sym] = { code = code, body = path.exists(r.out) and path.read_bytes(r.out) or "" }
        end
        path.remove(r.out, true)
      end
      if #again > 0 then
        local wait = 2 ^ attempt * 3
        log(("retry %d symbols in %ds"):format(#again, math.tointeger(wait)))
        ok.time.sleep(wait)
      end
      pending, attempt = again, attempt + 1
    end
    for _, sym in ipairs(group) do
      local r = results[sym]
      local res = O({ { "t", t0 } })
      if r.code == 404 then res.status = "404"
      elseif r.code == -1 then res.status = "error"; res.err = "connection failed"
      elseif r.code ~= 200 then res.status = "error"; res.err = "HTTP " .. r.code
      else
        local txt = r.body
        if not util.startswith(txt, "date,open,high,low,close,volume") then
          res.status = "error"; res.err = "unexpected header: " .. util.usub(txt, 1, 60):gsub("\n", " ")
        else
          local tmp = path.join(cdir, fname(sym) .. ".csv.part")
          path.write_text(tmp, txt)
          path.replace(tmp, path.join(cdir, fname(sym) .. ".csv"))
          res.status = "ok"; res.bytes = utf8.len(txt) or #txt
        end
      end
      finish(sym, res)
    end
    ok.time.sleep(delay * #group / workers)
    i = i + batch
  end
  path.rmtree(tmpdir)
  path.write_text(stp, json.encode(status, { indent = 0 }))
  local c, corder = {}, {}
  for _, k in ipairs(json.keys(status)) do
    local s = status[k].status
    if not c[s] then corder[#corder + 1] = s; c[s] = 0 end
    c[s] = c[s] + 1
  end
  local parts = {}
  for _, s in ipairs(corder) do parts[#parts + 1] = util.repr(s) .. ": " .. c[s] end
  log("fetch finished", "{" .. table.concat(parts, ", ") .. "}")
end

-- ------------------------------------------------------------------------------------ pack
local function pyint(s) -- int(str): optional sign, digits, surrounding whitespace, '_' between digits
  local t = util.strip(s)
  if not t:match("^[+-]?%d[%d_]*$") or t:find("__") or t:find("_$") then return nil end
  return math.tointeger(tonumber((t:gsub("_", ""))))
end
local function isfinite(x) return x == x and x ~= math.huge and x ~= -math.huge end

local function read_csv(p)
  local rows, bad = {}, 0
  local lines = md.splitlines(path.read_text(p))
  for k = 2, #lines do
    local pp = util.split(util.strip(lines[k]), ",")
    if #pp < 6 then bad = bad + 1
    else
      local d = pyint((pp[1]:gsub("-", "")))
      local o, h, l, c = md.pyfloat(pp[2]), md.pyfloat(pp[3]), md.pyfloat(pp[4]), md.pyfloat(pp[5])
      local v
      if pp[6] == "" or pp[6] == "nan" then v = 0
      else
        local fv = md.pyfloat(pp[6])
        if fv and isfinite(fv) then v = md.trunc(fv) end
      end
      if not (d and o and h and l and c and v) then bad = bad + 1
      elseif not (isfinite(o) and isfinite(h) and isfinite(l) and isfinite(c)) then bad = bad + 1
      else rows[#rows + 1] = { d, o, h, l, c, v } end
    end
  end
  table.sort(rows, function(a, b)
    for k = 1, 6 do if a[k] ~= b[k] then return a[k] < b[k] end end
    return false
  end)
  local dedup = {}
  for _, r in ipairs(rows) do
    if #dedup > 0 and dedup[#dedup][1] == r[1] then dedup[#dedup] = r; bad = bad + 1
    else dedup[#dedup + 1] = r end
  end
  return dedup, bad
end
M.read_csv = read_csv

local function p1000(x) return math.tointeger(math.floor(x * 1000 + 0.5)) end

local function encode(rows, cal_set, cal_min, is_index, stats)
  local kept = {}
  for _, r in ipairs(rows) do
    local d, o, h, l, c, v = table.unpack(r)
    if not (d < START or d > END) then
      if not cal_set[d] then stats.off_calendar = stats.off_calendar + 1
      elseif v <= 0 and not is_index then stats.zero_volume = stats.zero_volume + 1
      elseif math.min(o, h, l, c) <= 0 then stats.nonpositive = stats.nonpositive + 1
      else
        local hi, lo = math.max(o, h, l, c), math.min(o, h, l, c)
        if hi ~= h or lo ~= l then stats.ohlc_fixed = stats.ohlc_fixed + 1 end
        kept[#kept + 1] = { d, o, hi, lo, c, v }
      end
    end
  end
  if #kept == 0 then return nil, {}, {} end
  local buf = { string.pack(md.HDR, "OKBR", 1, 2, #kept, 1000) }
  local jumps = {}
  local prev_c
  for i, r in ipairs(kept) do
    local d, o, h, l, c, v = table.unpack(r)
    local vals = { p1000(o), p1000(h), p1000(l), p1000(c) }
    if math.max(table.unpack(vals)) > I32_MAX then
      error({ overflow = ("split-adjusted price %.2f on %d exceeds i32 x1000 range (serial reverse splits)"):format(math.max(o, h, l, c), d) })
    end
    local pre = prev_c ~= nil and p1000(prev_c) or vals[1]
    local flags = 0
    if i == 1 and d > cal_min and not is_index then flags = flags | 4 end -- first row in the source; treated as listing day (approximate)
    buf[#buf + 1] = md.pack_row({ d, vals[1], vals[2], vals[3], vals[4], pre, v, c * v, 1.0, 0.0, 0, 0, flags })
    if prev_c and prev_c > 0 then
      local ch = c / prev_c - 1
      if math.abs(ch) > 0.60 then jumps[#jumps + 1] = { d, util.round(prev_c, 4), util.round(c, 4), util.round(ch * 100, 1) } end
    end
    prev_c = c
  end
  return table.concat(buf), kept, jumps
end

local function cmd_pack(raw, level)
  local OUT = M.OUT
  local cdir = path.join(raw, "ttt")
  local status = ok.read_json(path.join(raw, "fetch_status.json"))
  local cj = ok.read_json(path.join(raw, "candidates.json"))
  local cands = {}
  for _, c in ipairs(cj.candidates) do cands[c.symbol] = c end

  -- calendar = SPY sessions in range (SPY trades every NYSE session)
  local spy = read_csv(path.join(cdir, "SPY.csv"))
  local cal = {}
  for _, r in ipairs(spy) do if START <= r[1] and r[1] <= END then cal[#cal + 1] = r[1] end end
  local cal_set, cal_min = util.set(cal), math.huge
  for _, d in ipairs(cal) do if d < cal_min then cal_min = d end end
  if cal[#cal] ~= END then log(("WARNING: SPY last date %d != expected %d"):format(cal[#cal], END)) end

  for _, sub in ipairs({ "daily", "index" }) do
    path.mkdir(path.join(OUT, sub), true)
    for _, f in ipairs(path.glob(path.join(OUT, sub), "*.okbar")) do path.remove(f) end -- regenerate cleanly (plain files only)
  end

  local stocks, files, failed, jumps_all, empty = {}, json.object(), json.object(), json.object(), {}
  local stats = O({ { "off_calendar", 0 }, { "zero_volume", 0 }, { "nonpositive", 0 }, { "ohlc_fixed", 0 }, { "bad_lines", 0 } })
  local delisted = { attempted = {}, found = {}, not_found = {} }
  local total_rows = 0
  local syms = util.set_list(cands)
  table.sort(syms)
  local function iso(n) return md.iso(n) end
  local zw = md.zstd_writer(level)
  for n, sym in ipairs(syms) do
    local c = cands[sym]
    local st = status[sym] and status[sym].status
    local removed = c.removed
    if removed == json.null then removed = nil end
    local inactive = util.contains_value(c.src, "ttt_identity_inactive")
    local is_del_cand = util.truthy(removed) or inactive
    if is_del_cand then table.insert(delisted.attempted, sym) end
    local cname = c.name ~= json.null and c.name or nil
    if st ~= "ok" then
      local reason
      if st == "404" then reason = "not on thetrading.tools (404)"
      else reason = (status[sym] and status[sym].err) or "not fetched" end
      failed[sym] = O({ { "reason", reason }, { "src", c.src } })
      if is_del_cand then table.insert(delisted.not_found, O({ { "symbol", sym }, { "removed", removed or json.null }, { "name", cname or json.null } })) end
    else
      local rows, bad = read_csv(path.join(cdir, fname(sym) .. ".csv"))
      stats.bad_lines = stats.bad_lines + bad
      local is_idx = util.contains_value(INDEX_ETFS, sym)
      local okv, blob, kept, jumps = pcall(encode, rows, cal_set, cal_min, is_idx, stats)
      if not okv then
        if type(blob) == "table" and blob.overflow then failed[sym] = O({ { "reason", blob.overflow }, { "src", c.src } })
        else error(blob, 0) end
      elseif blob == nil then
        empty[#empty + 1] = sym
        failed[sym] = O({ { "reason", ("no rows in %d..%d"):format(START, END) }, { "src", c.src } })
      else
        local first, last = kept[1][1], kept[#kept][1]
        local reused = false
        if is_del_cand and removed then
          -- reused-ticker guard: data must exist before the index removal date
          local rd = math.tointeger(tonumber((removed:gsub("-", ""))))
          if first >= rd then
            failed[sym] = O({ { "reason", ("data starts %d, after index removal %s: ticker likely reused"):format(first, removed) }, { "src", c.src } })
            table.insert(delisted.not_found, O({ { "symbol", sym }, { "removed", removed }, { "name", cname or json.null }, { "note", "ticker reused" } }))
            reused = true
          end
        end
        if not reused then
          local sub = is_idx and "index" or "daily"
          local rel = sub .. "/" .. fname(sym) .. ".okbar"
          local entry = O({ { "rows", #kept }, { "first", tostring(first) }, { "last", tostring(last) },
            { "sha256", "" }, { "bytes", 0 } })
          files[rel] = entry
          zw:add(path.join(OUT, rel), blob, function(info) entry.sha256 = info.sha256; entry.bytes = info.bytes end)
          total_rows = total_rows + #kept
          if #jumps > 0 then jumps_all[sym] = jumps end
          local stale = last < cal[#cal - 5] -- not trading in the last week of the period
          local delist_date = stale and iso(last) or json.null
          if is_del_cand then table.insert(delisted.found, O({ { "symbol", sym }, { "removed", removed or json.null }, { "first", tostring(first) }, { "last", tostring(last) } })) end
          local fd = iso(first)
          stocks[#stocks + 1] = O({
            { "code", fname(sym) }, { "symbol", sym }, { "name", cname and cname ~= "" and cname or sym },
            { "name_zh_tw", ZH_TW[sym] or json.null }, { "board", util.truthy(c.board) and c.board or "other" },
            { "list_date", first > cal[1] and fd or json.null }, { "delist_date", delist_date },
            { "is_index", is_idx }, { "etf", util.truthy(c.etf) or is_idx }, { "st_ranges", {} },
            { "rows", #kept }, { "first", tostring(first) }, { "last", tostring(last) },
          })
        end
      end
    end
    if n % 1000 == 0 then log(("packed %d/%d"):format(n, #syms)) end
  end
  zw:close()
  for _, sym in ipairs(INDEX_CARET) do
    if not (status[sym] and status[sym].status == "ok") then
      failed[sym] = O({ { "reason", "index level series not offered by thetrading.tools (404)" }, { "src", { "index" } } })
    end
  end

  local cal_iso = {}
  for i, d in ipairs(cal) do cal_iso[i] = iso(d) end
  path.write_text(path.join(OUT, "calendar.json"), json.encode(cal_iso))
  path.write_text(path.join(OUT, "stocks.json"), json.encode(stocks, { ensure_ascii = false, indent = 0 }))

  local n_daily, n_files = 0, 0
  for _, k in ipairs(json.keys(files)) do
    n_files = n_files + 1
    if util.startswith(k, "daily/") then n_daily = n_daily + 1 end
  end
  local n_failed = #json.keys(failed)
  local n_jumps = #json.keys(jumps_all)
  local n_del_found = #delisted.found
  local manifest = O({
    { "format", "STREET_SOCIAL_MARKET_DATA_FORMAT.md okbar v1" },
    { "market", "us" }, { "version", 1 },
    { "generated_at", md.utc_iso() },
    { "period", O({ { "start", cal_iso[1] }, { "end", cal_iso[#cal_iso] }, { "sessions", #cal } }) },
    { "source", O({
      { "name", "thetrading.tools per-symbol daily CSV" },
      { "url_template", TTT_CSV },
      { "terms", "https://www.thetrading.tools/terms (personal, non-commercial research use; do not redistribute)" },
      { "columns_used", "date,open,high,low,close,volume -> open/high/low/close/volume" },
      { "adjusted_close_column", "none provided; 'close' is split-adjusted but NOT dividend-adjusted (e.g. AAPL 2010-01-04 close 7.64 = raw 214.01 / 28)" },
      { "precision", "prices published rounded to 0.01 USD (split-adjusted), so old prices of heavily split stocks are coarse (NVDA 2010 ~0.46 with 0.01 ticks)" },
      { "volume", "split-adjusted share volume" },
    }) },
    { "price_basis", "split_adjusted_thetrading_tools" },
    { "dividends", "not_modeled" },
    { "split_ratio", "always 1.0 (source has no corporate-action data)" },
    { "amount", "close x volume estimate (split-adjusted close x split-adjusted volume = USD turnover)" },
    { "preclose", "previous row's close (first row: own open)" },
    { "flags", "bit2 set on a symbol's first row when it starts after the first calendar day (approximate listing day; may also be a ticker change). bit3 never set (single source)." },
    { "calendar", "NYSE sessions taken from SPY rows in the period" },
    { "row_rules", "rows outside the calendar, with volume 0 (non-index) or non-positive prices are dropped (split-adjusted sub-cent prices are published as 0.00 by the source); high/low widened to contain open/close when inconsistent" },
    { "index", O({ { "etfs", INDEX_ETFS }, { "levels_missing", INDEX_CARET },
      { "note", "^GSPC/^IXIC/^DJI not available from thetrading.tools; SPY/QQQ/DIA/IWM are stored only in index/" } }) },
    { "universe", O({
      { "candidates", cj.counts },
      { "packed_daily", n_daily }, { "packed_index", n_files - n_daily },
      { "failed_count", n_failed },
      { "filters", "NASDAQ Trader directory minus test issues, NextShares, warrants, rights, units (except LP common units), preferreds/$ symbols, notes/debentures; ETFs kept; ADRs kept" },
    }) },
    { "survivorship", O({
      { "note", "thetrading.tools covers ~5,500 currently listed US equities plus a few inactive ones. Stocks delisted/acquired since 2010 are mostly absent, so backtests over this universe are survivorship-biased (winners over-represented)." },
      { "delisted_candidates_attempted", #delisted.attempted },
      { "delisted_found", n_del_found },
      { "delisted_found_list", delisted.found },
      { "delisted_not_found", delisted.not_found },
    }) },
    { "row_stats", stats }, { "total_rows", total_rows },
    { "extreme_daily_moves_gt60pct", O({ { "symbols", n_jumps },
      { "note", "|close/prev_close-1|>60%; may be real events or source errors (unadjusted reverse splits etc.); listed for review" },
      { "by_symbol", jumps_all } }) },
    { "failed", failed },
    { "files", files },
  })
  path.write_text(path.join(OUT, "manifest.json"), json.encode(manifest, { ensure_ascii = false, indent = 1 }))
  log(("packed daily=%d index=%d rows=%d failed=%d delisted found %d/%d"):format(n_daily, n_files - n_daily, total_rows,
    n_failed, n_del_found, #delisted.attempted))
end
M.cmd_pack = cmd_pack

local function main()
  local ap = ok.args.parser("Build the StreetSocial US daily market dataset")
  ap:arg("step", { choices = { "lists", "fetch", "pack", "all" } })
  ap:opt("--raw", { default = M.RAW_DEFAULT, help = "raw cache directory" })
  ap:opt("--workers", { type = "int", default = 3 })
  ap:opt("--delay", { type = "float", default = 0.25, help = "seconds each worker sleeps per request" })
  ap:flag("--retry", { help = "re-request symbols previously 404/error" })
  ap:opt("--limit", { type = "int" })
  ap:opt("--zstd-level", { type = "int", default = 19 })
  local a = ap:parse(arg)
  path.mkdir(a.raw, true)
  if a.step == "lists" or a.step == "all" then cmd_lists(a.raw) end
  if a.step == "fetch" or a.step == "all" then cmd_fetch(a.raw, a.workers, a.delay, a.retry, a.limit) end
  if a.step == "pack" or a.step == "all" then cmd_pack(a.raw, a.zstd_level) end
end
M.main = main

if ok.is_main() then ok.main(main) end
return M
