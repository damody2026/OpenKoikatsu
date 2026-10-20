-- Python difflib (3.12) port: SequenceMatcher, unified_diff, ndiff/Differ.
--   local difflib = ok.difflib
--   local lines = difflib.unified_diff(a, b, {fromfile=, tofile=, n=3, lineterm=""})  -- array of lines
--   local sm = difflib.SequenceMatcher(nil, a, b); sm:get_opcodes(); sm:ratio()
--   difflib.ndiff(a, b)  -- array of "- "/"+ "/"  "/"? " lines
-- Sequences are Lua arrays (1-based storage); all indices exposed by the API
-- are 0-based like Python (opcodes, matching blocks). Strings passed to
-- SequenceMatcher are split into UTF-8 characters.
local M = {}

local function to_seq(x)
  if type(x) == "string" then
    local t = {}
    for _, c in utf8.codes(x) do t[#t + 1] = utf8.char(c) end
    return t
  end
  return x
end
M.chars = to_seq

local SM = {}
SM.__index = SM

function M.SequenceMatcher(isjunk, a, b, autojunk)
  local self = setmetatable({ isjunk = isjunk, autojunk = autojunk ~= false }, SM)
  self:set_seqs(a or {}, b or {})
  return self
end

function SM:set_seqs(a, b) self:set_seq1(a); self:set_seq2(b) end
function SM:set_seq1(a)
  if a == self.a_src then return end
  self.a_src, self.a = a, to_seq(a)
  self.matching_blocks, self.opcodes = nil, nil
end
function SM:set_seq2(b)
  if b == self.b_src then return end
  self.b_src, self.b = b, to_seq(b)
  self.matching_blocks, self.opcodes, self.fullbcount = nil, nil, nil
  self:_chain_b()
end

function SM:_chain_b()
  local b = self.b
  local b2j = {}
  for i = 1, #b do
    local elt = b[i]
    local idx = b2j[elt]
    if not idx then idx = {}; b2j[elt] = idx end
    idx[#idx + 1] = i - 1
  end
  local junk = {}
  if self.isjunk then
    for elt in pairs(b2j) do if self.isjunk(elt) then junk[elt] = true end end
    for elt in pairs(junk) do b2j[elt] = nil end
  end
  local popular = {}
  local n = #b
  if self.autojunk and n >= 200 then
    local ntest = n // 100 + 1
    for elt, idxs in pairs(b2j) do if #idxs > ntest then popular[elt] = true end end
    for elt in pairs(popular) do b2j[elt] = nil end
  end
  self.b2j, self.bjunk, self.bpopular = b2j, junk, popular
end

-- Returns i, j, k (0-based).
function SM:find_longest_match(alo, ahi, blo, bhi)
  local a, b, b2j, bjunk = self.a, self.b, self.b2j, self.bjunk
  alo, ahi, blo, bhi = alo or 0, ahi or #self.a, blo or 0, bhi or #self.b
  local besti, bestj, bestsize = alo, blo, 0
  local j2len = {}
  local nothing = {}
  for i = alo, ahi - 1 do
    local newj2len = {}
    for _, j in ipairs(b2j[a[i + 1]] or nothing) do
      if j >= blo then
        if j >= bhi then break end
        local k = (j2len[j - 1] or 0) + 1
        newj2len[j] = k
        if k > bestsize then besti, bestj, bestsize = i - k + 1, j - k + 1, k end
      end
    end
    j2len = newj2len
  end
  while besti > alo and bestj > blo and not bjunk[b[bestj]] and a[besti] == b[bestj] do
    besti, bestj, bestsize = besti - 1, bestj - 1, bestsize + 1
  end
  while besti + bestsize < ahi and bestj + bestsize < bhi and not bjunk[b[bestj + bestsize + 1]] and a[besti + bestsize + 1] == b[bestj + bestsize + 1] do
    bestsize = bestsize + 1
  end
  while besti > alo and bestj > blo and bjunk[b[bestj]] and a[besti] == b[bestj] do
    besti, bestj, bestsize = besti - 1, bestj - 1, bestsize + 1
  end
  while besti + bestsize < ahi and bestj + bestsize < bhi and bjunk[b[bestj + bestsize + 1]] and a[besti + bestsize + 1] == b[bestj + bestsize + 1] do
    bestsize = bestsize + 1
  end
  return besti, bestj, bestsize
end

function SM:get_matching_blocks()
  if self.matching_blocks then return self.matching_blocks end
  local la, lb = #self.a, #self.b
  local queue = { { 0, la, 0, lb } }
  local blocks = {}
  while #queue > 0 do
    local q = table.remove(queue)
    local alo, ahi, blo, bhi = q[1], q[2], q[3], q[4]
    local i, j, k = self:find_longest_match(alo, ahi, blo, bhi)
    if k > 0 then
      blocks[#blocks + 1] = { i, j, k }
      if alo < i and blo < j then queue[#queue + 1] = { alo, i, blo, j } end
      if i + k < ahi and j + k < bhi then queue[#queue + 1] = { i + k, ahi, j + k, bhi } end
    end
  end
  table.sort(blocks, function(x, y)
    if x[1] ~= y[1] then return x[1] < y[1] end
    if x[2] ~= y[2] then return x[2] < y[2] end
    return x[3] < y[3]
  end)
  local i1, j1, k1 = 0, 0, 0
  local out = {}
  for _, blk in ipairs(blocks) do
    local i2, j2, k2 = blk[1], blk[2], blk[3]
    if i1 + k1 == i2 and j1 + k1 == j2 then k1 = k1 + k2
    else
      if k1 > 0 then out[#out + 1] = { i1, j1, k1 } end
      i1, j1, k1 = i2, j2, k2
    end
  end
  if k1 > 0 then out[#out + 1] = { i1, j1, k1 } end
  out[#out + 1] = { la, lb, 0 }
  self.matching_blocks = out
  return out
end

-- Array of {tag, i1, i2, j1, j2} (0-based, half-open).
function SM:get_opcodes()
  if self.opcodes then return self.opcodes end
  local i, j = 0, 0
  local answer = {}
  for _, m in ipairs(self:get_matching_blocks()) do
    local ai, bj, size = m[1], m[2], m[3]
    local tag = ""
    if i < ai and j < bj then tag = "replace" elseif i < ai then tag = "delete" elseif j < bj then tag = "insert" end
    if tag ~= "" then answer[#answer + 1] = { tag, i, ai, j, bj } end
    i, j = ai + size, bj + size
    if size > 0 then answer[#answer + 1] = { "equal", ai, i, bj, j } end
  end
  self.opcodes = answer
  return answer
end

function SM:get_grouped_opcodes(n)
  n = n or 3
  local codes = {}
  for i, c in ipairs(self:get_opcodes()) do codes[i] = { c[1], c[2], c[3], c[4], c[5] } end
  if #codes == 0 then codes = { { "equal", 0, 1, 0, 1 } } end
  if codes[1][1] == "equal" then
    local c = codes[1]
    codes[1] = { c[1], math.max(c[2], c[3] - n), c[3], math.max(c[4], c[5] - n), c[5] }
  end
  if codes[#codes][1] == "equal" then
    local c = codes[#codes]
    codes[#codes] = { c[1], c[2], math.min(c[3], c[2] + n), c[4], math.min(c[5], c[4] + n) }
  end
  local nn = n + n
  local groups, group = {}, {}
  for _, c in ipairs(codes) do
    local tag, i1, i2, j1, j2 = c[1], c[2], c[3], c[4], c[5]
    if tag == "equal" and i2 - i1 > nn then
      group[#group + 1] = { tag, i1, math.min(i2, i1 + n), j1, math.min(j2, j1 + n) }
      groups[#groups + 1] = group
      group = {}
      i1, j1 = math.max(i1, i2 - n), math.max(j1, j2 - n)
    end
    group[#group + 1] = { tag, i1, i2, j1, j2 }
  end
  if #group > 0 and not (#group == 1 and group[1][1] == "equal") then groups[#groups + 1] = group end
  return groups
end

local function calculate_ratio(matches, length)
  if length > 0 then return 2.0 * matches / length end
  return 1.0
end

function SM:ratio()
  local matches = 0
  for _, m in ipairs(self:get_matching_blocks()) do matches = matches + m[3] end
  return calculate_ratio(matches, #self.a + #self.b)
end

function SM:quick_ratio()
  if not self.fullbcount then
    local full = {}
    for _, elt in ipairs(self.b) do full[elt] = (full[elt] or 0) + 1 end
    self.fullbcount = full
  end
  local full, avail, matches = self.fullbcount, {}, 0
  for _, elt in ipairs(self.a) do
    local numb = avail[elt]
    if numb == nil then numb = full[elt] or 0 end
    avail[elt] = numb - 1
    if numb > 0 then matches = matches + 1 end
  end
  return calculate_ratio(matches, #self.a + #self.b)
end

function SM:real_quick_ratio()
  local la, lb = #self.a, #self.b
  return calculate_ratio(math.min(la, lb), la + lb)
end

local function format_range_unified(start, stop)
  local beginning, length = start + 1, stop - start
  if length == 1 then return tostring(beginning) end
  if length == 0 then beginning = beginning - 1 end
  return beginning .. "," .. length
end

-- unified_diff(a, b, {fromfile, tofile, fromfiledate, tofiledate, n=3, lineterm="\n"}) -> array of lines
function M.unified_diff(a, b, opts)
  opts = opts or {}
  local n, lineterm = opts.n or 3, opts.lineterm or "\n"
  local out, started = {}, false
  for _, group in ipairs(M.SequenceMatcher(nil, a, b):get_grouped_opcodes(n)) do
    if not started then
      started = true
      local fromdate = (opts.fromfiledate and opts.fromfiledate ~= "") and ("\t" .. opts.fromfiledate) or ""
      local todate = (opts.tofiledate and opts.tofiledate ~= "") and ("\t" .. opts.tofiledate) or ""
      out[#out + 1] = "--- " .. (opts.fromfile or "") .. fromdate .. lineterm
      out[#out + 1] = "+++ " .. (opts.tofile or "") .. todate .. lineterm
    end
    local first, last = group[1], group[#group]
    out[#out + 1] = ("@@ -%s +%s @@%s"):format(format_range_unified(first[2], last[3]), format_range_unified(first[4], last[5]), lineterm)
    for _, c in ipairs(group) do
      local tag, i1, i2, j1, j2 = c[1], c[2], c[3], c[4], c[5]
      if tag == "equal" then
        for i = i1 + 1, i2 do out[#out + 1] = " " .. a[i] end
      else
        if tag == "replace" or tag == "delete" then for i = i1 + 1, i2 do out[#out + 1] = "-" .. a[i] end end
        if tag == "replace" or tag == "insert" then for j = j1 + 1, j2 do out[#out + 1] = "+" .. b[j] end end
      end
    end
  end
  return out
end

function M.IS_CHARACTER_JUNK(ch, ws) return (ws or " \t"):find(ch, 1, true) ~= nil and #ch == 1 end
function M.IS_LINE_JUNK(line) return line:match("^%s*#?%s*$") ~= nil end

local function is_space(c) return c:match("^%s$") ~= nil end
local function keep_original_ws(s, tags)
  local sc, out = to_seq(s), {}
  local tc = to_seq(tags)
  for i = 1, math.min(#sc, #tc) do
    out[i] = (tc[i] == " " and is_space(sc[i])) and sc[i] or tc[i]
  end
  return table.concat(out)
end

-- Differ(linejunk, charjunk):compare(a, b) -> array of lines (Python Differ.compare output).
local Differ = {}
Differ.__index = Differ
function M.Differ(linejunk, charjunk) return setmetatable({ linejunk = linejunk, charjunk = charjunk }, Differ) end

local function dump(out, tag, x, lo, hi) for i = lo + 1, hi do out[#out + 1] = tag .. " " .. x[i] end end

function Differ:_plain_replace(out, a, alo, ahi, b, blo, bhi)
  if bhi - blo < ahi - alo then dump(out, "+", b, blo, bhi); dump(out, "-", a, alo, ahi)
  else dump(out, "-", a, alo, ahi); dump(out, "+", b, blo, bhi) end
end

function Differ:_qformat(out, aline, bline, atags, btags)
  atags = keep_original_ws(aline, atags):gsub("%s+$", "")
  btags = keep_original_ws(bline, btags):gsub("%s+$", "")
  out[#out + 1] = "- " .. aline
  if atags ~= "" then out[#out + 1] = "? " .. atags .. "\n" end
  out[#out + 1] = "+ " .. bline
  if btags ~= "" then out[#out + 1] = "? " .. btags .. "\n" end
end

function Differ:_fancy_helper(out, a, alo, ahi, b, blo, bhi)
  if alo < ahi then
    if blo < bhi then self:_fancy_replace(out, a, alo, ahi, b, blo, bhi) else dump(out, "-", a, alo, ahi) end
  elseif blo < bhi then dump(out, "+", b, blo, bhi) end
end

function Differ:_fancy_replace(out, a, alo, ahi, b, blo, bhi)
  local best_ratio, cutoff = 0.74, 0.75
  local best_i, best_j
  local cruncher = M.SequenceMatcher(self.charjunk)
  local eqi, eqj
  for j = blo, bhi - 1 do
    local bj = b[j + 1]
    cruncher:set_seq2(bj)
    for i = alo, ahi - 1 do
      local ai = a[i + 1]
      if ai == bj then
        if eqi == nil then eqi, eqj = i, j end
      else
        cruncher:set_seq1(ai)
        if cruncher:real_quick_ratio() > best_ratio and cruncher:quick_ratio() > best_ratio and cruncher:ratio() > best_ratio then
          best_ratio, best_i, best_j = cruncher:ratio(), i, j
        end
      end
    end
  end
  if best_ratio < cutoff then
    if eqi == nil then self:_plain_replace(out, a, alo, ahi, b, blo, bhi); return end
    best_i, best_j, best_ratio = eqi, eqj, 1.0
  else
    eqi = nil
  end
  self:_fancy_helper(out, a, alo, best_i, b, blo, best_j)
  local aelt, belt = a[best_i + 1], b[best_j + 1]
  if eqi == nil then
    local atags, btags = {}, {}
    cruncher:set_seqs(aelt, belt)
    for _, c in ipairs(cruncher:get_opcodes()) do
      local tag, la, lb = c[1], c[3] - c[2], c[5] - c[4]
      if tag == "replace" then atags[#atags + 1] = ("^"):rep(la); btags[#btags + 1] = ("^"):rep(lb)
      elseif tag == "delete" then atags[#atags + 1] = ("-"):rep(la)
      elseif tag == "insert" then btags[#btags + 1] = ("+"):rep(lb)
      else atags[#atags + 1] = (" "):rep(la); btags[#btags + 1] = (" "):rep(lb) end
    end
    self:_qformat(out, aelt, belt, table.concat(atags), table.concat(btags))
  else
    out[#out + 1] = "  " .. aelt
  end
  self:_fancy_helper(out, a, best_i + 1, ahi, b, best_j + 1, bhi)
end

function Differ:compare(a, b)
  local out = {}
  local cruncher = M.SequenceMatcher(self.linejunk, a, b)
  for _, c in ipairs(cruncher:get_opcodes()) do
    local tag, alo, ahi, blo, bhi = c[1], c[2], c[3], c[4], c[5]
    if tag == "replace" then self:_fancy_replace(out, a, alo, ahi, b, blo, bhi)
    elseif tag == "delete" then dump(out, "-", a, alo, ahi)
    elseif tag == "insert" then dump(out, "+", b, blo, bhi)
    else dump(out, " ", a, alo, ahi) end
  end
  return out
end

function M.ndiff(a, b, linejunk, charjunk)
  if charjunk == nil then charjunk = M.IS_CHARACTER_JUNK end
  return M.Differ(linejunk, charjunk or nil):compare(a, b)
end

return M
