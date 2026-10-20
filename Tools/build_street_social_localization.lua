-- Build an exact-text Traditional Chinese overlay without changing source/audio.
local ok = require "ok"
local json, path, util = ok.json, ok.path, ok.util
local T = util.truthy
local M = {}

M.TOPIC_NAMES_ZH = util.split("釣魚|攀樹|獨木舟|浮潛|健行|賞鯨|香蕉船|水牛|貓咪|椰子蟹|雛鳥|山貓|紅樹林|牽牛花|能量景點|扶桑花|榕樹|月桃|流星|星空|穗花棋盤腳|招潮蟹|珊瑚|小丑魚|花園鰻|鬼蝠魟|海龜|海螢|星砂|伴手禮|特色料理|香檬果汁|絨毛玩偶|祈願之鐘|紅型染|三線琴|風獅爺|親吻的滋味|第一次經驗|溫暖|身體契合度|甜蜜之夜|胸部話題|臀部話題|私密部位|特殊喜好|喜歡的姿勢|慾求不滿", "|")

local function plain(text)
  if not T(text) then text = "" end
  return (text:gsub("<[^>]+>", ""))
end
M.plain = plain

local function markers(text)
  local c = {}
  for m in text:gmatch("%[[^%]]+%]") do c[m] = (c[m] or 0) + 1 end
  return c
end
-- str.strip() == "" (Unicode whitespace included).
local function blank(s)
  s = s:gsub("\227\128\128", ""):gsub("\194[\133\160]", ""):gsub("\226\128[\128-\138\168\169\175]", ""):gsub("\226\129\159", ""):gsub("\225\154\128", "")
  return s:match("^[%s\28-\31]*$") ~= nil
end
local function has_kana(s)
  for _, cp in utf8.codes(s) do
    if (cp >= 0x3041 and cp <= 0x3096) or (cp >= 0x30A1 and cp <= 0x30FA) then return true end
  end
  return false
end

function M.validate_pair(jp, zh)
  if not T(jp) or blank(zh) or not util.equal(markers(jp), markers(zh)) then
    error("Empty translation or changed placeholders: " .. util.repr(jp), 0)
  end
  if has_kana((zh:gsub("%[[^%]]+%]", ""))) then
    error("Japanese kana remains in Chinese translation: " .. util.repr(jp), 0)
  end
end
local validate_pair = M.validate_pair

