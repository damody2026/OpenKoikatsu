-- Stage294 source Yure policy, shape and dynamic enable lifecycle acceptance.
local ok = require "ok"
local G = require "gpp"
local VA = require "verify_animator"
local json, util = ok.json, ok.util
local O, A, F, I, check, findall = G.O, G.A, G.F, G.I, G.check, G.findall
local CARDS, TESTS, digest, read = VA.CARDS, VA.TESTS, VA.digest, VA.read

local ROOT = G.ROOT
local LOGS = ROOT .. "/Saved/Logs"
local DATA = ROOT .. "/Saved/Tests/Studio294"

local function concat(a, b) local out = { table.unpack(a) }; for _, v in ipairs(b) do out[#out + 1] = v end; return out end
local function rep(v, n) local out = {}; for i = 1, n do out[i] = v end; return out end
local function near(im, x, y, neutral) local r, g, b = im:get(x, y); return G.close_rgb(r, g, b, neutral) end

local function main()
  local native = json.object()
  for _, card in ipairs(CARDS) do
    local stem = G.sanitize(card) .. "Studio294.json"
    local completion = DATA .. "/" .. stem
    check(not util.truthy(json.decode(read(completion))["error"]))
    local frames = json.decode(read(DATA .. "/" .. stem .. ".yure.json"))
    check(#frames == 9)
    check(util.equal(G.map(frames, function(f) return f.category end), { 3, 128, 3, 183, 3, 128, 3, 183, 3 }))
    local masked_shape = concat(concat(rep(false, 6), rep(true, 3)), concat(rep(false, 6), rep(true, 3)))
    for i, f in ipairs(frames) do
      local idx = i - 1
      local masked = idx == 1 or idx == 2 or idx == 5 or idx == 6
      check(util.equal(f.active, masked and { false, false, true, true } or rep(true, 4)))
      check(util.equal(f.shapeEnabled, masked and masked_shape or rep(true, 18)))
      check(util.truthy(f.shapeBones))
    end
    local trace = json.decode(read(DATA .. "/" .. stem .. ".yure-physics.json"))
    check(#trace == 240)
    for i, f in ipairs(trace) do
      local idx = i - 1
      check(f.frame == idx and #f.bones == 10)
      check(util.equal(f.active, { not (60 <= idx and idx < 150), not (90 <= idx and idx < 180), not (120 <= idx and idx < 180), true }))
    end
    local digests = json.object()
    for _, s in ipairs({ "", ".yure.json", ".yure-baseline.json", ".yure-baseline.tsv", ".yure-setup.tsv", ".yure-physics.json" }) do digests[s] = digest(DATA .. "/" .. stem .. s) end
    native[card] = digests
  end
  for _, name in ipairs({ "Studio294YureRowsUnit.log", "Studio294EnableUnitFinal.log", "Studio294PhysicsAll.log", "Studio294SessionFinal.log", "Studio294LegacySession.log", "Studio294EyeLookColdFinal.log" }) do
    check(G.has(read(LOGS .. "/" .. name), "1 passed; 0 failed"), name)
  end
  local cold = read(LOGS .. "/Studio294EyeLookColdFinal.log")
  check(G.set_eq(findall([[Yure cold H EyeLook card=(.+?) rates=]], cold), CARDS))
  check(G.count(cold, "steps=60 paused_hold=1") == 5)
  check(G.has(read(LOGS .. "/Studio294EyeLookColdDiagnostic.log"), "EyeLook missing basis at step 0"))
  check(G.has(read(LOGS .. "/Studio294EyeLookMissingHead.log"), "head=false body=true body_head=true"))
  local physics = read(LOGS .. "/Studio294PhysicsAll.log")
  local rows = findall([[Ver02 enable card=(.+?) frames=240 bones=2400 max=([\deE.+-]+)]], physics)
  check(#rows == 5 and G.set_eq(G.map(rows, function(r) return r[1] end), CARDS) and G.all(rows, function(r) return F(r[2]) < 2e-5 end))
  check(G.has(physics, "Ver02 enable accepted frames=1200 bones=12000"))
  local session = read(LOGS .. "/Studio294SessionFinal.log")
  local shape = findall([[Yure Session card=(.+?) loads=9 shape_max=([\deE.+-]+)]], session)
  check(#shape == 5 and G.set_eq(G.map(shape, function(r) return r[1] end), CARDS) and G.all(shape, function(r) return F(r[2]) < 2e-5 end))
  check(G.has(session, "loads=45 shape_attributes=6390 masked_negative=20 invalid_atomic=45"))
  check(G.has(read(LOGS .. "/Studio294LegacySession.log"), "Studio Session accepted cards=5 frames=135 bones=80865 attachment_vertices=2929257 invalid_atomic=405"))
  check(G.has(read(LOGS .. "/Studio294UnrealBuildFinal.log"), "Result: Succeeded"))
  local tests = read(LOGS .. "/StudioUnrealAutomation294Console.log")
  check(not G.has(tests, "Result={Fail}") and G.has(tests, "Automation Test Queue Empty 18 tests performed") and G.has(tests, "RequestExitWithStatus(1, 0,"))
  check(G.set_eq(findall([[Test Completed\. Result=\{Success\} Name=\{([^}]+)\}]], tests), concat(TESTS, { "Animator2DLayers", "AnimatorParameterPropagation", "AnimatorStateClock", "StudioSourceInitialization", "StudioYureInitialization" })))
  local controls = findall([[StudioYure card=(.+?) frames=90 changed=(\d+) max_delta=([\d.]+) retained_normal=1 fallback=1 invalid_atomic=1]], tests)
  check(#controls == 5 and G.set_eq(G.map(controls, function(c) return c[1] end), CARDS) and G.all(controls, function(c) return I(c[2]) > 0 and F(c[3]) > 1e-4 end))
  check(G.count(tests, "invalid_atomic=1 cold_eye_look=1") == 5)
  for _, label in ipairs({ "BodyDynamics", "ExternalBody" }) do
    local rows_physics = findall(label .. [[ card=(.+?) frames=180 .*?changed=(\d+) max_delta_cm=([\d.]+) max_rotation_rad=([\d.]+)]], tests)
    check(#rows_physics == 5 and G.set_eq(G.map(rows_physics, function(c) return c[1] end), CARDS))
    check(G.all(rows_physics, function(c) return I(c[2]) > 0 and F(c[3]) > 1e-3 and F(c[4]) > 1e-4 end))
  end
  local capture = read(LOGS .. "/StudioShots294_OneD_open.log")
  check(not G.has(capture, "LogTemp: Error:") and G.has(capture, "RequestExitWithStatus(0, 0,") and G.count(capture, "Tracing Screenshot") == 15)
  check(G.count(capture, "OK Studio animation row=r00001390") == 5 and G.count(capture, "duration=1.333333 yure=0011") == 5)
  check(G.count(capture, "OK gaze display audit passed=1") == 5 and G.count(capture, "OK eye highlight display audit passed=1") == 5)
  check(G.count(capture, "OK eye look shot states=") == 5)
  -- ShotPreview initializes the first viewer once, then reloads each task.
  check(G.count(capture, "cameras=3 planes=6") == 1 + 2 * #CARDS)
  local attachments = G.floats(findall([[max_vertex_error_cm=([\d.]+)]], capture))
  check(#attachments == 71 and G.max(attachments) == 0)
  local fits = findall([[capture fit view=\w+ .*?ndc_x=([\d.]+) ndc_y=([\d.]+) limit=0.92]], capture)
  check(#fits == 15 and G.all(fits, function(f) return math.max(F(f[1]), F(f[2])) <= .920001 end))
  local shots = ROOT .. "/Saved/DebugShots/Studio294_OneD_open"
  local names = {}
  for _, c in ipairs(CARDS) do for _, v in ipairs({ "3d", "face", "face_side" }) do names[#names + 1] = c .. "_outfit0_" .. v .. ".png" end end
  check(G.set_eq(G.glob_names(shots, "*.png"), names))
  local sheet = G.sheet(900, 1920, { 0x30, 0x30, 0x30 })
  for row, card in ipairs(CARDS) do
    local r = row - 1
    sheet:text(8, r * 384 + 6, card .. " / imported H Stop_Idle, source Yure=0011")
    local neutral = { G.load_rgb(shots .. "/" .. card .. "_outfit0_3d.png"):get(0, 0) }
    check(120 <= math.min(table.unpack(neutral)) and math.max(table.unpack(neutral)) <= 135)
    for col, view in ipairs({ "3d", "face", "face_side" }) do
      local file = shots .. "/" .. card .. "_outfit0_" .. view .. ".png"
      check(G.size_is(file, 900, 1080))
      local im = G.load_rgb(file)
      check(G.top_row_neutral(im, neutral))
      if view == "3d" then check(near(im, 0, 0, neutral) and near(im, 899, 0, neutral) and near(im, 0, 1079, neutral) and near(im, 899, 1079, neutral)) end
      if view == "face_side" then check(G.top_row_neutral(im, neutral)) end
      sheet:paste(file, (col - 1) * 300, r * 384 + 24, 300, 360)
    end
  end
  sheet:save(DATA .. "/ContactSheet.png")
  local sources = { "Tools/ok-extract/src/anim/yure.rs", "Tools/ok-extract/src/shape.rs", "Tools/ok-extract/src/session.rs", "Tools/ok-extract/src/ffi.rs",
    "Tools/ok-extract/src/dbone/ver02.rs", "Tools/ok-extract/src/session_yure_tests.rs", "Tools/ok-extract/include/ok_extract.h",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/KKRustExtract.cpp", "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/KKAnimationCatalog.cpp",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/KKPreviewActor.cpp", "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/Tests/KKStudioYure.spec.cpp",
    "Tools/GamePoseProbe/Probe.cs", "old_ps1/Tools/GamePoseProbe/run_yure_native.ps1", "old_ps1/Tools/GamePoseProbe/run_yure.ps1", "Tools/GamePoseProbe/verify_yure.lua",
    "Docs/UNREAL_STUDIO_YURE.md", "Docs/ERROR_LOG.md", "HANDOFF2.md", "Saved/DebugShots/Studio294_OneD_open/README.md" }
  local logs = { "Studio294YureRowsUnit.log", "Studio294EnableUnitFinal.log", "Studio294PhysicsAll.log", "Studio294SessionFinal.log", "Studio294LegacySession.log",
    "Studio294EyeLookColdFinal.log", "Studio294EyeLookColdDiagnostic.log", "Studio294EyeLookMissingHead.log",
    "Studio294UnrealBuildCore.log", "Studio294UnrealBuildFinal.log", "StudioUnrealAutomation294Console.log", "StudioShots294_OneD_open.log" }
  local report = O("stage", 294, "cards", 5, "source_loads", 45, "shape_attributes", 6390, "masked_negative", 20, "invalid_atomic", 45,
    "shape_rows", G.rows(shape), "source_physics_frames", 1200, "source_physics_bones", 12000, "source_physics_rows", G.rows(rows),
    "legacy_session_frames", 135, "cold_loads", 5, "cold_eye_look_steps", 300, "unreal_tests", 18, "yure_controls", G.rows(controls), "unreal_yure_frames", 450,
    "captures", O("images", 15, "attachments", 71, "max_vertex_error_cm", 0, "contact_sheet_sha256", digest(DATA .. "/ContactSheet.png"), "sha256", G.sorted_object(names, function(n) return digest(shots .. "/" .. n) end)),
    "native_sha256", native, "source_sha256", G.digests(ROOT, sources), "log_sha256", G.digests(LOGS, logs),
    "limits", "Source Yure policy and per-side shape/enable lifecycle. Frozen native physics uses captured setup inputs. No full Unity automatic scheduler, full H gameplay, cross-package Yure dependencies or shader pixel equivalence; chin deferred.")
  G.write_json(LOGS .. "/Studio294Audit.json", report)
  print("PASS: 5 cards; 45 source loads / 6390 shape attributes; 1200 native physics frames / 12000 bones; 18 UE tests / 450 Yure frames; 15 RHI images / 71 attachment audits")
end

if ok.is_main() then ok.main(main) end
