-- Check a decoded brow texture against the game's runtime _MainTex capture.
--
-- The capture can change RGB through the game's readback path and the exporter
-- can tint the image. Alpha carries the actual brush shape and is compared in
-- both row orientations. This legacy PNG-only check does not establish the UV
-- sampling direction or distinguish bundle, Sideloader PNG and MaterialEditor
-- sources. Use verify_brow_uv_coverage.lua for a production row-transport gate.
local ok = require "ok"
local G = require "gpp"
local json, path = ok.json, ok.path
local O = G.O

local DOC = [[Check a decoded brow texture against the game's runtime _MainTex capture.

The capture can change RGB through the game's readback path and the exporter
can tint the image. Alpha carries the actual brush shape and is compared in
both row orientations. This legacy PNG-only check does not establish the UV
sampling direction or distinguish bundle, Sideloader PNG and MaterialEditor
sources. Use verify_brow_uv_coverage.lua for a production row-transport gate.
]]

local function pystr(p) return require("compare").pystr(p) end

local function main()
  local p = ok.args.parser(DOC)
  p:arg("native"); p:arg("exported")
  p:opt("--output")
  p:flag("--require-direct")
  local args = p:parse(arg)
  local r = ok.kit.call({ "probe", "alpha-flip", args.native, args.exported })
  if r.shape_mismatch then
    error(("ValueError: Shape mismatch: (%d, %d) vs (%d, %d)"):format(r.target[1], r.target[2], r.source[1], r.source[2]))
  end
  local comparisons = json.array({})
  for _, c in ipairs(r.comparisons) do
    comparisons[#comparisons + 1] = O("flip_y", c.flip_y, "mean_abs_alpha", c.mean_abs_alpha + 0.0,
      "max_abs_alpha", c.max_abs_alpha, "pixels_over_16", c.pixels_over_16)
  end
  local report = O("native", pystr(args.native), "exported", pystr(args.exported), "shape", r.shape,
    "comparisons", comparisons,
    "direct_rows_match", comparisons[1].mean_abs_alpha < comparisons[2].mean_abs_alpha)
  local result = json.encode(report, { ensure_ascii = false, indent = 2 }) .. "\n"
  if args.output then
    path.mkdir(path.parent(args.output))
    path.write_text(args.output, result)
  end
  print(result)
  if args.require_direct and not report.direct_rows_match then
    io.stdout:flush()
    io.stderr:write("Direct runtime brow orientation did not match\n")
    os.exit(1)
  end
end

if ok.is_main() then ok.main(main) end
