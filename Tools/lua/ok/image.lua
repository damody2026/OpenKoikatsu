-- Images as raw 8-bit pixel strings, decoded/encoded by ok-luakit (Rust).
--   local img = ok.image.load("a.png")            -- RGBA by default
--   local r, g, b, a = img:get(x, y)              -- 0-based like PIL
--   local gray = ok.image.load("a.png", "L")      -- modes: RGBA RGB LA L
--   local out = ok.image.new(w, h, "RGB", {0,0,0}); out:set(x, y, r, g, b); out:save("b.png")
--   ok.image.resize("in.png", "out.png", w, h, "lanczos3")   -- file to file
local path = require "ok.path"
local kit = require "ok.kit"
local M = {}

local CH = { RGBA = 4, RGB = 3, LA = 2, L = 1 }
M.channels = CH

local Image = {}
Image.__index = Image

local function wrap(w, h, mode, data)
  return setmetatable({ width = w, height = h, mode = mode, channels = CH[mode], data = data }, Image)
end

-- Load any format the image crate reads (png, jpg, webp, bmp, tga, tiff) as 8-bit.
function M.load(file, mode)
  mode = mode or "RGBA"
  assert(CH[mode], "unsupported mode " .. tostring(mode))
  local raw = path.temp(".raw")
  local info = kit.call({ "image-decode", file, raw, mode })
  local data = path.read_bytes(raw)
  path.remove(raw, true)
  local img = wrap(info.width, info.height, mode, data)
  img.source_mode = info.source_mode
  return img
end
M.open = M.load

-- Header-only size query: returns width, height, source color type.
function M.size(file)
  local info = kit.call({ "image-info", file })
  return info.width, info.height, info.color
end

-- New image; fill is a table of channel values (default zeros). Pixels are kept
-- in a mutable byte table until :commit()/:save().
function M.new(w, h, mode, fill)
  mode = mode or "RGBA"
  local c = CH[mode]
  local img = wrap(w, h, mode, nil)
  local buf = {}
  fill = fill or {}
  local n = w * h
  for i = 0, n - 1 do for k = 1, c do buf[i * c + k] = fill[k] or 0 end end
  img.buf = buf
  return img
end

-- Build from a raw byte string (as produced by load or string.pack).
function M.from_bytes(w, h, mode, data) return wrap(w, h, mode, data) end

function Image:index(x, y) return (y * self.width + x) * self.channels end

-- get(x, y) -> channel values (0-255). Out of range errors.
function Image:get(x, y)
  if x < 0 or y < 0 or x >= self.width or y >= self.height then error(("pixel %d,%d out of range"):format(x, y), 2) end
  local i, c = (y * self.width + x) * self.channels, self.channels
  if self.buf then return table.unpack(self.buf, i + 1, i + c) end
  return self.data:byte(i + 1, i + c)
end

function Image:set(x, y, ...)
  if not self.buf then self:to_buffer() end
  local i = (y * self.width + x) * self.channels
  local v = { ... }
  for k = 1, self.channels do self.buf[i + k] = v[k] or 0 end
end

-- Switch to a mutable byte table (slow for big images, fine for analysis outputs).
function Image:to_buffer()
  if self.buf then return self.buf end
  local buf, data, n = {}, self.data, #self.data
  local step = 4096
  for i = 1, n, step do
    local chunk = { data:byte(i, math.min(i + step - 1, n)) }
    for k = 1, #chunk do buf[i + k - 1] = chunk[k] end
  end
  self.buf, self.data = buf, nil
  return buf
end

