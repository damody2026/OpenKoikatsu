-- Measure fixed-pose whole-character CPU/GPU capture differences.
local ok = require "ok"
local json, P, U = ok.json, ok.path, ok.util
local O = json.object

local function exit_msg(msg) io.stdout:flush(); io.stderr:write(msg, "\n"); os.exit(1) end
local function win(p) return (P.norm(p):gsub("/", "\\")) end
local function get(t, k, d) local v = t[k]; if v == nil then return d end; return v end
local function need(t, k) local v = t[k]; if v == nil then error("KeyError: '" .. k .. "'", 0) end; return v end

local p = ok.args.parser()
p:arg("prefix")
p:opt("--reference", { choices = { "cpu", "original" }, default = "cpu" })
p:opt("--target", { choices = { "cpu", "gpu" }, default = "gpu" })
p:opt("--reference-prefix")
local args = p:parse(arg)
local prefix = win(P.abs(args.prefix))
local receipt = json.decode(P.read_text(prefix .. ".capture.json"))
if not (U.truthy(need(receipt, "captured")) and U.truthy(need(receipt, "full_gpu_character"))
    and U.truthy(need(receipt, "identical_pose_and_camera"))
    and get(receipt, "minimum_executed_gpu_parts", 0) > 0
    and get(receipt, "executed_gpu_passes", 0) > 0) then
  exit_msg("Capture did not prove native GPU execution")
end
local reference_prefix = args.reference_prefix and win(P.abs(args.reference_prefix)) or prefix
if args.reference_prefix then
  local other = json.decode(P.read_text(reference_prefix .. ".capture.json"))
  local differ = false
  for _, k in ipairs({ "character", "clip", "sample_times" }) do
    if not U.equal(need(other, k), need(receipt, k)) then differ = true; break end
  end
  if not U.truthy(other.captured) or differ then exit_msg("Reference character, clip or sampled times differ") end
end
local rows = json.array({})
for phase = 0, 6 do
  for _, view in ipairs({ "front", "side", "back" }) do
    local stem = ("%s.p%d.%s"):format(prefix, phase, view)
    local cpu = ok.image.load(("%s.p%d.%s.%s.png"):format(reference_prefix, phase, view, args.reference), "RGB")
    local gpu = ok.image.load(stem .. "." .. args.target .. ".png", "RGB")
    if cpu.width ~= gpu.width or cpu.height ~= gpu.height then exit_msg("Capture dimensions differ") end
    local w, h = cpu.width, cpu.height
    local a, b = cpu:bytes(), gpu:bytes()
    local total, maxv, over8 = 0, 0, 0
    local abs, byte = math.abs, string.byte
    for y = 0, h - 1 do
      local s = y * w * 3
      local ra = { byte(a, s + 1, s + w * 3) }
      local rb = { byte(b, s + 1, s + w * 3) }
      for i = 1, w * 3, 3 do
        local e1, e2, e3 = abs(ra[i] - rb[i]), abs(ra[i + 1] - rb[i + 1]), abs(ra[i + 2] - rb[i + 2])
        total = total + e1 + e2 + e3
        local peak = e1
        if e2 > peak then peak = e2 end
        if e3 > peak then peak = e3 end
        if peak > maxv then maxv = peak end
        if peak > 8 then over8 = over8 + 1 end
      end
    end
    rows[#rows + 1] = O({ { "phase", phase }, { "view", view },
      { "mean_byte_error", total / (w * h * 3) },
      { "max_byte_error", maxv },
      { "pixels_over_8_percent", over8 / (w * h) * 100 } })
  end
end
local gate = true
for _, row in ipairs(rows) do gate = gate and row.mean_byte_error < .1 and row.pixels_over_8_percent < .1 end
local result = O({ { "reference", args.reference }, { "reference_prefix", reference_prefix }, { "target", args.target }, { "pairs", rows },
  { "pixel_gate_passed", gate }, { "manual_visual_acceptance_required", true },
  { "note", "Non-temporal fixed-pose diagnostic; does not validate viewport TAA motion history." } })
local suffix = (args.reference == "cpu" and args.target == "gpu" and not args.reference_prefix) and ".diff.json" or ("." .. args.reference .. "-" .. args.target .. ".diff.json")
P.write_text(prefix .. suffix, json.encode(result, { indent = 2 }))
local worst_mean, worst_over
for _, r in ipairs(rows) do
  if worst_mean == nil or r.mean_byte_error > worst_mean then worst_mean = r.mean_byte_error end
  if worst_over == nil or r.pixels_over_8_percent > worst_over then worst_over = r.pixels_over_8_percent end
end
print(json.encode(O({ { "pairs", #rows }, { "pixel_gate_passed", gate }, { "worst_mean_byte_error", worst_mean },
  { "worst_pixels_over_8_percent", worst_over } }), { indent = 2 }))
if not gate then os.exit(1) end
