-- Stage301 independent native, Session, UE actual Tick and RHI gates.
local ok = require "ok"
local G = require "gpp"
local VA = require "verify_animator"
local json, util, path = ok.json, ok.util, ok.path
local O, A, F, I, check, findall = G.O, G.A, G.F, G.I, G.check, G.findall
local CARDS, digest, read = VA.CARDS, VA.digest, VA.read

local ROOT = G.ROOT
local LOGS = ROOT .. "/Saved/Logs"
local DATA = ROOT .. "/Saved/Tests/AnimatorCrossFade301"
local SHOTS = ROOT .. "/Saved/DebugShots/AnimatorCrossFade301_BaseCrossFade_open"
local GAME = ok.game_root(true)
local VIEWS = { "3d", "face", "face_side" }

local function near(im, x, y, neutral) local r, g, b = im:get(x, y); return G.close_rgb(r, g, b, neutral) end

local function contact()
  local expected = {}
  for _, c in ipairs(CARDS) do for _, v in ipairs(VIEWS) do expected[#expected + 1] = c .. "_outfit0_" .. v .. ".png" end end
  local with_readme = { table.unpack(expected) }; with_readme[#with_readme + 1] = "README.md"
  check(G.set_eq(G.file_names(SHOTS), with_readme))
  local sheet = G.sheet(900, 1920, { 0x30, 0x30, 0x30 })
  for row, card in ipairs(CARDS) do
    local r = row - 1
    sheet:text(8, r * 384 + 6, card .. " / Idle -> Anger CrossFade; elapsed 1.19")
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

-- text.split(start, 1)[1].split('}', 1)[0]
local function struct_body(text, start)
  local _, b = text:find(start, 1, true)
  if not b then error("IndexError: list index out of range", 2) end
  local rest = text:sub(b + 1)
  local e = rest:find("}", 1, true)
  return e and rest:sub(1, e - 1) or rest
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
  local decomp = GAME .. "/kks_decomp/Managed/CharaStudio_Data/Assembly-CSharp"
  local source_paths = { "ChaControl.cs", "Illusion.Game.Elements.EasyLoader/Motion.cs", "NodeCanvas.Tasks.Actions/MecanimPlayAnimation.cs" }
  check(G.all(source_paths, function(sp) return G.has(read(decomp .. "/" .. sp), "CrossFade(") end))
  local composition = json.decode(read(DATA .. "/NativeCompositionAudit.json"))
  check(composition.frames == 75 and composition.transform_snapshots == 34995)
  check(G.all(composition.maximum_trs_error, function(v) return v < 2e-5 end))
  check(composition.slerp_error > 2e-5 and composition.smoothstep_error > 2e-5)
  local native = json.object()
  for _, card in ipairs(CARDS) do
    local stem = G.sanitize(card) .. "AnimatorCrossFade301.json"
    check(json.decode(read(DATA .. "/" .. stem))["error"] == "")
    local frames = json.decode(read(DATA .. "/" .. stem .. ".crossfade.json"))
    check(#frames == 15)
    check(G.set_eq(G.map(frames, function(f) return f.bundle end), { "action/animator/00.unity3d" }))
    check(G.set_eq(G.map(frames, function(f) return f.controller end), { "base" }))
    check(G.set_eq(G.map(frames, function(f) return f.blend end), { 0, .4 }))
    check(G.set_eq(G.map(frames, function(f) return f.speed end), { 1, 1.6 }))
    for _, f in ipairs(frames) do
      check(#f.bones >= 400)
      check(#f.bones == #f.fromBones and #f.fromBones == #f.toBones)
      check(f.fromLength > 0 and f.toLength > 0)
      check(math.abs(f.offset + (f.elapsed - f.start) / f.toLength - f.toNormalized) < 2e-5)
    end
    native[card] = O("", digest(DATA .. "/" .. stem), ".crossfade.json", digest(DATA .. "/" .. stem .. ".crossfade.json"))
  end
  local rust = read(LOGS .. "/AnimatorCrossFade301RustAccepted.log")
  check(G.has(rust, "2 passed; 0 failed"))
  check(G.has(rust, "test anim::crossfade_tests::native_crossfade ... ok"))
  check(G.has(rust, "test anim::crossfade_tests::crossfade_session ... ok"))
  local maxima = G.search([=[native accepted frames=75 attributes=(\d+) max=\[([^]]+)\]]=], rust)
  check(maxima and I(maxima[1]) > 10000 and G.all(util.split(maxima[2], ","), function(v) return F(v) < 2e-5 end))
  local total = G.search([[Session accepted frames=75 bones=(\d+) attachment_vertices=(\d+) bone_max=([\deE.+-]+) attachment_max_m=([\deE.+-]+) rejected=(\d+)]], rust)
  check(total and I(total[1]) == 45000 and I(total[2]) > 1000000 and F(total[3]) < 2e-5 and F(total[4]) < 2e-5 and I(total[5]) == 525)
  local cache = read(ROOT .. "/Tools/ok-extract/src/sqlcache.rs")
  check(G.has(cache, "pub const CACHE_VER: i32 = 90;") and G.has(cache, "pub const SESSION_PIPELINE_VER: i32 = 141;"))
  for _, pair in ipairs({ { "Tools/ok-extract/src/ffi.rs", "pub struct OkRustBindings {" }, { "Tools/ok-extract/include/ok_extract.h", "typedef struct OkRustBindings {" } }) do
    local binding = struct_body(read(ROOT .. "/" .. pair[1]), pair[2])
    check(not G.any({ "ok_animation_crossfade", "ok_animation_elapsed", "ok_animation_in_transition" }, function(n) return G.has(binding, n) end))
  end
  check(G.has(read(LOGS .. "/AnimatorCrossFade301UnrealBuild.log"), "Result: Succeeded"))
  local target = read(LOGS .. "/AnimatorCrossFadeUnrealAutomation301Console.log")
  local core = read(LOGS .. "/CrossFadeRegressionUnrealAutomation301Console.log")
  local expected = { "AnimatorCrossFade", "AnimatorTriggerRestart", "AnimatorIndependentLayerClock", "AnimatorMultiLayer", "AnimatorAdditiveLayers", "Animator1DBlends", "Animator2DLayers", "AnimatorStateClock", "StudioSourceInitialization", "AnimatorMotionTimeScale" }
  check(not G.has(target .. core, "Result={Fail}"))
  check(G.set_eq(findall([[Result=\{Success\} Name=\{([^}]+)\}]], target .. "\n" .. core), expected))
  check(G.has(target, "Automation Test Queue Empty 1 tests performed.") and G.has(target, "RequestExitWithStatus(1, 0,"))
  check(G.has(core, "Automation Test Queue Empty 9 tests performed.") and G.has(core, "RequestExitWithStatus(1, 0,"))
  check(G.has(target, "CrossFade accepted cards=5 frames=75") and G.has(target, "invalid_rejected=285"))
  local regression_frames = json.object()
  for _, fam in ipairs({ { "Animator1D", 450 }, { "Animator2D", 300 }, { "Additive", 180 }, { "MotionTimeScale", 300 }, { "MultiLayer", 75 }, { "LayerClock", 100 }, { "AnimatorClock", 180 }, { "StudioSource", 135 } }) do
    local family, n = fam[1], fam[2]
    local count = G.sum(findall("LogAutomationController: " .. family .. [[ card=.*? frames=(\d+)]], core), I)
    check(count == n, family .. " " .. count)
    regression_frames[family] = count
  end
  check(G.has(core, "Restart accepted cards=5 frames=75"))
  regression_frames.Restart = 75
  local capture = read(LOGS .. "/StudioShots301_BaseCrossFade_open.log")
  check(not G.has(capture, "LogTemp: Error:") and G.has(capture, "RequestExitWithStatus(0, 0,"))
  local fits = findall([[capture fit view=\w+ .*?ndc_x=([\d.]+) ndc_y=([\d.]+) limit=0.92]], capture)
  check(#fits == 15 and G.all(fits, function(f) return math.max(F(f[1]), F(f[2])) <= .920001 end))
  local attachments = G.floats(findall([[max_vertex_error_cm=([\d.]+)]], capture))
  check(#attachments == 71 and G.max(attachments) == 0)
  local display = findall([[PV display audit passed=1 bindings=4 followers=(\d+) samples=(\d+) palette_error_cm=([\d.]+)]], capture)
  check(#display == 5 and G.all(display, function(d) return I(d[2]) == 8 * I(d[1]) and F(d[3]) < 1e-4 end))
  check(G.count(capture, "OK game animation scene mode=0") == 5 and G.count(capture, "Tracing Screenshot") == 15)
  local events = findall([[OK shot source CrossFade elapsed=([\d.]+) normalized_duration=([\d.]+) offset=([\d.]+) elapsed_preserved=1]], capture)
  check(#events == 5 and G.all(events, function(e) return math.abs(F(e[1]) - .61) < 1e-6 and math.abs(F(e[2]) - .4) < 1e-6 and math.abs(F(e[3]) - .37) < 1e-6 end))
  local actor = findall([[OK Animator CrossFade display audit passed=1 samples=3 actual_tick=1 elapsed_preserved=1 source_and_destination_playing=1 transition_complete=1 palette_error_cm=([\d.]+)]], capture)
  check(#actor == 5 and G.all(actor, function(n) return F(n) < 1e-4 end))
  for _, tag in ipairs({ "OK gaze display audit passed=1", "OK face time display audit passed=1", "OK eye highlight display audit passed=1", "OK eye look shot states=" }) do
    check(G.count(capture, tag) == 5)
  end
  local old = json.decode(read(LOGS .. "/EyeShader286Audit.json"))
  for n, h in pairs(old.captures.Tpose.sha256) do check(digest(ROOT .. "/Saved/Screenshots/" .. n) == h) end
  check(not path.exists(GAME .. "/BepInEx/plugins/OpenKoikatsu.GamePoseProbe.dll"))
  local runners = { "AnimatorCrossFade301NativeMarchRunner.log", "AnimatorCrossFade301NativeRemainingRunner.log", "AnimatorCrossFadeUnrealAutomation301Runner.log", "CrossFadeRegressionUnrealAutomation301Runner.log", "AnimatorCrossFade301ShotsRunner.log" }
  local pid_list = {}
  for _, name in ipairs(runners) do
    for _, pid in ipairs(findall([[PID (\d+)]], read(LOGS .. "/" .. name))) do pid_list[#pid_list + 1] = I(pid) end
  end
  local pids = G.unique(pid_list)
  for _, pid in ipairs(pids) do
    local out = (ok.kit.call({ "proc-wait", pid, 0 }).path or ""):lower()
    check(not G.any({ "charastudio.exe", "unrealeditor.exe", "unrealeditor-cmd.exe" }, function(n) return G.has(out, n) end), pid .. " " .. out)
  end
  local paths = { "Tools/ok-extract/src/anim.rs", "Tools/ok-extract/src/anim/controller.rs", "Tools/ok-extract/src/anim/crossfade.rs", "Tools/ok-extract/src/anim/crossfade_tests.rs", "Tools/ok-extract/src/session.rs", "Tools/ok-extract/src/ffi.rs", "Tools/ok-extract/src/lib.rs", "Tools/ok-extract/include/ok_extract.h",
    "Tools/GamePoseProbe/Probe.cs", "old_ps1/Tools/GamePoseProbe/run_crossfade_native.ps1", "old_ps1/Tools/GamePoseProbe/run_crossfade.ps1", "Tools/GamePoseProbe/verify_crossfade.lua", "Tools/anim_export/verify_native_crossfade.lua",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/Tests/KKAnimatorCrossFade.spec.cpp",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/KKPreviewActor.cpp", "Plugins/KKUnityBundle/Source/KKUnityBundle/Public/KKPreviewActor.h",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/KKRustExtract.cpp", "Plugins/KKUnityBundle/Source/KKUnityBundle/Public/KKRustExtract.h",
    "Tools/ok-extract/src/sqlcache.rs", "Docs/UNREAL_ANIMATOR_CROSSFADE.md", "Docs/UNREAL_ANIMATION_CATALOG.md", "HANDOFF2.md", "Docs/ERROR_LOG.md" }
  local logs = { "AnimatorCrossFade301ProbeBuild.log" }
  for _, n in ipairs(runners) do logs[#logs + 1] = n end
  for _, n in ipairs({ "AnimatorCrossFade301NativeComposition.log", "AnimatorCrossFade301RustAccepted.log", "AnimatorCrossFade301UnrealBuild.log", "AnimatorCrossFadeUnrealAutomation301Console.log", "CrossFadeRegressionUnrealAutomation301Console.log", "StudioShots301_BaseCrossFade_open.log" }) do logs[#logs + 1] = n end
  local historical = { "AnimatorCrossFade301RustFirstCompileFailed.log", "AnimatorCrossFade301RustFirstNumericFailed.log", "AnimatorCrossFade301RustMarch.log", "AnimatorCrossFade301RustMarchAttributes.log", "AnimatorCrossFade301RustMarchEffective.log", "AnimatorCrossFade301NativeCompositionPartial.log", "AnimatorCrossFade301UnrealBuildFirstFailed.log" }
  local regression_sum = 0
  for _, v in pairs(regression_frames) do regression_sum = regression_sum + v end
  local audit = O("stage", 301, "accepted", true, "sources", G.digests(ROOT, paths), "logs", G.digests(LOGS, logs),
    "binaries", G.digests(ROOT, { "Plugins/KKUnityBundle/Binaries/Win64/UnrealEditor-KKUnityBundle.dll", "Tools/ok-extract/target/release/ok_extract.lib", "Tools/GamePoseProbe/OpenKoikatsu.GamePoseProbe.crossfade301.last-run.dll" }),
    "historical_logs", G.digests(LOGS, historical), "decompiled_sources", G.digests(decomp, source_paths), "source_bundle_audit_sha256", digest(ROOT .. "/Saved/Tests/AnimatorAdditive297/SourceAudit.json"), "native", native, "independent_native_composition", composition,
    "composition_audit_sha256", digest(DATA .. "/NativeCompositionAudit.json"), "native_frames", 75, "native_attributes", I(maxima[1]), "native_maxima", A(G.floats(util.split(maxima[2], ","))),
    "session", O("frames", 75, "bones", I(total[1]), "attachment_vertices", I(total[2]), "max_bone", F(total[3]), "max_vertex", F(total[4]), "rejected", I(total[5])),
    "unreal_tests", A(util.sorted(expected)), "unreal_source_frames", 75 + regression_sum, "unreal_regression_frames", regression_frames, "actor_tick_samples", 15, "source_capture_events", 5, "owned_pids_exited", A(util.sorted(pids)),
    "captures", O("images", 15, "attachment_audits", 71, "fits", 15, "follower_samples", G.sum(display, function(d) return I(d[2]) end), "max_palette_error_cm", G.max(display, function(d) return F(d[3]) end), "crossfade_palette_error_cm", G.max(G.floats(actor)), "contact_sheet_sha256", digest(DATA .. "/ContactSheet.png"), "readme_sha256", digest(SHOTS .. "/README.md"), "sha256", G.sorted_object(images, function(n) return digest(SHOTS .. "/" .. n) end)), "tpose286_root_unchanged", true)
  G.write_json(LOGS .. "/AnimatorCrossFade301Audit.json", audit, true)
  print("AnimatorCrossFade301 accepted: native 75 frames/" .. maxima[1] .. " attributes; Session 75 frames; UE 10 tests; Actor.Tick 15 samples; RHI 15 images; Tpose286 unchanged")
end

if ok.is_main() then ok.main(main) end
