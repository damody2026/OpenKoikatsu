-- pandas.read_html (lxml flavour) for the market-data scripts: HTML tables -> frames with pandas'
-- header detection, colspan/rowspan expansion, NA strings and column type inference.
--   local html = require "market_html"
--   for _, t in ipairs(html.read_html(text)) do  t.columns[i] (string / {level1, level2, ...}), t.rows[r][i]  end
-- Cell values: strings, integers, floats, html.NA (NaN). t.dtypes[i] = "int64" | "float64" | "bool" | "object".
-- html.str(v, dtype) gives the value as pandas `astype(str)` would.
local ok = require "ok"
local util = ok.util
local M = {}

M.NA = 0 / 0
local function is_na(v) return type(v) == "number" and v ~= v end
M.is_na = is_na

local VOID = { area = 1, base = 1, br = 1, col = 1, embed = 1, hr = 1, img = 1, input = 1, link = 1, meta = 1, param = 1,
  source = 1, track = 1, wbr = 1 }
local RAW = { script = 1, style = 1, textarea = 1, title = 1 }

local ENT = { amp = "&", lt = "<", gt = ">", quot = '"', apos = "'", nbsp = "\u{a0}", ndash = "\u{2013}", mdash = "\u{2014}",
  lsquo = "\u{2018}", rsquo = "\u{2019}", ldquo = "\u{201c}", rdquo = "\u{201d}", hellip = "\u{2026}", middot = "\u{b7}",
  copy = "\u{a9}", reg = "\u{ae}", trade = "\u{2122}", times = "\u{d7}", divide = "\u{f7}", deg = "\u{b0}", eacute = "\u{e9}",
  egrave = "\u{e8}", aacute = "\u{e1}", agrave = "\u{e0}", oacute = "\u{f3}", ouml = "\u{f6}", uuml = "\u{fc}", auml = "\u{e4}",
  ccedil = "\u{e7}", ntilde = "\u{f1}", iacute = "\u{ed}", uacute = "\u{fa}", thinsp = "\u{2009}", ensp = "\u{2002}",
  emsp = "\u{2003}", zwj = "\u{200d}", zwnj = "\u{200c}", lrm = "\u{200e}", rlm = "\u{200f}", euro = "\u{20ac}",
  pound = "\u{a3}", yen = "\u{a5}", cent = "\u{a2}", sect = "\u{a7}", para = "\u{b6}", bull = "\u{2022}", minus = "\u{2212}" }

local function utf8char(n)
  if n < 0 or n > 0x10FFFF or (n >= 0xD800 and n <= 0xDFFF) then return "\u{fffd}" end
  return utf8.char(n)
end
function M.unescape(s)
  if not s:find("&", 1, true) then return s end
  return (s:gsub("&(#?[%w]+);?", function(e)
    if e:sub(1, 2) == "#x" or e:sub(1, 2) == "#X" then local n = tonumber(e:sub(3), 16); return n and utf8char(n) end
    if e:sub(1, 1) == "#" then local n = tonumber(e:sub(2)); return n and utf8char(n) end
    return ENT[e] or ENT[e:lower()]
  end))
end

local function parse_attrs(s)
  local a = {}
  local i = 1
  while i <= #s do
    local k, rest = s:match("^%s*([^%s=/>]+)()", i)
    if not k then break end
    i = rest
    local v
    local eq = s:match("^%s*=%s*()", i)
    if eq then
      i = eq
      local q = s:sub(i, i)
      if q == '"' or q == "'" then
        local e = s:find(q, i + 1, true) or #s + 1
        v = s:sub(i + 1, e - 1); i = e + 1
      else
        local e = s:find("[%s>]", i) or #s + 1
        v = s:sub(i, e - 1); i = e
      end
    end
    k = k:lower()
    if a[k] == nil then a[k] = v and M.unescape(v) or "" end
  end
  return a
end

