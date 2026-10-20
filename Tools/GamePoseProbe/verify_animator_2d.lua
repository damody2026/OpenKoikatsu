-- Stage290: native Cartesian weights/TRS, explicit masked layer, UE and UI gates.
local ok = require "ok"
local G = require "gpp"
local VA = require "verify_animator"
local json, util = ok.json, ok.util
local O, A, F, I, check, findall = G.O, G.A, G.F, G.I, G.check, G.findall
local CARDS, TESTS, digest, read = VA.CARDS, VA.TESTS, VA.digest, VA.read

local ROOT = G.ROOT
local LOGS = ROOT .. "/Saved/Logs"
local DATA = ROOT .. "/Saved/Tests/Animator290"
local SHOTS = ROOT .. "/Saved/DebugShots/Animator290_Layer_open"
local STATE_ORDER = { "17:Te_siri_D_momu_L.Te_siri_D_momu_L", "05:Te_mune_D_momu_L.Te_mune_D_momu_L" }
local STATES = {
  ["17:Te_siri_D_momu_L.Te_siri_D_momu_L"] = { 17, "siriL_X", "siriL_Y" },
  ["05:Te_mune_D_momu_L.Te_mune_D_momu_L"] = { 5, "muneR_X", "muneR_Y" },
}
local POINTS = { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 }, { .5, .5 }, { .5, 0 }, { 0, .5 },
  { 1, .5 }, { .5, 1 }, { .17, .43 }, { .31, .79 }, { .74, .18 }, { .68, .84 },
  { .43, .27 }, { -.25, .37 }, { 1.25, .61 }, { .61, -.25 }, { .37, 1.25 } }

