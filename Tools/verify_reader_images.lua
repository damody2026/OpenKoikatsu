-- Reject blank/black reader captures; this is not a visual fidelity oracle.
local ok = require "ok"
local json, P = ok.json, ok.path

-- PIL ImageStat over crop box (l, t, r, b) of an RGB image (pixels outside read as 0).
local function stat(img, l, t, r, b)
  local h = { {}, {}, {} }
  for c = 1, 3 do for j = 0, 255 do h[c][j] = 0 end end
  local w, data = img.width, img:bytes()
  for y = t, b - 1 do
    local inside_y = y >= 0 and y < img.height
    for x = l, r - 1 do
      if inside_y and x >= 0 and x < w then
        local i = (y * w + x) * 3
        local v1, v2, v3 = data:byte(i + 1, i + 3)
        h[1][v1] = h[1][v1] + 1; h[2][v2] = h[2][v2] + 1; h[3][v3] = h[3][v3] + 1
      else
        for c = 1, 3 do h[c][0] = h[c][0] + 1 end
      end
    end
  end
  local mean, stddev = json.array({}), json.array({})
  for c = 1, 3 do
    local count, sum, sum2 = 0, 0.0, 0.0
    for j = 0, 255 do count = count + h[c][j]; sum = sum + j * h[c][j]; sum2 = sum2 + (j * j) * (h[c][j] + 0.0) end
    mean[c] = count ~= 0 and sum / count or 0
    stddev[c] = count ~= 0 and math.sqrt((sum2 - (sum ^ 2.0) / count) / count) or 0
  end
  return mean, stddev
end

local function main()
  local p = ok.args.parser()
  p:arg("directory"); p:arg("width", { type = "int" }); p:arg("height", { type = "int" })
  local args = p:parse(arg)
  local rows = json.array({})
  local files = ok.util.sorted(P.glob(args.directory, "C*.png"), function(f) return f:lower() end)
  for _, path in ipairs(files) do
    local image = ok.image.load(path, "RGB")
    local width, height = image.width, image.height
    local corner = stat(image, 8, 8, 40, 40)
    -- The reader creates an unlit neutral gray backdrop. A black corner
    -- means its shader/view is not ready, regardless of geometry counts.
    local cmin, cmax = math.min(corner[1], corner[2], corner[3]), math.max(corner[1], corner[2], corner[3])
    local background_ready = cmin > 30 and cmax - cmin < 40
    local _, center = stat(image, width // 3, height // 10, width * 2 // 3, height * 9 // 10)
    local character_present = math.max(center[1], center[2], center[3]) > 12
    local valid = width == args.width and height == args.height and background_ready and character_present
    rows[#rows + 1] = json.object({ { "file", P.name(path) }, { "width", width }, { "height", height },
      { "background_rgb", corner }, { "character_present", character_present }, { "valid", valid } })
  end
  local all = #rows > 0
  for _, r in ipairs(rows) do all = all and r.valid end
  local result = json.object({ { "valid", all }, { "images", rows },
    { "scope", "resolution, neutral backdrop readiness and nonblank character region; manual visual review required" } })
  P.write_text(P.join(args.directory, "image_checks.json"), json.encode(result, { indent = 2 }))
  print(json.encode(result, { indent = 2 }))
  return result.valid and 0 or 1
end

ok.main(main)
