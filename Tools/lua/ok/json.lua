-- JSON with Python json-module semantics: ordered objects, explicit arrays,
-- float repr identical to Python, ensure_ascii/indent/sort_keys/separators.
local M = {}

local byte, sub, find, char, format, concat = string.byte, string.sub, string.find, string.char, string.format, table.concat
local mtype, floor = math.type, math.floor

M.null = setmetatable({}, { __tostring = function() return "null" end, __name = "json.null" })

-- Ordered object -------------------------------------------------------------
local ORDER = setmetatable({}, { __mode = "k" })
-- POS[t][k] = index of k in ORDER[t]; a re-added key leaves a GONE hole behind
-- (O(1) inserts, so decoding objects with 100k keys stays linear).
local POS = setmetatable({}, { __mode = "k" })
local GONE = {}
local OBJ, ARR = {}, {}

local function opairs(t)
  local keys, i = ORDER[t], 0
  return function()
    while true do
      i = i + 1
      local k = keys[i]
      if k == nil then return nil end
      if k ~= GONE then
        local v = rawget(t, k)
        if v ~= nil then return k, v end
      end
    end
  end
end
OBJ.__name = "json.object"
OBJ.__newindex = function(t, k, v)
  if v == nil then return end
  local keys, pos = ORDER[t], POS[t]
  local old = pos[k]
  if old then keys[old] = GONE end
  keys[#keys + 1] = k
  pos[k] = #keys
  rawset(t, k, v)
end
OBJ.__pairs = function(t) return opairs(t), t, nil end
ARR.__name = "json.array"

-- object([{k1, v1}, {k2, v2}, ...]) or object() ; keeps insertion order.
function M.object(pairs_list)
  local t = setmetatable({}, OBJ); ORDER[t] = {}; POS[t] = {}
  if pairs_list then for _, kv in ipairs(pairs_list) do t[kv[1]] = kv[2] end end
  return t
end
-- array(t) marks a Lua sequence as a JSON array (also makes {} encode as []).
function M.array(t) return setmetatable(t or {}, ARR) end
function M.is_object(t) return getmetatable(t) == OBJ end
function M.is_array(t) return getmetatable(t) == ARR end
-- Ordered keys of an object (or sorted keys of a plain table).
function M.keys(t)
  local out = {}
  if getmetatable(t) == OBJ then for k in opairs(t) do out[#out + 1] = k end
  else for k in pairs(t) do out[#out + 1] = k end; table.sort(out, function(a, b) return tostring(a) < tostring(b) end) end
  return out
end
-- Remove a key from an ordered object (t[k] = nil also works).
function M.remove(t, k) local v = rawget(t, k); rawset(t, k, nil); return v end

-- Python float repr ----------------------------------------------------------
function M.float_repr(x)
  if x ~= x then return "NaN" end
  if x == math.huge then return "Infinity" end
  if x == -math.huge then return "-Infinity" end
  if x == 0 then return (1 / x < 0) and "-0.0" or "0.0" end
  -- Shortest round-tripping digits. If some k<=15-digit decimal round-trips, the
  -- correctly rounded 15-digit form equals it padded with zeros (stripped below);
  -- otherwise the closest 16-, then 17-digit form is the shortest.
  -- (Subnormals have far less precision, so they search every length.)
  local s
  if math.abs(x) < 2.2250738585072014e-308 then
    for p = 1, 17 do
      s = format("%." .. (p - 1) .. "e", x)
      if tonumber(s) == x then break end
    end
  else
    s = format("%.14e", x)
    if tonumber(s) ~= x then
      s = format("%.15e", x)
      if tonumber(s) ~= x then s = format("%.16e", x) end
    end
  end
  local sign, int, frac, exp = s:match("^(-?)(%d)%.?(%d*)e([-+]%d+)$")
  exp = tonumber(exp)
  local digits = (int .. frac):gsub("0+$", "")
  if digits == "" then digits = "0" end
  if exp >= -4 and exp < 16 then
    local out
    if exp >= 0 then
      if #digits <= exp + 1 then out = digits .. string.rep("0", exp + 1 - #digits) .. ".0"
      else out = digits:sub(1, exp + 1) .. "." .. digits:sub(exp + 2) end
    else
      out = "0." .. string.rep("0", -exp - 1) .. digits
    end
    return sign .. out
  end
  local mant = digits:sub(1, 1) .. (#digits > 1 and ("." .. digits:sub(2)) or "")
  return format("%s%se%s%02d", sign, mant, exp < 0 and "-" or "+", math.abs(exp))
end

-- Encoder --------------------------------------------------------------------
local escapes = { ['"'] = '\\"', ['\\'] = '\\\\', ['\b'] = '\\b', ['\f'] = '\\f', ['\n'] = '\\n', ['\r'] = '\\r', ['\t'] = '\\t' }

local function encode_string(s, ascii)
  -- fast path: nothing to escape
  if not find(s, ascii and '[%c"\\\128-\255]' or '[%c"\\]') then return '"' .. s .. '"' end
  s = s:gsub('[%c"\\]', function(c)
    return escapes[c] or (byte(c) == 127 and not ascii and c) or format("\\u%04x", byte(c))
  end)
  if ascii and find(s, "[\128-\255]") then
    local out = {}
    for _, cp in utf8.codes(s, true) do
      if cp < 128 then out[#out + 1] = char(cp)
      elseif cp < 0x10000 then out[#out + 1] = format("\\u%04x", cp)
      else
        cp = cp - 0x10000
        out[#out + 1] = format("\\u%04x\\u%04x", 0xD800 + (cp >> 10), 0xDC00 + (cp & 0x3FF))
      end
    end
    s = concat(out)
  end
  return '"' .. s .. '"'
end
M.encode_string = encode_string

local function is_sequence(t)
  local n = #t
  if n == 0 then return next(t) == nil end
  local count = 0
  for _ in pairs(t) do count = count + 1 end
  return count == n
end

local function key_string(k)
  local kt = type(k)
  if kt == "string" then return k end
  if kt == "number" then return mtype(k) == "integer" and tostring(k) or M.float_repr(k) end
  if kt == "boolean" then return tostring(k) end
  error("json: unsupported key type " .. kt)
end

-- opts: indent (number|string), sort_keys, ensure_ascii (default true),
-- separators {item, key}, default (fn for unknown values)
function M.encode(value, opts)
  opts = opts or {}
  local ascii = opts.ensure_ascii ~= false
  local indent = opts.indent
  if type(indent) == "number" then indent = string.rep(" ", indent) end
  local item_sep, key_sep
  if opts.separators then item_sep, key_sep = opts.separators[1], opts.separators[2]
  elseif indent then item_sep, key_sep = ",", ": "
  else item_sep, key_sep = ", ", ": " end
  local buf, n, stack = {}, 0, {}
  local null, float_repr, sort_keys = M.null, M.float_repr, opts.sort_keys
  -- newline+indent strings per nesting level
  local pad = setmetatable({}, { __index = function(t, level)
    local s = indent and ("\n" .. string.rep(indent, level)) or ""
    rawset(t, level, s)
    return s
  end })

  local function emit(v, level)
    local vt = type(v)
    if vt == "string" then n = n + 1; buf[n] = encode_string(v, ascii)
    elseif vt == "number" then n = n + 1; buf[n] = mtype(v) == "integer" and format("%d", v) or float_repr(v)
    elseif v == nil or v == null then n = n + 1; buf[n] = "null"
    elseif vt == "boolean" then n = n + 1; buf[n] = v and "true" or "false"
    elseif vt == "table" then
      if stack[v] then error("json: circular reference") end
      stack[v] = true
      local mt = getmetatable(v)
      local arr
      if mt == ARR then arr = true elseif mt == OBJ then arr = false else arr = is_sequence(v) end
      local nl, close = pad[level + 1], pad[level]
      if arr then
        local len = #v
        if len == 0 then n = n + 1; buf[n] = "[]" else
          n = n + 1; buf[n] = "["
          for i = 1, len do
            n = n + 1; buf[n] = (i > 1) and (item_sep .. nl) or nl
            emit(v[i], level + 1)
          end
          n = n + 1; buf[n] = close .. "]"
        end
      elseif mt == OBJ and not sort_keys then
        -- ordered object, insertion order, string keys in the common case
        local first = true
        for k, val in opairs(v) do
          n = n + 1
          local ks = encode_string(type(k) == "string" and k or key_string(k), ascii)
          buf[n] = (first and ("{" .. nl) or (item_sep .. nl)) .. ks .. key_sep
          first = false
          emit(val, level + 1)
        end
        n = n + 1; buf[n] = first and "{}" or (close .. "}")
      else
        local ks = {}
        if mt == OBJ then for k in opairs(v) do ks[#ks + 1] = { key_string(k), k } end
        else for k in pairs(v) do ks[#ks + 1] = { key_string(k), k } end end
        table.sort(ks, function(a, b) return a[1] < b[1] end)
        if #ks == 0 then n = n + 1; buf[n] = "{}" else
          for i, kk in ipairs(ks) do
            n = n + 1
            buf[n] = ((i == 1) and ("{" .. nl) or (item_sep .. nl)) .. encode_string(kk[1], ascii) .. key_sep
            emit(v[kk[2]], level + 1)
          end
          n = n + 1; buf[n] = close .. "}"
        end
      end
      stack[v] = nil
    elseif opts.default then emit(opts.default(v), level)
    else error("json: cannot encode " .. vt) end
  end
  emit(value, 0)
  return concat(buf)
end
M.dumps = M.encode

-- Decoder --------------------------------------------------------------------
local unesc = { ['"'] = '"', ['\\'] = '\\', ['/'] = '/', b = '\b', f = '\f', n = '\n', r = '\r', t = '\t' }

local function decode_error(s, i, msg)
  local line = select(2, sub(s, 1, i):gsub("\n", "")) + 1
  error(format("json: %s at line %d (char %d)", msg, line, i), 0)
end

local function skip(s, i)
  local _, e = find(s, "^[ \t\r\n]*", i)
  return e + 1
end

local parse

local function parse_string(s, i)
  local a0 = find(s, '["\\]', i + 1)
  if a0 and byte(s, a0) == 34 then return sub(s, i + 1, a0 - 1), a0 + 1 end -- no escapes
  local out, j = {}, i + 1
  while true do
    local a, b = find(s, '["\\]', j)
    if not a then decode_error(s, i, "unterminated string") end
    out[#out + 1] = sub(s, j, a - 1)
    if byte(s, a) == 34 then return concat(out), b + 1 end
    local e = sub(s, a + 1, a + 1)
    if e == "u" then
      local cp = tonumber(sub(s, a + 2, a + 5), 16)
      if not cp then decode_error(s, a, "bad \\u escape") end
      j = a + 6
      if cp >= 0xD800 and cp <= 0xDBFF and sub(s, j, j + 1) == "\\u" then
        local lo = tonumber(sub(s, j + 2, j + 5), 16)
        if lo and lo >= 0xDC00 and lo <= 0xDFFF then
          cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); j = j + 6
        end
      end
      out[#out + 1] = utf8.char(cp)
    else
      local c = unesc[e]
      if not c then decode_error(s, a, "bad escape") end
      out[#out + 1] = c; j = a + 2
    end
  end
end

parse = function(s, i)
  i = skip(s, i)
  local c = byte(s, i)
  if c == 123 then
    -- build the ordered object directly (same state as M.object + __newindex)
    local obj, keys, pos, nk = setmetatable({}, OBJ), {}, {}, 0
    ORDER[obj], POS[obj] = keys, pos
    i = skip(s, i + 1)
    if byte(s, i) == 125 then return obj, i + 1 end
    while true do
      if byte(s, i) ~= 34 then decode_error(s, i, "expected string key") end
      local k; k, i = parse_string(s, i)
      i = skip(s, i)
      if byte(s, i) ~= 58 then decode_error(s, i, "expected ':'") end
      local v; v, i = parse(s, i + 1)
      local old = pos[k]
      if old then keys[old] = GONE end -- duplicate key: last wins, moves to the end
      nk = nk + 1; keys[nk] = k; pos[k] = nk
      rawset(obj, k, v)
      i = skip(s, i)
      local d = byte(s, i)
      if d == 125 then return obj, i + 1 end
      if d ~= 44 then decode_error(s, i, "expected ',' or '}'") end
      i = skip(s, i + 1)
    end
  elseif c == 91 then
    local arr, na = setmetatable({}, ARR), 0
    i = skip(s, i + 1)
    if byte(s, i) == 93 then return arr, i + 1 end
    while true do
      local v; v, i = parse(s, i)
      na = na + 1; arr[na] = v
      i = skip(s, i)
      local d = byte(s, i)
      if d == 93 then return arr, i + 1 end
      if d ~= 44 then decode_error(s, i, "expected ',' or ']'") end
      i = i + 1
    end
  elseif c == 34 then return parse_string(s, i)
  elseif (c >= 48 and c <= 57) or c == 45 then
    -- plain integer fast path; anything with . e E falls through to the general rule
    local a, b = find(s, "^-?%d+", i)
    local nxt = b and byte(s, b + 1)
    if a and nxt ~= 46 and nxt ~= 101 and nxt ~= 69 then
      local n = math.tointeger(tonumber(sub(s, a, b)))
      if n then return n, b + 1 end
    end
    local a2, b2 = find(s, "^-?%d+%.?%d*[eE]?[-+]?%d*", i)
    if not a2 then
      if find(s, "^%-Infinity", i) then return -math.huge, i + 9 end
      decode_error(s, i, "unexpected character")
    end
    local text = sub(s, a2, b2)
    local num
    if find(text, "[.eE]") then num = tonumber(text); if mtype(num) == "integer" then num = num * 1.0 end else num = tonumber(text) end
    if num == nil then decode_error(s, i, "bad number") end
    return num, b2 + 1
  elseif find(s, "^true", i) then return true, i + 4
  elseif find(s, "^false", i) then return false, i + 5
  elseif find(s, "^null", i) then return M.null, i + 4
  elseif find(s, "^NaN", i) then return 0 / 0, i + 3
  elseif find(s, "^Infinity", i) then return math.huge, i + 8
  elseif find(s, "^%-Infinity", i) then return -math.huge, i + 9
  else
    local a, b = find(s, "^-?%d+%.?%d*[eE]?[-+]?%d*", i)
    if not a then decode_error(s, i, "unexpected character") end
    local text = sub(s, a, b)
    local n
    if find(text, "[.eE]") then n = tonumber(text); if mtype(n) == "integer" then n = n * 1.0 end else n = math.tointeger(tonumber(text)) or tonumber(text) end
    if n == nil then decode_error(s, i, "bad number") end
    return n, b + 1
  end
end

function M.decode(s)
  if sub(s, 1, 3) == "\239\187\191" then s = sub(s, 4) end
  local v, i = parse(s, 1)
  i = skip(s, i)
  if i <= #s then decode_error(s, i, "extra data") end
  return v
end
M.loads = M.decode

-- Decode JSON Lines text into an array of values (blank lines skipped).
function M.decode_lines(s)
  local out = M.array({})
  for line in s:gmatch("[^\r\n]+") do
    if line:find("%S") then out[#out + 1] = M.decode(line) end
  end
  return out
end

return M
