-- Stage291: actual float -> per-frame Session -> UE displayed skeleton gates.
local ok = require "ok"
local G = require "gpp"
local VA = require "verify_animator"
local json, util, path = ok.json, ok.util, ok.path
local O, A, F, I, check, findall = G.O, G.A, G.F, G.I, G.check, G.findall
local CARDS, TESTS, digest, read = VA.CARDS, VA.TESTS, VA.digest, VA.read

local ROOT = G.ROOT
local LOGS = ROOT .. "/Saved/Logs"
local DATA = ROOT .. "/Saved/Tests/Animator291"

local function concat(a, b) local out = { table.unpack(a) }; for _, v in ipairs(b) do out[#out + 1] = v end; return out end

local function main()
  local before = read(LOGS .. "/AnimatorSession291BeforeFix.log")
  check(G.has(before, "session dropped animation parameters") and G.has(before, "test result: FAILED"))
  local accepted = read(LOGS .. "/AnimatorSession291Accepted.log")
  check(G.has(accepted, "1 passed; 0 failed"))
  local rows = findall([[AnimatorSession card=(.+?) frames=204 changed=(\d+) bone_max=([\d.]+) attachment_max_m=([\d.]+)]], accepted)
  check(#rows == 5 and G.set_eq(G.map(rows, function(r) return r[1] end), CARDS))
  check(G.all(rows, function(r) return I(r[2]) > 100 and math.max(F(r[3]), F(r[4])) < 2e-5 end))
  local total = G.search([[AnimatorSession accepted cards=5 frames=1020 bones=(\d+) attachment_vertices=(\d+) positive_frames=(\d+)]], accepted)
  check(total and math.min(I(total[1]), I(total[2]), I(total[3])) > 0)
  local priority = read(LOGS .. "/AnimatorCartesianPublisher291Diagnostic.log")
  check(G.has(priority, "1 passed; 0 failed"))
  local reference = G.search([[CartesianPublisher verified physics_reference_priority=1 isolated_delta=([\d.]+) physics_restored=1]], priority)
  check(reference and F(reference[1]) > 1e-4)
  check(G.has(read(LOGS .. "/AnimatorUnrealBuild291.log"), "Result: Succeeded"))
  check(G.has(read(LOGS .. "/AnimatorUnrealBuild291TestsAccepted.log"), "Result: Succeeded"))
  check(G.has(read(LOGS .. "/AnimatorUnrealBuild291GalleryAccepted.log"), "Result: Succeeded"))
  local tests = read(LOGS .. "/AnimatorUnrealAutomation291Console.log")
  check(not G.has(tests, "Result={Fail}") and G.has(tests, "Automation Test Queue Empty 15 tests performed"))
  check(G.has(tests, "RequestExitWithStatus(1, 0,"))
  check(G.set_eq(findall([[Test Completed\. Result=\{Success\} Name=\{([^}]+)\}]], tests), concat(TESTS, { "Animator2DLayers", "AnimatorParameterPropagation" })))
  local positive = findall([[AnimatorParameters card=(.+?) parameter=(\w+) changed_bones=(\d+) restored=1]], tests)
  local want = {}
  for _, c in ipairs(CARDS) do for _, pp in ipairs({ "Speed", "siriL_X", "muneR_X" }) do want[#want + 1] = { c, pp } end end
  check(#positive == 15 and G.set_eq(G.map(positive, function(r) return { r[1], r[2] } end), want))
  check(G.all(positive, function(r) return I(r[3]) > 0 end))
  for _, spec in ipairs({ { "Animator1D", 3, 90 }, { "Animator2D", 2, 60 } }) do
    local evaluated = findall(spec[1] .. " card=(.+?) states=" .. spec[2] .. " frames=" .. spec[3] .. " finite=1", tests)
    check(#evaluated == 5 and G.set_eq(evaluated, CARDS))
  end
  for _, label in ipairs({ "BodyDynamics", "ExternalBody" }) do
    local controls = findall(label .. [[ card=(.+?) frames=180 .*?changed=(\d+) max_delta_cm=([\d.]+) max_rotation_rad=([\d.]+)]], tests)
    check(#controls == 5 and G.set_eq(G.map(controls, function(c) return c[1] end), CARDS))
    check(G.all(controls, function(c) return I(c[2]) > 0 and F(c[3]) > 1e-3 and F(c[4]) > 1e-4 end))
  end
  check(G.has(tests, "preserved_missing=1 coordinate_roundtrips=2 invalid_clip_rejected=1"))
  local galleries, captures = json.object(), json.object()
  path.mkdir(DATA, true)
  local expected = {}
  for _, c in ipairs(CARDS) do for _, v in ipairs({ "3d", "face", "face_side" }) do expected[#expected + 1] = c .. "_outfit0_" .. v .. ".png" end end
  for _, mf in ipairs({ { "Layer", { "siriL_X=0.31", "siriL_Y=0.79" } }, { "OneD", { "Speed=0.37" } } }) do
    local mode, floats = mf[1], mf[2]
    local capture_path = LOGS .. "/AnimatorShots291_" .. mode .. "_open.log"
    local capture = read(capture_path)
    local shots = ROOT .. "/Saved/DebugShots/Animator291_" .. mode .. "_open"
    check(G.has(capture:gsub("\\", "/"), shots))
    check(G.has(capture, "RequestExitWithStatus(0, 0,") and not G.has(capture, "LogTemp: Error:"))
    check(not G.has(capture, "Failed to compile Material") and G.count(capture, "Tracing Screenshot") == 15)
    for _, value in ipairs(floats) do
      check(G.count(capture, "OK shot animation float " .. value) == 5)
    end
    local clip = mode == "Layer" and "states:studio/anime/00.unity3d|khs3_f_base3 / 17:Te_siri_D_momu_L.Te_siri_D_momu_L" or "states:action/animator/00.unity3d|base / Base Layer.Main.Locomotion_Anger"
    check(G.count(capture, "clip=" .. clip) == 5 and G.count(capture, "OK game animation scene mode=4") == 5)
    local attachments = G.floats(findall([[max_vertex_error_cm=([\d.]+)]], capture))
    check(#attachments == 71 and G.max(attachments) == 0)
    local fits = findall([[capture fit view=\w+ .*?ndc_x=([\d.]+) ndc_y=([\d.]+) limit=0.92]], capture)
    check(#fits == 15 and G.all(fits, function(f) return math.max(F(f[1]), F(f[2])) <= .920001 end))
    check(G.count(capture, "OK gaze display audit passed=1") == 5)
    check(G.count(capture, "OK eye highlight display audit passed=1") == 5)
    check(G.set_eq(G.glob_names(shots, "*.png"), expected))
    local sheet = G.sheet(900, 1920, { 0x30, 0x30, 0x30 })
    for row, card in ipairs(CARDS) do
      local r = row - 1
      sheet:text(8, r * 384 + 6, card .. " / " .. mode .. ": actual Session floats")
      local neutral = { G.load_rgb(shots .. "/" .. card .. "_outfit0_3d.png"):get(0, 0) }
      check(120 <= math.min(table.unpack(neutral)) and math.max(table.unpack(neutral)) <= 135)
      for col, view in ipairs({ "3d", "face", "face_side" }) do
        local file = shots .. "/" .. card .. "_outfit0_" .. view .. ".png"
        check(G.size_is(file, 900, 1080))
        if view == "face_side" then
          check(G.top_row_neutral(G.load_rgb(file), neutral))
        end
        sheet:paste(file, (col - 1) * 300, r * 384 + 24, 300, 360)
      end
    end
    sheet:save(DATA .. "/ContactSheet" .. mode .. ".png")
    local gallery_path = LOGS .. "/AnimatorGallery291_" .. mode .. ".log"
    local gallery = read(gallery_path)
    check(G.has(gallery, "RequestExitWithStatus(0, 0,") and not G.has(gallery, "LogTemp: Error:"))
    local response = G.search([[KK (?:Cartesian|animator) gallery verified .*?display_changed_bones=(\d+)]], gallery)
    check(response and I(response[1]) > 0)
    check(G.has(gallery, "phase_preserved=1 face_clock_unchanged=1"))
    if mode == "Layer" then
      check(G.has(gallery, "physics_isolated=1 physics_restored=1"))
      local changes = findall([[KK Cartesian slider response parameter=(\w+) world_changed=(\d+) local_changed=(\d+)]], gallery)
      check(#changes == 2 and G.set_eq(G.map(changes, function(c) return c[1] end), { "siriL_X", "siriL_Y" }))
      check(G.all(changes, function(c) return I(c[2]) > 0 and I(c[3]) > 0 end))
    end
    local ui = ROOT .. "/Saved/DebugShots/Animator291UI_" .. mode .. "/gallery_imported_studio.png"
    check(G.size_is(ui, 1440, 1000))
    galleries[mode] = O("changed_display_bones", I(response[1]), "image_sha256", digest(ui), "log_sha256", digest(gallery_path))
    captures[mode] = O("images", 15, "attachments", 71, "max_vertex_error_cm", 0, "log_sha256", digest(capture_path),
      "image_sha256", G.sorted_object(expected, function(n) return digest(shots .. "/" .. n) end))
  end
  local sources = { "Tools/ok-extract/src/session.rs", "Tools/ok-extract/src/session_animator_tests.rs",
    "Tools/ok-extract/src/anim.rs", "Tools/ok-extract/src/anim/controller.rs",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/KKRustExtract.cpp",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Public/KKRustExtract.h",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/Tests/KKAnimatorBlend.spec.cpp",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/Tests/KKAnimatorCartesian.spec.cpp",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/Tests/KKAnimatorParameters.spec.cpp",
    "Source/OpenKoikatsu/OKGalleryWidget.cpp", "old_ps1/Tools/GamePoseProbe/run_animator_session.ps1",
    "Tools/GamePoseProbe/verify_animator_session.lua", "Docs/UNREAL_ANIMATOR_RUNTIME.md",
    "Docs/ERROR_LOG.md", "HANDOFF2.md", "GPUSkin.md" }
  local native = json.object()
  for _, ss in ipairs({ { 289, "animator-blend" }, { 290, "animator-2d" } }) do
    local stage, suffix = ss[1], ss[2]
    for _, card in ipairs(CARDS) do
      local stem = G.sanitize(card) .. "Animator" .. stage .. ".json." .. suffix .. ".json"
      native[stage .. "/" .. card] = digest(ROOT .. "/Saved/Tests/Animator" .. stage .. "/" .. stem)
    end
  end
  local report = O("stage", 291, "cards", 5, "full_session_frames", 1020, "full_body_transforms", I(total[1]),
    "attachment_vertices", I(total[2]), "nondefault_raw_positive_frames", I(total[3]), "session_rows", G.rows(rows),
    "cartesian_physics_reference_control", O("isolated_delta", F(reference[1]), "sha256", digest(LOGS .. "/AnimatorCartesianPublisher291Diagnostic.log")),
    "unreal_tests", 15, "unreal_parameter_positive_controls", G.rows(positive), "galleries", galleries, "captures", captures,
    "native_fixture_sha256", native, "source_sha256", G.digests(ROOT, sources),
    "log_sha256", G.digests(LOGS, { "AnimatorSession291BeforeFix.log", "AnimatorSession291Accepted.log",
      "AnimatorUnrealBuild291.log", "AnimatorUnrealBuild291TestsAccepted.log", "AnimatorUnrealBuild291GalleryAccepted.log", "AnimatorUnrealAutomation291Console.log" }),
    "limits", "Stage289/290 native-validated raw evaluator is the reference for full Session plumbing; external entry supplies its locals independently. Full Session and RHI include physics. Cartesian Gallery positive control isolates physics-owned channels and restores physics before capture. Does not claim pixel equivalence or completed transitions, multiple weighted layers, HAnime physics-enable policy, additive, IK or mirror. Chin deferred.")
  G.write_json(LOGS .. "/Animator291Audit.json", report)
  print("PASS: five cards; 1020 full Session frames; " .. total[1] .. " body transforms; " .. total[2] .. " attachment vertices; 15 UE tests; 30 RHI images; both actual Gallery skeleton responses")
end

if ok.is_main() then ok.main(main) end
