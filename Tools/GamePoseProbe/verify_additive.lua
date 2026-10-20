-- Stage297 authored additive layers, native Animator, Session and RHI gates.
local ok = require "ok"
local G = require "gpp"
local VA = require "verify_animator"
local json, util = ok.json, ok.util
local O, A, F, I, check, findall = G.O, G.A, G.F, G.I, G.check, G.findall
local CARDS, digest, read = VA.CARDS, VA.digest, VA.read

local ROOT = G.ROOT
local LOGS = ROOT .. "/Saved/Logs"
local DATA = ROOT .. "/Saved/Tests/AnimatorAdditive297"
local SHOTS = ROOT .. "/Saved/DebugShots/AnimatorAdditive297_Layer8_open"
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
    sheet:text(8, r * 384 + 6, card .. " / source additive layer 8 weight .37; elapsed .4")
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
  local source = json.decode(read(DATA .. "/SourceAudit.json"))
  check(source.controllers == 437 and #source.layers == 526)
  check(util.equal(source.counts, { mode0 = 1420, timing_true = 0, active_extra = 1, mode1 = 526 }))
  check(G.all(source.layers, function(x) return #x.states == 1 and #x.states[1].nodes == 1 and x.motion_set == 0 end))
  check(G.all(source.layers, function(x) return util.truthy(x.states[1].nodes[1].reference_valid) and x.states[1].nodes[1].reference_count == 0 end))
  for _, r in ipairs(source.sources) do
    check(digest(GAME .. "/" .. r.source) == r.sha256)
  end
  local composition = json.decode(read(DATA .. "/NativeCompositionAudit.json"))
  check(composition.frames == 180 and composition.native_transform_snapshots > 80000)
  check(G.all(composition.maximum_trs_error, function(e) return e < 2e-5 end))
  check(composition.wrong_algorithm_maximum.slerp > 1e-4)
  check(composition.wrong_algorithm_maximum.nlerp > 1e-4)
  check(composition.wrong_algorithm_maximum.absolute_override > 1e-3)
  local native = json.object()
  for _, card in ipairs(CARDS) do
    local stem = G.sanitize(card) .. "AnimatorAdditive297.json"
    check(json.decode(read(DATA .. "/" .. stem))["error"] == "")
    local frames = json.decode(read(DATA .. "/" .. stem .. ".additive.json"))
    check(#frames == 36)
    for _, n in ipairs({ 7, 8, 70 }) do
      check(G.sum(frames, function(f) return f.layer == n and 1 or 0 end) == 12)
    end
    check(G.all(frames, function(f) return #f.bones >= 400 and #f.clips > 0 and f.length > 0 end))
    check(sorted_set_is(G.map(frames, function(f) return f.weight end), { 0, .37, 1 }))
    check(sorted_set_is(G.map(frames, function(f) return f.elapsed end), { 0, .17, .61, 1.7 }))
    check(G.all(frames, function(f) return #f.baseBones == #f.bones and #f.bones == #f.rawBones and #f.rawBones == #f.referenceBones end))
    native[card] = O("", digest(DATA .. "/" .. stem), ".additive.json", digest(DATA .. "/" .. stem .. ".additive.json"))
  end
  local rust = read(LOGS .. "/AnimatorAdditive297RustAccepted.log")
  check(G.has(rust, "2 passed; 0 failed"))
  check(G.has(rust, "test anim::additive_tests::native_additive_layers ... ok"))
  check(G.has(rust, "test anim::additive_tests::additive_session ... ok"))
  check(G.has(rust, "frames=180 attributes=48600"))
  local maxima = G.search([=[accepted frames=180 attributes=48600.*?max=\[([^]]+)\]]=], rust)
  check(maxima and G.all(util.split(maxima[1], ","), function(v) return F(v) < 2e-5 end))
  local session = rust
  check(G.count(session, "Session card=") == 5)
  local total = G.search([[Session accepted frames=180 bones=(\d+) attachment_vertices=(\d+) bone_max=([\deE.+-]+) attachment_max_m=([\deE.+-]+)]], session)
  check(total and I(total[1]) == 108000 and I(total[2]) > 1000000)
  check(F(total[3]) < 2e-5 and F(total[4]) < 2e-5)
  local unit = read(LOGS .. "/AnimatorAdditive297Unit.log")
  check(G.has(unit, "additive_weight_is_not_slerp_or_linear_quaternion ... ok") and G.has(unit, "2 passed; 0 failed"))
  check(G.has(unit, "additive_reference_metadata_rejects_partial_and_nonfinite_arrays ... ok"))
  check(G.has(read(LOGS .. "/AnimatorAdditive297UnrealBuildAccepted.log"), "Result: Succeeded"))
  local target = read(LOGS .. "/AnimatorAdditiveUnrealAutomation297Console.log")
  local core = read(LOGS .. "/AdditiveRegressionUnrealAutomation297Console.log")
  local expected = { "Animator1DBlends", "Animator2DLayers", "AnimatorStateClock", "StudioSourceInitialization", "AnimatorMotionTimeScale", "AnimatorAdditiveLayers" }
  check(not G.has(core, "Result={Fail}"))
  check(not G.has(target, "Result={Fail}"))
  check(G.set_eq(findall([[Result=\{Success\} Name=\{([^}]+)\}]], core .. "\n" .. target), expected))
  check(G.has(core, "Automation Test Queue Empty 5 tests performed.") and G.has(core, "RequestExitWithStatus(1, 0,"))
  check(G.has(target, "Automation Test Queue Empty 1 tests performed.") and G.has(target, "RequestExitWithStatus(1, 0,"))
  check(G.has(target, "Additive accepted cards=5 frames=180") and G.has(target, "invalid_weights_rejected=360"))
  local capture = read(LOGS .. "/StudioShots297_Layer8_open.log")
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
    "Tools/ok-extract/src/anim/additive_tests.rs", "Tools/GamePoseProbe/Probe.cs",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/Tests/KKAnimatorAdditive.spec.cpp",
    "Tools/anim_export/inspect_additive_layers.lua", "Tools/anim_export/verify_native_additive.lua", "old_ps1/Tools/GamePoseProbe/run_additive.ps1",
    "old_ps1/Tools/GamePoseProbe/run_additive_native.ps1", "Tools/GamePoseProbe/verify_additive.lua",
    "Docs/UNREAL_ANIMATOR_ADDITIVE.md", "HANDOFF2.md", "Docs/ERROR_LOG.md" }
  local logs = { "AnimatorAdditive297ProbeBuild.log", "AnimatorAdditive297NativeComposition.log", "AnimatorAdditive297NativeMarchRunner.log",
    "AnimatorAdditive297NativeRemainingRunner.log", "AnimatorAdditive297RustMarchFirst.log",
    "AnimatorAdditive297RustMarchFirstComparison.log", "AnimatorAdditive297RustMarchSecondComparison.log",
    "AnimatorAdditive297RustMarchAccepted.log", "AnimatorAdditive297RustAccepted.log", "AnimatorAdditive297Unit.log",
    "AnimatorAdditive297UnrealBuildAccepted.log", "AnimatorAdditiveUnrealAutomation297Console.log", "AdditiveRegressionUnrealAutomation297Console.log", "StudioShots297_Layer8_open.log" }
  local audit = O("stage", 297, "accepted", true, "source_controllers", 437, "source_additive_layers", 526,
    "sources", G.digests(ROOT, paths), "logs", G.digests(LOGS, logs),
    "binaries", G.digests(ROOT, { "Plugins/KKUnityBundle/Binaries/Win64/UnrealEditor-KKUnityBundle.dll",
      "Tools/ok-extract/target/release/ok_extract.lib", "Tools/GamePoseProbe/OpenKoikatsu.GamePoseProbe.additive297.last-run.dll" }),
    "source_audit_sha256", digest(DATA .. "/SourceAudit.json"), "native", native,
    "independent_native_composition", composition, "composition_audit_sha256", digest(DATA .. "/NativeCompositionAudit.json"),
    "native_frames", 180, "native_attributes", 48600, "native_maxima", A(G.floats(util.split(maxima[1], ","))),
    "session", O("frames", 180, "bones", I(total[1]), "attachment_vertices", I(total[2]), "max_bone", F(total[3]), "max_vertex", F(total[4])),
    "unreal_tests", A(util.sorted(expected)), "captures", O("images", 15, "attachment_audits", 71, "fits", 15,
      "scene", "source Auto; H/esthetic", "follower_samples", G.sum(display, function(d) return I(d[2]) end),
      "max_palette_error_cm", G.max(display, function(d) return F(d[3]) end),
      "contact_sheet_sha256", digest(DATA .. "/ContactSheet.png"), "sha256", G.sorted_object(images, function(n) return digest(SHOTS .. "/" .. n) end)),
    "tpose286_root_unchanged", true, "historical_failed_logs", A({ "AnimatorAdditive297RustMarchFirst.log", "AnimatorAdditive297RustMarchFirstComparison.log", "AnimatorAdditive297RustMarchSecondComparison.log" }))
  G.write_json(LOGS .. "/AnimatorAdditive297Audit.json", audit, true)
  print("AnimatorAdditive297 accepted: 526 authored layers; native 180 frames/48600 attributes; Session 180 frames; UE 6 tests; RHI 15 images; Tpose286 unchanged")
end

if ok.is_main() then ok.main(main) end
