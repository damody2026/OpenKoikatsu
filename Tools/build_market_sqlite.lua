-- Builds Content/IndependentResources/MarketData/market.sqlite from the .okbar / .okfx files
-- (Docs/STREET_SOCIAL_MARKET_DATA_FORMAT.md, section "SQLite"). The game (ok_mind.dll) reads the database when it
-- exists and falls back to the .okbar files otherwise. Other builders (fundamentals, US corporate actions) write
-- into the same database with the schema below.
--
--     Tools\lua\lua.exe Tools/build_market_sqlite.lua [--root Content/IndependentResources/MarketData] [--db .../market.sqlite]
local ok = require "ok"
local json, path, kit, util, time = ok.json, ok.path, ok.kit, ok.util, ok.time

local SCHEMA_VERSION = 1

local SCHEMA = [[
CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY, value TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS symbols(
 id INTEGER PRIMARY KEY, market TEXT NOT NULL, code TEXT NOT NULL, symbol TEXT, name TEXT, name_zh_tw TEXT, board TEXT,
 list_date INTEGER, delist_date INTEGER, is_index INTEGER NOT NULL DEFAULT 0, etf INTEGER NOT NULL DEFAULT 0,
 first INTEGER, last INTEGER, st_ranges TEXT, industry TEXT, cik INTEGER, price_basis TEXT,
 UNIQUE(market, code));
CREATE TABLE IF NOT EXISTS calendar(market TEXT NOT NULL, day INTEGER NOT NULL, PRIMARY KEY(market, day)) WITHOUT ROWID;
CREATE TABLE IF NOT EXISTS bars(
 sid INTEGER NOT NULL, day INTEGER NOT NULL, o INTEGER NOT NULL, h INTEGER NOT NULL, l INTEGER NOT NULL, c INTEGER NOT NULL,
 pc INTEGER NOT NULL, vol INTEGER NOT NULL, amount REAL NOT NULL, split REAL NOT NULL DEFAULT 1, div REAL NOT NULL DEFAULT 0,
 lu INTEGER NOT NULL DEFAULT 0, ld INTEGER NOT NULL DEFAULT 0, flags INTEGER NOT NULL DEFAULT 0,
 PRIMARY KEY(sid, day)) WITHOUT ROWID;
CREATE TABLE IF NOT EXISTS fx(pair TEXT NOT NULL, day INTEGER NOT NULL, rate REAL NOT NULL, PRIMARY KEY(pair, day)) WITHOUT ROWID;
CREATE TABLE IF NOT EXISTS fundamentals(
 sid INTEGER NOT NULL, period_end INTEGER NOT NULL, period TEXT NOT NULL, available INTEGER NOT NULL, currency TEXT NOT NULL,
 revenue REAL, net_income REAL, eps REAL, eps_ttm REAL, bvps REAL, roe REAL, total_shares REAL, equity REAL,
 total_assets REAL, operating_cf REAL, dividend_ps REAL, source TEXT,
 PRIMARY KEY(sid, period_end, period)) WITHOUT ROWID;
CREATE INDEX IF NOT EXISTS fundamentals_available ON fundamentals(sid, available);
]]

-- journal_mode / synchronous are per connection: every luakit call starts with them.
local PRAGMAS = { { sql = "PRAGMA journal_mode=OFF" }, { sql = "PRAGMA synchronous=OFF" } }

