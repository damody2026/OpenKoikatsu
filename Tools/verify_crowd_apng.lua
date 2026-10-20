-- Audit the completed 100-card APNG delivery and its independent-load receipts.
-- Module use: local v = require "verify_crowd_apng"
--   local product, strings, face, header, payload = v.metadata_values(v.card_metadata(path))
local ok = require "ok"
local json, P, U = ok.json, ok.path, ok.util
local M = {}

local function read_json(path) return json.decode(P.read_text(path)) end
local function sha256(path) return ok.hash.file(path, "sha256") end
local function fail(msg) error("ValueError: " .. msg, 0) end
local function win(p) return (P.norm(p):gsub("/", "\\")) end
local function bytes_repr(s) return "b'" .. s:gsub("[^\32-\126]", function(c) return ("\\x%02x"):format(c:byte()) end) .. "'" end
-- pathlib equality on Windows: separators and case do not matter.
local function path_key(p) return (P.norm(p):gsub("/+", "/"):gsub("/$", ""):gsub("^%./", ""):lower()) end

local CRC = {}
for i = 0, 255 do
  local c = i
  for _ = 1, 8 do c = (c & 1 == 1) and (0xEDB88320 ~ (c >> 1)) or (c >> 1) end
  CRC[i] = c
end
local function crc32(s, i, j)
  local crc, byte = 0xffffffff, string.byte
  local k = i
  while k <= j do
    local e = math.min(k + 4095, j)
    local t = { byte(s, k, e) }
    for n = 1, #t do crc = CRC[(crc ~ t[n]) & 0xff] ~ (crc >> 8) end
    k = e + 1
  end
  return crc ~ 0xffffffff
end

