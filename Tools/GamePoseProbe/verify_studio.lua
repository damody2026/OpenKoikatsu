-- Stage293: source LoadAnime, composed graph, Session and imported UE row.
local ok = require "ok"
local G = require "gpp"
local VA = require "verify_animator"
local json, util, path = ok.json, ok.util, ok.path
local O, A, F, I, check, findall = G.O, G.A, G.F, G.I, G.check, G.findall
local CARDS, TESTS, digest, read = VA.CARDS, VA.TESTS, VA.digest, VA.read

local ROOT = G.ROOT
local LOGS = ROOT .. "/Saved/Logs"
local DATA = ROOT .. "/Saved/Tests/Studio293"

local function concat(a, b) local out = { table.unpack(a) }; for _, v in ipairs(b) do out[#out + 1] = v end; return out end

local function main()
  local native = json.object()
  for _, card in ipairs(CARDS) do
    local stem = G.sanitize(card) .. "Studio293.json"
    local completion, fixture = DATA .. "/" .. stem, DATA .. "/" .. stem .. ".studio.json"
    check(not util.truthy(json.decode(read(completion))["error"]))
    local frames = json.decode(read(fixture))
    check(#frames == 27)
    local want = {}
    for _, category in ipairs({ 3, 128, 183 }) do
      for _, hbm in ipairs({ { 0, 0, 0 }, { .37, .61, .43 }, { .81, .19, .77 } }) do
        for _, t in ipairs({ 0, .23, .71 }) do want[#want + 1] = { category, hbm[1], hbm[2], hbm[3], t } end
      end
    end
    check(G.set_eq(G.map(frames, function(f) return { f.category, f.height, f.breast, f.motion, f.elapsed } end), want))
    check(G.all(frames, function(f) return util.truthy(f.bones) and util.truthy(f.clips) and util.truthy(f.parameters) and f.length > 0 end))
    local layers = {}
    for _, f in ipairs(frames) do layers[f.category] = f.layer end
    check(util.equal(layers, { [3] = 0, [128] = 79, [183] = 73 }))
    native[card] = O("completion", digest(completion), "fixture", digest(fixture))
  end
  local raw = read(LOGS .. "/Studio293NativeRustAll.log")
  check(G.has(raw, "1 passed; 0 failed"))
  local rows = findall([=[Studio native card=(.+?) frames=27 errors=\[([^]]+)\]]=], raw)
  check(#rows == 5 and G.set_eq(G.map(rows, function(r) return r[1] end), CARDS))
  check(G.all(rows, function(r) return G.max(util.split(r[2], ","), F) < 2e-5 end))
  check(G.has(raw, "cards=5 frames=135 transform_attributes=36450 override_negative=45"))
  check(G.has(read(LOGS .. "/Studio293NativeRustFinalMarch.log"), "1 passed; 0 failed"))
  check(G.has(read(LOGS .. "/Studio293SynchronizedBoundaryFinal.log"), "1 passed; 0 failed"))
  local identity = path.parent(DATA) .. "/Studio293Identity/_Card__March_7th_keiyuuStudio293.json.sampler.tsv"
  check(G.search([[clip-identity\t\d+\t\d+\tS_Stop_Idle\tFalse]], read(identity)))
  local session = read(LOGS .. "/Studio293Session.log")
  check(G.has(session, "1 passed; 0 failed"))
  local session_rows = findall([[Studio Session card=(.+?) frames=27 bone_max=([\deE.+-]+) attachment_max=([\deE.+-]+)]], session)
  check(#session_rows == 5 and G.set_eq(G.map(session_rows, function(r) return r[1] end), CARDS))
  check(G.all(session_rows, function(r) return math.max(F(r[2]), F(r[3])) < 2e-5 end))
  check(G.has(session, "cards=5 frames=135 bones=80865 attachment_vertices=2929257 invalid_atomic=405"))
  check(G.has(read(LOGS .. "/Studio293UnrealBuildFinal.log"), "Result: Succeeded"))
  local tests_path = LOGS .. "/StudioUnrealAutomation293Console.log"
  local tests = read(tests_path)
  check(not G.has(tests, "Result={Fail}") and G.has(tests, "Automation Test Queue Empty 17 tests performed"))
  check(G.has(tests, "RequestExitWithStatus(1, 0,"))
  check(G.set_eq(findall([[Test Completed\. Result=\{Success\} Name=\{([^}]+)\}]], tests), concat(TESTS, {
    "Animator2DLayers", "AnimatorParameterPropagation", "AnimatorStateClock", "StudioSourceInitialization" })))
  local controls = findall([[StudioSource card=(.+?) category=(\d+) layer=(\d+) frames=9 changed_bones=(\d+) invalid_atomic=3]], tests)
  local want_controls = {}
  for _, c in ipairs(CARDS) do for _, cat in ipairs({ 3, 128, 183 }) do want_controls[#want_controls + 1] = { c, cat } end end
  check(#controls == 15 and G.set_eq(G.map(controls, function(c) return { c[1], I(c[2]) } end), want_controls))
  check(G.all(controls, function(c) return c[2] == "3" or I(c[4]) > 0 end))
  for _, label in ipairs({ "BodyDynamics", "ExternalBody" }) do
    local physics = findall(label .. [[ card=(.+?) frames=180 .*?changed=(\d+) max_delta_cm=([\d.]+) max_rotation_rad=([\d.]+)]], tests)
    check(#physics == 5 and G.set_eq(G.map(physics, function(c) return c[1] end), CARDS))
    check(G.all(physics, function(c) return I(c[2]) > 0 and F(c[3]) > 1e-3 and F(c[4]) > 1e-4 end))
  end
  local capture_path = LOGS .. "/StudioShots293_OneD_open.log"
  local capture = read(capture_path)
  check(G.has(capture, "RequestExitWithStatus(0, 0,") and not G.has(capture, "LogTemp: Error:"))
  check(not G.has(capture, "Failed to compile Material") and G.count(capture, "Tracing Screenshot") == 15)
  check(G.count(capture, "OK Studio animation row=r00000022") == 5)
  local attachments = G.floats(findall([[max_vertex_error_cm=([\d.]+)]], capture))
  check(#attachments == 71 and G.max(attachments) == 0)
  local fits = findall([[capture fit view=\w+ .*?ndc_x=([\d.]+) ndc_y=([\d.]+) limit=0.92]], capture)
  check(#fits == 15 and G.all(fits, function(f) return math.max(F(f[1]), F(f[2])) <= .920001 end))
  check(G.count(capture, "OK gaze display audit passed=1") == 5 and G.count(capture, "OK eye highlight display audit passed=1") == 5)
  local shots = ROOT .. "/Saved/DebugShots/Studio293_OneD_open"
  local expected = {}
  for _, c in ipairs(CARDS) do for _, v in ipairs({ "3d", "face", "face_side" }) do expected[#expected + 1] = c .. "_outfit0_" .. v .. ".png" end end
  check(G.set_eq(G.glob_names(shots, "*.png"), expected))
  local sheet = G.sheet(900, 1920, { 0x30, 0x30, 0x30 })
  for row, card in ipairs(CARDS) do
    local r = row - 1
    sheet:text(8, r * 384 + 6, card .. " / imported Studio walk, elapsed=.4")
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
  local gallery_path = LOGS .. "/StudioGallery293_OneD.log"
  local gallery = read(gallery_path)
  check(G.has(gallery, "RequestExitWithStatus(0, 0,") and not G.has(gallery, "LogTemp: Error:"))
  check(G.has(gallery, "OK Studio animation row=r00000022") and G.has(gallery, "KK catalog gallery sources="))
  check(G.has(gallery, "KK catalog gallery captured after shader completion"))
  local ui = ROOT .. "/Saved/DebugShots/Studio293UI_OneD/gallery_imported_studio.png"
  check(G.size_is(ui, 1440, 1000))
  local sources = { "Tools/ok-extract/src/anim.rs", "Tools/ok-extract/src/anim/controller.rs",
    "Tools/ok-extract/src/anim/studio_tests.rs", "Tools/ok-extract/src/ffi.rs",
    "Tools/ok-extract/src/session_animator_tests.rs", "Source/OpenKoikatsu/OKGalleryWidget.cpp",
    "Source/OpenKoikatsu/OpenKoikatsu.Build.cs",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/KKAnimationCatalog.cpp",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/KKPreviewActor.cpp",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/Tests/KKStudioProfile.spec.cpp",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/Tests/KKAnimationCatalog.spec.cpp",
    "Tools/GamePoseProbe/Probe.cs", "old_ps1/Tools/GamePoseProbe/run_studio_native.ps1",
    "old_ps1/Tools/GamePoseProbe/run_studio.ps1", "Tools/GamePoseProbe/verify_studio.lua",
    "Docs/UNREAL_STUDIO_INITIALIZATION.md", "Docs/ERROR_LOG.md", "HANDOFF2.md" }
  local logs = { "Studio293NativeRustAll.log", "Studio293NativeRustFinalMarch.log",
    "Studio293SynchronizedBoundaryFinal.log", "Studio293Session.log", "Studio293UnrealBuildFinal.log",
    "StudioUnrealAutomation293Console.log", "StudioShots293_OneD_open.log", "StudioGallery293_OneD.log" }
  local report = O("stage", 293, "cards", 5, "native_frames", 135, "animated_trs_attributes", 36450, "override_negative", 45,
    "raw_rows", G.rows(rows), "session_frames", 135, "body_transforms", 80865, "attachment_vertices", 2929257,
    "invalid_atomic", 405, "session_rows", G.rows(session_rows), "unreal_tests", 17, "studio_controls", G.rows(controls),
    "captures", O("images", 15, "attachments", 71, "max_vertex_error_cm", 0, "sha256", G.sorted_object(expected, function(n) return digest(shots .. "/" .. n) end)),
    "gallery", O("image", digest(ui), "log", digest(gallery_path)), "native_sha256", native, "identity", digest(identity),
    "source_sha256", G.digests(ROOT, sources), "log_sha256", G.digests(LOGS, logs),
    "limits", "Source graph/options/synchronized motion composition. Raw controls isolate physics; existing Session/RHI physics retained. Yure enable/shape initialization and full Studio scene pending. Chin deferred.")
  G.write_json(LOGS .. "/Studio293Audit.json", report)
  print("PASS: 5 cards; 135 native frames / 36450 TRS attributes; 135 Session frames / 80865 body transforms / 2929257 accessory vertices; 405 atomic controls; 17 UE tests; 15 RHI images; imported Studio Gallery callback")
end

if ok.is_main() then ok.main(main) end
