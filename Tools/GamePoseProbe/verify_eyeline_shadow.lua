-- Validate the independent native slot1 coverage and cached shadow alpha.
--
-- Unlit/Transparent over clear writes source-alpha squared to the float RT.
-- RGBA8 ReadPixels quantizes the original source before PNG encoding. Bound
-- that conversion by one UNORM8 step at each texel, before bilinear sampling;
-- do not infer row order from lossy sRGB RGB coverage. The earlier full-eyeline
-- strict audit stays intact and remains a separate result.
local ok = require "ok"
local G = require "gpp"
local json, path = ok.json, ok.path
local O = G.O
local B = require "verify_brow_uv_coverage"

local DOC = [[Validate the independent native slot1 coverage and cached shadow alpha.

Unlit/Transparent over clear writes source-alpha squared to the float RT.
RGBA8 ReadPixels quantizes the original source before PNG encoding. Bound
that conversion by one UNORM8 step at each texel, before bilinear sampling;
do not infer row order from lossy sRGB RGB coverage. The earlier full-eyeline
strict audit stays intact and remains a separate result.]]

local function f32(x) return (string.unpack("<f", string.pack("<f", x))) end

local function as_list(v)
  -- Python iterates a dict's keys; every per-item access then fails with TypeError.
  if ok.json.is_object(v) then error("TypeError: string indices must be integers, not 'str'") end
  return v
end

local function main()
  local p = ok.args.parser(DOC)
  p:arg("inputs"); p:arg("output")
  local args = p:parse(arg)
  local rows = json.array({})
  for _, source in ipairs(as_list(json.decode(path.read_text(path.join(args.inputs, "inputs.json"), { keep_bom = true })))) do
    local part
    for _, pt in ipairs(source.parts) do if pt.material_slot == 1 then part = pt; break end end
    if part == nil then error("StopIteration") end
    local renderers = json.decode(path.read_text(source.native .. ".renderers.json"))
    local index
    for i, r in ipairs(renderers) do if r.mesh == "cf_O_eyeline" then index = i - 1; break end end
    if index == nil then error("StopIteration") end
    local stem = source.native .. ".renderer" .. index .. ".mat1._MainTex"
    local uv = B.loadtxt_uv(stem .. ".coverage.uv-samples.tsv")
    local gpu = G.read_f32(stem .. ".coverage.uv-samples.rgba32f", 4)
    local image = B.pixels(stem .. ".png")
    local lower = B.with_ops(image, { add = -(1 / 255), channels = "a" })
    local upper = B.with_ops(image, { add = 1 / 255, channels = "a" })
    local lo, hi = B.sample(lower, uv, true), B.sample(upper, uv, true)
    if #gpu ~= #uv then error("ValueError: operands could not be broadcast together") end
    local violations = {}
    for k = 1, #uv do
      local g = gpu[k][4]
      local alpha = f32(math.sqrt(g > 0 and g or 0.0))
      local v = math.max(lo[k][4] - alpha, alpha - hi[k][4])
      violations[k] = v > 0 and v or 0.0
    end
    local violation = G.np_max(violations)
    local a, b = B.sample(B.pixels(part.png), uv, true), B.sample(image, uv, true)
    local tr = {}
    for k = 1, #uv do tr[k] = math.abs(a[k][4] - b[k][4]) end
    local transport = G.np_max(tr)
    local diffs = {}
    for k = 1, #part.uvs do for c = 1, 2 do diffs[#diffs + 1] = math.abs(part.uvs[k][c] - uv[k][c]) end end
    local uv_error = G.np_max(diffs) + 0.0
    local passed = violation <= 2e-5 and transport <= 2e-5 and uv_error <= 1e-6
    rows[#rows + 1] = O("card", source.card, "slot", 1, "samples", #uv, "png", part.png,
      "native_gpu_quantization_bound_violation", violation, "ue_alpha_max", transport, "uv_max", uv_error, "passed", passed)
  end
  local result = O("passed", G.all(rows, function(r) return r.passed end),
    "scope", "independent slot1 identity and alpha at authored/dense UV; GPU float alpha-squared within per-texel RGBA8 conversion interval; not all-eyeline RGB/compression parity",
    "cases", rows)
  path.write_text(args.output, json.encode(result, { ensure_ascii = false, indent = 2 }) .. "\n")
  print(json.encode(O("passed", result.passed, "cases", #rows)))
  if not result.passed then os.exit(1) end
end

if ok.is_main() then ok.main(main) end