local function animation(path)
  local data = P.read_bytes(path)
  local frames, expected_sequence, counts = {}, 0, {}
  if data:sub(1, 8) ~= "\x89PNG\r\n\x1a\n" then fail("Invalid PNG signature: " .. win(path)) end
  local pos = 9
  while true do
    if pos + 7 > #data then fail("Truncated PNG: " .. win(path)) end
    local size, kind = string.unpack(">I4c4", data, pos)
    local body_start = pos + 8
    local body = data:sub(body_start, body_start + size - 1)
    local crc = data:sub(body_start + size, body_start + size + 3)
    if #body ~= size or #crc ~= 4 or crc32(data, pos + 4, body_start + size - 1) ~= string.unpack(">I4", crc) then
      fail(("Invalid chunk checksum: %s %s"):format(win(path), bytes_repr(kind)))
    end
    pos = body_start + size + 4
    counts[kind] = (counts[kind] or 0) + 1
    if kind == "acTL" then
      local a, b = string.unpack(">I4I4", body)
      if a ~= 4 or b ~= 0 then fail("Expected four frames with infinite loops: " .. win(path)) end
    end
    if kind == "fcTL" or kind == "fdAT" then
      if string.unpack(">I4", body) ~= expected_sequence then fail("Invalid APNG frame sequence: " .. win(path)) end
      expected_sequence = expected_sequence + 1
    end
    if kind == "fcTL" then
      local _, width, height, x, y, numerator, denominator = string.unpack(">I4I4I4I4I4I2I2BB", body)
      if width ~= 768 or height ~= 1024 or x ~= 0 or y ~= 0 then fail("Unexpected frame dimensions: " .. win(path)) end
      frames[#frames + 1] = numerator / (denominator ~= 0 and denominator or 100)
    end
    if kind == "IEND" then
      -- KKS-compatible card metadata deliberately follows IEND.
      local metadata = data:sub(pos)
      if not (#frames == 4 and frames[1] == 2 and frames[2] == 1 and frames[3] == 1 and frames[4] == 1)
          or counts.acTL ~= 1 or counts.okPv ~= 1 then
        fail("Missing animation or portable payload: " .. win(path))
      end
      return metadata
    end
  end
end

function M.card_metadata(path)
  local data = P.read_bytes(path)
  local pos = 9
  while true do
    if pos + 7 > #data then fail("Missing card metadata: " .. win(path)) end
    local size, kind = string.unpack(">I4c4", data, pos)
    pos = pos + 8 + size + 4
    if kind == "IEND" then return data:sub(pos) end
  end
end

function M.metadata_values(data)
  local pos = 1
  local function read(n)
    local s = data:sub(pos, pos + n - 1); pos = pos + #s
    return s
  end
  local product = read(4)
  local strings = {}
  for _ = 1, 2 do
    local length, shift = 0, 0
    while true do
      local b = read(1):byte()
      if not b then error("IndexError: index out of range", 0) end
      length = length | ((b & 127) << shift)
      if b & 128 == 0 then break end
      shift = shift + 7
    end
    strings[#strings + 1] = read(length)
  end
  local face = read((string.unpack("<i4", read(4))))
  local header = ok.kit.msgpack_decode(read((string.unpack("<i4", read(4)))))
  -- Unreal and Rust choose different signed encodings for positive header
  -- offsets; compare their values and preserve the actual block bytes exactly.
  return product, strings, face, header, data:sub(pos)
end

function M.verify(root)
  local output = P.join(root, "output/CrowdAPNG")
  local manifest = read_json(P.join(output, "manifest.json"))
  local selection = U.splitlines(P.read_text(P.join(root, "Saved/Crowd/SelectedCards.txt")))
  if not U.truthy(manifest.complete) or manifest.selected ~= 100 or #selection ~= 100 then fail("Expected a completed 100-card delivery") end
  local good = #manifest.cards == 100
  for i, r in ipairs(manifest.cards) do good = good and r.index == i - 1 end
  if not good then fail("Manifest must preserve the exact selected order") end
  local outfits, total_bytes = 0, 0
  for i, record in ipairs(manifest.cards) do
    local index = i - 1
    local path = P.join(output, ("C%03d.png"):format(index))
    if not U.equal(record, read_json(P.join(output, ("C%03d.verified.json"):format(index)))) then fail("Manifest and card receipt differ: " .. index) end
    if path_key(record.source) ~= path_key(selection[i]) or sha256(selection[i]) ~= record.source_sha256 then fail("Source changed: " .. index) end
    if path_key(record.apng) ~= path_key(path) or sha256(path) ~= record.apng_sha256 or not U.truthy(record.normalized_parameters) then
      fail("Delivery changed or is not normalized: " .. index)
    end
    local preparation = read_json(record.preparation)
    local check = read_json(record.verification)
    local normal = read_json(record.normalization)
    local count = record.outfits
    if count ~= #preparation.outfits or count ~= check.outfit_count or count ~= normal.outfit_count then fail("Outfit count changed: " .. index) end
    if #normal.geometry_sha256_by_outfit ~= count or #check.outfits ~= count then fail("Missing per-outfit verification: " .. index) end
    for o, result in ipairs(check.outfits) do
      if result.outfit ~= o - 1 or U.truthy(result.game_root) or result.parts <= 0 or #result.shape_checks ~= 3 then
        fail(("Incomplete independent editable outfit: %d/%d"):format(index, o - 1))
      end
    end
    local a = table.pack(M.metadata_values(animation(path)))
    local b = table.pack(M.metadata_values(M.card_metadata(P.join(root, "Saved/Crowd/Portable", ("C%03d.png"):format(index)))))
    if not U.equal(a, b) then fail("Prepared card metadata changed: " .. index) end
    outfits = outfits + count
    total_bytes = total_bytes + P.size(path)
  end
  return json.object({ { "complete", true }, { "cards", 100 }, { "outfits", outfits }, { "apng_bytes", total_bytes },
    { "source_and_apng_sha256", "passed" }, { "png_chunk_crc_and_animation", "passed" },
    { "all_outfits_independent_load_and_editable_shapes", "passed" },
    { "all_outfits_normalization_geometry_checks", "passed" } })
end
M.animation = animation

if ok.is_main() then
  local p = ok.args.parser()
  p:arg("root")
  local args = p:parse(arg)
  print(json.encode(M.verify(P.abs(args.root)), { indent = 2 }))
end

return M
