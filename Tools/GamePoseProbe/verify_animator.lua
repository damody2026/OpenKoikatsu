-- Stage289: native 1D/nested motion controls, UE playback and real gallery callbacks.
local ok = require "ok"
local G = require "gpp"
local json, path, util = ok.json, ok.path, ok.util
local O, F, I, check, findall = G.O, G.F, G.I, G.check, G.findall

local M = {}
local ROOT = G.ROOT
local LOGS = ROOT .. "/Saved/Logs"
local DATA = ROOT .. "/Saved/Tests/Animator289"
local SHOTS = ROOT .. "/Saved/DebugShots/Animator289_Walk_open"
M.CARDS = { "[Card] March_7th_keiyuu", "Alice_keiyuu",
  "Hayase_yuuka_keiyuu_V1_02 For KKS", "KK_091207", "KK_384283" }
local CARDS = M.CARDS
local STATE_ORDER = { "Base Layer.Main.Locomotion_Anger", "Base Layer.Twoshot.Twoshot_00", "Base Layer.Lesbian.Lesbian3" }
local STATES = { ["Base Layer.Main.Locomotion_Anger"] = "Speed",
  ["Base Layer.Twoshot.Twoshot_00"] = "height",
  ["Base Layer.Lesbian.Lesbian3"] = "height1" }
M.TESTS = { "HandShapes", "ImportedData", "Playback", "Animator1DBlends",
  "BodyDynamics", "ExternalBodyDynamics", "ExternalSparseTracks",
  "NativeWeights", "SharedDeformation", "NativeFaceControls",
  "NativeFaceTime", "NativeEyeGaze", "EyeLookPlayback" }

function M.read(p) return G.read(p) end
function M.digest(p) return G.digest(p) end
local read, digest = M.read, M.digest