-- Minimal recovering HTML parser -> element tree {tag, attrs, children (elements or text strings), parent}.
function M.parse(text)
  local root = { tag = "#root", attrs = {}, children = {} }
  local stack = { root }
  local function top() return stack[#stack] end
  local function close_to(tag) -- pop until `tag` (inclusive) if it is open
    for k = #stack, 2, -1 do
      if stack[k].tag == tag then
        for _ = #stack, k, -1 do stack[#stack] = nil end
        return true
      end
    end
    return false
  end
  local function close_within(tags, stop) -- implicit end tags: close open `tags` above the nearest `stop`
    for k = #stack, 2, -1 do
      local t = stack[k].tag
      if stop[t] then return end
      if tags[t] then for _ = #stack, k, -1 do stack[#stack] = nil end; return end
    end
  end
  local CELL, ROW, SECT = { td = 1, th = 1 }, { tr = 1 }, { thead = 1, tbody = 1, tfoot = 1 }
  local TABLE = { table = 1 }
  local i, n = 1, #text
  while i <= n do
    local lt = text:find("<", i, true)
    if not lt then lt = n + 1 end
    if lt > i then table.insert(top().children, M.unescape(text:sub(i, lt - 1))) end
    if lt > n then break end
    if text:sub(lt, lt + 3) == "<!--" then
      local e = text:find("-->", lt + 4, true)
      i = e and e + 3 or n + 1
    elseif text:sub(lt + 1, lt + 1) == "!" or text:sub(lt + 1, lt + 1) == "?" then
      local e = text:find(">", lt, true)
      i = e and e + 1 or n + 1
    elseif text:sub(lt + 1, lt + 1) == "/" then
      local e = text:find(">", lt, true) or n
      local tag = (text:sub(lt + 2, e - 1):match("^%s*([%w:]+)") or ""):lower()
      if tag ~= "" then close_to(tag) end
      i = e + 1
    else
      local tag, p = text:match("^<([%a][%w:%-]*)()", lt)
      if not tag then
        table.insert(top().children, "<")
        i = lt + 1
      else
        tag = tag:lower()
        -- find the end of the tag, honouring quoted attribute values
        local j, q = p, nil
        while j <= n do
          local c = text:sub(j, j)
          if q then if c == q then q = nil end
          elseif c == '"' or c == "'" then q = c
          elseif c == ">" then break end
          j = j + 1
        end
        local inner = text:sub(p, j - 1)
        local selfclose = inner:match("/%s*$") ~= nil
        local attrs = parse_attrs((inner:gsub("/%s*$", "")))
        if CELL[tag] then close_within(CELL, { tr = 1, table = 1 })
        elseif tag == "tr" then close_within({ tr = 1, td = 1, th = 1 }, TABLE)
        elseif SECT[tag] then close_within({ thead = 1, tbody = 1, tfoot = 1, tr = 1, td = 1, th = 1 }, TABLE)
        elseif tag == "p" then close_within({ p = 1 }, { td = 1, th = 1, table = 1, div = 1 })
        elseif tag == "li" then close_within({ li = 1 }, { ul = 1, ol = 1, table = 1 })
        elseif tag == "option" then close_within({ option = 1 }, { select = 1 }) end
        local el = { tag = tag, attrs = attrs, children = {}, parent = top() }
        table.insert(top().children, el)
        i = j + 1
        if RAW[tag] then
          local e = text:lower():find("</" .. tag, i, true)
          local body = text:sub(i, (e or n + 1) - 1)
          if tag == "title" or tag == "textarea" then body = M.unescape(body) end
          if body ~= "" then el.children[1] = body end
          if e then local g = text:find(">", e, true); i = (g or n) + 1 else i = n + 1 end
        elseif not VOID[tag] and not selfclose then
          stack[#stack + 1] = el
        end
      end
    end
  end
  return root
end

local function each(el, fn) -- pre-order over elements
  for _, c in ipairs(el.children) do
    if type(c) == "table" then fn(c); each(c, fn) end
  end
end
M.each = each

function M.text_content(el)
  local out = {}
  local function walk(e)
    for _, c in ipairs(e.children) do
      if type(c) == "string" then out[#out + 1] = c
      elseif c.tag == "br" then out[#out + 1] = "\n"
      else walk(c) end
    end
  end
  walk(el)
  return table.concat(out)
end

local function hidden(el) return ((el.attrs.style or ""):gsub(" ", "")):find("display:none", 1, true) ~= nil end
local function drop_hidden(el)
  local keep = {}
  for _, c in ipairs(el.children) do
    if type(c) == "string" then keep[#keep + 1] = c
    elseif c.tag ~= "style" and not hidden(c) then drop_hidden(c); keep[#keep + 1] = c end
  end
  el.children = keep
end

local function children(el, tags)
  local out = {}
  for _, c in ipairs(el.children) do if type(c) == "table" and tags[c.tag] then out[#out + 1] = c end end
  return out
end
local function descendants(el, tag)
  local out = {}
  each(el, function(c) if c.tag == tag then out[#out + 1] = c end end)
  return out
end

-- pandas _remove_whitespace: re.sub(r"[\r\n]+|\s{2,}", " ", s.strip())
local WS = "[ \t\n\r\f\v\u{1c}\u{1d}\u{1e}\u{1f}\u{85}\u{a0}\u{1680}\u{2000}-\u{200a}\u{2028}\u{2029}\u{202f}\u{205f}\u{3000}]"
local function is_ws(c)
  return c == " " or c == "\t" or c == "\n" or c == "\r" or c == "\f" or c == "\v" or c == "\u{1c}" or c == "\u{1d}"
    or c == "\u{1e}" or c == "\u{1f}" or c == "\u{85}" or c == "\u{a0}" or c == "\u{1680}" or c == "\u{2028}"
    or c == "\u{2029}" or c == "\u{202f}" or c == "\u{205f}" or c == "\u{3000}"
    or (c >= "\u{2000}" and c <= "\u{200a}" and #c == 3)
end
local function pystrip(s)
  local cs = util.chars(s)
  local a, b = 1, #cs
  while a <= b and is_ws(cs[a]) do a = a + 1 end
  while b >= a and is_ws(cs[b]) do b = b - 1 end
  return table.concat(cs, "", a, b), cs, a, b
end
function M.remove_whitespace(s)
  local _, cs, a, b = pystrip(s)
  local out, k = {}, a
  while k <= b do
    local c = cs[k]
    if c == "\r" or c == "\n" then
      while k <= b and (cs[k] == "\r" or cs[k] == "\n") do k = k + 1 end
      -- a [\r\n]+ run followed by more whitespace may instead start a \s{2,} run: regex alternation tries
      -- [\r\n]+ first at this position, so the run of newlines alone becomes one space
      out[#out + 1] = " "
    elseif is_ws(c) and k + 1 <= b and is_ws(cs[k + 1]) then
      while k <= b and is_ws(cs[k]) do k = k + 1 end
      out[#out + 1] = " "
    else
      out[#out + 1] = c; k = k + 1
    end
  end
  return table.concat(out)
end

local function expand(rows, remainder, overflow)
  local all = {}
  remainder = remainder or {}
  for _, tr in ipairs(rows) do
    local texts, nextr, index = {}, {}, 0
    for _, td in ipairs(children(tr, { td = 1, th = 1 })) do
      while #remainder > 0 and remainder[1][1] <= index do
        local pr = table.remove(remainder, 1)
        texts[#texts + 1] = pr[2]
        if pr[3] > 1 then nextr[#nextr + 1] = { pr[1], pr[2], pr[3] - 1 } end
        index = index + 1
      end
      local text = M.remove_whitespace(M.text_content(td))
      local rowspan = math.tointeger(tonumber(td.attrs.rowspan or "") or 1) or 1
      local colspan = math.tointeger(tonumber(td.attrs.colspan or "") or 1) or 1
      for _ = 1, colspan do
        texts[#texts + 1] = text
        if rowspan > 1 then nextr[#nextr + 1] = { index, text, rowspan - 1 } end
        index = index + 1
      end
    end
    for _, pr in ipairs(remainder) do
      texts[#texts + 1] = pr[2]
      if pr[3] > 1 then nextr[#nextr + 1] = { pr[1], pr[2], pr[3] - 1 } end
    end
    all[#all + 1] = texts
    remainder = nextr
  end
  if not overflow then
    while #remainder > 0 do
      local nextr, texts = {}, {}
      for _, pr in ipairs(remainder) do
        texts[#texts + 1] = pr[2]
        if pr[3] > 1 then nextr[#nextr + 1] = { pr[1], pr[2], pr[3] - 1 } end
      end
      all[#all + 1] = texts
      remainder = nextr
    end
  end
  return all, remainder
end

local NA_STRINGS = util.set({ "-1.#IND", "1.#QNAN", "1.#IND", "-1.#QNAN", "#N/A N/A", "#N/A", "N/A", "n/a", "NA", "<NA>",
  "#NA", "NULL", "null", "NaN", "-NaN", "nan", "-nan", "None", "" })
M.NA_STRINGS = NA_STRINGS

-- pandas C/python parser numeric conversion with thousands=",": returns number or nil.
local function to_number(s)
  local t = s
  if t:find(",", 1, true) then
    -- thousands separators only between digit groups
    if not t:match("^[+-]?%d%d?%d?(,%d%d%d)+%.?%d*$") and not t:match("^[+-]?%d%d?%d?(,%d%d%d)+%.%d*[eE][+-]?%d+$") then return nil end
    t = t:gsub(",", "")
  end
  t = util.strip(t)
  if t:match("^[+-]?%d+$") then
    local v = math.tointeger(tonumber(t))
    if v then return v end
    return tonumber(t) + 0.0
  end
  local lower = t:lower()
  if lower == "inf" or lower == "+inf" or lower == "infinity" or lower == "+infinity" then return math.huge end
  if lower == "-inf" or lower == "-infinity" then return -math.huge end
  if t:match("^[+-]?%d*%.?%d*$") and t:find("%d") then return tonumber(t) + 0.0 end
  if t:match("^[+-]?%d*%.?%d*[eE][+-]?%d+$") and t:match("^[+-]?%.?%d") then return tonumber(t) + 0.0 end
  return nil
end
M.to_number = to_number

-- Column inference like pandas.TextParser (python engine): NA strings -> NaN, then bool / int64 / float64 / object.
local function infer(values)
  local nums, all_num, any_val = {}, true, false
  local bools, all_bool = {}, true
  for i, s in ipairs(values) do
    if NA_STRINGS[s] then
      nums[i] = M.NA; bools[i] = M.NA
    else
      any_val = true
      local v = to_number(s)
      if v == nil then all_num = false else nums[i] = v end
      if s == "True" or s == "TRUE" or s == "true" then bools[i] = true
      elseif s == "False" or s == "FALSE" or s == "false" then bools[i] = false
      else all_bool = false end
    end
  end
  if not any_val then
    local out = {}
    for i in ipairs(values) do out[i] = M.NA end
    return out, "float64"
  end
  if all_bool then
    local has_na = false
    for i in ipairs(values) do if is_na(bools[i]) then has_na = true end end
    return bools, has_na and "object" or "bool"
  end
  if all_num then
    local is_int, has_na = true, false
    for i in ipairs(values) do
      local v = nums[i]
      if is_na(v) then has_na = true elseif math.type(v) ~= "integer" then is_int = false end
    end
    if is_int and not has_na then return nums, "int64" end
    for i in ipairs(values) do if not is_na(nums[i]) then nums[i] = nums[i] + 0.0 end end
    return nums, "float64"
  end
  local out = {}
  for i, s in ipairs(values) do out[i] = NA_STRINGS[s] and M.NA or s end
  return out, "object"
end
M.infer = infer

-- pandas astype(str) of one cell.
function M.str(v, dtype)
  if is_na(v) then return "nan" end
  if v == true then return "True" elseif v == false then return "False" end
  if type(v) == "number" then
    if dtype == "float64" or math.type(v) == "float" then return util.num(v + 0.0) end
    return string.format("%d", v)
  end
  return v
end

function M.read_html(text, opts)
  opts = opts or {}
  local root = M.parse(text)
  local tables = {}
  each(root, function(el)
    if el.tag == "table" and not hidden(el) then
      local t = M.text_content(el)
      if t:find("[^\n]") then tables[#tables + 1] = el end
    end
  end)
  local out = {}
  for _, tbl in ipairs(tables) do
    drop_hidden(tbl)
    local head_rows = {}
    for _, th in ipairs(descendants(tbl, "thead")) do
      for _, tr in ipairs(children(th, { tr = 1 })) do head_rows[#head_rows + 1] = tr end
      if #children(th, { td = 1, th = 1 }) > 0 then head_rows[#head_rows + 1] = th end
    end
    local body_rows = {}
    for _, tb in ipairs(descendants(tbl, "tbody")) do
      for _, tr in ipairs(descendants(tb, "tr")) do body_rows[#body_rows + 1] = tr end
    end
    do -- unique, document order (nested tbody would repeat rows)
      local seen, u = {}, {}
      for _, r in ipairs(body_rows) do if not seen[r] then seen[r] = true; u[#u + 1] = r end end
      body_rows = u
    end
    for _, tr in ipairs(children(tbl, { tr = 1 })) do body_rows[#body_rows + 1] = tr end
    local foot_rows = {}
    for _, tf in ipairs(descendants(tbl, "tfoot")) do
      for _, tr in ipairs(descendants(tf, "tr")) do foot_rows[#foot_rows + 1] = tr end
    end
    if #head_rows == 0 then
      while #body_rows > 0 do
        local tds = children(body_rows[1], { td = 1, th = 1 })
        local all_th = true
        for _, c in ipairs(tds) do if c.tag ~= "th" then all_th = false end end
        if not all_th then break end
        head_rows[#head_rows + 1] = table.remove(body_rows, 1)
      end
    end
    local header, rem = expand(head_rows, nil, true)
    local body
    body, rem = expand(body_rows, rem, #foot_rows > 0)
    local footer = expand(foot_rows, rem, false)
    local frame = M.to_frame(header, body, footer)
    if frame then out[#out + 1] = frame end
  end
  return out
end

-- _data_to_frame + TextParser(header=...) for the parsed (header, body, footer) rows.
function M.to_frame(head, body, foot)
  local rows = {}
  for _, r in ipairs(head) do rows[#rows + 1] = r end
  for _, r in ipairs(body) do rows[#rows + 1] = r end
  for _, r in ipairs(foot) do rows[#rows + 1] = r end
  if #rows == 0 then return nil end
  local header
  if #head == 1 then header = { 0 }
  elseif #head > 1 then
    header = {}
    for i, r in ipairs(head) do
      local any = false
      for _, t in ipairs(r) do if t ~= "" then any = true end end
      if any then header[#header + 1] = i - 1 end
    end
  end
  local width = 0
  for _, r in ipairs(rows) do width = math.max(width, #r) end
  for _, r in ipairs(rows) do for k = #r + 1, width do r[k] = "" end end
  local columns, first_data = {}, 1
  if header and #header > 0 then
    local levels = #header
    first_data = header[#header] + 2
    for c = 1, width do
      if levels == 1 then
        local name = rows[header[1] + 1][c]
        columns[c] = name ~= "" and name or ("Unnamed: " .. (c - 1))
      else
        local t = {}
        for l, h in ipairs(header) do
          local name = rows[h + 1][c]
          t[l] = name ~= "" and name or ("Unnamed: " .. (c - 1) .. "_level_" .. (l - 1))
        end
        columns[c] = t
      end
    end
    if levels == 1 then -- duplicate names get .1, .2 ... (mangle_dupe_cols)
      local count = {}
      for c = 1, width do
        local name = columns[c]
        if count[name] then
          local k = count[name]
          local cand = name .. "." .. k
          while util.contains_value(columns, cand) do k = k + 1; cand = name .. "." .. k end
          count[name] = k + 1
          columns[c] = cand
        else count[name] = 1 end
      end
    end
  else
    for c = 1, width do columns[c] = c - 1 end
  end
  local data = {}
  for r = first_data, #rows do data[#data + 1] = rows[r] end
  if #data == 0 and width == 0 then return nil end
  local cols, dtypes = {}, {}
  for c = 1, width do
    local vals = {}
    for r, row in ipairs(data) do vals[r] = row[c] end
    cols[c], dtypes[c] = infer(vals)
  end
  local out_rows = {}
  for r = 1, #data do
    local row = {}
    for c = 1, width do row[c] = cols[c][r] end
    out_rows[r] = row
  end
  return { columns = columns, rows = out_rows, dtypes = dtypes, width = width }
end

-- pd.to_datetime(str, errors="coerce") for the date strings these pages carry -> int YYYYMMDD or nil.
local MONTHS = { jan = 1, feb = 2, mar = 3, apr = 4, may = 5, jun = 6, jul = 7, aug = 8, sep = 9, sept = 9, oct = 10, nov = 11, dec = 12,
  january = 1, february = 2, march = 3, april = 4, june = 6, july = 7, august = 8, september = 9, october = 10,
  november = 11, december = 12 }
local function valid(y, m, d)
  if not (y and m and d) or m < 1 or m > 12 or d < 1 then return nil end
  local mdays = { 31, (y % 4 == 0 and (y % 100 ~= 0 or y % 400 == 0)) and 29 or 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }
  if d > mdays[m] then return nil end
  return y * 10000 + m * 100 + d
end
function M.parse_date(s)
  if type(s) ~= "string" then return nil end
  s = util.strip(s)
  local y, m, d = s:match("^(%d%d%d%d)[-/.](%d%d?)[-/.](%d%d?)$")
  if not y then y, m, d = s:match("^(%d%d%d%d)[-/.](%d%d?)[-/.](%d%d?)[ T]%d%d?:%d%d") end
  if y then return valid(tonumber(y), tonumber(m), tonumber(d)) end
  y, m, d = s:match("^(%d%d%d%d)(%d%d)(%d%d)$")
  if y then return valid(tonumber(y), tonumber(m), tonumber(d)) end
  local mon, day, year = s:match("^(%a+)%.?%s+(%d%d?),?%s+(%d%d%d%d)$")
  if mon and MONTHS[mon:lower()] then return valid(tonumber(year), MONTHS[mon:lower()], tonumber(day)) end
  day, mon, year = s:match("^(%d%d?)%s+(%a+)%.?,?%s+(%d%d%d%d)$")
  if mon and MONTHS[mon:lower()] then return valid(tonumber(year), MONTHS[mon:lower()], tonumber(day)) end
  return nil
end

return M
