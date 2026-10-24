-- Prepare a player selection, never card-specific cooked resources.
local ok = require "ok"
local json, path, util = ok.json, ok.path, ok.util
local T = util.truthy
local crowd = require "verify_crowd_apng"
local M = {}

local function fail(msg) error("ValueError: " .. msg, 0) end
local function read_json(p) return json.decode(path.read_text(p)) end
local function get(t, k, d) local v = t[k]; if v == nil then return d end; return v end
-- str(Path(p)) on Windows.
local function win(p) return (path.norm(p):gsub("/+", "/"):gsub("/", "\\")) end
local function isabs(p) p = path.norm(p); return p:match("^%a:/") ~= nil or p:match("^//") ~= nil end
-- str.strip() for names (ASCII and ideographic spaces).
local function strip(s)
  local changed = true
  while changed do
    changed = false
    local a = s:match("^%s+") or (s:sub(1, 3) == "\227\128\128" and s:sub(1, 3)) or nil
    if a then s = s:sub(#a + 1); changed = true end
    local b = s:match("%s+$") or (#s >= 3 and s:sub(-3) == "\227\128\128" and s:sub(-3)) or nil
    if b then s = s:sub(1, #s - #b); changed = true end
  end
  return s
end

-- Hash files in one batch; stop at the first unreadable file or mismatch, in order.
local function check_hashes(files, expected, message)
  local digests = ok.hash.files(files, "sha256")
  for i, f in ipairs(files) do
    if not digests[i] then error("FileNotFoundError: No such file or directory: " .. util.repr(win(f)), 0) end
    if digests[i] ~= expected[i] then fail(message) end
  end
end

-- Her identity: the sha256 of her card without its okVo chunk (her own TTS voice, Tools/ok-tts/src/cardvoice.rs), so
-- storing or changing a voice in a card does not make her a stranger. ok-tts puts the chunk right before IEND and
-- removing it gives the card's bytes back exactly, so a card with a voice keeps the id it had without one.
-- A card made again from an OK card (`ok-extract --oklib card/cards`, new resource pack, new bytes) carries the identity
-- it had in an okId chunk (64 hex digits, Tools/ok-extract/src/oklib.rs `card_identity`): that one is her identity.
function M.card_identity(p)
  local f = assert(io.open(p, "rb")); local data = f:read("a"); f:close()
  if data:sub(1, 8) == "\137PNG\r\n\26\n" and data:find("okId", 1, true) then
    local at = 9
    while at + 8 <= #data do
      local len = string.unpack(">I4", data, at)
      local kind = data:sub(at + 4, at + 7)
      if kind == "okId" then
        local id = data:sub(at + 8, at + 7 + len)
        if #id == 64 and id:match("^%x+$") then return id:lower() end
      end
      if kind == "IEND" then break end
      at = at + 12 + len
    end
  end
  if data:sub(1, 8) ~= "\137PNG\r\n\26\n" or not data:find("okVo", 1, true) then return ok.hash.file(p, "sha256") end
  local parts, at, found = { data:sub(1, 8) }, 9, false
  while at + 8 <= #data do
    local len = string.unpack(">I4", data, at)
    local kind, stop = data:sub(at + 4, at + 7), at + 12 + len
    if kind == "okVo" then found = true else parts[#parts + 1] = data:sub(at, stop - 1) end
    at = stop
    if kind == "IEND" then break end
  end
  if not found then return ok.hash.file(p, "sha256") end
  parts[#parts + 1] = data:sub(at)
  -- Hashed as a file: the native digest (ok.hash hashes strings in plain Lua, far too slow for a card).
  local tmp = path.join(os.getenv("TEMP") or path.parent(p), ("card-identity-%d-%s.png"):format(os.time(), path.stem(p)))
  path.write_bytes(tmp, table.concat(parts))
  local ok_hash, digest = pcall(ok.hash.file, tmp, "sha256")
  path.remove(tmp)
  if not ok_hash then error(digest, 0) end
  return digest
end

function M.character(p)
  p = path.abs(p)
  local _, _, _, header, payload = crowd.metadata_values(crowd.card_metadata(p))
  local block
  for _, v in ipairs(header.lstInfo) do if v.name == "Parameter" then block = v break end end
  if not block then error("StopIteration", 0) end
  -- The eight-byte block-data length precedes offsets in the KKS header.
  local offset = 8 + block.pos
  local parameter = ok.kit.msgpack_decode(payload:sub(offset + 1, offset + block.size))
  local identity = M.card_identity(p)
  local names = {}
  for _, k in ipairs({ "lastname", "firstname" }) do names[#names + 1] = util.str(get(parameter, k, "")) end
  local name = strip(table.concat(names, " "))
  if name == "" then name = util.str(get(parameter, "nickname", "")) end
  if name == "" then name = path.stem(p) end
  local interest = parameter.interest
  local answer = T(interest) and interest.answer or nil
  local list = {}
  for _, v in ipairs(T(answer) and answer or {}) do list[#list + 1] = v end
  return json.object({
    { "id", "apng-" .. identity }, { "path", win(p) }, { "name", name },
    { "personality", math.tointeger(get(parameter, "personality", 0)) or math.floor(get(parameter, "personality", 0)) },
    { "sex", math.tointeger(parameter.sex) or math.floor(parameter.sex) },
    { "interest_categories", list }, { "voice_rate", get(parameter, "voiceRate", .5) + 0.0 },
  })
end

-- How she stands while they talk: one of her personality group's waits (the game's Studio 会話 待機), the same one for
-- her every time (by her id). The source standing and open-talk curves raise a finger, so a girl with those pointed
-- up through every conversation.
local WAITS = {
  reserved = { "conversation_reserved.okanim", "conversation_wait_00.okanim", "conversation_wait_04.okanim", "conversation_wait_26.okanim" },
  outgoing = { "conversation_wait_02.okanim", "conversation_wait_08.okanim", "conversation_wait_20.okanim" },
  neutral = { "conversation_wait_06.okanim", "conversation_wait_10.okanim", "conversation_wait_14.okanim", "conversation_wait_24.okanim" },
}
function M.locomotion_metadata(sex, personality, id)
  if sex ~= 0 and sex ~= 1 then error("ValueError: Unsupported locomotion sex", 0) end
  local reserved = util.set({ 1, 5, 8, 12, 17, 18, 22, 27, 31, 34 })
  local outgoing = util.set({ 0, 3, 6, 9, 13, 20, 24, 28, 32, 37 })
  local function one(list)
    local h = 0
    for k = 1, #(id or "") do h = (h * 31 + (id or ""):byte(k)) % 1000003 end
    return list[h % #list + 1]
  end
  local idle
  if sex == 0 then idle = "conversation_male.okanim"
  elseif reserved[personality] then idle = one(WAITS.reserved)
  elseif outgoing[personality] then idle = one(WAITS.outgoing)
  else idle = one(WAITS.neutral) end
  return json.object({ { "walk_clip", "walk_01.okanim" }, { "conversation_idle", idle },
    { "run_clip", sex == 0 and "run_male.okanim" or "run_female.okanim" } })
end

-- os.path.commonpath on Windows paths (case-insensitive compare, first path's spelling).
local function commonpath(paths)
  local split = {}
  for i, p in ipairs(paths) do split[i] = util.split(win(p), "\\") end
  local first = split[1]
  local n = #first
  for i = 2, #split do
    local s = split[i]
    local k = 0
    while k < n and k < #s and first[k + 1]:lower() == s[k + 1]:lower() do k = k + 1 end
    n = k
  end
  if n == 0 then fail("Paths don't have the same drive") end
  local parts = util.slice(first, 1, n)
  if n == 1 then return parts[1] .. "\\" end
  return table.concat(parts, "\\")
end

function M.main(argv)
  local parser = ok.args.parser()
  parser:opt("--male", { required = true })
  parser:opt("--selection", { required = true })
  parser:opt("--catalogue", { required = true })
  parser:opt("--materials", { required = true })
  parser:opt("--output", { required = true })
  parser:opt("--root", { default = (ok.root:gsub("/", "\\")) })
  local args = parser:parse(argv)
  local root = path.abs(args.root)
  local source = read_json(args.selection)
  local paths = json.is_array(source) and source or source.cards
  -- One to ten heroines (the street's OKStreetFemaleCount).
  if #paths < 1 or #paths > 10 then fail("Street needs one to ten female cards") end
  local male = M.character(args.male)
  local females = util.map(paths, function(p) return M.character(p) end)
  -- Names the cards got wrong (the original work's spelling), by card id.
  local overrides = path.join(ok.root, "Content/IndependentResources/StreetSocialData/name_overrides.json")
  if path.exists(overrides) then
    local names = get(read_json(overrides), "names", {})
    for _, female in ipairs(females) do female.name = get(names, female.id, female.name) end
  end
  if male.sex ~= 0 or util.any(females, function(c) return c.sex ~= 1 end) then
    fail("Expected one male and only female heroine card metadata records")
  end
  -- Her everyday clothes: the card's coordinate her profile names (everyday_outfit), else the first one. Card authors
  -- put anything in the first slot (a bunny suit, a swimsuit); a university town is everyday clothes.
  for _, female in ipairs(females) do
    local profile_path = path.join(ok.root, "Content/IndependentResources/StreetSocialData/Profiles", female.id .. ".json")
    female.outfit = 0
    if path.exists(profile_path) then female.outfit = tonumber(get(read_json(profile_path), "everyday_outfit", 0)) or 0 end
  end
  male.outfit = 0
  local everyone = { male, table.unpack(females) }
  local seen = {}
  for _, c in ipairs(everyone) do
    if seen[c.id] then fail("Duplicate card identity in street selection") end
    seen[c.id] = true
  end
  local materials = read_json(args.materials)
  if not T(materials.passed) or not T(materials.runtime_reveal_supported) then
    fail("Verified common material library required")
  end
  local catalogue_path = path.abs(args.catalogue)
  local catalogue = read_json(catalogue_path)
  if not json.is_array(catalogue.nodes) or #catalogue.nodes == 0 then
    fail("Nonempty common dialogue catalogue required")
  end
  -- Common exact translations cover unvoiced male lines as well as reactions.
  -- Apply before either language catalogue is copied, preserving source audio.
  local localization_path = path.with_name(catalogue_path, "zh_tw.json")
  local topic_source_path = path.with_name(catalogue_path, "topics.json")
  local localized_topics = nil
  if path.is_file(localization_path) and path.is_file(topic_source_path) then
    local loc = require "build_street_social_localization"
    localized_topics = read_json(topic_source_path)
    loc.apply_overlay(catalogue, localized_topics, read_json(localization_path))
  end
  -- Common packs may use relative references. Resolve once during selection,
  -- never during an NPC's per-frame update.
  local declared_audio_root = get(catalogue, "audio_root", path.parent(catalogue_path))
  if not isabs(declared_audio_root) then declared_audio_root = path.join(root, declared_audio_root) end
  local audio_base = path.abs(declared_audio_root)
  local function resolve_audio(value)
    local p = isabs(value) and value or path.join(audio_base, value)
    return win(path.abs(p))
  end
  for _, node in ipairs(catalogue.nodes) do
    for _, field in ipairs({ "voice_files", "voice_files_zh", "voice_files_jp" }) do
      if node[field] ~= nil then node[field] = util.map(node[field], resolve_audio) end
    end
    local src = get(node, "source", {})
    for _, part in ipairs(get(src, "voice_sequence", {})) do
      for _, field in ipairs({ "path", "zh_ogg" }) do
        if T(part[field]) then part[field] = resolve_audio(part[field]) end
      end
    end
  end
  local japanese_catalogue = util.deepcopy(catalogue)
  for _, node in ipairs(japanese_catalogue.nodes) do node.voice_language = "ja" end
  -- Select a complete matching Chinese sequence; partial translations never
  -- remove/reorder segments. Keep the original catalogue immutable.
  for _, node in ipairs(catalogue.nodes) do
    local original = get(node, "voice_files", {})
    local chinese = get(node, "voice_files_zh", {})
    node.voice_language = "ja"
    if T(chinese) and #chinese == #original and util.all(chinese, function(p) return path.is_file(p) and path.suffix(p):lower() == ".ogg" end) then
      node.voice_files_jp = original
      node.voice_files = chinese
      node.voice_language = "zh"
    end
  end
  local voices = {}
  for _, c in ipairs({ catalogue, japanese_catalogue }) do
    for _, n in ipairs(c.nodes) do
      for _, p in ipairs(get(n, "voice_files", {})) do voices[#voices + 1] = path.parent(path.abs(p)) end
    end
  end
  if #voices == 0 then fail("Catalogue has no matched audio") end
  local voice_root = commonpath(voices)
  local output = path.abs(args.output)
  local out_dir = path.parent(output)
  path.mkdir(out_dir)
  local runtime_catalogue = path.join(out_dir, "dialogue.runtime.json")
  path.write_text(runtime_catalogue, json.encode(catalogue, { ensure_ascii = false }))
  local runtime_japanese = path.join(out_dir, "dialogue.runtime.ja.json")
  path.write_text(runtime_japanese, json.encode(japanese_catalogue, { ensure_ascii = false }))
  -- Original relation limits are common rules, not card-derived assets.
  local rules = path.with_name(catalogue_path, "rules.json")
  if not path.is_file(rules) then fail("Original common dialogue rules are required") end
  local runtime_rules = read_json(rules)
  if runtime_rules.preference_for_exported_personalities ~= nil then
    runtime_rules.preference = runtime_rules.preference_for_exported_personalities
  end
  if runtime_rules.communication_condition_probability ~= nil then
    local chance = json.object()
    for _, r in ipairs(runtime_rules.communication_condition_probability) do
      local function int(v) return math.tointeger(v) or math.tointeger(v >= 0 and math.floor(v) or math.ceil(v)) end
      chance[util.str(r.stage) .. ":" .. util.str(r.condition)] = json.object({ { "min", int(r.min) }, { "max", int(r.max) } })
    end
    runtime_rules.communication_chance = chance
  end
  path.write_text(path.join(out_dir, "rules.json"), json.encode(runtime_rules, { ensure_ascii = false }))
  local anim_dir = path.join(root, "Content/IndependentResources/RuntimeAnimations")
  local actions = read_json(path.join(anim_dir, "actions.manifest.json"))
  -- Shared source curves only; each selected APNG's private pose is prepared
  -- by the existing bounded background action admission when requested.
  local photo_data, seated_data, drink_data, splash_data
  for _, common_name in ipairs({ "facilities.manifest.json", "photo.manifest.json", "seated.manifest.json", "drink.manifest.json",
    "splash.manifest.json", "watermelon.manifest.json", "phone.manifest.json", "selfie.manifest.json", "groom.manifest.json",
    "touch.manifest.json", "venue.manifest.json", "lover.manifest.json", "photo_poses.manifest.json" }) do
    local common_path = path.join(anim_dir, common_name)
    if path.is_file(common_path) then
      local common_data = read_json(common_path)
      check_hashes(util.map(common_data.actions, function(a) return path.join(anim_dir, a.file) end),
        util.map(common_data.actions, function(a) return a.sha256 end), "Common activity curve hash mismatch")
      for _, action in ipairs(common_data.actions) do table.insert(actions.actions, action) end
      if common_name == "photo.manifest.json" then photo_data = common_data end
      if common_name == "seated.manifest.json" then seated_data = common_data end
      if common_name == "drink.manifest.json" then drink_data = common_data end
      if common_name == "splash.manifest.json" then splash_data = common_data end
    end
  end
  -- The original game's non-H Studio animations (Tools/export_street_animation_library.lua): admitted, read from disk
  -- the first time one is played (no hash pass over hundreds of files at every start).
  local library_path = path.join(anim_dir, "library.manifest.json")
  if path.is_file(library_path) then
    for _, action in ipairs(read_json(library_path).actions) do
      if path.is_file(path.join(anim_dir, action.file)) then table.insert(actions.actions, action) end
    end
  end
  local topic_path = path.with_name(catalogue_path, "topics.json")
  local runtime_topics = nil
  if path.is_file(topic_path) then
    local topic_data = localized_topics or read_json(topic_path)
    if localized_topics == nil then
      -- No overlay beside this pack: the topic names still show in Chinese (the 48 names are the same everywhere).
      local names = require("build_street_social_localization").TOPIC_NAMES_ZH
      for _, row in ipairs(topic_data.topics) do
        if row.No >= 0 and row.No < #names then
          if row.NameJp == nil then row.NameJp = row.Name end
          row.Name = names[row.No + 1]
        end
      end
    end
    for _, profile in ipairs(topic_data.profiles) do
      local sequences = {}
      for _, listen in ipairs(profile.listen) do
        for key, seq in pairs(listen) do if key ~= "topic" then sequences[#sequences + 1] = seq end end
      end
      for _, choices in pairs(profile.offer) do
        for _, seq in ipairs(choices) do sequences[#sequences + 1] = seq end
      end
      for _, row in ipairs(get(profile, "touch", {})) do sequences[#sequences + 1] = row.parts end
      for _, sequence in ipairs(sequences) do
        for _, part in ipairs(sequence) do part.voices = util.map(part.voices, resolve_audio) end
      end
    end
    runtime_topics = path.join(out_dir, "topics.runtime.json")
    path.write_text(runtime_topics, json.encode(topic_data, { ensure_ascii = false }))
  end
  local relationship_path = path.with_name(catalogue_path, "relationships.json")
  local runtime_relationships = nil
  if path.is_file(relationship_path) then
    local relationship_data = read_json(relationship_path)
    local refs = json.keys(relationship_data.source_voice_sha256)
    check_hashes(util.map(refs, resolve_audio), util.map(refs, function(r) return relationship_data.source_voice_sha256[r] end),
      "Relationship source voice hash mismatch")
    for _, profile in ipairs(relationship_data.profiles) do
      for _, sequence in ipairs({ profile.accept, profile.refuse }) do
        for _, part in ipairs(sequence) do part.voices = util.map(part.voices, resolve_audio) end
      end
    end
    runtime_relationships = path.join(out_dir, "relationships.runtime.json")
    path.write_text(runtime_relationships, json.encode(relationship_data, { ensure_ascii = false }))
  end
  local locomotion = read_json(path.join(anim_dir, "locomotion.manifest.json"))
  check_hashes(util.map(locomotion.actions, function(r) return path.join(anim_dir, r.file) end),
    util.map(locomotion.actions, function(r) return r.sha256 end), "Common locomotion curve hash mismatch")
  -- Project-defined styles, stable fallback for every future APNG personality.
  for _, c in ipairs(everyone) do
    for k, v in pairs(M.locomotion_metadata(c.sex, c.personality, c.id)) do c[k] = v end
  end
  local curves = { win(path.join(anim_dir, "idle_02.okanim")), win(path.join(anim_dir, "walk_01.okanim")) }
  for _, r in ipairs(locomotion.actions) do curves[#curves + 1] = win(path.join(anim_dir, r.file)) end
  local action_list = {}
  for _, a in ipairs(actions.actions) do
    local entry = json.object({ { "curve", win(path.join(anim_dir, a.file)) }, { "loop", a.loop } })
    if a.play_seconds ~= nil then entry.play_seconds = a.play_seconds end
    action_list[#action_list + 1] = entry
  end
  local manifest = json.object({
    { "cards", util.map(everyone, function(c) return c.path end) },
    { "outfits", util.map(everyone, function(c) return c.outfit end) },
    { "characters", everyone },
    { "curves", curves },
    { "actions", action_list },
    { "material_root", materials.root },
    { "material_library", "RuntimeMaterialBake" },
  })
  -- The HG render style's parents (ok.RenderStyle 1), cooked into the same patch.
  if T(materials.hg_root) then manifest.hg_material_root = materials.hg_root end
  for _, kv in ipairs({
    -- The game's shadow ramps for the render-style picker (Tools/ok-extract ok-ramps).
    { "ramp_catalogue", win(path.join(root, "Content/KKPreview/Ramps/ramps.json")) },
    { "display_lut", win(path.join(root, "Content/KKPreview/KKSDefaultSaturationLUT16.png")) },
    { "external_control", true }, { "dialogue_catalogue", win(runtime_catalogue) },
    { "dialogue_catalogue_ja", win(runtime_japanese) }, { "voice_root", voice_root },
    { "generated_voice_root", win(path.join(root, "Saved/StreetSocial/TTSCache")) },
    { "invitation_catalogue", win(path.join(root, "Content/IndependentResources/StreetSocialData/invitations.json")) },
    { "expression_catalogue", win(path.join(root, "Content/IndependentResources/StreetSocialData/RuntimeV2/expressions.json")) },
    { "enable_face_patterns", true },
    { "save_path", win(path.join(root, "Saved/StreetSocial/relationships.json")) },
    { "social_report", win(path.join(out_dir, "social.validation.json")) }, { "report", win(path.join(out_dir, "runtime.json")) },
    { "progressive_loading", true }, { "loading_cpu_preview", false }, { "parallel_loading_poses", true },
    { "auto_blink", true }, { "auto_action_streaming", false }, { "npc_action_preload_count", 0 },
    { "background_prepare_concurrency", 8 }, { "diagnostic_load_deadline_seconds", 180 },
    { "profile_seconds", 0 }, { "exit_after_report", false }, { "depth_prepass_enabled", true },
  }) do manifest[kv[1]] = kv[2] end
  if T(photo_data) then manifest.paired_photo = json.object({ { "male", photo_data.male }, { "groups", photo_data.groups } }) end
  if T(seated_data) then manifest.seated_talk = json.object({ { "male", seated_data.male }, { "female", seated_data.female } }) end
  if T(drink_data) then manifest.shared_drink = json.object({ { "male", drink_data.male }, { "female", drink_data.female } }) end
  if T(splash_data) then
    local s = json.object()
    for _, k in ipairs({ "male_throw", "male_receive", "female_throw", "female_receive", "cycle_seconds" }) do
      if splash_data[k] == nil then error("KeyError: '" .. k .. "'", 0) end
      s[k] = splash_data[k]
    end
    manifest.shared_splash = s
  end
  manifest.source_dialogue_catalogue = win(catalogue_path)
  if runtime_relationships then
    manifest.relationship_catalogue = win(runtime_relationships)
    manifest.source_relationship_catalogue = win(relationship_path)
  end
  if runtime_topics then
    manifest.topic_catalogue = win(runtime_topics)
    manifest.source_topic_catalogue = win(topic_path)
  end
  manifest.interaction_mode = "in_process"
  path.write_text(output, json.encode(manifest, { ensure_ascii = false, indent = 2 }))
  print(json.encode(json.object({ { "manifest", win(output) }, { "females", #females }, { "male", male.name },
    { "profiles", util.map(females, function(c) return c.personality end) } }), { ensure_ascii = false }))
end

if ok.is_main() then ok.main(function() M.main(arg) end) end

return M
