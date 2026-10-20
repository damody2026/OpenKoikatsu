-- Stage299 independent layer clocks, native Animator, Session and RHI gates.
local ok = require "ok"
local G = require "gpp"
local VA = require "verify_animator"
local json, util = ok.json, ok.util
local O, A, F, I, check, findall = G.O, G.A, G.F, G.I, G.check, G.findall
local CARDS, digest, read = VA.CARDS, VA.digest, VA.read

local ROOT = G.ROOT
local LOGS = ROOT .. "/Saved/Logs"
local DATA = ROOT .. "/Saved/Tests/AnimatorLayerClock299"
local SHOTS = ROOT .. "/Saved/DebugShots/AnimatorLayerClock299_Layers7_8_73_open"
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
    sheet:text(8, r * 384 + 6, card .. " / independent clocks; layers 7/8/73; elapsed .4")
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

-- sorted({x for x in list}) == expected (Python numeric set semantics).
local function sorted_set_is(list, expected)
  local s = util.sorted(G.unique(list))
  if #s ~= #expected then return false end
  for i = 1, #s do if s[i] ~= expected[i] then return false end end
  return true
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
  local source = json.decode(read(ROOT .. "/Saved/Tests/AnimatorAdditive297/SourceAudit.json"))
  for _, r in ipairs(source.sources) do
    check(digest(GAME .. "/" .. r.source) == r.sha256)
  end
  local composition = json.decode(read(DATA .. "/NativeCompositionAudit.json"))
  check(composition.frames == 100 and composition.transform_snapshots > 45000)
  check(G.all(composition.maximum_trs_error, function(v) return v < 2e-5 end))
  check(composition.reverse_order_error > 2e-5 and composition.shared_clock_error > 0.01)
  check(util.equal(composition.layer_modes, { ["7"] = 1, ["8"] = 1, ["70"] = 1, ["73"] = 0, ["74"] = 0 }))
  local native = json.object()
  for _, card in ipairs(CARDS) do
    local stem = G.sanitize(card) .. "AnimatorLayerClock299.json"
    check(json.decode(read(DATA .. "/" .. stem))["error"] == "")
    local frames = json.decode(read(DATA .. "/" .. stem .. ".layer-clock.json"))
    check(#frames == 20)
    local want = {}
    for _, v in ipairs({ { 7, 8 }, { 7, 8 }, { 8, 70 }, { 7, 8, 73 }, { 7, 8, 73, 74 } }) do
      for _ = 1, 4 do want[#want + 1] = v end
    end
    check(util.equal(G.map(frames, function(f) return G.map(f.layers, function(l) return l.layer end) end), want))
    check(sorted_set_is(G.map(frames, function(f) return f.elapsed end), { .05, .17, .61, 1.7 }))
    check(G.all(frames, function(f) return f.baseLength > 0 and #f.bones >= 400 and #f.bones == #f.baseBones end))
    check(G.all(frames, function(f) return G.all(f.layers, function(l) return #l.bones == #f.bones and #l.clips > 0 end) end))
    check(G.sum(frames, function(f) return G.sum(f.layers, function(l) return util.truthy(l.started) and 0 or 1 end) end) == 5)
    check(G.all(frames, function(f) return G.all(f.layers, function(l) return l.layer < 73 or (l.startTime == 0 and l.startPhase == 0) end) end))
    check(G.all(frames, function(f) return #f.zeroClockBones == #f.bones end))
    native[card] = O("", digest(DATA .. "/" .. stem), ".layer-clock.json", digest(DATA .. "/" .. stem .. ".layer-clock.json"))
  end
  local rust = read(LOGS .. "/AnimatorLayerClock299RustAccepted.log")
  check(G.has(rust, "2 passed; 0 failed"))
  check(G.has(rust, "test anim::layer_clock_tests::native_layer_clock ... ok"))
  check(G.has(rust, "test anim::layer_clock_tests::layer_clock_session ... ok"))
  local maxima = G.search([=[accepted frames=100 attributes=27000.*?max=\[([^]]+)\]]=], rust)
  check(maxima and G.all(util.split(maxima[1], ","), function(v) return F(v) < 2e-5 end))
  local total = G.search([[Session accepted frames=100 bones=(\d+) attachment_vertices=(\d+) bone_max=([\deE.+-]+) attachment_max_m=([\deE.+-]+)]], rust)
  check(total and I(total[1]) == 60000 and I(total[2]) > 1000000 and F(total[3]) < 2e-5 and F(total[4]) < 2e-5)
  check(G.has(read(LOGS .. "/AnimatorLayerClock299Unit.log"), "layer_clock_extremes_and_default_phase_are_finite ... ok"))
  check(G.has(read(LOGS .. "/AnimatorLayerClock299Unit.log"), "1 passed; 0 failed"))
  check(G.has(read(LOGS .. "/AnimatorLayerClock299UnrealBuild.log"), "Result: Succeeded"))
  local target = read(LOGS .. "/AnimatorLayerClockUnrealAutomation299Console.log")
  local core = read(LOGS .. "/LayerClockRegressionUnrealAutomation299Console.log")
  local expected = { "AnimatorIndependentLayerClock", "AnimatorMultiLayer", "AnimatorAdditiveLayers", "Animator1DBlends", "Animator2DLayers", "AnimatorStateClock", "StudioSourceInitialization", "AnimatorMotionTimeScale" }
  check(not G.has(target .. core, "Result={Fail}"))
  check(G.set_eq(findall([[Result=\{Success\} Name=\{([^}]+)\}]], target .. "\n" .. core), expected))
  check(G.has(target, "Automation Test Queue Empty 1 tests performed.") and G.has(target, "RequestExitWithStatus(1, 0,"))
  check(G.has(core, "Automation Test Queue Empty 7 tests performed.") and G.has(core, "RequestExitWithStatus(1, 0,"))
  check(G.has(target, "LayerClock accepted cards=5 frames=100"))
  check(G.has(target, "invalid_weights_rejected=520") and G.has(target, "invalid_clocks_rejected=1320"))
  local capture = read(LOGS .. "/StudioShots299_Layers7_8_73_open.log")
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
  check(G.count(capture, "OK Animator elapsed display audit passed=1 samples=8 base_cycles=3 delayed_epoch_after_base=1 loop_and_end_hold=1 actual_tick=1 restored_parameters=1") == 5)
  for _, tag in ipairs({ "OK gaze display audit passed=1", "OK face time display audit passed=1", "OK eye highlight display audit passed=1", "OK eye look shot states=" }) do
    check(G.count(capture, tag) == 5)
  end
  local old = json.decode(read(LOGS .. "/EyeShader286Audit.json"))
  for n, h in pairs(old.captures.Tpose.sha256) do check(digest(ROOT .. "/Saved/Screenshots/" .. n) == h) end
  local paths = { "Tools/ok-extract/src/anim.rs", "Tools/ok-extract/src/anim/controller.rs", "Tools/ok-extract/src/anim/layer_clock_tests.rs",
    "Tools/GamePoseProbe/Probe.cs", "old_ps1/Tools/GamePoseProbe/run_layer_clock_native.ps1", "old_ps1/Tools/GamePoseProbe/run_layer_clock.ps1",
    "Tools/GamePoseProbe/verify_layer_clock.lua", "Tools/anim_export/verify_native_layer_clock.lua",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/Tests/KKAnimatorLayerClock.spec.cpp",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/KKPreviewActor.cpp", "Plugins/KKUnityBundle/Source/KKUnityBundle/Public/KKPreviewActor.h",
    "Docs/UNREAL_ANIMATOR_LAYER_CLOCK.md", "Docs/UNREAL_ANIMATOR_MULTILAYER.md", "Docs/UNREAL_ANIMATION_CATALOG.md", "HANDOFF2.md", "Docs/ERROR_LOG.md" }
  local logs = { "AnimatorLayerClock299Unit.log", "AnimatorLayerClock299ProbeBuild.log", "AnimatorLayerClock299NativeMarchRunner.log", "AnimatorLayerClock299NativeRemainingRunner.log",
    "AnimatorLayerClock299NativeComposition.log", "AnimatorLayerClock299RustAccepted.log", "AnimatorLayerClock299UnrealBuild.log",
    "AnimatorLayerClockUnrealAutomation299Console.log", "LayerClockRegressionUnrealAutomation299Console.log", "StudioShots299_Layers7_8_73_open.log" }
  local audit = O("stage", 299, "accepted", true, "sources", G.digests(ROOT, paths), "logs", G.digests(LOGS, logs),
    "binaries", G.digests(ROOT, { "Plugins/KKUnityBundle/Binaries/Win64/UnrealEditor-KKUnityBundle.dll",
      "Tools/ok-extract/target/release/ok_extract.lib", "Tools/GamePoseProbe/OpenKoikatsu.GamePoseProbe.layer-clock299.last-run.dll" }),
    "native", native, "independent_native_composition", composition, "composition_audit_sha256", digest(DATA .. "/NativeCompositionAudit.json"),
    "source_bundle_sha256", digest(ok.game_root(true) .. "/abdata/h/anim/female/05_01_00.unity3d"),
    "native_frames", 100, "native_attributes", 27000, "native_maxima", A(G.floats(util.split(maxima[1], ","))),
    "session", O("frames", 100, "bones", I(total[1]), "attachment_vertices", I(total[2]), "max_bone", F(total[3]), "max_vertex", F(total[4])),
    "unreal_tests", A(util.sorted(expected)), "actor_tick_samples", 40, "captures", O("images", 15, "attachment_audits", 71, "fits", 15,
      "follower_samples", G.sum(display, function(d) return I(d[2]) end),
      "max_palette_error_cm", G.max(display, function(d) return F(d[3]) end), "contact_sheet_sha256", digest(DATA .. "/ContactSheet.png"),
      "sha256", G.sorted_object(images, function(n) return digest(SHOTS .. "/" .. n) end)), "tpose286_root_unchanged", true)
  G.write_json(LOGS .. "/AnimatorLayerClock299Audit.json", audit, true)
  print("AnimatorLayerClock299 accepted: native 100 frames/27000 attributes; Session 100 frames; UE 8 tests; Actor.Tick 40 samples; RHI 15 images; Tpose286 unchanged")
end

if ok.is_main() then ok.main(main) end
