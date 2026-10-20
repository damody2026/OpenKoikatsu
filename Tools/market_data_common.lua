-- Shared helpers for the StreetSocial market-data scripts (build_*_data.lua, build_us_*.lua,
-- verify_*_data.lua, build_market_sqlite.lua, build_stock_names.lua).
-- Format: Docs/STREET_SOCIAL_MARKET_DATA_FORMAT.md
local ok = require "ok"
local path, kit, json, util = ok.path, ok.kit, ok.json, ok.util
local M = {}

M.HDR = "<c4I2I2I4I4"                          -- struct '<4sHHII'
M.ROW = "<i4i4i4i4i4i4i8dffi4i4B"              -- struct '<iiiiiiqdffiiB7x' (+7 pad bytes)
M.ROW_SIZE = 64

-- Rows of a decompressed .okbar body: {d,o,h,l,c,pc,vol,amount,split,div,lu,ld,flags} (1-based fields),
-- plus the header fields. reserved=true adds the 7 padding bytes as field 14.
function M.okbar_parse(data, reserved)
  local magic, ver, market, n, scale = string.unpack(M.HDR, data)
  local rows = {}
  local unpack = string.unpack
  for r = 0, n - 1 do
    local o = 17 + r * 64
    if o + 63 > #data then break end
    local d, op, h, l, c, pc, vol, amt, sr, dv, lu, ld, fl = unpack(M.ROW, data, o)
    rows[r + 1] = { d, op, h, l, c, pc, vol, amt, sr, dv, lu, ld, fl, reserved and data:sub(o + 57, o + 63) or nil }
  end
  return rows, { magic = magic, version = ver, market = market, rows = n, scale = scale }
end

