-- Minimal dependency-free JSON decoder for Lua 5.4.
-- Returns a table; JSON arrays become Lua 1-based arrays, objects become
-- string-keyed tables, null becomes a sentinel (`json.null`).
local json = {}

json.null = setmetatable({}, { __tostring = function() return "null" end })

local byte, sub, find, char = string.byte, string.sub, string.find, string.char
local floor, huge = math.floor, math.huge

local esc = {
  ['"'] = '"', ['\\'] = '\\', ['/'] = '/', ['b'] = '\b',
  ['f'] = '\f', ['n'] = '\n', ['r'] = '\r', ['t'] = '\t',
}

local function skip_ws(s, i)
  while true do
    local c = byte(s, i)
    if c == 32 or c == 9 or c == 10 or c == 13 then i = i + 1 else return i end
  end
end

local parse_value

local function parse_string(s, i)
  -- i points at the opening quote
  local buf, n = {}, 0
  i = i + 1
  while true do
    local c = byte(s, i)
    if c == nil then error("unterminated string") end
    if c == 34 then return table.concat(buf), i + 1 end -- closing quote
    if c == 92 then -- backslash
      local e = sub(s, i + 1, i + 1)
      local mapped = esc[e]
      if mapped then
        n = n + 1; buf[n] = mapped; i = i + 2
      elseif e == 'u' then
        local hex = sub(s, i + 2, i + 5)
        local cp = tonumber(hex, 16)
        if not cp then error("bad \\u escape at " .. i) end
        i = i + 6
        if cp >= 0xD800 and cp <= 0xDBFF then
          -- high surrogate; expect a following \uDC00..\uDFFF
          local hex2 = sub(s, i, i + 1)
          if hex2 == '\\u' then
            local lo = tonumber(sub(s, i + 2, i + 5), 16)
            if lo and lo >= 0xDC00 and lo <= 0xDFFF then
              cp = 0x10000 + (cp - 0xD800) * 0x400 + (lo - 0xDC00)
              i = i + 6
            end
          end
        end
        n = n + 1; buf[n] = utf8.char(cp)
      else
        error("bad escape \\" .. tostring(e) .. " at " .. i)
      end
    else
      n = n + 1; buf[n] = char(c); i = i + 1
    end
  end
end

local function parse_number(s, i)
  local j = i
  while true do
    local c = byte(s, j)
    if c and (c >= 48 and c <= 57 or c == 45 or c == 43 or c == 46 or c == 101 or c == 69) then
      j = j + 1
    else
      break
    end
  end
  local num = tonumber(sub(s, i, j - 1))
  if not num then error("bad number at " .. i) end
  return num, j
end

local function parse_array(s, i)
  local arr, n = {}, 0
  i = skip_ws(s, i + 1)
  if byte(s, i) == 93 then return arr, i + 1 end -- ]
  while true do
    local v
    v, i = parse_value(s, i)
    n = n + 1; arr[n] = v
    i = skip_ws(s, i)
    local c = byte(s, i)
    if c == 44 then i = skip_ws(s, i + 1)
    elseif c == 93 then return arr, i + 1
    else error("expected , or ] at " .. i) end
  end
end

local function parse_object(s, i)
  local obj = {}
  i = skip_ws(s, i + 1)
  if byte(s, i) == 125 then return obj, i + 1 end -- }
  while true do
    if byte(s, i) ~= 34 then error("expected key string at " .. i) end
    local key
    key, i = parse_string(s, i)
    i = skip_ws(s, i)
    if byte(s, i) ~= 58 then error("expected : at " .. i) end
    local v
    v, i = parse_value(s, skip_ws(s, i + 1))
    obj[key] = v
    i = skip_ws(s, i)
    local c = byte(s, i)
    if c == 44 then i = skip_ws(s, i + 1)
    elseif c == 125 then return obj, i + 1
    else error("expected , or } at " .. i) end
  end
end

parse_value = function(s, i)
  i = skip_ws(s, i)
  local c = byte(s, i)
  if c == 34 then return parse_string(s, i)
  elseif c == 123 then return parse_object(s, i)
  elseif c == 91 then return parse_array(s, i)
  elseif c == 116 then -- true
    if sub(s, i, i + 3) == "true" then return true, i + 4 end
  elseif c == 102 then -- false
    if sub(s, i, i + 4) == "false" then return false, i + 5 end
  elseif c == 110 then -- null
    if sub(s, i, i + 3) == "null" then return json.null, i + 4 end
  elseif c == 45 or (c >= 48 and c <= 57) then
    return parse_number(s, i)
  end
  error("unexpected character at " .. i)
end

function json.decode(s)
  local v
  v = parse_value(s, 1)
  return v
end

return json
