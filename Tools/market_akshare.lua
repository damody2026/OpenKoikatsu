-- The AKShare calls build_ashare_data.lua / verify_ashare_data.lua need, reimplemented on curl + JSON / HTML
-- tables (AKShare 1.19.1 + pandas 3 semantics). Each function returns a frame as the Python scripts stored it
-- after `df[c].astype(str)` (_df_rows): {columns = {name, ...}, rows = {{str, ...}, ...}}; missing values are
-- the string "nan" (pandas 3 keeps NaN through astype(str) and csv writes it as nan).
--   stock_info_bj_name_code()                                  BSE list (www.bse.cn)
--   stock_zh_a_daily(symbol, start_date, end_date)             Sina unadjusted daily bars (adjust="")
--   stock_zh_index_daily(symbol)                               Sina index bars
--   stock_zh_a_hist(symbol, start_date, end_date)              Eastmoney daily bars (period daily, adjust "")
--   stock_fhps_detail_em(symbol)                               Eastmoney dividend / bonus plans
--   stock_history_dividend_detail(symbol, indicator)           Sina 分红 / 配股 tables
-- Sina's klc_kl.js payloads are decoded with a port of AKShare's hk_js_decode (JavaScript run by mini_racer
-- in Python).
local ok = require "ok"
local json, kit, util = ok.json, ok.kit, ok.util
local html = require "market_html"
local M = {}

local NAN = 0 / 0
local function isnan(v) return type(v) == "number" and v ~= v end
local NA = setmetatable({}, { __tostring = function() return "NA" end }) -- missing cell
M.NA = NA

-- ------------------------------------------------------------------------------------------- HTTP
M.UA = { ["User-Agent"] = "python-requests/2.32.5" }
-- M.fetch(method, url, opts) -> {status, body}; replaceable for offline replays.
function M.fetch(method, url, opts)
  opts = opts or {}
  local r = kit.http_request(url, { method = method ~= "get" and method:upper() or nil, params = opts.params, form = opts.form,
    headers = opts.headers or M.UA, timeout = opts.timeout or 60 })
  if r.status == -1 then error("ConnectionError(" .. util.repr(r.error or "") .. ")", 0) end
  return r
end
local function decode_text(body, enc)
  if enc and enc ~= "utf-8" then return kit.call({ "text-decode", enc, "-" }, { input = body }).text end
  return body
end
local function get_text(url, opts, enc) return decode_text(M.fetch("get", url, opts).body, enc) end

-- ------------------------------------------------------------------------------------ JS numbers
local function int32(x)
  if x == nil or x == true then return x == true and 1 or 0 end
  if x == false then return 0 end
  if x ~= x or x == math.huge or x == -math.huge then return 0 end
  if math.type(x) ~= "integer" then
    x = x >= 0 and math.floor(x) or math.ceil(x)
    x = math.fmod(x, 4294967296.0)
    x = math.tointeger(x) or 0
  end
  x = x % 4294967296
  if x >= 2147483648 then x = x - 4294967296 end
  return x
end
local function band(a, b) return int32(int32(a) & int32(b)) end
local function bor(a, b) return int32(int32(a) | int32(b)) end
local function bxor(a, b) return int32(int32(a) ~ int32(b)) end
local function shl(a, b) return int32(int32(a) << (int32(b) & 31)) end
local function shr(a, b) return int32(a) // (1 << (int32(b) & 31)) end
local function truthy(v) return not (v == nil or v == false or v == 0 or isnan(v)) end
local function num(v) -- ToNumber of booleans / undefined
  if v == true then return 1 elseif v == false then return 0 elseif v == nil then return NAN end
  return v
end

