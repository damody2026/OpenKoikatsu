-- Stage279: native full-face geometry and actual five-card UE regression.
local ok = require "ok"
local G = require "gpp"
local json, util = ok.json, ok.util
local O, F, check, findall = G.O, G.F, G.check, G.findall
local compare = require("compare_mouth_world").compare

local M = {}
M.ROOT = G.ROOT
M.LOGS = G.ROOT .. "/Saved/Logs"
M.CARDS = { "[Card] March_7th_keiyuu", "Alice_keiyuu",
  "Hayase_yuuka_keiyuu_V1_02 For KKS", "KK_091207", "KK_384283" }
local ROOT, LOGS, CARDS = M.ROOT, M.LOGS, M.CARDS

function M.read(p) return G.read(p) end
local read = M.read

-- verify{prefix=, stage=, pipeline=, build_log=, extra_tests={}, automation_log=, shots_log=,
--        unit_log=, geometry_log=, shots_folder=, save_audit=} (Python keyword defaults)
function M.verify(o)
  o = o or {}
  local prefix, stage, pipeline = o.prefix or "EyeClamp", o.stage or 279, o.pipeline or 126
  local extra_tests = o.extra_tests or {}
  local save_audit = o.save_audit ~= false
  compare(false, true)
  local world = json.decode(read(LOGS .. "/MouthWorld277Comparison.json"))
  local native = ROOT .. "/Saved/Tests/MouthWorld277/GameMouthWorldMarch.json.mouth-shapes.tsv"
  local neutral = {}
  for _, line in ipairs(util.splitlines(read(native))) do
    local r = util.split(line, "\t")
    if r[1] == "channel" and r[2] == "0" and r[3] == "0" then neutral[r[5]] = F(r[6]) end
  end
  if neutral["eye_face.f00_def_cl"] == nil or neutral["eye_face.f00_def_op"] == nil then error("KeyError: eye_face.f00_def_cl") end
  check(neutral["eye_face.f00_def_cl"] == 8)
  check(neutral["eye_face.f00_def_op"] == 92)
  for _, name in ipairs({ o.unit_log or (prefix .. "Unit" .. stage .. ".log"), o.geometry_log or (prefix .. "GeometryRust" .. stage .. ".log") }) do
    check(G.has(read(LOGS .. "/" .. name), "1 passed; 0 failed"))
  end
  check(G.has(read(LOGS .. "/" .. (o.build_log or (prefix .. "UnrealBuild" .. stage .. ".log"))), "Result: Succeeded"))
  local automation = read(LOGS .. "/" .. (o.automation_log or (prefix .. "UnrealAutomation" .. stage .. "Console.log")))
  local names = { "HandShapes", "ImportedData", "Playback", "NativeWeights", "SharedDeformation" }
  for _, t in ipairs(extra_tests) do names[#names + 1] = t end
  for _, name in ipairs(names) do
    check(G.has(automation, "Result={Success} Name={" .. name .. "}"))
  end
  check(not G.has(automation, "Result={Fail}"))
  check(G.has(automation, "Automation Test Queue Empty " .. (5 + #extra_tests) .. " tests performed"))
  check(G.has(automation, "RequestExitWithStatus(1, 0"))

  local shots = read(LOGS .. "/" .. (o.shots_log or (prefix .. "Shots" .. stage .. ".log")))
  check(G.has(shots, "explicit cards=5"))
  check(G.has(shots, "rendering 5 task(s) (5 card(s) x 1 variant(s) x 1 outfit(s))"))
  check(G.count(shots, "Tracing Screenshot") == 15)
  check(G.count(shots, "ok.ShotPreview imported Studio row=") == 5)
  check(G.has(shots, "RequestExitWithStatus(0, 0,"))
  check(not G.has(shots, "Failed to compile Material") and not G.has(shots, "Fatal error"))
  check(not G.has(shots, "mouth secondary section topology mismatch"))
  local intervals = ok.re.split([[ok.ShotPreview task \d/5]], shots)
  check(#intervals == 6)
  local counts = { 4, 3, 3, 4, 3 }
  for i = 1, 5 do
    local interval, count = intervals[i], counts[i]
    check(G.has(interval, "OK part source inactive name=cf_O_canine"))
    check(G.has(interval, "OK head item draw name=o_tang shader_kind=3 colors=" .. count))
  end
  local errors = G.floats(findall([[max_vertex_error_cm=([\d.]+)]], shots))
  check(#errors == 71 and G.max(errors) <= 1e-6)
  local fits = findall([[capture fit view=(\w+) points=(\d+) distance=([\d.]+) ndc_x=([\d.]+) ndc_y=([\d.]+)]], shots)
  check(#fits == 15 and G.all(fits, function(f) return F(f[4]) <= .92001 and F(f[5]) <= .92001 end))
  local folder = ROOT .. "/" .. (o.shots_folder or ("Saved/DebugShots/" .. prefix .. stage))
  local expected = {}
  for _, card in ipairs(CARDS) do
    for _, view in ipairs({ "3d", "face", "face_side" }) do expected[#expected + 1] = card .. "_outfit0_" .. view .. ".png" end
  end
  check(G.set_eq(G.glob_names(folder, "*.png"), expected))
  check(G.all(expected, function(name) return G.size_is(folder .. "/" .. name, 900, 1080) end))
  local bounds = json.object()
  for _, card in ipairs(CARDS) do
    local b = G.fg_bounds(folder .. "/" .. card .. "_outfit0_3d.png", 12)
    if b == nil then error("ValueError: zero-size array to reduction operation minimum which has no identity") end
    bounds[card] = b
    check(b[2] > 0 and b[4] <= 1036)
  end
  local geometry = util.values(world.geometry)
  local result = O("pipeline", pipeline, "source_eye_controller_limit", .92,
    "native_face_cases", #geometry,
    "native_full_face_max_error_m", G.max(geometry, function(v) return v.max end),
    "native_mouth_max_error_m", G.max(geometry, function(v) return v.mouth_max end),
    "source_native_fixture_sha256", G.digest(native),
    "unreal_tests_success", 5 + #extra_tests, "cards", CARDS, "images", 15,
    "attachment_audits", 71, "attachment_max_error_cm", G.max(errors),
    "projection_fits", 15, "full_body_bounds", bounds,
    "limitations", "Native full-face control is March in frozen neutral eyes/brows; five-card captures are UE regressions. Blink/gaze, all controllers and lighting/pixels are not claimed equal.")
  if save_audit then
    G.write_json(LOGS .. "/" .. prefix .. stage .. "Audit.json", result)
  end
  print(json.encode(result, { ensure_ascii = false }))
  return result
end

if ok.is_main() then ok.main(function() M.verify() end) end
return M
