-- Audit the native FBS fixture and actual Unreal capture/test evidence.
--
-- This checks completeness and numeric/render pipeline evidence. Image appearance
-- is reviewed separately; this does not claim pixel equivalence to the game.
local ok = require "ok"
local G = require "gpp"
local json, util, path = ok.json, ok.util, ok.path
local O, F, check, findall = G.O, G.F, G.check, G.findall

local ROOT = G.ROOT
local LOGS = ROOT .. "/Saved/Logs"

local function verify()
  local base = ROOT .. "/Saved/Tests/MouthShapes276/GameMouthMarchSourceFinal.json"
  check(json.decode(path.read_text(base))["error"] == "")
  local raw = path.read_bytes(base .. ".mouth-shapes.tsv")
  local text = raw:gsub("^\239\187\191", "")
  local rows = {}
  for _, line in ipairs(util.splitlines(text:gsub("\r\n?", "\n"))) do rows[#rows + 1] = util.split(line, "\t") end
  local counts = json.object()
  for _, kind in ipairs({ "target", "pattern", "case", "channel", "vertex" }) do
    counts[kind] = G.sum(rows, function(r) return r[1] == kind and 1 or 0 end)
  end
  check(util.equal(rows[1], { "target_count", "5" }) and util.equal(rows[#rows], { "cases", "10" }))
  check(counts.target == 5 and counts.pattern == 270 and counts.case == 10 and counts.channel == 3120 and counts.vertex == 33150)
  for _, pattern in ipairs({ 0, 8 }) do
    local png = base .. ".mouth-" .. pattern .. "-0.8.png"
    check(G.size_is(png, 900, 1080))
    local metadata = G.read(png .. ".renderers.tsv")
    check(G.has(metadata, "capture_camera\tsource_main\t"))
    check(G.count(metadata, "source_camera_buffer\tAfterForwardOpaque\t") == 3)
    for _, name in ipairs({ "cf_O_mayuge", "cf_O_eyeline", "cf_O_eyeline_low" }) do
      check(G.has(metadata, "renderer_component\t" .. name .. "\tIllusionFixes.Blitty"))
    end
  end
  check(G.has(G.read(LOGS .. "/MouthShapesUnrealBuild276Final.log"), "Result: Succeeded"))
  local automation = G.read(LOGS .. "/MouthShapesUnrealAutomation276Final.log")
  check(not G.has(automation, "Result={Fail}"))
  for _, name in ipairs({ "HandShapes", "ImportedData", "Playback", "NativeWeights", "SharedDeformation" }) do
    check(G.has(automation, "Result={Success} Name={" .. name .. "}"))
  end
  local shots = G.read(LOGS .. "/MouthFbsShots276.log")
  check(G.has(shots, "RequestExitWithStatus(0, 0,") and G.count(shots, "Tracing Screenshot") == 15)
  check(not G.has(shots, "mouth secondary section topology mismatch"))
  local errors = G.floats(findall([[max_vertex_error_cm=([\d.]+)]], shots))
  check(#errors == 71 and G.max(errors) <= 1e-6)
  local fits = findall([[capture fit view=(\w+) points=(\d+) distance=([\d.]+) ndc_x=([\d.]+) ndc_y=([\d.]+)]], shots)
  check(#fits == 15)
  check(G.all(fits, function(f) return F(f[4]) <= .92001 and F(f[5]) <= .92001 end))
  local cards = { "[Card] March_7th_keiyuu", "Alice_keiyuu", "Hayase_yuuka_keiyuu_V1_02 For KKS", "KK_091207", "KK_384283" }
  local folder = ROOT .. "/Saved/DebugShots/MouthFbs276"
  local expected = {}
  for _, card in ipairs(cards) do for _, view in ipairs({ "3d", "face", "face_side" }) do expected[#expected + 1] = card .. "_outfit0_" .. view .. ".png" end end
  check(G.set_eq(G.glob_names(folder, "*.png"), expected))
  check(G.all(expected, function(name) return G.size_is(folder .. "/" .. name, 900, 1080) end))
  local audit = O(
    "native_fixture_sha256", ok.hash.sha256(raw),
    "native_counts", counts, "native_camera", "source_main",
    "native_brow_eyeline_blitty_renderers", 3,
    "unreal_native_mouth_channel_comparisons", 2360,
    "unreal_automation_successes", 5, "unreal_image_count", 15,
    "unreal_attachment_display_audits", #errors,
    "unreal_display_to_extractor_max_vertex_error_cm", G.max(errors),
    "remaining", "Mouth 8 lower lip/tongue artifact; full game geometry/pixel comparison is not established.")
  G.write_json(LOGS .. "/MouthShapes276Audit.json", audit)
  print(json.encode(audit, { ensure_ascii = false }))
end

if ok.is_main() then ok.main(verify) end
