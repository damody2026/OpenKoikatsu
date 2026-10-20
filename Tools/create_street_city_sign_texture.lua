-- Deterministic 2K typography; no image service or model inference required.
local ok = require "ok"
local P = ok.path

local root = P.parent(ok.script_dir)
local out = P.join(root, "output/street-social-city-production/S01-modular-components-v1/assembly-building/furniture")
P.mkdir(out)
local font, size, text = "C:/Windows/Fonts/msjh.ttc", 210, "咖啡書屋  S01"
-- PIL sizes fonts in pixels per em; ok-luakit text-render scales by font height.
local m = ok.kit.call({ "font-metrics", font, 0 })
local px = size * m.height / m.units_per_em
local function render(output, x, y, width, height)
  return ok.image.render_text({ width = width, height = height, background = { 23, 35, 30, 255 }, output = output,
    items = { { text = text, font = font, size = px, x = x, y = y, color = { 232, 219, 184, 255 }, anchor = "la" } } })
end
-- PIL textbbox((0, 0)): horizontal extent is the pen advance (or ink, if wider),
-- vertical extent is the ink; centre it horizontally with the ink top at y=65.
local probe = P.temp(".png")
local item = render(probe, 0, 0, 2048, 416).items[1]
P.remove(probe, true)
local bb = { math.min(0, item.bbox[1]), item.bbox[2], math.max(item.advance, item.bbox[3]), item.bbox[4] }
local target = P.join(out, "sign-2k.png")
local tmp = P.temp(".png")
render(tmp, (2048 - (bb[3] - bb[1])) / 2, 65 - bb[2], 2048, 416)
ok.image.load(tmp, "RGB"):save(target)
P.remove(tmp, true)
