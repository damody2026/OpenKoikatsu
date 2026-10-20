-- Compare cached hair colours and source samplers with installed Studio GPU inputs.
local ok = require "ok"
local G = require "gpp"
local json, path, util = ok.json, ok.path, ok.util
local O, F, check = G.O, G.F, G.check
local B = require "verify_brow_uv_coverage"

local M = {}

-- sample(image, uv, repeat): flipped v, accumulated bilinear, repeat or clamp.
function M.sample(desc, uv, rep, shift)
  return B.tex_sample(desc, uv, { kernel = "accumulate", address = rep and "repeat" or "clip", flip = true, shift = shift }).samples
end

local function tex_diff(a, b)
  return ok.kit.call({ "probe", "tex-diff", "-" }, { input = json.encode({ a = { png = a.png, ops = json.array(a.ops) },
    b = { png = b.png, ops = json.array(b.ops) }, channels = "rgb" }) }).max
end

local function name_of(p) return (tostring(p):gsub("\\", "/"):match("([^/]*)$")) end

local function main()
  local native = "Saved/Tests/Hair322SamplingControlsNative"
  local sources = json.decode(path.read_text(native .. "/Sources.json"))
  local inputs = json.decode(path.read_text("Saved/Tests/Hair322CachedInputs/inputs.json", { keep_bom = true }))
  local rows, sampling = json.array({}), json.array({})
  for _, item in ipairs(inputs) do
    local card
    for i, s in ipairs(sources) do if name_of(s.card) == item.card then card = i - 1; break end end
    if card == nil then error("StopIteration") end
    local prefix = native .. "/Card" .. card .. ".json"
    local renderers = json.decode(path.read_text(prefix .. ".renderers.json"))
    local matches = {}
    for i, r in ipairs(renderers) do
      if r.mesh == item.mesh and util.split(r.materials[1], " (Instance)")[1] == item.material then matches[#matches + 1] = { i - 1, r } end
    end
    check(#matches == 1, { item.mesh, item.material, matches })
    local index, renderer = matches[1][1], matches[1][2]
    local tbl = {}
    for _, line in ipairs(util.splitlines(path.read_text(prefix .. ".materials.tsv"))) do tbl[#tbl + 1] = util.split(line, "\t") end
    local colors = {}
    for _, r in ipairs(tbl) do
      if r[1] == "color" and r[2] == renderer.path then
        local body = r[5]
        if util.startswith(body, "RGBA(") then body = body:sub(6) end
        if util.endswith(body, ")") then body = body:sub(1, -2) end
        colors[r[4]] = G.map(util.split(body, ","), F)
      end
    end
    local delta
    for i, key in ipairs({ "_Color", "_Color2", "_Color3" }) do
      local c = colors[key]
      if c == nil then error("KeyError: " .. key) end
      local m
      for k = 1, #c do
        local d = math.abs(item.colors[i][k] - c[k])
        if m == nil or d > m then m = d end
      end
      m = m + 0.0
      if delta == nil or m > delta then delta = m end
    end
    check(delta <= 1e-6, { item.mesh, delta })
    local function texture(prop)
      for _, r in ipairs(tbl) do if r[1] == "texture" and r[2] == renderer.path and r[4] == prop then return r[5] end end
      error("StopIteration")
    end
    local native_gloss = texture("_HairGloss")
    check((native_gloss == "null") == (item.textures._HairGloss == nil), { item.mesh, native_gloss })
    local native_main = texture("_MainTex")
    if native_main == "null" then
      local img = ok.image.load(item.textures._MainTex, "RGBA")
      check(not img.data:find("[^\255]"), item.mesh)
    end
    rows[#rows + 1] = O("card", item.card, "mesh", item.mesh, "renderer", index, "colour_max", delta, "gloss", native_gloss, "main", native_main)
    for _, property in ipairs({ "_ColorMask", "_DetailMask" }) do
      local stem = prefix .. ".renderer" .. index .. ".mat0." .. property
      local tsv = stem .. ".uv-samples.tsv"
      if path.exists(tsv) then
        local uv = B.loadtxt_uv(tsv)
        local gpu = G.read_f32(stem .. ".uv-samples.rgba32f", 4)
        -- KKPreviewActor's absent DetailMask metadata path explicitly uses
        -- bilinear, repeat and linear RGB. Verify that actual fallback too.
        local profile = item.runtime_textures[property] or { 1, 0, 0, 0, 1 }
        local encoded = B.pixels(item.textures[property])
        local native_encoded = B.pixels(stem .. ".png")
        -- Probe.WriteTexture blits Texture2D into an sRGB ARGB32 target.
        -- For a linear source, undo that output encoding before comparing
        -- source texels. A gamma source already round-trips encoded RGB.
        if not util.truthy(profile[4]) then native_encoded = B.with_ops(native_encoded, { srgb = true }) end
        local encoded_error = tex_diff(encoded, native_encoded)
        check(encoded_error <= 6 / 255 + 1e-7, { item.mesh, property, encoded_error })
        local function decode(desc, extra)
          if extra then desc = B.with_ops(desc, extra) end
          if util.truthy(profile[4]) then desc = B.with_ops(desc, { srgb = true }) end
          return desc
        end
        local rep = profile[2] == 0
        local image = decode(encoded)
        local value = M.sample(image, uv, rep)
        if #value ~= #gpu then error("ValueError: operands could not be broadcast together") end
        local errs = {}
        for k = 1, #value do for c = 1, 3 do errs[#errs + 1] = math.abs(value[k][c] - gpu[k][c]) end end
        local err = G.np_max(errs)
        -- DXT/RGBA8 readback bounds are in encoded texels. Propagate that
        -- interval through sRGB decoding and bilinear sampling, rather
        -- than imposing the same numeric bound on linear-light output.
        -- D3D11 sections 3.2.4.1 and 7.18.16.1 allow 0.6 ULP
        -- float-to-fixed error at at least eight fractional subtexel bits.
        -- Bound bilinear output over that coordinate interval. Include
        -- the centre to cover a possible tap boundary within the interval.
        local w, h = ok.image.size(item.textures[property])
        local lower = decode(encoded, { add = -(6 / 255), channels = "rgba" })
        local upper = decode(encoded, { add = 6 / 255, channels = "rgba" })
        local lo, hi
        for _, dy in ipairs({ -.6 / 256, 0, .6 / 256 }) do
          for _, dx in ipairs({ -.6 / 256, 0, .6 / 256 }) do
            local shift = { dx / w, dy / h }
            local l, u = M.sample(lower, uv, rep, shift), M.sample(upper, uv, rep, shift)
            if lo == nil then
              lo, hi = {}, {}
              for k = 1, #l do lo[k] = { l[k][1], l[k][2], l[k][3] }; hi[k] = { u[k][1], u[k][2], u[k][3] } end
            else
              for k = 1, #l do
                for c = 1, 3 do
                  local a, b = l[k][c], u[k][c]
                  if a < lo[k][c] or a ~= a then lo[k][c] = a end
                  if b > hi[k][c] or b ~= b then hi[k][c] = b end
                end
              end
            end
          end
        end
        local viol = {}
        for k = 1, #lo do
          for c = 1, 3 do
            local g = gpu[k][c]
            local v = math.max(lo[k][c] - g, g - hi[k][c])
            viol[#viol + 1] = v > 0 and v or 0.0
          end
        end
        local violation = G.np_max(viol) + 0.0
        sampling[#sampling + 1] = O("card", item.card, "mesh", item.mesh, "property", property, "samples", #uv,
          "rgb_max", err, "encoded_texture_rgb_max", encoded_error, "encoded_texel_bound", 6 / 255,
          "subtexel_coordinate_bound", .6 / 256, "bound_violation", violation, "passed", violation <= 2e-5)
      end
    end
  end
  check(#rows == 10 and #sampling == 8, { #rows, #sampling })
  local passed = G.all(sampling, function(r) return r.passed end)
  local report = O("passed", passed,
    "scope", "10 actual cached source material colours/null texture semantics; 4 selected renderers at native GPU mesh UV mip0; encoded DXT/RGBA8 bound 6/255 and D3D11 subtexel precision interval; final lighting parity separate",
    "precision_source", "https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#3.2.4.1%20FLOAT%20-%3E%20Fixed%20Point%20Integer",
    "colour_cases", rows, "sampling", sampling)
  path.write_text("Saved/Logs/Hair322NativeInputsAudit.json", json.encode(report, { ensure_ascii = false, indent = 2 }))
  print(json.encode(O("passed", passed, "cases", #rows, "sampling", #sampling)))
  if not passed then os.exit(1) end
end

if ok.is_main() then ok.main(main) end
return M
