-- A small backtracking subset of Python's `re` for the market-data scripts (Unicode code points, not bytes).
-- Syntax: literals, ., [...] / [^...] with ranges, \d \D \s \S \w \W \b \B, escapes, ( ), (?: ), (?= ), (?! ),
-- |, * + ? {m} {m,} {m,n} (each optionally lazy with ?), ^ $ (\A \Z). Flags: "i" (ASCII case-insensitive).
-- \s / \d / \w follow Python's str rules (Unicode whitespace, decimal digits incl. full-width, letters/digits/_).
--   local re = require "market_regex"
--   local m = re.match("^(S)(.*)$", s)         -- anchored at the start; m[1], m[2] (nil if unset), m.start, m.stop
--   re.search(p, s), re.fullmatch(p, s), re.sub(p, repl, s), re.split(p, s), re.findall(p, s)
local M = {}

local function decode(s)
  local cps, n = {}, 0
  for _, c in utf8.codes(s, true) do n = n + 1; cps[n] = c end
  return cps, n
end
local function encode(cps, i, j)
  local parts = {}
  for k = i, j do parts[#parts + 1] = utf8.char(cps[k]) end
  return table.concat(parts)
end
M.decode, M.encode = decode, encode

local WS = { [9] = 1, [10] = 1, [11] = 1, [12] = 1, [13] = 1, [28] = 1, [29] = 1, [30] = 1, [31] = 1, [32] = 1, [0x85] = 1,
  [0xA0] = 1, [0x1680] = 1, [0x2028] = 1, [0x2029] = 1, [0x202F] = 1, [0x205F] = 1, [0x3000] = 1 }
local function is_space(c) return WS[c] ~= nil or (c >= 0x2000 and c <= 0x200A) end
local function is_digit(c)
  return (c >= 48 and c <= 57) or (c >= 0xFF10 and c <= 0xFF19) or (c >= 0x660 and c <= 0x669) or (c >= 0x6F0 and c <= 0x6F9)
    or (c >= 0x966 and c <= 0x96F)
end
local function is_word(c)
  if c < 128 then return (c >= 48 and c <= 57) or (c >= 65 and c <= 90) or (c >= 97 and c <= 122) or c == 95 end
  if is_space(c) then return false end
  -- CJK ideographs, kana, hangul, Latin-1/extended letters, full-width alphanumerics
  return (c >= 0x4E00 and c <= 0x9FFF) or (c >= 0x3400 and c <= 0x4DBF) or (c >= 0x3040 and c <= 0x30FF)
    or (c >= 0xAC00 and c <= 0xD7AF) or (c >= 0xC0 and c <= 0x24F and c ~= 0xD7 and c ~= 0xF7)
    or (c >= 0xFF10 and c <= 0xFF19) or (c >= 0xFF21 and c <= 0xFF3A) or (c >= 0xFF41 and c <= 0xFF5A)
    or (c >= 0x370 and c <= 0x3FF) or (c >= 0x400 and c <= 0x4FF) or (c >= 0x20000 and c <= 0x2FFFF)
end
M.is_space, M.is_digit, M.is_word = is_space, is_digit, is_word
local function lower(c) if c >= 65 and c <= 90 then return c + 32 end return c end

-- ---------------------------------------------------------------------------------------- parser
local function parse(pat, flags)
  local p, n = decode(pat)
  local i = 1
  local ngroups = 0
  local icase = flags and flags:find("i") ~= nil
  local function peek() return p[i] end
  local function eat(c) if p[i] == c then i = i + 1; return true end return false end
  local parse_alt

  local function class_escape(c)
    if c == 100 then return { kind = "pred", f = is_digit } -- \d
    elseif c == 68 then return { kind = "pred", f = function(x) return not is_digit(x) end }
    elseif c == 115 then return { kind = "pred", f = is_space }
    elseif c == 83 then return { kind = "pred", f = function(x) return not is_space(x) end }
    elseif c == 119 then return { kind = "pred", f = is_word }
    elseif c == 87 then return { kind = "pred", f = function(x) return not is_word(x) end } end
    local map = { [110] = 10, [116] = 9, [114] = 13, [102] = 12, [118] = 11 }
    return { kind = "char", c = map[c] or c }
  end

  local function parse_class()
    local neg = eat(94) -- ^
    local items = {}
    local first = true
    while i <= n and (p[i] ~= 93 or first) do -- ]
      first = false
      local c = p[i]; i = i + 1
      local item
      if c == 92 then item = class_escape(p[i]); i = i + 1 else item = { kind = "char", c = c } end
      if item.kind == "char" and p[i] == 45 and p[i + 1] and p[i + 1] ~= 93 then -- range a-b
        i = i + 1
        local c2 = p[i]; i = i + 1
        if c2 == 92 then c2 = class_escape(p[i]).c; i = i + 1 end
        item = { kind = "range", a = item.c, b = c2 }
      end
      items[#items + 1] = item
    end
    i = i + 1 -- ]
    local function test(x)
      for _, it in ipairs(items) do
        if it.kind == "char" then
          if x == it.c or (icase and lower(x) == lower(it.c)) then return true end
        elseif it.kind == "range" then
          if x >= it.a and x <= it.b then return true end
          if icase then local lx = lower(x); if lx >= lower(it.a) and lx <= lower(it.b) then return true end
            if x >= 97 and x <= 122 and (x - 32) >= it.a and (x - 32) <= it.b then return true end end
        elseif it.f(x) then return true end
      end
      return false
    end
    return { kind = "pred", f = neg and function(x) return not test(x) end or test }
  end

  local function parse_atom()
    local c = p[i]
    if c == 40 then -- (
      i = i + 1
      local node
      if p[i] == 63 then -- ?
        local t = p[i + 1]
        if t == 58 then i = i + 2; node = { kind = "group", body = parse_alt() }
        elseif t == 61 then i = i + 2; node = { kind = "look", neg = false, body = parse_alt() }
        elseif t == 33 then i = i + 2; node = { kind = "look", neg = true, body = parse_alt() }
        elseif t == 80 and p[i + 2] == 60 then -- (?P<name>
          i = i + 3
          while p[i] ~= 62 do i = i + 1 end
          i = i + 1
          ngroups = ngroups + 1
          local idx = ngroups
          node = { kind = "cap", idx = idx, body = parse_alt() }
        else error("regex: unsupported group syntax") end
      else
        ngroups = ngroups + 1
        local idx = ngroups
        node = { kind = "cap", idx = idx, body = parse_alt() }
      end
      if p[i] ~= 41 then error("regex: missing )") end
      i = i + 1
      return node
    elseif c == 91 then i = i + 1; return parse_class()
    elseif c == 46 then i = i + 1; return { kind = "pred", f = function(x) return x ~= 10 end }
    elseif c == 94 then i = i + 1; return { kind = "bol" }
    elseif c == 36 then i = i + 1; return { kind = "eol" }
    elseif c == 92 then
      local e = p[i + 1]; i = i + 2
      if e == 98 then return { kind = "wordb", neg = false } elseif e == 66 then return { kind = "wordb", neg = true }
      elseif e == 65 then return { kind = "bos" } elseif e == 90 then return { kind = "eos" } end
      local ce = class_escape(e)
      if ce.kind == "char" then return { kind = "char", c = ce.c } end
      return ce
    else
      i = i + 1
      return { kind = "char", c = c }
    end
  end

  local function parse_quant(atom)
    local c = p[i]
    local mn, mx
    if c == 42 then mn, mx = 0, math.huge; i = i + 1
    elseif c == 43 then mn, mx = 1, math.huge; i = i + 1
    elseif c == 63 then mn, mx = 0, 1; i = i + 1
    elseif c == 123 then
      local j = i + 1
      local a, b = "", nil
      while p[j] and p[j] >= 48 and p[j] <= 57 do a = a .. utf8.char(p[j]); j = j + 1 end
      if p[j] == 44 then
        j = j + 1; b = ""
        while p[j] and p[j] >= 48 and p[j] <= 57 do b = b .. utf8.char(p[j]); j = j + 1 end
      end
      if p[j] ~= 125 or a == "" then return atom end -- literal {
      i = j + 1
      mn = tonumber(a)
      mx = b == nil and mn or (b == "" and math.huge or tonumber(b))
    else
      return atom
    end
    local lazy = eat(63)
    return { kind = "rep", body = atom, min = mn, max = mx, lazy = lazy }
  end

  local function parse_seq()
    local items = {}
    while i <= n and p[i] ~= 124 and p[i] ~= 41 do
      items[#items + 1] = parse_quant(parse_atom())
    end
    return { kind = "seq", items = items }
  end

  parse_alt = function()
    local alts = { parse_seq() }
    while p[i] == 124 do i = i + 1; alts[#alts + 1] = parse_seq() end
    if #alts == 1 then return alts[1] end
    return { kind = "alt", alts = alts }
  end

  local ast = parse_alt()
  if i <= n then error("regex: unbalanced ) in " .. pat) end
  return ast, ngroups, icase
end

-- --------------------------------------------------------------------------------------- matcher
local function compile(ast, icase)
  local S, N, caps -- subject cps, length, capture array (set per run)
  local function build(node, k)
    local kind = node.kind
    if kind == "char" then
      local c = node.c
      if icase then
        local lc = lower(c)
        return function(pos) local x = S[pos]; if x and lower(x) == lc then return k(pos + 1) end return false end
      end
      return function(pos) if S[pos] == c then return k(pos + 1) end return false end
    elseif kind == "pred" then
      local f = node.f
      return function(pos) local x = S[pos]; if x and f(x) then return k(pos + 1) end return false end
    elseif kind == "seq" then
      local m = k
      for j = #node.items, 1, -1 do m = build(node.items[j], m) end
      return m
    elseif kind == "alt" then
      local ms = {}
      for j, a in ipairs(node.alts) do ms[j] = build(a, k) end
      return function(pos)
        for _, m in ipairs(ms) do local r = m(pos); if r then return r end end
        return false
      end
    elseif kind == "group" then
      return build(node.body, k)
    elseif kind == "cap" then
      local idx = node.idx
      local inner = build(node.body, function(pos)
        local old_e = caps[2 * idx]
        caps[2 * idx] = pos
        local r = k(pos)
        if not r then caps[2 * idx] = old_e end
        return r
      end)
      return function(pos)
        local old_s, old_e = caps[2 * idx - 1], caps[2 * idx]
        caps[2 * idx - 1] = pos
        local r = inner(pos)
        if not r then caps[2 * idx - 1], caps[2 * idx] = old_s, old_e end
        return r
      end
    elseif kind == "look" then
      local body = build(node.body, function(pos) return pos end)
      local neg = node.neg
      return function(pos)
        local r = body(pos)
        if neg then if r then return false end return k(pos) end
        if not r then return false end
        return k(pos)
      end
    elseif kind == "bol" or kind == "bos" then
      return function(pos) if pos == 1 then return k(pos) end return false end
    elseif kind == "eol" then
      return function(pos)
        if pos == N + 1 or (pos == N and S[N] == 10) then return k(pos) end
        return false
      end
    elseif kind == "eos" then
      return function(pos) if pos == N + 1 then return k(pos) end return false end
    elseif kind == "wordb" then
      local neg = node.neg
      return function(pos)
        local a = pos > 1 and is_word(S[pos - 1]) or false
        local b = pos <= N and is_word(S[pos]) or false
        local at = a ~= b
        if neg then at = not at end
        if at then return k(pos) end
        return false
      end
    elseif kind == "rep" then
      local mn, mx, lazy = node.min, node.max, node.lazy
      local body = node.body
      -- simple single-position bodies: iterate without recursion depth per repetition
      if body.kind == "char" or body.kind == "pred" then
        local test
        if body.kind == "char" then
          local c = body.c
          if icase then local lc = lower(c); test = function(x) return lower(x) == lc end
          else test = function(x) return x == c end end
        else test = body.f end
        if lazy then
          return function(pos)
            local cnt = 0
            while cnt < mn do
              local x = S[pos + cnt]
              if not (x and test(x)) then return false end
              cnt = cnt + 1
            end
            while true do
              local r = k(pos + cnt)
              if r then return r end
              if cnt >= mx then return false end
              local x = S[pos + cnt]
              if not (x and test(x)) then return false end
              cnt = cnt + 1
            end
          end
        end
        return function(pos)
          local cnt = 0
          while cnt < mx do
            local x = S[pos + cnt]
            if not (x and test(x)) then break end
            cnt = cnt + 1
          end
          while cnt >= mn do
            local r = k(pos + cnt)
            if r then return r end
            cnt = cnt - 1
          end
          return false
        end
      end
      -- general body: recursive with counts
      local step
      local function try(pos, cnt)
        if lazy then
          if cnt >= mn then local r = k(pos); if r then return r end end
          if cnt < mx then return step(pos, cnt) end
          return false
        end
        if cnt < mx then local r = step(pos, cnt); if r then return r end end
        if cnt >= mn then return k(pos) end
        return false
      end
      local cnt_stack = {}
      local m_body = build(body, function(pos2)
        local top = cnt_stack[#cnt_stack]
        if pos2 == top.pos and top.cnt >= mn then return false end -- empty iteration guard
        return try(pos2, top.cnt + 1)
      end)
      step = function(pos, cnt)
        cnt_stack[#cnt_stack + 1] = { pos = pos, cnt = cnt }
        local r = m_body(pos)
        cnt_stack[#cnt_stack] = nil
        return r
      end
      return function(pos) return try(pos, 0) end
    end
    error("regex: unknown node " .. tostring(kind))
  end
  local final_pos
  local m = build(ast, function(pos) final_pos = pos; return true end)
  return function(cps, n, start, c)
    S, N, caps = cps, n, c
    final_pos = nil
    if m(start) then return final_pos end
    return nil
  end
end

local cache = {}
local function get(pat, flags)
  local key = (flags or "") .. "\0" .. pat
  local e = cache[key]
  if not e then
    local ast, ng, icase = parse(pat, flags)
    e = { run = compile(ast, icase), ngroups = ng }
    cache[key] = e
  end
  return e
end

local function make_match(e, cps, s_start, s_end, caps)
  local m = { start = s_start, stop = s_end, cps = cps }
  m[0] = encode(cps, s_start, s_end - 1)
  for g = 1, e.ngroups do
    local a, b = caps[2 * g - 1], caps[2 * g]
    if a and b then m[g] = encode(cps, a, b - 1) end
  end
  m.spans = caps
  return m
end

local function subject(s) if type(s) == "table" then return s, #s end return decode(s) end

function M.match(pat, s, flags, pos)
  local e = get(pat, flags)
  local cps, n = subject(s)
  local caps = {}
  local stop = e.run(cps, n, pos or 1, caps)
  if not stop then return nil end
  return make_match(e, cps, pos or 1, stop, caps)
end
function M.fullmatch(pat, s, flags)
  return M.match("(?:" .. pat .. ")\\Z", s, flags)
end
function M.search(pat, s, flags, pos)
  local e = get(pat, flags)
  local cps, n = subject(s)
  for st = pos or 1, n + 1 do
    local caps = {}
    local stop = e.run(cps, n, st, caps)
    if stop then return make_match(e, cps, st, stop, caps) end
  end
  return nil
end
-- re.sub(pat, repl, s): repl is a string (\1 / \g<1> references) or function(m) -> string.
function M.sub(pat, repl, s, flags)
  local e = get(pat, flags)
  local cps, n = subject(s)
  local out, last, pos = {}, 1, 1
  while pos <= n + 1 do
    local caps = {}
    local stop = e.run(cps, n, pos, caps)
    if stop then
      out[#out + 1] = encode(cps, last, pos - 1)
      local m = make_match(e, cps, pos, stop, caps)
      if type(repl) == "function" then out[#out + 1] = repl(m)
      else out[#out + 1] = (repl:gsub("\\g<(%d+)>", function(g) return m[tonumber(g)] or "" end):gsub("\\(%d)", function(g) return m[tonumber(g)] or "" end)) end
      if stop == pos then
        if pos <= n then out[#out + 1] = utf8.char(cps[pos]) end
        pos = pos + 1
      else
        pos = stop
      end
      last = pos
    else
      pos = pos + 1
    end
  end
  if last <= n then out[#out + 1] = encode(cps, last, n) end
  return table.concat(out)
end
function M.split(pat, s, flags)
  local e = get(pat, flags)
  local cps, n = subject(s)
  local out, last, pos = {}, 1, 1
  while pos <= n do
    local caps = {}
    local stop = e.run(cps, n, pos, caps)
    if stop and stop > pos then
      out[#out + 1] = encode(cps, last, pos - 1)
      for g = 1, e.ngroups do
        local a, b = caps[2 * g - 1], caps[2 * g]
        out[#out + 1] = (a and b) and encode(cps, a, b - 1) or nil
      end
      pos = stop
      last = pos
    else
      pos = pos + 1
    end
  end
  out[#out + 1] = encode(cps, last, n)
  return out
end
function M.findall(pat, s, flags)
  local out, pos = {}, 1
  local cps, n = subject(s)
  while pos <= n + 1 do
    local m = M.search(pat, cps, flags, pos)
    if not m then break end
    out[#out + 1] = m[1] or m[0]
    pos = m.stop > m.start and m.stop or m.stop + 1
  end
  return out
end

return M
