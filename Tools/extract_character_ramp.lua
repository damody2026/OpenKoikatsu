-- Extract a named shared ramp without baking character-specific resources.
local ok = require "ok"
local json, path = ok.json, ok.path

local DESCRIPTION = "Extract a named shared ramp without baking character-specific resources."

-- 5/6-bit to 8-bit expansion as PIL's BCn decoder does.
local function rgb565(c)
  local r, g, b = (c >> 11) & 31, (c >> 5) & 63, c & 31
  return (r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)
end

-- PIL bcn decoder (BC1 with 1-bit alpha, BC3) into an RGBA byte table (top row first).
local function decode_bcn(data, w, h, kind)
  local px = {}
  local bw, bh = (w + 3) // 4, (h + 3) // 4
  local pos = 1
  for by = 0, bh - 1 do
    for bx = 0, bw - 1 do
      local alpha
      if kind == 3 then
        local a0, a1 = data:byte(pos, pos + 1)
        local bits = 0
        for i = 7, 2, -1 do bits = (bits << 8) | data:byte(pos + i) end
        local a = { a0, a1 }
        if a0 > a1 then
          for i = 1, 6 do a[i + 2] = ((7 - i) * a0 + i * a1) // 7 end
        else
          for i = 1, 4 do a[i + 2] = ((5 - i) * a0 + i * a1) // 5 end
          a[7], a[8] = 0, 255
        end
        alpha = {}
        for i = 0, 15 do alpha[i] = a[((bits >> (3 * i)) & 7) + 1] end
        pos = pos + 8
      end
      local c0, c1, idx = string.unpack("<I2I2I4", data, pos)
      pos = pos + 8
      local r0, g0, b0 = rgb565(c0)
      local r1, g1, b1 = rgb565(c1)
      local col = { { r0, g0, b0, 255 }, { r1, g1, b1, 255 } }
      if c0 > c1 or kind == 3 then
        col[3] = { (2 * r0 + r1) // 3, (2 * g0 + g1) // 3, (2 * b0 + b1) // 3, 255 }
        col[4] = { (r0 + 2 * r1) // 3, (g0 + 2 * g1) // 3, (b0 + 2 * b1) // 3, 255 }
      else
        col[3] = { (r0 + r1) // 2, (g0 + g1) // 2, (b0 + b1) // 2, 255 }
        col[4] = { 0, 0, 0, 0 }
      end
      for i = 0, 15 do
        local x, y = bx * 4 + i % 4, by * 4 + i // 4
        if x < w and y < h then
          local c = col[((idx >> (2 * i)) & 3) + 1]
          local o = (y * w + x) * 4
          px[o + 1], px[o + 2], px[o + 3], px[o + 4] = c[1], c[2], c[3], alpha and alpha[i] or c[4]
        end
      end
    end
  end
  local out = {}
  for i = 1, #px, 4096 do out[#out + 1] = string.char(table.unpack(px, i, math.min(i + 4095, #px))) end
  return table.concat(out)
end

local FORMAT_NAMES = { [1] = "Alpha8", [2] = "ARGB4444", [3] = "RGB24", [4] = "RGBA32", [5] = "ARGB32", [7] = "RGB565", [10] = "DXT1", [12] = "DXT5", [13] = "RGBA4444", [14] = "BGRA32", [63] = "R8" }

-- UnityPy Texture2D.image: decode to (mode, raw bytes) and flip top/bottom.
local function texture_image(tree, bundle)
  local w, h, fmt = tree.m_Width, tree.m_Height, tree.m_TextureFormat
  local data = ok.kit.bytes(tree["image data"])
  local stream = tree.m_StreamData
  if (data == nil or data == "") and stream and stream.path ~= "" then
    local tmp = path.temp(".res")
    ok.kit.call({ "unity-stream", bundle, stream.path, stream.offset, stream.size, tmp })
    data = path.read_bytes(tmp)
    path.remove(tmp, true)
  end
  if data == nil or data == "" then error("Texture2D has no image data", 0) end
  local n = w * h
  local mode, raw
  local function map(stride, f)
    local t = {}
    for i = 0, n - 1 do t[#t + 1] = f(data, i * stride + 1) end
    return table.concat(t)
  end
  if fmt == 3 then mode, raw = "RGB", data:sub(1, n * 3)
  elseif fmt == 4 then mode, raw = "RGBA", data:sub(1, n * 4)
  elseif fmt == 5 then mode, raw = "RGBA", map(4, function(d, i) local a, r, g, b = d:byte(i, i + 3); return string.char(r, g, b, a) end)
  elseif fmt == 14 then mode, raw = "RGBA", map(4, function(d, i) local b, g, r, a = d:byte(i, i + 3); return string.char(r, g, b, a) end)
  elseif fmt == 1 then mode, raw = "RGBA", map(1, function(d, i) return string.char(0, 0, 0, d:byte(i)) end)
  elseif fmt == 63 then mode, raw = "RGB", map(1, function(d, i) return string.char(d:byte(i), 0, 0) end)
  elseif fmt == 10 then mode, raw = "RGBA", decode_bcn(data, w, h, 1)
  elseif fmt == 12 then mode, raw = "RGBA", decode_bcn(data, w, h, 3)
  else
    error("Not implemented texture format: " .. (FORMAT_NAMES[fmt] or tostring(fmt)), 0)
  end
  local c = mode == "RGB" and 3 or 4
  local rows = {}
  for y = h - 1, 0, -1 do rows[#rows + 1] = raw:sub(y * w * c + 1, (y + 1) * w * c) end
  return mode, table.concat(rows)
end

local function main()
  local parser = ok.args.parser(DESCRIPTION)
  parser:opt("--zipmod", { required = true })
  parser:opt("--bundle", { required = true })
  parser:opt("--texture", { required = true })
  parser:opt("--output", { required = true })
  local args = parser:parse(arg)
  local tmp = path.temp(".unity3d")
  ok.kit.call({ "zip-member", args.zipmod, args.bundle, tmp })
  local source_sha256 = ok.hash.sha256_file(tmp)
  local matches = {}
  for _, obj in ipairs(ok.kit.unity_objects(tmp, { type = "Texture2D" })) do
    if obj.tree.m_Name == args.texture then matches[#matches + 1] = obj.tree end
  end
  if #matches ~= 1 then path.remove(tmp, true); error(("Expected one texture, found %d"):format(#matches), 0) end
  local data = matches[1]
  if data.m_ColorSpace ~= 0 then path.remove(tmp, true); error("Expected a linear ramp", 0) end
  path.mkdir(path.parent(args.output))
  local okv, mode, raw = pcall(texture_image, data, tmp)
  path.remove(tmp, true)
  if not okv then error(mode, 0) end
  ok.image.from_bytes(data.m_Width, data.m_Height, mode, raw):save(args.output)
  local receipt = json.object({ { "source_zip", (path.abs(args.zipmod):gsub("/", "\\")) }, { "bundle", args.bundle },
    { "texture", args.texture }, { "color_space", "Linear" }, { "width", data.m_Width }, { "height", data.m_Height },
    { "source_sha256", source_sha256 }, { "png_sha256", ok.hash.sha256_file(args.output) } })
  path.write_text(path.with_suffix(args.output, ".json"), json.encode(receipt, { indent = 2 }))
  print(json.encode(receipt))
end

ok.main(main)