-- Freeze the byte table back into the raw string form.
function Image:commit()
  if not self.buf then return self.data end
  local parts, buf, n, step = {}, self.buf, #self.buf, 4096
  for i = 1, n, step do
    local j = math.min(i + step - 1, n)
    for k = i, j do local v = buf[k]; if v < 0 then buf[k] = 0 elseif v > 255 then buf[k] = 255 elseif math.type(v) ~= "integer" then buf[k] = math.floor(v + 0.5) end end
    parts[#parts + 1] = string.char(table.unpack(buf, i, j))
  end
  self.data, self.buf = table.concat(parts), nil
  return self.data
end

function Image:bytes() return self:commit() end

-- Save as png (or jpg/webp/bmp by extension).
function Image:save(file)
  local raw = path.temp(".raw")
  path.write_bytes(raw, self:commit())
  local okv, err = pcall(kit.call, { "image-encode", raw, self.width, self.height, self.mode, file })
  path.remove(raw, true)
  if not okv then error(err, 2) end
  return file
end

-- Crop box (left, top, right, bottom) exclusive like PIL.
function Image:crop(l, t, r, b)
  self:commit()
  local c, rows = self.channels, {}
  for y = t, b - 1 do
    local s = (y * self.width + l) * c
    rows[#rows + 1] = self.data:sub(s + 1, s + (r - l) * c)
  end
  return wrap(r - l, b - t, self.mode, table.concat(rows))
end

-- Convert between modes (RGBA/RGB/LA/L) with PIL's luma weights for L.
function Image:convert(mode)
  if mode == self.mode then return self end
  self:commit()
  local raw, out = path.temp(".raw"), path.temp(".raw")
  path.write_bytes(raw, self.data)
  local okv, err = pcall(kit.call, { "image-convert", raw, self.width, self.height, self.mode, mode, out })
  path.remove(raw, true)
  if not okv then error(err, 2) end
  local data = path.read_bytes(out); path.remove(out, true)
  return wrap(self.width, self.height, mode, data)
end

-- Iterate pixels: for x, y, r, g, b, a in img:pixels() do ... end
function Image:pixels()
  self:commit()
  local w, h, c, data = self.width, self.height, self.channels, self.data
  local i = -1
  return function()
    i = i + 1
    if i >= w * h then return nil end
    return i % w, i // w, data:byte(i * c + 1, i * c + c)
  end
end

-- Per-channel statistics computed in Rust: {mean={...}, min={...}, max={...}, count}
-- over an optional box {l,t,r,b} and optional alpha threshold (pixels with a<=t skipped).
function M.stats(file, opts)
  opts = opts or {}
  local args = { "image-stats", file }
  if opts.box then args[#args + 1] = "--box"; args[#args + 1] = table.concat(opts.box, ",") end
  if opts.alpha_min then args[#args + 1] = "--alpha-min"; args[#args + 1] = opts.alpha_min end
  return kit.call(args)
end

-- Pixel difference between two images of equal size, computed in Rust:
-- {max, mean, rms, differing (count > threshold), total, width, height}
function M.diff(a, b, opts)
  opts = opts or {}
  local args = { "image-diff", a, b }
  if opts.threshold then args[#args + 1] = "--threshold"; args[#args + 1] = opts.threshold end
  if opts.output then args[#args + 1] = "--output"; args[#args + 1] = opts.output end
  if opts.box then args[#args + 1] = "--box"; args[#args + 1] = table.concat(opts.box, ",") end
  return kit.call(args)
end

-- File-to-file resize: filter nearest|triangle|catmullrom|gaussian|lanczos3 (default lanczos3).
function M.resize(src, dst, w, h, filter)
  return kit.call({ "image-resize", src, dst, w, h, filter or "lanczos3" })
end

-- Paste/compose several images onto a canvas (contact sheets):
-- compose(dst, width, height, {{file=..., x=, y=, w=, h=}...}, {background={r,g,b,a}})
function M.compose(dst, w, h, items, opts)
  local json = require "ok.json"
  local spec = { width = w, height = h, items = items, background = (opts and opts.background) or { 0, 0, 0, 255 } }
  return kit.call({ "image-compose", "-", dst }, { input = json.encode(spec, { ensure_ascii = false }) })
end

-- Render text into a PNG with a TrueType font (see ok-luakit text-render).
function M.render_text(spec)
  local json = require "ok.json"
  return kit.call({ "text-render", "-" }, { input = json.encode(spec, { ensure_ascii = false }) })
end

return M
