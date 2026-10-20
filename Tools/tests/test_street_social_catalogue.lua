local ok = require "ok"
local cat = require "build_street_social_catalogue"
local json, path, util = ok.json, ok.path, ok.util
local null = json.null

local T = ok.test.suite("street social catalogue")

local function tempdir()
  local d = path.temp("")
  path.mkdir(d)
  return d
end
local function is_none(v) return v == nil or v == null end

-- VoiceMapTests
T:case("positive_and_negative_folders_match_game_rule", function(t)
  t:eq(cat.computed_voice_folder(0), "c00")
  t:eq(cat.computed_voice_folder(7), "c07")
  t:eq(cat.computed_voice_folder(-13), "c-13")
  t:eq(cat.computed_voice_folder(-100), "c-100")
end)

T:case("table_match_and_conflict_do_not_guess", function(t)
  local tbl = {
    [0] = { FileName = "c00", Personality = "セクシー", No = 0 },
    [-13] = { FileName = "c-13", Personality = "案内人", No = -13 },
    [4] = { FileName = "c99", Personality = "mismatch", No = 4 },
  }
  t:eq(cat.resolve_voice_profile(0, tbl).folder, "c00")
  t:eq(cat.resolve_voice_profile(-13, tbl).status, "matched")
  local conflict = cat.resolve_voice_profile(4, tbl)
  t:eq(conflict.status, "conflict")
  t:truthy(is_none(conflict.folder))
  local missing = cat.resolve_voice_profile(99, tbl)
  t:eq(missing.status, "missing")
  t:truthy(is_none(missing.folder))
end)

T:case("clip_name_never_assigns_a_category", function(t)
  for _, name in ipairs({ "sun_com_00_000", "com_ev_00_000", "h_so_00_00_000", "map_yobikake_00_00" }) do
    t:is_nil(cat.category_from_clip_name(name))
  end
end)

