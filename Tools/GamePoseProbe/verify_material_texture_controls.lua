-- Verify installed MaterialAPI factory/sampler controls, excluding normal conversion.
--
-- Usage: lua verify_material_texture_controls.lua FIXTURE_DIR OUTPUT_JSON
local ok = require "ok"
local G = require "gpp"
local json, path, util = ok.json, ok.path, ok.util
local O, check = G.O, G.check
local IU = require "verify_item_uv_samples"
local B = require "verify_brow_uv_coverage"

local function copy_object(row)
  local o = json.object()
  for _, k in ipairs(json.keys(row)) do o[k] = row[k] end
  return o
end

local function main()
  if #arg ~= 2 then error("ValueError: not enough values to unpack (expected 2, got " .. #arg .. ")") end
  local folder, output = arg[1], arg[2]
  local results, ignored = json.array({}), json.array({})
  for _, p in ipairs(G.sort_paths(path.glob(folder, "Card*.json.renderer*.material-texture-control.json"))) do
    local row = json.decode(path.read_text(p))
    local declared = row.declared
    if declared == nil then declared = true end
    if not util.truthy(declared) then
      check(not util.truthy(row.accepted))
      local r = copy_object(row)
      r.fixture = G.winpath(path.abs(p))
      ignored[#ignored + 1] = r
    else
      local expected = math.max(0, row.previousWrap)
      local accepted = row.accepted
      if accepted == nil then accepted = true end
      check(util.truthy(accepted))
      check(row.textureType == "RenderTexture" and util.truthy(row.srgb) and row.mips == 2, G.winpath(p))
      check(row.filter == "Bilinear" and row.wrapU == row.wrapV and row.wrapV == expected, G.winpath(p))
      local base = p:sub(1, #p - #".material-texture-control.json")
      local native = G.read_f32(base .. ".uv-samples.rgba32f", 4)
      local uv = B.loadtxt_uv(base .. ".uv-samples.tsv")
      local finite, minv = true, nil
      for _, r in ipairs(native) do for c = 1, 3 do
        if not G.isfinite(r[c]) then finite = false end
        if minv == nil or r[c] < minv then minv = r[c] end
      end end
      check(finite and minv >= 0 and #native == #uv)
      if row.stressSamples ~= nil then
        check(row.stressSamples == 8 and #native == row.sourceSamples + row.stressSamples)
      end
      local png = row.inputPng
      local difference = IU.stats(IU.abs_rows(native, IU.sample(png, uv, true, true, expected, expected)))
      local negative = IU.stats(IU.abs_rows(native, IU.sample(png, uv, false, true, expected, expected)))
      check(difference.max <= .025 and difference.mean <= .002, { G.winpath(p), difference.mean, difference.max })
      check(negative.mean > .05, { "linear negative control must fail", G.winpath(p) })
      local r = copy_object(row)
      r.samples = #uv
      r.mean_rgb = difference.mean_axis0
      r.max_rgb = difference.max_axis0
      r.incorrect_linear_mae = negative.mean
      r.input_sha256 = G.digest(png)
      r.native_sha256 = G.digest(base .. ".uv-samples.rgba32f")
      r.fixture = G.winpath(path.abs(p))
      results[#results + 1] = r
    end
  end
  check(#results > 0, "No MaterialAPI control fixtures")
  -- Every material/property needs null, Repeat, Clamp and Mirror controls.
  local groups, order = {}, {}
  for _, row in ipairs(results) do
    local name = row.fixture:gsub("\\", "/"):match("([^/]*)$")
    local key = util.split(name, ".wrap")[1] .. "\0" .. tostring(row.property)
    if not groups[key] then groups[key] = {}; order[#order + 1] = key end
    table.insert(groups[key], row.previousWrap)
  end
  check(G.all(order, function(k) return G.set_eq(groups[k], { -1, 0, 1, 2 }) end))
  path.write_text(output, json.encode(O("result", "pass", "scope", "installed ME factory, mip0 RGB and source wrap; alpha/normal conversion/final pixels excluded",
    "max_threshold", .025, "mean_threshold", .002, "cases", results, "undeclared_properties", ignored), { indent = 2 }))
  print(("PASS %d native MaterialAPI controls, %d RGB samples, %d rejected properties"):format(#results, G.sum(results, function(r) return r.samples end), #ignored))
end

if ok.is_main() then ok.main(main) end
