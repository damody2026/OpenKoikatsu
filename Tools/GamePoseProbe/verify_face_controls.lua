-- Stage282: installed-game FBS controls, actual mod sources and UE display.
local ok = require "ok"
local G = require "gpp"
local json, util = ok.json, ok.util
local O, F, I, check, findall = G.O, G.F, G.I, G.check, G.findall
local EC = require "verify_eye_clamp"
local verify, read = EC.verify, EC.read
local verify_sources = require("verify_head_fbs").verify_sources

local ROOT = G.ROOT
local LOGS = ROOT .. "/Saved/Logs"

local function main()
  local fixtures = json.object()
  for _, pair in ipairs({ { "GameMarchFace282", 141 }, { "GameAliceFace282", 135 } }) do
    local name, count = pair[1], pair[2]
    local base = ROOT .. "/Saved/Tests/FaceControls282/" .. name .. ".json"
    check(json.decode(read(base))["error"] == "")
    local tbl = base .. ".face-controls.tsv"
    local rows = {}
    for _, line in ipairs(util.splitlines(read(tbl))) do rows[#rows + 1] = util.split(line, "\t") end
    local cases = G.filter(rows, function(r) return r[1] == "case" end)
    check(#cases == count and #G.unique(cases, function(r) return r[2] end) == count)
    check(util.equal(rows[#rows], { "cases", tostring(count) }))
    check(G.set_eq(G.map(cases, function(r) return F(r[5]) end), { 0., .61337, 1. }))
    local targets = G.unique(G.filter(rows, function(r) return r[1] == "target" end), function(r) return r[3] end)
    check(G.all({ "cf_O_namida_L", "cf_O_namida_M", "cf_O_namida_S", "cf_O_mayuge" }, function(t) return G.contains(targets, t) end))
    fixtures[name] = O("cases", count, "targets", G.sorted(targets),
      "source_pairs", G.sum(rows, function(r) return r[1] == "pattern" and 1 or 0 end),
      "weights", G.sum(rows, function(r) return r[1] == "weight" and 1 or 0 end),
      "sha256", G.digest(tbl))
  end
  local automation = read(LOGS .. "/FaceControlsUnrealAutomation282Finalv4Console.log")
  check(G.has(automation, "KK native face controls cases=276 channel_comparisons=145524 source_pairs=1096"))
  check(G.has(automation, "KK source material-pass face pairs=58"))
  check(G.sum(util.values(fixtures), function(f) return f.source_pairs end) == 1096)
  check(G.sum(util.values(fixtures), function(f) return f.weights end) == 145524)
  local sources = verify_sources()
  check(G.has(read(LOGS .. "/FaceControlsVisibilityUnit282.log"), "1 passed; 0 failed"))
  local result = verify({ prefix = "FaceControls", stage = 282, pipeline = 132,
    build_log = "FaceControlsUnrealBuild282Finalv5.log",
    automation_log = "FaceControlsUnrealAutomation282Finalv4Console.log",
    shots_log = "FaceControlsShots282Finalv4.log",
    unit_log = "FaceControlsUnit282Finalv3.log",
    geometry_log = "FaceControlsGeometryRust282Finalv3.log",
    extra_tests = { "NativeFaceControls" } })
  local shots = read(LOGS .. "/FaceControlsShots282Finalv4.log")
  check(G.count(shots, "OK face controls slots=10 ") == 6)
  local audits = findall([[OK face display audit eyes=(\d+) brow=(\d+) changed=(\d+) vertices=(\d+) restore_error_cm=([\d.]+) invalid_accepted=(\d+)]], shots)
  check(#audits == 5)
  check(util.equal(G.map(audits, function(a) return I(a[1]) end), { 30, 28, 28, 30, 28 }))
  check(G.all(audits, function(a) return I(a[2]) == 17 and I(a[3]) > 0 and I(a[4]) > 7000
    and F(a[5]) <= 1e-5 and I(a[6]) == 0 end))
  check(G.count(shots, "face control=1 pattern=1 open_percent=61") == 5)
  check(G.count(shots, "face control=2 pattern=2 open_percent=80") == 5)
  local intervals = ok.re.split([[ok.ShotPreview task \d/5]], shots)
  -- ShotPreview first loads March to discover the outfit queue, then loads
  -- the first task. Later cards load once. Verify each load, not a guessed
  -- total equal to the number of final screenshots.
  local loads = { 2, 1, 1, 1, 1 }
  for i = 1, math.min(#intervals - 1, 5) do
    for _, n in ipairs({ "L", "M", "S" }) do
      check(G.count(intervals[i], "OK part source inactive name=cf_O_namida_" .. n) == loads[i])
    end
  end
  local gallery = read(LOGS .. "/FaceControlsGallery282Finalv2.log")
  check(G.has(gallery, "KK face gallery verified eyes=30 brow=17 patterns=(1,2) rates=(0.61337,0.8)"))
  check(G.has(gallery, "KK catalog gallery captured after shader completion"))
  check(G.has(gallery, "RequestExitWithStatus(0, 0,"))
  check(not G.has(gallery, "face gallery selection/rate/clock gate failed"))
  check(G.size_is(ROOT .. "/Saved/DebugShots/FaceControls282UI/gallery_face_controls.png", 1440, 1000))
  result.native_face_controls = fixtures
  result.native_channel_comparisons = 145524
  result.material_pass_source_pairs = 58
  result.controller_sources = sources
  result.face_display_roundtrips = G.A(G.map(audits, function(a)
    return O("eyes", I(a[1]), "brow", I(a[2]), "changed", I(a[3]), "vertices", I(a[4]), "restore_error_cm", F(a[5])) end))
  result.gallery_controls_verified = true
  result.limitations = "Native controller weights cover every eyes/brow pattern of March and mod Alice. Full native geometry covers March mouth targets with neutral eyes/brows. Five-card captures are UE regressions. Timed ChangePtn transitions, automatic blink/gaze, tear-level UI, complete Animator graphs and shader pixel parity are separate work; chin work is deferred by the user."
  G.write_json(LOGS .. "/FaceControls282Audit.json", result)
  print(json.encode(result, { ensure_ascii = false }))
end

if ok.is_main() then ok.main(main) end
