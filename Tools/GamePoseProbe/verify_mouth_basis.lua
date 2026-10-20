-- Verify stage 277's native world control and five actual Unreal cards.
local ok = require "ok"
local G = require "gpp"
local json, util = ok.json, ok.util
local F, check, findall = G.F, G.check, G.findall
local compare = require("compare_mouth_world").compare

local ROOT = G.ROOT
local LOGS = ROOT .. "/Saved/Logs"

local function verify()
  compare(true, false)
  local world = json.decode(ok.path.read_text(LOGS .. "/MouthWorld277Comparison.json"))
  local raw = ROOT .. "/Saved/Tests/MouthWorld277/GameMouthWorldMarch.json.mouth-world.tsv"
  check(G.sum(util.splitlines(G.read(raw)), function(line) return util.startswith(line, "world_vertex\t") and 1 or 0 end) == 33150)
  for _, name in ipairs({ "MouthBasisUnit277.log", "MouthNeutralUnit277Final.log" }) do
    check(G.has(G.read(LOGS .. "/" .. name), "1 passed; 0 failed"))
  end
  local card_flags = G.read(LOGS .. "/DoubleToothCard277Finalv2.log")
  check(G.has(card_flags, "2 passed; 0 failed"))
  check(G.count(card_flags, "DOUBLE_TOOTH_CARD name=") == 5)
  check(G.has(G.read(LOGS .. "/DoubleToothCache277Final.log"), "1 passed; 0 failed"))
  check(G.has(G.read(LOGS .. "/MouthGeometryRust277Final.log"), "1 passed; 0 failed"))
  check(G.has(G.read(LOGS .. "/MouthBasisUnrealBuild277Final.log"), "Result: Succeeded"))
  local automation = G.read(LOGS .. "/MouthBasisUnrealAutomation277Final.log")
  check(not G.has(automation, "Result={Fail}"))
  for _, name in ipairs({ "HandShapes", "ImportedData", "Playback", "NativeWeights", "SharedDeformation" }) do
    check(G.has(automation, "Result={Success} Name={" .. name .. "}"))
  end
  local shots = G.read(LOGS .. "/MouthBasisShots277Finalv2.log")
  check(G.has(shots, "explicit cards=5"))
  check(G.has(shots, "rendering 5 task(s) (5 card(s) x 1 variant(s) x 1 outfit(s))"))
  check(G.has(shots, "RequestExitWithStatus(0, 0,"))
  check(G.count(shots, "Tracing Screenshot") == 15)
  check(G.count(shots, "ok.ShotPreview imported Studio row=") == 5)
  local tasks = ok.re.split([[ok.ShotPreview task \d/5]], shots)
  check(#tasks == 6)
  -- LoadFromSession occurs immediately before each task line, so check each
  -- interval rather than accepting a skip from just the preflight actor.
  for i = 1, #tasks - 1 do check(G.has(tasks[i], "OK part source inactive name=cf_O_canine")) end
  check(G.count(card_flags, "visible=false") == 5)
  check(not G.has(shots, "mouth secondary section topology mismatch"))
  local errors = G.floats(findall([[max_vertex_error_cm=([\d.]+)]], shots))
  check(#errors == 71 and G.max(errors) <= 1e-6)
  local fits = findall([[capture fit view=(\w+) points=(\d+) distance=([\d.]+) ndc_x=([\d.]+) ndc_y=([\d.]+)]], shots)
  check(#fits == 15 and G.all(fits, function(f) return F(f[4]) <= .92001 and F(f[5]) <= .92001 end))
  local cards = { "[Card] March_7th_keiyuu", "Alice_keiyuu", "Hayase_yuuka_keiyuu_V1_02 For KKS", "KK_091207", "KK_384283" }
  local folder = ROOT .. "/Saved/DebugShots/MouthBasis277"
  local expected = {}
  for _, card in ipairs(cards) do for _, view in ipairs({ "3d", "face", "face_side" }) do expected[#expected + 1] = card .. "_outfit0_" .. view .. ".png" end end
  check(G.set_eq(G.glob_names(folder, "*.png"), expected))
  check(G.all(expected, function(name) return G.size_is(folder .. "/" .. name, 900, 1080) end))
  local bounds = json.object()
  for _, card in ipairs(cards) do
    local b = G.fg_bounds(folder .. "/" .. card .. "_outfit0_3d.png", 12)
    if b == nil then error("ValueError: zero-size array to reduction operation minimum which has no identity") end
    bounds[card] = b
    check(b[2] > 0 and b[4] <= 1036, "head and feet must retain the capture margin")
  end
  local geometry, keys = world.geometry, json.keys(world.geometry)
  local function sel(pred) return G.filter(keys, pred) end
  local flags = json.object()
  for _, card in ipairs(cards) do flags[card] = false end
  local audit = G.O(
    "native_world_fixture_sha256", G.digest(raw),
    "native_world_vertex_records", 33150, "coordinate_frame", world.coordinate_frame,
    "native_mouth_geometry_cases", #keys,
    "mouth_supported_vertices_per_case", G.sum(sel(function(k) return util.endswith(k, ":8:80") end), function(k) return geometry[k].mouth_vertices end),
    "native_mouth_support_max_error_m", G.max(keys, function(k) return geometry[k].mouth_max end),
    "native_teeth_tongue_full_mesh_max_error_m", G.max(sel(function(k)
      return util.startswith(k, "cf_O_tooth:") or util.startswith(k, "cf_O_canine:") or util.startswith(k, "o_tang:") end), function(k) return geometry[k].max end),
    "native_shared_head_bone_max_matrix_component_error", G.max(util.values(world.bone_max_component_errors)),
    "unreal_tests_success", 5, "unreal_capture_cards", cards, "unreal_image_count", 15,
    "unreal_attachment_audits", #errors, "unreal_attachment_max_display_error_cm", G.max(errors),
    "source_double_tooth_flags", flags,
    "source_inactive_canine_skips", G.count(shots, "OK part source inactive name=cf_O_canine"),
    "unreal_full_body_foreground_bbox", bounds,
    "limitations", "Native fixture is March; the other four cards have actual UE regressions. Native non-mouth eye state and full pixels are not claimed equal.")
  G.write_json(LOGS .. "/MouthBasis277Audit.json", audit)
  print(json.encode(audit, { ensure_ascii = false }))
end

if ok.is_main() then ok.main(verify) end
