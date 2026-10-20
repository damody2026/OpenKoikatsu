-- Verify stage 278 source materials, linked UE data and five-card captures.
--
-- Uses actual game mesh dumps; this is a material-input audit, not a claim of
-- full native/UE pixel equality. The native mouth geometry gate remains separate.
local ok = require "ok"
local G = require "gpp"
local json, util, path = ok.json, ok.util, ok.path
local O, F, I, check, findall = G.O, G.F, G.I, G.check, G.findall

local ROOT = G.ROOT
local LOGS = ROOT .. "/Saved/Logs"
local CARDS = { "[Card] March_7th_keiyuu", "Alice_keiyuu",
  "Hayase_yuuka_keiyuu_V1_02 For KKS", "KK_091207", "KK_384283" }

local function read(p) return G.read(p) end
-- ast.literal_eval of a list/tuple of numbers
local function literal(s)
  s = util.strip(s):gsub("^%(", "["):gsub("%)$", "]")
  return json.decode(s)
end
local function maxdiff(a, b)
  local m
  for i = 1, math.min(#a, #b) do local d = math.abs(a[i] - b[i]); if m == nil or d > m then m = d end end
  if m == nil then error("max() arg is an empty sequence") end
  return m
end

local function verify()
  local material_tests = read(LOGS .. "/MouthMaterialSelectedSource278Finalv2.log")
  check(G.has(material_tests, "3 passed; 0 failed"))
  check(G.count(material_tests, "HEAD_ITEM_SOURCE card=") == 5)
  check(G.has(read(LOGS .. "/MouthMaterialGeometryRust278Finalv2.log"), "1 passed; 0 failed"))
  check(G.has(read(LOGS .. "/MouthMaterialUnrealBuild278Finalv3.log"), "Result: Succeeded"))
  -- TestExit requests forced process exit; the async file logger can lose
  -- its tail. Console output retains every result and queue/exit gate.
  local automation = read(LOGS .. "/MouthMaterialUnrealAutomation278Finalv3Console.log")
  for _, test in ipairs({ "HandShapes", "ImportedData", "Playback", "NativeWeights", "SharedDeformation" }) do
    check(G.has(automation, "Result={Success} Name={" .. test .. "}"))
  end
  check(not G.has(automation, "Result={Fail}"))
  check(G.has(automation, "Automation Test Queue Empty 5 tests performed"))
  check(G.has(automation, "RequestExitWithStatus(1, 0"))

  local resolved, resolved_names = {}, {}
  for _, line in ipairs(util.splitlines(read(ROOT .. "/Saved/Tests/MouthMaterial278/resolved-materials.tsv"))) do
    local f = util.split(line, "\t")
    if #f ~= 4 then error("ValueError: not enough values to unpack") end
    check(f[1] == "CARD")
    local name = f[2]:gsub("%.png$", "")
    if resolved[name] == nil then resolved_names[#resolved_names + 1] = name end
    resolved[name] = { literal(f[3]), literal(f[4]) }
  end
  check(G.set_eq(resolved_names, CARDS))
  check(#G.unique(util.values(resolved), function(c) return c[1] end) == 4)

  -- Fresh controls identify the source by the actual card path. Old dump
  -- folder labels can disagree with material values (Alice/Yuuka crossed).
  local native_sources, native_colors = json.object(), json.object()
  local folder_native = ROOT .. "/Saved/Tests/MouthMaterial278"
  for _, card in ipairs(CARDS) do
    local tag = (card .. ".png"):gsub("%[", ""):gsub("%]", ""):gsub(" ", "_")
    local basename = card == "Alice_keiyuu" and "GameAliceMaterials278.json" or ("Game" .. tag .. "Materials278.json")
    check(json.decode(read(folder_native .. "/" .. basename))["error"] == "")
    local records = {}
    for _, line in ipairs(util.splitlines(read(folder_native .. "/" .. basename .. ".head-materials-final.tsv"))) do records[#records + 1] = util.split(line, "\t") end
    local materials = G.filter(records, function(r) return r[1] == "material" and util.endswith(r[2], "/o_tang") end)
    check(#materials == 1)
    local shader = materials[1][4]
    local count = ({ ["Shader Forge/main_item"] = 3, ["Koikano/main_clothes_item"] = 4 })[shader]
    if count == nil then error("KeyError: " .. tostring(shader)) end
    local colors = json.object()
    for _, r in ipairs(records) do
      if r[1] == "color" and r[2] == "o_tang" then
        local v = {}
        for k = 5, #r do v[#v + 1] = F(r[k]) end
        colors[r[4]] = v
      end
    end
    for k, property in ipairs({ "_Color", "_ShadowColor" }) do
      if colors[property] == nil then error("KeyError: " .. property) end
      check(maxdiff(colors[property], resolved[card][k]) <= 1e-6)
    end
    for i = 2, count do check(colors["_Color" .. i] ~= nil) end
    native_sources[card] = O("path", G.winpath(folder_native .. "/" .. basename), "shader", shader, "color_count", count)
    native_colors[card] = colors
  end

  local dump_root = ok.game_root(true) .. "/mesh_dump"
  local sources = json.object()
  for _, card in ipairs(CARDS) do
    local directory = dump_root .. "/" .. card .. (util.startswith(card, "KK_") and ".png" or "_outfit0_face.png")
    local candidates = json.array({})
    for _, p in ipairs(G.sort_paths(path.rglob(directory, "*o_tang.materials.json"))) do
      local mesh = json.decode(read(path.with_name(p, path.name(p):gsub("%.materials%.json", ".json"))))
      if G.has(mesh.hierarchyPath, "/p_cf_head_bone/ct_head/") then
        for _, material in ipairs(json.decode(read(p)).materials) do
          if material.shader == "Koikano/main_clothes_item" or material.shader == "Shader Forge/main_item" then
            local props = {}
            for _, pr in ipairs(material.properties) do props[pr.name] = pr end
            local color = G.map({ "r", "g", "b", "a" }, function(c) return props._Color.value[c] end)
            local shadow = G.map({ "r", "g", "b", "a" }, function(c) return props._ShadowColor.value[c] end)
            check(props._MainTex.texture.textureName == json.null)
            check(props._ColorMask.texture.textureName == "cf_dummy_mc")
            candidates[#candidates + 1] = O("path", G.winpath(p), "shader", material.shader, "color", color, "shadow", shadow,
              "matches_current_native", material.shader == native_sources[card].shader
                and maxdiff(color, resolved[card][1]) <= 1e-6 and maxdiff(shadow, resolved[card][2]) <= 1e-6)
          end
        end
      end
    end
    check(#candidates > 0, "no legacy head material data for " .. card)
    sources[card] = candidates
  end

  local shots = read(LOGS .. "/MouthMaterialShots278Final.log")
  check(G.has(shots, "ok.ShotPreview explicit cards=5"))
  check(G.has(shots, "rendering 5 task(s) (5 card(s) x 1 variant(s) x 1 outfit(s))"))
  check(G.count(shots, "Tracing Screenshot") == 15)
  check(G.has(shots, "FPlatformMisc::RequestExitWithStatus(0, 0"))
  check(not G.has(shots, "Failed to compile Material") and not G.has(shots, "Fatal error"))
  local tasks = util.split(shots, "ok.ShotPreview task ")
  check(#tasks == 6)
  for i = 1, #tasks - 1 do check(G.has(tasks[i], "OK part source inactive name=cf_O_canine")) end
  -- Materials are loaded before the task line. Check each matching task's
  -- preceding load interval so the preflight cannot satisfy every card.
  local draw_records = json.object()
  for index, card in ipairs(CARDS) do
    local records = findall([[OK head item draw name=o_tang shader_kind=(\d+) colors=(\d+) color=\(([^)]+)\) mask_bytes=(\d+) mask_decoded=(\d+)]], tasks[index])
    check(#records > 0, "head draw material missing for " .. card)
    local r = records[#records]
    local values = G.map(util.split(r[3], ","), F)
    check(r[1] == "3" and r[5] == "1" and I(r[4]) > 16)
    check(I(r[2]) == native_sources[card].color_count)
    check(maxdiff(values, resolved[card][1]) <= 1e-6)
    draw_records[card] = values
  end
  local errors = G.floats(findall([[max_vertex_error_cm=([\d.]+)]], shots))
  check(#errors == 71 and G.max(errors) <= 1e-6)
  local fits = findall([[capture fit view=(\w+) points=(\d+) distance=([\d.]+) ndc_x=([\d.]+) ndc_y=([\d.]+)]], shots)
  check(#fits == 15 and G.all(fits, function(f) return F(f[4]) <= .92001 and F(f[5]) <= .92001 end))

  local folder = ROOT .. "/Saved/DebugShots/MouthMaterial278"
  local expected = {}
  for _, card in ipairs(CARDS) do for _, view in ipairs({ "3d", "face", "face_side" }) do expected[#expected + 1] = card .. "_outfit0_" .. view .. ".png" end end
  check(G.set_eq(G.glob_names(folder, "*.png"), expected))
  check(G.all(expected, function(name) return G.size_is(folder .. "/" .. name, 900, 1080) end))
  local bounds = json.object()
  for _, card in ipairs(CARDS) do
    local b = G.fg_bounds(folder .. "/" .. card .. "_outfit0_3d.png", 12)
    if b == nil then error("ValueError: zero-size array to reduction operation minimum which has no identity") end
    bounds[card] = b
    check(b[2] > 0 and b[4] <= 1036)
  end
  local world = json.decode(read(LOGS .. "/MouthWorld277Comparison.json"))
  local geometry = util.values(world.geometry)
  local native_error = G.max(geometry, function(c) return c.mouth_max end)
  check(#geometry == 40 and native_error <= 2e-5)
  local result = O("pipeline", 125, "rust_material_tests", 3, "ue_tests_success", 5,
    "native_geometry_cases", 40, "native_mouth_max_error_m", native_error,
    "cards", CARDS, "images", 15, "attachment_audits", 71,
    "attachment_max_error_cm", G.max(errors), "fits", 15,
    "tongue_draw_colors", draw_records, "legacy_dump_color_files", sources,
    "native_head_materials", native_sources, "native_colors", native_colors,
    "full_body_bounds", bounds,
    "limitations", "Source material inputs and mouth geometry verified; native/UE lighting, eye state and full pixels are not claimed equal.")
  G.write_json(LOGS .. "/MouthMaterial278Audit.json", result)
  print(json.encode(result, { ensure_ascii = false }))
end

if ok.is_main() then ok.main(verify) end
