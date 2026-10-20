-- Independent UnityPy source tables and stage280 five-card UE regression.
local ok = require "ok"
local G = require "gpp"
local json, path = ok.json, ok.path
local check = G.check

local M = {}
local ROOT = G.ROOT
M.FOLDER = ROOT .. "/Saved/Tests/HeadFbs280"
local FOLDER = M.FOLDER

-- Tools/anim_export/export_anim_mouth.lua provides
-- extract_fbs(env, origin, asset, w_patterns, w_channels) with UnityPy-style objects
-- from ok.kit.unity_objects and csv writers (ok.csv.writer).
local function animation_export()
  local file = ROOT .. "/Tools/anim_export/export_anim_mouth.lua"
  if not path.is_file(file) then
    error("Tools/anim_export/export_anim_mouth.lua is missing")
  end
  return dofile(file)
end

local function tsv_writer()
  local w = ok.csv.writer({ delimiter = "\t" })
  w.writerow = w.row
  return w
end

function M.verify_sources()
  local module = animation_export()
  local rows = ok.csv.parse_dicts(G.read(FOLDER .. "/selected-patterns.tsv"), { delimiter = "\t" })
  local cards = G.sorted(G.unique(rows, function(r) return r.card end))
  check(#cards == 5)
  local source_audit = json.object()
  for _, card in ipairs(cards) do
    -- Match Rust sanitize: ASCII alphanumerics, every other char '_', 48 chars.
    local stem = G.ascii_stem(card, 48)
    local source = FOLDER .. "/" .. stem .. "_selected_head.unity3d"
    local patterns, channels = tsv_writer(), tsv_writer()
    module.extract_fbs(ok.kit.unity_objects(source), "source", card, patterns, channels)
    local control_rows = G.filter(rows, function(r) return r.card == card end)
    local prefabs = G.unique(control_rows, function(r) return r.prefab end)
    check(#prefabs == 1)
    local wanted = { MouthCtrl = true, EyesCtrl = true, EyebrowCtrl = true }
    local expected = {}
    for _, row in ipairs(ok.csv.parse(patterns:text(), { delimiter = "\t" })) do
      if G.contains(prefabs, row[3]) and wanted[row[4]] then
        expected[#expected + 1] = { row[3], row[4], G.I(row[5]), row[6]:lower(), G.I(row[8]), G.I(row[9]), G.I(row[11]) }
      end
    end
    local actual = G.map(control_rows, function(row)
      return { row.prefab, row.controller, G.I(row.target), row.mesh, G.I(row.pattern), G.I(row.close), G.I(row.open) }
    end)
    check(#G.unique(actual) == #control_rows, "duplicate Rust rows: " .. card)
    check(G.set_eq(actual, expected), card)
    check(G.set_eq(G.map(control_rows, function(r) return r.controller end), { "MouthCtrl", "EyesCtrl", "EyebrowCtrl" }))
    source_audit[card] = G.O("rows", #G.unique(actual), "sha256", G.digest(source))
  end
  local log = G.read(ROOT .. "/Saved/Logs/HeadFbsSource280.log")
  check(G.has(log, "1 passed; 0 failed"))
  check(G.count(log, "source-index baseline passed") == 15)
  local output = ROOT .. "/Saved/Logs/HeadFbsSources280Audit.json"
  G.write_json(output, source_audit)
  print(json.encode(source_audit, { ensure_ascii = false }))
  return source_audit
end

local function main()
  if ok.args.has(arg, "--sources-only") then
    M.verify_sources()
  else
    local sources = M.verify_sources()
    local result = require("verify_eye_clamp").verify({ prefix = "HeadFbs", stage = 280, pipeline = 127 })
    result.controller_sources = sources
    G.write_json(ROOT .. "/Saved/Logs/HeadFbs280Audit.json", result)
  end
end

if ok.is_main() then ok.main(main) end
return M
