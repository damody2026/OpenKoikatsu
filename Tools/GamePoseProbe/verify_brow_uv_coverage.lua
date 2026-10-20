-- Verify brow row transport against native GPU coverage, including ME controls.
--
-- PNG readback direction is first checked using native Unlit/Transparent coverage
-- at authored mesh UVs and a dense 64x64 UV grid. Exported alpha is then sampled at
-- the actual UE coordinate (1-v), independent of the baked brow tint.
local ok = require "ok"
local G = require "gpp"
local json, path = ok.json, ok.path
local O = G.O

local M = {}
local DOC = [[Verify brow row transport against native GPU coverage, including ME controls.

PNG readback direction is first checked using native Unlit/Transparent coverage
at authored mesh UVs and a dense 64x64 UV grid. Exported alpha is then sampled at
the actual UE coordinate (1-v), independent of the baked brow tint.]]

-- pixels(path, linear): float64 RGBA / 255 (sRGB-decoded RGB when linear). The
-- image stays in ok-luakit; this returns a descriptor that sample() and
-- with_ops() understand. Use M.values(desc) for the actual array.
function M.pixels(p, linear)
  return { png = path.norm(p), ops = linear and { { srgb = true } } or {} }
end
-- Copy of a descriptor with an extra per-channel op: {add=d, channels="a"|"rgb"|"rgba"} or {srgb=true}.
function M.with_ops(desc, ...)
  local ops = {}
  for _, op in ipairs(desc.ops) do ops[#ops + 1] = op end
  for _, op in ipairs({ ... }) do ops[#ops + 1] = op end
  return { png = desc.png, ops = ops }
end
-- Run ok-luakit tex-sample. uv is {{u,v},...} or {tsv=path}; extra spec fields in opts.
function M.tex_sample(desc, uv, opts)
  local spec = { png = desc.png, ops = json.array(desc.ops) }
  if uv.tsv then spec.uv_tsv = path.norm(uv.tsv) else spec.uv = uv end
  for k, v in pairs(opts or {}) do spec[k] = v end
  return ok.kit.call({ "probe", "tex-sample", "-" }, { input = json.encode(spec) })
end
-- sample(a, uv, flip) -> rows {r, g, b, a} (clip addressing, accumulated bilinear)
function M.sample(desc, uv, flip)
  return M.tex_sample(desc, uv, { kernel = "accumulate", address = "clip", flip = flip and true or false }).samples
end
-- np.loadtxt(tsv, skiprows=1)[:, 1:3] (parsed in Rust, correctly rounded)
function M.loadtxt_uv(tsv)
  local res = ok.kit.call({ "probe", "tex-sample", "-" }, { input = json.encode({ png = nil, uv_tsv = path.norm(tsv) }) })
  return res.uv
end

local function pystr(p) return require("compare").pystr(p) end

local function as_list(v)
  -- Python iterates a dict's keys; every per-item access then fails with TypeError.
  if ok.json.is_object(v) then error("TypeError: string indices must be integers, not 'str'") end
  return v
end

local function main()
  local p = ok.args.parser(DOC)
  p:arg("native"); p:arg("inputs"); p:arg("output")
  local args = p:parse(arg)
  local sources = as_list(json.decode(ok.path.read_text(path.join(args.inputs, "inputs.json"), { keep_bom = true })))
  local rows = json.array({})
  for idx, source in ipairs(sources) do
    local i = idx - 1
    local prefix = source.native ~= nil and source.native or path.join(args.native, "Card" .. i)
    local files = path.glob(path.parent(prefix), path.name(prefix) .. ".*._MainTex.coverage.uv-samples.tsv")
    if #files ~= 1 then error("ValueError: Ambiguous or absent native brow coverage: card " .. i) end
    local tsv = files[1]
    local meta = json.decode(ok.path.read_text(path.with_suffix(tsv, ".json"), { keep_bom = true }))
    local native_png = (tsv:gsub("%.coverage%.uv%-samples%.tsv", ".png"))
    local exported = path.join(args.inputs, "Card" .. i .. ".png")
    local native_res = M.tex_sample(M.pixels(native_png, true), { tsv = tsv }, { kernel = "accumulate", address = "clip", flip = true })
    local uv, native = native_res.uv, native_res.samples
    local observed = G.read_f32(path.with_suffix(tsv, ".rgba32f"), 4)
    if #observed ~= #uv or not G.all(observed, function(r) return G.isfinite(r[1]) and G.isfinite(r[2]) and G.isfinite(r[3]) end) then
      error("ValueError: Invalid native GPU coverage")
    end
    local coverage = {}
    for k = 1, #uv do
      local n, o = native[k], observed[k]
      for c = 1, 3 do coverage[#coverage + 1] = math.abs(n[c] * n[4] - o[c]) end
    end
    local direct = M.sample(M.pixels(exported), uv, true)
    local opposite = M.sample(M.pixels(exported), uv, false)
    local alpha_error, alternate_error = {}, {}
    for k = 1, #uv do
      alpha_error[k] = math.abs(direct[k][4] - native[k][4])
      alternate_error[k] = math.abs(opposite[k][4] - native[k][4])
    end
    local mesh_count = meta.mesh_count
    local suv = source.uvs
    if #suv ~= math.min(mesh_count, #uv) then error("ValueError: operands could not be broadcast together") end
    local uv_diffs = {}
    for k = 1, #suv do for c = 1, 2 do uv_diffs[#uv_diffs + 1] = math.abs(suv[k][c] - uv[k][c]) end end
    local uv_error = G.np_max(uv_diffs)
    local cov_max, alpha_max = G.np_max(coverage), G.np_max(alpha_error)
    local passed = cov_max <= .002 and alpha_max <= 2e-5 and uv_error <= 1e-6
    rows[#rows + 1] = O("card", source.card, "key", source.key, "samples", #uv,
      "mesh_samples", mesh_count, "native_png", pystr(native_png),
      "input_png", pystr(exported), "input_sha256", G.digest(exported),
      "native_gpu_coverage_max", cov_max,
      "ue_alpha_max", alpha_max, "ue_alpha_mean", G.np_mean(alpha_error),
      "opposite_row_alpha_mean", G.np_mean(alternate_error), "uv_max", uv_error + 0.0,
      "passed", passed)
  end
  local report = O("passed", G.all(rows, function(r) return r.passed end),
    "scope", "mip0 native coverage validates PNG orientation; UE 1-v alpha transport and authored UV; not final shader RGB or facial pose parity",
    "gates", O("native_gpu_coverage_max", .002, "ue_alpha_max", 2e-5, "uv_max", 1e-6), "cases", rows)
  ok.path.write_text(args.output, json.encode(report, { ensure_ascii = false, indent = 2 }) .. "\n")
  print(json.encode(O("passed", report.passed, "cases", #rows, "alpha_max", G.A(G.map(rows, function(r) return r.ue_alpha_max end)))))
  if not report.passed then os.exit(1) end
end

if ok.is_main() then ok.main(main) end
return M
