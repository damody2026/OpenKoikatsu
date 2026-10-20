-- Stage295 source PV callback, runtime policy and actual RHI acceptance.
local ok = require "ok"
local G = require "gpp"
local VA = require "verify_animator"
local json, util = ok.json, ok.util
local O, A, F, I, check, findall = G.O, G.A, G.F, G.I, G.check, G.findall
local CARDS, TESTS, digest, read = VA.CARDS, VA.TESTS, VA.digest, VA.read

local ROOT = G.ROOT
local LOGS = ROOT .. "/Saved/Logs"
local DATA = ROOT .. "/Saved/Tests/Studio295"
local SHOTS = ROOT .. "/Saved/DebugShots/Studio295_OneD_open"
local VIEWS = { "3d", "face", "face_side" }

local function near(im, x, y, neutral) local r, g, b = im:get(x, y); return G.close_rgb(r, g, b, neutral) end

local function contact()
  local expected = {}
  for _, c in ipairs(CARDS) do for _, v in ipairs(VIEWS) do expected[#expected + 1] = c .. "_outfit0_" .. v .. ".png" end end
  check(G.set_eq(G.glob_names(SHOTS, "*.png"), expected))
  local sheet = G.sheet(900, 1920, { 0x30, 0x30, 0x30 })
  for row, card in ipairs(CARDS) do
    local r = row - 1
    sheet:text(8, r * 384 + 6, card .. " / imported H; PV=0011, Yure=0011")
    local neutral = { G.load_rgb(SHOTS .. "/" .. card .. "_outfit0_3d.png"):get(0, 0) }
    check(120 <= math.min(table.unpack(neutral)) and math.max(table.unpack(neutral)) <= 135)
    for col, view in ipairs(VIEWS) do
      local file = SHOTS .. "/" .. card .. "_outfit0_" .. view .. ".png"
      check(G.size_is(file, 900, 1080))
      local im = G.load_rgb(file)
      check(G.top_row_neutral(im, neutral))
      if view == "3d" then
        check(near(im, 0, 0, neutral) and near(im, 899, 0, neutral) and near(im, 0, 1079, neutral) and near(im, 899, 1079, neutral))
      end
      sheet:paste(file, (col - 1) * 300, r * 384 + 24, 300, 360)
    end
  end
  sheet:save(DATA .. "/ContactSheet.png")
  return expected
end

local function concat(a, b) local out = { table.unpack(a) }; for _, v in ipairs(b) do out[#out + 1] = v end; return out end

local function main()
  local p = ok.args.parser()
  p:flag("--contact-only")
  local args = p:parse(arg)
  local images = contact()
  if args.contact_only then print("Contact sheet: 15 images, five cards"); return end
  local native = json.object()
  local copied, held = 0, 0
  local masks = { { true, true, true, true }, { false, false, true, true }, { false, false, false, false }, { false, false, true, true }, { true, true, true, true } }
  for _, card in ipairs(CARDS) do
    local stem = G.sanitize(card) .. "Studio295.json"
    check(json.decode(read(DATA .. "/" .. stem))["error"] == "")
    local frames = json.decode(read(DATA .. "/" .. stem .. ".pv.json")); check(#frames == 15)
    local want = {}
    for s = 0, 4 do for t = 0, 2 do want[#want + 1] = { s, t } end end
    check(G.set_eq(G.map(frames, function(f) return { f.stage, f.sample } end), want))
    for _, f in ipairs(frames) do
      local mask = masks[f.stage + 1]
      check(util.equal(f.enabled, mask) and f.fk == (f.stage == 2))
      check(G.has(f.callback, "<Start>") and #f.pv == #f.bone and #f.bone == #f.after and #f.after == 4)
      local unique_paths = #G.unique(f.before, function(b) return b.path end)
      check(unique_paths == #f.before and #f.before >= 450)
      local on = G.sum(mask, function(m) return m and 1 or 0 end)
      copied = copied + on; held = held + 4 - on
    end
    native[card] = O("", digest(DATA .. "/" .. stem), ".pv.json", digest(DATA .. "/" .. stem .. ".pv.json"))
  end
  check(copied == 180 and held == 120)
  local rust = read(LOGS .. "/Studio295RustAccepted.log"); check(G.has(rust, "2 passed; 0 failed"))
  local native_rows = findall([[PV native card=(.+?) frames=15 max=([\deE.+-]+)]], rust)
  check(#native_rows == 5 and G.set_eq(G.map(native_rows, function(r) return r[1] end), CARDS) and G.all(native_rows, function(r) return F(r[2]) < 2e-5 end))
  check(G.has(rust, "frames=75 copied=180 held=120 positive=180"))
  check(G.has(rust, "PV Session accepted frames=300 changed=720 atomic=10"))
  check(G.count(rust, "bindings=4 normal_reset=1 cache_reset=1") == 5)
  local legacy = read(LOGS .. "/Studio295LegacySession.log"); check(G.has(legacy, "1 passed; 0 failed"))
  check(G.has(legacy, "Studio Session accepted cards=5 frames=135 bones=81000 attachment_vertices=2929257 invalid_atomic=405"))
  check(G.has(read(LOGS .. "/Studio295UnrealBuildAccepted.log"), "Result: Succeeded"))
  local core = read(LOGS .. "/StudioUnrealAutomation295Console.log")
  local repair = read(LOGS .. "/StudioPVUnrealAutomation295Console.log")
  check(G.set_eq(findall([[Result=\{Fail\} Name=\{([^}]+)\}]], core), { "StudioPVPlayback" }))
  check(G.has(core, "Expected 'actual H disables hands only' to be 12, but it was 15."))
  check(not G.has(repair, "Result={Fail}") and G.has(repair, "Automation Test Queue Empty 1 tests performed.") and G.has(repair, "RequestExitWithStatus(1, 0,"))
  local yure = read(LOGS .. "/StudioYureUnrealAutomation295Console.log")
  check(not G.has(yure, "Result={Fail}") and G.has(yure, "Automation Test Queue Empty 1 tests performed.") and G.has(yure, "RequestExitWithStatus(1, 0,"))
  check(G.count(yure, "invalid_atomic=1 cold_eye_look=1") == 5)
  local tests = core .. "\n" .. repair .. "\n" .. yure
  local core_success = G.filter(G.unique(findall([[Result=\{Success\} Name=\{([^}]+)\}]], core)), function(n) return n ~= "StudioYureInitialization" end)
  local success = G.unique(concat(core_success, findall([[Result=\{Success\} Name=\{([^}]+)\}]], repair .. "\n" .. yure)))
  local expected = G.unique(concat(TESTS, { "Animator2DLayers", "AnimatorParameterPropagation", "AnimatorStateClock", "StudioSourceInitialization", "StudioYureInitialization", "StudioPVPlayback" }))
  check(#success == 19 and G.set_eq(success, expected))
  check(G.has(core, "RequestExitWithStatus(1, 0,") and G.has(core, "Automation Test Queue Empty 19 tests performed."))
  for _, name in ipairs({ "Studio295UnrealBuildPVAccepted.log", "Studio295UnrealBuildYureAccepted.log" }) do check(G.has(read(LOGS .. "/" .. name), "Result: Succeeded")) end
  local pv = findall([[StudioPV card=(.+?) frames=60 changed=(\d+) max_delta=([\d.]+) mask=0011 FK_hold=1 normal_reset=1 invalid_atomic=2]], tests)
  check(#pv == 5 and G.set_eq(G.map(pv, function(r) return r[1] end), CARDS) and G.all(pv, function(r) return I(r[2]) > 0 and F(r[3]) > 1e-3 end))
  local capture = read(LOGS .. "/StudioShots295_OneD_open.log")
  check(not G.has(capture, "LogTemp: Error:") and G.has(capture, "RequestExitWithStatus(0, 0,") and G.count(capture, "Tracing Screenshot") == 15)
  check(G.count(capture, "OK Studio animation row=r00001390") == 5 and G.count(capture, "yure=0011 pv=0011") == 5)
  local display = findall([[OK Studio PV display audit passed=1 bindings=4 followers=(\d+) samples=(\d+) palette_error_cm=([\d.]+) flags=0011]], capture)
  check(#display == 5 and G.all(display, function(d) return I(d[2]) == 8 * I(d[1]) and I(d[1]) > 0 and F(d[3]) < 1e-4 end))
  for _, gate in ipairs({ "OK gaze display audit passed=1", "OK eye highlight display audit passed=1", "OK eye look shot states=" }) do check(G.count(capture, gate) == 5) end
  check(G.count(capture, "cameras=3 planes=6") == 11)
  local attachments = G.floats(findall([[max_vertex_error_cm=([\d.]+)]], capture)); check(#attachments == 71 and G.max(attachments) == 0)
  local fits = findall([[capture fit view=\w+ .*?ndc_x=([\d.]+) ndc_y=([\d.]+) limit=0.92]], capture)
  check(#fits == 15 and G.all(fits, function(f) return math.max(F(f[1]), F(f[2])) <= .920001 end))
  -- Historical accepted T-pose references stay intact; this stage is H only.
  local old = json.decode(read(LOGS .. "/EyeShader286Audit.json"))
  for n, h in pairs(old.captures.Tpose.sha256) do check(digest(ROOT .. "/Saved/Screenshots/" .. n) == h) end
  local sources = { "Tools/ok-extract/src/pv.rs", "Tools/ok-extract/src/session_pv_tests.rs", "Tools/ok-extract/src/unity.rs", "Tools/ok-extract/src/pose.rs", "Tools/ok-extract/src/lib.rs", "Tools/ok-extract/src/session.rs", "Tools/ok-extract/src/ffi.rs", "Tools/ok-extract/src/sqlcache.rs", "Tools/ok-extract/include/ok_extract.h",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/KKRustExtract.cpp", "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/KKAnimationCatalog.cpp", "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/KKPreviewActor.cpp", "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/Tests/KKStudioPV.spec.cpp", "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/Tests/KKStudioYure.spec.cpp", "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/Tests/KKAnimationCatalog.spec.cpp",
    "Tools/GamePoseProbe/Probe.cs", "old_ps1/Tools/GamePoseProbe/run_pv_native.ps1", "old_ps1/Tools/GamePoseProbe/run_pv.ps1", "Tools/GamePoseProbe/verify_pv.lua", "Docs/UNREAL_STUDIO_PV.md", "Docs/ERROR_LOG.md", "HANDOFF2.md", "Saved/DebugShots/Studio295_OneD_open/README.md" }
  sources = concat(sources, { "Plugins/KKUnityBundle/Source/KKUnityBundle/Public/KKRustExtract.h", "Plugins/KKUnityBundle/Source/KKUnityBundle/Public/KKAnimationCatalog.h", "Plugins/KKUnityBundle/Source/KKUnityBundle/Public/KKPreviewActor.h", "Docs/UNREAL_STUDIO_YURE.md" })
  local logs = { "Studio295ProbeBuild.log", "Studio295NativeMarchRunner.log", "Studio295NativeAllRunner.log", "Studio295RustAccepted.log", "Studio295LegacySession.log", "Studio295UnrealBuildFinal.log", "Studio295UnrealBuildAccepted.log", "Studio295UnrealBuildPVAccepted.log", "Studio295UnrealBuildYureAccepted.log", "StudioUnrealAutomation295Console.log", "StudioPVUnrealAutomation295Console.log", "StudioYureUnrealAutomation295Console.log", "StudioShots295_OneD_open.log" }
  local report = O("stage", 295, "pipeline", 141, "cards", 5, "native_frames", 75, "copied", 180, "held", 120, "native_rows", G.rows(native_rows), "shared_session_frames", 300, "shared_positive", 720, "atomic", 10,
    "unreal_tests", 19, "unreal_core_success", 17, "unreal_repair_success", 2, "initial_failed_test", "StudioPVPlayback: wrong group identity", "independent_yure_frames", 450, "unreal_pv_frames", 300, "unreal_pv_rows", G.rows(pv), "legacy_session_frames", 135, "legacy_attachment_vertices", 2929257,
    "display", O("bindings_per_card", 4, "samples", G.sum(display, function(d) return I(d[2]) end), "max_error_cm", G.max(display, function(d) return F(d[3]) end)),
    "captures", O("images", 15, "attachment_audits", 71, "max_attachment_error_cm", 0, "fits", 15, "contact_sheet_sha256", digest(DATA .. "/ContactSheet.png"), "sha256", G.sorted_object(images, function(n) return digest(SHOTS .. "/" .. n) end)),
    "native_sha256", native, "source_sha256", G.digests(ROOT, sources), "log_sha256", G.digests(LOGS, logs),
    "source_rig", O("path", ok.game_root(true) .. "/abdata/studio/base/00.unity3d", "sha256", digest(ok.game_root(true) .. "/abdata/studio/base/00.unity3d")),
    "native_binaries", O("assembly_sha256", digest(ok.game_root(true) .. "/CharaStudio_Data/Managed/Assembly-CSharp.dll"), "probe_sha256", digest(ROOT .. "/Tools/GamePoseProbe/OpenKoikatsu.GamePoseProbe.studio295.last-run.dll")),
    "limits", "Source compiled PV callback, policy and shared UE display. Not full Unity scheduler, FK/IK solver, every mod controller or shader pixel equivalence; external GameObject refs/singular parents not source-certified; chin deferred.")
  G.write_json(LOGS .. "/Studio295Audit.json", report)
  print("PASS: 5 cards; 75 native PV frames / 180 copied / 120 held; 300 shared frames; 19 UE tests; 15 RHI images / 71 attachment audits")
end

if ok.is_main() then ok.main(main) end
