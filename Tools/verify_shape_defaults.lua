-- Verify editor reset values against the game's unmodified maker presets.
--   lua.exe Tools/verify_shape_defaults.lua <game> [--generate]
local ok = require "ok"
local path = ok.path

-- m_Script bytes of the first TextAsset in a bundle (UnityPy asset.m_Script, surrogateescape-encoded).
local function text_asset_script(bundle)
  local objs = ok.kit.unity_objects(bundle, { type = "TextAsset", tree = false })
  assert(objs[1], "StopIteration")
  local tmp = path.temp(".bin")
  ok.kit.call({ "unity-raw", bundle, "--path-id", objs[1].path_id, tmp })
  local raw = path.read_bytes(tmp)
  path.remove(tmp, true)
  local pos = 1
  local n = string.unpack("<i4", raw, pos); pos = pos + 4 + n
  pos = (pos - 1 + 3) // 4 * 4 + 1 -- m_Name is an aligned string
  n = string.unpack("<i4", raw, pos); pos = pos + 4
  return raw:sub(pos, pos + n - 1)
end

local function shapes(game, sex)
  local data = text_asset_script(path.join(game, "abdata/custom/presets_" .. sex .. "_00.unity3d"))
  local offset = 8 -- 0-based positions as in the Python original
  while true do
    local size = string.unpack(">I4", data, offset + 1)
    local kind = data:sub(offset + 5, offset + 8)
    offset = offset + 12 + size
    if kind == "IEND" then break end
  end
  offset = offset + 4
  for _ = 1, 2 do
    local size, shift = 0, 0
    while true do
      local byte = data:byte(offset + 1)
      offset = offset + 1
      size = size | ((byte & 127) << shift)
      if byte & 128 == 0 then break end
      shift = shift + 7
    end
    offset = offset + size
  end
  local size = string.unpack("<i4", data, offset + 1)
  offset = offset + 4 + size
  size = string.unpack("<i4", data, offset + 1)
  offset = offset + 4
  local header = ok.kit.msgpack_decode(data:sub(offset + 1, offset + size))
  local base = offset + size + 8
  local block
  for _, b in ipairs(header.lstInfo) do if b.name == "Custom" then block = b break end end
  assert(block, "StopIteration")
  local raw = data:sub(base + block.pos + 1, base + block.pos + block.size)
  local parts, pos = {}, 0
  for _ = 1, 2 do
    local n = string.unpack("<i4", raw, pos + 1)
    pos = pos + 4
    parts[#parts + 1] = ok.kit.msgpack_decode(raw:sub(pos + 1, pos + n))
    pos = pos + n
  end
  return parts[2].shapeValueBody, parts[1].shapeValueFace
end

local function main()
  local parser = ok.args.parser()
  parser:arg("game")
  parser:flag("--generate")
  local args = parser:parse(arg)
  local target = ok.root .. "/Source/OpenKoikatsu/OKShapeDefaults.h"
  local arrays, order = {}, {}
  for _, spec in ipairs({ { "f", "Female" }, { "m", "Male" } }) do
    local body, face = shapes(args.game, spec[1])
    assert(#body == 44 and #face == 52)
    arrays[spec[2] .. "Body"] = body; order[#order + 1] = spec[2] .. "Body"
    arrays[spec[2] .. "Face"] = face; order[#order + 1] = spec[2] .. "Face"
  end
  if args.generate then
    local source = { "#pragma once\n\n#include \"CoreMinimal.h\"\n\n// Original maker reset targets: CustomControl.Entry loads ill_Default_Female/Male\n// from abdata/custom/presets_f_00.unity3d and presets_m_00.unity3d.\n// Verify with Tools/verify_shape_defaults.lua against the user-owned game.\nnamespace OKShapeDefaults\n{\n" }
    for _, name in ipairs(order) do
      local values = arrays[name]
      source[#source + 1] = "\tinline constexpr float " .. name .. "[] = {\n"
      for start = 1, #values, 8 do
        local cells = {}
        for i = start, math.min(start + 7, #values) do cells[#cells + 1] = string.format("%.9ff", values[i]) end
        source[#source + 1] = "\t\t" .. table.concat(cells, ", ") .. ",\n"
      end
      source[#source + 1] = "\t};\n"
    end
    source[#source + 1] = "\tinline float Value(int32 Sex, bool bBody, int32 Index)\n\t{\n\t\tconst float* Values = bBody ? (Sex == 0 ? MaleBody : FemaleBody) : (Sex == 0 ? MaleFace : FemaleFace);\n\t\treturn Index >= 0 && Index < (bBody ? 44 : 52) ? Values[Index] : 0.5f;\n\t}\n}\n"
    path.write_text(target, table.concat(source))
  end
  local source = path.read_text(target)
  for _, name in ipairs(order) do
    local expected = arrays[name]
    local body = source:match(name .. "%[%] = {(.-)};")
    assert(body, "TypeError: 'NoneType' object is not subscriptable")
    local actual = {}
    for v in body:gmatch("([%d.-]+)f") do
      local x = tonumber(v)
      if x == nil then error("ValueError: could not convert string to float: '" .. v .. "'", 0) end
      actual[#actual + 1] = x + 0.0
    end
    assert(#actual == #expected)
    for i = 1, #actual do assert(math.abs(actual[i] - expected[i]) < 1e-8, name) end
    print(("%s: %d values match original maker preset"):format(name, #actual))
  end
end

ok.main(main)
