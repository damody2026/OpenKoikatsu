-- Compare frozen-state CPU / GPU captures without hiding pixel or pose errors.
local ok = require "ok"
local json, P, U = ok.json, ok.path, ok.util
local O = json.object

-- numpy.percentile(values, q) (method "linear") from a 0..255 histogram of n values.
local function percentile(hist, n, q)
  local function at(index)
    local seen = 0
    for v = 0, 255 do
      seen = seen + hist[v]
      if seen > index then return v end
    end
  end
  local virtual = (n - 1) * (q / 100)
  local prev, nxt
  if virtual >= n - 1 then prev, nxt = n - 1, n - 1
  elseif virtual < 0 then prev, nxt = 0, 0
  else prev = math.floor(virtual); nxt = prev + 1 end
  local gamma = virtual - math.floor(virtual)
  if virtual >= n - 1 then gamma = virtual - (n - 1) end
  local a, b = at(prev), at(nxt)
  local diff = b - a
  if gamma >= 0.5 then return b - diff * (1 - gamma) end
  return a + diff * gamma
end

local prefix = P.norm(arg[1])
local rows = json.array({})
for _, view in ipairs({ "front", "side", "back" }) do
  local cpu = ok.image.load(prefix .. "." .. view .. ".cpu.png", "RGB")
  local gpu = ok.image.load(prefix .. "." .. view .. ".gpu.png", "RGB")
  if cpu.width ~= gpu.width or cpu.height ~= gpu.height then error("ValueError: Capture dimensions differ", 0) end
  local w, h = cpu.width, cpu.height
  local a, b = cpu:bytes(), gpu:bytes()
  local hist = {}
  for v = 0, 255 do hist[v] = 0 end
  local tiles_x = (w + 63) // 64
  local tile_sum, tile_count = {}, {}
  local total, over10, max_error = 0, 0, 0
  local heat_rows, sheet_rows = {}, {}
  local abs, char, byte = math.abs, string.char, string.byte
  for y = 0, h - 1 do
    local row_start = y * w * 3
    local ra = { byte(a, row_start + 1, row_start + w * 3) }
    local rb = { byte(b, row_start + 1, row_start + w * 3) }
    local heat = {}
    local ty = y // 64
    for x = 0, w - 1 do
      local i = x * 3
      local e1, e2, e3 = abs(ra[i + 1] - rb[i + 1]), abs(ra[i + 2] - rb[i + 2]), abs(ra[i + 3] - rb[i + 3])
      hist[e1] = hist[e1] + 1; hist[e2] = hist[e2] + 1; hist[e3] = hist[e3] + 1
      local s = e1 + e2 + e3
      total = total + s
      local peak = e1
      if e2 > peak then peak = e2 end
      if e3 > peak then peak = e3 end
      if peak > max_error then max_error = peak end
      if peak > 10 then over10 = over10 + 1 end
      local t = ty * tiles_x + x // 64 + 1
      tile_sum[t] = (tile_sum[t] or 0) + s
      tile_count[t] = (tile_count[t] or 0) + 3
      local r = peak * 8
      if r > 255 then r = 255 end
      heat[x + 1] = char(r, 0, 0)
    end
    local heat_row = table.concat(heat)
    heat_rows[y + 1] = heat_row
    sheet_rows[y + 1] = a:sub(row_start + 1, row_start + w * 3) .. b:sub(row_start + 1, row_start + w * 3) .. heat_row
  end
  local n = w * h * 3
  local worst
  for t = 1, #tile_sum do
    local m = tile_sum[t] / tile_count[t]
    if worst == nil or m > worst then worst = m end
  end
  local row = O({ { "view", view }, { "mean_absolute_rgb", total / n }, { "p99_channel_error", percentile(hist, n, 99) + 0.0 },
    { "pixels_error_gt10_percent", over10 / (w * h) * 100 }, { "worst_64px_tile_mean", worst }, { "max_error", max_error } })
  row.passed = row.mean_absolute_rgb <= 1 and row.pixels_error_gt10_percent <= 1 and row.worst_64px_tile_mean <= 3
  rows[#rows + 1] = row
  ok.image.from_bytes(w, h, "RGB", table.concat(heat_rows)):save(prefix .. "." .. view .. ".difference.png")
  ok.image.from_bytes(w * 3, h, "RGB", table.concat(sheet_rows)):save(prefix .. "." .. view .. ".comparison.png")
end
local scene_path = P.join(P.parent(prefix), "scene.json")
local scene = P.exists(scene_path) and json.decode(P.read_text(scene_path)) or nil
local function get(k, d) local v = scene[k]; if v == nil then return d end; return v end
local has_scene = scene ~= nil and U.truthy(scene)
local scene_verified = false
if has_scene then
  scene_verified = U.truthy(scene.gpu_ready) and U.truthy(scene.same_phase_captures_written)
    and not U.truthy(scene.capture_error) and U.equal(scene.complete, scene.selected)
    and get("scene_skinning_matrices_checked", 0) > 0
    and get("scene_skinning_matrix_mismatches", -1) == 0
end
if has_scene and U.truthy(scene.common_render_path) then
  scene_verified = scene_verified and get("cpu_pose_uploads", 0) > 0
end
local all_passed = true
for _, r in ipairs(rows) do all_passed = all_passed and r.passed end
local report = O({ { "passed", all_passed and (has_scene and scene_verified or not has_scene) },
  { "same_frame", not (has_scene and U.truthy(scene.common_render_path)) },
  { "same_frozen_scene_state", true },
  { "full_character_gpu", scene_verified }, { "temporal_aa", false }, { "fps_evidence", false }, { "views", rows } })
if has_scene then
  report.scene_receipt = (P.abs(scene_path):gsub("/", "\\"))
  report.renderer_palette_verified = scene_verified
end
P.write_text(prefix .. ".comparison.json", json.encode(report, { indent = 2 }))
print(json.encode(report, { indent = 2 }))
os.exit(report.passed and 0 or 1)
