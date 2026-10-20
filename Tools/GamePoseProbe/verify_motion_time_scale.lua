-- Stage296 serialized motion duration, native Animator, Session and RHI gates.
local ok = require "ok"
local G = require "gpp"
local VA = require "verify_animator"
local json, util = ok.json, ok.util
local O, A, F, I, check, findall = G.O, G.A, G.F, G.I, G.check, G.findall
local CARDS, digest, read = VA.CARDS, VA.digest, VA.read

local ROOT = G.ROOT
local LOGS = ROOT .. "/Saved/Logs"
local DATA = ROOT .. "/Saved/Tests/MotionTimeScale296"
local SHOTS = ROOT .. "/Saved/DebugShots/MotionTimeScale296_Sync73_open"
local GAME = ok.game_root(true)
local VIEWS = { "3d", "face", "face_side" }

local function near(im, x, y, neutral) local r, g, b = im:get(x, y); return G.close_rgb(r, g, b, neutral) end

local function contact()
  local expected = {}
  for _, c in ipairs(CARDS) do for _, v in ipairs(VIEWS) do expected[#expected + 1] = c .. "_outfit0_" .. v .. ".png" end end
  check(G.set_eq(G.glob_names(SHOTS, "*.png"), expected))
  local with_readme = { table.unpack(expected) }; with_readme[#with_readme + 1] = "README.md"
  check(G.set_eq(G.file_names(SHOTS), with_readme))
  local sheet = G.sheet(900, 1920, { 0x30, 0x30, 0x30 })
  for row, card in ipairs(CARDS) do
    local r = row - 1
    sheet:text(8, r * 384 + 6, card .. " / source synced layer 73; elapsed .4")
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

local function main()
  local p = ok.args.parser()
  p:flag("--contact-only")
  local args = p:parse(arg)
  local images = contact()
  if args.contact_only then
    print("Contact sheet: five cards, 15 images")
    return
  end
  local source = json.decode(read(DATA .. "/SourceAudit.json"))
  check(source.controllers == 437 and #source.scaled_leaves == 6)
  check(util.equal(util.sorted(G.map(source.scaled_leaves, function(r) return util.round(r.node.m_Duration, 6) end)), { .5, .5, .5, .5, .5, .833333 }))
  for _, r in ipairs(source.sources) do
    check(digest(GAME .. "/" .. r.source) == r.sha256)
  end
  local native = json.object()
  for _, card in ipairs(CARDS) do
    local stem = G.sanitize(card) .. "MotionTimeScale296.json"
    check(json.decode(read(DATA .. "/" .. stem))["error"] == "")
    local frames = json.decode(read(DATA .. "/" .. stem .. ".motion-time-scale.json"))
    check(#frames == 60)
    local counts = G.map({ 0, 73, 74 }, function(n) return G.sum(frames, function(f) return f.layer == n and 1 or 0 end) end)
    check(util.equal(counts, { 24, 18, 18 }))
    check(G.all(frames, function(f) return #f.bones >= 400 and #f.clips > 0 and f.length > 0 end))
    native[card] = O("", digest(DATA .. "/" .. stem), ".motion-time-scale.json", digest(DATA .. "/" .. stem .. ".motion-time-scale.json"))
  end
  local rust = read(LOGS .. "/MotionTimeScale296RustAccepted.log")
  check(G.has(rust, "2 passed; 0 failed"))
  check(G.has(rust, "test anim::motion_time_scale_tests::native_motion_time_scale ... ok"))
  check(G.has(rust, "test anim::motion_time_scale_tests::motion_time_scale_session ... ok"))
  check(G.has(rust, "frames=300 attributes=81000 scaled_clock_controls=80 synced_controls=180"))
  local maxima = G.search([=[accepted frames=300 attributes=81000.*?max=\[([^]]+)\]]=], rust)
  check(maxima and G.all(util.split(maxima[1], ","), function(v) return F(v) < 2e-5 end))
  local session = rust
  check(G.count(session, "Session card=") == 5)
  local total = G.search([[Session accepted frames=300 bones=(\d+) attachment_vertices=(\d+) bone_max=([\deE.+-]+) attachment_max_m=([\deE.+-]+)]], session)
  check(total and I(total[1]) == 180000 and I(total[2]) > 1000000)
  check(F(total[3]) < 2e-5 and F(total[4]) < 2e-5)
  local unit = read(LOGS .. "/MotionTimeScale296Unit.log")
  check(G.has(unit, "scaled_single_motion_preserves_offsets_and_rejects_invalid_duration ... ok"))
  check(G.has(unit, "controller_composition_preserves_source_scene ... ok") and G.has(unit, "4 passed; 0 failed"))
  check(G.has(read(LOGS .. "/MotionTimeScale296UnrealBuildContextAccepted.log"), "Result: Succeeded"))
  local target = read(LOGS .. "/MotionTimeScaleUnrealAutomation296Console.log")
  local core = read(LOGS .. "/MotionRegressionUnrealAutomation296Console.log")
  local expected = { "Animator1DBlends", "Animator2DLayers", "AnimatorStateClock", "StudioSourceInitialization", "AnimatorMotionTimeScale" }
  check(not G.has(core, "Result={Fail}"))
  check(not G.has(target, "Result={Fail}"))
  check(G.set_eq(findall([[Result=\{Success\} Name=\{([^}]+)\}]], core .. "\n" .. target), expected))
  check(G.has(core, "Automation Test Queue Empty 4 tests performed.") and G.has(core, "RequestExitWithStatus(1, 0,"))
  check(G.has(target, "Automation Test Queue Empty 1 tests performed.") and G.has(target, "RequestExitWithStatus(1, 0,"))
  check(G.has(target, "MotionTimeScale accepted cards=5 frames=300 scaled_clock_controls=80 synced_controls=180"))
  local capture = read(LOGS .. "/StudioShots296_Sync73_open.log")
  check(not G.has(capture, "LogTemp: Error:") and G.has(capture, "RequestExitWithStatus(0, 0,"))
  local fits = findall([[capture fit view=\w+ .*?ndc_x=([\d.]+) ndc_y=([\d.]+) limit=0.92]], capture)
  check(#fits == 15 and G.all(fits, function(f) return math.max(F(f[1]), F(f[2])) <= .920001 end))
  local attachments = G.floats(findall([[max_vertex_error_cm=([\d.]+)]], capture))
  check(#attachments == 71 and G.max(attachments) == 0)
  check(#findall([[OK Studio PV display audit passed=1]], capture) == 5)
  local display = findall([[PV display audit passed=1 bindings=4 followers=(\d+) samples=(\d+) palette_error_cm=([\d.]+)]], capture)
  check(#display == 5 and G.all(display, function(d) return I(d[2]) == 8 * I(d[1]) and F(d[3]) < 1e-4 end))
  check(G.count(capture, "OK game animation scene mode=0") == 5)
  check(G.count(capture, "Tracing Screenshot") == 15)
  for _, tag in ipairs({ "OK gaze display audit passed=1", "OK face time display audit passed=1", "OK eye highlight display audit passed=1", "OK eye look shot states=" }) do
    check(G.count(capture, tag) == 5)
  end
  local old = json.decode(read(LOGS .. "/EyeShader286Audit.json"))
  for n, h in pairs(old.captures.Tpose.sha256) do check(digest(ROOT .. "/Saved/Screenshots/" .. n) == h) end
  local paths = { "Tools/ok-extract/src/anim.rs", "Tools/ok-extract/src/anim/controller.rs",
    "Tools/ok-extract/src/anim/motion_time_scale_tests.rs", "Tools/GamePoseProbe/Probe.cs",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/Tests/KKMotionTimeScale.spec.cpp",
    "Tools/anim_export/inspect_motion_time_scale.lua", "old_ps1/Tools/GamePoseProbe/run_motion_time_scale.ps1",
    "old_ps1/Tools/GamePoseProbe/run_motion_time_scale_native.ps1", "Tools/GamePoseProbe/verify_motion_time_scale.lua",
    "Docs/UNREAL_ANIMATOR_MOTION_TIME_SCALE.md", "HANDOFF2.md", "Docs/ERROR_LOG.md" }
  local logs = { "MotionTimeScale296ProbeBuild.log", "MotionTimeScale296NativeMarchRunner.log",
    "MotionTimeScale296NativeRemainingRunner.log", "MotionTimeScale296RustFirstRun.log",
    "MotionTimeScale296RustContextFirstRun.log", "MotionTimeScale296RustAccepted.log", "MotionTimeScale296Unit.log",
    "MotionTimeScale296UnrealBuildContextAccepted.log", "MotionTimeScaleUnrealAutomation296Console.log", "MotionRegressionUnrealAutomation296Console.log", "StudioShots296_Sync73_open.log" }
  local audit = O("stage", 296, "accepted", true, "source_controllers", 437, "source_scaled_leaves", 6,
    "sources", G.digests(ROOT, paths), "logs", G.digests(LOGS, logs),
    "binaries", G.digests(ROOT, { "Plugins/KKUnityBundle/Binaries/Win64/UnrealEditor-KKUnityBundle.dll",
      "Tools/ok-extract/target/release/ok_extract.lib", "Tools/GamePoseProbe/OpenKoikatsu.GamePoseProbe.motion296.last-run.dll" }),
    "source_audit_sha256", digest(DATA .. "/SourceAudit.json"), "native", native,
    "native_frames", 300, "native_attributes", 81000, "native_maxima", A(G.floats(util.split(maxima[1], ","))),
    "session", O("frames", 300, "bones", I(total[1]), "attachment_vertices", I(total[2]), "max_bone", F(total[3]), "max_vertex", F(total[4])),
    "unreal_tests", A(util.sorted(expected)), "captures", O("images", 15, "attachment_audits", 71, "fits", 15,
      "scene", "source Auto; H/esthetic", "follower_samples", G.sum(display, function(d) return I(d[2]) end),
      "max_palette_error_cm", G.max(display, function(d) return F(d[3]) end),
      "contact_sheet_sha256", digest(DATA .. "/ContactSheet.png"), "sha256", G.sorted_object(images, function(n) return digest(SHOTS .. "/" .. n) end)),
    "tpose286_root_unchanged", true, "historical_failed_logs", A({ "MotionTimeScale296RustFirstRun.log", "MotionTimeScale296RustContextFirstRun.log" }),
    "rejected_initial_captures", "Saved/DebugShots/old/MotionTimeScale296BeforeSourceContext")
  G.write_json(LOGS .. "/MotionTimeScale296Audit.json", audit, true)
  print("MotionTimeScale296 accepted: 6 source leaves; native 300 frames/81000 attributes; Session 300 frames; UE 5 tests; RHI 15 images; Tpose286 unchanged")
end

if ok.is_main() then ok.main(main) end