local function concat(a, b) local out = { table.unpack(a) }; for _, v in ipairs(b) do out[#out + 1] = v end; return out end

-- value.get('data', value) if isinstance(value, dict) else value
local function unwrap(value)
  if json.is_object(value) and value.data ~= nil then return value.data end
  return value
end
-- next(x for x in list if pred(x))
local function first(list, pred)
  for _, v in ipairs(list) do if pred(v) then return v end end
  error("StopIteration", 2)
end

local function main()
  local graph = json.decode(read(DATA .. "/Controllers.json"))
  check(G.set_eq(G.map(graph, function(c) return c.name end), { "khs3_o_base", "khh_f_base", "khh3_f_base", "khs3_f_base3" }))
  local record = first(graph, function(c) return c.name == "khs3_f_base3" end)
  local chosen = record.controller
  local timing = {}
  for _, name in ipairs(STATE_ORDER) do
    local index = STATES[name][1]
    local layer = unwrap(chosen.m_LayerArray[index + 1])
    local machine = unwrap(chosen.m_StateMachineArray[layer.m_StateMachineIndex + 1])
    local state = unwrap(first(machine.m_StateConstantArray, function(s) return record.tos[util.str(unwrap(s).m_FullPathID)] == name end))
    local multiplier = 1
    if util.truthy(state.m_SpeedParamID) then
      local parameter = unwrap(first(unwrap(chosen.m_Values).m_ValueArray,
        function(pp) return unwrap(pp).m_ID == state.m_SpeedParamID and unwrap(pp).m_Type == 1 end))
      multiplier = unwrap(chosen.m_DefaultValues).m_FloatValues[parameter.m_Index + 1]
    end
    timing[name] = { state.m_Speed, multiplier }
  end
  local native = json.object()
  local coverage = {}
  for _, s in ipairs(STATE_ORDER) do
    for _, pt in ipairs(POINTS) do
      for _, ph in ipairs({ 0, .17, .43, .91 }) do coverage[#coverage + 1] = { s, pt, ph } end
    end
  end
  for _, card in ipairs(CARDS) do
    local stem = G.sanitize(card) .. "Animator290.json"
    local complete, fpath = DATA .. "/" .. stem, DATA .. "/" .. stem .. ".animator-2d.json"
    check(json.decode(read(complete))["error"] == "")
    local frames = json.decode(read(fpath))
    check(#frames == 144)
    local actual = G.map(frames, function(f)
      return { f.state, { util.round(f.value, 2), util.round(f.value2, 2) }, util.round(f.phase, 2) }
    end)
    check(G.set_eq(actual, coverage))
    local transforms = #frames[1].bones
    check(transforms >= 450)
    for _, f in ipairs(frames) do
      local st = STATES[f.state]
      if st == nil then error("KeyError: " .. tostring(f.state)) end
      check(f.layer == st[1] and f.parameter == st[2] and f.parameter2 == st[3])
      check(f.controller == "khs3_f_base3" and f.baseState == "Base Layer.Idle")
      check(f.baseParameter == "height" and math.abs(f.baseValue - .37) < 1e-6)
      check(f.parameter3 == "Breast" and math.abs(f.value3 - .61) < 1e-6)
      check(G.all({ "speed", "speedMultiplier" }, function(k) return G.isfinite(f[k]) and f[k] > 0 end))
      check(math.abs(f.speed - timing[f.state][1]) < 1e-6)
      check(math.abs(f.speedMultiplier - timing[f.state][2]) < 1e-6)
      check(G.all({ "length", "baseLength" }, function(k) return G.isfinite(f[k]) and f[k] > 0 end))
      check(math.abs(f.basePhase * f.baseLength - f.phase * f.length) < 1e-5)
      check(#f.clips > 0 and math.abs(G.sum(f.clips, function(c) return c.weight end) - 1) < 1e-5)
      check(G.all(f.clips, function(c) return G.isfinite(c.weight) and c.weight >= 0 and
        G.isfinite(c.length) and c.length > 0 end))
      check(#f.bones == transforms and #G.unique(f.bones, function(b) return b.name end) == transforms)
      check(G.all(f.bones, function(b)
        for _, k in ipairs({ "position", "rotation", "scale" }) do
          for _, v in pairs(b[k]) do if not G.isfinite(v) then return false end end
        end
        return true
      end))
    end
    native[card] = O("frames", 144, "unique_transforms_per_frame", transforms,
      "sha256", digest(fpath), "completion_sha256", digest(complete))
  end

  for _, index in ipairs({ 5, 17 }) do
    local layer = unwrap(chosen.m_LayerArray[index + 1])
    check(layer["(int&)m_LayerBlendingMode"] == 0 and layer.m_DefaultWeight == 0)
    local mask = unwrap(layer.m_SkeletonMask).m_Data
    check(#mask == 627 and G.any(mask, function(v) return v.m_Weight == 0 end))
  end

  local rust = read(LOGS .. "/AnimatorRust290AllAccepted.log")
  check(G.has(rust, "1 passed; 0 failed"))
  local rows = findall([[Animator2D card=(.+?) frames=144 transform_samples=(\d+) position_max_m=([\d.]+) rotation_max=([\d.]+) scale_max=([\d.]+)]], rust)
  check(#rows == 5 and G.set_eq(G.map(rows, function(r) return r[1] end), CARDS))
  check(G.all(rows, function(r) return I(r[2]) == 38880 and math.max(F(r[3]), F(r[4]), F(r[5])) < 2e-5 end))
  check(G.has(rust, "accepted cards=5 frames=720 transform_samples=194400"))
  local one_d = read(LOGS .. "/AnimatorRust290OneDRegression.log")
  check(G.has(one_d, "accepted cards=5 frames=300 transform_samples=81000") and G.has(one_d, "1 passed; 0 failed"))
  local regression = json.object()
  for _, name in ipairs({ "Controller", "Catalogue" }) do
    local match = G.search([[test result: ok\. (\d+) passed; 0 failed]],
      read(LOGS .. "/AnimatorRust290" .. name .. "Regression.log"))
    check(match and I(match[1]) > 0)
    regression[name] = I(match[1])
  end
  check(G.has(read(LOGS .. "/AnimatorUnrealBuild290.log"), "Result: Succeeded"))
  check(G.has(read(LOGS .. "/AnimatorUnrealBuild290CameraAccepted.log"), "Result: Succeeded"))
  local tests = read(LOGS .. "/AnimatorUnrealAutomation290Console.log")
  check(not G.has(tests, "Result={Fail}"))
  check(G.set_eq(findall([[Test Completed\. Result=\{Success\} Name=\{([^}]+)\}]], tests), concat(TESTS, { "Animator2DLayers" })))
  check(G.count(tests, "Test Completed. Result={Success}") == 14)
  check(G.has(tests, "Automation Test Queue Empty 14 tests performed") and G.has(tests, "RequestExitWithStatus(1, 0,"))
  for _, kp in ipairs({ { "Animator1D", "states=3 frames=90" }, { "Animator2D", "states=2 frames=60" } }) do
    local cards = findall(kp[1] .. [[ card=(.+?) ]] .. kp[2] .. [[ finite=1]], tests)
    check(#cards == 5 and G.set_eq(cards, CARDS))
  end
  for _, label in ipairs({ "BodyDynamics", "ExternalBody" }) do
    local controls = findall(label .. [[ card=(.+?) frames=180 .*?changed=(\d+) max_delta_cm=([\d.]+) max_rotation_rad=([\d.]+)]], tests)
    check(#controls == 5 and G.set_eq(G.map(controls, function(c) return c[1] end), CARDS))
    check(G.all(controls, function(c) return I(c[2]) > 0 and F(c[3]) > 1e-3 and F(c[4]) > 1e-4 end))
  end
  check(G.has(tests, "preserved_missing=1 coordinate_roundtrips=2 invalid_clip_rejected=1"))

  local capture = read(LOGS .. "/AnimatorShots290_Layer_open.log")
  check(G.has(capture, "RequestExitWithStatus(0, 0,") and not G.has(capture, "LogTemp: Error:"))
  check(not G.has(capture, "Failed to compile Material"))
  check(G.has(capture:gsub("\\", "/"), SHOTS))
  check(G.count(capture, "Tracing Screenshot") == 15)
  for _, name in ipairs({ "siriL_X=0.31", "siriL_Y=0.79" }) do
    check(G.count(capture, "OK shot animation float " .. name) == 5)
  end
  check(G.count(capture, "OK game animation scene mode=4") == 5)
  check(G.count(capture, "clip=states:studio/anime/00.unity3d|khs3_f_base3 / 17:Te_siri_D_momu_L.Te_siri_D_momu_L") == 5)
  local attachments = G.floats(findall([[max_vertex_error_cm=([\d.]+)]], capture))
  check(#attachments == 71 and G.max(attachments) == 0)
  local fits = findall([[capture fit view=\w+ .*?ndc_x=([\d.]+) ndc_y=([\d.]+) limit=0.92]], capture)
  check(#fits == 15 and G.all(fits, function(f) return math.max(F(f[1]), F(f[2])) <= .920001 end))
  check(G.count(capture, "OK gaze display audit passed=1") == 5)
  check(G.count(capture, "OK eye highlight display audit passed=1") == 5)
  local floors = findall([[OK studio posed floor min_z=([-\d.]+) plane_z=([-\d.]+)]], capture)
  check(#floors == 5 and G.any(floors, function(fl) return F(fl[1]) < 0 end))
  check(G.all(floors, function(fl) return F(fl[2]) <= F(fl[1]) - .499 end))
  local expected = {}
  for _, card in ipairs(CARDS) do
    for _, view in ipairs({ "3d", "face", "face_side" }) do expected[#expected + 1] = card .. "_outfit0_" .. view .. ".png" end
  end
  check(G.set_eq(G.glob_names(SHOTS, "*.png"), expected))
  local sheet = G.sheet(900, 1920, { 0x30, 0x30, 0x30 })
  for row, card in ipairs(CARDS) do
    local r = row - 1
    sheet:text(8, r * 384 + 6, card .. " / explicit Cartesian layer (.31,.79)")
    local neutral = { G.load_rgb(SHOTS .. "/" .. card .. "_outfit0_3d.png"):get(0, 0) }
    check(120 <= math.min(table.unpack(neutral)) and math.max(table.unpack(neutral)) <= 135)
    for col, view in ipairs({ "3d", "face", "face_side" }) do
      local file = SHOTS .. "/" .. card .. "_outfit0_" .. view .. ".png"
      check(G.size_is(file, 900, 1080))
      if view == "face_side" then
        check(G.top_row_neutral(G.load_rgb(file), neutral))
      end
      sheet:paste(file, (col - 1) * 300, r * 384 + 24, 300, 360)
    end
  end
  sheet:save(DATA .. "/ContactSheet.png")
  local gallery = read(LOGS .. "/AnimatorGallery290.log")
  check(not G.has(gallery, "LogTemp: Error:") and G.has(gallery, "RequestExitWithStatus(0, 0,"))
  check(G.has(gallery, "KK Cartesian gallery verified source_callback=1 state_callback=1 slider_callbacks=2 rows=3 phase_preserved=1 face_clock_unchanged=1"))
  local ui = ROOT .. "/Saved/DebugShots/Animator290UI/gallery_imported_studio.png"
  check(G.size_is(ui, 1440, 1000))
  local sources = { "Tools/ok-extract/src/anim.rs", "Tools/ok-extract/src/anim/controller.rs",
    "Tools/ok-extract/src/anim/graph_tests.rs", "Tools/ok-extract/src/session.rs",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/KKPreviewActor.cpp",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/Tests/KKAnimatorCartesian.spec.cpp",
    "Source/OpenKoikatsu/OKGalleryWidget.cpp", "Tools/GamePoseProbe/Probe.cs",
    "old_ps1/Tools/GamePoseProbe/run_animator_2d_native.ps1", "old_ps1/Tools/GamePoseProbe/run_animator_2d.ps1",
    "old_ps1/Tools/GamePoseProbe/run_animator_2d_rust.ps1",
    "Tools/GamePoseProbe/verify_animator_2d.lua", "Tools/anim_export/inspect_2d_graph.lua" }
  local logs = { "AnimatorRust290AllAccepted.log", "AnimatorRust290OneDRegression.log",
    "AnimatorRust290ControllerRegression.log", "AnimatorRust290CatalogueRegression.log",
    "AnimatorUnrealBuild290.log", "AnimatorUnrealBuild290CameraAccepted.log", "AnimatorUnrealAutomation290Console.log",
    "AnimatorShots290_Layer_open.log", "AnimatorGallery290.log", "AnimatorNative290All.log" }
  local report = O("stage", 290, "cards", 5, "native_frames", 720, "animated_trs_samples", 194400,
    "native_controls", native, "native_comparisons", G.rows(rows), "rust_regression", regression,
    "controller_audit_sha256", digest(DATA .. "/Controllers.json"), "unreal_tests", 14,
    "unreal_2d_frames", 300, "unreal_1d_frames", 450, "one_d_native_frames", 300,
    "source_physics_negative_frames", 900, "external_physics_negative_frames", 900,
    "images", 15, "attachments", 71, "attachment_max_error_cm", 0, "gallery_actual_callbacks", 4,
    "gallery_sha256", digest(ui), "image_sha256", G.sorted_object(expected, function(n) return digest(SHOTS .. "/" .. n) end),
    "source_sha256", G.digests(ROOT, sources),
    "log_sha256", G.digests(LOGS, logs),
    "limits", "Explicit Cartesian (type3) state in one Override layer at weight1 over authored default base at same elapsed seconds. Not automatic transitions/CrossFade, simultaneous multiple active layers, additive, directional 2D, IK or mirror. Native comparison before helpers/physics, not pixel equivalence; chin deferred.")
  G.write_json(LOGS .. "/Animator290Audit.json", report)
  print("PASS: five cards; 720 native frames, 194400 animated TRS samples; 14 UE tests; 15 RHI images; 71 attachments; four actual gallery callbacks")
end

if ok.is_main() then ok.main(main) end
