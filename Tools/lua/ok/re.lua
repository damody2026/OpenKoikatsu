-- Python `re` subset as a backtracking engine (byte based, UTF-8 safe for
-- matching ASCII patterns against UTF-8 text).
--   local re = ok.re
--   re.findall([[card=(.+?) n=(\d+)]], text)   -- {{"a","1"}, ...}; one group -> strings
--   local m = re.search([[x=(\d+)]], text); if m then print(m[1], m:group(0)) end
--   re.sub("[^A-Za-z0-9_]", "_", s); re.split([[\s*,\s*]], s); re.match/fullmatch/finditer
-- Supported: literals and escapes (\t \n \r \. \[ ...), . [] [^] ranges, \d \D \w \W \s \S,
-- * + ? {n} {n,} {n,m} and lazy forms, (...) (?:...) (?P<name>...), |, ^ $ \A \Z \b \B,
-- flags table {i=true, s=true (DOTALL), m=true (MULTILINE)} or inline (?i)(?s)(?m) at start.
-- Differences from Python: \w/\d/\s are ASCII; bytes >= 0x80 count as \w (letters
-- of UTF-8 text) and `.` consumes single bytes (same spans for ASCII-delimited patterns).
local M = {}
local byte, sub, find, char = string.byte, string.sub, string.find, string.char

-- Character class helpers ----------------------------------------------------
local function is_digit(b) return b >= 48 and b <= 57 end
local function is_word(b) return (b >= 48 and b <= 57) or (b >= 65 and b <= 90) or (b >= 97 and b <= 122) or b == 95 or b >= 128 end
local function is_space(b) return b == 32 or (b >= 9 and b <= 13) end

local function lower_b(b) if b >= 65 and b <= 90 then return b + 32 end return b end
local function upper_b(b) if b >= 97 and b <= 122 then return b - 32 end return b end

-- Parser -----------------------------------------------------------------------
-- Nodes: {t="lit", s=str} {t="set", f=fn(b)->bool} {t="seq", list} {t="alt", list}
--        {t="group", n=index|nil, node} {t="rep", node, min, max, lazy}
--        {t="bol"} {t="eol"} {t="bos"} {t="eos"} {t="wordb", neg}
local Parser = {}
Parser.__index = Parser

local function perr(p, msg) error(("re: %s at position %d in %q"):format(msg, p.i - 1, p.src), 0) end

function Parser:peek(n) return sub(self.src, self.i, self.i + (n or 1) - 1) end
function Parser:next() local c = sub(self.src, self.i, self.i); self.i = self.i + 1; return c end

local ESC = { t = 9, n = 10, r = 13, f = 12, v = 11, a = 7, ["0"] = 0 }

-- Returns a set predicate for class escapes, or a byte for a literal escape.
function Parser:escape()
  local c = self:next()
  if c == "" then perr(self, "bad escape (end of pattern)") end
  if c == "d" then return is_digit end
  if c == "D" then return function(b) return not is_digit(b) end end
  if c == "w" then return is_word end
  if c == "W" then return function(b) return not is_word(b) end end
  if c == "s" then return is_space end
  if c == "S" then return function(b) return not is_space(b) end end
  if c == "x" then local h = self:next() .. self:next(); return tonumber(h, 16) end
  if ESC[c] then return ESC[c] end
  return byte(c)
end

function Parser:class()
  -- after '['
  local neg = false
  if self:peek() == "^" then neg = true; self.i = self.i + 1 end
  local items, first = {}, true
  while true do
    local c = self:next()
    if c == "" then perr(self, "unterminated character set") end
    if c == "]" and not first then break end
    first = false
    local lo
    if c == "\\" then
      local e = self:escape()
      if type(e) == "function" then items[#items + 1] = e; goto continue end
      lo = e
    else lo = byte(c) end
    if self:peek() == "-" and self:peek(2) ~= "-]" and sub(self.src, self.i + 1, self.i + 1) ~= "]" and sub(self.src, self.i + 1, self.i + 1) ~= "" then
      self.i = self.i + 1
      local d = self:next()
      local hi
      if d == "\\" then hi = self:escape(); if type(hi) == "function" then perr(self, "bad character range") end
      else hi = byte(d) end
      local a, z = lo, hi
      items[#items + 1] = function(b) return b >= a and b <= z end
    else
      local v = lo
      items[#items + 1] = function(b) return b == v end
    end
    ::continue::
  end
  local icase = self.flags.i
  local n = #items
  return function(b)
    local hit = false
    for k = 1, n do if items[k](b) then hit = true break end end
    if not hit and icase then
      local o = lower_b(b); if o == b then o = upper_b(b) end
      if o ~= b then for k = 1, n do if items[k](o) then hit = true break end end end
    end
    if neg then return not hit end
    return hit
  end
end

function Parser:atom()
  local c = self:next()
  if c == "(" then
    local n, name
    if self:peek() == "?" then
      self.i = self.i + 1
      local d = self:next()
      if d == ":" then n = nil
      elseif d == "P" and self:peek() == "<" then
        self.i = self.i + 1
        local close = find(self.src, ">", self.i, true) or perr(self, "unterminated group name")
        name = sub(self.src, self.i, close - 1); self.i = close + 1
        self.ngroups = self.ngroups + 1; n = self.ngroups; self.names[name] = n
      else perr(self, "unsupported group (?" .. d) end
    else
      self.ngroups = self.ngroups + 1; n = self.ngroups
    end
    local node = self:alt()
    if self:next() ~= ")" then perr(self, "missing )") end
    return { t = "group", n = n, node = node }
  elseif c == "[" then return { t = "set", f = self:class() }
  elseif c == "." then
    if self.flags.s then return { t = "set", f = function() return true end } end
    return { t = "set", f = function(b) return b ~= 10 end }
  elseif c == "^" then return { t = self.flags.m and "bol" or "bos" }
  elseif c == "$" then return { t = self.flags.m and "eol" or "eos_nl" }
  elseif c == "\\" then
    local d = self:peek()
    if d == "A" then self.i = self.i + 1; return { t = "bos" } end
    if d == "Z" then self.i = self.i + 1; return { t = "eos" } end
    if d == "b" then self.i = self.i + 1; return { t = "wordb", neg = false } end
    if d == "B" then self.i = self.i + 1; return { t = "wordb", neg = true } end
    local e = self:escape()
    if type(e) == "function" then return { t = "set", f = e } end
    return self:literal(e)
  else
    return self:literal(byte(c))
  end
end

function Parser:literal(b)
  if self.flags.i and lower_b(b) ~= upper_b(b) then
    local lo, up = lower_b(b), upper_b(b)
    return { t = "set", f = function(x) return x == lo or x == up end }
  end
  return { t = "lit", s = char(b) }
end

function Parser:quant(node)
  local c = self:peek()
  local min, max
  if c == "*" then min, max = 0, math.huge
  elseif c == "+" then min, max = 1, math.huge
  elseif c == "?" then min, max = 0, 1
  elseif c == "{" then
    local a, b, close = self.src:match("^{(%d*)(,?%d*)}()", self.i)
    if not a or (a == "" and b == "") then return node end
    min = tonumber(a) or 0
    if b == "" then max = min elseif b == "," then max = math.huge else max = tonumber(b:sub(2)) end
    self.i = close - 1
  else return node end
  self.i = self.i + 1
  local lazy = false
  if self:peek() == "?" then lazy = true; self.i = self.i + 1 end
  if node.t == "bos" or node.t == "eos" or node.t == "bol" or node.t == "eol" then perr(self, "nothing to repeat") end
  return { t = "rep", node = node, min = min, max = max, lazy = lazy }
end

function Parser:seq()
  local list = {}
  while true do
    local c = self:peek()
    if c == "" or c == "|" or c == ")" then break end
    local node = self:quant(self:atom())
    -- merge adjacent literals
    local last = list[#list]
    if node.t == "lit" and last and last.t == "lit" then last.s = last.s .. node.s
    else list[#list + 1] = node end
  end
  if #list == 1 then return list[1] end
  return { t = "seq", list = list }
end

function Parser:alt()
  local branches = { self:seq() }
  while self:peek() == "|" do self.i = self.i + 1; branches[#branches + 1] = self:seq() end
  if #branches == 1 then return branches[1] end
  return { t = "alt", list = branches }
end

-- Compiler: node -> matcher(i, k) using state upvalues ---------------------------
local function compile_node(node, st)
  local t = node.t
  if t == "lit" then
    local s, n = node.s, #node.s
    if n == 1 then
      local b0 = byte(s)
      return function(i, k) if byte(st.S, i) == b0 then return k(i + 1) end end
    end
    return function(i, k) if sub(st.S, i, i + n - 1) == s then return k(i + n) end end
  elseif t == "set" then
    local f = node.f
    return function(i, k) local b = byte(st.S, i); if b and f(b) then return k(i + 1) end end
  elseif t == "seq" then
    local ms = {}
    for idx, child in ipairs(node.list) do ms[idx] = compile_node(child, st) end
    local n = #ms
    local function run(idx, i, k)
      if idx > n then return k(i) end
      return ms[idx](i, function(j) return run(idx + 1, j, k) end)
    end
    return function(i, k) return run(1, i, k) end
  elseif t == "alt" then
    local ms = {}
    for idx, child in ipairs(node.list) do ms[idx] = compile_node(child, st) end
    return function(i, k)
      for idx = 1, #ms do local r = ms[idx](i, k); if r then return r end end
    end
  elseif t == "group" then
    local m, n = compile_node(node.node, st), node.n
    if not n then return m end
    return function(i, k)
      local caps = st.caps
      local os_, oe = caps[2 * n - 1], caps[2 * n]
      local r = m(i, function(j)
        local ps, pe = caps[2 * n - 1], caps[2 * n]
        caps[2 * n - 1], caps[2 * n] = i, j
        local r2 = k(j)
        if not r2 then caps[2 * n - 1], caps[2 * n] = ps, pe end
        return r2
      end)
      if not r then caps[2 * n - 1], caps[2 * n] = os_, oe end
      return r
    end
  elseif t == "rep" then
    local min, max, lazy, child = node.min, node.max, node.lazy, node.node
    if child.t == "set" then
      local f = child.f
      if lazy then
        return function(i, k)
          local S, j, c = st.S, i, 0
          while c < min do local b = byte(S, j); if not (b and f(b)) then return nil end; j = j + 1; c = c + 1 end
          while true do
            local r = k(j); if r then return r end
            if c >= max then return nil end
            local b = byte(S, j); if not (b and f(b)) then return nil end
            j = j + 1; c = c + 1
          end
        end
      end
      return function(i, k)
        local S, j, c = st.S, i, 0
        while c < max do local b = byte(S, j); if not (b and f(b)) then break end; j = j + 1; c = c + 1 end
        if c < min then return nil end
        while true do
          local r = k(j); if r then return r end
          if c <= min then return nil end
          j = j - 1; c = c - 1
        end
      end
    end
    local m = compile_node(child, st)
    local function rep(i, c, k)
      if lazy then
        if c >= min then local r = k(i); if r then return r end end
        if c < max then
          return m(i, function(j) if j == i and c >= min then return nil end return rep(j, c + 1, k) end)
        end
        return nil
      end
      if c < max then
        local r = m(i, function(j) if j == i and c >= min then return nil end return rep(j, c + 1, k) end)
        if r then return r end
      end
      if c >= min then return k(i) end
    end
    return function(i, k) return rep(i, 0, k) end
  elseif t == "bos" then
    return function(i, k) if i == 1 then return k(i) end end
  elseif t == "eos" then
    return function(i, k) if i == #st.S + 1 then return k(i) end end
  elseif t == "eos_nl" then
    return function(i, k)
      local n = #st.S
      if i == n + 1 or (i == n and byte(st.S, n) == 10) then return k(i) end
    end
  elseif t == "bol" then
    return function(i, k) if i == 1 or byte(st.S, i - 1) == 10 then return k(i) end end
  elseif t == "eol" then
    return function(i, k) if i == #st.S + 1 or byte(st.S, i) == 10 then return k(i) end end
  elseif t == "wordb" then
    local neg = node.neg
    return function(i, k)
      local a, b = byte(st.S, i - 1), byte(st.S, i)
      local wa, wb = a ~= nil and is_word(a), b ~= nil and is_word(b)
      if (wa ~= wb) ~= neg then return k(i) end
    end
  end
  error("re: internal node " .. tostring(t))
end

-- First-byte analysis: returns (set table | false for "any"), nullable.
local function first_info(node)
  local t = node.t
  if t == "lit" then return { [byte(node.s)] = true }, false end
  if t == "set" then
    local s, f = {}, node.f
    for b = 0, 255 do if f(b) then s[b] = true end end
    return s, false
  end
  if t == "group" then return first_info(node.node) end
  if t == "rep" then
    local s, nul = first_info(node.node)
    return s, nul or node.min == 0
  end
  if t == "alt" then
    local out, nul = {}, false
    for _, br in ipairs(node.list) do
      local s, n = first_info(br)
      if s == false then return false, true end
      for b in pairs(s) do out[b] = true end
      nul = nul or n
    end
    return out, nul
  end
  if t == "seq" then
    local out = {}
    for _, el in ipairs(node.list) do
      local s, n = first_info(el)
      if s == false then return false, true end
      for b in pairs(s) do out[b] = true end
      if not n then return out, false end
    end
    return out, true
  end
  -- zero-width assertions
  return {}, true
end

local function starts_with_bos(node)
  if node.t == "bos" then return true end
  if node.t == "seq" then return starts_with_bos(node.list[1]) end
  if node.t == "group" then return starts_with_bos(node.node) end
  return false
end

-- Leading literal for fast candidate search (only when unconditional).
local function leading_literal(node)
  if node.t == "lit" then return node.s end
  if node.t == "seq" then return leading_literal(node.list[1]) end
  if node.t == "group" then return leading_literal(node.node) end
  if node.t == "rep" and node.min >= 1 and node.node.t == "lit" then return node.node.s end
  return nil
end

local Pattern = {}
Pattern.__index = Pattern
local cache = {}

local function flags_key(flags)
  if type(flags) ~= "table" then return "" end
  return (flags.i and "i" or "") .. (flags.s and "s" or "") .. (flags.m and "m" or "")
end

function M.compile(pattern, flags)
  if getmetatable(pattern) == Pattern then return pattern end
  local key = flags_key(flags) .. "\0" .. pattern
  if cache[key] then return cache[key] end
  local fl = { i = flags and flags.i, s = flags and flags.s, m = flags and flags.m }
  local src = pattern
  while true do
    local f, rest = src:match("^%(%?([ism]+)%)(.*)$")
    if not f then break end
    for c in f:gmatch(".") do fl[c] = true end
    src = rest
  end
  local p = setmetatable({ src = src, i = 1, flags = fl, ngroups = 0, names = {} }, Parser)
  local ast = p:alt()
  if p.i <= #src then perr(p, "unbalanced parenthesis") end
  local st = { S = "", caps = {} }
  local first, nullable = first_info(ast)
  local obj = setmetatable({ pattern = pattern, groups = p.ngroups, groupindex = p.names, st = st,
    m = compile_node(ast, st), prefix = (not fl.i) and leading_literal(ast) or nil,
    first = (first and not nullable) and first or nil, bos = starts_with_bos(ast) }, Pattern)
  cache[key] = obj
  return obj
end

local Match = {}
Match.__index = function(t, k)
  if type(k) == "number" then return t:group(k) end
  return Match[k]
end

local function make_match(pat, s, i, e, caps)
  local m = setmetatable({ string = s, re = pat, pos0 = i, pos1 = e, caps = caps }, Match)
  return m
end
-- group(n) -> string or nil (Python None); group(0) whole match; name lookup too.
function Match:group(n)
  n = n or 0
  if type(n) == "string" then n = self.re.groupindex[n] end
  if n == 0 then return sub(self.string, self.pos0, self.pos1 - 1) end
  local a, b = self.caps[2 * n - 1], self.caps[2 * n]
  if not a then return nil end
  return sub(self.string, a, b - 1)
end
function Match:groups(default)
  local out = {}
  for n = 1, self.re.groups do local v = self:group(n); if v == nil then v = default end; out[n] = v end
  return out
end
-- 0-based start/end like Python (span of group n).
function Match:start(n) n = n or 0; if n == 0 then return self.pos0 - 1 end; local a = self.caps[2 * n - 1]; return a and a - 1 or -1 end
function Match:finish(n) n = n or 0; if n == 0 then return self.pos1 - 1 end; local b = self.caps[2 * n]; return b and b - 1 or -1 end
Match["end"] = Match.finish
function Match:span(n) return self:start(n), self:finish(n) end
function Match:groupdict()
  local out = {}
  for name, n in pairs(self.re.groupindex) do out[name] = self:group(n) end
  return out
end

-- Try a match exactly at position i (1-based); returns end index (exclusive) or nil.
function Pattern:_at(s, i)
  local st = self.st
  st.S = s
  local caps = {}
  st.caps = caps
  local e = self.m(i, function(j) return j end)
  if e then return e, caps end
  return nil
end

-- Search from 1-based pos; returns start, end(excl), caps.
function Pattern:_search(s, pos, anchored_end)
  local n = #s
  local prefix, first = self.prefix, self.first
  local i = pos
  local st = self.st
  st.S = s
  local caps = {}
  st.caps = caps
  local m = self.m
  local function k(j) return j end
  if self.bos then
    if i ~= 1 then return nil end
    local e = m(1, k)
    if e then return 1, e, caps end
    return nil
  end
  while i <= n + 1 do
    if prefix then
      local a = find(s, prefix, i, true)
      if not a then return nil end
      i = a
    elseif first then
      while true do
        local b = byte(s, i)
        if b == nil then return nil end
        if first[b] then break end
        i = i + 1
      end
    end
    local e = m(i, k)
    if e then return i, e, caps end
    i = i + 1
  end
  return nil
end

function Pattern:search(s, pos)
  local a, e, caps = self:_search(s, (pos or 0) + 1)
  if a then return make_match(self, s, a, e, caps) end
  return nil
end
function Pattern:match(s, pos)
  local i = (pos or 0) + 1
  local e, caps = self:_at(s, i)
  if e then return make_match(self, s, i, e, caps) end
  return nil
end
function Pattern:fullmatch(s, pos)
  local i = (pos or 0) + 1
  local st = self.st
  st.S = s
  local caps = {}
  st.caps = caps
  local n = #s
  local e = self.m(i, function(j) if j == n + 1 then return j end end)
  if e then return make_match(self, s, i, e, caps) end
  return nil
end

-- Iterate non-overlapping matches like re.finditer (empty matches advance by one).
function Pattern:finditer(s)
  local out = {}
  local pos, n = 1, #s
  while pos <= n + 1 do
    local a, e, caps = self:_search(s, pos)
    if not a then break end
    out[#out + 1] = make_match(self, s, a, e, caps)
    if e == a then pos = a + 1 else pos = e end
  end
  local i = 0
  return function() i = i + 1; return out[i] end, out
end

function Pattern:findall(s)
  local out = {}
  local _, list = self:finditer(s)
  for _, m in ipairs(list) do
    if self.groups == 0 then out[#out + 1] = m:group(0)
    elseif self.groups == 1 then out[#out + 1] = m:group(1) or ""
    else out[#out + 1] = m:groups("") end
  end
  return out
end

-- Python-style replacement template: \1, \g<1>, \g<name>, \\ ; or a function(match).
local function expand(m, repl)
  if type(repl) == "function" then return repl(m) end
  return (repl:gsub("\\(g<([^>]+)>)", function(_, ref)
    return m:group(tonumber(ref) or ref) or ""
  end):gsub("\\(%d%d?)", function(d) return m:group(tonumber(d)) or "" end)
    :gsub("\\([ntr\\])", { n = "\n", t = "\t", r = "\r", ["\\"] = "\\" }))
end

function Pattern:subn(repl, s, count)
  count = count or 0
  local parts, pos, n, done = {}, 1, #s, 0
  while pos <= n + 1 do
    if count > 0 and done >= count then break end
    local a, e, caps = self:_search(s, pos)
    if not a then break end
    local m = make_match(self, s, a, e, caps)
    parts[#parts + 1] = sub(s, pos, a - 1)
    parts[#parts + 1] = expand(m, repl)
    done = done + 1
    if e == a then
      parts[#parts + 1] = sub(s, a, a)
      pos = a + 1
    else pos = e end
  end
  parts[#parts + 1] = sub(s, pos)
  return table.concat(parts), done
end
function Pattern:sub(repl, s, count) return (self:subn(repl, s, count)) end

function Pattern:split(s, maxsplit)
  maxsplit = maxsplit or 0
  local out, pos, n, done = {}, 1, #s, 0
  local search_pos = 1
  while search_pos <= n + 1 do
    if maxsplit > 0 and done >= maxsplit then break end
    local a, e, caps = self:_search(s, search_pos)
    if not a then break end
    if e == a and a > n then break end
    local m = make_match(self, s, a, e, caps)
    out[#out + 1] = sub(s, pos, a - 1)
    for g = 1, self.groups do out[#out + 1] = m:group(g) or require("ok.json").null end
    done = done + 1
    pos = e
    search_pos = (e == a) and a + 1 or e
  end
  out[#out + 1] = sub(s, pos)
  return out
end

-- Module-level API mirroring Python: re.findall(pattern, s, flags) etc.
function M.search(p, s, flags) return M.compile(p, flags):search(s) end
function M.match(p, s, flags) return M.compile(p, flags):match(s) end
function M.fullmatch(p, s, flags) return M.compile(p, flags):fullmatch(s) end
function M.findall(p, s, flags) return M.compile(p, flags):findall(s) end
function M.finditer(p, s, flags) return M.compile(p, flags):finditer(s) end
function M.sub(p, repl, s, count, flags) return M.compile(p, flags):sub(repl, s, count) end
function M.subn(p, repl, s, count, flags) return M.compile(p, flags):subn(repl, s, count) end
function M.split(p, s, maxsplit, flags) return M.compile(p, flags):split(s, maxsplit) end
-- re.escape
function M.escape(s) return (s:gsub("[^%w_]", function(c) if byte(c) >= 128 then return c end return "\\" .. c end)) end

return M
