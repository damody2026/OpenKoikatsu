-- Shared helpers for the GamePoseProbe verifiers (Lua ports of the former
-- Python scripts). Keeps Python semantics that the reports depend on:
-- ordered dicts, int/float distinction, utf-8-sig reads, sha256 digests,
-- set comparisons and PIL contact sheets (pixel-identical via ok-luakit).
local ok = require "ok"
local json, path, util, re = ok.json, ok.path, ok.util, ok.re

local M = {}
M.ROOT = ok.root
M.LOGS = ok.root .. "/Saved/Logs"
M.null = json.null
M.re = re

-- Ordered JSON object from alternating key/value arguments: O("a", 1, "b", 2).
function M.O(...)
  local o = json.object()
  local n = select("#", ...)
  for i = 1, n, 2 do
    local k, v = select(i, ...)
    if v == nil then v = json.null end
    o[k] = v
  end
  return o
end
-- JSON array (keeps [] for empty lists).
function M.A(t) return json.array(t or {}) end

-- Python float()/int() of strings.
function M.F(s) local v = tonumber(s); if v == nil then error("could not convert string to float: " .. tostring(s), 2) end; return v + 0.0 end
function M.I(s)
  local v = math.tointeger(tonumber(s))
  if v == nil then error("invalid literal for int(): " .. tostring(s), 2) end
  return v
end

-- path.read_text(encoding='utf-8-sig')
function M.read(p) return path.read_text(p) end
function M.read_json(p) return json.decode(path.read_text(p)) end
-- hashlib.sha256(path.read_bytes()).hexdigest()
function M.digest(p) return ok.hash.sha256_file(p) end
function M.join(...) return path.join(...) end
function M.exists(p) return path.exists(p) end
function M.mtime(p)
  local m = path.mtime(p)
  if m == nil then error("No such file or directory: " .. tostring(p), 2) end
  return m
end
-- Write like Path.write_text(text, encoding='utf-8') on Windows.
function M.write_text(p, s) path.write_text(p, s) end
-- json.dumps(v, indent=2[, ensure_ascii=False])
function M.dumps(v, opts) return json.encode(v, opts) end
function M.write_json(p, v, ensure_ascii)
  path.write_text(p, json.encode(v, { indent = 2, ensure_ascii = ensure_ascii and true or false }))
end
function M.print(s) io.write(s, "\n") end

-- Python str(Path) on Windows for printing.
function M.winpath(p) return (tostring(p):gsub("/", "\\")) end

