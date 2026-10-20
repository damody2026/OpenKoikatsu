-- Source timed FBS/blink traces, final engine tests and five actual cards.
local ok = require "ok"
local G = require "gpp"
local json, util = ok.json, ok.util
local O, F, I, check, findall = G.O, G.F, G.I, G.check, G.findall
local EC = require "verify_eye_clamp"
local ROOT, LOGS, CARDS, read, verify = EC.ROOT, EC.LOGS, EC.CARDS, EC.read, EC.verify
local FOLDER = require("verify_head_fbs").FOLDER

local M = {}

local function tsv_rows(text)
  local rows = {}
  for _, line in ipairs(util.splitlines(text)) do rows[#rows + 1] = util.split(line, "\t") end
  return rows
end

function M.source_settings()
  local rows = ok.csv.parse_dicts(read(FOLDER .. "/selected-patterns.tsv"), { delimiter = "\t" })
  local result = json.object()
  for _, card in ipairs(CARDS) do
    local selected = G.unique(G.filter(rows, function(r) return r.card == card .. ".png" end), function(r) return r.prefab end)
    -- selected-patterns stores the filename including .png.
    check(#selected == 1, { card, selected })
    local stem = G.ascii_stem(card .. ".png", 48)
    local file = FOLDER .. "/" .. stem .. "_selected_head.unity3d"
    local gos = {}
    for _, o in ipairs(ok.kit.unity_objects(file, { type = "GameObject" })) do gos[o.path_id] = o.tree and o.tree.m_Name or o.name end
    local heads = {}
    for _, o in ipairs(ok.kit.unity_objects(file, { type = "MonoBehaviour" })) do
      if o.type == "MonoBehaviour" then
        local data = o.tree
        if data and data.EyesCtrl ~= nil and gos[data.m_GameObject.m_PathID] == selected[1] then heads[#heads + 1] = data end
      end
    end
    check(#heads == 1)
    local head = heads[1]
    local sync = head.EyebrowCtrl.SyncBlink
    if type(sync) == "boolean" then sync = sync and 1 or 0 else sync = math.tointeger(sync) or sync end
    local settings = json.array({ head.BlinkCtrl.BlinkFrequency, head.BlinkCtrl.BaseSpeed, sync, json.null,
      head.EyesCtrl.OpenMin, head.EyebrowCtrl.OpenMin, head.EyesCtrl.FixedRate, head.EyebrowCtrl.FixedRate })
    result[card] = O("prefab", selected[1], "settings", settings, "bundle_sha256", G.digest(file))
  end
  return result
end

local function main()
  local sources = M.source_settings()
  local fixtures = json.object()
  for _, pair in ipairs({ { "GameMarchTime283", CARDS[1] }, { "GameAliceTime283", CARDS[2] } }) do
    local stem, card = pair[1], pair[2]
    local p = ROOT .. "/Saved/Tests/FaceTime283/" .. stem .. ".json"
    check(json.decode(read(p))["error"] == "")
    local tbl = p .. ".face-time.tsv"
    check(G.mtime(tbl) <= G.mtime(LOGS .. "/FaceTimeUnrealAutomation283Console.log"))
    local rows = tsv_rows(read(tbl))
    local settings = json.object()
    for _, r in ipairs(rows) do if r[1] == "setting" then settings[I(r[2])] = F(r[3]) end end
    check(G.set_len(settings) == 8)
    for index, value in ipairs(sources[card].settings) do
      if value ~= json.null then
        local s = settings[index - 1]
        if s == nil then error("KeyError: " .. (index - 1)) end
        check(math.abs(s - value) < 1e-6)
      end
    end
    local frames = G.filter(rows, function(r) return r[1] == "frame" end)
    local blinks = G.filter(rows, function(r) return r[1] == "blink" end)
    check(#frames == 72 and #blinks == 240)
    for control = 0, 2 do
      local sequence = G.filter(frames, function(r) return I(r[3]) == control end)
      local seq = G.map(sequence, function(r) return I(r[4]) end)
      check(util.equal(seq, util.range(24)))
      local picked = {}
      for _, r in ipairs(sequence) do if I(r[5]) >= 0 then picked[#picked + 1] = { I(r[4]), I(r[5]), I(r[6]) } end end
      check(util.equal(picked, { { 1, 8, 1 }, { 4, 2, 1 }, { 9, 3, 0 }, { 10, 3, 1 }, { 11, 1, 1 } }))
      check(F(sequence[10][8]) == 1 and F(sequence[11][8]) == 1)
      check(F(sequence[#sequence][8]) == 1)
    end
    check(G.set_eq(G.map(blinks, function(r) return r[4] end), { "none", "fixed", "resume", "close", "open", "frequency0", "speed" }))
    check(G.set_eq(G.map(blinks, function(r) return I(r[12]) end), { -1, 0, 1 }))
    check(G.all(blinks, function(r) return -1e-6 <= F(r[5]) and F(r[5]) <= .050001 and -1e-6 <= F(r[7]) and F(r[7]) <= .050001 end))
    for k = 61, 70 do check(F(blinks[k][16]) == F(blinks[60][16])) end
    check(F(blinks[181][11]) == 1) -- native SetSpeed(.2) -> Max(1,.2).
    fixtures[stem] = O("frames", 72, "blink_frames", 240,
      "channels", G.sum(rows, function(r) return r[1] == "weight" and 1 or 0 end),
      "pairs", G.sum(rows, function(r) return r[1] == "pattern" and 1 or 0 end),
      "sha256", G.digest(tbl), "settings", settings)
  end
  local tests = read(LOGS .. "/FaceTimeUnrealAutomation283Console.log")
  check(G.has(tests, "KK native face time settings=16 pairs=1096 frames=144 channel_comparisons=75888 blink_frames=480"))
  check(G.has(tests, "KK native face controls cases=276 channel_comparisons=145524 source_pairs=1096"))
  local result = verify({ prefix = "FaceTime", stage = 283, pipeline = 133,
    build_log = "FaceTimeUnrealBuild283Final.log", extra_tests = { "NativeFaceControls", "NativeFaceTime" },
    shots_log = "FaceTimeShots283_half.log", unit_log = "FaceTimeUnit283Final.log",
    shots_folder = "Saved/DebugShots/FaceTime283_half", save_audit = false })
  local shots = read(LOGS .. "/FaceTimeShots283_half.log")
  local audits = findall([[OK face display audit eyes=(\d+) brow=(\d+) changed=(\d+) vertices=(\d+) restore_error_cm=([\d.]+) invalid_accepted=(\d+)]], shots)
  check(#audits == 5 and G.all(audits, function(r) return I(r[3]) > 0 and I(r[4]) > 7000 and F(r[5]) <= 1e-5 and I(r[6]) == 0 end))
  check(G.count(shots, "face time display audit passed=1") == 5)
  check(G.count(shots, "clock_unchanged=1 restore_error_cm=0.000000000") == 5)
  local blink_shots = findall([[OK blink shot state=half open_rate=([\d.]+) eyes=([\d.]+) brow=([\d.]+) sync=(\d+) frequency=(\d+) base_speed=([\d.]+)]], shots)
  check(#blink_shots == 5)
  for i = 1, math.min(#CARDS, #blink_shots) do
    local trace = G.floats(blink_shots[i])
    local rate, sync, frequency, speed = trace[1], trace[4], trace[5], trace[6]
    local settings = sources[CARDS[i]].settings
    check(math.abs(rate - .5) < 1e-5)
    check(math.tointeger(sync // 1) == settings[3] and math.tointeger(frequency // 1) == settings[1] and math.abs(speed - settings[2]) < 1e-6)
  end
  local gallery = read(LOGS .. "/FaceTimeGallery283.log")
  check(G.has(gallery, "KK face time gallery verified alpha=0.333333 interrupted_back=2 blink=0.500000"))
  check(G.has(gallery, "RequestExitWithStatus(0, 0,") and not G.has(gallery, "LogTemp: Error:"))
  local ui = ROOT .. "/Saved/DebugShots/FaceTime283UI/gallery_imported_studio.png"
  check(G.size_is(ui, 1440, 1000))
  local endpoints = json.object()
  for _, sv in ipairs({ { "open", 1. }, { "closed", 0. } }) do
    local state, value = sv[1], sv[2]
    local log = read(LOGS .. "/FaceTimeShots283_" .. state .. ".log")
    check(G.has(log, "rendering 2 task(s) (2 card(s) x 1 variant(s) x 1 outfit(s))"))
    check(G.count(log, "Tracing Screenshot") == 6)
    check(G.count(log, "face time display audit passed=1") == 2)
    check(G.has(log, "RequestExitWithStatus(0, 0,") and not G.has(log, "LogTemp: Error:"))
    local rates = findall("OK blink shot state=" .. state .. [[ open_rate=([\d.]+) eyes=([\d.]+) brow=([\d.]+)]], log)
    check(#rates == 2)
    check(G.all(rates, function(r) return math.abs(F(r[1]) - value) < 1e-6 and math.abs(F(r[2]) - .92 * value) < 1e-6
      and math.abs(F(r[3]) - value) < 1e-6 end))
    local errors = G.floats(findall([[max_vertex_error_cm=([\d.]+)]], log))
    local counts = G.map(findall([[audited runtime parts=(\d+)]], log), I)
    check(#counts == 2 and G.sum(counts) == #errors and G.max(errors) <= 1e-6)
    local folder = ROOT .. "/Saved/DebugShots/FaceTime283_" .. state
    local expected = {}
    for c = 1, 2 do for _, view in ipairs({ "3d", "face", "face_side" }) do expected[#expected + 1] = CARDS[c] .. "_outfit0_" .. view .. ".png" end end
    check(G.set_eq(G.glob_names(folder, "*.png"), expected))
    check(G.all(expected, function(name) return G.size_is(folder .. "/" .. name, 900, 1080) end))
    endpoints[state] = O("images", 6, "rates", G.rows(rates), "attachment_audits", #errors, "attachment_max_error_cm", G.max(errors))
  end
  for c = 1, 2 do
    local card = CARDS[c]
    local n = G.diff_count(ROOT .. "/Saved/DebugShots/FaceTime283_open/" .. card .. "_outfit0_face.png",
      ROOT .. "/Saved/DebugShots/FaceTime283_closed/" .. card .. "_outfit0_face.png", 2)
    check(n > 100)
  end
  local plugin = ROOT .. "/Plugins/KKUnityBundle/Source/KKUnityBundle"
  local build_inputs = { plugin .. "/Public/KKFaceAnimation.h", plugin .. "/Private/KKFaceBlendShape.h",
    plugin .. "/Private/KKPreviewActor.cpp", plugin .. "/Public/KKPreviewActor.h",
    plugin .. "/Private/Tests/KKFaceBlendShape.spec.cpp", ROOT .. "/Source/OpenKoikatsu/OKGalleryWidget.cpp",
    ROOT .. "/Source/OpenKoikatsu/OKGalleryWidget.h", ROOT .. "/Tools/ok-extract/src/ffi.rs",
    ROOT .. "/Tools/ok-extract/src/unity.rs", ROOT .. "/Tools/ok-extract/src/session.rs",
    ROOT .. "/Tools/ok-extract/src/card.rs", ROOT .. "/Tools/ok-extract/src/sqlcache.rs",
    ROOT .. "/Tools/ok-extract/src/lib.rs", ROOT .. "/Tools/ok-extract/include/ok_extract.h" }
  local build_time = G.mtime(LOGS .. "/FaceTimeUnrealBuild283Final.log")
  check(G.all(build_inputs, function(p) return G.mtime(p) <= build_time end))
  local hashes = json.object()
  for _, p in ipairs(build_inputs) do hashes[G.rel(p)] = G.digest(p) end
  check(not G.exists(ok.game_root(true) .. "/BepInEx/plugins/OpenKoikatsu.GamePoseProbe.dll"))
  result.native_time = fixtures
  result.selected_head_runtime_sources = sources
  result.production_source_sha256 = hashes
  result.face_roundtrips = G.rows(audits)
  result.half_blink_display = G.rows(blink_shots)
  result.gallery_verified = true
  result.blink_endpoints = endpoints
  result.total_regression_images = 27
  result.limitations = "Native timed channel weights cover March and mod Alice. Native blink state tests inject the recorded Unity random draws; UE RNG sequences are not identical. Five-card captures validate default CPU-head display. Gaze corrections, tear-level UI, mouth width randomization, complete Animator graphs, GPU-head visual certification and shader pixel parity remain separate work. Chin work is deferred by the user."
  G.write_json(LOGS .. "/FaceTime283Audit.json", result)
  print(json.encode(result, { ensure_ascii = false }))
end

if ok.is_main() then ok.main(main) end
return M
