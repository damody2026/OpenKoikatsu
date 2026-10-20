-- CSV/TSV compatible with Python's csv module (excel dialect).
local M = {}

-- Parse text into rows (arrays of strings). opts: delimiter (default ","), quotechar ('"').
-- Scans with string.find (field-at-a-time), so large files stay fast.
function M.parse(text, opts)
  opts = opts or {}
  local d, q = opts.delimiter or ",", opts.quotechar or '"'
  if text:sub(1, 3) == "\239\187\191" then text = text:sub(4) end
  local find, sub, byte = string.find, string.sub, string.byte
  local stop = "[" .. d:gsub("%p", "%%%0") .. "\r\n]"
  local qpat = q:gsub("%p", "%%%0")
  local rows, row, i, n = {}, {}, 1, #text
  local qb = byte(q)
  while i <= n do
    local parts = {}
    local line_start = (#row == 0)
    local quoted = false
    if byte(text, i) == qb then
      -- quoted section: "" is a literal quote; text after the closing quote is kept (Python)
      quoted = true
      i = i + 1
      while true do
        local a = find(text, qpat, i)
        if not a then parts[#parts + 1] = sub(text, i); i = n + 1; break end
        parts[#parts + 1] = sub(text, i, a - 1)
        if byte(text, a + 1) == qb then parts[#parts + 1] = q; i = a + 2
        else i = a + 1; break end
      end
    end
    local a = find(text, stop, i)
    local e = a and a - 1 or n
    if e >= i then parts[#parts + 1] = sub(text, i, e) end
    local field = table.concat(parts)
    local c = a and byte(text, a)
    if c == byte(d) then
      row[#row + 1] = field; i = a + 1
      if i > n then row[#row + 1] = ""; rows[#rows + 1] = row; row = {} end
    else
      -- end of record; a line with no characters is an empty row like csv.reader
      -- (DictReader-style readers below skip those)
      if not (line_start and field == "" and not quoted) then row[#row + 1] = field end
      if a or #row > 0 then rows[#rows + 1] = row end
      row = {}
      if not a then break end
      i = a + 1
      if c == 13 and byte(text, i) == 10 then i = i + 1 end
    end
  end
  return rows
end

function M.read(path, opts)
  local p = require "ok.path"
  return M.parse(p.read_bytes(path), opts)
end

-- DictReader: array of tables keyed by header (plus .fields = header order);
-- empty rows are skipped like csv.DictReader.
local function dicts(rows)
  local out, header = {}, nil
  for _, r in ipairs(rows) do
    if #r > 0 then
      if not header then header = r
      else
        local t = {}
        for j, h in ipairs(header) do t[h] = r[j] end
        out[#out + 1] = t
      end
    end
  end
  out.fields = header or {}
  return out
end
function M.read_dicts(path, opts) return dicts(M.read(path, opts)) end
function M.parse_dicts(text, opts) return dicts(M.parse(text, opts)) end

-- Streaming DictReader for large files: for row in iter_dicts(path) do ... end
-- (row.fields is not set; the header is returned as the second value of each call).
-- Records spanning lines (quoted newlines) are joined; empty lines are skipped.
function M.iter_dicts(path, opts)
  local f = assert(io.open(path, "rb"))
  local header, first = nil, true
  return function()
    while true do
      local line = f:read("L")
      if not line then f:close(); return nil end
      if first then first = false; if line:sub(1, 3) == "\239\187\191" then line = line:sub(4) end end
      local _, quotes = line:gsub('"', "")
      while quotes % 2 == 1 do
        local more = f:read("L")
        if not more then break end
        line = line .. more
        local _, q = more:gsub('"', ""); quotes = quotes + q
      end
      local row = M.parse(line, opts)[1]
      if row and #row > 0 then
        if not header then header = row
        else
          local t = {}
          for j, h in ipairs(header) do t[h] = row[j] end
          return t, header
        end
      end
    end
  end
end

local function fmt_value(v)
  if v == nil then return "" end
  if type(v) == "number" then
    if math.type(v) == "integer" then return string.format("%d", v) end
    return require("ok.json").float_repr(v)
  end
  if type(v) == "boolean" then return v and "True" or "False" end
  return tostring(v)
end

-- Format one row (QUOTE_MINIMAL). opts: delimiter, lineterminator (default "\r\n").
function M.format_row(row, opts)
  opts = opts or {}
  local d = opts.delimiter or ","
  local cells = {}
  for i = 1, (row.n or #row) do
    local s = fmt_value(row[i])
    if s:find('[' .. d:gsub("%p", "%%%0") .. '"\r\n]') or (s ~= "" and (s:sub(1, 1) == " " and false)) then
      s = '"' .. s:gsub('"', '""') .. '"'
    end
    cells[i] = s
  end
  return table.concat(cells, d) .. (opts.lineterminator or "\r\n")
end

-- Writer accumulating rows; :text() returns content, :save(path) writes bytes as-is.
function M.writer(opts)
  local w = { rows = {}, opts = opts or {} }
  function w:row(r) self.rows[#self.rows + 1] = M.format_row(r, self.opts) end
  function w:rows_(rs) for _, r in ipairs(rs) do self:row(r) end end
  function w:dict_header(fields) self.fields = fields; self:row(fields) end
  function w:dict(t) local r = {}; for i, f in ipairs(self.fields) do r[i] = t[f] end; r.n = #self.fields; self:row(r) end
  function w:text() return table.concat(self.rows) end
  function w:save(path, wopts) require("ok.path").write_bytes(path, self:text(), wopts) end
  return w
end

return M
