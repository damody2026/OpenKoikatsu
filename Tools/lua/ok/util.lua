-- Python-semantics helpers that Lua lacks or does differently.
local json = require "ok.json"
local M = {}

-- str()/repr() of numbers like Python (1 -> "1", 1.0 -> "1.0", 0.1 -> "0.1").
function M.num(v)
  if math.type(v) == "integer" then return string.format("%d", v) end
  return json.float_repr(v)
end
-- Python str() for common values (True/False/None, lists via repr).
function M.str(v)
  local t = type(v)
  if v == nil or v == json.null then return "None" end
  if t == "boolean" then return v and "True" or "False" end
  if t == "number" then return M.num(v) end
  if t == "string" then return v end
  return M.repr(v)
end
function M.repr(v)
  local t = type(v)
  if t == "string" then
    local q = (v:find("'") and not v:find('"')) and '"' or "'"
    return q .. v:gsub("\\", "\\\\"):gsub("\n", "\\n"):gsub("\r", "\\r"):gsub("\t", "\\t"):gsub(q, "\\" .. q) .. q
  end
  if t ~= "table" or v == json.null then return M.str(v) end
  if json.is_array(v) or (not json.is_object(v) and (#v > 0 or next(v) == nil)) then
    local parts = {}
    for i = 1, #v do parts[i] = M.repr(v[i]) end
    return "[" .. table.concat(parts, ", ") .. "]"
  end
  local parts = {}
  for _, k in ipairs(json.keys(v)) do parts[#parts + 1] = M.repr(k) .. ": " .. M.repr(v[k]) end
  return "{" .. table.concat(parts, ", ") .. "}"
end

-- Python float(str): accepts surrounding whitespace, inf/infinity/nan with sign, "_"-free decimals; errors like ValueError.
function M.float(s)
  if type(s) == "number" then return s + 0.0 end
  local t = tostring(s):match("^%s*(.-)%s*$")
  local sign, word = t:match("^([+-]?)(%a+)$")
  if word then
    word = word:lower()
    if word == "inf" or word == "infinity" then return sign == "-" and -math.huge or math.huge end
    if word == "nan" then return 0 / 0 end
  end
  if t:match("^[+-]?%d*%.?%d*[eE]?[+-]?%d*$") and t:match("%d") then
    local v = tonumber(t)
    if v then return v + 0.0 end
  end
  error(("could not convert string to float: '%s'"):format(tostring(s)), 2)
end

-- Python round(): banker's rounding; round(x) -> integer, round(x, n) -> float.
function M.round(x, n)
  if n == nil or n == 0 then
    local f = math.floor(x)
    local d = x - f
    local r
    if d > 0.5 then r = f + 1 elseif d < 0.5 then r = f else r = (f % 2 == 0) and f or f + 1 end
    r = math.tointeger(r) or r
    if n == nil then return r end
    return r + 0.0
  end
  -- CPython rounds the exact binary value (ties to even); %.nf does the same.
  return tonumber(string.format("%." .. n .. "f", x)) + 0.0
end

-- Python // and % on integers/floats (Lua's // and % already floor; kept for clarity).
function M.floordiv(a, b) return a // b end
function M.divmod(a, b) return a // b, a % b end

-- String helpers.
function M.split(s, sep, maxsplit)
  local out = {}
  if sep == nil then
    for w in s:gmatch("%S+") do out[#out + 1] = w end
    return out
  end
  local start, count = 1, 0
  while true do
    if maxsplit and count >= maxsplit then break end
    local a, b = s:find(sep, start, true)
    if not a then break end
    out[#out + 1] = s:sub(start, a - 1); start = b + 1; count = count + 1
  end
  out[#out + 1] = s:sub(start)
  return out
end
function M.strip(s, chars)
  if chars then local c = chars:gsub("%p", "%%%0"); return (s:gsub("^[" .. c .. "]+", ""):gsub("[" .. c .. "]+$", "")) end
  return (s:gsub("^%s+", ""):gsub("%s+$", ""))
end
function M.lstrip(s) return (s:gsub("^%s+", "")) end
function M.rstrip(s) return (s:gsub("%s+$", "")) end
function M.startswith(s, p) return s:sub(1, #p) == p end
function M.endswith(s, p) return p == "" or s:sub(-#p) == p end
function M.replace(s, a, b) return (s:gsub(a:gsub("%p", "%%%0"), (b:gsub("%%", "%%%%")))) end
function M.count(s, sub) local n, i = 0, 1; while true do local a, b = s:find(sub, i, true); if not a then return n end; n = n + 1; i = b + 1 end end
function M.find(s, sub, init) local a = s:find(sub, init or 1, true); return a end
function M.contains(s, sub) return s:find(sub, 1, true) ~= nil end
function M.splitlines(s) local t = {}; for l in (s .. "\n"):gmatch("(.-)\r?\n") do t[#t + 1] = l end; if t[#t] == "" then t[#t] = nil end; return t end
function M.join(sep, list) local t = {}; for i, v in ipairs(list) do t[i] = M.str(v) end; return table.concat(t, sep) end
function M.zfill(s, n) s = tostring(s); return string.rep("0", n - #s) .. s end
function M.upper(s) return s:upper() end
function M.lower(s) return s:lower() end
function M.isdigit(s) return s ~= "" and not s:find("%D") end
function M.ljust(s, n, c) return s .. string.rep(c or " ", n - utf8.len(s)) end
function M.rjust(s, n, c) return string.rep(c or " ", n - utf8.len(s)) .. s end
-- Python str.isascii() and str.isalnum() for ASCII.
function M.isascii(s) return not s:find("[\128-\255]") end
-- Iterate UTF-8 characters.
function M.chars(s) local t = {}; for _, c in utf8.codes(s) do t[#t + 1] = utf8.char(c) end; return t end
function M.ulen(s) return utf8.len(s) end
function M.usub(s, i, j)
  local n = utf8.len(s)
  i = i or 1; j = j or n
  if i < 0 then i = n + i + 1 end
  if j < 0 then j = n + j + 1 end
  if i > j or i > n then return "" end
  local a = utf8.offset(s, i)
  local b = utf8.offset(s, j + 1)
  return s:sub(a, (b or #s + 1) - 1)
end

-- Table helpers.
function M.keys(t) return json.keys(t) end
function M.sorted_keys(t) local k = {}; for key in pairs(t) do k[#k + 1] = key end; table.sort(k, function(a, b) if type(a) == type(b) then return a < b end return tostring(a) < tostring(b) end); return k end
function M.values(t) local out = {}; for _, k in ipairs(json.keys(t)) do out[#out + 1] = t[k] end; return out end
function M.len(t) if type(t) == "string" then return utf8.len(t) or #t end local n = 0; for _ in pairs(t) do n = n + 1 end; return n end
function M.map(t, f) local out = {}; for i, v in ipairs(t) do out[i] = f(v, i) end; return out end
function M.filter(t, f) local out = {}; for i, v in ipairs(t) do if f(v, i) then out[#out + 1] = v end end; return out end
function M.sum(t, f) local s = 0; for i, v in ipairs(t) do s = s + (f and f(v, i) or v) end; return s end
-- Python 3.12+ builtin sum(): integers add exactly; once a float appears the rest
-- is Neumaier-compensated (CPython builtin_sum_impl), so float totals match Python.
function M.pysum(t, start)
  local result, i, n = start or 0, 1, #t
  while i <= n and math.type(result) == "integer" and math.type(t[i]) == "integer" do result = result + t[i]; i = i + 1 end
  if i > n then return result end
  local f, c = result + t[i], 0.0
  for j = i + 1, n do
    local x = t[j]
    if math.type(x) == "integer" then f = f + x
    else
      local s = f + x
      if math.abs(f) >= math.abs(x) then c = c + ((f - s) + x) else c = c + ((x - s) + f) end
      f = s
    end
  end
  if c ~= 0 and c == c and c ~= math.huge and c ~= -math.huge then f = f + c end
  return f
end
function M.any(t, f) for i, v in ipairs(t) do if (f == nil and v) or (f and f(v, i)) then return true end end; return false end
function M.all(t, f) for i, v in ipairs(t) do if not ((f == nil and v) or (f and f(v, i))) then return false end end; return true end
function M.min(t, f) local best, bv; for _, v in ipairs(t) do local k = f and f(v) or v; if bv == nil or k < bv then best, bv = v, k end end; return best end
function M.max(t, f) local best, bv; for _, v in ipairs(t) do local k = f and f(v) or v; if bv == nil or k > bv then best, bv = v, k end end; return best end
function M.contains_value(t, x) for _, v in ipairs(t) do if v == x then return true end end; return false end
M.index_of = function(t, x) for i, v in ipairs(t) do if v == x then return i end end; return nil end
function M.slice(t, i, j) local out = {}; local n = #t; i = i or 1; j = j or n; if i < 0 then i = n + i + 1 end; if j < 0 then j = n + j + 1 end; for k = math.max(i, 1), math.min(j, n) do out[#out + 1] = t[k] end; return out end
function M.extend(t, more) for _, v in ipairs(more) do t[#t + 1] = v end; return t end
function M.reverse(t) local out = {}; for i = #t, 1, -1 do out[#out + 1] = t[i] end; return out end
function M.range(a, b, step) if b == nil then a, b = 0, a end; step = step or 1; local out = {}; for i = a, b - (step > 0 and 1 or -1), step do out[#out + 1] = i end; return out end
function M.set(list) local s = {}; for _, v in ipairs(list or {}) do s[v] = true end; return s end
function M.set_list(s) local out = {}; for k in pairs(s) do out[#out + 1] = k end; table.sort(out, function(a, b) return tostring(a) < tostring(b) end); return out end
function M.unique(list) local seen, out = {}, {}; for _, v in ipairs(list) do if not seen[v] then seen[v] = true; out[#out + 1] = v end end; return out end
function M.counter(list, f) local c = {}; for _, v in ipairs(list) do local k = f and f(v) or v; c[k] = (c[k] or 0) + 1 end; return c end
function M.group_by(list, f) local g = {}; for _, v in ipairs(list) do local k = f(v); g[k] = g[k] or {}; table.insert(g[k], v) end; return g end
function M.copy(t) local o = {}; for k, v in pairs(t) do o[k] = v end; return setmetatable(o, getmetatable(t)) end
function M.deepcopy(t)
  if type(t) ~= "table" or t == json.null then return t end
  if json.is_object(t) then local o = json.object(); for _, k in ipairs(json.keys(t)) do o[k] = M.deepcopy(t[k]) end; return o end
  local o = {}
  for k, v in pairs(t) do o[k] = M.deepcopy(v) end
  return setmetatable(o, getmetatable(t))
end
-- Stable sort (Python sorted is stable; table.sort is not). key(v) optional, reverse optional.
function M.sorted(t, key, reverse)
  local idx = {}
  for i, v in ipairs(t) do idx[i] = { v = v, i = i, k = key and key(v) or v } end
  local function lt(a, b)
    return M.compare(a, b) < 0
  end
  table.sort(idx, function(a, b)
    local c = M.compare(a.k, b.k)
    if c ~= 0 then if reverse then return c > 0 end return c < 0 end
    return a.i < b.i
  end)
  local out = {}
  for i, e in ipairs(idx) do out[i] = e.v end
  return out
end
-- Python-style comparison (numbers, strings, booleans, arrays lexicographic).
function M.compare(a, b)
  local ta, tb = type(a), type(b)
  if ta == "table" and tb == "table" then
    for i = 1, math.max(#a, #b) do
      if a[i] == nil then return -1 end
      if b[i] == nil then return 1 end
      local c = M.compare(a[i], b[i])
      if c ~= 0 then return c end
    end
    return 0
  end
  if ta == "boolean" then a = a and 1 or 0 end
  if tb == "boolean" then b = b and 1 or 0 end
  if a == nil and b == nil then return 0 end
  if a == nil then return -1 end
  if b == nil then return 1 end
  if type(a) ~= type(b) then a, b = tostring(a), tostring(b) end
  if a < b then return -1 elseif a > b then return 1 end
  return 0
end
-- Deep structural equality (arrays/objects/scalars; floats compared exactly).
function M.equal(a, b)
  if a == b then return true end
  if type(a) ~= "table" or type(b) ~= "table" then return false end
  for k, v in pairs(a) do if not M.equal(v, b[k]) then return false end end
  for k in pairs(b) do if a[k] == nil then return false end end
  return true
end
function M.isclose(a, b, rel, abs) rel = rel or 1e-9; abs = abs or 0; return math.abs(a - b) <= math.max(rel * math.max(math.abs(a), math.abs(b)), abs) end

-- Python truthiness: None/False/0/""/empty containers are falsy.
function M.truthy(v)
  if v == nil or v == false or v == json.null or v == 0 or v == "" then return false end
  if type(v) == "table" then return next(v) ~= nil end
  return true
end

-- Python "%.6g"-like and f-string formats are string.format; thousands separator helper:
function M.commas(n) local s = M.num(n); local int, rest = s:match("^(-?%d+)(.*)$"); int = int:reverse():gsub("(%d%d%d)", "%1,"):reverse():gsub("^(-?),", "%1"); return int .. rest end

-- Assertion like Python assert with message (raises error with the message).
function M.check(cond, msg, ...) if not cond then error(msg and string.format(tostring(msg), ...) or "assertion failed", 2) end; return cond end

-- Hex encode/decode of byte strings.
function M.hex(s) return (s:gsub(".", function(c) return string.format("%02x", c:byte()) end)) end
function M.unhex(h) return (h:gsub("%x%x", function(x) return string.char(tonumber(x, 16)) end)) end

-- Base64 (standard alphabet, padding).
local b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
local b64enc, b64dec = {}, {}
for i = 1, 64 do b64enc[i - 1] = b64:byte(i); b64dec[b64:byte(i)] = i - 1 end
function M.b64encode(s)
  local out, n, char = {}, #s, string.char
  local chunk = {}
  for i = 1, n, 3 do
    local a, b, c = s:byte(i, i + 2)
    local v = (a << 16) | ((b or 0) << 8) | (c or 0)
    chunk[#chunk + 1] = char(b64enc[(v >> 18) & 63], b64enc[(v >> 12) & 63],
      b and b64enc[(v >> 6) & 63] or 61, c and b64enc[v & 63] or 61)
    if #chunk == 4096 then out[#out + 1] = table.concat(chunk); chunk = {} end
  end
  out[#out + 1] = table.concat(chunk)
  return table.concat(out)
end
function M.b64decode(s)
  s = s:gsub("[^%w%+/]", "")
  local out, chunk, char = {}, {}, string.char
  local n = #s
  for i = 1, n, 4 do
    local a, b, c, d = s:byte(i, i + 3)
    local v = (b64dec[a] << 18) | ((b and b64dec[b] or 0) << 12) | ((c and b64dec[c] or 0) << 6) | (d and b64dec[d] or 0)
    if d then chunk[#chunk + 1] = char((v >> 16) & 255, (v >> 8) & 255, v & 255)
    elseif c then chunk[#chunk + 1] = char((v >> 16) & 255, (v >> 8) & 255)
    elseif b then chunk[#chunk + 1] = char((v >> 16) & 255) end
    if #chunk == 4096 then out[#out + 1] = table.concat(chunk); chunk = {} end
  end
  out[#out + 1] = table.concat(chunk)
  return table.concat(out)
end

return M
