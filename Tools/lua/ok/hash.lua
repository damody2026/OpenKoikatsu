-- Hashes. Strings hash in pure Lua; files larger than 1 MiB go through
-- ok-luakit (Rust) for speed. All digests are lowercase hex.
local M = {}

local function u32(x) return x & 0xffffffff end
local function rrot(x, n) return u32((x >> n) | (x << (32 - n))) end

local K = {
  0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
  0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
  0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
  0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
  0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
  0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
  0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
  0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
}

function M.sha256(msg)
  local len = #msg
  msg = msg .. "\128" .. string.rep("\0", (55 - len) % 64) .. string.pack(">I8", len * 8)
  local h0, h1, h2, h3, h4, h5, h6, h7 = 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
  local w = {}
  for chunk = 1, #msg, 64 do
    for i = 0, 15 do w[i] = string.unpack(">I4", msg, chunk + i * 4) end
    for i = 16, 63 do
      local a, b = w[i - 15], w[i - 2]
      local s0 = rrot(a, 7) ~ rrot(a, 18) ~ (a >> 3)
      local s1 = rrot(b, 17) ~ rrot(b, 19) ~ (b >> 10)
      w[i] = u32(w[i - 16] + s0 + w[i - 7] + s1)
    end
    local a, b, c, d, e, f, g, h = h0, h1, h2, h3, h4, h5, h6, h7
    for i = 0, 63 do
      local S1 = rrot(e, 6) ~ rrot(e, 11) ~ rrot(e, 25)
      local ch = (e & f) ~ (~e & g)
      local t1 = u32(h + S1 + ch + K[i + 1] + w[i])
      local S0 = rrot(a, 2) ~ rrot(a, 13) ~ rrot(a, 22)
      local maj = (a & b) ~ (a & c) ~ (b & c)
      local t2 = u32(S0 + maj)
      h, g, f, e, d, c, b, a = g, f, e, u32(d + t1), c, b, a, u32(t1 + t2)
    end
    h0, h1, h2, h3 = u32(h0 + a), u32(h1 + b), u32(h2 + c), u32(h3 + d)
    h4, h5, h6, h7 = u32(h4 + e), u32(h5 + f), u32(h6 + g), u32(h7 + h)
  end
  return string.format("%08x%08x%08x%08x%08x%08x%08x%08x", h0, h1, h2, h3, h4, h5, h6, h7)
end

-- hashlib.sha1(msg).hexdigest()
function M.sha1(msg)
  local len = #msg
  msg = msg .. "\128" .. string.rep("\0", (55 - len) % 64) .. string.pack(">I8", len * 8)
  local h0, h1, h2, h3, h4 = 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0
  local w = {}
  local function lrot1(x, n) return u32((x << n) | (x >> (32 - n))) end
  for chunk = 1, #msg, 64 do
    for i = 0, 15 do w[i] = string.unpack(">I4", msg, chunk + i * 4) end
    for i = 16, 79 do w[i] = lrot1(w[i - 3] ~ w[i - 8] ~ w[i - 14] ~ w[i - 16], 1) end
    local a, b, c, d, e = h0, h1, h2, h3, h4
    for i = 0, 79 do
      local f, k
      if i < 20 then f = (b & c) | (u32(~b) & d); k = 0x5A827999
      elseif i < 40 then f = b ~ c ~ d; k = 0x6ED9EBA1
      elseif i < 60 then f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC
      else f = b ~ c ~ d; k = 0xCA62C1D6 end
      local t = u32(lrot1(a, 5) + f + e + k + w[i])
      e, d, c, b, a = d, c, lrot1(b, 30), a, t
    end
    h0, h1, h2, h3, h4 = u32(h0 + a), u32(h1 + b), u32(h2 + c), u32(h3 + d), u32(h4 + e)
  end
  return string.format("%08x%08x%08x%08x%08x", h0, h1, h2, h3, h4)
end

local S = { 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
  4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21 }
local T = {}
for i = 1, 64 do T[i] = u32(math.floor(math.abs(math.sin(i)) * 2 ^ 32)) end
local function lrot(x, n) return u32((x << n) | (x >> (32 - n))) end

function M.md5(msg)
  local len = #msg
  msg = msg .. "\128" .. string.rep("\0", (55 - len) % 64) .. string.pack("<I8", len * 8)
  local a0, b0, c0, d0 = 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476
  for chunk = 1, #msg, 64 do
    local m = {}
    for i = 0, 15 do m[i] = string.unpack("<I4", msg, chunk + i * 4) end
    local a, b, c, d = a0, b0, c0, d0
    for i = 0, 63 do
      local f, g
      if i < 16 then f = (b & c) | (~b & d); g = i
      elseif i < 32 then f = (d & b) | (~d & c); g = (5 * i + 1) % 16
      elseif i < 48 then f = b ~ c ~ d; g = (3 * i + 5) % 16
      else f = c ~ (b | u32(~d)); g = (7 * i) % 16 end
      f = u32(f + a + T[i + 1] + m[g])
      a, d, c = d, c, b
      b = u32(b + lrot(f, S[i + 1]))
    end
    a0, b0, c0, d0 = u32(a0 + a), u32(b0 + b), u32(c0 + c), u32(d0 + d)
  end
  local out = string.pack("<I4I4I4I4", a0, b0, c0, d0)
  return (out:gsub(".", function(ch) return string.format("%02x", ch:byte()) end))
end

local crc_table
function M.crc32(s, crc)
  if not crc_table then
    crc_table = {}
    for i = 0, 255 do
      local c = i
      for _ = 1, 8 do c = (c & 1 == 1) and (0xEDB88320 ~ (c >> 1)) or (c >> 1) end
      crc_table[i] = c
    end
  end
  crc = u32(~(crc or 0))
  for i = 1, #s do crc = crc_table[(crc ~ s:byte(i)) & 0xff] ~ (crc >> 8) end
  return u32(~crc)
end

-- File digests: algo "sha256" | "md5".
function M.file(path, algo)
  algo = algo or "sha256"
  local p = require "ok.path"
  local size = p.size(path)
  if size == nil then error("hash: no such file " .. tostring(path), 2) end
  if size > (1 << 20) then
    return require("ok.kit").call({ "hash", algo, path }).digest
  end
  return M[algo](p.read_bytes(path))
end
function M.sha256_file(path) return M.file(path, "sha256") end

-- Digests of many files in one ok-luakit call (hash-many):
-- files({p1, p2, ...}, "sha256") -> {d1, d2, ...}; an unreadable file gives false.
function M.files(paths, algo)
  if #paths == 0 then return {} end
  local p = require "ok.path"
  local list = {}
  for i, f in ipairs(paths) do list[i] = p.norm(f) end
  local res = require("ok.kit").call({ "hash-many", algo or "sha256", "-" }, { input = table.concat(list, "\n") .. "\n" })
  local null = require("ok.json").null
  local out = {}
  for i = 1, #paths do
    local d = res[i]
    out[i] = (d ~= nil and d ~= null) and d or false
  end
  return out
end
function M.md5_file(path) return M.file(path, "md5") end

return M