-- re.sub(r'[^A-Za-z0-9_]', '_', s) per code point.
function M.sanitize(s)
  local out = {}
  for _, c in utf8.codes(s) do
    local ch = utf8.char(c)
    out[#out + 1] = ch:match("^[A-Za-z0-9_]$") and ch or "_"
  end
  return table.concat(out)
end
-- ''.join(c if c.isascii() and c.isalnum() else '_' for c in s)[:n]
function M.ascii_stem(s, n)
  local out, count = {}, 0
  for _, c in utf8.codes(s) do
    local ch = utf8.char(c)
    out[#out + 1] = ch:match("^[A-Za-z0-9]$") and ch or "_"
    count = count + 1
    if n and count >= n then break end
  end
  return table.concat(out)
end

-- Sets ---------------------------------------------------------------------
-- key(v): tuple/list -> stable string key; scalars keep Python equality (1 == 1.0).
local function key(v)
  if type(v) == "table" then
    local parts = {}
    for i = 1, #v do parts[i] = key(v[i]) end
    return "(" .. table.concat(parts, "\31") .. ")"
  end
  if type(v) == "number" then
    if v == math.floor(v) and math.abs(v) < 2 ^ 53 then return "n" .. string.format("%d", math.floor(v)) end
    return "n" .. json.float_repr(v)
  end
  if type(v) == "boolean" then return "n" .. (v and "1" or "0") end
  return "s" .. tostring(v)
end
M.key = key
-- set(list) -> {key -> value}
function M.set(list)
  local s = {}
  for _, v in ipairs(list) do s[key(v)] = v end
  return s
end
function M.set_len(s) local n = 0; for _ in pairs(s) do n = n + 1 end; return n end
function M.set_eq(a, b)
  if a[1] ~= nil or next(a) == nil then a = M.set(a) end
  if b[1] ~= nil or next(b) == nil then b = M.set(b) end
  for k in pairs(a) do if b[k] == nil then return false end end
  for k in pairs(b) do if a[k] == nil then return false end end
  return true
end
-- {f(x) for x in list} as list of unique values (first occurrence order).
function M.unique(list, f)
  local seen, out = {}, {}
  for _, v in ipairs(list) do
    local x; if f then x = f(v) else x = v end
    local k = key(x)
    if not seen[k] then seen[k] = true; out[#out + 1] = x end
  end
  return out
end
function M.contains(list, x) for _, v in ipairs(list) do if key(v) == key(x) then return true end end return false end

-- Assertions: assert c, msg -> raise AssertionError (exit 1).
function M.check(cond, msg)
  if not cond then error("AssertionError" .. (msg ~= nil and (": " .. util.str(msg)) or ""), 2) end
  return cond
end

-- Counting and regex helpers -------------------------------------------------
function M.count(s, sub) return util.count(s, sub) end
function M.has(s, sub) return s:find(sub, 1, true) ~= nil end
function M.findall(p, s) return re.findall(p, s) end
function M.search(p, s) return re.search(p, s) end
function M.floats(list) local out = {}; for i, v in ipairs(list) do out[i] = M.F(v) end; return out end
function M.max(list, f)
  local best
  for _, v in ipairs(list) do local x; if f then x = f(v) else x = v end; if best == nil or x > best then best = x end end
  if best == nil then error("max() arg is an empty sequence", 2) end
  return best
end
function M.min(list, f)
  local best
  for _, v in ipairs(list) do local x; if f then x = f(v) else x = v end; if best == nil or x < best then best = x end end
  if best == nil then error("min() arg is an empty sequence", 2) end
  return best
end
-- Python 3.12 sum(): integers exactly, then Neumaier-compensated float addition.
function M.sum(list, f)
  local s, c, floating = 0, 0.0, false
  for _, v in ipairs(list) do
    local x; if f then x = f(v) else x = v end
    if type(x) == "boolean" then x = x and 1 or 0 end
    if not floating then
      if math.type(x) == "float" then floating = true; s = s + x else s = s + x end
    elseif math.type(x) == "float" then
      local t = s + x
      if math.abs(s) >= math.abs(x) then c = c + ((s - t) + x) else c = c + ((x - t) + s) end
      s = t
    else
      s = s + x
    end
  end
  if floating and c ~= 0 and c == c and c ~= math.huge and c ~= -math.huge then s = s + c end
  return s
end
function M.all(list, f) for _, v in ipairs(list) do if not f(v) then return false end end return true end
function M.any(list, f) for _, v in ipairs(list) do if f(v) then return true end end return false end
function M.map(list, f) local out = {}; for i, v in ipairs(list) do out[i] = f(v, i) end; return out end
function M.filter(list, f) local out = {}; for _, v in ipairs(list) do if f(v) then out[#out + 1] = v end end; return out end
function M.isfinite(x) return type(x) == "number" and x == x and x ~= math.huge and x ~= -math.huge end
-- Arrays of regex tuples as JSON arrays.
function M.rows(list) return json.array(list) end

-- File listings ----------------------------------------------------------------
-- {p.name for p in folder.glob(pattern)}
function M.glob_names(dir, pattern)
  local out = {}
  for _, p in ipairs(path.glob(dir, pattern)) do out[#out + 1] = path.name(p) end
  return out
end
-- {p.name for p in folder.iterdir() if p.is_file()}
function M.file_names(dir)
  local out = {}
  for _, n in ipairs(path.list(dir)) do if path.is_file(path.join(dir, n)) then out[#out + 1] = n end end
  return out
end
function M.sorted(list, keyf, reverse) return util.sorted(list, keyf, reverse) end
-- sorted(list_of_WindowsPath): pathlib compares case-folded parts on Windows.
function M.sort_paths(list)
  return util.sorted(list, function(p)
    local parts = {}
    for s in path.norm(p):lower():gmatch("[^/]+") do parts[#parts + 1] = s end
    return parts
  end)
end
-- Unsorted Path.glob / iterdir order on NTFS (directory B-tree: upper-cased names).
function M.ntfs_sorted(list) return util.sorted(list, function(p) return path.name(p):upper() end) end
-- sorted(folder.glob(pattern)) as full paths in pathlib order.
function M.glob_sorted(dir, pattern) return M.sort_paths(path.glob(dir, pattern)) end

-- Images -------------------------------------------------------------------------
-- Image.open(p).size
function M.size(p)
  local w, h = ok.image.size(p)
  return w, h
end
function M.size_is(p, w, h) local a, b = M.size(p); return a == w and b == h end
function M.load_rgb(p) return ok.image.load(p, "RGB") end
-- np.where(max|rgb - rgb[0,0]| > t) -> {xmin, ymin, xmax, ymax} or nil
function M.fg_bounds(p, t)
  local r = ok.kit.call({ "probe", "fg-bounds", p, t })
  if r.bounds == json.null then return nil, r end
  return r.bounds, r
end
-- count(max|a - b| > t) over RGB
function M.diff_count(a, b, t) return ok.kit.call({ "probe", "diff-count", a, b, t }).count end

-- Contact sheet with ImageDraw.text + LANCZOS thumbnails, written by ok-luakit.
local Sheet = {}
Sheet.__index = Sheet
-- Image.new('RGB', (w, h), '#303030') -> M.sheet(w, h, {0x30,0x30,0x30})
function M.sheet(w, h, bg) return setmetatable({ w = w, h = h, bg = bg or { 0, 0, 0 }, items = {}, texts = {} }, Sheet) end
function Sheet:text(x, y, s, fill) self.texts[#self.texts + 1] = { x = x, y = y, text = s, fill = fill or { 255, 255, 255 } } end
-- sheet.paste(Image.open(file).convert('RGB').resize((w, h), LANCZOS), (x, y))
function Sheet:paste(file, x, y, w, h) self.items[#self.items + 1] = { file = file, x = x, y = y, w = w, h = h } end
function Sheet:save(out)
  local spec = { width = self.w, height = self.h, background = self.bg, items = json.array(self.items),
    texts = json.array(self.texts), output = out }
  ok.kit.call({ "probe", "contact-sheet", "-" }, { input = json.encode(spec, { ensure_ascii = false }) })
  return out
end

-- The neutral backdrop checks shared by the animator verifiers.
-- returns neutral rgb of (0,0) and a function rgb(x, y).
function M.pixels(p)
  local img = ok.image.load(p, "RGB")
  return img, function(x, y) return img:get(x, y) end
end
function M.close_rgb(r, g, b, n, tol)
  return math.max(math.abs(r - n[1]), math.abs(g - n[2]), math.abs(b - n[3])) <= (tol or 1)
end
-- all(max(|im.getpixel((x,0)) - neutral|) <= 1 for x in range(width))
function M.top_row_neutral(img, neutral)
  for x = 0, img.width - 1 do
    local r, g, b = img:get(x, 0)
    if not M.close_rgb(r, g, b, neutral) then return false end
  end
  return true
end

-- numpy float64 reductions --------------------------------------------------
-- np.add.reduce over a contiguous 1-D array: pairwise summation (blocks of 8,
-- leaves of <= 128), added to 0.0. Verified bit-for-bit against numpy 2.x.
local function pairwise(a, lo, n)
  if n < 8 then
    local r = 0.0
    for i = lo, lo + n - 1 do r = r + a[i] end
    return r
  elseif n <= 128 then
    local r1, r2, r3, r4, r5, r6, r7, r8 = a[lo], a[lo + 1], a[lo + 2], a[lo + 3], a[lo + 4], a[lo + 5], a[lo + 6], a[lo + 7]
    local i = 8
    local stop = n - n % 8
    while i < stop do
      local b = lo + i
      r1, r2, r3, r4 = r1 + a[b], r2 + a[b + 1], r3 + a[b + 2], r4 + a[b + 3]
      r5, r6, r7, r8 = r5 + a[b + 4], r6 + a[b + 5], r7 + a[b + 6], r8 + a[b + 7]
      i = i + 8
    end
    local res = ((r1 + r2) + (r3 + r4)) + ((r5 + r6) + (r7 + r8))
    while i < n do res = res + a[lo + i]; i = i + 1 end
    return res
  end
  local n2 = n // 2
  n2 = n2 - n2 % 8
  return pairwise(a, lo, n2) + pairwise(a, lo + n2, n - n2)
end
function M.np_sum(list) return 0.0 + pairwise(list, 1, #list) end
-- arr.mean() of a 1-D float array (or a flattened C-order array).
function M.np_mean(list) return M.np_sum(list) / #list end
-- arr.mean(axis=0) of an (n, k) C-order array: sequential row accumulation.
function M.np_mean_axis0(rows, k)
  local acc = {}
  for j = 1, k do acc[j] = rows[1][j] + 0.0 end
  for i = 2, #rows do local r = rows[i]; for j = 1, k do acc[j] = acc[j] + r[j] end end
  for j = 1, k do acc[j] = acc[j] / #rows end
  return acc
end
-- Flatten rows (k columns each) in C order.
function M.flatten(rows, k)
  local out = {}
  for _, r in ipairs(rows) do for j = 1, k do out[#out + 1] = r[j] end end
  return out
end
-- arr.max() / max(axis=0) for floats (NaN propagates like numpy).
function M.np_max(list)
  local m
  for _, v in ipairs(list) do
    if v ~= v then return v end
    if m == nil or v > m then m = v end
  end
  if m == nil then error("ValueError: zero-size array to reduction operation maximum which has no identity", 2) end
  return m
end

-- np.fromfile(p, dtype="<f4").reshape(-1, k) as rows of Lua floats.
function M.read_f32(p, k)
  local data = path.read_bytes(p)
  local n = #data // 4
  local rows, row = {}, {}
  local fmt = "<" .. string.rep("f", math.min(n, 64))
  local i = 1
  local vals = {}
  while i <= n * 4 do
    local count = math.min(64, (n * 4 - i + 1) // 4)
    local chunk = { string.unpack("<" .. string.rep("f", count), data, i) }
    for c = 1, count do vals[#vals + 1] = chunk[c] end
    i = i + count * 4
  end
  if #vals % k ~= 0 then error("ValueError: cannot reshape array of size " .. #vals .. " into shape (-1," .. k .. ")", 2) end
  for j = 1, #vals, k do
    row = {}
    for c = 0, k - 1 do row[c + 1] = vals[j + c] end
    rows[#rows + 1] = row
  end
  return rows
end

-- Python dict(sorted(...)) helper: ordered object from sorted keys.
function M.sorted_object(keys, f)
  local o = json.object()
  for _, k in ipairs(util.sorted(keys)) do o[k] = f(k) end
  return o
end

-- {name: digest(base/name) for name in list} preserving order.
function M.digests(base, names)
  local o = json.object()
  for _, n in ipairs(names) do o[n] = M.digest(path.join(base, n)) end
  return o
end

-- relative_to(ROOT).as_posix()
function M.rel(p) return path.rel(p, M.ROOT) end

return M
