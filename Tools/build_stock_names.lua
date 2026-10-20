-- Point-in-time security names / tickers -> market.sqlite table `names`.
--
-- The game must never show a name or ticker that did not exist yet on the game date (2012: "Google Inc." / GOOG,
-- not "Alphabet Inc." / GOOGL; no "*ST" before the stock was actually put under risk warning; old 简称 before a
-- rename). Table (created by `load`):
--
--     names(sid, start YYYYMMDD, name, symbol, source, PRIMARY KEY(sid, start)) WITHOUT ROWID
--
-- A row applies from `start` until the next row's `start` for the same sid. `symbol` NULL = symbols.symbol.
-- cn names are Traditional Chinese (OpenCC s2tw, the same conversion as symbols.name_zh_tw).
--
-- Sources (no account, no API key, no contact info; neutral User-Agent):
--   cn / Shenzhen : SZSE 名称变更 report, tab2 简称变更 (www.szse.cn/api/report/ShowReport CATALOGID=SSGSGMXX, xlsx;
--                   every 简称 change since 1994 with its date, including ST/*ST/N/C/退 changes).
--   cn / Shanghai + Beijing : East Money announcement index (np-anotice-stock.eastmoney.com). Every announcement title
--                   starts with the security's 简称 *at publication time* ("*ST夏新：…"), so the titles are dated name
--                   samples. Exact rename dates come from the text of 证券简称变更 announcements
--                   (np-cnotice-stock.eastmoney.com, else the PDF on pdf.dfcfw.com; "证券简称变更日期：2026年7月28日"). ST periods
--                   come from bars.flags bit1 (BaoStock isST); which prefix (ST or *ST) from the title samples.
--                   BSE pre-2024 listings: old NEEQ code (43/83/87xxxx) from the code printed in an old announcement,
--                   switched to 920xxx on 2025-10-09 (6 pilot stocks 2025-05-06).
--   us names      : SEC submissions formerNames (cached data.sec.gov JSON, from/to dates).
--   us tickers    : data.sec.gov submissions filing lists: inline-XBRL / Workiva document names start with the
--                   ticker at filing time ("fb-20220331.htm" -> "meta-20220630.htm"); Nasdaq symbol-change-history
--                   JSON (last ~12 months); a small curated table for share-class / successor-issuer cases the filing
--                   names cannot show (GOOG/GOOGL 2014, Google->Alphabet 2015, ...).
--
-- Steps
--     Tools\lua\lua.exe Tools/build_stock_names.lua fetch    --raw <dir> --db <market.sqlite> [--market cn|us|all]
--     Tools\lua\lua.exe Tools/build_stock_names.lua load     --raw <dir> --db <market.sqlite>
--     Tools\lua\lua.exe Tools/build_stock_names.lua validate --db <market.sqlite>
--
-- `fetch` only reads the database (symbol list, ST flags) and is resumable (cached files are skipped).
-- `load` deletes and refills the whole `names` table and writes meta `manifest.names`; it refuses to run while
-- <db>.building exists.
local ok = require "ok"
local json, path, kit, util = ok.json, ok.path, ok.kit, ok.util
local md = require "market_data_common"
local re = require "market_regex"

local M = {}
local UA_BROWSER = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36"
local UA_TOOL = "OpenKoikatsu-market-data/1.0 (game research)"
local START = 20100104
local END = 20261009
local LOG = nil
local O = json.object
local NULL = json.null

local function log(msg)
  local line = os.date("%Y-%m-%d %H:%M:%S ") .. msg
  print(line); io.stdout:flush()
  if LOG then path.append_text(LOG, line .. "\n", { newline = "\n" }) end
end

local function nz(v) if v == nil or v == NULL then return nil end return v end
local ulen = utf8.len
local function usub(s, i, j) return util.usub(s, i, j) end
local function contains(s, sub) return s:find(sub, 1, true) ~= nil end

-- Python repr of tuples / lists / ints / strings / None
local function repr(v)
  if type(v) == "table" and v.tuple then
    local parts = {}
    for k = 1, v.n do parts[k] = repr(v[k]) end
    return "(" .. table.concat(parts, ", ") .. (v.n == 1 and ",)" or ")")
  end
  if type(v) == "table" and v ~= NULL and not json.is_object(v) then
    local parts = {}
    for k = 1, #v do parts[k] = repr(v[k]) end
    return "[" .. table.concat(parts, ", ") .. "]"
  end
  if v == NULL then return "None" end
  return util.repr(v)
end
local function tuple(...) local t = table.pack(...); t.tuple = true; return t end

-- ----------------------------------------------------------------------------------------------- http
local Http = {}
Http.__index = Http
function M.new_http(gaps) return setmetatable({ gaps = gaps, next_ok = {} }, Http) end
function Http:get(url, ua, headers, tries, binary)
  ua, tries = ua or UA_BROWSER, tries or 6
  local host = url:match("^%a+://([^/:]+)") or ""
  local gap = self.gaps[host] or 0.35
  local last
  for attempt = 0, tries - 1 do
    local now = ok.time.time()
    local nxt = self.next_ok[host] or 0.0
    local wait = math.max(0.0, nxt - now)
    self.next_ok[host] = math.max(now, nxt) + gap
    if wait > 0 then ok.time.sleep(wait) end
    local h = { ["User-Agent"] = ua, ["Accept"] = "*/*" }
    for k, v in pairs(headers or {}) do h[k] = v end
    local r = kit.http_request(url, { headers = h, timeout = 60, compressed = false })
    if r.status == 200 or (r.status > 0 and r.status < 400) then
      local data = r.body
      if data:sub(1, 2) == "\x1f\x8b" then data = kit.gzip_decompress_bytes(data) end
      return data
    end
    last = r.status == -1 and (r.error or "connection failed") or ("HTTP Error " .. r.status)
    if r.status == 400 or r.status == 403 or r.status == 404 or r.status == 410 or r.status == 567 then return nil end -- 567: East Money 'no such article'
    local delay = math.min(120, 3 * 2 ^ attempt)
    log(("  retry %d/%d %s: %s; sleep %ds"):format(attempt + 1, tries, url:sub(1, 120), last, math.tointeger(delay)))
    ok.time.sleep(delay)
  end
  error(("GET failed: %s: %s"):format(url, tostring(last)), 0)
end

local function write_atomic(p, data)
  path.mkdir(path.parent(p), true)
  local tmp = p .. ".tmp"
  path.write_bytes(tmp, data) -- JSON texts here carry no raw newlines, so text vs binary mode is the same
  path.replace(tmp, p)
end

-- "2026-07-28..." / int -> int YYYYMMDD
local function ymd(s)
  if s == nil or s == NULL then return nil end
  if math.type(s) == "integer" then return s end
  s = tostring(s)
  if #s >= 10 and (s:sub(5, 5) == "-" or s:sub(5, 5) == "/") then
    return math.tointeger(tonumber(s:sub(1, 4) .. s:sub(6, 7) .. s:sub(9, 10)))
  end
  return math.tointeger(tonumber(s:sub(1, 8)))
end
local function d2s(d) d = tostring(d); return d:sub(1, 4) .. "-" .. d:sub(5, 6) .. "-" .. d:sub(7, 8) end
local function add_days(d, n) return md.add_days(d, n) end

local function ro(db) return "file:" .. path.norm(db) .. "?mode=ro" end
local function q(db, sql, params) return kit.sql_rows(ro(db), sql, params or {}, { readonly = true }) end

-- All ST runs (consecutive bars with flags bit1) of the cn symbols: sid -> {{first, last}, ...}
local function all_st_runs(db)
  local out = {}
  for _, r in ipairs(q(db, [[SELECT sid, MIN(day), MAX(day) FROM (
      SELECT sid, day, flags & 2 AS f,
        ROW_NUMBER() OVER (PARTITION BY sid ORDER BY day) - ROW_NUMBER() OVER (PARTITION BY sid, (flags & 2) ORDER BY day) AS g
      FROM bars WHERE sid IN (SELECT id FROM symbols WHERE market='cn')) WHERE f != 0 GROUP BY sid, g ORDER BY sid, MIN(day)]])) do
    out[r[1]] = out[r[1]] or {}
    table.insert(out[r[1]], { r[2], r[3] })
  end
  return out
end

-- ------------------------------------------------------------------------------------------- cn names
local function norm_cn(name)
  if name == nil or name == NULL then return nil end
  local s = type(name) == "string" and name or util.str(name)
  local out = {}
  for _, c in utf8.codes(s, true) do
    if c >= 0xFF01 and c <= 0xFF5E then c = c - 0xFF01 + 0x21 end
    if c ~= 0x3000 and c ~= 32 then out[#out + 1] = utf8.char(c) end
  end
  s = table.concat(out)
  local cps, n = re.decode(s)
  local a, b = 1, n
  while a <= b and re.is_space(cps[a]) do a = a + 1 end
  while b >= a and re.is_space(cps[b]) do b = b - 1 end
  return re.encode(cps, a, b)
end
M.norm_cn = norm_cn

local CJK = "[\u{4e00}-\u{9fff}]"
local function split_st(name)
  local m = re.match("^(S)(?=\\*ST|ST|" .. CJK .. ")(.*)$", name)
  local s, rest
  if m and not util.startswith(name, "ST") then s, rest = "S", m[2] else s, rest = "", name end
  local m2 = re.match("^(\\*ST|ST)(.+)$", rest)
  if m2 then return s, m2[1], m2[2] end
  return s, "", rest
end
local function strip_ipo(name)
  local m = re.match("^[NC](?=" .. CJK .. ")(.+)$", name)
  return m and m[1] or name
end

local BAD_PREFIX = { "关于", "上交所", "深交所", "北交所", "证监会", "交易所", "公告", "董事会", "监事会", "独立董事", "中国", "上海证券交易所", "深圳证券交易所",
  "北京证券交易所", "证券", "股份", "公司" }
local function bad_prefix(p) for _, b in ipairs(BAD_PREFIX) do if util.startswith(p, b) then return true end end return false end

local function plausible(p)
  if not util.truthy(p) then return false end
  local n = ulen(p)
  return 2 <= n and n <= 10 and not re.search("\\d|CWB|JTP|JTB|权证", p)
end

local function title_name(title, cands)
  local t = norm_cn(title or "")
  local m = re.match("^(\\d{6})[:：](.*)$", t)
  if m then
    local rest = m[2]
    local m2 = re.match("^([^:：，,（(]{2,12}?)[:：]", rest)
    if m2 and plausible(m2[1]) and not bad_prefix(m2[1]) and not re.search("(有限公司|集团股份|年度|公告|报告|H股)", m2[1]) then
      return m2[1]
    end
    local pf = re.match("^(S\\*ST|SST|\\*ST|ST|S|N|C)?", rest)[0] or ""
    local best
    for _, c in ipairs(cands or {}) do
      local variants = pf ~= "" and { pf, "" } or { "" }
      for _, p_ in ipairs(variants) do
        if util.startswith(rest:sub(#p_ + 1), c) and (best == nil or ulen(c) + ulen(p_) > ulen(best)) then best = p_ .. c end
      end
    end
    return best
  end
  m = re.match("^\\s*([^：:，,（(\\s]{2,12}?)\\s*[：:]", t)
  if not m then return nil end
  local p = norm_cn(m[1])
  if not plausible(p) then return nil end
  if bad_prefix(p) or util.endswith(p, "B") or util.endswith(p, "B股") or ulen(p) > 10 then return nil end
  if re.search("(有限公司|集团股份|年度|公告|报告)", p) then return nil end
  return p
end
M.title_name = title_name

local function notice_day(it) return ymd(util.truthy(it.d) and it.d or it.n) end

-- All cached East Money notices of one stock (deduplicated by art_code).
local function em_samples(cache_dir, code)
  local files = path.glob(path.join(cache_dir, "em"), code .. "_*.json")
  table.sort(files, function(a, b) return a:lower() < b:lower() end)
  local seen, out = {}, {}
  local seen_none = false
  for _, p in ipairs(files) do
    local okv, d = pcall(ok.read_json, p)
    if okv then
      for _, it in ipairs(d.items or {}) do
        local a = nz(it.a)
        local dup = (a == nil and seen_none) or (a ~= nil and seen[a])
        if not dup then
          if a == nil then seen_none = true else seen[a] = true end
          out[#out + 1] = it
        end
      end
    end
  end
  return util.sorted(out, function(it) return util.truthy(it.d) and it.d or it.n end)
end

local DATE_PATTERNS = {
  "(?:证券简称|股票简称|简称)[^。；\\n]{0,30}?(?:变更日期|变更实施日期|启用日期|生效日期)[为：:\\s]*(\\d{4})\\s*年\\s*(\\d{1,2})\\s*月\\s*(\\d{1,2})\\s*日",
  "(?:自|从|于)\\s*(\\d{4})\\s*年\\s*(\\d{1,2})\\s*月\\s*(\\d{1,2})\\s*日\\s*(?:起|开始)[^。]{0,60}?(?:证券简称|股票简称|简称)",
  "(?:证券简称|股票简称|简称)[^。]{0,80}?(?:自|从|于)\\s*(\\d{4})\\s*年\\s*(\\d{1,2})\\s*月\\s*(\\d{1,2})\\s*日\\s*(?:起|开始)",
  "变更日期[为：:\\s]*(\\d{4})\\s*年\\s*(\\d{1,2})\\s*月\\s*(\\d{1,2})\\s*日",
}
local NEWNAME_RE = "(?:变更后的?(?:股票|证券)?(?:证券)?简称|(?:证券|股票)简称(?:将)?(?:变更|更)为)[为：:\\s“\"]*([^\\s，,。；;”\"）)]{2,10})"

local function pyint_digits(s) -- int() of a \d+ match (ASCII or full-width digits)
  local out = {}
  for _, c in utf8.codes(s) do
    if c >= 0xFF10 and c <= 0xFF19 then c = c - 0xFF10 + 48 end
    if c >= 0x660 and c <= 0x669 then c = c - 0x660 + 48 end
    if c >= 0x6F0 and c <= 0x6F9 then c = c - 0x6F0 + 48 end
    if c >= 0x966 and c <= 0x96F then c = c - 0x966 + 48 end
    out[#out + 1] = string.char(c)
  end
  return math.tointeger(tonumber(table.concat(out)))
end
local function valid_date(y, m, d)
  if not (y and m and d) or y < 1 or y > 9999 or m < 1 or m > 12 or d < 1 then return nil end
  local mdays = { 31, (y % 4 == 0 and (y % 100 ~= 0 or y % 400 == 0)) and 29 or 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }
  if d > mdays[m] then return nil end
  return y * 10000 + m * 100 + d
end

-- -> (yyyymmdd or nil, new short name or nil) from a 证券简称变更 announcement.
local function parse_rename_text(txt)
  if not util.truthy(txt) then return nil, nil end
  local t = re.sub("[ \\t\\r　]+", "", txt)
  local cps = re.decode(t)
  local day
  for _, pat in ipairs(DATE_PATTERNS) do
    local m = re.search(pat, cps)
    if m then
      day = valid_date(pyint_digits(m[1]), pyint_digits(m[2]), pyint_digits(m[3]))
      if day then break end
    end
  end
  local m = re.search(NEWNAME_RE, cps)
  local nn = m and norm_cn(m[1]) or nil
  return day, nn
end
M.parse_rename_text = parse_rename_text

local function is_rename_notice(it)
  local t = it.t
  local in_cols = false
  for _, c in ipairs(it.c or {}) do if c == "证券简称变更" then in_cols = true end end
  return (in_cols or (contains(t, "简称") and (contains(t, "变更") or contains(t, "更名")))) and not contains(t, "扩位")
end

local function cal_next(cal, d)
  for _, x in ipairs(cal) do if x > d then return x end end
  return add_days(d, 1)
end

-- Name timeline of one SH/BSE stock from title samples + exact dates from rename announcements.
local Timeline = {}
Timeline.__index = Timeline
local function base(name) local s, _, rest = split_st(name); return s .. rest end
Timeline.base = base

local function new_timeline(sym, items, contents, st, extra, cal)
  local self = setmetatable({ sym = sym, items = items, contents = contents, st = st }, Timeline)
  local eset = {}
  for _, ab in ipairs(st) do eset[ab[1]] = true; eset[cal and cal_next(cal, ab[2]) or add_days(ab[2], 1)] = true end
  self.edges = util.set_list(eset)
  table.sort(self.edges)
  self.extra, self.seq = {}, {}
  for _, x in ipairs(extra or {}) do
    if util.truthy(x) then
      self.extra[base(strip_ipo(norm_cn(x)))] = true
      self.seq[#self.seq + 1] = norm_cn(x) -- EM FORMERNAME order + current 简称
    end
  end
  self.last_seg_day = nil
  self.st_only = {}
  self.notice_based = 0
  return self
end

function Timeline:samples()
  if self._smp then return self._smp end
  local cands = {}
  for c in pairs(self.extra) do cands[c] = true end
  for _, it in ipairs(self.items) do
    local nm = title_name(it.t)
    if nm and not re.match("^\\d", util.strip(it.t)) then cands[base(strip_ipo(nm))] = true end
  end
  local clist = {}
  for c in pairs(cands) do if plausible(c) then clist[#clist + 1] = c end end
  table.sort(clist)
  local out = {}
  for _, it in ipairs(self.items) do
    local nm = title_name(it.t, clist)
    if nm then out[#out + 1] = { notice_day(it), strip_ipo(nm), it } end
  end
  -- drop isolated outliers: one sample whose base differs from equal neighbours on both sides
  local b = {}
  for i, s in ipairs(out) do b[i] = base(s[2]) end
  local keep = {}
  for i, s in ipairs(out) do
    if not (i > 1 and i < #out and b[i] ~= b[i - 1] and b[i - 1] == b[i + 1]) then keep[#keep + 1] = s end
  end
  self._smp = keep
  return keep
end

-- {(start_day, base_name, exact_bool)} (start of the first one is the first sample day), need
function Timeline:segments()
  local smp = self:samples()
  if #smp == 0 then return {}, {} end
  local segs = {} -- {base, first_day, last_day, st_only}
  for _, x in ipairs(smp) do
    local day, nm = x[1], x[2]
    local b = base(nm)
    local stp = select(2, split_st(nm)) ~= ""
    if #segs > 0 and segs[#segs][1] == b then
      segs[#segs][3] = day
      segs[#segs][4] = segs[#segs][4] and stp
    else
      segs[#segs + 1] = { b, day, day, stp }
    end
  end
  local only, notonly = {}, {}
  for _, x in ipairs(segs) do if x[4] then only[x[1]] = true else notonly[x[1]] = true end end
  self.st_only = {}
  for k in pairs(only) do if not notonly[k] then self.st_only[k] = true end end
  self.last_seg_day = segs[#segs][3]
  local out = { { segs[1][2], segs[1][1], false } }
  local need = {}
  for k = 2, #segs do
    local prev, cur = segs[k - 1], segs[k]
    local lo, hi = prev[3], cur[2] -- old name seen on lo, new name seen on hi
    local exact
    -- ST start / end inside the window: the 简称 changed with the risk-warning flag
    for _, e in ipairs(self.edges) do
      if lo < e and e <= add_days(hi, 3) and (self.st_only[cur[1]] or self.st_only[prev[1]]) then exact = e end
    end
    for _, ce in ipairs(self.contents.order) do
      local cday, cname = ce[2], ce[3]
      if exact == nil and cday and lo <= cday and cday <= add_days(hi, 3) and (cname == nil or base(strip_ipo(cname)) == cur[1]
          or contains(cur[1], cname) or contains(cname, cur[1])) then
        exact = cday
      end
    end
    local done = false
    if exact == nil then
      -- announcement of the rename inside the window but no readable date: implementation notices come
      -- 1-10 days before the switch -> notice day + 5 calendar days (capped at the first new title)
      local rn = {}
      for _, it in ipairs(self.items) do
        local nd = notice_day(it)
        if is_rename_notice(it) and lo <= nd and nd <= hi then rn[#rn + 1] = nd end
      end
      if #rn > 0 and hi - lo > 5 then
        local mx = rn[1]
        for _, x in ipairs(rn) do if x > mx then mx = x end end
        out[#out + 1] = { math.min(hi, add_days(mx, 5)), cur[1], false }
        self.notice_based = self.notice_based + 1
        need[#need + 1] = { lo, hi, cur[1] }
        done = true
      end
    end
    if not done then
      if exact ~= nil then out[#out + 1] = { exact, cur[1], true }
      else
        out[#out + 1] = { hi, cur[1], false }
        need[#need + 1] = { lo, hi, cur[1] }
      end
    end
  end
  return out, need
end

-- Contiguous runs of ST/*ST names in the EM former-name sequence (oldest first).
function Timeline:st_groups()
  local groups, cur = {}, {}
  for _, n in ipairs(self.seq) do
    if select(2, split_st(n)) ~= "" then
      if #cur == 0 or cur[#cur] ~= n then cur[#cur + 1] = n end
    elseif #cur > 0 then
      groups[#groups + 1] = cur
      cur = {}
    end
  end
  if #cur > 0 then groups[#groups + 1] = cur end
  return groups
end

function Timeline:st_prefix_runs()
  local smp = self:samples()
  local groups = self:st_groups()
  local gmap = {}
  for k = 1, math.min(#groups, #self.st) do
    local run = self.st[#self.st - k + 1]
    gmap[run[1] .. ":" .. run[2]] = groups[#groups - k + 1]
  end
  local out = {}
  for _, ab in ipairs(self.st) do
    local a, b = ab[1], ab[2]
    local inside = {}
    for _, x in ipairs(smp) do
      if a <= x[1] and x[1] <= b and select(2, split_st(x[2])) ~= "" then inside[#inside + 1] = { x[1], x[2] } end
    end
    local g = gmap[a .. ":" .. b]
    if #inside == 0 then
      if g and #g > 0 then out[#out + 1] = { a, b, g[1], "flags+former-names" }
      else out[#out + 1] = { a, b, nil, "flags-default" } end
    else
      local cur_n, cur_a = inside[1][2], a
      for _, x in ipairs(inside) do
        if x[2] ~= cur_n then
          out[#out + 1] = { cur_a, add_days(x[1], -1), cur_n, "flags+titles" }
          cur_n, cur_a = x[2], x[1]
        end
      end
      out[#out + 1] = { cur_a, b, cur_n, "flags+titles" }
    end
  end
  return out
end

local function survey_names(cache, code)
  local fp = path.join(cache, "survey", code .. ".json")
  if not path.exists(fp) then return {} end
  local d = ok.read_json(fp)
  local out = {}
  for _, x in ipairs(re.split("[→>\\-]+", nz(d.former) or "")) do if x ~= "" then out[#out + 1] = x end end
  if util.truthy(nz(d.abbr)) then out[#out + 1] = d.abbr end
  return out
end

-- art -> (day, name) for the cached announcement texts of `items` (ordered like the Python dict)
local content_cache = {}
local function load_contents(cache, items)
  local out = { order = {}, by = {} }
  for _, it in ipairs(items) do
    local a = nz(it.a)
    if a ~= nil then
      local fp = path.join(cache, "content", a .. ".json")
      local entry = content_cache[fp]
      if entry == nil then
        entry = false
        if path.exists(fp) then
          local okv, j = pcall(ok.read_json, fp)
          if okv then
            local okp, day, nn = pcall(parse_rename_text, nz(j.text))
            if okp then entry = { day, nn } end
          end
        end
        content_cache[fp] = entry
      end
      if entry then
        if not out.by[a] then out.order[#out.order + 1] = { a, entry[1], entry[2] } end
        out.by[a] = entry
      end
    end
  end
  return out
end

-- SZSE tab2 xlsx -> code -> {{day, before, after}, ...}
local function read_xlsx_rows(p)
  local z = kit.call({ "zip-read", p, "--suffix", ".xml" })
  local texts = {}
  for _, t in ipairs(z.texts) do texts[t.name] = t.text end
  local html = require "market_html"
  local shared = {}
  if texts["xl/sharedStrings.xml"] then
    for si in texts["xl/sharedStrings.xml"]:gmatch("<si>(.-)</si>") do
      local parts = {}
      for tt in si:gmatch("<t[^>]*>(.-)</t>") do parts[#parts + 1] = html.unescape(tt) end
      shared[#shared + 1] = table.concat(parts)
    end
  end
  local sheet = texts["xl/worksheets/sheet1.xml"] or error("xlsx: no sheet1")
  local rows = {}
  for rattr, body in sheet:gmatch("<row([^>]*)>(.-)</row>") do
    local row = {}
    for cattr, cbody in body:gmatch("<c([^>]*)>(.-)</c>") do
      local ref = cattr:match('r="(%u+)%d+"')
      local col = 0
      for ch in ref:gmatch(".") do col = col * 26 + (ch:byte() - 64) end
      local t = cattr:match('t="([^"]*)"')
      local v
      if t == "inlineStr" then
        local parts = {}
        for tt in cbody:gmatch("<t[^>]*>(.-)</t>") do parts[#parts + 1] = html.unescape(tt) end
        v = table.concat(parts)
      elseif t == "s" then
        v = shared[(tonumber(cbody:match("<v>(.-)</v>")) or 0) + 1]
      else
        local raw = cbody:match("<v>(.-)</v>")
        if raw then
          local num = tonumber(raw)
          v = num and (math.tointeger(num) and tostring(math.tointeger(num)) or util.num(num + 0.0)) or html.unescape(raw)
        end
      end
      row[col] = v
    end
    rows[#rows + 1] = row
  end
  return rows
end
M.read_xlsx_rows = read_xlsx_rows

local function szse_events(cache)
  local rows = read_xlsx_rows(path.join(cache, "szse_namechange_tab2.xlsx"))
  local header = rows[1]
  local col = {}
  for k, v in pairs(header) do col[v] = k end
  local ev, order = {}, {}
  local function cell(r, name) local v = r[col[name]]; if v == nil then return "nan" end return v end
  for k = 2, #rows do
    local r = rows[k]
    local code = util.strip(cell(r, "证券代码"))
    code = string.rep("0", 6 - ulen(code)) .. code
    if not ev[code] then ev[code] = {}; order[#order + 1] = code end
    table.insert(ev[code], { ymd(usub(cell(r, "变更日期"), 1, 10)), norm_cn(cell(r, "变更前简称")), norm_cn(cell(r, "变更后简称")) })
  end
  for _, v in pairs(ev) do
    local sorted = util.sorted(v, function(x) return x[1] end)
    for i = 1, #v do v[i] = sorted[i] end
  end
  return ev
end

local function ipo_prefix_days(sym, cal)
  local ld = sym.list_date
  if not util.truthy(ld) or ld < START or sym.first ~= ld then return {} end
  local days = {}
  for _, d in ipairs(cal) do if d >= ld then days[#days + 1] = d; if #days == 5 then break end end end
  local out = #days > 0 and { { days[1], "N" } } or {}
  local code = sym.symbol
  local reg = (util.startswith(code, "688") and ld >= 20190722) or (util.startswith(code, "60") and ld >= 20230410)
  if reg then for k = 2, #days do out[#out + 1] = { days[k], "C" } end end
  return out
end

local function is_delist_name(n) return util.truthy(n) and (util.startswith(n, "退市") or util.endswith(n, "退")) end

-- [(start, name)] sorted -> rows with the first row at `first` (names in force on `first`).
local function timeline_to_rows(tl, first)
  tl = util.sorted(tl, function(x) return x[1] end)
  local cur, out = nil, {}
  for _, x in ipairs(tl) do
    local d, n = x[1], x[2]
    if d <= first then cur = n
    else
      if #out == 0 then out[1] = { first, cur ~= nil and cur or n } end
      if out[#out][2] ~= n then out[#out + 1] = { d, n } end
    end
  end
  if #out == 0 then out[1] = { first, cur } end
  return out
end

local function add_symbol_change(rows, old, switch)
  local out = {}
  for i, r in ipairs(rows) do
    local d, n = r[1], r[2]
    local nxt = rows[i + 1] and rows[i + 1][1] or 99999999
    if d < switch then
      out[#out + 1] = { d, n, old }
      if nxt > switch then out[#out + 1] = { switch, n, nil } end
    else
      out[#out + 1] = { d, n, nil }
    end
  end
  return out
end

-- Counter keeping first-insertion order; add() like Counter[key] += n.
local function counter()
  local c = { v = {} }
  function c:add(k, n) self.v[k] = (self.v[k] or 0) + (n or 1) end
  return c
end

-- Count stocks whose name ST state disagrees with flags on some row start / flag edge.
local function check_st(rows, runs, stats, tag)
  local function name_at(d)
    local n
    for _, r in ipairs(rows) do if r[1] <= d then n = r[2] end end
    return util.truthy(n) and n or ""
  end
  local bad = 0
  for _, ab in ipairs(runs) do if not contains(name_at(ab[1]), "ST") then bad = bad + 1 end end
  for _, r in ipairs(rows) do
    if contains((r[2]:gsub("SST", "ST")), "ST") then
      local inside = false
      for _, ab in ipairs(runs) do if ab[1] <= r[1] and r[1] <= ab[2] then inside = true end end
      if not inside then bad = bad + 1 end -- a name with ST before / outside the flagged days
    end
  end
  if bad > 0 then stats:add("cn_" .. tag .. "_st_mismatch_stocks") end
end

-- SZSE sometimes dates an ST/*ST 简称 a few days before the first ST trading day (the change is effective
-- after a suspension / weekend). Move such rows to the first flagged day so 'ST' never shows early.
local function align_st_entry(out, runs, stats)
  local res = {}
  for i, r in ipairs(out) do res[i] = { r[1], r[2] } end
  for i = 2, #res do
    local d, n = res[i][1], res[i][2]
    if contains(n, "ST") and not contains(res[i - 1][2], "ST") then
      local nxt = res[i + 1] and res[i + 1][1] or 99999999
      local fl, inside = {}, false
      local lim = math.min(nxt, add_days(d, 10))
      for _, ab in ipairs(runs) do
        if ab[1] >= d and ab[1] < lim then fl[#fl + 1] = ab[1] end
        if ab[1] <= d and d <= ab[2] then inside = true end
      end
      if #fl > 0 and not inside and fl[1] > d then
        res[i] = { fl[1], n }
        stats:add("cn_sz_st_entry_aligned")
      end
    end
  end
  return res
end

local function cn_symbols(db, boards)
  local rows = q(db, "SELECT id, code, symbol, name, board, list_date, delist_date, first, last, st_ranges FROM symbols WHERE market='cn' AND is_index=0")
  local out = {}
  for _, r in ipairs(rows) do
    local s = { sid = r[1], code = r[2], symbol = nz(r[3]), name = nz(r[4]), board = nz(r[5]), list_date = nz(r[6]), delist_date = nz(r[7]),
      first = nz(r[8]), last = nz(r[9]), st = nz(r[10]) }
    if boards == nil or boards[s.code:sub(1, 2)] then out[#out + 1] = s end
  end
  return out
end

local function cn_build(raw, db)
  local cache = path.join(raw, "cn")
  local cal = {}
  for _, r in ipairs(q(db, "SELECT day FROM calendar WHERE market='cn' ORDER BY day")) do cal[#cal + 1] = r[1] end
  local rows, stats = {}, counter()
  local ev = szse_events(cache)
  local syms = cn_symbols(db)
  local all_runs = all_st_runs(db)
  for _, s in ipairs(syms) do
    local sid, code = s.sid, s.symbol
    local first = util.truthy(s.first) and s.first or START
    local last = util.truthy(s.last) and s.last or END
    local runs = all_runs[sid] or {}
    if util.startswith(s.code, "sz.") then
      local e = ev[code] or {}
      local timeline, src = {}, nil
      if #e > 0 then
        timeline[1] = { 0, e[1][2] }
        for _, x in ipairs(e) do timeline[#timeline + 1] = { x[1], x[3] } end
        stats:add("cn_sz_with_events")
        src = "szse"
      else
        timeline[1] = { 0, norm_cn(s.name) }
        stats:add("cn_sz_no_events")
        src = "symbols"
      end
      local all = timeline_to_rows(timeline, first)
      local out = {}
      for _, r in ipairs(all) do if r[1] <= last then out[#out + 1] = r end end
      if #out == 0 then out = { all[1] } end
      out = align_st_entry(out, runs, stats)
      for _, r in ipairs(out) do rows[#rows + 1] = { sid, r[1], r[2], nil, src } end
      check_st(out, runs, stats, "sz") -- ST cross-check against flags
    else
      -- SH / BSE
      local items = em_samples(cache, code)
      local contents = load_contents(cache, items)
      local extra = survey_names(cache, code)
      extra[#extra + 1] = s.name
      local tl = new_timeline(s, items, contents, runs, extra, cal)
      local segs = tl:segments()
      local cur_base = base(strip_ipo(norm_cn(s.name)))
      if #segs == 0 then
        segs = { { 0, cur_base, false } }
        stats:add("cn_shbj_no_titles")
      else
        stats:add("cn_shbj_with_titles")
      end
      local n_exact, n_window = 0, 0
      for k = 2, #segs do if segs[k][3] then n_exact = n_exact + 1 else n_window = n_window + 1 end end
      stats:add("cn_shbj_renames_exact", n_exact)
      stats:add("cn_shbj_renames_window", n_window - tl.notice_based)
      stats:add("cn_shbj_renames_notice_plus5d", tl.notice_based)
      -- the last title base should be the current base name (else the current name came later)
      local lastb = segs[#segs][2]
      if lastb ~= cur_base and not (tl.st_only[lastb] or lastb == cur_base) then
        stats:add("cn_shbj_current_after_titles")
        if is_delist_name(cur_base) then
          -- 退市整理期: starts the day after the last ST day if that is close to the end, else the last
          -- 15 trading days (SH/BJ rule since 2020; 30 days before) of the bars
          local bars_days = {}
          for _, d in ipairs(cal) do if first <= d and d <= last then bars_days[#bars_days + 1] = d end end
          local start = #bars_days >= 15 and bars_days[#bars_days - 14] or first
          if #runs > 0 and runs[#runs][2] >= add_days(last, -90) then start = cal_next(cal, runs[#runs][2]) end
          segs[#segs + 1] = { math.max(start, segs[#segs][1]), cur_base, false }
        else
          segs[#segs + 1] = { math.max(cal_next(cal, tl.last_seg_day or segs[#segs][1]), segs[#segs][1]), cur_base, false }
        end
      end
      local base_tl = {}
      for i, x in ipairs(segs) do base_tl[i] = { i == 1 and 0 or x[1], x[2] } end
      local plain = {}
      for _, x in ipairs(base_tl) do if not tl.st_only[x[2]] then plain[#plain + 1] = x end end
      if #plain == 0 then plain = { { 0, cur_base } } end
      local st_tl = tl:st_prefix_runs()
      for _, x in ipairs(st_tl) do stats:add("cn_st_runs_" .. x[4]) end
      local ipo = ipo_prefix_days(s, cal)
      -- compose day-wise change points
      local pset = { [0] = true, [first] = true }
      for _, x in ipairs(base_tl) do pset[x[1]] = true end
      for _, x in ipairs(st_tl) do pset[x[1]] = true; pset[add_days(x[2], 1)] = true end
      for _, x in ipairs(ipo) do pset[x[1]] = true; pset[cal_next(cal, x[1])] = true end
      local points = {}
      for p in pairs(pset) do points[#points + 1] = p end
      table.sort(points)
      local out = {}
      for _, p in ipairs(points) do
        local b
        for _, x in ipairs(base_tl) do if x[1] <= p then b = x[2] end end
        local run
        for _, x in ipairs(st_tl) do if x[1] <= p and p <= x[2] then run = x; break end end
        if (run == nil or is_delist_name(b)) and tl.st_only[b] then
          local pb
          for _, x in ipairs(plain) do if x[1] <= p then pb = x[2] end end
          b = pb or plain[1][2]
        end
        local ip = ""
        for _, x in ipairs(ipo) do if x[1] == p then ip = x[2]; break end end
        local name
        if run ~= nil and not is_delist_name(b) then
          if util.truthy(run[3]) then name = run[3]
          else
            local s_flag, rest = "", b
            if re.match("^S" .. CJK, b) then s_flag, rest = "S", b:sub(2) end
            name = s_flag .. "ST" .. rest
          end
        else
          name = ip ~= "" and ip .. b or b
        end
        if #out == 0 or out[#out][2] ~= name then out[#out + 1] = { p, name } end
      end
      local all = timeline_to_rows(out, first)
      out = {}
      for _, r in ipairs(all) do if r[1] <= last then out[#out + 1] = r end end
      if #out == 0 then out = { all[1] } end
      local src = #items > 0 and "em-titles" or "symbols"
      if util.startswith(code, "920") then
        local fp = path.join(cache, "bse_oldcode", code .. ".json")
        local old = path.exists(fp) and nz(ok.read_json(fp).old) or nil
        if util.truthy(old) then
          local pilot = util.contains_value({ "833819", "830799", "831445", "430489", "839167", "834682" }, old)
          out = add_symbol_change(out, old, pilot and 20250506 or 20251009)
          stats:add("cn_bse_old_code")
        elseif (s.list_date or 0) < 20240422 then
          stats:add("cn_bse_old_code_missing")
        end
      end
      for _, r in ipairs(out) do rows[#rows + 1] = { sid, r[1], r[2], r[3], src } end
      local pairs2 = {}
      for i, r in ipairs(out) do pairs2[i] = { r[1], r[2] } end
      check_st(pairs2, runs, stats, "shbj")
    end
  end
  return rows, stats
end

-- ------------------------------------------------------------------------------------------- us
local KEEP_FORMS = util.set({ "10-K", "10-Q", "8-K", "20-F", "40-F", "6-K", "10-K/A", "10-Q/A", "8-K/A", "S-1", "S-4", "DEF 14A", "10-KT", "11-K" })

local function compact_filings(f)
  local out = {}
  local forms = nz(f.form) or {}
  local isx = nz(f.isXBRL)
  for i = 1, #forms do
    if KEEP_FORMS[forms[i]] then out[#out + 1] = { f.filingDate[i], forms[i], f.primaryDocument[i], isx and isx[i] or 0 } end
  end
  return out
end

-- Ticker-like prefix of an EDGAR document name: 'meta-20220630.htm' -> 'META', 'fb-12312012x10k.htm' -> 'FB'.
local function doc_prefix(doc)
  doc = doc or ""
  local m = re.match("^([a-z]{1,5})-(?:\\d{8}|\\d{6,8}x|\\d{4}x|q\\dfy\\d|\\d{8}x|\\d{2}\\d{2}\\d{4}x)", doc, "i")
  if m then return m[1]:upper() end
  m = re.match("^([a-z]{1,5})-?(?:20\\d{6})\\.htm", doc, "i")
  return m and m[1]:upper() or nil
end
M.doc_prefix = doc_prefix

local function is_alpha_cp(c) return (c >= 65 and c <= 90) or (c >= 97 and c <= 122) or (c >= 0xC0 and c <= 0x24F and c ~= 0xD7 and c ~= 0xF7) end
local function is_upper_cp(c) return (c >= 65 and c <= 90) or (c >= 0xC0 and c <= 0xDE and c ~= 0xD7) end
local function py_upper(s) return (s:gsub("%l", string.upper)) end
local function py_lower(s) return (s:gsub("%u", string.lower)) end
local function py_isalpha(s)
  if s == "" then return false end
  for _, c in utf8.codes(s) do if not is_alpha_cp(c) then return false end end
  return true
end

local function title_case_sec(name)
  if not util.truthy(name) then return name end
  name = util.strip(re.sub("\\s*/[A-Z]{2,3}/?\\s*$", "", name)) -- EDGAR state suffix: 'CORP /DE/', 'INC/ FA'
  name = util.strip(re.sub("\\s*/\\s*[A-Z]{2}\\s*$", "", name))
  local nl, nu = 0, 0
  for _, c in utf8.codes(name) do if is_alpha_cp(c) then nl = nl + 1; if is_upper_cp(c) then nu = nu + 1 end end end
  if nl == 0 or nu / nl < 0.7 then return name end
  local small = util.set({ "OF", "AND", "THE", "FOR", "IN", "ON", "AT", "BY", "TO", "DE" })
  local keep_upper = util.set({ "LLC", "LP", "L.P.", "PLC", "NV", "N.V.", "SA", "S.A.", "AG", "SE", "II", "III", "IV", "USA", "US",
    "REIT", "ETF", "NA", "N.A.", "UK", "AB", "ASA", "BV", "B.V.", "LTD.", "CO." })
  local out = {}
  local i = 0
  for w in name:gmatch("%S+") do
    local core = util.strip(w, ",")
    if core ~= py_upper(core) then out[#out + 1] = w
    elseif keep_upper[core] and core ~= "LTD." and core ~= "CO." then out[#out + 1] = w
    elseif i > 0 and small[core] then out[#out + 1] = py_lower(w)
    elseif ulen(core) <= 3 and not re.search("[AEIOUY]", core) and py_isalpha(core) then out[#out + 1] = w -- acronyms like IBM, CVS
    else
      local parts = {}
      for _, p in ipairs(util.split(w, "-")) do parts[#parts + 1] = usub(p, 1, 1) .. py_lower(usub(p, 2)) end
      out[#out + 1] = table.concat(parts, "-")
    end
    i = i + 1
  end
  return table.concat(out, " ")
end
M.title_case_sec = title_case_sec

local function norm_en(n)
  local s = py_lower(n or "")
  s = util.replace(s, "&", "and"); s = util.replace(s, "corporation", "corp"); s = util.replace(s, "incorporated", "inc")
  s = util.replace(s, "company", "co"); s = util.replace(s, "limited", "ltd")
  return re.sub("[^a-z0-9]", "", s)
end

-- Full overrides (names + tickers) for successor-issuer cases the SEC data of the current CIK cannot show.
-- Alphabet: CIK 1652044 only exists since the 2015-10-02 holding reorganisation (Google Inc. = CIK 1288776).
-- Before 2014-04-03 the single listed class traded as GOOG; from 2014-04-03 class A = GOOGL, class C = GOOG.
local US_FULL = {
  GOOGL = { { 0, "Google Inc.", "GOOG" }, { 20140403, "Google Inc.", nil }, { 20151002, "Alphabet Inc.", nil } },
  GOOG = { { 0, "Google Inc.", nil }, { 20140403, "Google Inc. - Class C Capital Stock", nil },
    { 20151002, "Alphabet Inc. - Class C Capital Stock", nil } },
}
-- Well-documented ticker changes: current symbol -> [(old ticker, first trading day under the next ticker)].
local US_TICKERS = {
  META = { { "FB", 20220609 } }, RTX = { { "UTX", 20200403 } }, XYZ = { { "SQ", 20250121 } }, WBD = { { "DISCA", 20220411 } },
  ELV = { { "WLP", 20141203 }, { "ANTM", 20220628 } }, BKNG = { { "PCLN", 20180227 } }, LUMN = { { "CTL", 20200918 } },
  TFC = { { "BBT", 20191209 } }, MDLZ = { { "KFT", 20121002 } },
}
local US_DOCNAMES_REJECT = util.set({ "PCG", "WASH" }) -- checked by hand: utility-subsidiary / unrelated document prefixes
local DOC_PREFIX_NOISE = util.set({ "TMB", "FORM", "DOC", "PART", "TEN", "TENK", "TENQ", "EX", "Q", "K", "FY", "R", "D", "A", "MAIN" })
local PERIODIC = util.set({ "10-K", "10-Q", "20-F", "40-F", "10-K/A", "10-Q/A", "10-KT" })

local function timeline_to_rows3(tl, first)
  tl = util.sorted(tl, function(x) return x[1] end)
  local cur, out = nil, {}
  for _, x in ipairs(tl) do
    local d, n, t = x[1], x[2], x[3]
    if d <= first then cur = { n, t }
    else
      if #out == 0 then out[1] = cur and { first, cur[1], cur[2] } or { first, n, t } end
      if not (out[#out][2] == n and out[#out][3] == t) then out[#out + 1] = { d, n, t } end
    end
  end
  if #out == 0 then out[1] = { first, cur[1], cur[2] } end
  return out
end

local function ticker_like_change(old_p, cur, n_old)
  if n_old < 2 or old_p == cur or util.startswith(old_p, cur) or util.startswith(cur, old_p) or DOC_PREFIX_NOISE[old_p] then return nil end
  if #old_p > #cur then -- 'ACCEL' / 'ARROW': spelled-out names, not tickers (subsequence test)
    local j = 1
    local all_in = true
    for k = 1, #cur do
      local ch = cur:sub(k, k)
      local found = false
      while j <= #old_p do
        if old_p:sub(j, j) == ch then found = true; j = j + 1; break end
        j = j + 1
      end
      if not found then all_in = false; break end
    end
    if all_in then return nil end
  end
  if #old_p == 5 and util.endswith(old_p, "U") then return nil end -- SPAC units
  return true
end

local function us_ticker_from_docs(sym, sub, name_tl)
  local cur = re.sub("[^A-Z]", "", py_upper(sym))
  local fil = {}
  for _, f in ipairs(sub.filings) do
    if PERIODIC[f[2]] then fil[#fil + 1] = { ymd(f[1]), doc_prefix(f[3]) or "" } end
  end
  fil = util.sorted(fil)
  local kept = {}
  for _, x in ipairs(fil) do if x[2] ~= "" then kept[#kept + 1] = x end end
  fil = kept
  if #fil < 4 then return nil end
  local cnt, corder = {}, {}
  for k = #fil - 3, #fil do
    local p = fil[k][2]
    if not cnt[p] then cnt[p] = 0; corder[#corder + 1] = p end
    cnt[p] = cnt[p] + 1
  end
  local recent, bestc = nil, -1
  for _, p in ipairs(corder) do if cnt[p] > bestc then recent, bestc = p, cnt[p] end end
  if recent ~= cur then return nil end
  local segs = {}
  for _, x in ipairs(fil) do
    local d, p = x[1], x[2]
    if #segs > 0 and segs[#segs][1] == p then segs[#segs][3] = d; segs[#segs][4] = segs[#segs][4] + 1
    else segs[#segs + 1] = { p, d, d, 1 } end
  end
  local f2 = {}
  for _, s in ipairs(segs) do if s[4] >= 2 or s[1] == cur then f2[#f2 + 1] = s end end
  local m = {}
  for _, s in ipairs(f2) do
    if #m > 0 and m[#m][1] == s[1] then m[#m][3] = s[3]; m[#m][4] = m[#m][4] + s[4]
    else m[#m + 1] = { s[1], s[2], s[3], s[4] } end
  end
  if #m < 2 or m[#m][1] ~= cur then return nil end
  local chain = {}
  local k = #m
  while k >= 2 and #chain < 3 do
    local old, new = m[k - 1], m[k]
    local inchain = old[1] == cur
    for _, c in ipairs(chain) do if c[1] == old[1] then inchain = true end end
    if inchain or not ticker_like_change(old[1], new[1], old[4]) then break end
    local lo, hi = old[3], new[2]
    local day = hi
    for j = 2, #name_tl do
      local d = name_tl[j][1]
      if lo < d and d <= hi then day = d end
    end
    table.insert(chain, 1, { old[1], day })
    k = k - 1
  end
  return #chain > 0 and chain or nil
end

local function load_sub(cik, cache, sec_cache)
  for _, d in ipairs({ path.join(cache, "subs"), util.truthy(sec_cache) and sec_cache or false }) do
    if d then
      local fp = path.join(d, tostring(cik) .. ".json")
      if path.exists(fp) then
        local okv, sub = pcall(ok.read_json, fp)
        if okv and not util.truthy(nz(sub.missing)) then return sub end
      end
    end
  end
  return nil
end

local function us_build(raw, db, sec_cache)
  local rows, stats, changes = {}, counter(), {}
  local cache = path.join(raw, "us")
  local nasdaq = {}
  local p = path.join(cache, "nasdaq_symbolchangehistory.json")
  if path.exists(p) then
    local okv, err = pcall(function()
      for _, r in ipairs(ok.read_json(p).data.symbolChangeHistoryTable.rows) do
        local parts = util.split(r.effective, "/")
        local m_, d_, y = parts[1], parts[2], parts[3]
        local key = py_upper(r.newSymbol)
        nasdaq[key] = nasdaq[key] or {}
        table.insert(nasdaq[key], { py_upper(r.oldSymbol), math.tointeger(tonumber(y .. m_ .. d_)) })
      end
    end)
    if not okv then log("nasdaq list unreadable: " .. tostring(err)) end
  end
  local syms = q(db, "SELECT id, symbol, name, cik, first, last FROM symbols WHERE market='us'")
  local span = {}
  for _, r in ipairs(syms) do span[r[2]] = { util.truthy(nz(r[5])) and r[5] or START, util.truthy(nz(r[6])) and r[6] or END } end
  for _, r in ipairs(syms) do
    local sid, sym, name, cik = r[1], nz(r[2]), nz(r[3]), nz(r[4])
    local first = math.max(util.truthy(nz(r[5])) and r[5] or START, START)
    if US_FULL[sym] then
      stats:add("us_curated_full")
      local out = timeline_to_rows3(US_FULL[sym], first)
      for _, x in ipairs(out) do rows[#rows + 1] = { sid, x[1], x[2], x[3], "curated" } end
      for k, x in ipairs(out) do
        local nxt = out[k + 1] or { 99999999 }
        if util.truthy(x[3]) then
          changes[#changes + 1] = O({ { "symbol", sym }, { "old", x[3] }, { "from", x[1] }, { "until", nxt[1] }, { "how", "curated" } })
        end
      end
    else
      local sub = util.truthy(cik) and load_sub(cik, cache, sec_cache) or nil
      -- --- names: SEC formerNames
      local name_tl = { { 0, name } }
      if sub and util.truthy(nz(sub.formerNames)) then
        local fns0 = {}
        for _, f in ipairs(sub.formerNames) do if util.truthy(nz(f.to)) then fns0[#fns0 + 1] = f end end
        local fns = util.sorted(fns0, function(f) return f.to end)
        if #fns > 0 and util.truthy(nz(fns[1].from)) and ymd(fns[1].from) > add_days(first, 90) then
          -- the filer (CIK) was created after the price series starts: a successor holding company
          -- (e.g. Disney's 2019 'TWDC Holdco 613'); its shell names never traded -> keep the current name
          stats:add("us_successor_cik_names_skipped")
          fns = {}
        end
        if #fns > 0 then
          local tl = {}
          for i, f in ipairs(fns) do tl[i] = { i == 1 and 0 or add_days(ymd(fns[i - 1].to), 1), title_case_sec(f.name) } end
          tl[#tl + 1] = { add_days(ymd(fns[#fns].to), 1), name }
          local merged = {}
          for _, x in ipairs(tl) do
            if #merged > 0 and norm_en(merged[#merged][2]) == norm_en(x[2]) then
              merged[#merged] = { merged[#merged][1], x[2] } -- spelling-only change: keep the later spelling
            else
              merged[#merged + 1] = x
            end
          end
          local nchg = 0
          for k = 2, #merged do if merged[k][1] > first then nchg = nchg + 1 end end
          if nchg > 0 then
            stats:add("us_name_changes_in_period", nchg)
            stats:add("us_symbols_with_name_history")
          end
          name_tl = merged
        end
      end
      -- --- tickers
      local how, tick
      if US_TICKERS[sym] then tick, how = US_TICKERS[sym], "curated"
      elseif nasdaq[sym] then tick, how = util.sorted(nasdaq[sym], function(x) return x[2] end), "nasdaq"
      elseif sub and util.truthy(nz(sub.filings)) and not US_DOCNAMES_REJECT[sym] then
        tick = us_ticker_from_docs(sym, sub, name_tl)
        if tick and span[tick[1][1]] and span[tick[1][1]][1] < tick[1][2] and span[tick[1][1]][2] > first then
          -- the old prefix is another listed symbol trading at the same time (joint filings, e.g. CTVA/DD)
          stats:add("us_docnames_rejected_conflict")
          tick = nil
        end
        how = tick and "sec-docnames" or nil
      end
      local tick_tl = { { 0, nil } }
      if tick and tick[#tick][2] > first then
        tick_tl = { { 0, tick[1][1] } }
        for i, x in ipairs(tick) do tick_tl[#tick_tl + 1] = { x[2], tick[i + 1] and tick[i + 1][1] or nil } end
        stats:add("us_ticker_changes_" .. how)
        local prev = 0
        for _, x in ipairs(tick) do
          local t, d = x[1], x[2]
          if d > first then
            changes[#changes + 1] = O({ { "symbol", sym }, { "old", t }, { "from", math.max(prev, first) }, { "until", d }, { "how", how } })
          end
          prev = d
        end
      else
        how = nil
      end
      local pset = {}
      for _, x in ipairs(name_tl) do pset[x[1]] = true end
      for _, x in ipairs(tick_tl) do pset[x[1]] = true end
      local pts = {}
      for p_ in pairs(pset) do pts[#pts + 1] = p_ end
      table.sort(pts)
      local tl = {}
      for _, p_ in ipairs(pts) do
        local nn, tt
        for _, x in ipairs(name_tl) do if x[1] <= p_ then nn = x[2] end end
        for _, x in ipairs(tick_tl) do if x[1] <= p_ then tt = x[2] end end
        tl[#tl + 1] = { p_, nn, tt }
      end
      local src = sub and "sec" or "symbols"
      if how then src = src .. "+" .. how end
      for _, x in ipairs(timeline_to_rows3(tl, first)) do rows[#rows + 1] = { sid, x[1], x[2], x[3], src } end
    end
  end
  return rows, stats, changes
end

-- ------------------------------------------------------------------------------------------- fetch
local function em_ann_url(code, page, size, f_node, begin, end_, sr)
  local qs = { { "sr", sr }, { "page_size", size }, { "page_index", page }, { "ann_type", "A" }, { "client_source", "web" },
    { "stock_list", code }, { "f_node", f_node }, { "s_node", 0 } }
  if begin then qs[#qs + 1] = { "begin_time", d2s(begin) } end
  if end_ then qs[#qs + 1] = { "end_time", d2s(end_) } end
  return "https://np-anotice-stock.eastmoney.com/api/security/ann?" .. kit.query_string(qs)
end

local function em_compact(lst)
  local out = {}
  for _, x in ipairs(lst or {}) do
    local disp = nz(x.display_time) or nz(x.notice_date) or ""
    local cols = {}
    for _, c in ipairs(nz(x.columns) or {}) do cols[#cols + 1] = nz(c.column_name) or NULL end
    out[#out + 1] = O({ { "d", usub(disp, 1, 19) }, { "n", usub(nz(x.notice_date) or "", 1, 10) }, { "t", nz(x.title) or "" },
      { "a", nz(x.art_code) or NULL }, { "c", cols } })
  end
  return out
end

local function em_fetch_list(http, code, opts)
  opts = opts or {}
  local f_node, begin, end_ = opts.f_node or 0, opts.begin, opts["end"]
  local max_pages, size, sr = opts.max_pages or 60, opts.size or 100, opts.sr or -1
  local items, total, page = {}, nil, 1
  while true do
    local txt = http:get(em_ann_url(code, page, size, f_node, begin, end_, sr))
    if txt == nil then break end
    local d = json.decode(txt)
    local data = nz(d.data) or {}
    total = nz(data.total_hits) or 0
    for _, it in ipairs(em_compact(nz(data.list))) do items[#items + 1] = it end
    if size == 1 or page * size >= math.min(total, 5000) or page >= max_pages or not util.truthy(nz(data.list)) then break end
    page = page + 1
  end
  return O({ { "total", total == nil and NULL or total }, { "items", items } })
end

-- Announcement body: East Money text API, else the PDF (pdf.dfcfw.com, first 3 pages). nil when unavailable.
local function notice_text(http, art)
  local txt = http:get("https://np-cnotice-stock.eastmoney.com/api/content/ann?" ..
    kit.query_string({ { "art_code", art }, { "client_source", "web" }, { "page_index", 1 } }))
  if txt ~= nil then
    local okv, j = pcall(json.decode, txt)
    if okv then
      local body = (nz(j.data) or {}).notice_content
      if util.truthy(nz(body)) then return body end
    end
  end
  local pdf = http:get(("https://pdf.dfcfw.com/pdf/H2_%s_1.pdf"):format(art), nil, nil, nil, true)
  if not pdf or pdf:sub(1, 4) ~= "%PDF" then return nil end
  local pages = kit.pdf_text_pages(pdf, 3)
  if not pages then return nil end
  return table.concat(pages, "\n")
end

local function cn_fetch(args, http, raw)
  local db = args.db
  local cache = path.join(raw, "cn")
  path.mkdir(cache, true)
  -- 1. SZSE 简称变更 (refetched when older than 1 day)
  local p = path.join(cache, "szse_namechange_tab2.xlsx")
  if not path.exists(p) or ok.time.time() - path.mtime(p) > 86400 then
    local data = http:get("https://www.szse.cn/api/report/ShowReport?SHOWTYPE=xlsx&CATALOGID=SSGSGMXX&TABKEY=tab2&random=0.5", nil, nil, nil, true)
    write_atomic(p, data)
    log(("szse tab2: %d bytes"):format(#data))
  end
  -- 2. SH + BSE: East Money announcement titles
  local syms = cn_symbols(db, { sh = true, bj = true })
  if util.truthy(args.codes) then
    local want = util.set(util.split(args.codes, ","))
    syms = util.filter(syms, function(s) return want[s.symbol] end)
  end
  if util.truthy(args.limit) then syms = util.slice(syms, 1, args.limit) end
  local all_runs = all_st_runs(db)
  path.mkdir(path.join(cache, "em"), true)
  local blocked = 0

  local function phase1(s)
    local code = s.symbol
    local first = math.max(util.truthy(s.first) and s.first or START, START)
    local jobs = { { code .. "_start.json", { f_node = 0, ["end"] = add_days(first, 0), size = 20, max_pages = 1 } },
      { code .. "_f4.json", { f_node = 4, begin = add_days(first, -200), ["end"] = END + 3 } } }
    for _, ab in ipairs(all_runs[s.sid] or {}) do
      jobs[#jobs + 1] = { ("%s_st_%d_%d.json"):format(code, ab[1], ab[2]), { f_node = 0, begin = add_days(ab[1], -45), ["end"] = add_days(ab[2], 45) } }
    end
    local n = 0
    local fp = path.join(cache, "survey", code .. ".json")
    if not path.exists(fp) then
      local mk = util.startswith(s.code, "bj.") and "BJ" or "SH"
      local txt = http:get(("https://emweb.securities.eastmoney.com/PC_HSF10/CompanySurvey/PageAjax?code=%s%s"):format(mk, code))
      local j = {}
      pcall(function() j = (nz(json.decode(txt).jbzl) or { {} })[1] or {} end)
      write_atomic(fp, json.encode(O({ { "former", nz(j.FORMERNAME) or NULL }, { "abbr", nz(j.SECURITY_NAME_ABBR) or NULL } }), { ensure_ascii = false }))
      n = n + 1
    end
    for _, jb in ipairs(jobs) do
      local f = path.join(cache, "em", jb[1])
      if not path.exists(f) then
        write_atomic(f, json.encode(em_fetch_list(http, code, jb[2]), { ensure_ascii = false }))
        n = n + 1
      end
    end
    return n
  end

  local function phase2(s)
    -- Refine rename windows wider than 5 days with all notices inside, then read rename announcements.
    local code = s.symbol
    local runs = all_runs[s.sid] or {}
    local n = 0
    for _ = 1, 2 do
      local items = em_samples(cache, code)
      local contents = load_contents(cache, items)
      local extra = survey_names(cache, code)
      extra[#extra + 1] = s.name
      local tl = new_timeline(s, items, contents, runs, extra)
      local _, need = tl:segments()
      local todo = 0
      for _, x in ipairs(need) do
        local lo, hi = x[1], x[2]
        if hi - lo > 5 and add_days(lo, 5) < hi then
          local fp = path.join(cache, "em", ("%s_ref_%d_%d.json"):format(code, lo, hi))
          if not path.exists(fp) then
            write_atomic(fp, json.encode(em_fetch_list(http, code, { f_node = 0, begin = lo, ["end"] = hi, max_pages = 4 }), { ensure_ascii = false }))
            n = n + 1; todo = todo + 1
          end
        end
      end
      -- contents of rename notices near each window
      items = em_samples(cache, code)
      local wins = {}
      for _, x in ipairs(need) do wins[#wins + 1] = { add_days(x[1], -45), add_days(x[2], 5) } end
      for _, it in ipairs(items) do
        if is_rename_notice(it) then
          local d = notice_day(it)
          local inwin = false
          for _, w in ipairs(wins) do if w[1] <= d and d <= w[2] then inwin = true end end
          local fp = path.join(cache, "content", tostring(it.a) .. ".json")
          if inwin and not path.exists(fp) then
            local body = notice_text(http, it.a)
            if body == nil then blocked = blocked + 1 -- text API blocked (HTTP 567) and no PDF: not cached, retried next run
            else
              write_atomic(fp, json.encode(O({ { "a", it.a }, { "text", usub(body, 1, 6000) } }), { ensure_ascii = false }))
              content_cache[fp] = nil
              n = n + 1; todo = todo + 1
            end
          end
        end
      end
      if todo == 0 then break end
    end
    -- BSE: old NEEQ code from the header of an early announcement
    if util.startswith(code, "920") and (s.list_date or 0) < 20240422 then
      local fp = path.join(cache, "bse_oldcode", code .. ".json")
      local prev = path.exists(fp) and ok.read_json(fp) or nil
      if prev == nil or (not util.truthy(nz(prev.old)) and (prev.tries or 3) < 12) then
        local items = {}
        for _, it in ipairs(em_samples(cache, code)) do if notice_day(it) < 20250401 then items[#items + 1] = it end end
        items = util.sorted(items, function(x) return x.d end, true)
        local done_n = prev and (prev.tries or 3) or 0
        local old
        -- some article bodies are blocked (HTTP 567): try further notices, spread over the years
        local cand = util.slice(items, 1, 3)
        if prev then
          local stepn = math.max(1, #items // 9)
          for k = 4, #items, stepn do cand[#cand + 1] = items[k] end
          cand = util.slice(cand, done_n + 1, done_n + 9)
        end
        for _, it in ipairs(cand) do
          local body = notice_text(http, it.a)
          if body == nil then blocked = blocked + 1; body = "" end
          local m = re.search("(?:证券|股票)代码[：:\\s]*((?:4|8)\\d{5})", norm_cn(body) and (function()
            local out = {}
            for _, c in utf8.codes(body, true) do
              if c >= 0xFF01 and c <= 0xFF5E then c = c - 0xFF01 + 0x21 end
              out[#out + 1] = utf8.char(c)
            end
            return table.concat(out)
          end)() or body)
          n = n + 1
          if m then old = m[1]; break end
        end
        write_atomic(fp, json.encode(O({ { "code", code }, { "old", old or NULL }, { "tries", done_n + #cand } })))
      end
    end
    return n
  end

  for _, ph in ipairs({ { "phase1", phase1 }, { "phase2", phase2 } }) do
    local t0, done, req = ok.time.time(), 0, 0
    for _, s in ipairs(syms) do
      req = req + ph[2](s)
      done = done + 1
      if done % 100 == 0 or done == #syms then
        log(("cn %s %d/%d stocks, %d requests, %.0fs, %d announcement texts blocked"):format(ph[1], done, #syms, req, ok.time.time() - t0, blocked))
      end
    end
  end
end

local function us_fetch(args, http, raw)
  local cache = path.join(raw, "us")
  path.mkdir(path.join(cache, "subs"), true)
  local cset = {}
  for _, r in ipairs(q(args.db, "SELECT cik FROM symbols WHERE market='us' AND cik IS NOT NULL")) do cset[r[1]] = true end
  local ciks = {}
  for c in pairs(cset) do ciks[#ciks + 1] = c end
  table.sort(ciks)
  if util.truthy(args.ciks) then
    ciks = {}
    for _, x in ipairs(util.split(args.ciks, ",")) do ciks[#ciks + 1] = math.tointeger(tonumber(x)) end
  end
  if util.truthy(args.limit) then ciks = util.slice(ciks, 1, args.limit) end
  -- Nasdaq symbol change history (recent ~12 months)
  local p = path.join(cache, "nasdaq_symbolchangehistory.json")
  if not path.exists(p) or ok.time.time() - path.mtime(p) > 86400 then
    local txt = http:get("https://api.nasdaq.com/api/quote/list-type-extended/symbolchangehistory?sortColumn=EffectiveDate&sortOrder=DESC&limit=1000",
      nil, { Accept = "application/json" })
    write_atomic(p, txt)
  end
  local t0, done, req = ok.time.time(), 0, 0
  for _, cik in ipairs(ciks) do
    local fp = path.join(cache, "subs", cik .. ".json")
    local n = 0
    if not path.exists(fp) then
      local txt = http:get(("https://data.sec.gov/submissions/CIK%010d.json"):format(cik), UA_TOOL)
      n = n + 1
      if txt == nil then
        write_atomic(fp, json.encode(O({ { "cik", cik }, { "missing", true } })))
      else
        local d = json.decode(txt)
        local filings = compact_filings(d.filings.recent)
        for _, f in ipairs(nz(d.filings.files) or {}) do
          local t2 = http:get("https://data.sec.gov/submissions/" .. f.name, UA_TOOL)
          n = n + 1
          if t2 then for _, x in ipairs(compact_filings(json.decode(t2))) do filings[#filings + 1] = x end end
        end
        write_atomic(fp, json.encode(O({ { "cik", cik }, { "name", nz(d.name) or NULL }, { "tickers", nz(d.tickers) or NULL },
          { "formerNames", nz(d.formerNames) or NULL }, { "filings", filings } }), { ensure_ascii = false }))
      end
    end
    done = done + 1
    req = req + n
    if done % 200 == 0 or done == #ciks then log(("us subs %d/%d ciks, %d requests, %.0fs"):format(done, #ciks, req, ok.time.time() - t0)) end
  end
end

-- ------------------------------------------------------------------------------------------- load
local SCHEMA = [[CREATE TABLE IF NOT EXISTS names(sid INTEGER NOT NULL, start INTEGER NOT NULL, name TEXT NOT NULL,
 symbol TEXT, source TEXT, PRIMARY KEY(sid, start)) WITHOUT ROWID]]

local function cmd_load(args)
  local dbp = args.db
  if path.exists(dbp .. ".building") then
    io.stderr:write(dbp .. ".building exists (build_market_sqlite.lua running); not loading\n")
    os.exit(1)
  end
  local raw = args.raw
  local cn_rows, cn_stats = cn_build(raw, dbp)
  local us_rows, us_stats, us_changes = us_build(raw, dbp, args.sec_cache)
  local names = {}
  for i, r in ipairs(cn_rows) do names[i] = r[3] end
  local tw = kit.zhconv("s2tw", names)
  local rows = {}
  for i, r in ipairs(cn_rows) do rows[#rows + 1] = { r[1], r[2], tw[i], r[4], r[5] } end
  for _, r in ipairs(us_rows) do rows[#rows + 1] = r end
  local have = {}
  for _, r in ipairs(rows) do have[r[1]] = true end
  for _, r in ipairs(q(dbp, "SELECT id, COALESCE(name_zh_tw, name), first FROM symbols WHERE market='cn'")) do
    if not have[r[1]] then
      rows[#rows + 1] = { r[1], math.max(util.truthy(nz(r[3])) and r[3] or START, START), r[2], nil, "symbols" }
      cn_stats:add("cn_index_or_other_rows")
    end
  end
  local many = {}
  for i, r in ipairs(rows) do
    many[i] = { r[1], r[2], r[3] == nil and NULL or r[3], r[4] == nil and NULL or r[4], r[5] == nil and NULL or r[5] }
  end
  kit.sqlite(dbp, { { sql = SCHEMA }, { sql = "BEGIN" }, { sql = "DELETE FROM names" },
    { sql = "INSERT INTO names VALUES(?,?,?,?,?)", many = many }, { sql = "COMMIT" } })
  local cov = O()
  for _, m in ipairs({ "cn", "us" }) do
    local r = q(dbp, [[SELECT COUNT(*), SUM(n>0), SUM(n), SUM(n>1) FROM (SELECT s.id, (SELECT COUNT(*) FROM names x WHERE x.sid=s.id) n
            FROM symbols s WHERE s.market=?)]], { m })[1]
    cov[m] = O({ { "symbols", r[1] }, { "with_rows", r[2] }, { "rows", r[3] }, { "symbols_with_history", r[4] } })
  end
  -- dict(sorted((cn_stats + us_stats).items())): Counter addition keeps positive totals only
  local tot = {}
  for k, v in pairs(cn_stats.v) do tot[k] = (tot[k] or 0) + v end
  for k, v in pairs(us_stats.v) do tot[k] = (tot[k] or 0) + v end
  local skeys = {}
  for k, v in pairs(tot) do if v > 0 then skeys[#skeys + 1] = k end end
  table.sort(skeys)
  local stats = O()
  for _, k in ipairs(skeys) do stats[k] = tot[k] end
  local curated = {}
  for k in pairs(US_FULL) do curated[k] = true end
  for k in pairs(US_TICKERS) do curated[k] = true end
  local clist = util.set_list(curated)
  table.sort(clist)
  local manifest = O({
    { "built_at", os.date("%Y-%m-%dT%H:%M:%S") },
    { "table", "names(sid, start, name, symbol, source): row applies from start to the next row; symbol NULL = symbols.symbol" },
    { "coverage", cov },
    { "stats", stats },
    { "sources", O({
      { "cn_sz", "SZSE 名称变更 tab2 简称变更 (www.szse.cn/api/report/ShowReport?SHOWTYPE=xlsx&CATALOGID=SSGSGMXX&TABKEY=tab2)" },
      { "cn_sh_bj", "East Money announcement titles (np-anotice-stock.eastmoney.com/api/security/ann): 简称 at publication time; exact dates from 证券简称变更 announcement text (np-cnotice-stock.eastmoney.com)" },
      { "cn_st", "bars.flags bit1 (BaoStock isST) for SH/BJ ST periods; ST vs *ST from the title samples" },
      { "cn_bse_codes", "old NEEQ code from an old announcement header; 920xxx from 2025-10-09 (pilot 6 stocks 2025-05-06)" },
      { "us_names", "SEC submissions formerNames (data.sec.gov), current name = symbols.name" },
      { "us_tickers", "data.sec.gov filing document-name prefixes (e.g. fb-20220331.htm -> meta-20220630.htm); Nasdaq symbolchangehistory (recent); curated: " .. table.concat(clist, ", ") },
    }) },
    { "rules", {
      "cn names stored in Traditional Chinese via OpenCC s2tw (same as symbols.name_zh_tw); full-width ASCII folded to half-width, spaces removed",
      "SZ: name on day D = 变更后简称 of the last SZSE event <= D, before the first event its 变更前简称; an ST-entry event dated a few days before the first ST-flagged bar is moved to that bar",
      "SH/BJ: base name (without ST/*ST) from announcement-title samples; rename date = date read from the 证券简称变更 announcement (text API, else its PDF) when found; else rename-notice day + 5 calendar days (capped at the first new-name title); else the first day a title with the new name was published",
      "SH/BJ ST/*ST names only on days with bars.flags bit1 (renames into/out of ST aligned to the flag edges); the name (ST xx / *ST xx, incl. the shortened ST base) from prefixed titles inside the run, split where they switch; runs without prefixed titles take the matching ST group of the East Money former-name list (aligned from the most recent run); no other source -> ST + base (flags-default)",
      "SH/BJ 退市 names: from the day after the last ST day when that is within 90 days of the last bar, else the last 15 trading days",
      "SH/BJ first-days markers by rule: N on the listing day; C on days 2-5 for STAR (from 2019-07-22) and SH main board (from 2023-04-10) registration listings. SZ markers come from SZSE itself",
      "BJ: symbol = old NEEQ code (read from an old announcement header) until 2025-10-09 (pilot 833819, 830799, 831445, 430489, 839167, 834682: 2025-05-06), then NULL (= 920xxx)",
      "US: former names from SEC formerNames (to-date + 1 = start of next name); spelling-only changes merged; all-caps EDGAR names title-cased and /DE/-style suffixes dropped; when the CIK was created > 90 days after the first bar (successor holding company) its shell names are skipped",
      "US tickers: curated list first, then Nasdaq symbol-change history, then filing document-name prefixes (10-K/10-Q/20-F/40-F only; latest prefix must equal the current ticker; old prefix in >= 2 reports; up to 3 steps; rejected when the old prefix is another symbol trading at the same time); change date = SEC name-change date inside the window, else the first report with the new prefix",
    } },
    { "gaps", O({
      { "us_ticker", "document-name ticker dates are approximate (± one quarter; can be early when a filer renamed documents before the ticker switch); share-class changes and companies whose filings are not named after the ticker are not detected except the curated list" },
      { "us_successor", "symbols whose price series predates the CIK (holding-company reorganisations) keep the current name before the CIK existed, except the curated Google/Alphabet case" },
      { "cn_sh_bj", "renames without a readable announcement fall back to title sampling (median window 6 days); 2013-2022 East Money titles often omit the ST prefix, so ST vs *ST may come from the former-name list instead of dates" },
    }) },
  })
  kit.sqlite(dbp, { { sql = "INSERT OR REPLACE INTO meta VALUES(?,?)", params = { "manifest.names", json.encode(manifest, { ensure_ascii = false }) } } })
  path.write_text(path.join(raw, "us_ticker_changes.json"), json.encode(us_changes, { ensure_ascii = false, indent = 0 }))
  log(("load: %d rows; coverage %s"):format(#rows, util.repr(cov)))
  log("stats " .. json.encode(stats, { ensure_ascii = false }))
end

-- ------------------------------------------------------------------------------------------- validate
local function cmd_validate(args)
  local db = args.db
  local bad = {}
  local n_sym = 0
  for _, r in ipairs(q(db, "SELECT id, market, symbol, first, (SELECT MIN(start) FROM names WHERE sid=symbols.id) FROM symbols")) do
    n_sym = n_sym + 1
    local first, mn = nz(r[4]), nz(r[5])
    if mn == nil or mn > math.max(first or START, START) then bad[#bad + 1] = tuple("no row at first bar", r[2], nz(r[3]), first, mn) end
  end
  log(("validate: %d symbols, %d without a row at the first bar: %s"):format(n_sym, #bad, repr(util.slice(bad, 1, 10))))
  -- ST names outside flagged days (cn)
  local rows = q(db, [[WITH n AS (SELECT sid, start, name, LEAD(start, 1, 99999999) OVER (PARTITION BY sid ORDER BY start) AS e
      FROM names WHERE sid IN (SELECT id FROM symbols WHERE market='cn' AND is_index=0))
    SELECT n.sid, s.symbol, n.start, n.name,
      (SELECT flags & 2 FROM bars WHERE sid=n.sid AND day>=n.start AND day<n.e ORDER BY day LIMIT 1),
      (SELECT COUNT(*) FROM bars WHERE sid=n.sid AND day>=n.start AND day<n.e AND NOT (flags & 2)),
      (SELECT COUNT(*) FROM bars WHERE sid=n.sid AND day>=n.start AND day<n.e AND (flags & 2))
    FROM n JOIN symbols s ON s.id=n.sid ORDER BY n.sid, n.start]])
  local st_bad, st_glitch, unst = {}, {}, 0
  for _, r in ipairs(rows) do
    local sym, st, nm, f0, unflag, flagged = nz(r[2]), r[3], r[4], nz(r[5]), r[6], r[7]
    if contains(nm, "ST") then
      if f0 ~= nil and f0 == 0 then st_bad[#st_bad + 1] = tuple(sym, st, nm, "first bar day of the row is not ST-flagged")
      elseif unflag > 0 then st_glitch[#st_glitch + 1] = tuple(sym, st, nm, unflag) end
    elseif flagged > 0 then
      unst = unst + 1
    end
  end
  log(("validate: cn rows whose name has ST before the flags say ST (row start unflagged): %d; e.g. %s"):format(#st_bad, repr(util.slice(st_bad, 1, 15))))
  log(("validate: cn ST rows with isolated unflagged bar days inside (flag glitches): %d; e.g. %s"):format(#st_glitch, repr(util.slice(st_glitch, 1, 8))))
  log(("validate: cn name rows without ST on flagged bar days: %d"):format(unst))
  local function show(code_or_sym, days)
    local r = q(db, "SELECT id, market, symbol FROM symbols WHERE code=? OR symbol=? ORDER BY market LIMIT 1", { code_or_sym, code_or_sym })[1]
    if not r then log(("  spot %s: not in symbols"):format(code_or_sym)); return end
    local hist = q(db, "SELECT start, name, symbol FROM names WHERE sid=? ORDER BY start", { r[1] })
    local at = {}
    for _, d in ipairs(days) do
      local x
      for _, h in ipairs(hist) do if h[1] <= d then x = h end end
      if x then at[#at + 1] = ("%d:%s/%s"):format(d, x[2], util.truthy(nz(x[3])) and x[3] or util.str(nz(r[3]))) else at[#at + 1] = d .. ":-" end
    end
    log(("  spot %s: "):format(code_or_sym) .. table.concat(at, " | "))
  end
  for _, sd in ipairs({ { "GOOGL", { 20120601, 20140402, 20140403, 20151001, 20151002, 20200101 } },
    { "GOOG", { 20120601, 20140403, 20151002 } },
    { "META", { 20121001, 20211027, 20211028, 20220608, 20220609 } },
    { "sh.600104", { 20100104, 20110101, 20120601, 20200101 } },
    { "sh.600057", { 20100104, 20110830, 20120101, 20130101 } },
    { "sz.000001", { 20100104, 20120801, 20120802 } },
    { "sh.600069", { 20150301, 20150324, 20160401, 20200430, 20200801 } },
    { "sh.600071", { 20150430, 20150504, 20160308, 20160310 } } }) do
    show(sd[1], sd[2])
  end
end

-- ------------------------------------------------------------------------------------------- main
local function main()
  local ap = ok.args.parser()
  ap:arg("cmd", { choices = { "fetch", "load", "validate" } })
  ap:opt("--raw")
  ap:opt("--db", { required = true })
  ap:opt("--market", { default = "all", choices = { "cn", "us", "all" } })
  ap:opt("--workers", { type = "int", default = 2 })
  ap:opt("--limit", { type = "int", default = 0 })
  ap:opt("--codes", { default = "", help = "fetch only these cn symbols (testing)" })
  ap:opt("--ciks", { default = "", help = "fetch only these SEC CIKs (testing)" })
  ap:opt("--log")
  ap:opt("--sec-cache", { default = "", help = "existing SEC submissions cache dir (<cik>.json with formerNames)" })
  local args = ap:parse(arg)
  LOG = args.log
  if args.cmd == "fetch" then
    path.mkdir(args.raw, true)
    local http = M.new_http({ ["np-anotice-stock.eastmoney.com"] = 0.3, ["np-cnotice-stock.eastmoney.com"] = 0.5, ["pdf.dfcfw.com"] = 0.5,
      ["data.sec.gov"] = 0.25, ["www.szse.cn"] = 1.0, ["api.nasdaq.com"] = 1.0 })
    if args.market == "cn" or args.market == "all" then cn_fetch(args, http, args.raw) end
    if args.market == "us" or args.market == "all" then us_fetch(args, http, args.raw) end
  elseif args.cmd == "load" then
    cmd_load(args)
  else
    cmd_validate(args)
  end
end
M.main = main

if ok.is_main() then ok.main(main) end
return M