function M.topic_parts(topics)
  local out = {}
  for _, profile in ipairs(topics.profiles) do
    local sequences = {}
    for _, row in ipairs(profile.listen) do
      for k, v in pairs(row) do if k ~= "topic" then sequences[#sequences + 1] = v end end
    end
    for _, choices in pairs(profile.offer) do
      for _, seq in ipairs(choices) do sequences[#sequences + 1] = seq end
    end
    for _, row in ipairs(profile.touch) do sequences[#sequences + 1] = row.parts end
    for _, seq in ipairs(sequences) do
      for _, part in ipairs(seq) do out[#out + 1] = part end
    end
  end
  return out
end
local topic_parts = M.topic_parts

function M.apply_overlay(catalogue, topics, overlay)
  local translations = overlay.translations
  local rows = util.extend(util.copy(catalogue.nodes), topic_parts(topics))
  for _, row in ipairs(rows) do
    local jp = row.text_jp
    if jp == nil then jp = "" end
    if T(jp) then
      local zh = translations[jp]
      if T(zh) then
        validate_pair(jp, zh)
        row.text_zh = zh
        row.translation_source = "common_zh_tw_overlay_v1"
      end
    end
  end
  local names = overlay.topic_names
  for _, row in ipairs(topics.topics) do
    local no = util.num(row.No)
    if names[no] ~= nil then
      if row.NameJp == nil then row.NameJp = row.Name end
      row.Name = names[no]
    end
  end
end

function M.build(game, data)
  local ve = path.join(game, "voice_extract")
  local source_paths = { path.join(ve, "comm_lines.json"), path.join(ve, "translation_comm/comm_zh.json"), path.join(data, "zh_tw_supplement.tsv") }
  local lines = json.decode(path.read_text(source_paths[1]))
  local chinese = json.decode(path.read_text(source_paths[2]))
  local candidates = {}
  local function add(k, v)
    local set = candidates[k]
    if not set then set = { n = 0, items = {} }; candidates[k] = set end
    if not set.items[v] then set.items[v] = true; set.n = set.n + 1; set.first = set.first or v end
  end
  for key, row in pairs(lines) do
    local parts = util.split(key, ",")
    local translated = chinese[util.strip(parts[#parts])]
    if T(translated) then add(plain(row.text), plain(translated)) end
  end
  local overrides, override_keys = {}, {}
  for _, line in ipairs(util.splitlines(path.read_text(source_paths[3]))) do
    if util.strip(line) ~= "" and line:sub(1, 1) ~= "#" then
      local tab = line:find("\t", 1, true)
      if not tab then error("not enough values to unpack (expected 2, got 1)", 0) end
      local jp, zh = line:sub(1, tab - 1), line:sub(tab + 1)
      validate_pair(jp, zh)
      if overrides[jp] ~= nil then error("Duplicate manual translation: " .. jp, 0) end
      overrides[jp] = zh; override_keys[#override_keys + 1] = jp
      candidates[jp] = { n = 1, items = { [zh] = true }, first = zh }
    end
  end
  local catalogue = json.decode(path.read_text(path.join(data, "catalogue.json")))
  local topics = json.decode(path.read_text(path.join(data, "topics.json")))
  local rows = util.extend(util.copy(catalogue.nodes), topic_parts(topics))
  local needed = {}
  local present = {}
  for _, r in ipairs(rows) do
    local jp = r.text_jp
    if jp == nil then jp = "" end
    if T(jp) then present[jp] = true end
    if T(jp) then
      local zh = r.text_zh
      if zh == nil then zh = "" end
      if not pcall(validate_pair, jp, zh) then needed[jp] = true end
    end
  end
  for _, x in ipairs(override_keys) do if present[x] then needed[x] = true end end
  local sorted_needed = util.sorted_keys(needed)
  local missing = {}
  for _, x in ipairs(sorted_needed) do
    local c = candidates[x]
    if not c or c.n ~= 1 then missing[#missing + 1] = x end
  end
  if #missing > 0 then
    error("Unresolved exact translations: " .. json.encode(missing, { ensure_ascii = false }), 0)
  end
  local translations = json.object()
  for _, x in ipairs(sorted_needed) do translations[x] = candidates[x].first end
  for jp, zh in pairs(translations) do validate_pair(jp, zh) end
  local topic_names = json.object()
  for i, x in ipairs(M.TOPIC_NAMES_ZH) do topic_names[tostring(i - 1)] = x end
  local sources = {}
  for _, p in ipairs(source_paths) do
    local rel
    if path.is_relative_to(p, game) then rel = path.rel(p, game):gsub("/", "\\") else rel = path.name(p) end
    sources[#sources + 1] = json.object({ { "path", rel }, { "sha256", ok.hash.file(p, "sha256") } })
  end
  local overlay = json.object({ { "schema", 1 }, { "language", "zh-TW" }, { "translations", translations },
    { "topic_names", topic_names }, { "sources", sources } })
  M.apply_overlay(catalogue, topics, overlay)
  local after = util.extend(util.copy(catalogue.nodes), topic_parts(topics))
  local missing_after = {}
  for _, r in ipairs(after) do
    if T(r.text_jp) and not T(r.text_zh) then missing_after[#missing_after + 1] = r.text_jp end
  end
  local report = json.object({
    { "passed", #missing_after == 0 }, { "full_acceptance", false }, { "normal_nodes", #catalogue.nodes },
    { "topic_parts", #topic_parts(topics) }, { "topic_names", #topics.topics },
    { "new_exact_translations", #sorted_needed }, { "untranslated", missing_after },
    { "audio_changed", false }, { "source_text_changed", false },
  })
  return overlay, report
end

if ok.is_main() then
  ok.main(function()
    local p = ok.args.parser()
    p:opt("--game", { default = ok.game_root(true) })
    p:opt("--data", { default = "Content/IndependentResources/StreetSocialData/RuntimeV2" })
    p:opt("--report", { default = "Saved/StreetSocial/Build/localization-coverage.json" })
    local a = p:parse(arg)
    local overlay, report = M.build(a.game, a.data)
    path.write_text(path.join(a.data, "zh_tw.json"), json.encode(overlay, { ensure_ascii = false, indent = 2 }))
    path.mkdir(path.parent(a.report))
    path.write_text(a.report, json.encode(report, { ensure_ascii = false, indent = 2 }))
    print(json.encode(report, { ensure_ascii = false }))
  end)
end

return M