-- Decompress many zstd files in one luakit call; fn(i, info, data) per file (data = first frame or nil).
-- info: {bytes, sha256, content_size, size, eof, trailing, error}. Files go through in chunks to bound memory.
function M.zstd_each(files, fn, chunk)
  chunk = chunk or 400
  local dir = path.temp(".zb")
  path.mkdir(dir, true)
  local i = 1
  while i <= #files do
    local spec = { files = {} }
    local last = math.min(#files, i + chunk - 1)
    for k = i, last do spec.files[#spec.files + 1] = { ["in"] = files[k], out = dir .. "/" .. (k - i) .. ".bin" } end
    local infos = kit.call({ "zstd-batch", "-" }, { input = json.encode(spec, { ensure_ascii = false }) })
    for k = i, last do
      local info = infos[k - i + 1]
      local out = dir .. "/" .. (k - i) .. ".bin"
      local data = (not info.error and path.exists(out)) and path.read_bytes(out) or nil
      path.remove(out, true)
      fn(k, info, data)
    end
    i = last + 1
  end
  path.rmtree(dir)
end

-- Rows of one .okbar file (python: zstandard decompressobj + struct).
function M.read_okbar(p)
  local data = kit.zstd_decompress_bytes(path.read_bytes(p))
  if data:sub(1, 4) ~= "OKBR" then error("bad header " .. tostring(p)) end
  return (M.okbar_parse(data))
end

-- Pack one row (13 values) like ROW.pack.
function M.pack_row(r)
  return string.pack(M.ROW, r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8], r[9], r[10], r[11], r[12], r[13]) .. string.rep("\0", 7)
end

-- zstd frame like python-zstandard ZstdCompressor(level=level).compress(data) (content size in the header).
function M.zstd_compress(data, level) return kit.zstd_compress_sized_bytes(data, level) end

-- Deferred zstd writes: w = md.zstd_writer(level); w:add(dst, data, fn(info)) ... w:flush()
-- (one multi-threaded luakit call per `batch` files; fn gets {bytes, sha256} of the written frame).
function M.zstd_writer(level, batch)
  local w = { level = level, batch = batch or 200, items = {}, dir = path.temp(".zw") }
  path.mkdir(w.dir, true)
  function w:add(dst, data, fn)
    local src = self.dir .. "/" .. (#self.items + 1) .. ".raw"
    path.write_bytes(src, data)
    self.items[#self.items + 1] = { src = src, dst = dst, fn = fn }
    if #self.items >= self.batch then self:flush() end
  end
  function w:flush()
    if #self.items == 0 then return end
    local spec = { level = self.level, files = {} }
    for i, it in ipairs(self.items) do
      path.mkdir(path.parent(it.dst), true)
      spec.files[i] = { ["in"] = it.src, out = path.norm(it.dst) }
    end
    local res = kit.call({ "zstd-c-batch", "-" }, { input = json.encode(spec, { ensure_ascii = false }) })
    for i, it in ipairs(self.items) do
      path.remove(it.src, true)
      if it.fn then it.fn(res[i]) end
    end
    self.items = {}
  end
  function w:close() self:flush(); path.rmtree(self.dir) end
  return w
end

-- time.strftime("%H:%M:%S") / log(*a) of the Python scripts.
function M.log(...)
  local parts = { os.date("%H:%M:%S") }
  for _, v in ipairs({ ... }) do parts[#parts + 1] = util.str(v) end
  io.write(table.concat(parts, " "), "\n")
  io.stdout:flush()
end

-- Seconds east of UTC for the local time zone (handed to luakit for its log timestamps).
function M.tz_offset()
  local t = os.time()
  return os.difftime(t, os.time(os.date("!*t", t)))
end

-- int YYYYMMDD <-> days since epoch, "YYYY-MM-DD" helpers.
function M.ymd_days(n) return ok.time.days_from_civil(n // 10000, n // 100 % 100, n % 100) end
function M.days_ymd(z) local y, m, d = ok.time.civil_from_days(z); return y * 10000 + m * 100 + d end
function M.add_days(n, k) return M.days_ymd(M.ymd_days(n) + k) end
function M.iso(n) local s = tostring(n); return s:sub(1, 4) .. "-" .. s:sub(5, 6) .. "-" .. s:sub(7, 8) end

-- Python float(s) for numeric strings (None for anything Python would reject).
function M.pyfloat(s)
  if type(s) == "number" then return s + 0.0 end
  if type(s) ~= "string" then return nil end
  local t = util.strip(s):lower():gsub("_", "")
  if t == "nan" or t == "+nan" or t == "-nan" then return 0 / 0 end
  if t == "inf" or t == "+inf" or t == "infinity" or t == "+infinity" then return math.huge end
  if t == "-inf" or t == "-infinity" then return -math.huge end
  if not t:match("^[+-]?%d*%.?%d*[eE]?[+-]?%d*$") or not t:find("%d") then return nil end
  if t:find("[eE]") and not t:match("[eE][+-]?%d+$") then return nil end
  local v = tonumber(t)
  return v and v + 0.0 or nil
end

-- Python int(float(x)) truncation.
function M.trunc(x)
  if x ~= x or x == math.huge or x == -math.huge then error("cannot convert float to integer") end
  return math.tointeger(x >= 0 and math.floor(x) or math.ceil(x)) or x
end

-- csv.writer(lineterminator="\n") rows -> gzip bytes; read back as DictReader rows.
local function csv_field(v)
  if v == nil or v == json.null then return "" end
  local s = util.str(v)
  if s:find('[",\n\r]') then return '"' .. s:gsub('"', '""') .. '"' end
  return s
end
function M.csv_text(header, rows)
  local out = {}
  local function line(r)
    local t = {}
    for i = 1, #r do t[i] = csv_field(r[i]) end
    -- csv.writer quotes a single empty field so the row is not lost
    if #r == 1 and t[1] == "" then t[1] = '""' end
    out[#out + 1] = table.concat(t, ",") .. "\n"
  end
  line(header)
  for _, r in ipairs(rows) do line(r) end
  return table.concat(out)
end
function M.write_atomic(p, data)
  path.mkdir(path.parent(p), true)
  local tmp = p .. ".part"
  path.write_bytes(tmp, data)
  path.replace(tmp, p)
end
function M.write_rows_gz(p, header, rows)
  M.write_atomic(p, kit.gzip_compress_bytes(M.csv_text(header, rows), 9))
end
function M.read_rows_gz(p)
  return ok.csv.parse_dicts(kit.gzip_decompress_bytes(path.read_bytes(p)))
end

-- str.splitlines() (no keepends).
local LINE_BREAKS = { ["\n"] = 1, ["\r"] = 1, ["\v"] = 1, ["\f"] = 1, ["\x1c"] = 1, ["\x1d"] = 1, ["\x1e"] = 1 }
function M.splitlines(s)
  if not s:find("[\r\v\f\x1c\x1d\x1e]") and not s:find("\xc2\x85", 1, true) and not s:find("\xe2\x80[\xa8\xa9]") then
    local out = {}
    for line in s:gmatch("([^\n]*)\n") do out[#out + 1] = line end
    local tail = s:match("[^\n]+$")
    if tail then out[#out + 1] = tail end
    return out
  end
  local out, cur = {}, {}
  local i, n = 1, #s
  while i <= n do
    local c = s:sub(i, i)
    if LINE_BREAKS[c] then
      out[#out + 1] = table.concat(cur); cur = {}
      if c == "\r" and s:sub(i + 1, i + 1) == "\n" then i = i + 1 end
      i = i + 1
    elseif c == "\xc2" and s:sub(i + 1, i + 1) == "\x85" then
      out[#out + 1] = table.concat(cur); cur = {}; i = i + 2
    elseif c == "\xe2" and (s:sub(i, i + 2) == "\u{2028}" or s:sub(i, i + 2) == "\u{2029}") then
      out[#out + 1] = table.concat(cur); cur = {}; i = i + 3
    else
      cur[#cur + 1] = c; i = i + 1
    end
  end
  if #cur > 0 then out[#out + 1] = table.concat(cur) end
  return out
end

-- datetime.now(timezone.utc).isoformat(timespec="seconds")
function M.utc_iso() return os.date("!%Y-%m-%dT%H:%M:%S") .. "+00:00" end

-- random.Random (MT19937) with the methods the scripts use: seed(int), getrandbits, random, sample.
function M.PyRandom(seed)
  local N, MM = 624, 397
  local mt, mti = {}, N + 1
  local function init_genrand(sd)
    mt[0] = sd & 0xffffffff
    for i = 1, N - 1 do
      local prev = mt[i - 1]
      mt[i] = (1812433253 * (prev ~ (prev >> 30)) + i) & 0xffffffff
    end
    mti = N
  end
  local function init_by_array(key)
    init_genrand(19650218)
    local i, j = 1, 0
    local k = math.max(N, #key)
    for _ = k, 1, -1 do
      local prev = mt[i - 1]
      mt[i] = ((mt[i] ~ ((prev ~ (prev >> 30)) * 1664525)) + key[j + 1] + j) & 0xffffffff
      i = i + 1; j = j + 1
      if i >= N then mt[0] = mt[N - 1]; i = 1 end
      if j >= #key then j = 0 end
    end
    for _ = N - 1, 1, -1 do
      local prev = mt[i - 1]
      mt[i] = ((mt[i] ~ ((prev ~ (prev >> 30)) * 1566083941)) - i) & 0xffffffff
      i = i + 1
      if i >= N then mt[0] = mt[N - 1]; i = 1 end
    end
    mt[0] = 0x80000000
  end
  local function genrand()
    if mti >= N then
      for kk = 0, N - 1 do
        local y = (mt[kk] & 0x80000000) | (mt[(kk + 1) % N] & 0x7fffffff)
        mt[kk] = mt[(kk + MM) % N] ~ (y >> 1) ~ ((y & 1) ~= 0 and 0x9908b0df or 0)
      end
      mti = 0
    end
    local y = mt[mti]
    mti = mti + 1
    y = y ~ (y >> 11)
    y = y ~ ((y << 7) & 0x9d2c5680)
    y = y ~ ((y << 15) & 0xefc60000)
    y = y ~ (y >> 18)
    return y & 0xffffffff
  end
  local key = {}
  local a = math.abs(seed)
  if a == 0 then key = { 0 } else while a > 0 do key[#key + 1] = a & 0xffffffff; a = a >> 32 end end
  init_by_array(key)
  local R = {}
  function R.getrandbits(k)
    if k <= 32 then return genrand() >> (32 - k) end
    local out, shift = 0, 0
    while k > 0 do
      local w = genrand()
      if k < 32 then w = w >> (32 - k) end
      out = out | (w << shift)
      shift = shift + 32; k = k - 32
    end
    return out
  end
  function R.random()
    local a2, b2 = genrand() >> 5, genrand() >> 6
    return (a2 * 67108864.0 + b2) * (1.0 / 9007199254740992.0)
  end
  local function bit_length(n) local k = 0; while n > 0 do k = k + 1; n = n >> 1 end; return k end
  function R.randbelow(n)
    local k = bit_length(n)
    local r = R.getrandbits(k)
    while r >= n do r = R.getrandbits(k) end
    return r
  end
  function R.sample(pop, k)
    local n = #pop
    if k < 0 or k > n then error("ValueError: Sample larger than population or is negative") end
    local result = {}
    local setsize = 21
    if k > 5 then setsize = setsize + 4 ^ math.ceil(math.log(k * 3) / math.log(4)) end
    if n <= setsize then
      local pool = {}
      for i = 1, n do pool[i] = pop[i] end
      for i = 0, k - 1 do
        local j = R.randbelow(n - i)
        result[i + 1] = pool[j + 1]
        pool[j + 1] = pool[n - i]
      end
    else
      local selected = {}
      for i = 1, k do
        local j = R.randbelow(n)
        while selected[j] do j = R.randbelow(n) end
        selected[j] = true
        result[i] = pop[j + 1]
      end
    end
    return result
  end
  return R
end

return M