local function contact_sheet()
  local expected = {}
  for _, card in ipairs(CARDS) do
    for _, view in ipairs({ "3d", "face", "face_side" }) do expected[#expected + 1] = card .. "_outfit0_" .. view .. ".png" end
  end
  check(G.set_eq(G.glob_names(SHOTS, "*.png"), expected))
  local sheet = G.sheet(900, 1920, { 0x30, 0x30, 0x30 })
  for row, card in ipairs(CARDS) do
    local r = row - 1
    sheet:text(8, r * 384 + 6, card .. " / source 1D Speed=0.37")
    for col, view in ipairs({ "3d", "face", "face_side" }) do
      local file = SHOTS .. "/" .. card .. "_outfit0_" .. view .. ".png"
      check(G.size_is(file, 900, 1080))
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
  local images = contact_sheet()
  if args.contact_only then
    print("Contact sheet: 15 images, five cards")
    return
  end
  local native = json.object()
  local coverage = {}
  for _, s in ipairs(STATE_ORDER) do
    for _, v in ipairs({ 0, .25, .5, .75, 1 }) do
      for _, ph in ipairs({ 0, .17, .43, .91 }) do coverage[#coverage + 1] = { s, v, ph } end
    end
  end
  for _, card in ipairs(CARDS) do
    local stem = G.sanitize(card) .. "Animator289.json"
    local completion = DATA .. "/" .. stem
    local fpath = DATA .. "/" .. stem .. ".animator-blend.json"
    check(json.decode(read(completion))["error"] == "")
    local frames = json.decode(read(fpath))
    check(#frames == 60)
    local actual = {}
    for _, f in ipairs(frames) do actual[#actual + 1] = { f.state, util.round(f.value, 2), util.round(f.phase, 2) } end
    check(G.set_eq(actual, coverage))
    local transform_count = #frames[1].bones
    check(transform_count >= 450)
    for _, f in ipairs(frames) do
      check(f.parameter == STATES[f.state])
      check(G.isfinite(f.length) and f.length > 0)
      check(f.speed > 0 and f.speedMultiplier == 1)
      if f.parameter == "height1" then
        check(f.parameter2 == "height" and math.abs(f.value2 - .37) < 1e-6)
      end
      check(#f.clips > 0 and math.abs(G.sum(f.clips, function(c) return c.weight end) - 1) < 1e-5)
      check(G.all(f.clips, function(c) return c.length > 0 and G.isfinite(c.length)
        and G.isfinite(c.weight) and c.weight >= 0 end))
      check(#f.bones == transform_count)
      check(#G.unique(f.bones, function(b) return b.name end) == transform_count)
      check(G.all(f.bones, function(b)
        for _, field in ipairs({ "position", "rotation", "scale" }) do
          for _, v in pairs(b[field]) do if not G.isfinite(v) then return false end end
        end
        return true
      end))
    end
    native[card] = O("frames", 60, "unique_transforms_per_frame", transform_count,
      "sha256", digest(fpath), "completion_sha256", digest(completion))
  end
  local controller = json.decode(read(DATA .. "/ControllerAudit.json"))
  check(#controller.controllers == 139)
  check(G.sum(controller.controllers, function(c) return #c.states end) == 805)
  local bt = controller.blend_types
  check(G.set_len(bt) == 2 and bt["0"] == 907 and bt["3"] == 168)

  local rust = read(LOGS .. "/AnimatorRust289AllAccepted.log")
  check(G.has(rust, "1 passed; 0 failed"))
  local rows = findall([[Animator1D card=(.+?) frames=60 transform_samples=(\d+) position_max_m=([\d.]+) rotation_max=([\d.]+) scale_max=([\d.]+)]], rust)
  check(G.set_eq(G.map(rows, function(r) return r[1] end), CARDS) and #rows == 5)
  check(G.all(rows, function(r) return I(r[2]) == 16200 and math.max(F(r[3]), F(r[4]), F(r[5])) < 2e-5 end))
  check(G.has(rust, "accepted cards=5 frames=300 transform_samples=81000"))
  local regression = json.object()
  for _, name in ipairs({ "Controller", "Catalogue" }) do
    local text = read(LOGS .. "/AnimatorRust289" .. name .. "Regression.log")
    local match = G.search([[test result: ok\. (\d+) passed; 0 failed]], text)
    check(match and I(match[1]) > 0)
    regression[name] = I(match[1])
  end
  check(G.has(read(LOGS .. "/AnimatorUnrealBuild289UIAccepted.log"), "Result: Succeeded"))
  check(G.has(read(LOGS .. "/AnimatorUnrealBuild289BackdropAccepted.log"), "Result: Succeeded"))

  local tests = read(LOGS .. "/AnimatorUnrealAutomation289Console.log")
  check(not G.has(tests, "Result={Fail}"))
  check(G.set_eq(findall([[Test Completed\. Result=\{Success\} Name=\{([^}]+)\}]], tests), M.TESTS))
  check(G.count(tests, "Test Completed. Result={Success}") == 13)
  check(G.has(tests, "Automation Test Queue Empty 13 tests performed"))
  check(G.has(tests, "RequestExitWithStatus(1, 0,"))
  local finite = findall([[Animator1D card=(.+?) states=3 frames=90 finite=1]], tests)
  check(#finite == 5 and G.set_eq(finite, CARDS))
  for _, label in ipairs({ "BodyDynamics", "ExternalBody" }) do
    local controls = findall(label .. [[ card=(.+?) frames=180 .*?changed=(\d+) max_delta_cm=([\d.]+) max_rotation_rad=([\d.]+)]], tests)
    check(#controls == 5 and G.set_eq(G.map(controls, function(c) return c[1] end), CARDS))
    check(G.all(controls, function(c) return I(c[2]) > 0 and F(c[3]) > 1e-3 and F(c[4]) > 1e-4 end))
  end
  check(G.has(tests, "preserved_missing=1 coordinate_roundtrips=2 invalid_clip_rejected=1"))

  local capture = read(LOGS .. "/AnimatorShots289_Walk_open.log")
  check(G.has(capture, "RequestExitWithStatus(0, 0,") and not G.has(capture, "LogTemp: Error:"))
  check(not G.has(capture, "Failed to compile Material"))
  check(G.has(capture:gsub("\\", "/"), SHOTS))
  check(G.count(capture, "Tracing Screenshot") == 15)
  check(G.count(capture, "OK shot animation float Speed=0.37") == 5)
  check(G.count(capture, "OK game animation scene mode=4") == 5)
  check(G.count(capture, "clip=states:action/animator/00.unity3d|base / Base Layer.Main.Locomotion_Anger") == 5)
  local attachments = G.floats(findall([[max_vertex_error_cm=([\d.]+)]], capture))
  check(#attachments == 71 and G.max(attachments) == 0)
  local fits = findall([[capture fit view=\w+ .*?ndc_x=([\d.]+) ndc_y=([\d.]+) limit=0.92]], capture)
  check(#fits == 15 and G.all(fits, function(f) return math.max(F(f[1]), F(f[2])) <= .920001 end))
  check(G.count(capture, "OK gaze display audit passed=1") == 5)
  check(G.count(capture, "OK eye highlight display audit passed=1") == 5)
  for _, card in ipairs(CARDS) do
    local front = G.load_rgb(SHOTS .. "/" .. card .. "_outfit0_3d.png")
    local neutral = { front:get(0, 0) }
    check(120 <= math.min(table.unpack(neutral)) and math.max(table.unpack(neutral)) <= 135)
    local im = G.load_rgb(SHOTS .. "/" .. card .. "_outfit0_face_side.png")
    check(G.top_row_neutral(im, neutral), card .. ": profile backdrop differs from the front studio background")
  end

  local gallery = read(LOGS .. "/AnimatorGallery289.log")
  check(not G.has(gallery, "LogTemp: Error:") and G.has(gallery, "RequestExitWithStatus(0, 0,"))
  check(G.has(gallery, "KK animator gallery verified source_callback=1 state_callback=1 slider_callback=1 value=0.37 phase_preserved=1 face_clock_unchanged=1"))
  local ui = ROOT .. "/Saved/DebugShots/Animator289UI/gallery_imported_studio.png"
  check(G.size_is(ui, 1440, 1000))
  local sources = { "Tools/ok-extract/src/anim.rs", "Tools/ok-extract/src/anim/controller.rs",
    "Tools/ok-extract/src/anim/graph_tests.rs", "Tools/ok-extract/src/session.rs",
    "Tools/ok-extract/src/ffi.rs", "Tools/ok-extract/src/lib.rs", "Tools/ok-extract/include/ok_extract.h",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/KKPreviewActor.cpp",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/KKRustExtract.cpp",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/Tests/KKAnimatorBlend.spec.cpp",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Public/KKPreviewActor.h",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Public/KKRustExtract.h",
    "Source/OpenKoikatsu/OKGalleryWidget.cpp", "Source/OpenKoikatsu/OKGalleryWidget.h",
    "Source/OpenKoikatsu/OKShapeSlider.cpp", "Source/OpenKoikatsu/OKShapeSlider.h",
    "Tools/GamePoseProbe/Probe.cs", "old_ps1/Tools/GamePoseProbe/run_animator_native.ps1",
    "old_ps1/Tools/GamePoseProbe/run_animator.ps1", "Tools/GamePoseProbe/verify_animator.lua",
    "Tools/anim_export/inspect_controller_graph.lua" }
  local logs = { "AnimatorRust289AllAccepted.log", "AnimatorRust289ControllerRegression.log",
    "AnimatorRust289CatalogueRegression.log", "AnimatorUnrealBuild289UIAccepted.log",
    "AnimatorUnrealAutomation289Console.log", "AnimatorShots289_Walk_open.log",
    "AnimatorGallery289.log", "AnimatorNative289All.log", "AnimatorUnrealBuild289BackdropAccepted.log" }
  local report = O("stage", 289, "cards", 5, "native_frames", 300, "animated_trs_samples", 81000,
    "native_controls", native, "native_comparisons", G.rows(rows), "rust_regression", regression,
    "controller_audit_sha256", digest(DATA .. "/ControllerAudit.json"), "unreal_tests", 13,
    "unreal_1d_frames", 450, "source_physics_negative_frames", 900,
    "external_physics_negative_frames", 900, "images", 15, "attachments", 71,
    "attachment_max_error_cm", 0, "gallery_actual_callbacks", 3,
    "gallery_sha256", digest(ui), "image_sha256", G.sorted_object(images, function(n) return digest(SHOTS .. "/" .. n) end),
    "source_sha256", G.digests(ROOT, sources),
    "log_sha256", G.digests(LOGS, logs),
    "limits", "1D and nested 1D explicit state playback. Automatic transitions/CrossFade, 2D, mirror, foot IK, additional layers and dynamic state-time parameters remain unsupported; chin deferred. Native parity compares animated local TRS before source helpers/physics, not final pixel equivalence.")
  G.write_json(LOGS .. "/Animator289Audit.json", report)
  print("PASS: five cards; 300 native frames, 81000 animated TRS samples; 13 UE tests, 450 blend frames; 15 RHI images; 71 attachments; three actual gallery callbacks")
end

if ok.is_main() then ok.main(main) end
return M