local function run(db, ops)
  local all = {}
  for _, p in ipairs(PRAGMAS) do all[#all + 1] = p end
  for _, o in ipairs(ops) do all[#all + 1] = o end
  local res = kit.sqlite(db, all)
  return table.move(res, #PRAGMAS + 1, #res, 1, {})
end

local function nul(v) if v == nil then return json.null end return v end

local function ymd(s)
  if not util.truthy(s) then return json.null end
  return math.tointeger(tonumber(util.str(s):gsub("-", ""):sub(1, 8)))
end

local function okbar_path(d, code, mkt, is_index)
  local name = mkt == "cn" and code or code:gsub("%.", "-"):gsub("/", "-")
  local sub = (util.truthy(is_index) or (mkt == "us" and util.contains_value({ "SPY", "QQQ", "DIA", "IWM" }, code))) and "index" or "daily"
  local p = path.join(d, sub, name .. ".okbar")
  if path.exists(p) then return p end
  return path.join(d, sub == "index" and "daily" or "index", name .. ".okbar")
end

local UPSERT = [[INSERT INTO symbols(market,code,symbol,name,name_zh_tw,board,list_date,delist_date,is_index,etf,first,last,st_ranges,price_basis)
                      VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)
                      ON CONFLICT(market,code) DO UPDATE SET symbol=excluded.symbol,name=excluded.name,name_zh_tw=excluded.name_zh_tw,
                      board=excluded.board,list_date=excluded.list_date,delist_date=excluded.delist_date,is_index=excluded.is_index,
                      etf=excluded.etf,first=excluded.first,last=excluded.last,st_ranges=excluded.st_ranges,price_basis=excluded.price_basis]]

local function load_market(db, root, mkt)
  local d = path.join(root, mkt)
  if not path.exists(path.join(d, "stocks.json")) then
    print(mkt .. ": no stocks.json, skipped")
    return
  end
  local stocks = ok.read_json(path.join(d, "stocks.json"))
  local manifest = path.exists(path.join(d, "manifest.json")) and ok.read_json(path.join(d, "manifest.json")) or json.object()
  local basis = manifest.price_basis
  if basis == nil then basis = "raw" end
  local cal = ok.read_json(path.join(d, "calendar.json"))
  local calrows = {}
  for i, x in ipairs(cal) do calrows[i] = { mkt, ymd(x) } end
  run(db, { { sql = "DELETE FROM calendar WHERE market=?", params = { mkt } },
    { sql = "BEGIN" }, { sql = "INSERT INTO calendar VALUES(?,?)", many = calrows }, { sql = "COMMIT" } })
  local t0, rows_total = time.time(), 0
  local n_stocks = #stocks
  -- Python commits (and reports) after symbol n when n % 500 == 0 (n counted from 0)
  local n = 0
  while n < n_stocks do
    local last = n == 0 and 0 or math.min(n_stocks - 1, n + 499)
    local ops, bar_ops = { { sql = "BEGIN" } }, {}
    for k = n, last do
      local s = stocks[k + 1]
      local code = s.code
      ops[#ops + 1] = { sql = UPSERT, params = { mkt, code, nul(s.symbol), nul(s.name), nul(s.name_zh_tw), nul(s.board), ymd(s.list_date),
        ymd(s.delist_date), util.truthy(s.is_index) and 1 or 0, util.truthy(s.etf) and 1 or 0, ymd(s.first),
        ymd(s.last), json.encode(util.truthy(s.st_ranges) and s.st_ranges or {}, { ensure_ascii = false }), basis } }
      local p = okbar_path(d, code, mkt, s.is_index)
      if not path.exists(p) then
        io.stderr:write(mkt .. " " .. code .. ": no .okbar\n")
      else
        ops[#ops + 1] = { okbar = p, market = mkt, code = code }
        bar_ops[#ops] = true
      end
    end
    ops[#ops + 1] = { sql = "COMMIT" }
    local res = run(db, ops)
    for i in pairs(bar_ops) do rows_total = rows_total + res[i].rows end
    if last % 500 == 0 then
      print(string.format("%s: %d/%d symbols, %d rows, %.0fs", mkt, last, n_stocks, rows_total, time.time() - t0))
      io.stdout:flush()
    end
    n = last + 1
  end
  local man = json.object()
  for _, k in ipairs(json.keys(manifest)) do
    if k ~= "files" and k ~= "rows" then man[k] = manifest[k] end
  end
  run(db, { { sql = "INSERT OR REPLACE INTO meta VALUES(?,?)", params = { "manifest." .. mkt, json.encode(man, { ensure_ascii = false }) } } })
  print(string.format("%s: %d symbols, %d rows", mkt, n_stocks, rows_total))
end

local function load_fx(db, root)
  local files = path.glob(path.join(root, "fx"), "*.okfx")
  table.sort(files)
  for _, f in ipairs(files) do
    local pair = path.stem(f) -- usd_twd, cny_twd
    run(db, { { sql = "BEGIN" }, { okfx = f, pair = pair }, { sql = "COMMIT" } })
    print("fx " .. pair .. ": done")
  end
end

local function win(p) return (path.norm(p):gsub("/", "\\")) end

local function main()
  local ap = ok.args.parser()
  ap:opt("--root", { default = path.join(ok.root, "Content/IndependentResources/MarketData") })
  ap:opt("--db", { default = nil })
  ap:opt("--markets", { default = nil, help = "default cn,us; cn only once Tools/build_us_actions.lua has loaded as-traded US prices" })
  local a = ap:parse(arg)
  local root = a.root
  local dbpath = a.db or path.join(root, "market.sqlite")
  local tmp = path.with_suffix(dbpath, ".sqlite.building")
  if path.exists(tmp) then path.remove(tmp) end
  if path.exists(dbpath) then -- keep tables other builders wrote (fundamentals, ...): rebuild on a copy
    path.copy(dbpath, tmp)
  end
  run(tmp, { { script = SCHEMA }, { sql = "INSERT OR REPLACE INTO meta VALUES(?,?)", params = { "schema_version", tostring(SCHEMA_VERSION) } } })
  local markets = a.markets
  if markets == nil then
    local done = #kit.sql_rows(tmp, "SELECT 1 FROM meta WHERE key='manifest.us_actions'") > 0
    markets = done and "cn" or "cn,us"
    if done then
      print("us: kept (as-traded prices from Tools/build_us_actions.lua); pass --markets cn,us to rebuild it, then run build_us_actions.lua load again")
    end
  end
  for _, m in ipairs(util.split(markets, ",")) do load_market(tmp, root, m) end
  load_fx(tmp, root)
  run(tmp, { { sql = "INSERT OR REPLACE INTO meta VALUES(?,?)", params = { "built_at", os.date("%Y-%m-%dT%H:%M:%S") } } })
  run(tmp, { { sql = "VACUUM" } })
  path.replace(tmp, dbpath)
  print(string.format("wrote %s (%.0f MB)", win(dbpath), path.size(dbpath) / 1e6))
end

ok.main(main)
