-- Stage285 native EyeLookCalc, actual rig loading, UE playback/display gates.
local ok = require "ok"
local G = require "gpp"
local json, path = ok.json, ok.path
local O, F, I, check, findall = G.O, G.F, G.I, G.check, G.findall
local EC = require "verify_eye_clamp"
local ROOT, LOGS, CARDS, read, verify = EC.ROOT, EC.LOGS, EC.CARDS, EC.read, EC.verify
local sources = require("verify_eye_gaze").sources

local function main()
  local heads, rig = sources()
  local folder = ROOT .. "/Saved/Tests/EyeLook285"
  local native = json.object()
  for _, p in ipairs(G.glob_sorted(folder, "*.json.eye-look.json")) do
    local completion = p:sub(1, #p - #".eye-look.json")
    check(json.decode(read(completion))["error"] == "")
    local data = json.decode(read(p))
    check(#data.config.patterns == 6 and #data.frames == 144)
    check(data.weighted_eye_slots == 0 and data.renderers > 20 and data.bone_slots > 100)
    check(G.set_eq(G.map(data.frames, function(f) return f.pattern end), ok.util.range(6)))
    check(G.any(data.frames, function(f) return not f.enabled end))
    check(G.all(data.frames, function(f) return #f.center == 16 and #f.rotations == 8 end))
    native[path.name(p)] = O("frames", 144, "renderers", data.renderers, "bone_slots", data.bone_slots,
      "weighted_eye_slots", 0, "sha256", G.digest(p))
  end
  check(G.set_len(native) == 5)
  local unit = read(LOGS .. "/EyeLookRust285Final.log")
  check(G.has(unit, "native EyeLookCalc 5 cards 720 frames") and G.has(unit, "1 passed; 0 failed"))
  local basis = read(LOGS .. "/EyeLookRust285BasisAccepted.log")
  check(G.has(basis, "posed world basis: 5 cards") and G.has(basis, "1 passed; 0 failed"))
  local result = verify({ prefix = "EyeLook", stage = 285, pipeline = 137, build_log = "EyeLookUnrealBuild285Accepted.log",
    extra_tests = { "NativeFaceControls", "NativeFaceTime", "NativeEyeGaze", "EyeLookPlayback" },
    unit_log = "EyeLookRust285Final.log", geometry_log = "EyeLookGeometryRust285Accepted.log",
    shots_log = "EyeLookShots285_open.log", shots_folder = "Saved/DebugShots/EyeLook285_open", save_audit = false })
  local automation = read(LOGS .. "/EyeLookUnrealAutomation285Console.log")
  check(G.has(automation, "EyeLookPlayback verified 5 cards 120 animated state steps"))
  check(G.has(automation, "channel_comparisons=210800 uv_comparisons=143200"))
  local shots = read(LOGS .. "/EyeLookShots285_open.log")
  local target_rates = findall([[OK eye look shot states=6 rates=\(([-\d.]+),([-\d.]+),([-\d.]+)\) paused_hold=1 clock_unchanged=1]], shots)
  check(#target_rates == 5)
  local gaze = findall([[OK gaze display audit passed=1 iris_parts=2 changed_uvs=(\d+) vertices=(\d+) restore_position_cm=([\d.]+) restore_uv=([\d.]+)]], shots)
  check(#gaze == 5 and G.all(gaze, function(r) return I(r[1]) == 358 and F(r[3]) == 0 and F(r[4]) == 0 end))
  local blink = findall([[OK blink shot state=open open_rate=([\d.]+) eyes=([\d.]+) brow=([\d.]+)]], shots)
  check(#blink == 5)
  for i = 1, math.min(#CARDS, #target_rates, #blink) do
    local gaze_rates, rates = target_rates[i], blink[i]
    local h, v = F(gaze_rates[1]), F(gaze_rates[3])
    check(G.all(gaze_rates, function(x) return math.abs(F(x)) <= 1 end))
    local s = heads[CARDS[i]].settings
    local delta = (v > 0 and s[9] * (v ^ .5) or -s[10] * ((-v) ^ .5)) - s[11] * (math.abs(h) ^ .5)
    delta = math.max(-math.max(s[10], s[11]), math.min(s[9], delta))
    local expected = math.min(1 - s[9], .92) + delta * .92
    check(math.abs(F(rates[2]) - expected) < 3e-6 and F(rates[1]) == 1 and F(rates[3]) == 1)
  end
  local gallery = read(LOGS .. "/EyeLookGallery285.log")
  check(G.has(gallery, "KK eye look gallery verified camera_tracking=1 clock_unchanged=1"))
  check(G.has(gallery, "RequestExitWithStatus(0, 0,") and not G.has(gallery, "LogTemp: Error:"))
  check(G.size_is(ROOT .. "/Saved/DebugShots/EyeLook285UI/gallery_imported_studio.png", 1440, 1000))
  local paths = {}
  for _, name in ipairs({ "lib.rs", "unity.rs", "eye_look.rs", "eye_gaze.rs", "ffi.rs", "session.rs", "sqlcache.rs" }) do paths[#paths + 1] = ROOT .. "/Tools/ok-extract/src/" .. name end
  for _, p in ipairs({ "Tools/ok-extract/include/ok_extract.h",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Public/KKPreviewActor.h",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/KKPreviewActor.cpp",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/Tests/KKFaceBlendShape.spec.cpp",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Public/KKRustExtract.h",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/KKRustExtract.cpp",
    "Source/OpenKoikatsu/OKGalleryWidget.cpp", "Source/OpenKoikatsu/OKGalleryWidget.h" }) do paths[#paths + 1] = ROOT .. "/" .. p end
  local build_time = G.mtime(LOGS .. "/EyeLookUnrealBuild285Accepted.log")
  check(G.all(paths, function(p) return G.mtime(p) <= build_time end))
  check(G.mtime(LOGS .. "/EyeLookUnrealAutomation285Console.log") > build_time)
  check(G.mtime(LOGS .. "/EyeLookShots285_open.log") > build_time)
  check(G.mtime(LOGS .. "/EyeLookGallery285.log") > build_time)
  check(not G.exists(ok.game_root(true) .. "/BepInEx/plugins/OpenKoikatsu.GamePoseProbe.dll"))
  local hashes = json.object()
  for _, p in ipairs(paths) do hashes[G.rel(p)] = G.digest(p) end
  result.native_eye_look = native
  result.selected_heads = heads
  result.source_rig = rig
  result.animated_gaze_steps = 120
  result.target_rates = G.rows(target_rates)
  result.gallery_camera_tracking = true
  result.source_sha256 = hashes
  result.limitations = "Five source/mod cards and default CPU-head display verified. Separate GPU-head, full iris/highlight shader pixels, tear UI and Animator graph work remains; chin deferred."
  G.write_json(LOGS .. "/EyeLook285Audit.json", result)
  local sheet = G.sheet(900, 1920, { 0x30, 0x30, 0x30 })
  for row, card in ipairs(CARDS) do
    local y = (row - 1) * 384
    sheet:text(8, y + 6, card .. " : world target / full / face / side")
    for col, view in ipairs({ "3d", "face", "face_side" }) do
      sheet:paste(ROOT .. "/Saved/DebugShots/EyeLook285_open/" .. card .. "_outfit0_" .. view .. ".png", (col - 1) * 300, y + 24, 300, 360)
    end
  end
  sheet:save(folder .. "/ContactSheet.png")
  print(json.encode(result, { ensure_ascii = false }))
end

if ok.is_main() then ok.main(main) end
