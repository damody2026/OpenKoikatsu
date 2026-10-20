-- Development tool: turn the checked dialogue translations into the subtitle tables the game reads.
--
-- Writes StreetSocialData/Localization/<lang>/dialogue.json: {key: text}, the key being the first 16 hex digits of
-- SHA-1(jp + U+001F + zh) with zh as the game holds it at run time (topic lines have no Chinese there). The game looks a
-- subtitle up by that key (OKStreetLineLabel) and falls back to the line's own Chinese.
-- - zh-CN and en: every translated line.
-- - zh-TW: only the lines the run-time data lacks (no Chinese) or had wrong (a corrected zh-TW from the translators).
-- Run Tools/i18n_check_dialogue.lua --merge first (it writes translations.json from the chunks that pass).
local ok = require "ok"
local json, path, util = ok.json, ok.path, ok.util
local T = util.truthy

local function key_of(jp, zh) return ok.hash.sha1(jp .. "\31" .. zh):sub(1, 16) end
local function get(t, k, d) local v = t[k]; if v == nil then return d end; return v end

-- (jp, zh at run time, zh as extracted) of every catalogue node and topic line.
local function lines(data, overlay)
  local out = {}
  local function push(jp, zh)
    local extracted = T(zh) and zh or get(overlay, jp, "")
    out[#out + 1] = { jp, zh, extracted }
  end
  local cat = json.decode(path.read_text(path.join(data, "catalogue.json")))
  for _, n in ipairs(cat.nodes) do push(get(n, "text_jp", ""), get(n, "text_zh", "")) end
  local topics = json.decode(path.read_text(path.join(data, "topics.json")))
  for _, prof in ipairs(topics.profiles) do
    local seqs = {}
    for _, listen in ipairs(prof.listen) do
      for k, seq in pairs(listen) do if k ~= "topic" then seqs[#seqs + 1] = seq end end
    end
    for _, choices in pairs(get(prof, "offer", json.object())) do
      for _, seq in ipairs(choices) do seqs[#seqs + 1] = seq end
    end
    for _, seq in ipairs(seqs) do
      for _, part in ipairs(seq) do push(get(part, "text_jp", ""), get(part, "text_zh", "")) end
    end
  end
  return out
end

local function main()
  local p = ok.args.parser()
  p:opt("--data", { default = "Content/IndependentResources/StreetSocialData/RuntimeV5" })
  p:opt("--overlay", { default = "Content/IndependentResources/StreetSocialData/RuntimeV2/zh_tw.json" })
  p:opt("--translations", { default = "Saved/StreetSocial/i18n/dialogue/translations.json" })
  p:opt("--out", { default = "Content/IndependentResources/StreetSocialData/Localization" })
  local a = p:parse(arg)
  local overlay = get(json.decode(path.read_text(a.overlay)), "translations", {})
  local tr = json.decode(path.read_text(a.translations))
  local langs = { "zh-TW", "zh-CN", "en" }
  local out = { ["zh-TW"] = {}, ["zh-CN"] = {}, en = {} }
  local missing = 0
  for _, line in ipairs(lines(a.data, overlay)) do
    local jp, runtime_zh, extracted_zh = line[1], line[2], line[3]
    if T(jp) or T(extracted_zh) then
      local t = tr[key_of(jp, extracted_zh)]
      local k = key_of(jp, runtime_zh)
      if not T(t) then
        missing = missing + 1
        if not T(runtime_zh) and T(extracted_zh) then out["zh-TW"][k] = extracted_zh end
      else
        for _, lang in ipairs({ "zh-CN", "en" }) do
          if t[lang] == nil then error("KeyError: '" .. lang .. "'", 0) end
          out[lang][k] = t[lang]
        end
        if T(t["zh-TW"]) then out["zh-TW"][k] = t["zh-TW"]
        elseif not T(runtime_zh) and T(extracted_zh) then out["zh-TW"][k] = extracted_zh end
      end
    end
  end
  for _, lang in ipairs(langs) do
    local tbl = out[lang]
    local folder = path.join(a.out, lang)
    path.mkdir(folder)
    local text = next(tbl) == nil and "{}" or json.encode(tbl, { ensure_ascii = false, indent = 0, sort_keys = true })
    path.write_text(path.join(folder, "dialogue.json"), text)
    print(("%s: %d lines"):format(lang, util.len(tbl)))
  end
  print(("%d lines without a translation"):format(missing))
end

if ok.is_main() then ok.main(main) end