-- Number.prototype.toString()
local function js_str(x)
  if x ~= x then return "NaN" end
  if x == math.huge then return "Infinity" elseif x == -math.huge then return "-Infinity" end
  if x == 0 then return "0" end
  if math.type(x) == "integer" then return string.format("%d", x) end
  local r = json.float_repr(x) -- shortest round-trip digits (Python repr)
  local sign = ""
  if r:sub(1, 1) == "-" then sign = "-"; r = r:sub(2) end
  local mant, ex = r:match("^([%d%.]+)e([+-]%d+)$")
  local e10 = 0
  if mant then e10 = tonumber(ex) else mant = r end
  local ip, fp = mant:match("^(%d*)%.?(%d*)$")
  local digits = (ip .. fp):gsub("^0+", "")
  local nexp = #ip + e10 -- value = 0.digits x 10^n when no leading zeros stripped
  if ip == "0" or ip == "" then
    local lead = fp:match("^0*")
    nexp = e10 - #lead
    digits = fp:sub(#lead + 1)
  end
  digits = digits:gsub("0+$", "")
  if digits == "" then return "0" end
  local k, n = #digits, nexp
  if k <= n and n <= 21 then return sign .. digits .. string.rep("0", n - k) end
  if 0 < n and n <= 21 then return sign .. digits:sub(1, n) .. "." .. digits:sub(n + 1) end
  if -6 < n and n <= 0 then return sign .. "0." .. string.rep("0", -n) .. digits end
  local e = n - 1
  return sign .. digits:sub(1, 1) .. (k > 1 and ("." .. digits:sub(2)) or "") .. "e" .. (e >= 0 and "+" or "-") .. math.abs(e)
end
M.js_str = js_str

-- Number(string)
local function js_num(s)
  s = s:gsub("^[%s\u{a0}\u{feff}]+", ""):gsub("[%s\u{a0}\u{feff}]+$", "")
  if s == "" then return 0 end
  if s == "Infinity" or s == "+Infinity" then return math.huge elseif s == "-Infinity" then return -math.huge end
  if s:match("^0[xX]%x+$") then return tonumber(s:sub(3), 16) + 0.0 end
  if s:match("^[+-]?%d+%.?%d*$") or s:match("^[+-]?%.%d+$") or s:match("^[+-]?%d+%.?%d*[eE][+-]?%d+$") or s:match("^[+-]?%.%d+[eE][+-]?%d+$") then
    local v = tonumber(s)
    if v then return v + 0.0 end
  end
  return NAN
end
M.js_num = js_num

-- -------------------------------------------------------------------------- hk_js_decode (port)
local ALPHA = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
local AIDX = {}
for k = 1, 64 do AIDX[ALPHA:sub(k, k)] = k - 1 end
local M15 = { [0] = 0, 3, 5, 6, 9, 10, 12, 15, 17, 18, 20, 23, 24, 27, 29, 30 }
local H30 = 0x3FFFFFFF
local F30 = 1 << 30

-- Records: list of {fields = {name -> number}, keys = {name order}, date = epoch day}
function M.sina_decode(t)
  local i, n = {}, #t
  for k = 1, n do i[k - 1] = AIDX[t:sub(k, k)] or -1 end
  local e, o = 0, 0
  local d = {}
  for l = 0, 63 do d[l] = 2.0 ^ l end
  local r = setmetatable({}, { __index = function() return NAN end })
  local U = 7657
  local s
  local function y()
    if e >= n then return false end
    local v = band(i[e], shl(1, o))
    o = o + 1
    if o >= 6 then o = o - 6; e = e + 1 end
    return v ~= 0
  end
  local function new_list() return setmetatable({}, { __index = function() return NAN end }) end
  local w
  w = function(tt, rr, aa)
    local l = new_list()
    rr = rr or {}; aa = aa or {}
    for s_ = 1, #tt do
      local c = tt[s_]
      local u = 0
      if truthy(c) then
        if e >= n then return l end
        if tt[s_] <= 0 then u = 0
        elseif tt[s_] <= 30 then
          repeat
            local h = 6 - o
            h = c > h and h or c
            u = bor(u, shl(band(shr(i[e], o), shl(1, h) - 1), tt[s_] - c))
            o = o + h
            if o >= 6 then o = o - 6; e = e + 1 end
            c = c - h
          until c <= 0
          if truthy(rr[s_]) and u >= d[tt[s_] - 1] then u = u - d[tt[s_]] end
        else
          u = w({ 30, tt[s_] - 30 }, { 0, rr[s_] })
          if not truthy(aa[s_]) then u = u[1] + u[2] * d[30] end
        end
        l[s_] = u
      else
        l[s_] = 0
      end
    end
    return l
  end
  local function N()
    local t_ = y()
    local k = 1
    while true do
      if not y() then return k * (2 * (t_ and 1 or 0) - 1) end
      k = k + 1
    end
  end
  local function x()
    local t_ = w({ 3 })[1]
    if t_ == 1 then r.d = w({ 18 }, { 1 })[1]; t_ = 0
    elseif not truthy(t_) then t_ = w({ 6 })[1] end
    return t_
  end
  local function S(t_)
    local k = 0
    while (num(t_) > k) do
      r.d = r.d + 1
      local nn = math.fmod(r.d, 7)
      if nn == 3 or nn == 4 then r.d = r.d + 5 - nn end
      k = k + 1
    end
    return U + r.d
  end
  local function K(t_)
    local nn = truthy(r.wd) and r.wd or 62
    local k = 0
    while (num(t_) > k) do
      repeat r.d = r.d + 1 until band(nn, shl(1, math.fmod(math.fmod(r.d, 7) + 10, 7))) ~= 0
      k = k + 1
    end
    return U + r.d
  end
  local T
  T = function(t_)
    if not truthy(t_) then return { 0, 0 } end
    if t_ < 0 then local q = T(-t_); return { -q[1], -q[2] } end
    local rem = math.fmod(t_, 3)
    local q = (t_ - rem) / 3
    local out = { q, q }
    if rem ~= 0 then out[math.tointeger(rem)] = out[math.tointeger(rem)] + 1 end
    return out
  end
  local function E(tv)
    if not truthy(tv) then tv = 0 end
    local str = js_str(tv)
    local ii = str:lower():find("e", 1, true)
    if ii and ii - 1 > 0 then
      local out = {}
      local ev = js_num(str:sub(ii + 1))
      while ev >= 0 do
        out[#out + 1] = js_str(math.floor(ev * 10 ^ (-ev) + .5) + 0.0)
        ev = ev - 1
      end
      return table.concat(out)
    end
    return str
  end
  local function P(tv, ev, iv)
    local rr = type(ev) == "number" and T(ev) or ev
    local a_ = T(iv)
    local nn = { a_[1] - rr[1], a_[2] - rr[2] }
    local rm = 1
    while nn[1] < nn[2] do rm = rm * 5; nn[2] = nn[2] - 1 end
    while nn[2] < nn[1] do rm = rm * 2; nn[1] = nn[1] - 1 end
    if rm > 1 then tv = num(tv) * rm end
    local nv = nn[1]
    local ts = E(tv)
    if nv < 0 then
      while #ts + nv <= 0 do ts = "0" .. ts end
      nv = nv + #ts
      local r2 = js_num(ts:sub(1, nv))
      if iv == nil then return js_num(js_str(r2) .. "." .. ts:sub(nv + 1)) end
      local av = js_num(ts:sub(nv + 1, nv + 1))
      if av > 5 then r2 = r2 + 1
      elseif av == 5 then
        if js_num(ts:sub(nv + 2)) > 0 then r2 = r2 + 1 else r2 = r2 + band(1, r2) end
      end
      return r2
    end
    for _ = 1, nv do ts = ts .. "0" end
    return js_num(ts)
  end

  local function O_()
    for k, v in pairs({ b_avp = 1, b_ph = 0, b_phx = 0, b_sep = 0, p_p = 6, p_v = 0, p_a = 0, p_e = 0, p_t = 0, l_o = 3, l_h = 3,
      l_l = 3, l_c = 3, l_v = 5, l_a = 5, l_e = 3, l_t = 0, u_p = 0, u_v = 0, u_a = 0, wd = 62, d = 0 }) do r[k] = v end
    if s > 0 then return {} end
    local out = {}
    local prev
    while true do
      if e >= n then return nil end
      local a = setmetatable({ d = 1, c = 0 }, { __index = function() return NAN end })
      if y() then
        if y() then
          if y() then
            a.c = a.c + 1
            a.a = r.b_avp
            if y() then
              r.b_avp = bxor(r.b_avp, y() and 1 or 0)
              r.b_ph = bxor(r.b_ph, y() and 1 or 0)
              r.b_phx = bxor(r.b_phx, y() and 1 or 0)
              a.s = r.b_sep
              r.b_sep = bxor(r.b_sep, y() and 1 or 0)
              if y() then r.wd = w({ 7 })[1] end
              if bxor(a.s, r.b_sep) ~= 0 then
                if truthy(a.s) then r.u_p = r.u_c
                else r.u_c = r.u_p; r.u_l = r.u_c; r.u_h = r.u_l; r.u_o = r.u_h end
              end
            end
            local u = 0
            while u < 3 + 2 * r.b_ph do
              if y() then
                local l = ("pvaet"):sub(u + 1, u + 1)
                local ov = r["p_" .. l]
                r["p_" .. l] = r["p_" .. l] + N()
                r["u_" .. l] = P(r["u_" .. l], ov, r["p_" .. l])
                if truthy(r.b_sep) and u == 0 then
                  for c = 0, 3 do
                    local l2 = ("ohlc"):sub(c + 1, c + 1)
                    r["u_" .. l2] = P(r["u_" .. l2], ov, r.p_p)
                  end
                end
              end
              u = u + 1
            end
            if not truthy(r.b_avp) and truthy(a.a) then
              local am = prev and truthy(prev.fields.amount) and prev.fields.amount or 0
              r.u_a = P(am, 0, r.p_a)
            end
          end
          if y() then
            a.c = a.c + 1
            local u = 0
            while u < 7 + r.b_ph + r.b_phx do
              if y() then
                if u == 6 then a.d = x()
                else
                  local key = "l_" .. ("ohlcva*et"):sub(u + 1, u + 1)
                  r[key] = r[key] + N()
                end
              end
              u = u + 1
            end
          end
          if y() then
            a.c = a.c + 1
            local l = r.l_o + (y() and N() or 0)
            local ov = w({ 3 * l }, { 1 })[1]
            if truthy(r.b_sep) then a.p = r.u_c + ov
            else r.u_p = r.u_p + ov; a.p = r.u_p end
          end
          if not truthy(a.c) then break end
        else
          if y() then
            if y() then
              if y() then a.d = x() else r.l_v = r.l_v + N() end
            else
              if truthy(r.b_ph) and y() then
                local ch = 0
                if truthy(r.b_phx) then ch = y() and 1 or 0 end
                local key = "l_" .. ("et"):sub(ch + 1, ch + 1)
                r[key] = r[key] + N()
              else
                r.l_a = r.l_a + N()
              end
            end
          else
            local wv = w({ 2 })[1]
            local idx = isnan(wv) and 0 or math.tointeger(wv)
            local key = "l_" .. ("ohlc"):sub(idx + 1, idx + 1)
            r[key] = r[key] + N()
          end
        end
      end
      for u = 0, 5 + r.b_ph + r.b_phx do
        local c = ("ohlcvaet"):sub(u + 1, u + 1)
        local ov = band(shr(truthy(r.b_sep) and 191 or 185, u), 1)
        a["v_" .. c] = w({ 3 * r["l_" .. c] }, { ov })[1]
      end
      local rec = { fields = {}, keys = {} }
      local function put(k, v) rec.fields[k] = v; rec.keys[#rec.keys + 1] = k end
      rec.date = K(a.d)
      rec.keys[1] = "date"
      if truthy(a.p) then put("prevclose", P(a.p, r.p_p)) end
      if truthy(r.b_sep) then
        r.u_o = r.u_o + a.v_o; put("open", P(r.u_o, r.p_p))
        r.u_h = r.u_h + a.v_h; put("high", P(r.u_h, r.p_p))
        r.u_l = r.u_l + a.v_l; put("low", P(r.u_l, r.p_p))
        r.u_c = r.u_c + a.v_c; put("close", P(r.u_c, r.p_p))
      else
        a.o = r.u_p + a.v_o
        put("open", P(a.o, r.p_p))
        put("high", P(a.o + a.v_h, r.p_p))
        put("low", P(a.o - a.v_l, r.p_p))
        r.u_p = a.o + a.v_c
        put("close", P(r.u_p, r.p_p))
      end
      r.u_v = r.u_v + a.v_v
      put("volume", P(r.u_v, r.p_v))
      if truthy(r.b_avp) then
        local oo, ll = T(r.p_p), T(r.p_v)
        local base = truthy(r.b_sep) and (r.u_o + r.u_h + r.u_l + r.u_c) / 4 or a.o + (a.v_h - a.v_l + a.v_c) / 4
        put("amount", P(P(math.floor(base * r.u_v + .5) + 0.0, { oo[1] + ll[1], oo[2] + ll[2] }, r.p_a) + a.v_a, r.p_a))
      else
        r.u_a = r.u_a + a.v_a
        put("amount", P(r.u_a, r.p_a))
      end
      if truthy(r.b_ph) then
        put("postVol", P(a.v_e, r.p_e))
        put("postAmt", P(math.floor(rec.fields.postVol * rec.fields.close + (truthy(r.b_phx) and P(a.v_t, r.p_t) or 0) + .5) + 0.0, 0))
      end
      out[#out + 1] = rec
      prev = rec
    end
    return out
  end

  local function D_()
    if s >= 1 then return {} end
    r.lv = 0; r.ld = 0; r.cd = 0; r.cv = { 0, 0 }
    r.p = w({ 6 })[1]
    r.d = w({ 18 }, { 1 })[1] - 1
    r.m = 10 ^ r.p
    local a = w({ 3, 3 })
    r.md = a[1]; r.mv = a[2]
    local out = {}
    while true do
      a = w({ 6 })
      if #a == 0 then break end
      local iv = setmetatable({ c = a[1], d = 1 }, { __index = function() return nil end })
      local rec = { fields = {}, keys = { "date" } }
      local function put(k, v) rec.fields[k] = v; rec.keys[#rec.keys + 1] = k end
      if band(32, iv.c) ~= 0 then
        while true do
          local av = w({ 6 })[1]
          if bor(16, av) == 63 then
            local l = band(16, av) ~= 0 and "x" or "u"
            local a2 = w({ 3, 3 })
            iv[l .. "_d"] = a2[1] + r.md
            iv[l .. "_v"] = a2[2] + r.mv
            break
          end
          if band(32, av) ~= 0 then
            local ov = band(8, av) ~= 0 and "d" or "v"
            local l = band(16, av) ~= 0 and "x" or "u"
            iv[l .. "_" .. ov] = band(7, av) + r["m" .. ov]
            break
          end
          local ov = band(15, av)
          if ov == 0 then iv.d = w({ 6 })[1]
          elseif ov == 1 then r.d = w({ 18 })[1]; iv.d = 0
          else iv.d = ov end
          if band(16, av) == 0 then break end
        end
      end
      rec.date = S(iv.d)
      for _, ov in ipairs({ "v", "d" }) do
        if iv["x_" .. ov] ~= nil then r["l" .. ov] = iv["x_" .. ov] end
        if iv["u_" .. ov] == nil then iv["u_" .. ov] = r["l" .. ov] end
      end
      local ll = { iv.u_d, iv.u_d, iv.u_d, iv.u_d, iv.u_v }
      local l = M15[band(15, iv.c)]
      if band(1, iv.u_v) ~= 0 then l = 31 - l end
      if band(16, iv.c) ~= 0 then ll[5] = ll[5] + 2 end
      for k = 0, 4 do
        if band(l, shl(1, 4 - k)) ~= 0 then ll[k + 1] = ll[k + 1] + 1 end
        ll[k + 1] = ll[k + 1] * 3
      end
      local dv = w(ll, { 1, 0, 0, 1, 1 }, { 0, 0, 0, 0, 1 })
      local ov = r.cd + dv[1]
      put("open", ov / r.m)
      put("high", (ov + dv[2]) / r.m)
      put("low", (ov - dv[3]) / r.m)
      put("close", (ov + dv[4]) / r.m)
      local av = dv[5]
      if type(av) == "number" then av = { av, av >= 0 and 0 or -1 } end
      r.cd = ov + dv[4]
      local lsum = r.cv[1] + av[1]
      local carry = band(band(r.cv[1], H30) + band(av[1], H30), F30) ~= 0 and 1 or 0
      r.cv = { band(lsum, H30), r.cv[2] + av[2] + carry }
      put("volume", band(r.cv[1], F30 - 1) + r.cv[2] * F30)
      out[#out + 1] = rec
    end
    return out
  end

  local u = w({ 12, 6 })
  s = bxor(63, u[2])
  local which = u[1]
  if which == 3466 then return O_() end
  if which == 1479 then return D_() end
  error("sina_decode: unsupported payload type _" .. js_str(which))
end

-- ---------------------------------------------------------------------------- pandas emulation
local function is_int(v) return math.type(v) == "integer" or (type(v) == "number" and v == math.floor(v) and v ~= math.huge and v ~= -math.huge) end
-- str() of a float64 / int64 cell
local function fstr(v) if isnan(v) or v == NA then return "nan" end; return util.num(v + 0.0) end
local function istr(v) return string.format("%d", math.tointeger(v) or v) end

-- pandas column from Python objects (JSON values / mini_racer numbers): dtype + values
local function obj_column(vals, n)
  local all_num, any_float, any_missing, any_val = true, false, false, false
  for k = 1, n do
    local v = vals[k]
    if v == nil or v == NA or v == json.null then any_missing = true
    elseif type(v) == "number" then
      any_val = true
      if math.type(v) ~= "integer" then any_float = true end
    else all_num = false; any_val = true end
  end
  local out = {}
  if all_num and any_val then
    for k = 1, n do
      local v = vals[k]
      out[k] = (v == nil or v == NA or v == json.null) and NAN or v
    end
    if any_float or any_missing then
      for k = 1, n do out[k] = out[k] + 0.0 end
      return { dtype = "float64", vals = out }
    end
    return { dtype = "int64", vals = out }
  end
  for k = 1, n do local v = vals[k]; out[k] = (v == nil or v == json.null) and NA or v end
  return { dtype = "object", vals = out }
end
-- mini_racer gives Python ints for integral JS numbers
local function js_value(v) if type(v) == "number" and is_int(v) and math.abs(v) < 2 ^ 63 then return math.tointeger(v) end return v end

-- pd.to_numeric(errors="coerce")
local function to_numeric(col)
  local n = #col.vals
  if col.dtype == "int64" or col.dtype == "float64" then return col end
  local out, all_int = {}, true
  for k = 1, n do
    local v = col.vals[k]
    local x
    if v == NA or v == nil then x = NAN
    elseif type(v) == "number" then x = v
    elseif v == true then x = 1 elseif v == false then x = 0
    else
      local t = util.strip(v)
      if t:match("^[+-]?%d+$") then x = math.tointeger(tonumber(t)) or tonumber(t)
      else x = html.to_number(t) or NAN end
      if x ~= x then local lower = t:lower(); if lower == "nan" then x = NAN end end
    end
    out[k] = x
    if isnan(x) or math.type(x) ~= "integer" then all_int = false end
  end
  if all_int then return { dtype = "int64", vals = out } end
  for k = 1, n do out[k] = out[k] + 0.0 end
  return { dtype = "float64", vals = out }
end
-- pd.to_datetime(errors="coerce" [, format="%Y-%m-%d"]).dt.date -> "YYYY-MM-DD" or NA
local function to_date(col, strict)
  local out = {}
  for k = 1, #col.vals do
    local v = col.vals[k]
    local d
    if type(v) == "string" then
      if strict then
        local y, m, dd = v:match("^(%d%d%d%d)%-(%d%d?)%-(%d%d?)$")
        if y then d = html.parse_date(y .. "-" .. m .. "-" .. dd) end
      else
        d = html.parse_date(v)
        if not d then
          local y, m, dd = v:match("^(%d%d%d%d)[-/](%d%d?)[-/](%d%d?)[ T]")
          if y then d = html.parse_date(y .. "-" .. m .. "-" .. dd) end
        end
      end
    elseif type(v) == "number" and not isnan(v) and is_int(v) and v >= 19000101 and v <= 29991231 and not strict then
      d = nil -- integers are epoch nanoseconds for pandas, never dates here
    end
    out[k] = d and string.format("%04d-%02d-%02d", d // 10000, d // 100 % 100, d % 100) or NA
  end
  return { dtype = "date", vals = out }
end
local function cell_str(col, k)
  local v = col.vals[k]
  if v == NA or v == nil or isnan(v) then return "nan" end
  if col.dtype == "float64" then return fstr(v) end
  if col.dtype == "int64" then return istr(v) end
  if v == true then return "True" elseif v == false then return "False" end
  if type(v) == "number" then return math.type(v) == "integer" and istr(v) or fstr(v) end
  return v
end
local function frame(names, cols)
  local n = cols[1] and #cols[1].vals or 0
  local rows = {}
  for r = 1, n do
    local row = {}
    for c = 1, #names do row[c] = cell_str(cols[c], r) end
    rows[r] = row
  end
  return { columns = names, rows = rows }
end
M.empty = function() return { columns = {}, rows = {} } end

-- numpy.round(x, 2): rint(x * 100) / 100
local function rint(x)
  local f = math.floor(x)
  local dlt = x - f
  if dlt > 0.5 then return f + 1.0 elseif dlt < 0.5 then return f + 0.0 end
  return (f % 2 == 0) and f + 0.0 or f + 1.0
end
local function np_round2(x) if isnan(x) then return x end return rint(x * 100) / 100 end
M.np_round2 = np_round2

local function day_iso(z) return ok.time.days_to_date(z) end

-- --------------------------------------------------------------------------------- the API calls
local function between_eq(s) local a = s:find("=", 1, true); if not a then error("IndexError: list index out of range", 0) end
  local b = s:find("=", a + 1, true); return s:sub(a + 1, (b or #s + 1) - 1) end
local function js_payload(text)
  local p = between_eq(text)
  p = p:match("^([^;]*)") or p
  return (p:gsub('"', ""))
end

function M.stock_info_bj_name_code()
  local url = "https://www.bse.cn/nqxxController/nqxxCnzq.do"
  local hdr = { ["User-Agent"] = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/110.0.0.0 Safari/537.36" }
  local function page(p)
    local text = M.fetch("post", url, { form = { { "page", tostring(p) }, { "typejb", "T" }, { "xxfcbj[]", "2" }, { "xxzqdm", "" },
      { "sortfield", "xxzqdm" }, { "sorttype", "asc" } }, headers = hdr }).body
    local a = text:find("[", 1, true)
    return json.decode(text:sub(a, #text - 1))
  end
  local total = page(0)[1].totalPages
  local recs = {}
  for p = 0, total - 1 do
    for _, rec in ipairs(page(p)[1].content) do recs[#recs + 1] = rec end
  end
  local NAMES = { "上市日期", "-", "-", "-", "-", "-", "-", "-", "-", "-", "-", "流通股本", "-", "-", "-", "-", "-", "所属行业", "-", "-",
    "-", "-", "报告日期", "-", "-", "-", "-", "-", "-", "地区", "-", "-", "-", "-", "-", "券商", "总股本", "-", "证券代码", "-",
    "证券简称", "-", "-", "-", "-", "-", "-", "-" }
  local keys = recs[1] and json.keys(recs[1]) or {}
  local bypos = {}
  for i, nm in ipairs(NAMES) do if nm ~= "-" and keys[i] then bypos[nm] = keys[i] end end
  local function col(nm)
    local vals = {}
    for k, rec in ipairs(recs) do vals[k] = rec[bypos[nm]] end
    return obj_column(vals, #recs)
  end
  local names = { "证券代码", "证券简称", "总股本", "流通股本", "上市日期", "所属行业", "地区", "报告日期" }
  local cols = {}
  for i, nm in ipairs(names) do cols[i] = col(nm) end
  cols[8] = to_date(cols[8]); cols[5] = to_date(cols[5])
  return frame(names, cols)
end

local function md_float(v)
  if type(v) == "number" then return v + 0.0 end
  if v == nil or v == json.null then return NAN end
  local x = tonumber(util.strip(tostring(v)))
  return x and x + 0.0 or NAN
end

-- Sina unadjusted daily bars merged with share counts (stock_zh_a_daily, adjust="").
function M.stock_zh_a_daily(symbol, start_date, end_date)
  start_date, end_date = start_date or "19900101", end_date or "21000118"
  local text = get_text(("https://finance.sina.com.cn/realstock/company/%s/hisdata_klc2/klc_kl.js"):format(symbol))
  local recs = M.sina_decode(js_payload(text)) or error("TypeError: decoded data is undefined", 0)
  local data = {}
  for _, rec in ipairs(recs) do
    local f = rec.fields
    data[#data + 1] = { day = rec.date, src = 0, vals = { num(f.open), num(f.high), num(f.low), num(f.close), num(f.volume), num(f.amount), NAN } }
  end
  local atext = get_text(("https://stock.finance.sina.com.cn/stock/api/jsonp.php/var%%20KKE_ShareAmount_%s=/StockService.getAmountBySymbol?_=20&symbol=%s"):format(symbol, symbol), nil, "gbk")
  local a, b = atext:find("[", 1, true), nil
  for k = #atext, 1, -1 do if atext:sub(k, k) == "]" then b = k; break end end
  local amounts = json.decode(atext:sub(a, b))
  local shares = {}
  for _, it in ipairs(amounts) do
    local keys = json.keys(it)
    local dv, av = it[keys[1]], it[keys[2]]
    local d = html.parse_date(tostring(dv))
    shares[#shares + 1] = { day = d and ok.time.days_from_civil(d // 10000, d // 100 % 100, d % 100), amount = (type(av) == "number" and av or (md_float(av))) }
  end
  -- outer merge on the date index (sorted keys; equal keys -> left x right in order)
  local keyset, keys = {}, {}
  local L, R = {}, {}
  for _, row in ipairs(data) do
    L[row.day] = L[row.day] or {}; table.insert(L[row.day], row)
    if not keyset[row.day] then keyset[row.day] = true; keys[#keys + 1] = row.day end
  end
  for _, sh in ipairs(shares) do
    local k = sh.day
    if k ~= nil then
      R[k] = R[k] or {}; table.insert(R[k], sh)
      if not keyset[k] then keyset[k] = true; keys[#keys + 1] = k end
    end
  end
  table.sort(keys)
  local merged = {}
  for _, k in ipairs(keys) do
    local ls, rs = L[k] or { false }, R[k] or { false }
    for _, lrow in ipairs(ls) do
      for _, rrow in ipairs(rs) do
        local v = {}
        for c = 1, 6 do v[c] = lrow and lrow.vals[c] or NAN end
        v[7] = rrow and (rrow.amount + 0.0) or NAN
        merged[#merged + 1] = { day = k, v = v }
      end
    end
  end
  -- ffill, outstanding_share * 10000, turnover
  local last = {}
  for _, m in ipairs(merged) do
    for c = 1, 7 do
      if isnan(m.v[c]) then m.v[c] = last[c] or NAN else last[c] = m.v[c] end
    end
  end
  for _, m in ipairs(merged) do
    m.v[7] = m.v[7] * 10000
    m.v[8] = m.v[5] / m.v[7]
  end
  -- [start_date:end_date] on the (sorted) DatetimeIndex
  local function ymd_days(s) return ok.time.days_from_civil(tonumber(s:sub(1, 4)), tonumber(s:sub(5, 6)), tonumber(s:sub(7, 8))) end
  local lo, hi = ymd_days(start_date), ymd_days(end_date)
  local sliced = {}
  for _, m in ipairs(merged) do if m.day >= lo and m.day <= hi then sliced[#sliced + 1] = m end end
  local function key_of(v, n)
    local parts = {}
    for c = 1, n do parts[c] = isnan(v[c]) and "nan" or string.format("%.17g", v[c]) end
    return table.concat(parts, "|")
  end
  local seen, step = {}, {}
  for _, m in ipairs(sliced) do
    local k = key_of(m.v, 6)
    if not seen[k] then seen[k] = true; step[#step + 1] = m end
  end
  for _, m in ipairs(step) do for c = 1, 4 do m.v[c] = np_round2(m.v[c]) end end
  local nonna = {}
  for _, m in ipairs(step) do
    local any = false
    for c = 1, 8 do if isnan(m.v[c]) then any = true end end
    if not any then nonna[#nonna + 1] = m end
  end
  seen = {}
  local final = {}
  for _, m in ipairs(nonna) do
    local k = key_of(m.v, 8)
    if not seen[k] then seen[k] = true; final[#final + 1] = m end
  end
  local names = { "date", "open", "high", "low", "close", "volume", "amount", "outstanding_share", "turnover" }
  local rows = {}
  for i, m in ipairs(final) do
    local row = { day_iso(m.day) }
    for c = 1, 8 do row[c + 1] = fstr(m.v[c]) end
    rows[i] = row
  end
  return { columns = names, rows = rows }
end

function M.stock_zh_index_daily(symbol)
  local text = get_text(("https://finance.sina.com.cn/realstock/company/%s/hisdata/klc_kl.js"):format(symbol), { params = { { "d", "2020_2_4" } } })
  local recs = M.sina_decode(js_payload(text)) or error("TypeError: decoded data is undefined", 0)
  local names, seen = {}, {}
  for _, rec in ipairs(recs) do
    for _, k in ipairs(rec.keys) do if not seen[k] then seen[k] = true; names[#names + 1] = k end end
  end
  local cols = {}
  for c, nm in ipairs(names) do
    local vals = {}
    for k, rec in ipairs(recs) do
      if nm == "date" then vals[k] = day_iso(rec.date) else vals[k] = js_value(rec.fields[nm]) end
    end
    cols[c] = nm == "date" and { dtype = "date", vals = vals } or obj_column(vals, #recs)
    if nm == "open" or nm == "close" or nm == "high" or nm == "low" or nm == "volume" then cols[c] = to_numeric(cols[c]) end
  end
  return frame(names, cols)
end

function M.stock_zh_a_hist(symbol, start_date, end_date)
  start_date, end_date = start_date or "19700101", end_date or "20500101"
  local market_code = util.startswith(symbol, "6") and 1 or 0
  local r = M.fetch("get", "https://push2his.eastmoney.com/api/qt/stock/kline/get", { params = {
    { "fields1", "f1,f2,f3,f4,f5,f6" }, { "fields2", "f51,f52,f53,f54,f55,f56,f57,f58,f59,f60,f61,f116" },
    { "ut", "7eea3edcaed734bea9cbfc24409ed989" }, { "klt", "101" }, { "fqt", "0" }, { "secid", market_code .. "." .. symbol },
    { "beg", start_date }, { "end", end_date } } })
  local j = json.decode(r.body)
  local data = j.data
  if not util.truthy(data ~= json.null and data) or not util.truthy(data.klines ~= json.null and data.klines) then return M.empty() end
  local raw = {}
  for i, item in ipairs(data.klines) do raw[i] = util.split(item, ",") end
  local n = #raw
  local in_names = { "日期", "开盘", "收盘", "最高", "最低", "成交量", "成交额", "振幅", "涨跌幅", "涨跌额", "换手率" }
  local cols = {}
  for c, nm in ipairs(in_names) do
    local vals = {}
    for k = 1, n do vals[k] = raw[k][c] end
    local col = { dtype = "object", vals = vals }
    cols[nm] = c == 1 and to_date(col) or to_numeric(col)
  end
  local code = {}
  for k = 1, n do code[k] = symbol end
  cols["股票代码"] = { dtype = "object", vals = code }
  local names = { "日期", "股票代码", "开盘", "收盘", "最高", "最低", "成交量", "成交额", "振幅", "涨跌幅", "涨跌额", "换手率" }
  local list = {}
  for i, nm in ipairs(names) do list[i] = cols[nm] end
  return frame(names, list)
end

function M.stock_fhps_detail_em(symbol)
  local url = "https://datacenter-web.eastmoney.com/api/data/v1/get"
  local function params(p)
    return { { "sortColumns", "REPORT_DATE" }, { "sortTypes", "-1" }, { "pageSize", "500" }, { "pageNumber", tostring(p) },
      { "reportName", "RPT_SHAREBONUS_DET" }, { "columns", "ALL" }, { "quoteColumns", "" }, { "js", '{"data":(x),"pages":(tp)}' },
      { "source", "WEB" }, { "client", "WEB" }, { "filter", ('(SECURITY_CODE="%s")'):format(symbol) } }
  end
  local j = json.decode(M.fetch("get", url, { params = params(1) }).body)
  local total = math.tointeger(tonumber(tostring(j.result.pages)))
  local recs = {}
  for p = 1, total do
    local jj = json.decode(M.fetch("get", url, { params = params(p) }).body)
    for _, rec in ipairs(jj.result.data) do recs[#recs + 1] = rec end
  end
  local NAMES = { "_", "-", "_", "_", "-", "送转股份-送转总比例", "送转股份-送股比例", "送转股份-转股比例", "现金分红-现金分红比例", "业绩披露日期",
    "股权登记日", "除权除息日", "报告期", "方案进度", "现金分红-现金分红比例描述", "最新公告日期", "-", "-", "-", "每股收益", "每股净资产",
    "每股公积金", "每股未分配利润", "净利润同比增长", "总股本", "预案公告日", "现金分红-股息率", "-", "-", "-" }
  local keys, kseen = {}, {}
  for _, rec in ipairs(recs) do for _, k in ipairs(json.keys(rec)) do if not kseen[k] then kseen[k] = true; keys[#keys + 1] = k end end end
  if #keys ~= #NAMES then error(("ValueError: Length mismatch: Expected axis has %d elements, new values have %d elements"):format(#keys, #NAMES), 0) end
  local bypos = {}
  for i, nm in ipairs(NAMES) do if nm ~= "-" and nm ~= "_" then bypos[nm] = keys[i] end end
  local n = #recs
  local function col(nm) local vals = {}; for k, rec in ipairs(recs) do vals[k] = rec[bypos[nm]] end; return obj_column(vals, n) end
  local names = { "报告期", "业绩披露日期", "送转股份-送转总比例", "送转股份-送股比例", "送转股份-转股比例", "现金分红-现金分红比例",
    "现金分红-现金分红比例描述", "现金分红-股息率", "每股收益", "每股净资产", "每股公积金", "每股未分配利润", "净利润同比增长", "总股本",
    "预案公告日", "股权登记日", "除权除息日", "方案进度", "最新公告日期" }
  local DATES = util.set({ "报告期", "业绩披露日期", "预案公告日", "股权登记日", "除权除息日", "最新公告日期" })
  local NUMS = util.set({ "送转股份-送转总比例", "送转股份-送股比例", "送转股份-转股比例", "现金分红-现金分红比例", "现金分红-股息率", "每股收益",
    "每股净资产", "每股公积金", "每股未分配利润", "净利润同比增长", "总股本" })
  local cols = {}
  for i, nm in ipairs(names) do
    local c = col(nm)
    if DATES[nm] then c = to_date(c) elseif NUMS[nm] then c = to_numeric(c) end
    cols[i] = c
  end
  -- sort_values(["报告期"]) (NaT last), ties keep order
  local order = {}
  for k = 1, n do order[k] = k end
  local rp = cols[1].vals
  table.sort(order, function(x, y)
    local a, b = rp[x], rp[y]
    if a == NA and b == NA then return x < y end
    if a == NA then return false end
    if b == NA then return true end
    if a ~= b then return a < b end
    return x < y
  end)
  for i, c in ipairs(cols) do
    local v = {}
    for k, idx in ipairs(order) do v[k] = c.vals[idx] end
    cols[i] = { dtype = c.dtype, vals = v }
  end
  return frame(names, cols)
end

function M.stock_history_dividend_detail(symbol, indicator)
  local url = ("https://vip.stock.finance.sina.com.cn/corp/go.php/vISSUE_ShareBonus/stockid/%s.phtml"):format(symbol)
  local text = get_text(url, nil, "gbk")
  local tables = html.read_html(text)
  local fh = indicator == "分红"
  local t = tables[fh and 13 or 14]
  if not t then error("IndexError: list index out of range", 0) end
  local names = fh and { "公告日期", "送股", "转增", "派息", "进度", "除权除息日", "股权登记日", "红股上市日", "查看详细" }
    or { "公告日期", "配股方案", "配股价格", "基准股本", "除权日", "股权登记日", "缴款起始日", "缴款终止日", "配股上市日", "募集资金合计", "查看详细" }
  if t.width ~= #names then error(("ValueError: Length mismatch: Expected axis has %d elements, new values have %d elements"):format(t.width, #names), 0) end
  local cols = {}
  for c = 1, #names - 1 do
    local vals = {}
    for k, row in ipairs(t.rows) do vals[k] = html.is_na(row[c]) and NA or row[c] end
    local dt = t.dtypes[c]
    cols[c] = { dtype = dt == "bool" and "object" or dt, vals = vals }
  end
  names[#names] = nil
  if #t.rows > 0 and html.str(t.rows[1][1], t.dtypes[1]) == "暂时没有数据！" then return M.empty() end
  local function conv(nm, how)
    for c, x in ipairs(names) do
      if x == nm then
        if how == "num" then cols[c] = to_numeric(cols[c])
        elseif how == "date" then cols[c] = to_date(cols[c])
        else cols[c] = to_date(cols[c], true) end
      end
    end
  end
  conv("公告日期", "date")
  if fh then
    conv("送股", "num"); conv("转增", "num"); conv("派息", "num")
    conv("除权除息日", "fmt"); conv("股权登记日", "fmt"); conv("红股上市日", "fmt")
  else
    conv("配股方案", "num"); conv("配股价格", "num"); conv("基准股本", "num"); conv("募集资金合计", "num")
    conv("除权日", "fmt"); conv("股权登记日", "fmt"); conv("缴款起始日", "fmt"); conv("缴款终止日", "fmt"); conv("配股上市日", "fmt")
  end
  return frame(names, cols)
end

return M
