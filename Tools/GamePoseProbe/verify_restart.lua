-- Stage300 source transitions, native Animator, Session, real Tick and RHI gates.
local ok = require "ok"
local G = require "gpp"
local VA = require "verify_animator"
local json, util, path = ok.json, ok.util, ok.path
local O, A, F, I, check, findall = G.O, G.A, G.F, G.I, G.check, G.findall
local CARDS, digest, read = VA.CARDS, VA.digest, VA.read

local ROOT = G.ROOT
local LOGS = ROOT .. "/Saved/Logs"
local DATA = ROOT .. "/Saved/Tests/AnimatorRestart300"
local SHOTS = ROOT .. "/Saved/DebugShots/AnimatorRestart300_Layers7_8_73_open"
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
    sheet:text(8, r * 384 + 6, card .. " / source Restart .61/1.07; elapsed 1.19")
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

local function source_rules()
  local bundle = GAME .. "/abdata/h/anim/female/05_01_00.unity3d"
  local c
  for _, o in ipairs(ok.kit.unity_objects(bundle, { type = "AnimatorController" })) do
    if o.type == "AnimatorController" and o.tree.m_Name == "khr_f_02_00" then c = o.tree; break end
  end
  if c == nil then error("StopIteration") end
  check(util.equal(c, json.decode(read(DATA .. "/Controller.json"))))
  local ctrl = c.m_Controller
  check(G.any(ctrl.m_Values.data.m_ValueArray, function(v) return v.m_ID == 675552155 and v.m_Type == 9 end))
  local rules = json.object()
  for _, i in ipairs({ 0, 7, 8, 70, 73, 74 }) do
    local layer = ctrl.m_LayerArray[i + 1].data
    local sm = ctrl.m_StateMachineArray[layer.m_StateMachineIndex + 1].data
    local authored = A(G.map(sm.m_AnyStateTransitionConstantArray, function(r) return r.data end))
    if i == 7 or i == 8 or i == 70 then
      check(#authored == 1)
      local r = authored[1]
      check(r.m_DestinationState == 0 and r.m_TransitionDuration == 0 and r.m_TransitionOffset == 0)
      check(not util.truthy(r.m_HasExitTime) and util.truthy(r.m_CanTransitionToSelf))
      check(#r.m_ConditionConstantArray == 1)
      local cond = r.m_ConditionConstantArray[1].data
      check(cond.m_EventID == 675552155 and cond.m_ConditionMode == 1)
    else
      check(#authored == 0)
    end
    rules[tostring(i)] = authored
  end
  return O("bundle_sha256", digest(bundle), "controller_sha256", digest(DATA .. "/Controller.json"), "layers", rules)
end

-- text.split(start, 1)[1].split('}', 1)[0]
local function struct_body(text, start)
  local _, b = text:find(start, 1, true)
  if not b then error("IndexError: list index out of range", 2) end
  local rest = text:sub(b + 1)
  local e = rest:find("}", 1, true)
  return e and rest:sub(1, e - 1) or rest
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
  local rules = source_rules()
  local composition = json.decode(read(DATA .. "/NativeCompositionAudit.json"))
  check(composition.frames == 75 and composition.transform_snapshots == 34995)
  check(G.all(composition.maximum_trs_error, function(v) return v < 2e-5 end))
  check(composition.reverse_order_error > 2e-5 and composition.base_reset_error > 0.01)
  check(util.equal(composition.layer_modes, { ["7"] = 1, ["8"] = 1, ["70"] = 1, ["73"] = 0, ["74"] = 0 }))
  local native = json.object()
  for _, card in ipairs(CARDS) do
    local stem = G.sanitize(card) .. "AnimatorRestart300.json"
    check(json.decode(read(DATA .. "/" .. stem))["error"] == "")
    local frames = json.decode(read(DATA .. "/" .. stem .. ".restart.json"))
    check(#frames == 15)
    local want = {}
    for _, v in ipairs({ { 7, 8 }, { 8, 70 }, { 7, 8, 73, 74 } }) do
      for _ = 1, 5 do want[#want + 1] = v end
    end
    check(util.equal(G.map(frames, function(f) return G.map(f.layers, function(l) return l.layer end) end), want))
    check(sorted_set_is(G.map(frames, function(f) return f.elapsed end), { .49, .61, .73, 1.07, 1.7 }))
    for _, f in ipairs(frames) do
      check(util.equal(f.triggerTimes, G.filter({ .61, 1.07 }, function(t) return t <= f.elapsed end)))
      check(f.baseLength > 0 and #f.bones >= 400)
      check(#f.bones == #f.baseBones and #f.baseBones == #f.zeroClockBones)
      check(math.abs(f.elapsed / f.baseLength - f.baseNormalizedTime) < 2e-5)
      for _, l in ipairs(f.layers) do
        check(#l.bones == #f.bones and #l.clips > 0 and util.truthy(l.started))
        if l.layer >= 73 then
          check(l.startTime == l.startPhase and l.startPhase == 0)
          check(math.abs(l.normalizedTime - f.baseNormalizedTime) < 2e-5)
        else
          local start, phase
          if #f.triggerTimes > 0 then start, phase = f.triggerTimes[#f.triggerTimes], 0
          else start, phase = l.startTime, l.startPhase end
          check(math.abs((f.elapsed - start) / l.length + phase - l.normalizedTime) < 2e-5)
        end
      end
    end
    local last = {}
    for i = math.max(1, #frames - 4), #frames do last[#last + 1] = frames[i] end
    check(G.all(last, function(f) return G.all(f.layers, function(l) return l.layer ~= 7 or l.weight == 0 end) end))
    native[card] = O("", digest(DATA .. "/" .. stem), ".restart.json", digest(DATA .. "/" .. stem .. ".restart.json"))
  end
  local rust = read(LOGS .. "/AnimatorRestart300RustAccepted.log")
  check(G.has(rust, "2 passed; 0 failed"))
  check(G.has(rust, "test anim::trigger_tests::native_trigger_restart ... ok"))
  check(G.has(rust, "test anim::trigger_tests::trigger_restart_session ... ok"))
  local maxima = G.search([=[accepted frames=75 attributes=20250.*?max=\[([^]]+)\]]=], rust)
  check(maxima and G.all(util.split(maxima[1], ","), function(v) return F(v) < 2e-5 end))
  local total = G.search([[Session accepted frames=75 bones=(\d+) attachment_vertices=(\d+) bone_max=([\deE.+-]+) attachment_max_m=([\deE.+-]+)]], rust)
  check(total and I(total[1]) == 45000 and I(total[2]) > 1000000 and F(total[3]) < 2e-5 and F(total[4]) < 2e-5)
  local unit = read(LOGS .. "/AnimatorRestart300Unit.log")
  check(G.has(unit, "trigger_metadata_keeps_priority_and_unsupported_guards ... ok") and G.has(unit, "1 passed; 0 failed"))
  local cache = read(ROOT .. "/Tools/ok-extract/src/sqlcache.rs")
  check(G.has(cache, "pub const CACHE_VER: i32 = 90;") and G.has(cache, "pub const SESSION_PIPELINE_VER: i32 = 141;"))
  for _, pair in ipairs({ { "Tools/ok-extract/src/ffi.rs", "pub struct OkRustBindings {" }, { "Tools/ok-extract/include/ok_extract.h", "typedef struct OkRustBindings {" } }) do
    local binding = struct_body(read(ROOT .. "/" .. pair[1]), pair[2])
    check(not G.has(binding, "ok_animation_trigger") and not G.has(binding, "ok_set_animation_trigger"))
  end
  check(G.has(read(LOGS .. "/AnimatorRestart300UnrealBuild.log"), "Result: Succeeded"))
  local target = read(LOGS .. "/AnimatorRestartUnrealAutomation300Console.log")
  local core = read(LOGS .. "/RestartRegressionUnrealAutomation300Console.log")
  local expected = { "AnimatorTriggerRestart", "AnimatorIndependentLayerClock", "AnimatorMultiLayer", "AnimatorAdditiveLayers", "Animator1DBlends", "Animator2DLayers", "AnimatorStateClock", "StudioSourceInitialization", "AnimatorMotionTimeScale" }
  check(not G.has(target .. core, "Result={Fail}"))
  check(G.set_eq(findall([[Result=\{Success\} Name=\{([^}]+)\}]], target .. "\n" .. core), expected))
  check(G.has(target, "Automation Test Queue Empty 1 tests performed.") and G.has(target, "RequestExitWithStatus(1, 0,"))
  check(G.has(core, "Automation Test Queue Empty 8 tests performed.") and G.has(core, "RequestExitWithStatus(1, 0,"))
  check(G.has(target, "Restart accepted cards=5 frames=75") and G.has(target, "invalid_triggers_rejected=225") and G.has(target, "events=90"))
  local regression_frames = json.object()
  for _, fam in ipairs({ { "Animator1D", 450 }, { "Animator2D", 300 }, { "Additive", 180 }, { "MotionTimeScale", 300 }, { "MultiLayer", 75 }, { "LayerClock", 100 }, { "AnimatorClock", 180 }, { "StudioSource", 135 } }) do
    local family, expected_frames = fam[1], fam[2]
    local count = G.sum(findall("LogAutomationController: " .. family .. [[ card=.*? frames=(\d+)]], core), I)
    check(count == expected_frames, family .. " " .. count)
    regression_frames[family] = count
  end
  local capture = read(LOGS .. "/StudioShots300_Layers7_8_73_open.log")
  check(not G.has(capture, "LogTemp: Error:") and G.has(capture, "RequestExitWithStatus(0, 0,"))
  local fits = findall([[capture fit view=\w+ .*?ndc_x=([\d.]+) ndc_y=([\d.]+) limit=0.92]], capture)
  check(#fits == 15 and G.all(fits, function(f) return math.max(F(f[1]), F(f[2])) <= .920001 end))
  local attachments = G.floats(findall([[max_vertex_error_cm=([\d.]+)]], capture))
  check(#attachments == 71 and G.max(attachments) == 0)
  local display = findall([[PV display audit passed=1 bindings=4 followers=(\d+) samples=(\d+) palette_error_cm=([\d.]+)]], capture)
  check(#display == 5 and G.all(display, function(d) return I(d[2]) == 8 * I(d[1]) and F(d[3]) < 1e-4 end))
  check(G.count(capture, "OK game animation scene mode=0") == 5 and G.count(capture, "Tracing Screenshot") == 15)
  local events = findall([[OK shot source trigger name=Restart elapsed=([\d.]+) base_clock_unchanged=1]], capture)
  local at = { .61, 1.07 }
  check(#events == 10 and G.all(G.map(events, function(t, i) return math.abs(F(t) - at[(i - 1) % 2 + 1]) < 1e-6 end), function(x) return x end))
  local actor = findall([[OK Animator trigger display audit passed=1 events=3 samples=3 actual_tick=1 clock_unchanged=1 weights_unchanged=1 changed_bones=(\d+) restored_parameters=1]], capture)
  check(#actor == 5 and G.all(actor, function(n) return I(n) > 0 end))
  for _, tag in ipairs({ "OK gaze display audit passed=1", "OK face time display audit passed=1", "OK eye highlight display audit passed=1", "OK eye look shot states=" }) do
    check(G.count(capture, tag) == 5)
  end
  local old = json.decode(read(LOGS .. "/EyeShader286Audit.json"))
  for n, h in pairs(old.captures.Tpose.sha256) do check(digest(ROOT .. "/Saved/Screenshots/" .. n) == h) end
  check(not path.exists(GAME .. "/BepInEx/plugins/OpenKoikatsu.GamePoseProbe.dll"))
  local runners = { "AnimatorRestart300NativeMarchRunner.log", "AnimatorRestart300NativeRemainingRunner.log", "AnimatorRestartUnrealAutomation300Runner.log", "RestartRegressionUnrealAutomation300Runner.log", "AnimatorRestart300ShotsRunner.log" }
  local pid_logs = { table.unpack(runners) }; pid_logs[#pid_logs + 1] = "AnimatorRestart300NativeWrongBranchFirst.log"
  local pid_list = {}
  for _, name in ipairs(pid_logs) do
    for _, pid in ipairs(findall([[PID (\d+)]], read(LOGS .. "/" .. name))) do pid_list[#pid_list + 1] = I(pid) end
  end
  local pids = G.unique(pid_list)
  for _, pid in ipairs(pids) do
    -- Only inspect the recorded owned IDs; unrelated user processes are allowed.
    local out = (ok.kit.call({ "proc-wait", pid, 0 }).path or ""):lower()
    check(not G.any({ "charastudio.exe", "unrealeditor.exe", "unrealeditor-cmd.exe" }, function(n) return G.has(out, n) end), pid .. " " .. out)
  end
  local paths = { "Tools/ok-extract/src/anim.rs", "Tools/ok-extract/src/anim/controller.rs", "Tools/ok-extract/src/anim/trigger_tests.rs", "Tools/ok-extract/src/session.rs", "Tools/ok-extract/src/ffi.rs", "Tools/ok-extract/include/ok_extract.h",
    "Tools/GamePoseProbe/Probe.cs", "old_ps1/Tools/GamePoseProbe/run_restart_native.ps1", "old_ps1/Tools/GamePoseProbe/run_restart.ps1", "Tools/GamePoseProbe/verify_restart.lua", "Tools/anim_export/verify_native_restart.lua",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/Tests/KKAnimatorTriggerRestart.spec.cpp",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/KKPreviewActor.cpp", "Plugins/KKUnityBundle/Source/KKUnityBundle/Public/KKPreviewActor.h",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/KKRustExtract.cpp", "Plugins/KKUnityBundle/Source/KKUnityBundle/Public/KKRustExtract.h",
    "Tools/ok-extract/src/sqlcache.rs", "Docs/UNREAL_ANIMATOR_TRIGGER_REENTRY.md", "Docs/UNREAL_ANIMATOR_LAYER_CLOCK.md", "Docs/UNREAL_ANIMATOR_MULTILAYER.md", "Docs/UNREAL_ANIMATION_CATALOG.md", "HANDOFF2.md", "Docs/ERROR_LOG.md" }
  local logs = { "AnimatorRestart300Unit.log", "AnimatorRestart300ProbeBuild.log" }
  for _, n in ipairs(runners) do logs[#logs + 1] = n end
  for _, n in ipairs({ "AnimatorRestart300NativeComposition.log", "AnimatorRestart300RustAccepted.log", "AnimatorRestart300UnrealBuild.log", "AnimatorRestartUnrealAutomation300Console.log", "RestartRegressionUnrealAutomation300Console.log", "StudioShots300_Layers7_8_73_open.log" }) do logs[#logs + 1] = n end
  local regression_sum = 0
  for _, v in pairs(regression_frames) do regression_sum = regression_sum + v end
  local audit = O("stage", 300, "accepted", true, "sources", G.digests(ROOT, paths), "logs", G.digests(LOGS, logs),
    "binaries", G.digests(ROOT, { "Plugins/KKUnityBundle/Binaries/Win64/UnrealEditor-KKUnityBundle.dll", "Tools/ok-extract/target/release/ok_extract.lib", "Tools/GamePoseProbe/OpenKoikatsu.GamePoseProbe.restart300.last-run.dll" }),
    "historical_logs", G.digests(LOGS, { "AnimatorRestart300NativeWrongBranchFirst.log", "AnimatorRestart300NativeCompositionPartial.log", "AnimatorRestart300RustMarch.log", "AnimatorRestart300UnrealBuildFirstFailed.log" }),
    "native", native, "source_trigger_rules", rules, "independent_native_composition", composition, "composition_audit_sha256", digest(DATA .. "/NativeCompositionAudit.json"),
    "native_frames", 75, "native_attributes", 20250, "native_maxima", A(G.floats(util.split(maxima[1], ","))),
    "session", O("frames", 75, "bones", I(total[1]), "attachment_vertices", I(total[2]), "max_bone", F(total[3]), "max_vertex", F(total[4])),
    "unreal_tests", A(util.sorted(expected)), "unreal_source_frames", 75 + regression_sum, "unreal_regression_frames", regression_frames, "actor_tick_samples", 15, "source_capture_events", 10, "fixture_trigger_events", 90, "owned_pids_exited", A(util.sorted(pids)),
    "captures", O("images", 15, "attachment_audits", 71, "fits", 15, "follower_samples", G.sum(display, function(d) return I(d[2]) end), "max_palette_error_cm", G.max(display, function(d) return F(d[3]) end), "contact_sheet_sha256", digest(DATA .. "/ContactSheet.png"), "readme_sha256", digest(SHOTS .. "/README.md"), "sha256", G.sorted_object(images, function(n) return digest(SHOTS .. "/" .. n) end)), "tpose286_root_unchanged", true)
  G.write_json(LOGS .. "/AnimatorRestart300Audit.json", audit, true)
  print("AnimatorRestart300 accepted: native 75 frames/20250 attributes; Session 75 frames; UE 9 tests; Actor.Tick 15 samples; source events 10; RHI 15 images; Tpose286 unchanged")
end

if ok.is_main() then ok.main(main) end
