-- Verify a saved legacy FaceOver against the installed KSOX runtime layer.
-- Opaque overlay pixels isolate this missing-layer defect from existing face-create
-- color/paint differences. This is not a full face shader or facial-pose parity gate.
local ok = require "ok"
local G = require "gpp"
local json, path = ok.json, ok.path
local O, check = G.O, G.check

local DOC = [[Verify a saved legacy FaceOver against the installed KSOX runtime layer.
Opaque overlay pixels isolate this missing-layer defect from existing face-create
color/paint differences. This is not a full face shader or facial-pose parity gate.]]

local function rgba_diff(a, b, mask)
  local args = { "probe", "rgba-max-diff", a, b }
  if mask then args[#args + 1] = "--mask"; args[#args + 1] = mask end
  return ok.kit.call(args)
end

local function main()
  local p = ok.args.parser(DOC)
  p:arg("native"); p:arg("inputs"); p:arg("output")
  p:opt("--cached-name", { default = "final_cached_face.png" })
  local args = p:parse(arg)
  local native_overlay = args.native .. ".face-overlay2.0.png"
  local native_face = args.native .. ".face-before-probe.png"
  local source = path.join(args.inputs, "ksox_0_2.png")
  local t = rgba_diff(source, native_overlay)
  check(not t.shape_mismatch)
  local transport = t.max
  local opaque = rgba_diff(source, source, source).count
  check(opaque > 100, "fixture must contain opaque eye-rim coverage")
  local rows = json.array({})
  for _, pair in ipairs({ { "cf_O_face.png", false }, { "resolved_face.png", true }, { args.cached_name, true } }) do
    local name, expected = pair[1], pair[2]
    local image = path.join(args.inputs, name)
    local d = rgba_diff(image, native_face, source)
    if d.shape_mismatch then error("IndexError: boolean index did not match indexed array") end
    rows[#rows + 1] = O("path", G.winpath(path.abs(image)), "sha256", G.digest(image), "rgba_max_255", d.max,
      "passed", d.max == 0, "expected_pass", expected)
  end
  local version = G.read(args.native .. ".face-overlays.tsv")
  check(G.has(version, "version\t1"), "installed source must confirm legacy version1")
  local passed = transport == 0 and G.all(rows, function(r) return r.passed == r.expected_pass end)
  local report = O("passed", passed, "scope", "saved legacy source/runtime RGBA transport and opaque composite coverage; baseline must fail; not whole native face/pixel/pose parity",
    "native", G.winpath(path.abs(args.native)), "source_png_sha256", G.digest(source), "source_runtime_max_255", transport,
    "opaque_pixels", opaque, "cases", rows)
  path.write_text(args.output, json.encode(report, { indent = 2 }) .. "\n")
  print(json.encode(O("passed", passed, "opaque_pixels", opaque, "rgba_max_255", G.A(G.map(rows, function(r) return r.rgba_max_255 end)))))
  if not passed then os.exit(1) end
end

if ok.is_main() then ok.main(main) end
