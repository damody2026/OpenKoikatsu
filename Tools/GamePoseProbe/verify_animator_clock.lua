-- Stage292: native elapsed-time clocks through Session and actual UE display.
local ok = require "ok"
local G = require "gpp"
local VA = require "verify_animator"
local json, util, path = ok.json, ok.util, ok.path
local O, A, F, I, check, findall = G.O, G.A, G.F, G.I, G.check, G.findall
local CARDS, TESTS, digest, read = VA.CARDS, VA.TESTS, VA.digest, VA.read

local ROOT = G.ROOT
local LOGS = ROOT .. "/Saved/Logs"
local DATA = ROOT .. "/Saved/Tests/Animator292"

local function concat(a, b) local out = { table.unpack(a) }; for _, v in ipairs(b) do out[#out + 1] = v end; return out end

local function main()
  local native = json.object()
  for _, card in ipairs(CARDS) do
    local stem = G.sanitize(card) .. "Animator292.json"
    local completion = DATA .. "/" .. stem
    local fixture = DATA .. "/" .. stem .. ".animator-clock.json"
    check(not util.truthy(json.decode(read(completion))["error"]))
    local frames = json.decode(read(fixture))
    check(#frames == 24)
    local want = {}
    for _, state in ipairs({ "Base Layer.Main.Idle", "Base Layer.Main.Locomotion_Anger" }) do
      for _, value in ipairs({ .4, 1, 1.6 }) do
        for _, time in ipairs({ 0, .17, .43, .91 }) do want[#want + 1] = { state, value, time } end
      end
    end
    check(G.set_eq(G.map(frames, function(f) return { f.state, f.value, f.elapsed } end), want))
    check(G.all(frames, function(f) return util.truthy(f.bones) and util.truthy(f.clips) and math.abs(f.value - f.speedMultiplier) < 1e-6 end))
    native[card] = O("completion", digest(completion), "fixture", digest(fixture))
    local layer_completion = DATA .. "/" .. util.replace(stem, "Animator292.json", "Animator292Layer.json")
    local layer_fixture = DATA .. "/" .. path.name(layer_completion) .. ".animator-clock.json"
    check(not util.truthy(json.decode(read(layer_completion))["error"]))
    local layer_frames = json.decode(read(layer_fixture))
    check(#layer_frames == 12)
    local want_layer = {}
    for _, value in ipairs({ .2, .5, .8 }) do
      for _, time in ipairs({ 0, .17, .43, .91 }) do
        want_layer[#want_layer + 1] = { "khs3_f_base3", 5, "05:Te_mune_D_momu_L.Te_mune_D_momu_L", value, time }
      end
    end
    check(G.set_eq(G.map(layer_frames, function(f) return { f.controller, f.layer, f.state, f.value, f.elapsed } end), want_layer))
    check(G.all(layer_frames, function(f) return f.parameter == "speedHand" and math.abs(f.value - f.speedMultiplier) < 1e-6 end))
    native[card].layer_completion = digest(layer_completion)
    native[card].layer_fixture = digest(layer_fixture)
  end
  local raw = read(LOGS .. "/AnimatorClock292Raw.log")
  check(G.has(raw, "1 passed; 0 failed"))
  local rows = findall([=[AnimatorClock native card=(.+?) frames=36 positive=(\d+) errors=\[([^]]+)\]]=], raw)
  check(#rows == 5 and G.set_eq(G.map(rows, function(r) return r[1] end), CARDS))
  check(G.all(rows, function(r) return I(r[2]) >= 18 and G.max(util.split(r[3], ","), F) < 2e-5 end))
  local summary = G.search([[AnimatorClock native accepted cards=5 frames=180 transform_attributes=(\d+) positive=(\d+)]], raw)
  check(summary and I(summary[1]) > 10000 and I(summary[2]) >= 90)
  local session = read(LOGS .. "/AnimatorClock292Session.log")
  check(G.has(session, "1 passed; 0 failed"))
  local session_rows = findall([[AnimatorClock Session card=(.+?) frames=36 bone_max=([\deE.+-]+) attachment_max=([\deE.+-]+) invalid_atomic=180]], session)
  check(#session_rows == 5 and G.set_eq(G.map(session_rows, function(r) return r[1] end), CARDS))
  check(G.all(session_rows, function(r) return math.max(F(r[2]), F(r[3])) < 2e-5 end))
  local total = G.search([[AnimatorClock Session accepted cards=5 frames=180 bones=(\d+) attachment_vertices=(\d+) invalid_atomic=900]], session)
  check(total and I(total[1]) > 0 and I(total[2]) > 0)
  check(G.has(read(LOGS .. "/AnimatorClock292CatalogRegression.log"), "5 passed; 0 failed"))
  check(G.has(read(LOGS .. "/AnimatorClock292RawApiRegression.log"), "1 passed; 0 failed"))
  check(G.has(read(LOGS .. "/AnimatorClock292OneDRegression.log"), "Animator1D accepted cards=5 frames=300 transform_samples=81000"))
  check(G.has(read(LOGS .. "/AnimatorClock292CartesianRegression.log"), "Animator2D accepted cards=5 frames=720 transform_samples=194400"))
  local schema_dir = ROOT .. "/Saved/Tests/Animator292Schema"
  local schema_completion = schema_dir .. "/_Card__March_7th_keiyuuAnimator292.json"
  local schema_fixture = schema_dir .. "/" .. path.name(schema_completion) .. ".animator-clock.json"
  check(not util.truthy(json.decode(read(schema_completion))["error"]))
  local schema = json.decode(read(schema_fixture))
  check(#schema == 24 and G.all(schema, function(f) return f.controller == "base" and f.layer == 0 and f.baseState == "" end))
  check(G.has(read(LOGS .. "/AnimatorClock292UnrealBuildFinal.log"), "Result: Succeeded"))
  local tests_path = LOGS .. "/AnimatorUnrealAutomation292Console.log"
  local tests = read(tests_path)
  check(not G.has(tests, "Result={Fail}") and G.has(tests, "Automation Test Queue Empty 16 tests performed"))
  check(G.has(tests, "RequestExitWithStatus(1, 0,"))
  check(G.set_eq(findall([[Test Completed\. Result=\{Success\} Name=\{([^}]+)\}]], tests), concat(TESTS, { "Animator2DLayers", "AnimatorParameterPropagation", "AnimatorStateClock" })))
  local controls = findall([[AnimatorClock card=(.+?) state=(.+?) frames=12 changed_bones=(\d+) restored=1 invalid_atomic=5]], tests)
  local want_controls = {}
  for _, c in ipairs(CARDS) do
    for _, s in ipairs({ "Base Layer.Main.Idle", "Base Layer.Main.Locomotion_Anger", "05:Te_mune_D_momu_L.Te_mune_D_momu_L" }) do want_controls[#want_controls + 1] = { c, s } end
  end
  check(#controls == 15 and G.set_eq(G.map(controls, function(c) return { c[1], c[2] } end), want_controls))
  check(G.all(controls, function(c) return I(c[3]) > 0 end))
  check(#findall([[AnimatorParameters card=]], tests) == 15)
  for _, label in ipairs({ "BodyDynamics", "ExternalBody" }) do
    local controls_physics = findall(label .. [[ card=(.+?) frames=180 .*?changed=(\d+) max_delta_cm=([\d.]+) max_rotation_rad=([\d.]+)]], tests)
    check(#controls_physics == 5 and G.set_eq(G.map(controls_physics, function(c) return c[1] end), CARDS))
    check(G.all(controls_physics, function(c) return I(c[2]) > 0 and F(c[3]) > 1e-3 and F(c[4]) > 1e-4 end))
  end
  local capture_path = LOGS .. "/AnimatorShots292_OneD_open.log"
  local capture = read(capture_path)
  check(G.has(capture, "RequestExitWithStatus(0, 0,") and not G.has(capture, "LogTemp: Error:"))
  check(not G.has(capture, "Failed to compile Material") and G.count(capture, "Tracing Screenshot") == 15)
  for _, value in ipairs({ "Speed=0.37", "MotionSpeed=1.6" }) do
    check(G.count(capture, "OK shot animation float " .. value) == 5)
  end
  check(G.count(capture, "clip=states:action/animator/00.unity3d|base / Base Layer.Main.Locomotion_Anger") == 5)
  local attachments = G.floats(findall([[max_vertex_error_cm=([\d.]+)]], capture))
  check(#attachments == 71 and G.max(attachments) == 0)
  local fits = findall([[capture fit view=\w+ .*?ndc_x=([\d.]+) ndc_y=([\d.]+) limit=0.92]], capture)
  check(#fits == 15 and G.all(fits, function(f) return math.max(F(f[1]), F(f[2])) <= .920001 end))
  check(G.count(capture, "OK gaze display audit passed=1") == 5 and G.count(capture, "OK eye highlight display audit passed=1") == 5)
  local shots = ROOT .. "/Saved/DebugShots/Animator292_OneD_open"
  local expected = {}
  for _, c in ipairs(CARDS) do for _, v in ipairs({ "3d", "face", "face_side" }) do expected[#expected + 1] = c .. "_outfit0_" .. v .. ".png" end end
  check(G.set_eq(G.glob_names(shots, "*.png"), expected))
  local sheet = G.sheet(900, 1920, { 0x30, 0x30, 0x30 })
  for row, card in ipairs(CARDS) do
    local r = row - 1
    sheet:text(8, r * 384 + 6, card .. " / MotionSpeed=1.6, Speed=.37, elapsed=.4")
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
  sheet:save(DATA .. "/ContactSheet.png")
  local gallery_path = LOGS .. "/AnimatorGallery292_OneD.log"
  local gallery = read(gallery_path)
  check(G.has(gallery, "RequestExitWithStatus(0, 0,") and not G.has(gallery, "LogTemp: Error:"))
  local blend = G.search([[KK animator gallery verified .*?display_changed_bones=(\d+)]], gallery)
  local clock = G.search([[KK animator clock gallery verified multiplier=1.6 phase_preserved=1 face_clock_unchanged=1 immediate_pose_unchanged=1 duration_ratio=0.625 advanced_display_bones=(\d+)]], gallery)
  check(blend and clock and math.min(I(blend[1]), I(clock[1])) > 0)
  local ui = ROOT .. "/Saved/DebugShots/Animator292UI_OneD/gallery_imported_studio.png"
  check(G.size_is(ui, 1440, 1000))
  local sources = { "Tools/ok-extract/src/anim.rs", "Tools/ok-extract/src/anim/controller.rs",
    "Tools/ok-extract/src/anim/clock_tests.rs", "Tools/ok-extract/src/session.rs",
    "Tools/ok-extract/src/session_animator_tests.rs", "Source/OpenKoikatsu/OKGalleryWidget.cpp",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/Tests/KKAnimatorClock.spec.cpp",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/Tests/KKAnimatorBlend.spec.cpp",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/Tests/KKAnimatorCartesian.spec.cpp",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/KKPreviewActor.cpp",
    "Tools/GamePoseProbe/Probe.cs", "old_ps1/Tools/GamePoseProbe/run_animator_clock_native.ps1",
    "old_ps1/Tools/GamePoseProbe/run_animator_clock.ps1", "Tools/GamePoseProbe/verify_animator_clock.lua",
    "Docs/UNREAL_ANIMATOR_CLOCK.md", "Docs/ERROR_LOG.md", "HANDOFF2.md" }
  local report = O("stage", 292, "cards", 5, "native_frames", 180, "animated_trs_attributes", I(summary[1]),
    "raw_positive_frames", I(summary[2]), "raw_rows", G.rows(rows), "full_session_frames", 180,
    "body_transforms", I(total[1]), "attachment_vertices", I(total[2]), "invalid_atomic_controls", 900,
    "session_rows", G.rows(session_rows), "unreal_tests", 16, "unreal_clock_controls", G.rows(controls),
    "gallery", O("blend_changed", I(blend[1]), "clock_advanced_changed", I(clock[1]), "image", digest(ui), "log", digest(gallery_path)),
    "native_schema", O("completion", digest(schema_completion), "fixture", digest(schema_fixture)),
    "captures", O("images", 15, "attachments", 71, "max_vertex_error_cm", 0, "log", digest(capture_path),
      "image_sha256", G.sorted_object(expected, function(n) return digest(shots .. "/" .. n) end)), "native_sha256", native,
    "source_sha256", G.digests(ROOT, sources),
    "log_sha256", G.digests(LOGS, { "AnimatorClock292Raw.log", "AnimatorClock292Session.log",
      "AnimatorClock292CatalogRegression.log", "AnimatorClock292RawApiRegression.log",
      "AnimatorClock292OneDRegression.log", "AnimatorClock292CartesianRegression.log",
      "AnimatorClock292UnrealBuildFinal.log", "AnimatorUnrealAutomation292Console.log" }),
    "limits", "Positive state multipliers; native raw control isolates physics; full Session and RHI retain physics. No reverse/pause speed parameter, automatic transitions, CrossFade, simultaneous weighted layers/additive, full HAnime layer/physics initialization or pixel equivalence. Chin deferred.")
  G.write_json(LOGS .. "/Animator292Audit.json", report)
  print("PASS: 5 cards; 180 native frames / " .. summary[1] .. " animated TRS attributes; 180 full Session frames / " .. total[1] .. " body transforms / " .. total[2] .. " accessory vertices; 900 atomic controls; 16 UE tests; 15 RHI images; actual Gallery clock callback")
end

if ok.is_main() then ok.main(main) end
