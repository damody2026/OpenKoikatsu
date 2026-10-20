-- Validate both native upper-eyeline material slots and the lower line.
-- Uses the installed GPU mip0 coverage oracle, authored UV and current source
-- resolver PNGs. It does not assert final shader RGB or expression parity.
local ok = require "ok"
local G = require "gpp"
local json, path, util = ok.json, ok.path, ok.util
local O, check = G.O, G.check
local B = require "verify_brow_uv_coverage"

local DOC = [[Validate both native upper-eyeline material slots and the lower line.
Uses the installed GPU mip0 coverage oracle, authored UV and current source
resolver PNGs. It does not assert final shader RGB or expression parity.]]

local function as_list(v)
  -- Python iterates a dict's keys; every per-item access then fails with TypeError.
  if ok.json.is_object(v) then error("TypeError: string indices must be integers, not 'str'") end
  return v
end

local function main()
  local p = ok.args.parser(DOC)
  p:arg("inputs"); p:arg("output")
  local args = p:parse(arg)
  local sources = as_list(json.decode(path.read_text(path.join(args.inputs, "inputs.json"), { keep_bom = true })))
  local cases = json.array({})
  for _, source in ipairs(sources) do
    local prefix = source.native
    local renderers = json.decode(path.read_text(prefix .. ".renderers.json"))
    for _, part in ipairs(source.parts) do
      local mesh = util.endswith(part.name, "_kage") and "cf_O_eyeline" or part.name
      local indices = {}
      for i, r in ipairs(renderers) do if r.mesh == mesh then indices[#indices + 1] = i - 1 end end
      check(#indices == 1)
      local stem = prefix .. ".renderer" .. indices[1] .. ".mat" .. part.material_slot .. "._MainTex"
      local tsv = stem .. ".coverage.uv-samples.tsv"
      local meta = json.decode(path.read_text(path.with_suffix(tsv, ".json"), { keep_bom = true }))
      local res = B.tex_sample(B.pixels(stem .. ".png", true), { tsv = tsv }, { kernel = "accumulate", address = "clip", flip = true })
      local uv, native = res.uv, res.samples
      local observed = G.read_f32(path.with_suffix(tsv, ".rgba32f"), 4)
      if #observed ~= #uv then error("ValueError: operands could not be broadcast together") end
      local cov = {}
      for k = 1, #uv do for c = 1, 3 do cov[#cov + 1] = math.abs(native[k][c] * native[k][4] - observed[k][c]) end end
      local coverage = G.np_max(cov)
      local s = B.sample(B.pixels(part.png), uv, true)
      local al = {}
      for k = 1, #uv do al[k] = math.abs(s[k][4] - native[k][4]) end
      local alpha = G.np_max(al)
      if #part.uvs ~= math.min(meta.mesh_count, #uv) then error("ValueError: operands could not be broadcast together") end
      local diffs = {}
      for k = 1, #part.uvs do for c = 1, 2 do diffs[#diffs + 1] = math.abs(part.uvs[k][c] - uv[k][c]) end end
      local delta = G.np_max(diffs) + 0.0
      local passed = coverage <= .002 and alpha <= 2e-5 and delta <= 1e-6
      cases[#cases + 1] = O("card", source.card, "part", part.name, "material", part.material, "slot", part.material_slot,
        "samples", #uv, "native_gpu_coverage_max", coverage, "ue_alpha_max", alpha, "uv_max", delta, "passed", passed)
    end
  end
  local report = O("passed", G.all(cases, function(c) return c.passed end),
    "scope", "native mip0 coverage, independent material slots and authored UV transport; not RGB/expression parity", "cases", cases)
  path.write_text(args.output, json.encode(report, { ensure_ascii = false, indent = 2 }) .. "\n")
  print(json.encode(O("passed", report.passed, "cases", #cases, "alpha_max", G.A(G.map(cases, function(c) return c.ue_alpha_max end)))))
  if not report.passed then os.exit(1) end
end

if ok.is_main() then ok.main(main) end
