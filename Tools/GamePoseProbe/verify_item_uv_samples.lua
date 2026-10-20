-- Compare native mask RGB GPU samples with exact session PNG inputs.
--
-- Usage: lua verify_item_uv_samples.lua NATIVE_CARD_JSON INPUT_DIR OUTPUT_JSON
-- The Unlit/Texture probe forces alpha one. This tests mip0 RGB/UV transport,
-- not packed normal alpha, compressed mip chains, or final shader colour parity.
local ok = require "ok"
local G = require "gpp"
local json, path, util = ok.json, ok.path, ok.util
local O = G.O
local B = require "verify_brow_uv_coverage"

local M = {}
local DOC = [[Compare native mask RGB GPU samples with exact session PNG inputs.

Usage: Tools\lua\lua.exe verify_item_uv_samples.lua NATIVE_CARD_JSON INPUT_DIR OUTPUT_JSON
The Unlit/Texture probe forces alpha one. This tests mip0 RGB/UV transport,
not packed normal alpha, compressed mip chains, or final shader colour parity.
]]

function M.material_key(name)
  return util.strip((name:gsub("%(Instance%)", ""):gsub(" Instance", ""))):lower()
end
local material_key = M.material_key

-- sample(png, uv, srgb, flip, wrap_u=0, wrap_v=0) -> rows {r, g, b} (alpha also present as [4])
function M.sample(png, uv, srgb, flip, wrap_u, wrap_v)
  local desc = B.pixels(png, srgb)
  return B.tex_sample(desc, uv, { kernel = "lerp", flip = flip and true or false, wrap_u = wrap_u or 0, wrap_v = wrap_v or 0 }).samples
end

-- |native - sample| as rows of 3, plus numpy reductions.
function M.abs_rows(native, sampled)
  local out = {}
  for k = 1, #native do out[k] = { math.abs(native[k][1] - sampled[k][1]), math.abs(native[k][2] - sampled[k][2]), math.abs(native[k][3] - sampled[k][3]) } end
  return out
end
function M.stats(rows)
  local flat = G.flatten(rows, 3)
  local colmax = {}
  for c = 1, 3 do local m; for _, r in ipairs(rows) do if m == nil or r[c] > m then m = r[c] end end; colmax[c] = m end
  return { max = G.np_max(flat), mean = G.np_mean(flat), mean_axis0 = G.np_mean_axis0(rows, 3), max_axis0 = colmax }
end

local function pystr(p) return require("compare").pystr(p) end

local function main()
  if #arg ~= 3 then io.stderr:write(DOC, "\n"); os.exit(1) end
  local card, inputs, output = arg[1], arg[2], arg[3]
  local renderers = json.decode(path.read_text(card .. ".renderers.json"))
  local sources = {}
  for _, p in ipairs(G.ntfs_sorted(path.glob(inputs, "*.material-inputs.json"))) do sources[#sources + 1] = { p, json.decode(path.read_text(p)) } end
  local results = json.array({})
  for _, p in ipairs(G.sort_paths(path.glob(path.parent(card), path.name(card) .. ".renderer*.mat*._*Mask.uv-samples.rgba32f"))) do
    local m = ok.re.search([[\.renderer(\d+)\.mat(\d+)\.(_\w+Mask)\.]], path.name(p))
    local renderer, slot, prop = m[1], m[2], m[3]
    local native = renderers[G.I(renderer) + 1]
    if native.shaders[G.I(slot) + 1]:lower() == "xukmi/mainitemplus" then
      local name = material_key(native.materials[G.I(slot) + 1])
      local candidates = G.filter(sources, function(c)
        local s = c[2]
        return util.truthy(s.material) and material_key(s.material.name) == name and s.shader_inputs[prop] ~= nil
      end)
      -- A requested mesh can also occur on unrelated legacy draw materials.
      -- This fixture verifies the implemented native MainItemPlus boundary.
      candidates = G.filter(candidates, function(c) return c[2].material.shader:lower() == "xukmi/mainitemplus" end)
      local accessory = ok.re.search([[/ca_slot(\d+)/]], native.path)
      if accessory then
        candidates = G.filter(candidates, function(c) return G.has(path.name(c[1]), "_accessory" .. accessory[1] .. "_") end)
      end
      if #candidates == 0 then error("ValueError: No exact session material/slot input for " .. path.name(p)) end
      local hashes = G.unique(G.map(candidates, function(c) return G.digest(path.join(path.parent(c[1]), c[2].shader_inputs[prop])) end))
      if #hashes ~= 1 then error("ValueError: Ambiguous session input for " .. path.name(p)) end
      local cp, source = candidates[1][1], candidates[1][2]
      local png = path.join(path.parent(cp), source.shader_inputs[prop])
      local native_values = G.read_f32(p, 4)
      local base = p:sub(1, #p - #".uv-samples.rgba32f")
      local uv = B.loadtxt_uv(base .. ".uv-samples.tsv")
      local valid = #uv == #native_values
      local minv
      for _, r in ipairs(native_values) do
        for c = 1, 3 do
          if not G.isfinite(r[c]) then valid = false end
          if minv == nil or r[c] < minv then minv = r[c] end
        end
      end
      if not valid or (minv ~= nil and minv < 0) then error("ValueError: Invalid GPU samples: " .. pystr(p)) end
      local settings = source.runtime_textures[prop]
      local provenance = "source-runtime-texture"
      if settings == nil then
        -- ME/generated mask PNGs have no serialized texture metadata;
        -- KKPreviewActor binds them as linear Bilinear/Repeat, mip0.
        settings = { 1, 0, 0, 0, 1 }
        provenance = "generated-mask-linear-fallback"
      end
      if not (settings[2] == 0 and settings[3] == 0) then error("ValueError: This oracle requires the recorded Repeat sampler on both axes") end
      local srgb = util.truthy(settings[4])
      local err = M.stats(M.abs_rows(native_values, M.sample(png, uv, srgb, true)))
      local wrong = M.stats(M.abs_rows(native_values, M.sample(png, uv, srgb, false)))
      if err.max > .025 or err.mean > .002 then
        error("AssertionError: Source GPU/session sampling mismatch: " .. path.name(p) .. ", max=" .. util.num(err.max) .. ", mean=" .. util.num(err.mean))
      end
      results[#results + 1] = O("mesh", native.mesh, "renderer", G.I(renderer), "material_slot", G.I(slot),
        "native_active", native.active, "native_enabled", native.enabled,
        "property", prop, "samples", #uv, "srgb", srgb,
        "sampling_provenance", provenance,
        "mae_rgb", err.mean_axis0, "max_rgb", err.max_axis0,
        "without_v_conversion_mae_rgb", wrong.mean_axis0,
        "session_png", G.winpath(path.abs(png)), "png_sha256", hashes[1],
        "native_gpu_samples", G.winpath(path.abs(p)),
        "native_sha256", G.digest(p))
    end
  end
  if #results == 0 then error("ValueError: No native mask RGB sampling fixtures") end
  path.write_text(output, json.encode(O("result", "pass", "scope", "mip0 mask RGB and UV transport; alpha excluded",
    "max_threshold", .025, "mean_threshold", .002, "samples", results), { indent = 2 }))
  print(("PASS %d native mask inputs, %d UV samples"):format(#results, G.sum(results, function(r) return r.samples end)))
end

M.main = main
if ok.is_main() then ok.main(main) end
return M