-- VoiceSequenceTests
T:case("multipart_order_and_missing_middle_keep_a_gap", function(t)
  local root = tempdir()
  local folder = path.join(root, "c00", "adv")
  path.mkdir(folder)
  path.write_bytes(path.join(folder, "com_ev_00_000.ogg"), "OggSfake")
  path.write_bytes(path.join(folder, "com_ev_00_002.ogg"), "OggSfake")
  local index = {
    com_ev_00_000 = { "sound/data/pcm/c00/adv/00.unity3d", 22050, 1, 1.0 },
    com_ev_00_001 = { "sound/data/pcm/c00/adv/00.unity3d", 22050, 1, 1.0 },
    com_ev_00_002 = { "sound/data/pcm/c00/adv/00.unity3d", 22050, 1, 1.2 },
    other_voice = { "sound/data/pcm/c01/adv/00.unity3d", 22050, 1, 1.0 },
  }
  local sequence, files = cat.resolve_voice_sequence("com_ev_00_000,callName0,com_ev_00_001,com_ev_00_002",
    "sound/data/pcm/c00/adv/00.unity3d", "c00", index, root)
  t:eq(util.map(sequence, function(p) return p.token end), { "com_ev_00_000", "callName0", "com_ev_00_001", "com_ev_00_002" })
  t:eq(util.map(sequence, function(p) return p.status end), { "matched", "unsupported", "missing_ogg", "matched" })
  t:eq(#files, 2)
  t:truthy(util.endswith(files[1], "com_ev_00_000.ogg"))
  t:truthy(util.endswith(files[2], "com_ev_00_002.ogg"))
  t:not_contains(files[2], "other_voice")
  path.rmtree(root)
end)

T:case("abdata_prefix_matches_the_same_bundle", function(t)
  local root = tempdir()
  local folder = path.join(root, "c00", "adv")
  path.mkdir(folder)
  path.write_bytes(path.join(folder, "com_ev_00_000.ogg"), "OggSfake")
  local index = { com_ev_00_000 = { "abdata/sound/data/pcm/c00/adv/00.unity3d", 22050, 1, 1.0 } }
  local sequence, files = cat.resolve_voice_sequence("com_ev_00_000", "sound/data/pcm/c00/adv/00.unity3d", "c00", index, root)
  t:eq(sequence[1].status, "matched")
  t:eq(#files, 1)
  path.rmtree(root)
end)

T:case("integer_file_is_not_a_voice_lookup", function(t)
  local index = { ["0"] = { "abdata/sound/data/pcm/c00/adv/00.unity3d", 22050, 1, 1.0 } }
  local sequence, files = cat.resolve_voice_sequence("0", "adv/scenario/c00/00.unity3d", "c00", index, ".")
  t:eq(sequence[1].status, "not_voice")
  t:eq(files, {})
end)

T:case("bundle_or_profile_mismatch_is_not_used", function(t)
  local root = tempdir()
  local folder = path.join(root, "c01", "adv")
  path.mkdir(folder)
  path.write_bytes(path.join(folder, "com_ev_01_000.ogg"), "OggSfake")
  local index = { com_ev_01_000 = { "sound/data/pcm/c01/adv/00.unity3d", 22050, 1, 1.0 } }
  local sequence, files = cat.resolve_voice_sequence("com_ev_01_000", "sound/data/pcm/c00/adv/00.unity3d", "c00", index, root)
  t:eq(sequence[1].status, "bundle_mismatch")
  t:eq(files, {})
  path.rmtree(root)
end)

T:case("call_name_is_not_replaced_from_the_index", function(t)
  local root = tempdir()
  local index = { callName0 = { "sound/data/pcm/c00/adv/00.unity3d", 22050, 1, 0.2 } }
  local sequence, files = cat.resolve_voice_sequence("callName0", "sound/data/pcm/c00/adv/00.unity3d", "c00", index, root)
  t:eq(sequence[1].status, "unsupported")
  t:eq(files, {})
  path.rmtree(root)
end)

-- PlacementTests
local function rows_with(placements, category, kind)
  local out = {}
  for row, places in pairs(placements) do
    for _, p in ipairs(places) do
      if cat.category_for_placement(p.group, p.command, kind) == category then out[row] = true end
    end
  end
  return out
end
local function disjoint(a, b) for k in pairs(a) do if b[k] then return false end end return true end

T:case("heroine_walker_covers_real_table_length_without_guessing", function(t)
  local placements, errors = cat.place_heroine_rows(436)
  t:eq(errors, {})
  local categories = {}
  for row, places in pairs(placements) do
    for _, place in ipairs(places) do
      local category = cat.category_for_placement(place.group, place.command, "heroine")
      if category then categories[place.group .. "|" .. place.command .. "|" .. category] = true end
      t:is_nil(cat.category_from_clip_name("sun_com_" .. row))
    end
  end
  t:truthy(categories["Talk|0|daily"])
  t:truthy(categories["Talk|1|romance"])
  t:truthy(categories["Talk|2|intimacy"])
  t:truthy(categories["Introduction|0|greeting"])
  t:truthy(categories["EndConversation|0|farewell"])
  local unplaced = 0
  for row = 0, 435 do if placements[row] == nil then unplaced = unplaced + 1 end end
  t:greater_eq(unplaced, 10)
  local daily, romance, intimacy = rows_with(placements, "daily", "heroine"), rows_with(placements, "romance", "heroine"), rows_with(placements, "intimacy", "heroine")
  t:truthy(disjoint(daily, romance))
  t:truthy(disjoint(daily, intimacy))
  t:truthy(disjoint(romance, intimacy))
  t:eq(cat.category_for_placement(placements[0][1].group, placements[0][1].command, "heroine"), "greeting")
end)

T:case("short_table_is_an_error_and_does_not_wrap", function(t)
  local _, errors = cat.place_heroine_rows(10)
  t:truthy(#errors > 0)
  t:truthy(util.all(errors, function(e) return e.row >= 10 or e.row < 0 end))
end)

T:case("npc_talk_is_not_split_into_three_categories", function(t)
  local placements, errors = cat.place_npc_rows(400)
  t:eq(errors, {})
  local talk, intro = {}, {}
  for _, places in pairs(placements) do
    for _, place in ipairs(places) do
      local c = cat.category_for_placement(place.group, place.command, "npc")
      if place.group == "Talk" then talk[tostring(c)] = true end
      if place.group == "Introduction" then intro[tostring(c)] = true end
    end
  end
  t:eq(talk, { ["nil"] = true })
  t:eq(intro, { greeting = true })
end)

T:case("nodes_keep_schema_and_skip_unplaced_rows", function(t)
  local params = {}
  for index = 0, 435 do
    params[#params + 1] = json.object({
      { "priority", 0 }, { "conditions", index ~= 0 and -1 or 2 }, { "favor", 3 }, { "lewdness", 1 }, { "anger", 0 },
      { "assetbundle", "sound/data/pcm/c00/adv/00.unity3d" }, { "file", string.format("com_ev_00_%03d", index) },
      { "text", "line " .. index }, { "pose", "ベース00" }, { "facialExpression", "笑顔" }, { "eyeLook", 1 }, { "neckLook", 0 }, { "topic", -1 },
    })
  end
  local placements, errors = cat.place_heroine_rows(#params)
  t:eq(errors, {})
  local nodes, other = cat.build_nodes_for_table(0, params, placements, "heroine", "Communication_00", "communication/info_50.unity3d")
  t:truthy(util.any(other, function(item) return item.reason == "unplaced" end))
  t:truthy(#nodes > 0)
  local allowed = util.set({ "daily", "romance", "intimacy", "greeting", "farewell" })
  for _, node in ipairs(nodes) do
    t:eq(util.sorted(json.keys(node)), util.sorted({ "id", "personality", "category", "text_jp", "text_zh", "voice_files", "favor", "lewdness", "conditions", "source" }))
    t:truthy(allowed[node.category])
    t:eq(math.type(node.favor), "integer")
    t:eq(math.type(node.lewdness), "integer")
    t:eq(type(node.voice_files), "table")
    t:truthy(node.conditions.street_support ~= nil)
  end
  local greeting = nodes[1]
  t:eq(greeting.category, "greeting")
  t:eq(greeting.conditions.street_support, "unsupported")
  t:eq(greeting.conditions.id, 2)
end)

-- TranscodeTests
T:case("wav_transcode_when_ffmpeg_exists", function(t)
  local ffmpeg = cat.find_ffmpeg()
  if not ffmpeg then io.write("(skipped: ffmpeg is not available) "); return end
  local root = tempdir()
  local wav = path.join(root, "silence.wav")
  local data = string.rep("\0\0", 22050)
  path.write_bytes(wav, "RIFF" .. string.pack("<I4", 36 + #data) .. "WAVEfmt " .. string.pack("<I4I2I2I4I4I2I2", 16, 1, 1, 22050, 44100, 2, 16)
    .. "data" .. string.pack("<I4", #data) .. data)
  local dest = path.join(root, "silence.ogg")
  local digest, err = cat.transcode_wav(ffmpeg, wav, dest)
  t:is_nil(err)
  t:truthy(path.is_file(dest))
  t:eq(path.read_bytes(dest):sub(1, 4), "OggS")
  t:eq(#digest, 64)
  path.rmtree(root)
end)

-- CompletionSemanticsTests
T:case("unknown_speak_id_is_not_accepted_and_known_ids_keep_their_filter", function(t)
  local always = cat.condition_semantics(-1, "talk")
  local never = cat.condition_semantics(0, "talk")
  local nick = cat.condition_semantics(40, "talk")
  local other = cat.condition_semantics(7, "talk")
  t:eq(always.street_support, "supported")
  t:eq(always.speak_filter, "always")
  t:eq(never.speak_filter, "never")
  t:eq(nick.street_support, "unsupported")
  t:eq(other.street_support, "unsupported")
  t:eq(other.speak_filter, "original_default_true_not_accepted")
  local intro = cat.condition_semantics(-1, "introduction")
  t:eq(intro.rule, "fallback_shuffle_pool")
  t:ne(intro.rule, "always")
  t:eq(cat.condition_semantics(101, "introduction").street_support, "unsupported")
  t:eq(cat.condition_semantics(10, "farewell").inputs, { "favor >= stepUpDic[stage]" })
end)

T:case("branch_selection_exports_inputs_and_does_not_pick", function(t)
  local attribute = cat.branch_selection(1, 2, false)
  t:truthy(is_none(attribute.picked))
  t:eq(attribute.weights_when_matched, { 45, 20, 35 })
  t:eq(attribute.weights_when_not_matched, { 30, 30, 40 })
  t:eq(attribute.reachable_indexes, { 0, 1, 2 })
  local random_branch = cat.branch_selection(-1, 2, false)
  t:eq(random_branch.weights, { 20, 40, 40 })
  t:eq(random_branch.indexer[3].role, "failure")
  t:contains(random_branch.reachable_indexes, 2)
  t:truthy(is_none(random_branch.picked))
  local shared = cat.branch_selection(-1, 1, true)
  t:eq(shared.weights, { 30, 30, 40 })
  t:eq(shared.indexer[3].role, "no_interested")
  local temper = cat.branch_selection(13, 1, true)
  t:eq(temper.kind, "talk_temper")
  t:eq(temper.domain, { 0, 1 })
  t:eq(cat.branch_selection(5, 2, false).domain, { 0, 1, 2 })
  local preference = cat.branch_selection(7, 1, true)
  t:eq(preference.index_when_true, 1)
  t:eq(preference.index_when_false, 0)
  t:not_contains(preference.reachable_indexes, 2)
  t:eq(cat.branch_selection(41, 1, false).kind, "fixed_index")
  t:truthy(is_none(cat.branch_selection(41, 1, false).picked))
end)

T:case("gesture_mapping_is_exact_and_does_not_reuse_talk_gesture_00", function(t)
  local gestures = { f_talk_00_00_00 = { file = "talk_gesture_00.okanim", sha256 = "abc" } }
  local stand = cat.map_gesture("ベース21", { "adv/motion/controller/adv/00.unity3d", "cf_adv_00_00", "Stand_21_00" }, gestures)
  t:eq(stand.status, "missing_clip")
  t:truthy(is_none(stand.clip))
  t:eq(stand.source_state, "Stand_21_00")
  local exact = cat.map_gesture("会話00", { "action/animator/00.unity3d", "cf_talk", "f_talk_00_00_00" }, gestures)
  t:eq(exact.status, "matched")
  t:eq(exact.clip, "talk_gesture_00.okanim")
  t:eq(cat.map_gesture("", nil, gestures).status, "absent")
  t:eq(cat.map_gesture("ベース99", nil, gestures).status, "missing_pose")
  local performance = cat.map_performance("ベース21", { "adv/motion/controller/adv/00.unity3d", "cf_adv_00_00", "Stand_21_00" }, "笑顔", 1, 0, gestures)
  t:truthy(is_none(performance.expression.clip))
  t:eq(performance.expression.status, "source_only")
  t:eq(performance.look.eyes_pattern, 1)
  t:eq(performance.look.neck_rate, 0)
end)

T:case("portable_audio_keeps_order_and_refuses_invented_refs", function(t)
  local root = tempdir()
  local folder = path.join(root, "zh_ogg", "c00")
  path.mkdir(folder)
  path.write_bytes(path.join(folder, "com_ev_00_001.ogg"), "OggSkept")
  local sequence = {
    { index = 0, token = "com_ev_00_000", status = "missing_ogg", expected = "C:\\portable\\KoikatuSunshine\\voice_extract\\audio\\c00\\adv\\com_ev_00_000.ogg" },
    { index = 1, token = "callName0", status = "unsupported", reason = "runtime nickname" },
    { index = 2, token = "com_ev_00_001", status = "matched", kind = "adv", bundle = "abdata/sound/data/pcm/c00/adv/00.unity3d", rate = 22050, channels = 1, length = 1.2,
      path = "C:\\portable\\KoikatuSunshine\\voice_extract\\audio\\c00\\adv\\com_ev_00_001.ogg" },
  }
  local slots = cat.portable_audio_slots(sequence, "c00", root, {})
  t:eq(util.map(slots, function(s) return s.token end), { "com_ev_00_000", "callName0", "com_ev_00_001" })
  t:truthy(is_none(slots[1].zh_ref))
  t:eq(slots[2].zh_status, "unsupported")
  t:truthy(cat.is_portable_audio_ref(slots[3].zh_ref))
  t:not_contains(json.encode(slots), "KoikatuSunshine")
  t:falsy(cat.is_portable_audio_ref("C:\\portable\\KoikatuSunshine\\voice_extract\\audio\\c00\\adv\\x.ogg"))
  t:falsy(cat.is_portable_audio_ref("zh_ogg/c00/../c01/com_ev_00_001.ogg"))
  t:truthy(cat.is_portable_audio_ref("ja_ogg/c00/com_ev_00_001.ogg"))
  t:falsy(cat.is_portable_audio_ref("wav/c00/com_ev_00_001.ogg"))
  path.rmtree(root)
end)

T:case("ja_pack_copies_bytes_and_runtime_voice_keeps_holes", function(t)
  local root = tempdir()
  local source = path.join(root, "orig", "com_ev_00_001.ogg")
  path.mkdir(path.parent(source))
  local payload = "OggSsame-bytes"
  path.write_bytes(source, payload)
  local other = path.join(root, "orig", "com_ev_00_002.ogg")
  path.write_bytes(other, "OggSother-bytes")
  local content = path.join(root, "content")
  path.mkdir(path.join(content, "zh_ogg", "c00"))
  path.write_bytes(path.join(content, "zh_ogg", "c00", "com_ev_00_001.ogg"), "OggSzh-bytes")
  path.write_bytes(path.join(content, "zh_ogg", "c00", "com_ev_00_002.ogg"), "OggSzh-two")
  local sequence = {
    { index = 0, token = "com_ev_00_000", status = "missing_ogg", expected = "C:\\portable\\KoikatuSunshine\\voice_extract\\audio\\c00\\adv\\com_ev_00_000.ogg" },
    { index = 1, token = "callName0", status = "unsupported", reason = "runtime nickname" },
    { index = 2, token = "com_ev_00_001", status = "matched", kind = "adv", bundle = "abdata/sound/data/pcm/c00/adv/00.unity3d", rate = 22050, channels = 1, length = 1.2, path = source },
  }
  local cache = {}
  local slots = cat.portable_audio_slots(sequence, "c00", content, {}, cache)
  t:truthy(is_none(slots[1].ja_ref))
  t:eq(slots[1].ja_status, "not_applicable")
  t:eq(slots[2].ja_status, "unsupported")
  t:eq(slots[3].ja_ref, "ja_ogg/c00/com_ev_00_001.ogg")
  t:eq(slots[3].ja_status, "copied")
  t:eq(path.read_bytes(path.join(content, slots[3].ja_ref)), payload)
  local again = cat.portable_audio_slots(sequence, "c00", content, {}, cache)
  t:eq(again[3].ja_status, "reused")
  local voice_sequence, files, files_zh = cat.bind_runtime_voice(slots)
  t:eq(util.map(voice_sequence, function(item) return item.index end), { 0, 1, 2 })
  t:eq(voice_sequence[1].status, "missing_ogg")
  t:truthy(is_none(voice_sequence[1].path))
  t:truthy(is_none(voice_sequence[2].zh_ogg))
  t:eq(voice_sequence[3].path, "ja_ogg/c00/com_ev_00_001.ogg")
  t:is_nil(files)
  t:is_nil(files_zh)
  local dumped = json.encode({ slots = slots, sequence = voice_sequence })
  t:not_contains(dumped, "KoikatuSunshine")
  t:not_contains(dumped, "voice_extract")
  local complete = {
    { index = 0, token = "com_ev_00_001", status = "matched", kind = "adv", bundle = "abdata/sound/data/pcm/c00/adv/00.unity3d", path = source },
    { index = 1, token = "com_ev_00_002", status = "matched", kind = "adv", bundle = "abdata/sound/data/pcm/c00/adv/00.unity3d", path = other },
  }
  local packed = cat.portable_audio_slots(complete, "c00", content, {}, cache)
  local sequence_ok, voice_files, voice_files_zh = cat.bind_runtime_voice(packed)
  t:eq(voice_files, { "ja_ogg/c00/com_ev_00_001.ogg", "ja_ogg/c00/com_ev_00_002.ogg" })
  t:eq(voice_files_zh, { "zh_ogg/c00/com_ev_00_001.ogg", "zh_ogg/c00/com_ev_00_002.ogg" })
  t:truthy(util.all(util.extend(util.copy(voice_files), voice_files_zh), cat.is_portable_audio_ref))
  local occupied = path.join(content, "ja_ogg", "c00", "com_ev_00_002.ogg")
  path.write_bytes(occupied, "OggSdifferent")
  local conflict = cat.portable_audio_slots({ complete[2] }, "c00", content, {}, {})
  t:eq(conflict[1].ja_status, "sha_conflict")
  t:truthy(is_none(conflict[1].ja_ref))
  t:eq(path.read_bytes(occupied), "OggSdifferent")
  local view = cat.japanese_catalogue({
    schema = 2, audio_schema = cat.AUDIO_SCHEMA, generator = "test", audio_root = "Content/IndependentResources/StreetSocialData",
    nodes = { { id = "line", voice_files = voice_files, voice_files_zh = voice_files_zh, audio_slots = packed, source = { voice_sequence = sequence_ok } } },
  })
  t:eq(view.nodes[1].voice_language, "ja")
  t:truthy(is_none(view.nodes[1].source.voice_sequence[1].zh_ogg))
  t:eq(view.nodes[1].voice_files, voice_files)
  t:eq(sequence_ok[1].zh_ogg, "zh_ogg/c00/com_ev_00_001.ogg")
  local empty_sequence, empty_files, empty_zh = cat.bind_runtime_voice({})
  t:eq(empty_sequence, {})
  t:eq(empty_files, {})
  t:eq(empty_zh, {})
  path.rmtree(root)
end)

T:case("branch_groups_keep_every_talk_row_and_the_introduction_gate", function(t)
  local placements, errors = cat.place_heroine_rows(436)
  t:eq(errors, {})
  local conditions = {}
  for row = 0, 435 do conditions[row] = row == 14 and 7 or -1 end
  local branches = cat.build_branch_records(0, placements, conditions, "heroine")
  local talk_rows, grouped_rows = {}, {}
  for row, places in pairs(placements) do
    for _, place in ipairs(places) do if place.group == "Talk" then talk_rows[row] = true end end
  end
  for _, branch in ipairs(branches) do
    if branch.group == "Talk" then for _, member in ipairs(branch.members) do grouped_rows[member.row] = true end end
  end
  t:eq(talk_rows, grouped_rows)
  local first
  for _, branch in ipairs(branches) do
    if branch.group == "Talk" and branch.command == 0 and branch.stage == 0
      and util.any(branch.members, function(m) return m.role == "introduction" and m.row == 14 end) then first = branch break end
  end
  t:truthy(first, "StopIteration")
  t:eq(first.controlling_row, 14)
  t:eq(first.controlling_condition.id, 7)
  t:eq(first.selection.kind, "preference_compare")
  t:truthy(is_none(first.selection.picked))
  t:eq(cat.friendship_command().mechanism, "end_conversation_rank_up")
  t:truthy(is_none(cat.friendship_command().command_func))
  t:eq(cat.confession_command().command_func, 11)
  t:eq(cat.confession_command().event_command, 6)
  t:truthy(is_none(cat.confession_command().label_jp))
end)

T:case("reachability_counts_only_supported_voiced_lines", function(t)
  local function node(row, category, condition, voiced)
    return {
      id = string.format("p0_r%s_%s", row, category), personality = 0, category = category, conditions = { id = condition },
      audio_slots = voiced and { { status = "matched", zh_ref = "zh_ogg/c00/a.ogg" } } or { { status = "missing_ogg", zh_ref = null } },
      source = { row = row, priority = 10, group = "Talk", stage = 0 },
    }
  end
  local nodes = { node(1, "daily", -1, true), node(2, "daily", -1, true), node(3, "daily", -1, true), node(4, "daily", 0, true), node(5, "romance", -1, false) }
  local branches = {}
  for _, spec in ipairs({ { 1, "daily", -1 }, { 2, "daily", -1 }, { 3, "daily", -1 }, { 4, "daily", 0 } }) do
    local row, category, condition = spec[1], spec[2], spec[3]
    branches[#branches + 1] = {
      id = "b" .. row, personality = 0, stage = 0, group = "Talk", command = category == "daily" and 0 or 1, category = category,
      controlling_row = row, controlling_condition = cat.condition_semantics(condition, "talk"), selection = cat.branch_selection(-1, 1, false),
      indexer = { { row = row, role = row == 3 and "failure" or "success", part_index = 0 } },
      members = { { row = row, role = "success", part_index = 0 } },
    }
  end
  branches[#branches + 1] = {
    id = "romance-silent", personality = 0, stage = 0, group = "Talk", command = 1, category = "romance", controlling_row = 5,
    controlling_condition = cat.condition_semantics(-1, "talk"), selection = cat.branch_selection(-1, 1, false),
    indexer = { { row = 5, role = "success", part_index = 0 } }, members = { { row = 5, role = "success", part_index = 0 } },
  }
  branches[#branches + 1] = {
    id = "hello", personality = 0, stage = 0, group = "Introduction", command = 0, category = "greeting", controlling_row = 1,
    controlling_condition = cat.condition_semantics(-1, "introduction"), selection = null, indexer = {},
    members = { { row = 1, role = "generic", part_index = 0 } }, source_rows = { 1 },
  }
  nodes[1].source = { row = 1, priority = 1, group = "Introduction", stage = 0 }
  local farewell = node(9, "farewell", 3, true)
  farewell.source = { row = 9, priority = 5, group = "EndConversation", stage = 0 }
  nodes[#nodes + 1] = farewell
  branches[#branches + 1] = {
    id = "bye", personality = 0, stage = 0, group = "EndConversation", command = 0, category = "farewell", controlling_row = 9,
    controlling_condition = cat.condition_semantics(3, "farewell"), selection = null, indexer = {},
    members = { { row = 9, role = "generic", part_index = 0 } },
  }
  local report = cat.evaluate_reachability(0, nodes, branches, { [0] = 75, [1] = 100, [2] = 150 })
  t:eq(report.topics_distinct_rows.daily, 3)
  t:eq(report.topics_distinct_rows.romance, 0)
  t:falsy(report.topics_at_least_three)
  t:truthy(report.refusal_reachable)
  t:truthy(report.greeting_reachable)
  t:truthy(report.farewell_reachable)
  t:falsy(report.friendship_spoken_at_cap)
  t:falsy(report.friendship_row_selected_at_cap)
end)

T:case("header_lists_the_same_gesture_states_and_weights", function(t)
  local text = path.read_text(path.join(ok.root, "Source", "OpenKoikatsu", "OKStreetSocialPerformance.h"))
  for _, g in ipairs(cat.GESTURE_SOURCE) do
    t:contains(text, g[1])
    t:contains(text, g[2])
  end
  t:contains(text, "20, 40, 40")
  t:contains(text, "30, 30, 40")
  t:contains(text, "OKStreetGestureForState")
  t:contains(text, "OKStreetIsPortableAudioRef")
  t:contains(text, "ja_ogg/")
  t:not_contains(text, "Stand_00_00")
end)

T:run()
