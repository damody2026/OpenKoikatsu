-- Check native controls, five-card tests and actual Unreal capture artifacts.
local ok = require "ok"
local G = require "gpp"
local json, util, path = ok.json, ok.util, ok.path
local O, F, I, check, findall = G.O, G.F, G.I, G.check, G.findall

local ROOT = G.ROOT
local LOGS = ROOT .. "/Saved/Logs"

local function read(name) return G.read(LOGS .. "/" .. name) end

local function verify()
  local cards = { "[Card] March_7th_keiyuu", "Alice_keiyuu",
    "Hayase_yuuka_keiyuu_V1_02 For KKS", "KK_091207", "KK_384283" }
  local fixture = ROOT .. "/Saved/Tests/HandShapes275/GameHandShapes.json"
  check(json.decode(path.read_text(fixture))["error"] == "")
  local raw = path.read_bytes(fixture .. ".hand-shapes.tsv")
  local lines = util.splitlines((raw:gsub("^\239\187\191", ""):gsub("\r\n?", "\n")))
  local count = I(util.split(lines[1], "\t")[2])
  local cases = G.sum(lines, function(l) return util.startswith(l, "case\t") and 1 or 0 end)
  local compared = G.sum(lines, function(l) return util.startswith(l, "bone\t") and 1 or 0 end)
  check(cases == 2 * (count + 5) and compared == cases * 34)
  local native = read("HandShapesGameControl275.log")
  check(G.has(native, "1 passed; 0 failed"))
  local match = G.search([[patterns=(\d+) cases=(\d+) bone_comparisons=(\d+) max_quaternion_error=([\d.]+)]], native)
  check(match and I(match[1]) == count and I(match[2]) == cases and I(match[3]) == compared)
  local native_error = F(match[4])
  check(native_error <= 2e-5)
  local tests = read("HandShapesFiveCards275Final.log")
  check(G.has(tests, "1 passed; 0 failed"))
  for _, card in ipairs(cards) do
    check(G.has(tests, "Hand shapes " .. card .. ".png: 48 selections/30 finger targets/GPU palette/disable/cache/clock pass"))
  end
  local automation = read("HandShapesUnrealAutomation275Final.log")
  check(not G.has(automation, "Result={Fail}"))
  for _, test in ipairs({ "HandShapes", "ImportedData", "Playback" }) do
    check(G.has(automation, "Result={Success} Name={" .. test .. "}"))
  end
  check(G.has(read("HandShapesUnrealBuild275Finalv2.log"), "Result: Succeeded"))
  local ui = read("HandShapesGallery275.log")
  check(G.has(ui, "KK hand gallery verified count=24 left=1->23 t=0.3 right=23->1 t=0.8"))
  check(G.has(ui, "RequestExitWithStatus(0, 0,"))
  check(G.size_is(ROOT .. "/Saved/DebugShots/HandShapes275UI/gallery_hand_shapes.png", 1600, 1080))
  local shots = read("HandShapesShots275Final.log")
  check(G.has(shots, "rendering 5 task(s) (5 card(s) x 1 variant(s) x 1 outfit(s))"))
  check(G.has(shots, "RequestExitWithStatus(0, 0,"))
  check(G.count(shots, "Tracing Screenshot") == 15)
  check(G.count(shots, "ok.ShotPreview imported Studio row=") == 5)
  check(G.count(shots, "ok.ShotPreview hand side=") == 10)
  local errors = G.floats(findall([[max_vertex_error_cm=([\d.]+)]], shots))
  local declared = G.sum(G.map(findall([[display audited runtime parts=(\d+)]], shots), I))
  check(#errors == declared and declared == 71 and G.max(errors) <= 1e-6)
  local fits = findall([[capture fit view=(\w+) points=(\d+) distance=([\d.]+) ndc_x=([\d.]+) ndc_y=([\d.]+)]], shots)
  check(#fits == 15)
  check(G.all(fits, function(f) return F(f[4]) <= 0.92001 and F(f[5]) <= 0.92001 end))
  local folder = ROOT .. "/Saved/DebugShots/HandShapes275"
  local expected = {}
  for _, card in ipairs(cards) do for _, view in ipairs({ "3d", "face", "face_side" }) do expected[#expected + 1] = card .. "_outfit0_" .. view .. ".png" end end
  check(G.set_eq(G.glob_names(folder, "*.png"), expected))
  local bboxes = json.object()
  for _, name in ipairs(G.sorted(expected)) do
    check(G.size_is(folder .. "/" .. name, 900, 1080))
    if util.endswith(name, "_3d.png") then
      local bbox = G.fg_bounds(folder .. "/" .. name, 8)
      if bbox == nil then error("ValueError: zero-size array to reduction operation minimum which has no identity") end
      check(bbox[2] >= 20 and bbox[4] <= 1060)
      bboxes[name] = bbox
    end
  end
  local output = O(
    "native_game_patterns", count, "native_game_cases", cases,
    "native_game_bone_comparisons", compared,
    "native_game_max_quaternion_component_error", native_error,
    "native_fixture_sha256", ok.hash.sha256(raw),
    "five_cards_all_hand_selections", 240,
    "unreal_automation_successes", 3, "unreal_image_count", #expected,
    "unreal_display_attachment_audits", #errors,
    "unreal_display_to_extractor_max_vertex_error_cm", G.max(errors),
    "unreal_projection_fit_count", #fits,
    "full_body_foreground_bbox_threshold", 8, "full_body_foreground_bbox", bboxes)
  ok.path.write_text(LOGS .. "/HandShapes275Audit.json", json.encode(output, { ensure_ascii = false, indent = 2 }) .. "\n")
  print(json.encode(output, { ensure_ascii = false, indent = 2 }))
end

if ok.is_main() then ok.main(verify) end
