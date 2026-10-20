-- Verify compiled editor registries against user-owned plugin sources.
local ok = require "ok"
local json, P, U = ok.json, ok.path, ok.util
local generate = require("generate_bone_controls").generate

local function has_cjk(s)
  for _, c in utf8.codes(s) do if c >= 0x3400 and c <= 0x9fff then return true end end
  return false
end

local function main()
  local p = ok.args.parser()
  local plugins = ok.game_root(true) .. "/kks_decomp/plugins"
  p:opt("--abmx-source", { default = plugins .. "/KKSABMX/KKABMX.GUI/InterfaceData.cs" })
  p:opt("--pregnancy-source", { default = plugins .. "/KKS_PregnancyPlus/KK_PregnancyPlus/BellyTemplate.cs" })
  local args = p:parse(arg)
  local root = P.parent(ok.script_dir)
  local expected, count = generate(args.abmx_source)
  assert(P.read_text(P.join(root, "Source/OpenKoikatsu/OKBoneControls.h")) == expected, "AssertionError")
  local presets = {}
  local text = P.read_text(args.pregnancy_source)
  for name, body in text:gmatch('case "([^"]+)":(.-)return pregnancyPlusData;') do
    local values = { GameplayEnabled = true, inflationSize = 40 }
    for field, value in body:gmatch("pregnancyPlusData%.(inflation[%w_]+) = ([%d%.%-]+)f;") do
      values[field] = U.float(value)
    end
    presets[name] = values
  end
  local source = P.read_text(P.join(root, "Source/OpenKoikatsu/OKPregnancyPresets.h"))
  local literal = [["(?:\\.|[^"\\])*"]]
  local entry = [[\{TEXT\((]] .. literal .. [[)\), TEXT\((]] .. literal .. [[)\), TEXT\((]] .. literal .. [[)\)\}]]
  local entries = ok.re.findall(entry, source)
  local actual = {}
  for _, e in ipairs(entries) do actual[json.decode(e[1])] = json.decode(json.decode(e[3])) end
  assert(U.equal(actual, presets), "Pregnancy+ presets differ from installed plugin source")
  local labels = {}
  for label in expected:gmatch('{TEXT%("[^"]+"%), TEXT%("([^"]+)"%)') do labels[#labels + 1] = label end
  local good = #labels == count
  for _, label in ipairs(labels) do good = good and has_cjk(label) and not label:find("[A-Wa-z]") end
  assert(good, "Untranslated bone setting")
  local preset_labels, seen, unique = {}, {}, 0
  for _, e in ipairs(entries) do
    local label = json.decode(e[2]); preset_labels[#preset_labels + 1] = label
    if not seen[label] then seen[label] = true; unique = unique + 1 end
  end
  local npresets = U.len(presets)
  good = #preset_labels == npresets and unique == npresets
  for _, label in ipairs(preset_labels) do good = good and has_cjk(label) end
  assert(good, "Untranslated or ambiguous preset label")
  print(json.encode(json.object({ { "valid", true }, { "abmx_controls", count }, { "pregnancy_presets", npresets } })))
end

ok.main(main)
