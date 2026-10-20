-- Stage287: source solver, negative control and Unreal's actual GPU followers.
local ok = require "ok"
local G = require "gpp"
local json, util = ok.json, ok.util
local O, F, I, check, findall = G.O, G.F, G.I, G.check, G.findall
local EC = require "verify_eye_clamp"
local ROOT, LOGS, CARDS, read = EC.ROOT, EC.LOGS, EC.CARDS, EC.read

local digest = G.digest

local function main()
  local native = read(LOGS .. "/BodyDynamicsNative287HalfRate.log")
  check(G.has(native, "1 passed; 0 failed"))
  local errors = findall([[Ver02 game (\w+) local position ([\d.]+) m, rotation ([\d.]+)]], native)
  check(#errors == 10)
  check(G.max(errors, function(e) return F(e[2]) end) < 2e-6)
  check(G.max(errors, function(e) return F(e[3]) end) < 2e-5)
  local tests = read(LOGS .. "/BodyDynamicsUnrealAutomation287Console.log")
  check(G.has(tests, "Result={Success} Name={BodyDynamics}") and not G.has(tests, "Result={Fail}"))
  check(G.has(tests, "Automation Test Queue Empty 1 tests performed"))
  local positive = findall([[BodyDynamics card=(.+?) frames=180 joints=6 changed=(\d+) max_delta_cm=([\d.]+) max_rotation_rad=([\d.]+)]], tests)
  check(#positive == 5 and G.set_eq(G.map(positive, function(p) return p[1] end), CARDS))
  check(G.all(positive, function(p) return I(p[2]) > 0 and F(p[3]) > 1e-3 and F(p[4]) > 1e-4 end))
  check(G.has(read(LOGS .. "/BodyDynamicsUnrealBuild287Accepted.log"), "Result: Succeeded"))

  local log = read(LOGS .. "/BodyDynamicsShots287_Walk_open.log")
  check(G.has(log, "RequestExitWithStatus(0, 0,") and not G.has(log, "LogTemp: Error:"))
  check(not G.has(log, "Failed to compile Material"))
  local audits = findall([[OK body dynamics display audit passed=(\d) frames=180 joints=(\d+) followers=(\d+) samples=(\d+) motion_cm=([\d.]+) palette_error_cm=([\d.]+)]], log)
  check(#audits == 5)
  check(G.all(audits, function(a) return a[1] == "1" and a[2] == "6" and I(a[3]) > 0 and I(a[4]) == 180 * 6 * I(a[3]) and F(a[5]) > 1e-4 and F(a[6]) == 0 end))
  check(G.count(log, "OK game animation clip=states:studio/anime/00.unity3d|walk_run / Base Layer.f_aruki_00_00_01") == 10)
  check(G.count(log, "Tracing Screenshot") == 15)
  local attachments = G.floats(findall([[max_vertex_error_cm=([\d.]+)]], log))
  check(#attachments == 71 and G.max(attachments) <= 1e-6)

  local root = ROOT .. "/Saved/Tests/BodyDynamics287"
  local traces = json.object()
  local bones = {}
  for j = 1, 3 do for _, s in ipairs({ "L", "R" }) do bones[#bones + 1] = string.format("cf_j_bust%02d_%s", j, s) end end
  for _, card in ipairs(CARDS) do
    local p = root .. "/" .. card .. ".display.tsv"
    local rows = ok.csv.parse_dicts(ok.path.read_text(p), { delimiter = "\t" })
    check(#rows == 1080)
    check(G.set_eq(G.map(rows, function(r) return I(r.frame) end), util.range(180)))
    check(G.set_eq(G.map(rows, function(r) return r.bone end), bones))
    check(G.all(rows, function(r)
      for _, k in ipairs({ "phase", "x_cm", "y_cm", "z_cm", "qx", "qy", "qz", "qw" }) do if not G.isfinite(F(r[k])) then return false end end
      return true
    end))
    traces[card] = O("rows", #rows, "sha256", digest(p))
  end
  local shots = ROOT .. "/Saved/DebugShots/BodyDynamics287_Walk_open"
  local expected = {}
  for _, card in ipairs(CARDS) do for _, view in ipairs({ "3d", "face", "face_side" }) do expected[#expected + 1] = card .. "_outfit0_" .. view .. ".png" end end
  check(G.set_eq(G.glob_names(shots, "*.png"), expected))
  check(G.all(expected, function(name) return G.size_is(shots .. "/" .. name, 900, 1080) end))
  local sheet = G.sheet(900, 1920, { 0x30, 0x30, 0x30 })
  for row, card in ipairs(CARDS) do
    sheet:text(8, (row - 1) * 384 + 6, card .. " / source walk + physics")
    for column, view in ipairs({ "3d", "face", "face_side" }) do
      sheet:paste(shots .. "/" .. card .. "_outfit0_" .. view .. ".png", (column - 1) * 300, (row - 1) * 384 + 24, 300, 360)
    end
  end
  sheet:save(root .. "/ContactSheet.png")
  check(not G.exists(ok.game_root(true) .. "/BepInEx/plugins/OpenKoikatsu.GamePoseProbe.dll"))
  local sources = { "Tools/ok-extract/src/dbone/ver02.rs", "Tools/ok-extract/src/session.rs",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/KKPreviewActor.cpp",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Public/KKPreviewActor.h",
    "Source/OpenKoikatsu/OKGalleryWidget.cpp",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/Tests/KKBodyDynamics.spec.cpp" }
  local report = O("cards", 5, "negative_control_frames", 900, "display_frames", 900,
    "display_samples", G.sum(audits, function(a) return I(a[4]) end), "palette_error_cm", 0,
    "native_position_error_max_m", G.max(errors, function(e) return F(e[2]) end),
    "native_rotation_error_max", G.max(errors, function(e) return F(e[3]) end),
    "negative_controls", G.rows(positive), "display_audits", G.rows(audits), "traces", traces,
    "attachments", 71, "images", 15,
    "image_sha256", G.sorted_object(expected, function(n) return digest(shots .. "/" .. n) end),
    "source_sha256", G.digests(ROOT, sources),
    "limitations", "This report covers source native/mod game animation. External Unreal retargeting has separate Stage288 evidence; arbitrary physics plugins and cross-character collisions remain separate.")
  local recovery = root .. "/Recovery288.json"
  if G.exists(recovery) then
    local recovered = json.decode(G.read(recovery))
    check(#recovered == 15 and G.all(recovered, function(r) return r.restored == digest(shots .. "/" .. r.name) end))
    report.capture_recovery = O(
      "reason", "External Stage288 runner initially targeted the Stage287 capture directory; source walk images regenerated after correcting the path.",
      "original_audit", "Saved/Logs/BodyDynamics287Before288RecoveryAudit.json",
      "original_capture_log", "Saved/Logs/BodyDynamicsShots287Before288Recovery.log",
      "comparison", "Saved/Tests/BodyDynamics287/Recovery288.json",
      "matching_original_file_hashes", G.sum(recovered, function(r) return r.matches end),
      "current_capture_log_sha256", digest(LOGS .. "/BodyDynamicsShots287_Walk_open.log"))
  end
  G.write_json(LOGS .. "/BodyDynamics287Audit.json", report)
  print("PASS: five cards, 900 negative-control frames, 900 displayed frames, source solver and shared GPU palettes, 15 images")
end

if ok.is_main() then ok.main(main) end
