-- Export KKS communication tables joined to voice_extract audio.
--
-- The row walker follows ActionGame.Communication.Info.LoadData and
-- LoadCommunicationNPCData. Clip prefixes are not categories. callName
-- tokens stay unresolved: nickname audio is chosen at runtime.
local ok = require "ok"
local json, path, util, kit, proc = ok.json, ok.path, ok.util, ok.kit, ok.proc
local null = json.null
local T = util.truthy
local M = {}

-- Python helpers ---------------------------------------------------------------
local function none(v) return v == nil or v == null end
local function N(v) if v == nil then return null end return v end
-- Python `v or d`.
local function orv(v, d) if T(v) then return v end return d end
local function obj(list) return json.object(list) end
local function is_list(v) return type(v) == "table" and v ~= null and not json.is_object(v) end
local function is_dict(v) return json.is_object(v) or (type(v) == "table" and v ~= null and next(v) ~= nil and #v == 0) end
local function pystr(v) return util.str(v) end

-- Shallow copy that keeps key order (dict(x)).
local function ocopy(t)
  local o = json.object()
  if json.is_object(t) then
    for k, v in pairs(t) do o[k] = v end
  else
    for _, k in ipairs(util.sorted_keys(t)) do o[k] = t[k] end
  end
  return o
end

-- str.strip() whitespace (ASCII plus the Unicode spaces Python strips).
local WS = { "\194\133", "\194\160", "\225\154\128", "\226\128\168", "\226\128\169", "\226\128\175", "\226\129\159", "\227\128\128" }
for c = 0x80, 0x8A do WS[#WS + 1] = "\226\128" .. string.char(c) end
local function pystrip(s)
  local changed = true
  while changed do
    changed = false
    local a = s:match("^[%s\28-\31]+")
    if a then s = s:sub(#a + 1); changed = true end
    local b = s:match("[%s\28-\31]+$")
    if b then s = s:sub(1, #s - #b); changed = true end
    for _, w in ipairs(WS) do
      if s:sub(1, #w) == w then s = s:sub(#w + 1); changed = true end
      if #s >= #w and s:sub(-#w) == w then s = s:sub(1, #s - #w); changed = true end
    end
  end
  return s
end

-- bytes.decode("utf-8", "replace")
local function utf8_replace(s)
  if utf8.len(s) then return s end
  local out, i, n = {}, 1, #s
  while i <= n do
    local c = s:byte(i)
    local len = c < 0x80 and 1 or (c >= 0xC2 and c <= 0xDF) and 2 or (c >= 0xE0 and c <= 0xEF) and 3 or (c >= 0xF0 and c <= 0xF4) and 4 or 0
    local seq = len > 0 and s:sub(i, i + len - 1) or nil
    if seq and #seq == len and utf8.len(seq) then out[#out + 1] = seq; i = i + len
    else out[#out + 1] = "\239\191\189"; i = i + 1 end
  end
  return table.concat(out)
end

-- str.splitlines()
local function pysplitlines(s)
  s = s:gsub("\r\n", "\n"):gsub("[\r\11\12\28\29\30]", "\n"):gsub("\194\133", "\n"):gsub("\226\128[\168\169]", "\n")
  local t = {}
  for line in (s .. "\n"):gmatch("(.-)\n") do t[#t + 1] = line end
  if s:sub(-1) == "\n" or s == "" then t[#t] = nil end
  return t
end

-- int(x) that returns nil where Python raises TypeError/ValueError.
local function pyint(v)
  if type(v) == "number" then
    if v ~= v or v == math.huge or v == -math.huge then return nil end
    return math.tointeger(v >= 0 and math.floor(v) or math.ceil(v))
  end
  if type(v) == "boolean" then return v and 1 or 0 end
  if type(v) == "string" then
    local s = pystrip(v)
    if not s:match("^[+-]?%d[%d_]*$") or s:find("__", 1, true) or s:sub(-1) == "_" then return nil end
    return math.tointeger(tonumber((s:gsub("_", ""))))
  end
  return nil
end
local function need_int(v)
  local n = pyint(v)
  if n == nil then error("invalid literal for int(): " .. util.repr(v), 2) end
  return n
end

-- float(str) or nil on ValueError.
local function pyfloat(v)
  if type(v) == "number" then return v + 0.0 end
  if type(v) ~= "string" then return nil end
  local s = pystrip(v):lower():gsub("_", "")
  local sign = 1
  local body = s
  if body:sub(1, 1) == "+" then body = body:sub(2) elseif body:sub(1, 1) == "-" then sign = -1; body = body:sub(2) end
  if body == "inf" or body == "infinity" then return sign * math.huge end
  if body == "nan" then return 0 / 0 end
  if not (body:match("^%d+%.?%d*$") or body:match("^%.%d+$") or body:match("^%d+%.?%d*e[+-]?%d+$") or body:match("^%.%d+e[+-]?%d+$")) then return nil end
  return sign * (tonumber(body) + 0.0)
end

-- str(Path(p)) on Windows.
local function winpath(p)
  p = tostring(p):gsub("/", "\\")
  local prefix = ""
  if p:sub(1, 2) == "\\\\" then prefix = "\\\\"; p = p:sub(3) end
  p = p:gsub("\\+", "\\")
  local n
  repeat p, n = p:gsub("\\%.\\", "\\") until n == 0
  p = p:gsub("^%.\\", ""):gsub("\\%.$", "")
  if not p:match("^%a:\\$") then p = p:gsub("\\$", "") end
  if p == "" then p = "." end
  return prefix .. p
end
-- str(Path(p).resolve())
local function resolved(p) return winpath(path.abs(p)) end

-- Sorted Path.glob() (Windows paths sort case-insensitively).
local function sorted_glob(dir, pattern)
  local list = path.glob(dir, pattern)
  return util.sorted(list, function(p) return winpath(p):lower() end)
end

-- Ordered counter (collections.Counter -> dict) with None mapped like json.dumps does.
local function counter()
  return json.object()
end
local function count(c, k, n)
  if k == nil or k == null then k = "null" end
  c[k] = (c[k] or 0) + (n or 1)
end
local function slice(t, n)
  local out = {}
  for i = 1, math.min(n, #t) do out[i] = t[i] end
  return out
end
local function ulast(s, k)
  local n = utf8.len(s)
  if not n or n <= k then return s end
  return s:sub(utf8.offset(s, n - k + 1))
end

-- Constants -------------------------------------------------------------------
M.SCHEMA = 1
local SCHEMA = M.SCHEMA
local function call_name(token) return token:match("^callName%d*$") ~= nil end

-- TalkScene.buttonTalkContents index -> CommandFunc -> Group.Talk command.
-- Labels were read from KoikatsuSunshine_Data/level6 TextMeshPro children:
-- Button Talk 0 = 世間話, Button Talk 1 = コイバナ, Button Talk 2 = エッチ.
-- Button Talk 2 is hidden when Game.isAddH is false (TalkScene.cs).
local TALK_COMMAND_CATEGORY = { [0] = "daily", [1] = "romance", [2] = "intimacy" }
local TALK_COMMAND_LABEL = { [0] = "世間話", [1] = "コイバナ", [2] = "エッチ" }
local GROUP_CATEGORY = { Introduction = "greeting", EndConversation = "farewell" }
M.TALK_COMMAND_CATEGORY, M.TALK_COMMAND_LABEL, M.GROUP_CATEGORY = TALK_COMMAND_CATEGORY, TALK_COMMAND_LABEL, GROUP_CATEGORY

-- ConditionDeterminationSpeak. Anything else returns true in the original
-- game; this exporter does not treat that default as street support.
local SPEAK_SUPPORTED = obj({ { -1, "always" }, { 0, "never" } })
local SPEAK_DESCRIBED_UNSUPPORTED = obj({ { 40, "nickname_not_normal_and_not_event" }, { 41, "nickname_not_normal_and_event_54" } })
M.SPEAK_SUPPORTED, M.SPEAK_DESCRIBED_UNSUPPORTED = SPEAK_SUPPORTED, SPEAK_DESCRIBED_UNSUPPORTED

-- Manager.Voice plus IntExtensions.MinusThroughToString("00").
function M.computed_voice_folder(personality)
  personality = need_int(personality)
  if personality < 0 then return "c" .. tostring(personality) end
  return "c" .. string.format("%02d", personality)
end

function M.resolve_voice_profile(personality, voice_table)
  local row = voice_table[need_int(personality)]
  local computed = M.computed_voice_folder(personality)
  if row == nil then
    return obj({ { "status", "missing" }, { "folder", null }, { "computed", computed }, { "personality_name", null }, { "file_name", null } })
  end
  local file_name = pystr(orv(row.FileName, ""))
  local name = N(row.Personality)
  if file_name == "" or file_name ~= computed then
    return obj({ { "status", "conflict" }, { "folder", null }, { "computed", computed }, { "personality_name", name },
      { "file_name", file_name ~= "" and file_name or null } })
  end
  return obj({ { "status", "matched" }, { "folder", file_name }, { "computed", computed }, { "personality_name", name }, { "file_name", file_name } })
end

-- Filenames do not identify daily, romance, or intimacy.
function M.category_from_clip_name(_clip) return nil end

function M.category_for_placement(group, command, table_kind)
  if table_kind == "npc" and group == "Talk" then return nil end
  if group == "Talk" then return TALK_COMMAND_CATEGORY[need_int(command)] end
  if need_int(command) ~= 0 then return nil end
  return GROUP_CATEGORY[group]
end

function M.speak_support(condition_id)
  condition_id = need_int(condition_id)
  if SPEAK_SUPPORTED[condition_id] then
    return obj({ { "id", condition_id }, { "game_speak_filter", SPEAK_SUPPORTED[condition_id] }, { "street_support", "supported" } })
  end
  if SPEAK_DESCRIBED_UNSUPPORTED[condition_id] then
    return obj({ { "id", condition_id }, { "game_speak_filter", SPEAK_DESCRIBED_UNSUPPORTED[condition_id] }, { "street_support", "unsupported" } })
  end
  return obj({ { "id", condition_id }, { "game_speak_filter", "original_default_true_not_accepted" }, { "street_support", "unsupported" } })
end

function M.split_file_field(file_field)
  if none(file_field) then return {} end
  local text = pystrip(pystr(file_field))
  if text == "" then return {} end
  local out = {}
  for _, part in ipairs(util.split(text, ",")) do
    part = pystrip(part)
    if part ~= "" then out[#out + 1] = part end
  end
  return out
end

function M.pcm_folder_kind(bundle)
  local parts = {}
  for _, part in ipairs(util.split(pystr(orv(bundle, "")):gsub("\\", "/"), "/")) do
    if part ~= "" then parts[#parts + 1] = part end
  end
  local index = util.index_of(parts, "pcm")
  if not index then return nil, nil end
  if index + 1 >= #parts then return nil, nil end
  return parts[index + 1], parts[index + 2]
end

-- Communication rows omit the abdata root that audio_index keeps.
function M.normalize_bundle(bundle)
  local text = pystr(orv(bundle, "")):gsub("\\", "/"):gsub("^/+", ""):lower()
  if text:sub(1, 7) == "abdata/" then text = text:sub(8) end
  return text
end

local function ogg_magic_ok(p)
  local f = io.open(path.norm(p), "rb")
  if not f then return false end
  local head = f:read(4)
  f:close()
  return head == "OggS"
end
M._ogg_magic_ok = ogg_magic_ok

-- Return (sequence, voice_files). voice_files keeps matched ogg order only.
function M.resolve_voice_sequence(file_field, assetbundle, voice_folder, audio_index, audio_root)
  local sequence, voice_files = {}, {}
  for i, token in ipairs(M.split_file_field(file_field)) do
    local index = i - 1
    if call_name(token) then
      sequence[#sequence + 1] = obj({ { "index", index }, { "token", token }, { "status", "unsupported" },
        { "reason", "callName is Heroine.CallNameChange runtime nickname audio (scenario vars callBundle and callNameN). No ogg is selected." } })
      goto continue
    end
    if token:match("^%d+$") then
      sequence[#sequence + 1] = obj({ { "index", index }, { "token", token }, { "status", "not_voice" },
        { "reason", "Integer file with an adv/scenario bundle is an event id or scenario open (Info.AddTransfer), not a pcm clip." } })
      goto continue
    end
    do
      local entry = (not none(audio_index)) and audio_index[token] or nil
      if not T(entry) or not is_list(entry) or #entry < 4 then
        sequence[#sequence + 1] = obj({ { "index", index }, { "token", token }, { "status", "missing_index" } })
        goto continue
      end
      local bundle = pystr(orv(entry[1], ""))
      local row_bundle = pystr(orv(assetbundle, ""))
      if row_bundle ~= "" and M.normalize_bundle(bundle) ~= M.normalize_bundle(row_bundle) then
        sequence[#sequence + 1] = obj({ { "index", index }, { "token", token }, { "status", "bundle_mismatch" },
          { "index_bundle", bundle }, { "row_bundle", row_bundle } })
        goto continue
      end
      local folder, kind = M.pcm_folder_kind(bundle)
      if not folder or not kind then
        sequence[#sequence + 1] = obj({ { "index", index }, { "token", token }, { "status", "unparsed_bundle" }, { "index_bundle", bundle } })
        goto continue
      end
      if not T(voice_folder) or folder:lower() ~= pystr(voice_folder):lower() then
        sequence[#sequence + 1] = obj({ { "index", index }, { "token", token }, { "status", "voice_profile_mismatch" },
          { "clip_folder", folder }, { "voice_folder", N(voice_folder) } })
        goto continue
      end
      local ogg = path.join(audio_root, folder, kind, token .. ".ogg")
      if not path.is_file(ogg) or (path.size(ogg) or 0) <= 0 or not ogg_magic_ok(ogg) then
        sequence[#sequence + 1] = obj({ { "index", index }, { "token", token }, { "status", "missing_ogg" },
          { "expected", winpath(ogg) }, { "kind", kind }, { "rate", N(entry[2]) }, { "channels", N(entry[3]) }, { "length", N(entry[4]) } })
        goto continue
      end
      local p = resolved(ogg)
      sequence[#sequence + 1] = obj({ { "index", index }, { "token", token }, { "status", "matched" }, { "path", p },
        { "bundle", bundle }, { "kind", kind }, { "rate", N(entry[2]) }, { "channels", N(entry[3]) }, { "length", N(entry[4]) } })
      voice_files[#voice_files + 1] = p
    end
    ::continue::
  end
  return sequence, voice_files
end

-- Row placement -----------------------------------------------------------------
local function new_placer(nrows)
  local placer = { nrows = need_int(nrows), placements = json.object(), errors = {} }
  function placer:read(row, placement)
    row = need_int(row)
    if row < 0 or row >= self.nrows then
      self.errors[#self.errors + 1] = obj({ { "row", row }, { "reason", "row_outside_table" }, { "placement", obj({
        { "group", N(placement.group) }, { "command", N(placement.command) }, { "role", N(placement.role) }, { "stage", N(placement.stage) } }) } })
      return false
    end
    local list = self.placements[row]
    if list == nil then list = {}; self.placements[row] = list end
    local copy = {}
    for k, v in pairs(placement) do copy[k] = v end
    list[#list + 1] = copy
    return true
  end
  return placer
end

local function base(stage, group, command, container, role, part_index)
  return { stage = need_int(stage), group = group, command = need_int(command), container = need_int(container), role = role, part_index = need_int(part_index or 0) }
end

-- Transcription of Info.LoadData. Returns placements and layout errors.
function M.place_heroine_rows(nrows)
  local placer = new_placer(nrows)
  local row = 0

  local function load_generic_only(start, stage, group, num, command)
    command = command or 0
    local cursor = need_int(start)
    for _ = 1, num do
      placer:read(cursor, base(stage, group, command, cursor, "generic", 0))
      cursor = cursor + 1
    end
    return cursor
  end
  local function create_branch(success_num, failure_row, stage, group, command)
    local container = row
    placer:read(row, base(stage, group, command, container, "introduction", 0))
    row = row + 1
    for index = 0, success_num - 1 do
      placer:read(row, base(stage, group, command, container, "success", index))
      row = row + 1
    end
    placer:read(failure_row, base(stage, group, command, container, "failure", 0))
  end
  local function create_talk(stage, group, command, no_interested_row, introduction)
    if no_interested_row == nil then no_interested_row = -1 end
    if introduction == nil then introduction = true end
    local container = row
    if introduction then
      placer:read(row, base(stage, group, command, container, "introduction", 0)); row = row + 1
      placer:read(row, base(stage, group, command, container, "success", 0)); row = row + 1
    else
      placer:read(row, base(stage, group, command, container, "success", 0)); row = row + 1
    end
    placer:read(row, base(stage, group, command, container, "failure", 0)); row = row + 1
    if no_interested_row ~= -1 then
      placer:read(no_interested_row, base(stage, group, command, container, "no_interested", 0))
    end
  end
  local function create_generic(num, stage, group, command)
    local container = row
    for index = 0, num - 1 do
      placer:read(row, base(stage, group, command, container, "generic", index)); row = row + 1
    end
  end
  local function create_generic_at(at, stage, group, command)
    placer:read(at, base(stage, group, command, at, "generic", 0))
  end
  local function create_select(num, stage, group, command)
    local container = row
    placer:read(row, base(stage, group, command, container, "select_intro", 0)); row = row + 1
    for index = 0, num - 1 do
      local item = base(stage, group, command, container, "choice", index)
      item.label_row = container
      placer:read(row, item); row = row + 1
    end
  end
  local function load_touch(stage, group, num)
    local shared = row + 3 * num
    for command = 0, num - 1 do
      for _ = 1, 3 do
        create_generic_at(row, stage, group, command); row = row + 1
      end
      create_generic_at(shared, stage, group, command)
    end
    row = row + 1
  end
  local function load_event(stage, command, branches, selects)
    for _ = 1, branches do
      local failure = row + 2
      create_branch(1, failure, stage, "Event", command); row = row + 1
    end
    for _ = 1, selects do create_select(2, stage, "Event", command) end
  end
  local function talk_stage0(command, branches, generics, extra_branches)
    extra_branches = extra_branches or 0
    local failure = row + (3 * branches)
    for _ = 1, branches do create_branch(2, failure, 0, "Talk", command) end
    row = row + 1
    for _ = 1, generics do create_generic(2, 0, "Talk", command) end
    for _ = 1, extra_branches do
      local f = row + 2
      create_branch(1, f, 0, "Talk", command); row = row + 1
    end
  end
  local function talk_stage(stage, command, generics_after)
    local no_interested = row + 12
    for _ = 1, 4 do create_talk(stage, "Talk", command, no_interested, true) end
    row = row + 1
    for _ = 1, generics_after do create_generic(2, stage, "Talk", command) end
  end

  load_generic_only(row, 0, "Introduction", 14)
  row = 14
  talk_stage0(0, 2, 3, 0)
  talk_stage0(1, 3, 3, 6)
  talk_stage0(2, 3, 3, 0)
  load_touch(0, "See", 2)
  load_touch(0, "Touch", 4)
  row = load_generic_only(row, 0, "Interlude", 2)
  row = load_generic_only(row, 0, "LeaveAlone", 2)
  row = load_generic_only(row, 0, "EndConversation", 9)
  row = load_generic_only(row, 0, "EndConversation", 1)
  for command = 0, 4 do load_event(0, command, 1, 0) end
  row = load_generic_only(row, 1, "Introduction", 17)
  talk_stage(1, 0, 1)
  talk_stage(1, 1, 1)
  talk_stage(1, 2, 1)
  load_touch(1, "See", 2)
  load_touch(1, "Touch", 4)
  row = load_generic_only(row, 1, "Interlude", 3)
  row = load_generic_only(row, 1, "LeaveAlone", 2)
  row = load_generic_only(row, 1, "EndConversation", 9)
  load_event(1, 6, 2, 1)
  load_event(1, 7, 4, 1)
  -- Info.LoadData includes a two-choice heroine invitation after each
  -- player invitation for commands 0..3. Omitting it shifts every later row.
  for command = 0, 4 do load_event(1, command, 1, command < 4 and 1 or 0) end
  row = load_generic_only(row, 2, "Introduction", 17)
  talk_stage(2, 0, 2)
  talk_stage(2, 1, 2)
  talk_stage(2, 2, 2)
  load_touch(2, "See", 2)
  load_touch(2, "Touch", 4)
  row = load_generic_only(row, 2, "Interlude", 3)
  row = load_generic_only(row, 2, "LeaveAlone", 2)
  row = load_generic_only(row, 2, "EndConversation", 9)
  create_generic(1, 2, "Event", 7)
  local num11 = row
  create_talk(2, "Event", 7, -1, false)
  local num12 = row
  create_select(2, 2, "Event", 7)
  for _ = 1, 2 do
    local failure = row + 2
    create_branch(1, failure, 2, "Event", 7); row = row + 1
  end
  for command = 0, 3 do
    create_generic(1, 2, "Event", command)
    create_select(2, 2, "Event", command)
  end
  local failure = row + 2
  create_branch(1, failure, 2, "Event", 8); row = row + 1
  failure = row + 2
  create_branch(1, failure, 2, "Event", 4); row = row + 1
  row = row + 10
  local num14 = row
  load_generic_only(num14 + 12, 0, "Introduction", 1)
  load_generic_only(num14 + 10, 1, "Introduction", 1)
  load_generic_only(num14 + 13, 1, "Introduction", 2)
  load_generic_only(num14 + 11, 2, "Introduction", 1)
  load_generic_only(num14 + 15, 2, "Introduction", 2)
  create_generic(1, 2, "Talk", 0)
  create_talk(2, "Talk", 0, -1, true)
  create_generic(1, 2, "Interlude", 0)
  create_generic(3, 2, "Interlude", 0)
  -- Patched copies. CreateTalk/CreateSelect re-read earlier event rows and
  -- then replace one child from num14. Those rows stay in Event.
  local saved = row
  row = num11
  create_talk(2, "Event", 7, -1, false)
  row = num12
  create_select(2, 2, "Event", 7)
  row = saved
  placer:read(num14 + 8, base(2, "Event", 7, num11, "patched_success", 0))
  placer:read(num14 + 9, base(2, "Event", 7, num12, "patched_intro", 0))
  return placer.placements, placer.errors
end

-- Transcription of Info.LoadCommunicationNPCData.
function M.place_npc_rows(nrows)
  local placer = new_placer(nrows)
  local row = 0
  local function load_generic(stage, group, num)
    for _ = 1, num do placer:read(row, base(stage, group, 0, row, "generic", 0)); row = row + 1 end
  end
  local function load_generic_len(stage, group, num, length)
    for _ = 1, num do
      local container = row
      for index = 0, length - 1 do placer:read(row, base(stage, group, 0, container, "generic", index)); row = row + 1 end
    end
  end
  local function load_generic_adv(stage, group, command, length)
    local container = row
    for index = 0, length - 1 do placer:read(row, base(stage, group, command, container, "generic", index)); row = row + 1 end
  end
  load_generic(0, "Introduction", 3)
  load_generic_len(0, "Listen", 3, 2)
  load_generic(0, "EndConversation", 1)
  load_generic(1, "Introduction", 6)
  load_generic_len(1, "Listen", 3, 2)
  load_generic(1, "EndConversation", 1)
  load_generic(2, "Introduction", 6)
  load_generic_len(2, "Listen", 4, 2)
  load_generic(2, "EndConversation", 1)
  load_generic(3, "Introduction", 6)
  load_generic_len(3, "Listen", 4, 2)
  load_generic(3, "EndConversation", 1)
  load_generic_adv(0, "RankUp", 0, 11)
  load_generic_adv(1, "RankUp", 0, 10)
  load_generic_adv(2, "RankUp", 0, 15)
  load_generic_len(0, "Talk", 7, 2)
  load_generic_len(1, "Talk", 8, 2)
  for _, length in ipairs({ 4, 3, 5, 4, 4, 3, 4, 4 }) do load_generic_adv(0, "Tips", 0, length) end
  for _, length in ipairs({ 4, 3, 5, 4, 4, 3, 4, 4 }) do load_generic_adv(1, "Tips", 0, length) end
  load_generic_adv(3, "Event", 0, 8)
  load_generic_adv(3, "Event", 1, 2)
  load_generic_adv(3, "Event", 8, 2)
  for _, stage in ipairs({ 0, 1, 3 }) do
    load_generic_adv(stage, "See", 0, 1)
    load_generic_adv(stage, "See", 1, 1)
    for command = 0, 3 do load_generic_adv(stage, "Touch", command, 1) end
    load_generic(stage, "LeaveAlone", 1)
  end
  return placer.placements, placer.errors
end

local function int_field(row, key, default)
  local v = row[key]
  if none(v) then return default end
  local n = pyint(v)
  if n == nil then return default end
  return n
end
M._int_field = int_field

local function sorted_unique(list)
  local seen, out = {}, {}
  for _, v in ipairs(list) do if not seen[v] then seen[v] = true; out[#out + 1] = v end end
  return util.sorted(out)
end

-- One catalogue node per row and category. Unmapped rows are not nodes.
function M.build_nodes_for_table(personality, params, placements, table_kind, source_asset, source_bundle)
  local nodes, other = {}, {}
  for row_index = 0, #params - 1 do
    local places = placements[row_index] or {}
    if #places == 0 then
      other[#other + 1] = obj({ { "row", row_index }, { "reason", "unplaced" } })
      goto continue
    end
    do
      local cats, by_category, unmapped = {}, {}, {}
      for _, place in ipairs(places) do
        local category = M.category_for_placement(place.group, place.command, table_kind)
        if category == nil then unmapped[#unmapped + 1] = place
        else
          if not by_category[category] then by_category[category] = {}; cats[#cats + 1] = category end
          table.insert(by_category[category], place)
        end
      end
      if #unmapped > 0 and #cats == 0 then
        other[#other + 1] = obj({ { "row", row_index }, { "reason", "not_in_street_categories" },
          { "groups", sorted_unique(util.map(unmapped, function(item) return item.group end)) } })
      end
      local param = params[row_index + 1]
      if not T(param) then param = {} end
      local favor_defined = param.favor ~= nil
      local lewd_defined = param.lewdness ~= nil
      local anger_defined = param.anger ~= nil
      for _, category in ipairs(cats) do
        local group = by_category[category]
        local roles = sorted_unique(util.map(group, function(item) return item.role end))
        local buckets
        if #roles == 1 then buckets = { group }
        else
          buckets = {}
          for _, role in ipairs(roles) do buckets[#buckets + 1] = util.filter(group, function(item) return item.role == role end) end
        end
        for _, bucket in ipairs(buckets) do
          local first = bucket[1]
          local role_names = sorted_unique(util.map(bucket, function(item) return item.role end))
          local suffix = #roles == 1 and "" or ("_" .. role_names[1])
          local node_id = string.format("p%s_r%s_%s%s", personality, row_index, category, suffix)
          local condition_id = int_field(param, "conditions", -1)
          if condition_id == nil then condition_id = -1 end
          local node = obj({
            { "id", node_id },
            { "personality", need_int(personality) },
            { "category", category },
            { "text_jp", orv(param.text, "") },
            { "text_zh", "" },
            { "voice_files", {} },
            { "favor", favor_defined and int_field(param, "favor", 0) or 0 },
            { "lewdness", lewd_defined and int_field(param, "lewdness", 0) or 0 },
            { "conditions", M.speak_support(condition_id) },
            { "source", obj({
              { "table", table_kind == "npc" and "CommunicationNPCData" or "CommunicationInfo" },
              { "asset", source_asset },
              { "bundle", source_bundle },
              { "row", row_index },
              { "stage", first.stage },
              { "group", first.group },
              { "command", first.command },
              { "command_label_jp", first.group == "Talk" and N(TALK_COMMAND_LABEL[first.command]) or null },
              { "roles", role_names },
              { "containers", sorted_unique(util.map(bucket, function(item) return item.container end)) },
              { "part_index", first.part_index },
              { "assetbundle", orv(param.assetbundle, "") },
              { "file", orv(param.file, "") },
              { "pose", orv(param.pose, "") },
              { "facial_expression", orv(param.facialExpression, "") },
              { "eye_look", N(int_field(param, "eyeLook", 1)) },
              { "neck_look", N(int_field(param, "neckLook", 0)) },
              { "anger", anger_defined and N(int_field(param, "anger", 0)) or null },
              { "topic", N(int_field(param, "topic", -1)) },
              { "priority", N(int_field(param, "priority", 0)) },
              { "effects_defined", favor_defined and lewd_defined and anger_defined },
              { "label_row", N(first.label_row) },
            }) },
          })
          nodes[#nodes + 1] = node
        end
      end
    end
    ::continue::
  end
  return nodes, other
end

function M.translation_for(file_field, text_jp, comm_lines, comm_zh, clip_owner)
  local key = pystr(orv(file_field, ""))
  local record = T(comm_lines) and comm_lines[key] or nil
  if not T(record) or record.text ~= text_jp then return "", "line_not_in_comm_lines" end
  local tokens = M.split_file_field(key)
  if #tokens == 0 then return "", "no_clip" end
  local clip = tokens[#tokens]
  local owner = clip_owner[clip]
  if owner ~= text_jp then return "", "clip_owner_mismatch" end
  local text = (T(comm_zh) and comm_zh or {})[clip]
  if not T(text) then return "", "missing_translation" end
  return text, "comm_zh_last_clip"
end

-- First full-line key that ends on a clip owns that clip, matching build_comm_translation.
function M.clip_owners_from_lines(comm_lines)
  local owners = {}
  for key, record in pairs(comm_lines) do
    local tokens = M.split_file_field(key)
    if #tokens > 0 then
      local clip = tokens[#tokens]
      if owners[clip] == nil then owners[clip] = orv(record.text, "") end
    end
  end
  return owners
end

-- Exact Japanese key to Chinese values. Conflicting values stay unresolved.
-- Returns key -> {count=n, first=value}.
function M.load_exact_translations(text_dir)
  local found = {}
  if not T(text_dir) or not path.is_dir(text_dir) then return {} end
  for _, p in ipairs(path.rglob(text_dir, "*.txt")) do
    local okr, raw = pcall(path.read_bytes, p)
    if okr then
      for _, line in ipairs(pysplitlines(utf8_replace(raw))) do
        if line:find("=", 1, true) and line:sub(1, 2) ~= "//" then
          local a = line:find("=", 1, true)
          local key, value = line:sub(1, a - 1), line:sub(a + 1)
          if key ~= "" and value ~= "" then
            local entry = found[key]
            if not entry then entry = { count = 0, values = {} }; found[key] = entry end
            if not entry.values[value] then entry.values[value] = true; entry.count = entry.count + 1; entry.first = entry.first or value end
          end
        end
      end
    end
  end
  return found
end

function M.attach_text_and_voice(nodes, voice_profile, comm_lines, comm_zh, clip_owner, audio_index, audio_root, zh_root, exact_translations)
  local gaps = {}
  exact_translations = exact_translations or {}
  for _, node in ipairs(nodes) do
    local source = node.source
    local text_zh, status = M.translation_for(orv(source.file, ""), orv(node.text_jp, ""), comm_lines, comm_zh, clip_owner)
    if not T(text_zh) then
      local values = exact_translations[orv(node.text_jp, "")]
      if values and values.count == 1 then text_zh = values.first; status = "exact_jp"
      elseif values and values.count > 0 then status = "translation_conflict" end
    end
    node.text_zh = text_zh
    source.translation_status = status
    if status ~= "comm_zh_last_clip" and status ~= "exact_jp" then
      gaps[#gaps + 1] = obj({ { "id", node.id }, { "kind", "translation" }, { "status", status } })
    end
    local matched_profile = voice_profile.status == "matched"
    local sequence, files = M.resolve_voice_sequence(orv(source.file, ""), orv(source.assetbundle, ""),
      matched_profile and N(voice_profile.folder) or nil, audio_index, audio_root)
    if not matched_profile then
      for _, part in ipairs(sequence) do
        if part.status == "matched" then
          part.status = "voice_profile_" .. pystr(voice_profile.status)
          json.remove(part, "path")
        end
      end
      files = {}
    end
    node.voice_files = files
    source.voice_sequence = sequence
    source.voice_profile = N(voice_profile.folder)
    source.voice_profile_status = N(voice_profile.status)
    local zh_files = {}
    for _, part in ipairs(sequence) do
      if part.status ~= "matched" then
        gaps[#gaps + 1] = obj({ { "id", node.id }, { "kind", "audio" }, { "status", N(part.status) }, { "token", N(part.token) }, { "index", N(part.index) } })
      else
        local token, kind, folder = part.token, part.kind, voice_profile.folder
        local wav = path.join(zh_root, folder, kind, token .. ".wav")
        if path.is_file(wav) and (path.size(wav) or 0) > 0 then
          part.zh_wav = resolved(wav)
          zh_files[#zh_files + 1] = part.zh_wav
        else
          part.zh_wav = null
          gaps[#gaps + 1] = obj({ { "id", node.id }, { "kind", "zh_wav" }, { "status", "missing_wav" }, { "token", token } })
        end
      end
    end
    source.zh_wavs = zh_files
    if util.any(sequence, function(part) return part.kind == "h" end) then
      gaps[#gaps + 1] = obj({ { "id", node.id }, { "kind", "content" }, { "status", "h_bundle_linked" } })
    end
  end
  return gaps
end

function M.choose_sample_profiles(nodes, limit)
  local by_personality, voiced, order = {}, {}, {}
  for _, node in ipairs(nodes) do
    if node.source.table == "CommunicationInfo" then
      local personality, category = node.personality, node.category
      if not by_personality[personality] then by_personality[personality] = json.object(); order[#order + 1] = personality end
      count(by_personality[personality], category)
      if T(node.voice_files) then
        voiced[personality] = voiced[personality] or json.object()
        count(voiced[personality], category)
      end
    end
  end
  local chosen = {}
  for _, personality in ipairs(util.sorted(order)) do
    local counts = voiced[personality] or json.object()
    local function c(k) return counts[k] or 0 end
    if c("daily") >= 3 and c("romance") >= 3 and c("intimacy") >= 3 and c("greeting") >= 1 and c("farewell") >= 1 then
      chosen[#chosen + 1] = obj({ { "personality", personality }, { "voiced", ocopy(counts) }, { "nodes", ocopy(by_personality[personality]) } })
    end
    if #chosen >= limit then break end
  end
  return chosen
end

function M.sha256_file(p) return ok.hash.file(p, "sha256") end
local sha256_file = M.sha256_file

-- shutil.which("ffmpeg"), then the binary imageio_ffmpeg bundles with the Python on PATH.
function M.find_ffmpeg()
  local found = proc.which("ffmpeg")
  if found then return winpath(found) end
  local env = os.getenv("IMAGEIO_FFMPEG_EXE")
  if env and env ~= "" and path.is_file(env) then return env end
  local candidates = {}
  for dir in (os.getenv("PATH") or ""):gmatch("[^;]+") do
    local version = path.name(dir):match("^[Pp]ython(%d%d+)$")
    if version and path.is_file(path.join(dir, "python.exe")) then
      local appdata = os.getenv("APPDATA")
      if appdata then candidates[#candidates + 1] = path.join(appdata, "Python", "Python" .. version, "site-packages") end
      candidates[#candidates + 1] = path.join(dir, "Lib", "site-packages")
    end
  end
  for _, site in ipairs(candidates) do
    local bins = path.glob(path.join(site, "imageio_ffmpeg", "binaries"), "ffmpeg-win*.exe")
    if #bins > 0 then return winpath(bins[#bins]) end
  end
  return nil
end

function M.transcode_wav(ffmpeg, source, dest)
  path.mkdir(path.parent(dest))
  local temporary = path.with_suffix(dest, ".part.ogg")
  if path.exists(temporary) then path.remove(temporary) end
  local completed = proc.run({ ffmpeg, "-y", "-hide_banner", "-loglevel", "error", "-i", winpath(source),
    "-c:a", "libvorbis", "-q:a", "4", "-ar", "22050", winpath(temporary) }, { capture = true, text = false })
  if completed.code ~= 0 or not path.is_file(temporary) or (path.size(temporary) or 0) <= 0 then
    if path.exists(temporary) then path.remove(temporary) end
    local err = ulast(utf8_replace(completed.stderr or ""), 400)
    if err == "" then err = "ffmpeg exit " .. tostring(completed.code) end
    return nil, err
  end
  if not ogg_magic_ok(temporary) then
    path.remove(temporary)
    return nil, "transcode output is not an ogg"
  end
  path.replace(temporary, dest)
  return sha256_file(dest), nil
end

function M.decode_check(ffmpeg, p)
  local completed = proc.run({ ffmpeg, "-v", "error", "-i", winpath(p), "-f", "null", "-" }, { capture = true, text = false })
  local err = pystrip(utf8_replace(completed.stderr or ""))
  return completed.code == 0 and err == "", ulast(err, 300)
end

-- Unity tables ----------------------------------------------------------------------
function M._excel_rows(tree)
  local rows = {}
  for _, item in ipairs(orv(tree.list, {})) do
    local cells
    if json.is_object(item) then cells = item.list else cells = item end
    if is_list(cells) then
      local r = {}
      for i, cell in ipairs(cells) do r[i] = none(cell) and "" or pystr(cell) end
      rows[#rows + 1] = r
    end
  end
  return rows
end
local excel_rows = M._excel_rows

local tree_cache = {}
function M._mono_trees(p)
  local key = path.abs(p):lower()
  if tree_cache[key] then return tree_cache[key] end
  local found = {}
  for _, o in ipairs(kit.unity_objects(p, { type = "MonoBehaviour" }) or {}) do
    local tree = o.tree
    if json.is_object(tree) and T(tree.m_Name) then found[#found + 1] = tree end
  end
  tree_cache[key] = found
  return found
end
local mono_trees = M._mono_trees

function M.load_voice_table(config_dir)
  local tbl = {}
  for _, p in ipairs(sorted_glob(config_dir, "*.unity3d")) do
    for _, tree in ipairs(mono_trees(p)) do
      for _, row in ipairs(orv(tree.param, {})) do
        if json.is_object(row) and row.No ~= nil then
          local no = need_int(row.No)
          tbl[no] = obj({ { "Personality", N(row.Personality) }, { "No", no }, { "FileName", N(row.FileName) }, { "Sort", N(row.Sort) }, { "source", winpath(p) } })
        end
      end
    end
  end
  return tbl
end

function M.load_stepup(stepup_dir)
  local limits, sources = json.object(), {}
  for _, p in ipairs(sorted_glob(stepup_dir, "*.unity3d")) do
    for _, tree in ipairs(mono_trees(p)) do
      sources[#sources + 1] = obj({ { "bundle", winpath(p) }, { "name", N(tree.m_Name) } })
      for _, row in ipairs(orv(tree.param, {})) do
        if json.is_object(row) and row.no ~= nil then limits[need_int(row.no)] = need_int(row.limit) end
      end
    end
  end
  return obj({ { "limits", limits }, { "sources", sources } })
end

-- Last sorted info_*.unity3d row wins, matching GameAssist.LoadInfo.
function M.load_communication_paths(communication_dir)
  local paths, order = {}, {}
  for _, p in ipairs(sorted_glob(communication_dir, "info_*.unity3d")) do
    for _, tree in ipairs(mono_trees(p)) do
      local name = pystr(orv(tree.m_Name, ""))
      if name:sub(1, 5) == "info_" then
        local rows = excel_rows(tree)
        if #rows > 0 then
          order[#order + 1] = winpath(p)
          for i = 2, #rows do
            local cells = rows[i]
            if #cells > 0 and cells[1] ~= "" then
              local personality = pyint(cells[1])
              if personality ~= nil then
                local function cell(n) return cells[n] or "" end
                paths[personality] = obj({
                  { "personality", personality },
                  { "bundle", cell(2) }, { "file", cell(3) },
                  { "option_bundle", cell(4) }, { "option_file", cell(5) },
                  { "event_bundle", cell(6) }, { "event_file", cell(7) },
                  { "listen_bundle", cell(8) }, { "listen_file", cell(9) },
                  { "topic_common_bundle", cell(10) }, { "topic_common_file", cell(11) },
                  { "topic_rare_bundle", cell(12) }, { "topic_rare_file", cell(13) },
                  { "path_source", name },
                })
              end
            end
          end
        end
      end
    end
  end
  return paths, order
end

local function bundle_file(abdata, bundle) return path.join(abdata, (pystr(bundle):gsub("\\", "/"))) end

function M.load_named_tree(abdata, bundle, asset_name, cache)
  local p = bundle_file(abdata, bundle)
  local key = winpath(p)
  if cache[key] == nil then
    cache[key] = {}
    if path.is_file(p) then
      for _, tree in ipairs(mono_trees(p)) do cache[key][pystr(tree.m_Name)] = tree end
    end
  end
  return cache[key][asset_name], p
end

function M.load_option_items(abdata, bundle, asset_name, cache)
  local tree = M.load_named_tree(abdata, bundle, asset_name, cache)
  local items = {}
  if not T(tree) then return items end
  local rows = excel_rows(tree)
  for i = 2, #rows do
    local cells = rows[i]
    if #cells > 0 then
      local key = pyint(cells[1])
      if key ~= nil then items[key] = util.slice(cells, 2) end
    end
  end
  return items
end

function M.load_occurrence(communication_dir)
  local tables = json.object()
  for _, p in ipairs(sorted_glob(communication_dir, "info_*.unity3d")) do
    for _, tree in ipairs(mono_trees(p)) do
      local name = pystr(orv(tree.m_Name, ""))
      local digits = name:lower():match("^occurrencecondition_(%d+)$")
      if digits then
        local stage_file = need_int(digits)
        local rows = excel_rows(tree)
        for i = 2, #rows do
          local cells = rows[i]
          if #cells >= 2 then
            local stage, condition = pyint(cells[1]), pyint(cells[2])
            if stage ~= nil and condition ~= nil then
              local function cell(n) return cells[n] or "" end
              tables[stage .. "," .. condition] = obj({
                { "stage", stage }, { "condition", condition }, { "name", cell(3) }, { "favor", cell(4) },
                { "lewdness", cell(5) }, { "probability", cell(6) }, { "reference", cell(7) }, { "source", name },
                { "bundle", path.name(p) }, { "stage_file", stage_file }, { "street_support", "unsupported" },
              })
            end
          end
        end
      end
    end
  end
  return tables
end

function M.load_json(p) return json.decode(path.read_text(p)) end
local load_json = M.load_json

function M.write_json(p, payload, encoded)
  path.mkdir(path.parent(p))
  path.write_text(p, encoded or json.encode(payload, { ensure_ascii = false, separators = { ",", ":" } }), { newline = "\n", atomic = true })
end
local write_json = M.write_json

local function count_nodes(nodes)
  local counts, voiced, translated = counter(), counter(), counter()
  for _, node in ipairs(nodes) do
    count(counts, node.category)
    if T(node.voice_files) then count(voiced, node.category) end
    if T(node.text_zh) then count(translated, node.category) end
  end
  return obj({ { "nodes", #nodes }, { "by_category", counts }, { "voiced_by_category", voiced }, { "translated_by_category", translated } })
end
M._count_nodes = count_nodes

M.COMPLETION_SCHEMA = 2
M.AUDIO_SCHEMA = "street-social-audio-v1"
local COMPLETION_SCHEMA, AUDIO_SCHEMA = M.COMPLETION_SCHEMA, M.AUDIO_SCHEMA
-- actions.manifest.json source states. Pose excel states are not renamed onto these.
M.GESTURE_SOURCE = {
  { "f_talk_00_00_00", "talk_gesture_00.okanim" },
  { "f_talk_00_03_00", "talk_gesture_03.okanim" },
  { "f_talk_00_07_00", "talk_gesture_07.okanim" },
  { "f_talk_00_12_00", "talk_gesture_12.okanim" },
}
local GESTURE_SOURCE = M.GESTURE_SOURCE
M.TALK_TEMPER_LENGTH = 39
local TALK_TEMPER_LENGTH = M.TALK_TEMPER_LENGTH
-- Heroine.Initialize: Random.Range(0, 2) when (slot - 1) is in 7..12, else Random.Range(0, 3).
local TALK_TEMPER_BINARY_SLOTS = util.set({ 8, 9, 10, 11, 12, 13 })
-- GetIntroductionADV switch. Other ids fall through to the shuffle pool. 101 is an empty break.
local INTRODUCTION_SPECIAL = util.set({ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 30, 31, 32 })
local INTRODUCTION_EXCLUDED = util.set({ 101 })
-- GetEndConversationADV. 101 and 102 are empty breaks.
local FAREWELL_SPECIAL = util.set({ 0, 1, 2, 3, 10, 101, 102 })
local FEMALE_PROFILES = { 0, 18, 17, 27, 37 }
M.FEMALE_PROFILES = FEMALE_PROFILES

local function introduction_inputs(condition_id)
  local tbl = {
    [1] = { "mapNo in {28,30,31}", "not isOtherPeople" },
    [2] = { "mapNo in {28,30,31}" },
    [3] = { "mapNo in {17,33}", "not isOtherPeople" },
    [4] = { "mapNo in {17,33}", "relation < 1 or isOtherPeople" },
    [5] = { "talkTime <= 0" },
    [6] = { "not isOtherPeople", "GetNowClothesType() % 2 == 1" },
    [7] = { "isOtherPeople", "GetNowClothesType() % 2 == 1" },
    [8] = { "not isOtherPeople", "NowCoordinate == 1" },
    [9] = { "isOtherPeople", "NowCoordinate == 1" },
    [10] = { "isChase" },
    [11] = { "femaleOnly" },
    [12] = { "favor / stepUpDic[stage] >= 0.5" },
    [13] = { "lewdness >= 50" },
    [14] = { "not isNotice" },
    [30] = { "mapNo in {13,14}", "not isOtherPeople" },
    [31] = { "mapNo in {13,14}", "relation < 1 or isOtherPeople" },
    [32] = { "isAttack" },
    [101] = {},
  }
  return tbl[need_int(condition_id)] or {}
end

local function farewell_inputs(condition_id)
  local tbl = {
    [0] = { "talkTime <= 0", "favor / stepUpDic[stage] >= 0.5" },
    [1] = { "talkTime <= 0", "lewdness >= 50" },
    [2] = { "endSelect", "talkTime / talkTimeMax < 0.5" },
    [3] = { "endSelect", "talkTime / talkTimeMax >= 0.5" },
    [10] = { "favor >= stepUpDic[stage]" },
    [101] = {},
    [102] = {},
  }
  local v = tbl[need_int(condition_id)]
  if v then return v end
  return { "talkTime <= 0", "no earlier priority row returned" }
end

-- ConfirmEvent*(_conditions, heroine=false). Stage tables differ.
function M.event_button_predicate(condition_id)
  condition_id = need_int(condition_id)
  local stage0 = {
    [0] = "talkCommand does not contain 0",
    [1] = "talkCommand does not contain 1",
    [2] = "talkCommand does not contain 2",
    [3] = "talkCommand does not contain 3",
    [4] = "isChasePossible and talkTime > 1",
  }
  local stage1 = {
    [0] = "talkCommand does not contain 6 and favor/stepUp >= 0.7",
    [1] = "talkCommand does not contain 6 and favor/stepUp >= 1",
    [2] = "talkCommand does not contain 7 and isHPossible and not isSecondH",
    [3] = "talkCommand does not contain 7 and isHPossible and isSecondH",
    [4] = "talkCommand does not contain 7 and isHPossible and isSecondH and isOtherPeople",
    [5] = "talkCommand does not contain 7 and isHPossible and isSecondH and exposure",
    [6] = "talkCommand does not contain 0",
    [7] = "talkCommand does not contain 1",
    [8] = "talkCommand does not contain 2",
    [9] = "talkCommand does not contain 3",
    [10] = "isChasePossible and talkTime > 1",
    [11] = "talkCommand does not contain 5",
  }
  local stage2 = {
    [0] = "talkCommand does not contain 7 and isHPossible and not isSecondH",
    [3] = "talkCommand does not contain 7 and isHPossible and isSecondH",
    [4] = "talkCommand does not contain 7 and isHPossible and isSecondH and isOtherPeople",
    [5] = "talkCommand does not contain 7 and isHPossible and isSecondH and exposure",
    [6] = "talkCommand does not contain 0",
    [7] = "talkCommand does not contain 1",
    [8] = "talkCommand does not contain 2",
    [9] = "talkCommand does not contain 3",
    [10] = "isChasePossible and talkTime > 1",
    [11] = "talkCommand does not contain 5",
    [12] = "talkCommand does not contain 8",
    [13] = "talkCommand does not contain 7 and isHPossible and isSecondH and talkEvent contains 54",
  }
  return obj({ { "stage0", N(stage0[condition_id]) }, { "stage1", N(stage1[condition_id]) }, { "stage2", N(stage2[condition_id]) }, { "absent_stage_is_hidden", true } })
end

-- Speak filter and the context switch are different uses of the same id.
function M.condition_semantics(condition_id, context)
  condition_id = need_int(condition_id)
  local speak = M.speak_support(condition_id)
  if context == "talk" then
    return obj({ { "id", condition_id }, { "context", "talk" }, { "speak_filter", speak.game_speak_filter }, { "street_support", speak.street_support },
      { "source", "Info.ConditionDeterminationSpeak" },
      { "note", "CreateBranchADV may still use this id as a line picker. That formula is on the branch and does not make an unsupported speak filter pass." } })
  end
  if context == "introduction" then
    local rule, support
    if INTRODUCTION_EXCLUDED[condition_id] then rule, support = "excluded_empty_break", "unsupported"
    elseif INTRODUCTION_SPECIAL[condition_id] then rule, support = "special_predicate", "described"
    else rule, support = "fallback_shuffle_pool", "supported" end
    return obj({ { "id", condition_id }, { "context", "introduction" }, { "rule", rule }, { "street_support", support },
      { "speak_filter_not_used", true }, { "source", "Info.GetIntroductionADV" }, { "inputs", introduction_inputs(condition_id) } })
  end
  if context == "farewell" then
    local rule, support
    if condition_id == 101 or condition_id == 102 then rule, support = "excluded_empty_break", "unsupported"
    elseif FAREWELL_SPECIAL[condition_id] then rule, support = "special_predicate", "described"
    else rule, support = "fallback_when_talk_time_empty", "described" end
    return obj({ { "id", condition_id }, { "context", "farewell" }, { "rule", rule }, { "street_support", support },
      { "speak_filter_not_used", true }, { "source", "Info.GetEndConversationADV" }, { "inputs", farewell_inputs(condition_id) } })
  end
  if context == "event" then
    return obj({ { "id", condition_id }, { "context", "event" }, { "street_support", "described" }, { "speak_filter_not_used", true },
      { "source", "Info.ConfirmEvent0/1/2 and Info.GetEventADV" }, { "button_predicate", M.event_button_predicate(condition_id) } })
  end
  return obj({ { "id", condition_id }, { "context", orv(context, "other") }, { "speak_filter", speak.game_speak_filter },
    { "street_support", "unsupported" }, { "source", "no street category switch" } })
end

function M.temper_domain(condition_id)
  local slot = need_int(condition_id) - 1
  if TALK_TEMPER_BINARY_SLOTS[slot] then return { 0, 1 } end
  return { 0, 1, 2 }
end

local function reachable_indexes(selection, length)
  local kind = selection.kind
  if kind == "attribute_weights" then
    local found, list = {}, {}
    for _, key in ipairs({ "weights_when_matched", "weights_when_not_matched" }) do
      for i, weight in ipairs(orv(selection[key], {})) do
        local index = i - 1
        if weight > 0 and index < length and not found[index] then found[index] = true; list[#list + 1] = index end
      end
    end
    table.sort(list)
    return list
  end
  if kind == "preference_compare" then
    local out = {}
    for _, index in ipairs({ selection.index_when_true, selection.index_when_false }) do
      if index < length then out[#out + 1] = index end
    end
    return out
  end
  if kind == "fixed_index" then
    local index = selection.index
    if math.type(index) == "integer" and index < length then return { index } end
    return {}
  end
  if kind == "talk_temper" then
    return util.filter(orv(selection.domain, {}), function(v) return v < length end)
  end
  if kind == "random_weights" then
    local out = {}
    for i, weight in ipairs(orv(selection.weights, {})) do
      if weight > 0 and i - 1 < length then out[#out + 1] = i - 1 end
    end
    return out
  end
  return {}
end
M._reachable_indexes = reachable_indexes

-- CreateBranchADV(BranchInfo, ChangeValueInfo). Does not choose a line.
function M.branch_selection(condition_id, success_count, has_no_interested)
  condition_id = need_int(condition_id)
  success_count = need_int(success_count)
  local indexer = {}
  for index = 0, success_count - 1 do indexer[#indexer + 1] = obj({ { "role", "success" }, { "part_index", index } }) end
  indexer[#indexer + 1] = obj({ { "role", "failure" }, { "part_index", 0 } })
  if has_no_interested then indexer[#indexer + 1] = obj({ { "role", "no_interested" }, { "part_index", 0 } }) end
  local result = obj({ { "source", "Info.CreateBranchADV(BranchInfo, ChangeValueInfo)" }, { "indexer", indexer },
    { "introduction_always_plays", true }, { "picked", null } })
  local function finish(kind, extra)
    result.kind = kind
    for _, kv in ipairs(extra) do result[kv[1]] = kv[2] end
    result.reachable_indexes = reachable_indexes(result, #indexer)
    return result
  end
  local attribute = {
    [1] = { 0, { 45, 20, 35 }, { 30, 30, 40 } },
    [2] = { 1, { 45, 20, 35 }, { 30, 30, 40 } },
    [3] = { 0, { 30, 40, 30 }, { 20, 40, 40 } },
    [4] = { 1, { 30, 40, 30 }, { 20, 40, 40 } },
  }
  if attribute[condition_id] then
    local a = attribute[condition_id]
    return finish("attribute_weights", { { "street_support", "described" }, { "input", "ChaInfo.GetAttributeType(charFile)" },
      { "matched_attribute_type", a[1] }, { "weights_when_matched", a[2] }, { "weights_when_not_matched", a[3] }, { "index_count_required", 3 } })
  end
  local preference = {
    [7] = { 0, "<=", 3 }, [8] = { 0, ">=", -3 }, [9] = { 1, "<=", 3 }, [10] = { 1, ">=", -3 }, [11] = { 2, "<=", 3 }, [12] = { 2, ">=", -3 },
  }
  if preference[condition_id] then
    local axis, op, threshold = table.unpack(preference[condition_id])
    return finish("preference_compare", { { "street_support", "described" },
      { "input", string.format("GameAssist.PreferenceValue(heroine)[%d] %s %d", axis, op, threshold) },
      { "axis", axis }, { "operator", op }, { "threshold", threshold }, { "index_when_true", 1 }, { "index_when_false", 0 },
      { "preference_base_is_personality_plus_attribute_custom", true } })
  end
  if condition_id == 41 then
    return finish("fixed_index", { { "street_support", "described" }, { "index", 0 }, { "input", "none; the 41 speak filter is separate and still unsupported" } })
  end
  if condition_id >= 1 and condition_id <= TALK_TEMPER_LENGTH then
    return finish("talk_temper", { { "street_support", "described" }, { "input", string.format("heroine.talkTemper[%d]", condition_id - 1) },
      { "slot", condition_id - 1 }, { "domain", M.temper_domain(condition_id) }, { "out_of_range", "not_a_match" } })
  end
  local weights
  if success_count == 1 and has_no_interested then weights = { 30, 30, 40 }
  elseif success_count == 1 then weights = { 30, 70 }
  elseif success_count == 2 then weights = { 20, 40, 40 } end
  if weights == nil then
    return finish("unresolved_null", { { "street_support", "unsupported" }, { "reason", "CreateBranchADV leaves talkData null for this success length." } })
  end
  local unrolled = {}
  for i = #weights, #indexer - 1 do unrolled[#unrolled + 1] = i end
  return finish("random_weights", { { "street_support", "described" }, { "weights", weights }, { "unrolled_indexes", unrolled },
    { "input", "Utils.ProbabilityCalclator.DetermineFromDict; a weight of 100 would win before the roll" } })
end

-- Exact animator state -> clip file. Missing files are not mapped.
function M.load_gesture_clips(manifest_path, animation_dir)
  local by_state = {}
  for _, g in ipairs(GESTURE_SOURCE) do by_state[g[1]] = g[2] end
  local found, missing = json.object(), {}
  if T(manifest_path) and path.is_file(manifest_path) then
    local payload = load_json(manifest_path)
    for _, action in ipairs(orv(payload.actions, {})) do
      local source = pystr(orv(action.source, ""))
      local parts = util.split(source, "|")
      local state = source:find("|", 1, true) and parts[#parts] or ""
      local filename = pystr(orv(action.file, ""))
      if by_state[state] ~= nil and by_state[state] == filename then
        local p = path.join(animation_dir, filename)
        if path.is_file(p) and (path.size(p) or 0) > 0 then
          found[state] = obj({ { "file", filename }, { "source", source }, { "sha256", N(action.sha256) } })
        else
          missing[#missing + 1] = obj({ { "state", state }, { "file", filename }, { "status", "missing_file" } })
        end
      end
    end
  else
    missing[#missing + 1] = obj({ { "status", "missing_manifest" }, { "path", winpath(manifest_path) } })
  end
  for _, g in ipairs(GESTURE_SOURCE) do
    local state, filename = g[1], g[2]
    if found[state] == nil and not util.any(missing, function(item) return item.state == state end) then
      missing[#missing + 1] = obj({ { "state", state }, { "file", filename }, { "status", "not_in_manifest" } })
    end
  end
  return obj({ { "by_state", found }, { "missing", missing } })
end

-- Map a pose row only when its state string is an available common clip.
function M.map_gesture(pose_name, pose_cells, gestures)
  pose_name = none(pose_name) and "" or pystr(pose_name)
  if pose_name == "" then
    return obj({ { "status", "absent" }, { "clip", null }, { "source_state", null }, { "reason", "Source pose is empty. AddTransfer skips Motion." } })
  end
  if not T(pose_cells) then
    return obj({ { "status", "missing_pose" }, { "clip", null }, { "pose", pose_name }, { "source_state", null },
      { "reason", "GetPoseInfo has no row for this personality and pose name. No gesture is substituted." } })
  end
  local state = #pose_cells > 2 and pystr(pose_cells[3]) or ""
  local bundle = pystr(pose_cells[1])
  local asset = #pose_cells > 1 and pystr(pose_cells[2]) or ""
  local clip = (gestures or {})[state]
  if not T(clip) then
    return obj({ { "status", "missing_clip" }, { "clip", null }, { "pose", pose_name }, { "source_state", state ~= "" and state or null },
      { "source_bundle", bundle }, { "source_asset", asset },
      { "reason", "Pose state is not an exact common-gesture source state. Number suffixes are not a match." } })
  end
  return obj({ { "status", "matched" }, { "clip", clip.file }, { "pose", pose_name }, { "source_state", state },
    { "source_bundle", bundle }, { "source_asset", asset }, { "sha256", N(clip.sha256) } })
end

function M.map_performance(pose_name, pose_cells, expression, eye_look, neck_look, gestures)
  local has = T(pose_cells)
  local function col(i) return has and #pose_cells >= i and N(pose_cells[i]) or null end
  return obj({
    { "pose", orv(pose_name, "") },
    { "pose_columns", obj({ { "bundle", col(1) }, { "asset", col(2) }, { "state", col(3) }, { "ik_bundle", col(4) }, { "ik_asset", col(5) } }) },
    { "gesture", M.map_gesture(pose_name, pose_cells, gestures) },
    { "expression", obj({ { "source_name", orv(expression, "") }, { "clip", null }, { "status", T(expression) and "source_only" or "absent" },
      { "api", "Game.GetExpression(personality, name)" }, { "reason", "Expression names are not common gesture clips and are not remapped." } }) },
    { "look", obj({
      { "eyes_pattern", none(eye_look) and null or need_int(eye_look) },
      { "neck_pattern", none(neck_look) and null or need_int(neck_look) },
      { "neck_rate", 0 },
      { "eyes_api", "ChaControl.ChangeLookEyesPtn" },
      { "neck_api", "ChaControl.ChangeLookNeckPtn" },
      { "status", "pattern_id" },
      { "source", "ADV.Commands.Chara.LookEyes uses args No,Ptn. LookNeck uses No,Ptn,Rate. AddTransfer passes rate 0." },
    }) },
  })
end

function M.is_portable_audio_ref(ref)
  if not T(ref) or type(ref) ~= "string" then return false end
  local text = ref:gsub("\\", "/")
  if text:sub(1, 1) == "/" or text:find(":", 1, true) or util.contains_value(util.split(text, "/"), "..") then return false end
  local lowered = text:lower()
  if lowered:find("koikatusunshine", 1, true) or lowered:find("voice_extract", 1, true) then return false end
  return (text:sub(1, 7) == "zh_ogg/" or text:sub(1, 7) == "ja_ogg/") and text:sub(-4) == ".ogg"
end

local function safe_pack_name(name)
  if not T(name) or name == "." or name == ".." or name:find("..", 1, true) then return false end
  return not name:find("[/\\:]")
end

-- Copy one verified original ogg into ja_ogg/<folder>/<token>.ogg. Bytes only.
local function shared_ja_copy(part, folder, content_root, cache)
  local token = pystr(orv(part.token, ""))
  folder = pystr(orv(folder, ""))
  local root = content_root
  local key = winpath(root) .. "\0" .. folder .. "\0" .. token
  local cached = cache[key]
  if cached ~= nil then
    local hit = ocopy(cached)
    if hit.ja_status == "copied" then hit.ja_status = "reused" end
    return hit
  end
  local result = obj({ { "ja_ref", null }, { "ja_sha256", null }, { "ja_status", "rejected" } })
  if not safe_pack_name(folder) or not safe_pack_name(token) then
    cache[key] = result
    return ocopy(result)
  end
  local source = part.path
  local source_path = T(source) and source or nil
  if source_path == nil or not path.is_file(source_path) or (path.size(source_path) or 0) <= 0 or not ogg_magic_ok(source_path) then
    result.ja_status = "missing_original"
    cache[key] = result
    return ocopy(result)
  end
  local rel = string.format("ja_ogg/%s/%s.ogg", folder, token)
  local dest = path.join(root, "ja_ogg", folder, token .. ".ogg")
  local source_sha = sha256_file(source_path)
  if path.is_file(dest) then
    if (path.size(dest) or 0) <= 0 or not ogg_magic_ok(dest) or sha256_file(dest) ~= source_sha then
      result.ja_status = "sha_conflict"
      cache[key] = result
      return ocopy(result)
    end
    result.ja_ref = rel; result.ja_sha256 = source_sha; result.ja_status = "reused"
    cache[key] = result
    return ocopy(result)
  end
  path.mkdir(path.parent(dest))
  local temporary = dest .. ".part"
  local okc, matched = pcall(function()
    path.copy(source_path, temporary)
    if not ogg_magic_ok(temporary) or sha256_file(temporary) ~= source_sha then return false end
    path.replace(temporary, dest)
    return true
  end)
  if path.is_file(temporary) then pcall(path.remove, temporary) end
  if not okc or not matched then
    result.ja_status = "copy_mismatch"
    cache[key] = result
    return ocopy(result)
  end
  result.ja_ref = rel; result.ja_sha256 = source_sha; result.ja_status = "copied"
  cache[key] = result
  return ocopy(result)
end
M._shared_ja_copy = shared_ja_copy

-- Holes stay in voice_sequence. Playable lists exist only for a complete sequence.
function M.bind_runtime_voice(slots)
  local voice_sequence = {}
  for _, slot in ipairs(slots or {}) do
    voice_sequence[#voice_sequence + 1] = obj({ { "index", N(slot.index) }, { "status", N(slot.status) }, { "token", N(slot.token) },
      { "path", T(slot.ja_ref) and slot.ja_ref or null }, { "zh_ogg", T(slot.zh_ref) and slot.zh_ref or null } })
  end
  if #voice_sequence == 0 then return voice_sequence, {}, {} end
  local complete = util.all(voice_sequence, function(item) return item.status == "matched" and T(item.path) end)
  if not complete then return voice_sequence, nil, nil end
  local voice_files = util.map(voice_sequence, function(item) return item.path end)
  local voice_files_zh = nil
  if util.all(voice_sequence, function(item) return T(item.zh_ogg) end) then
    voice_files_zh = util.map(voice_sequence, function(item) return item.zh_ogg end)
  end
  return voice_sequence, voice_files, voice_files_zh
end

-- Same nodes, but the current parser selects Japanese voice_files.
function M.japanese_catalogue(completion)
  local nodes = {}
  for _, node in ipairs(orv(completion.nodes, {})) do
    local clone = ocopy(node)
    local source = ocopy(orv(node.source, {}))
    local sequence = {}
    for _, slot in ipairs(orv(source.voice_sequence, {})) do
      local copied = ocopy(slot)
      copied.zh_ogg = null
      sequence[#sequence + 1] = copied
    end
    source.voice_sequence = sequence
    clone.source = source
    if T(clone.voice_files) then clone.voice_language = "ja" end
    nodes[#nodes + 1] = clone
  end
  return obj({
    { "schema", N(completion.schema) },
    { "audio_schema", N(completion.audio_schema) },
    { "generator", N(completion.generator) },
    { "audio_root", N(completion.audio_root) },
    { "voice_language", "ja" },
    { "voice_files_are", "Japanese view of the completion catalogue. source.voice_sequence.zh_ogg is null so a parser without voice_language still uses path. voice_language=ja selects voice_files. voice_files_zh stays on the node for the same line and is not read by current Rules." },
    { "replaces_runtime_schema_1", false },
    { "nodes", nodes },
  })
end

-- Full slot order. Holes stay. Refs are content-relative and are not invented.
function M.portable_audio_slots(sequence, folder, content_root, manifest_index, ja_cache)
  if ja_cache == nil then ja_cache = {} end
  local slots = {}
  for _, part in ipairs(sequence or {}) do
    local token, status = part.token, part.status
    local slot = obj({ { "index", N(part.index) }, { "token", N(token) }, { "status", N(status) }, { "zh_ref", null }, { "zh_sha256", null }, { "zh_status", null } })
    if status == "unsupported" and T(token) and call_name(pystr(token)) then
      slot.reason = orv(part.reason, "callName is unresolved runtime nickname audio")
      slot.zh_status = "unsupported"
    elseif status ~= "matched" then
      slot.zh_status = "not_applicable"
      slot.reason = N(orv(part.reason, status))
      if T(part.kind) then slot.kind = part.kind end
    else
      slot.kind = N(part.kind)
      slot.bundle = M.normalize_bundle(orv(part.bundle, ""))
      slot.original = obj({ { "rate", N(part.rate) }, { "channels", N(part.channels) }, { "length", N(part.length) }, { "matched", true } })
      local rel = string.format("zh_ogg/%s/%s.ogg", folder, token)
      local p = path.join(content_root, rel)
      local entry = (manifest_index or {})[pystr(folder) .. "\0" .. pystr(token)]
      if path.is_file(p) and (path.size(p) or 0) > 0 and ogg_magic_ok(p) then
        local sha = sha256_file(p)
        slot.zh_ref = rel
        slot.zh_sha256 = sha
        slot.zh_status = "reused"
        slot.codec = "libvorbis"
        slot.q = 4
        slot.ar = 22050
        if entry and T(entry.output_sha256) then slot.manifest_sha_match = entry.output_sha256 == sha
        else slot.manifest_sha_match = false end
      else
        slot.zh_status = "missing_zh"
        slot.reason = "Converted ogg is not in the shared content pack. No other clip is substituted."
      end
    end
    if status == "matched" then
      for k, v in pairs(shared_ja_copy(part, folder, content_root, ja_cache)) do slot[k] = v end
    elseif slot.zh_status == "unsupported" then
      slot.ja_ref = null; slot.ja_status = "unsupported"
    else
      slot.ja_ref = null; slot.ja_status = "not_applicable"
    end
    slots[#slots + 1] = slot
  end
  return slots
end

local function context_for_group(group)
  if group == "Talk" then return "talk" end
  if group == "Introduction" then return "introduction" end
  if group == "EndConversation" then return "farewell" end
  if group == "Event" then return "event" end
  return "other"
end

local function controlling_member(members)
  local intros = util.filter(members, function(item) return item.role == "introduction" end)
  if #intros > 0 then return intros[1] end
  local successes = util.filter(members, function(item) return item.role == "success" end)
  if #successes > 0 then return util.sorted(successes, function(item) return item.part_index end)[1] end
  local generics = util.filter(members, function(item) return item.role == "generic" end)
  if #generics > 0 then return util.sorted(generics, function(item) return { item.part_index, item.row } end)[1] end
  return util.sorted(members, function(item) return item.row end)[1]
end

function M.build_branch_records(personality, placements, row_conditions, table_kind)
  local grouped, keys = {}, {}
  for row, places in pairs(placements) do
    for _, place in ipairs(places) do
      local g = place.group
      if g == "Talk" or g == "Introduction" or g == "EndConversation" or g == "Event" then
        local key = place.stage .. "\0" .. g .. "\0" .. place.command .. "\0" .. place.container
        if not grouped[key] then grouped[key] = {}; keys[#keys + 1] = { place.stage, g, place.command, place.container, key } end
        table.insert(grouped[key], obj({ { "row", need_int(row) }, { "role", place.role }, { "part_index", need_int(place.part_index) },
          { "stage", place.stage }, { "group", g }, { "command", place.command }, { "container", place.container } }))
      end
    end
  end
  local records = {}
  for _, k in ipairs(util.sorted(keys, function(k) return { k[1], k[2], k[3], k[4] } end)) do
    local members = grouped[k[5]]
    local stage, group, command, container = k[1], k[2], k[3], k[4]
    local control = controlling_member(members)
    local condition_id = row_conditions[control.row]
    if none(condition_id) then condition_id = -1 end
    local successes = util.sorted(util.filter(members, function(item) return item.role == "success" end), function(item) return item.part_index end)
    local failure = util.filter(members, function(item) return item.role == "failure" end)
    local no_interested = util.filter(members, function(item) return item.role == "no_interested" end)
    local is_branch = #successes > 0 or #failure > 0
    local selection = null
    if is_branch and group == "Talk" then
      selection = M.branch_selection(condition_id, #successes, #no_interested > 0)
    elseif is_branch and group == "Event" then
      selection = obj({ { "kind", "event_probability_or_forced" }, { "street_support", "described" }, { "picked", null },
        { "source", "Info.GetEventADV / CreateBranchADV(BranchInfo, bool)" }, { "success_index", 0 }, { "failure_is_alternate", true } })
    end
    local indexer = {}
    if is_branch then
      for _, item in ipairs(successes) do indexer[#indexer + 1] = item end
      if failure[1] then indexer[#indexer + 1] = failure[1] end
      if no_interested[1] then indexer[#indexer + 1] = no_interested[1] end
    end
    records[#records + 1] = obj({
      { "id", string.format("p%s_s%s_%s_c%s_k%s", personality, stage, group, command, container) },
      { "personality", need_int(personality) },
      { "stage", need_int(stage) },
      { "group", group },
      { "command", need_int(command) },
      { "container", need_int(container) },
      { "category", N(M.category_for_placement(group, command, table_kind)) },
      { "controlling_row", control.row },
      { "controlling_condition", M.condition_semantics(condition_id, context_for_group(group)) },
      { "selection", selection },
      { "source_rows", util.sorted(util.map(members, function(item) return item.row end)) },
      { "indexer", util.map(indexer, function(item) return obj({ { "row", item.row }, { "role", item.role }, { "part_index", item.part_index } }) end) },
      { "members", util.sorted(members, function(item) return { item.row, item.part_index, item.role } end) },
    })
  end
  return records
end

local function slot_has_audio(slots)
  return util.any(slots or {}, function(slot) return slot.status == "matched" or T(slot.zh_ref) end)
end

local function node_by_id(by_row, node_id)
  if not T(node_id) then return nil end
  for _, node in pairs(by_row) do if node.id == node_id then return node end end
  return nil
end
local function id_voiced(by_row, node_id)
  local node = node_by_id(by_row, node_id)
  return node ~= nil and slot_has_audio(node.audio_slots)
end

-- First GetEndConversationADV match. ends must already be priority-descending.
local function first_farewell(ends, by_row, talk_empty, favor_half, favor_cap, lewdness_high, time_ratio_low, limit)
  for _, item in ipairs(ends) do
    local condition_id = need_int(item.controlling_condition.id)
    local node = by_row[item.controlling_row]
    if condition_id ~= 101 and condition_id ~= 102 then
      local matched
      if condition_id == 0 then matched = talk_empty and favor_half
      elseif condition_id == 1 then matched = talk_empty and lewdness_high
      elseif condition_id == 2 then matched = (not talk_empty) and time_ratio_low
      elseif condition_id == 3 then matched = (not talk_empty) and not time_ratio_low
      elseif condition_id == 10 then matched = favor_cap and limit ~= nil
      else matched = talk_empty end
      if matched then return node and node.id or nil end
    end
  end
  return nil
end

-- Street-neutral reachability. Unsupported speak ids are not treated as true.
function M.evaluate_reachability(personality, nodes, branches, stepup)
  local by_row = {}
  for _, node in ipairs(nodes) do
    if node.personality == personality and by_row[node.source.row] == nil then by_row[node.source.row] = node end
  end
  local topic_keys = { "daily", "romance", "intimacy" }
  local topics = { daily = {}, romance = {}, intimacy = {} }
  local refusals = {}
  for _, branch in ipairs(branches) do
    if branch.personality == personality and branch.group == "Talk" then
      local category = branch.category
      if topics[category] then
        local cc = T(branch.controlling_condition) and branch.controlling_condition or {}
        local support, speak = cc.street_support, cc.speak_filter
        if support == "supported" and speak ~= "never" then
          local selection = T(branch.selection) and branch.selection or {}
          local indexes = orv(selection.reachable_indexes, {})
          local indexer = orv(branch.indexer, {})
          if #indexer == 0 then
            local voiced
            for _, member in ipairs(util.sorted(orv(branch.members, {}), function(item) return item.part_index end)) do
              local node = by_row[member.row]
              if node and slot_has_audio(node.audio_slots) then voiced = { member, node } break end
            end
            if voiced then
              local member, node = voiced[1], voiced[2]
              table.insert(topics[category], obj({ { "branch", branch.id }, { "row", member.row }, { "role", member.role }, { "node", node.id } }))
            end
          else
            for _, index in ipairs(indexes) do
              if index < #indexer then
                local member = indexer[index + 1]
                local node = by_row[member.row]
                if node and slot_has_audio(node.audio_slots) then
                  local item = obj({ { "branch", branch.id }, { "row", member.row }, { "role", member.role }, { "node", node.id }, { "index", index } })
                  table.insert(topics[category], item)
                  if member.role == "failure" or member.role == "no_interested" then refusals[#refusals + 1] = item end
                end
              end
            end
          end
        end
      end
    end
  end
  local greetings = {}
  local farewells = obj({ { "player_end_full_time", {} }, { "rank_up_at_cap", {} }, { "talk_time_empty", {} } })
  local intro_rows, end_rows = {}, {}
  for _, branch in ipairs(branches) do
    if branch.personality == personality then
      if branch.group == "Introduction" then intro_rows[#intro_rows + 1] = branch
      elseif branch.group == "EndConversation" then end_rows[#end_rows + 1] = branch end
    end
  end
  for stage = 0, 2 do
    local pool = {}
    for _, item in ipairs(intro_rows) do
      if item.stage == stage then
        local condition = item.controlling_condition
        if condition.rule == "fallback_shuffle_pool" and condition.street_support == "supported" then
          local node = by_row[item.controlling_row]
          if node and slot_has_audio(node.audio_slots) then pool[#pool + 1] = node.id end
        end
      end
    end
    greetings[#greetings + 1] = obj({ { "stage", stage }, { "neutral_fallback_voiced", pool } })
    local ends = util.filter(end_rows, function(item) return item.stage == stage end)
    ends = util.sorted(ends, function(item)
      local node = by_row[item.controlling_row]
      local priority = node and node.source and node.source.priority
      return { -need_int(orv(priority, 0)), item.controlling_row }
    end)
    local limit = nil
    if T(stepup) then
      local v1 = stepup[stage]
      if T(v1) then limit = v1 else limit = stepup[tostring(stage)] end
      if limit == null then limit = nil end
    end
    local full = first_farewell(ends, by_row, false, false, false, false, false, limit)
    local cap = first_farewell(ends, by_row, false, true, true, false, false, limit)
    local empty = first_farewell(ends, by_row, true, false, false, false, false, limit)
    table.insert(farewells.player_end_full_time, obj({ { "stage", stage }, { "node", N(full) }, { "voiced", id_voiced(by_row, full) },
      { "assumption", "endSelect, talk budget still full, favor below half, lewdness below 50" } }))
    local cap_node = node_by_id(by_row, cap)
    local cap_condition = cap_node and N(cap_node.conditions.id) or null
    table.insert(farewells.rank_up_at_cap, obj({ { "stage", stage }, { "node", N(cap) }, { "condition_id", cap_condition },
      { "is_friendship_condition", cap_condition == 10 }, { "voiced", id_voiced(by_row, cap) }, { "stepup_limit", N(limit) } }))
    table.insert(farewells.talk_time_empty, obj({ { "stage", stage }, { "node", N(empty) }, { "voiced", id_voiced(by_row, empty) },
      { "assumption", "talkTime is 0, favor below half, lewdness below 50" } }))
  end
  local distinct = json.object()
  for _, category in ipairs(topic_keys) do
    distinct[category] = sorted_unique(util.map(topics[category], function(item) return item.row end))
  end
  local voiced_outcomes, distinct_rows = json.object(), json.object()
  local at_least_three = true
  for _, key in ipairs(topic_keys) do
    voiced_outcomes[key] = #topics[key]
    distinct_rows[key] = #distinct[key]
    if #distinct[key] < 3 then at_least_three = false end
  end
  return obj({
    { "personality", personality },
    { "street_assumptions", {
      "ConditionDeterminationSpeak is not given the original default-true.",
      "Street map is not a KKS map id, isOtherPeople/isChase/isAttack/femaleOnly are false, clothes type is even, coordinate is not 1, isNotice is true.",
      "Opening talkTime is still above 0. Player End is endSelect with a full talk budget unless noted.",
      "H-scene flags and hotel/prayer bonuses are not granted.",
    } },
    { "topics_voiced_outcomes", voiced_outcomes },
    { "topics_distinct_rows", distinct_rows },
    { "topics_at_least_three", at_least_three },
    { "refusal_voiced_outcomes", #refusals },
    { "refusal_reachable", #refusals > 0 },
    { "greeting_fallback_voiced", greetings },
    { "greeting_reachable", util.any(greetings, function(item) return #item.neutral_fallback_voiced > 0 end) },
    { "farewell", farewells },
    { "topics_rows", distinct },
    { "farewell_reachable", util.any(farewells.player_end_full_time, function(item) return item.voiced end)
      or util.any(farewells.talk_time_empty, function(item) return item.voiced end) },
    { "friendship_spoken_at_cap", util.any(farewells.rank_up_at_cap, function(item) return item.is_friendship_condition and item.voiced end) },
    { "friendship_row_selected_at_cap", util.any(farewells.rank_up_at_cap, function(item) return item.is_friendship_condition end) },
  })
end

function M.friendship_command()
  return obj({
    { "id", "friendship" },
    { "label_jp", null },
    { "label_status", "button_text_not_extracted" },
    { "mechanism", "end_conversation_rank_up" },
    { "command_func", null },
    { "event_command", null },
    { "result", "TalkScene.TalkEnd EndADV.RankUp sets isFriend and favor = 0 for a non-NPC" },
    { "trigger", "GetEndConversationADV condition 10 when favor >= stepUpDic[stage]" },
    { "source_refs", { "ActionGame.Communication/Info.cs GetEndConversationADV case 10", "TalkScene.cs TalkEnd EndADV.RankUp" } },
    { "not_a_topic_button", true },
    { "street_support", "described" },
  })
end

function M.confession_command()
  return obj({
    { "id", "confession" },
    { "label_jp", null },
    { "label_status", "button_text_not_extracted" },
    { "button_index", 6 },
    { "command_func", 11 },
    { "event_command", 6 },
    { "stage", 1 },
    { "stage0_rows", "LoadData only creates event commands 0..4 at stage 0" },
    { "stage2_heroine", "GetEventADV case 6 returns null when not an NPC" },
    { "success_effects", { "talkEvent add 1", "talkEvent add 2", "isGirlfriend = true", "favor = 0" } },
    { "selection_kind", "RandomBranch(communicationCondition.Get(favor), 100 - that)" },
    { "not_the_talk_attribute_switch", true },
    { "ignored_on_street", { "benefitsOfHotel.confession", "prayedResult sr_PrayConfess forces success" } },
    { "source_refs", {
      "TalkScene.cs buttonEventContents[i] calls CommandFunc(i + 5)",
      "TalkScene.cs CommandFunc cases 5..11 pass command _kind - 5 to GetEventADV",
      "Info.GetEventADV stage 1 command 6",
      "Info.ConfirmEvent shows buttonEventContents[command] from ConfirmEvent1/2",
    } },
    { "street_support", "described" },
  })
end

local function manifest_index(p)
  if not T(p) or not path.is_file(p) then return {} end
  local payload = load_json(p)
  local found = {}
  for _, entry in ipairs(orv(payload.entries, {})) do
    if T(entry.folder) and T(entry.token) then found[pystr(entry.folder) .. "\0" .. pystr(entry.token)] = entry end
  end
  return found
end
M._manifest_index = manifest_index

local function parse_range(text)
  if none(text) then return nil end
  local raw = pystrip(pystr(text))
  if raw == "" then return nil end
  local parts = util.split(raw, ",")
  local minimum = pyfloat(parts[1])
  if minimum == nil then return nil end
  local maximum = minimum
  if #parts > 1 and pystrip(parts[2]) ~= "" then
    maximum = pyfloat(parts[2])
    if maximum == nil then return nil end
  end
  return obj({ { "min", minimum }, { "max", maximum }, { "through", minimum == -1 and maximum == -1 }, { "raw", raw } })
end
M._parse_range = parse_range

-- Pose, preference, and communication-condition rows. Later sorted bundles overwrite.
function M.load_side_tables(communication_dir)
  local poses, preference, preference_custom, male = {}, {}, json.object(), {}
  for _, p in ipairs(sorted_glob(communication_dir, "info_*.unity3d")) do
    local file_no = path.stem(p):match("_(%d+)$")
    for _, tree in ipairs(mono_trees(p)) do
      local name = pystr(orv(tree.m_Name, ""))
      local pose_no = name:lower():match("^pose_(%-?%d+)$")
      if pose_no then
        local key = need_int(pose_no)
        poses[key] = poses[key] or {}
        local tbl = poses[key]
        local rows = excel_rows(tree)
        for i = 2, #rows do
          local cells = rows[i]
          if #cells > 0 and cells[1] ~= "" then tbl[cells[1]] = util.slice(cells, 2) end
        end
      elseif file_no then
        local lowered = name:lower()
        local rows = excel_rows(tree)
        local function triple(cells)
          local a, b, c, d = pyint(cells[2]), pyint(cells[3]), pyint(cells[4]), pyint(cells[5])
          if a == nil or b == nil or c == nil or d == nil then return nil end
          return a, { b, c, d }
        end
        if lowered == "preference_" .. file_no then
          for i = 2, #rows do
            local cells = rows[i]
            if #cells >= 5 then
              local k, v = triple(cells)
              if k ~= nil then preference[k] = v end
            end
          end
        elseif lowered == "custom_" .. file_no then
          for i = 2, #rows do
            local cells = rows[i]
            if #cells >= 5 then
              local k, v = triple(cells)
              if k ~= nil then preference_custom[k] = v end
            end
          end
        elseif lowered == "communicationcondition_" .. file_no then
          for i = 2, #rows do
            local cells = rows[i]
            if #cells >= 4 then
              local stage, condition = pyint(cells[1]), pyint(cells[2])
              if stage ~= nil and condition ~= nil then
                local parsed = parse_range(cells[4])
                if parsed then male[stage .. "," .. condition] = { stage = stage, condition = condition, value = parsed } end
              end
            end
          end
        end
      end
    end
  end
  return { poses = poses, preference = preference, preference_custom = preference_custom, male_condition = male }
end

local function row_snapshot(param)
  return {
    conditions = int_field(param, "conditions", -1),
    priority = int_field(param, "priority", 0),
    text = orv(param.text, ""),
    favor = param.favor ~= nil and int_field(param, "favor", 0) or nil,
    lewdness = param.lewdness ~= nil and int_field(param, "lewdness", 0) or nil,
    anger = param.anger ~= nil and int_field(param, "anger", 0) or nil,
    pose = orv(param.pose, ""),
    facialExpression = orv(param.facialExpression, ""),
    eyeLook = int_field(param, "eyeLook", 1),
    neckLook = int_field(param, "neckLook", 0),
    file = orv(param.file, ""),
    assetbundle = orv(param.assetbundle, ""),
    topic = int_field(param, "topic", -1),
  }
end

-- Attach the event-command-6 rows without choosing success or failure.
function M.decorate_confession_branches(branches, snapshots, voice_folder, audio_index, audio_root, male)
  local selected = util.filter(branches, function(branch) return branch.group == "Event" and branch.command == 6 end)
  selected = util.sorted(selected, function(branch) return { branch.stage, branch.source_rows[1] or 0 } end)
  for i, branch in ipairs(selected) do
    local index = i - 1
    branch.event_list_index = index
    local condition_id = need_int(branch.controlling_condition.id)
    local entry = (male or {})[need_int(branch.stage) .. "," .. condition_id]
    local probability = entry and entry.value or nil
    local is_choice = util.any(branch.members, function(member) return member.role == "select_intro" or member.role == "choice" end)
    if is_choice then
      branch.selection = obj({ { "kind", "select_not_confession_roll" }, { "picked", null }, { "street_support", "unsupported" },
        { "reason", "GetEventADV command 6 casts list index 0 or 1 as BranchInfo. This select is not that cast." } })
      branch.used_when = null
    else
      branch.selection = obj({ { "kind", "communication_probability" }, { "picked", null },
        { "street_support", probability and "described" or "unsupported" }, { "probability", N(probability) },
        { "success_plays_indexer_0", true }, { "failure_role", "failure" },
        { "ignored_inputs", { "benefitsOfHotel.confession", "prayedResult sr_PrayConfess forces success" } },
        { "source", "Info.GetEventADV stage 1 command 6" } })
      branch.used_when = index == 0 and "mapNo != 9" or "mapNo == 9"
    end
    local lines = {}
    for _, member in ipairs(util.sorted(branch.members, function(item) return { item.row, item.part_index } end)) do
      local snap = member.row < #snapshots and snapshots[member.row + 1] or {}
      local sequence = M.resolve_voice_sequence(orv(snap.file, ""), orv(snap.assetbundle, ""), voice_folder, audio_index, audio_root)
      lines[#lines + 1] = obj({
        { "row", member.row }, { "role", member.role }, { "part_index", member.part_index },
        { "text_jp", orv(snap.text, "") }, { "favor", N(snap.favor) }, { "lewdness", N(snap.lewdness) }, { "anger", N(snap.anger) },
        { "pose", orv(snap.pose, "") }, { "expression", orv(snap.facialExpression, "") },
        { "eye_look", N(snap.eyeLook) }, { "neck_look", N(snap.neckLook) }, { "voice_sequence", sequence },
      })
    end
    branch.lines = lines
  end
end

local function attach_runtime_voice(target, slots)
  local voice_sequence, voice_files, voice_files_zh = M.bind_runtime_voice(slots)
  target.source.voice_sequence = voice_sequence
  if voice_files ~= nil then target.voice_files = voice_files end
  if voice_files_zh ~= nil then target.voice_files_zh = voice_files_zh end
end

function M.build_completion_catalogue(packs, gestures, content_root, manifest_idx, stepup)
  local nodes, branches, reachability, ja_cache = {}, {}, {}, {}
  local by_state = T(gestures.by_state) and gestures.by_state or json.object()
  for _, pack in ipairs(packs) do
    local personality, pose_table, folder = pack.personality, pack.pose_table, pack.voice_folder
    for _, node in ipairs(pack.nodes) do
      local source = ocopy(node.source)
      local sequence = orv(source.voice_sequence, {})
      local slots
      if T(folder) then
        slots = M.portable_audio_slots(sequence, folder, content_root, manifest_idx, ja_cache)
      else
        slots = {}
        for _, part in ipairs(sequence) do
          slots[#slots + 1] = obj({ { "index", N(part.index) }, { "token", N(part.token) }, { "status", "voice_profile_" .. pystr(pack.voice_status) },
            { "zh_ref", null }, { "zh_status", "not_applicable" }, { "ja_ref", null }, { "ja_status", "not_applicable" } })
        end
      end
      local pose_cells = pose_table[orv(source.pose, "")]
      local performance = M.map_performance(orv(source.pose, ""), pose_cells, orv(source.facial_expression, ""),
        source.eye_look, source.neck_look, by_state)
      json.remove(source, "zh_wavs")
      local row_condition = json.is_object(node.conditions) and node.conditions.id or -1
      local built = obj({
        { "id", node.id }, { "personality", node.personality }, { "category", node.category },
        { "text_jp", node.text_jp }, { "text_zh", node.text_zh }, { "favor", node.favor }, { "lewdness", node.lewdness },
        { "conditions", M.condition_semantics(row_condition, context_for_group(source.group)) },
        { "audio_slots", slots }, { "performance", performance }, { "source", source },
      })
      attach_runtime_voice(built, slots)
      nodes[#nodes + 1] = built
    end
    for _, b in ipairs(pack.branches) do branches[#branches + 1] = b end
    reachability[#reachability + 1] = M.evaluate_reachability(personality, nodes, pack.branches, stepup)
  end
  local commands = obj({ { "friendship", M.friendship_command() }, { "confession", M.confession_command() } })
  local friendship_lines = {}
  for _, node in ipairs(nodes) do
    if node.category == "farewell" and json.is_object(node.conditions) and node.conditions.id == 10 then
      friendship_lines[#friendship_lines + 1] = obj({ { "node", node.id }, { "personality", node.personality },
        { "stage", node.source.stage }, { "row", node.source.row }, { "condition", N(node.conditions.id) } })
    end
  end
  commands.friendship.lines = friendship_lines
  local confession_branches = {}
  for _, pack in ipairs(packs) do
    local folder, pose_table = pack.voice_folder, pack.pose_table
    for _, branch in ipairs(pack.branches) do
      if branch.group == "Event" and branch.command == 6 then
        for _, line in ipairs(orv(branch.lines, {})) do
          local sequence = orv(line.voice_sequence, {})
          if T(folder) then
            line.audio_slots = M.portable_audio_slots(sequence, folder, content_root, manifest_idx, ja_cache)
          else
            line.audio_slots = util.map(sequence, function(part)
              return obj({ { "index", N(part.index) }, { "token", N(part.token) }, { "status", N(part.status) }, { "zh_ref", null }, { "ja_ref", null } })
            end)
          end
          line.performance = M.map_performance(orv(line.pose, ""), pose_table[orv(line.pose, "")], orv(line.expression, ""),
            line.eye_look, line.neck_look, by_state)
          local voice_sequence, voice_files, voice_files_zh = M.bind_runtime_voice(line.audio_slots)
          line.voice_sequence = voice_sequence
          if voice_files ~= nil then line.voice_files = voice_files end
          if voice_files_zh ~= nil then line.voice_files_zh = voice_files_zh end
        end
        confession_branches[#confession_branches + 1] = branch
      end
    end
  end
  commands.confession.branches = confession_branches
  local available = {}
  for _, state in ipairs(util.sorted(json.keys(by_state))) do
    local item = by_state[state]
    available[#available + 1] = obj({ { "state", state }, { "file", item.file }, { "sha256", N(item.sha256) } })
  end
  return obj({
    { "schema", COMPLETION_SCHEMA },
    { "audio_schema", AUDIO_SCHEMA },
    { "generator", "Tools/build_street_social_catalogue.lua" },
    { "audio_root", "Content/IndependentResources/StreetSocialData" },
    { "voice_files_are", "source.voice_sequence keeps every slot, including holes. path is a portable ja_ogg ref or null. zh_ogg is a portable zh_ogg ref or null. voice_files and voice_files_zh are the same order and are emitted only when every slot is matched. Relative refs resolve against audio_root." },
    { "runtime_adapter", obj({
      { "parser", "OKStreetReadVoiceSequence in Source/OpenKoikatsu/OKStreetSocialRules.h" },
      { "do_not_edit_rules", true },
      { "voice_sequence_complete_requires", "status matched, sequential index, non-empty path on every slot" },
      { "default_playback", "zh_ogg when set, otherwise path. voice_files is ignored unless the node has voice_language." },
      { "voice_files_zh_is_not_read", true },
      { "incomplete_omits_voice_files", true },
      { "resolve_against", "audio_root, not the CompletionData directory. OKStreetResolveAgainst joins a relative ref to the catalogue file directory." },
      { "japanese_file", "catalogue_ja.json" },
      { "japanese_selection", "voice_language=ja and voice_files are ja_ogg refs; voice_sequence.zh_ogg is null" },
      { "same_line_fallback", "a matched slot with a null zh_ogg plays path, the shared ja_ogg ref" },
    }) },
    { "replaces_runtime_schema_1", false },
    { "nodes", nodes },
    { "branches", util.filter(branches, function(branch) return branch.group ~= "Event" end) },
    { "commands", commands },
    { "reachability", reachability },
    { "gestures_available", available },
    { "gestures_missing", orv(gestures.missing, {}) },
  })
end

local function attach_branch_conditions(completion)
  local by_row = {}
  for _, branch in ipairs(orv(completion.branches, {})) do
    for _, member in ipairs(orv(branch.members, {})) do
      local key = branch.personality .. "\0" .. member.row
      by_row[key] = by_row[key] or {}
      table.insert(by_row[key], branch)
    end
  end
  for _, node in ipairs(orv(completion.nodes, {})) do
    local found = by_row[node.personality .. "\0" .. node.source.row] or {}
    node.branch_ids = util.map(found, function(item) return item.id end)
    if node.source.group == "Talk" and #found > 0 then
      node.controlling_conditions = util.map(found, function(item) return item.controlling_condition end)
      node.conditions.row_spreadsheet_is_not_the_branch_gate = true
    end
  end
end

local function completion_rules(stepup, side_tables, gestures, completion)
  local male = {}
  local entries = {}
  for _, v in pairs((side_tables or {}).male_condition or {}) do entries[#entries + 1] = v end
  for _, e in ipairs(util.sorted(entries, function(e) return { e.stage, e.condition } end)) do
    local row = obj({ { "stage", e.stage }, { "condition", e.condition } })
    for k, v in pairs(e.value) do row[k] = v end
    male[#male + 1] = row
  end
  local preference = json.object()
  for _, personality in ipairs(FEMALE_PROFILES) do
    preference[tostring(personality)] = N(((side_tables or {}).preference or {})[personality])
  end
  local custom = (side_tables or {}).preference_custom
  if not T(custom) then custom = json.object() end
  return obj({
    { "schema", COMPLETION_SCHEMA },
    { "stepup", stepup },
    { "speak_support", obj({ { "supported", SPEAK_SUPPORTED }, { "described_unsupported", SPEAK_DESCRIBED_UNSUPPORTED },
      { "other_condition_ids", "unsupported even though ConditionDeterminationSpeak returns true" } }) },
    { "branch_selection_does_not_pick", true },
    { "friendship", M.friendship_command() },
    { "confession", M.confession_command() },
    { "preference_for_exported_personalities", preference },
    { "preference_custom_attribute_deltas", custom },
    { "communication_condition_probability", male },
    { "gesture_exact_states", util.map(GESTURE_SOURCE, function(g) return obj({ { "state", g[1] }, { "file", g[2] } }) end) },
    { "gestures_available", N(completion.gestures_available) },
    { "gestures_missing", N(completion.gestures_missing) },
    { "mapping_apis", obj({ { "speak", "OKStreetSpeakRule" }, { "branch_selection", "OKStreetTalkSelection" },
      { "gesture", "OKStreetGestureForState" }, { "audio_ref", "OKStreetIsPortableAudioRef" },
      { "header", "Source/OpenKoikatsu/OKStreetSocialPerformance.h" } }) },
  })
end

local function completion_gaps(completion)
  local audio, ja, gestures = counter(), counter(), counter()
  local nonportable = 0
  for _, node in ipairs(orv(completion.nodes, {})) do
    for _, slot in ipairs(orv(node.audio_slots, {})) do
      count(audio, orv(slot.zh_status, slot.status))
      count(ja, orv(slot.ja_status, "missing"))
    end
    local performance = T(node.performance) and node.performance or {}
    local gesture = performance.gesture
    if gesture == nil then gesture = {} end
    count(gestures, gesture.status)
    for _, slot in ipairs(orv((T(node.source) and node.source or {}).voice_sequence, {})) do
      for _, key in ipairs({ "path", "zh_ogg" }) do
        local ref = slot[key]
        if T(ref) and not M.is_portable_audio_ref(ref) then nonportable = nonportable + 1 end
      end
    end
  end
  return obj({
    { "audio_slot_status", audio },
    { "ja_slot_status", ja },
    { "gesture_status", gestures },
    { "callname_not_fabricated", audio.unsupported or 0 },
    { "missing_zh", audio.missing_zh or 0 },
    { "missing_ja", (ja.missing_original or 0) + (ja.sha_conflict or 0) + (ja.copy_mismatch or 0) },
    { "nonportable_voice_refs", nonportable },
    { "missing_clip", gestures.missing_clip or 0 },
    { "missing_pose", gestures.missing_pose or 0 },
  })
end

local function completion_summary(completion, content_root, manifest_path)
  local headline = {}
  for _, item in ipairs(orv(completion.reachability, {})) do
    headline[#headline + 1] = obj({
      { "personality", item.personality },
      { "topics_distinct_rows", item.topics_distinct_rows },
      { "topics_at_least_three", item.topics_at_least_three },
      { "refusal_reachable", item.refusal_reachable },
      { "greeting_reachable", item.greeting_reachable },
      { "farewell_reachable", item.farewell_reachable },
      { "friendship_spoken_at_cap", item.friendship_spoken_at_cap },
      { "friendship_row_selected_at_cap", item.friendship_row_selected_at_cap },
      { "player_end_full_time", util.map(item.farewell.player_end_full_time, function(row) return N(row.node) end) },
      { "talk_time_empty", util.map(item.farewell.talk_time_empty, function(row) return N(row.node) end) },
      { "rank_up_at_cap", util.map(item.farewell.rank_up_at_cap, function(row)
        return obj({ { "node", N(row.node) }, { "condition_id", N(row.condition_id) }, { "voiced", N(row.voiced) } })
      end) },
    })
  end
  return obj({
    { "schema", COMPLETION_SCHEMA },
    { "audio_schema", AUDIO_SCHEMA },
    { "nodes", #orv(completion.nodes, {}) },
    { "branches", #orv(completion.branches, {}) },
    { "audio_root", winpath(content_root) },
    { "reused_manifest", winpath(manifest_path) },
    { "retranscoded", false },
    { "headline", headline },
    { "profiles", util.copy(FEMALE_PROFILES) },
  })
end

-- Confirm completion slots still follow the schema-1 original links.
local function schema1_voice_check(completion, catalogue_path, content_root)
  if not path.is_file(catalogue_path) then return obj({ { "checked", false }, { "reason", "schema1_catalogue_missing" } }) end
  local payload = load_json(catalogue_path)
  if payload.schema ~= 1 then return obj({ { "checked", false }, { "reason", "not_schema_1" }, { "schema", N(payload.schema) } }) end
  local by_id = {}
  for _, node in ipairs(orv(payload.nodes, {})) do
    local id = node.id
    if id == nil then id = null end
    by_id[id] = node
  end
  local checked, sequence_mismatches, sequence_mismatch_count = 0, {}, 0
  local sha_match, sha_mismatch, sha_mismatch_count, missing_link = 0, {}, 0, {}
  local sha_cache = {}
  local function cached_sha(p)
    local key = winpath(p)
    if sha_cache[key] == nil then sha_cache[key] = path.is_file(p) and sha256_file(p) or false end
    return sha_cache[key] or nil
  end
  local function same(a, b) return (none(a) and none(b)) or a == b end
  for _, node in ipairs(orv(completion.nodes, {})) do
    local id = node.id
    local original = id ~= nil and by_id[id] or nil
    if T(original) then
      checked = checked + 1
      local old = orv((T(original.source) and original.source or {}).voice_sequence, {})
      local new = orv((T(node.source) and node.source or {}).voice_sequence, {})
      local ok_same = #old == #new
      if ok_same then
        for i = 1, #old do
          local l, r = old[i], new[i]
          if not (same(l.index, r.index) and same(l.token, r.token) and same(l.status, r.status)) then ok_same = false break end
        end
      end
      if not ok_same then
        sequence_mismatch_count = sequence_mismatch_count + 1
        if #sequence_mismatches < 20 then sequence_mismatches[#sequence_mismatches + 1] = N(node.id) end
      else
        for i = 1, math.min(#old, #new) do
          local left, right = old[i], new[i]
          if right.status == "matched" then
            local link, ja_ref = left.path, right.path
            if not T(link) or not T(ja_ref) then
              if #missing_link < 20 then missing_link[#missing_link + 1] = obj({ { "id", N(node.id) }, { "token", N(right.token) } }) end
            else
              local dest = path.join(content_root, pystr(ja_ref))
              local a = cached_sha(link)
              if a and a == cached_sha(dest) then sha_match = sha_match + 1
              else
                sha_mismatch_count = sha_mismatch_count + 1
                if #sha_mismatch < 20 then sha_mismatch[#sha_mismatch + 1] = obj({ { "id", N(node.id) }, { "token", N(right.token) } }) end
              end
            end
          end
        end
      end
    end
  end
  return obj({
    { "checked", true }, { "nodes", checked }, { "sequence_mismatch_count", sequence_mismatch_count },
    { "sequence_mismatch_ids", sequence_mismatches }, { "ja_sha_match", sha_match },
    { "ja_sha_mismatch_count", sha_mismatch_count }, { "ja_sha_mismatch", sha_mismatch }, { "missing_original_link", missing_link },
  })
end

-- Portable refs only. Original KKS paths are not copied into this file.
local function audio_catalogue(completion)
  local rows = {}
  for _, node in ipairs(orv(completion.nodes, {})) do
    for _, slot in ipairs(orv(node.audio_slots, {})) do
      rows[#rows + 1] = obj({
        { "node", node.id }, { "index", N(slot.index) }, { "token", N(slot.token) }, { "status", N(slot.status) },
        { "zh_ref", N(slot.zh_ref) }, { "zh_sha256", N(slot.zh_sha256) }, { "zh_status", N(slot.zh_status) },
        { "ja_ref", N(slot.ja_ref) }, { "ja_sha256", N(slot.ja_sha256) }, { "ja_status", N(slot.ja_status) },
        { "kind", N(slot.kind) }, { "bundle", N(slot.bundle) }, { "original", N(slot.original) },
      })
    end
  end
  return obj({ { "schema", AUDIO_SCHEMA }, { "root", N(completion.audio_root) },
    { "order", "node audio_slots index order; missing slots stay in place" }, { "slots", rows } })
end

local function obj_len(t) local n = 0; for _ in pairs(t) do n = n + 1 end; return n end

function M.export_catalogue(args)
  local game = args.game
  local abdata = T(args.abdata) and args.abdata or path.join(game, "abdata")
  local voice_root = args.voice_extract
  local audio_root = path.join(voice_root, "audio")
  local zh_root = path.join(voice_root, "cn_audio_comm")
  local out_data, out_content = args.out_data, args.out_content
  local personalities_filter = nil
  if T(args.personalities) then
    personalities_filter = {}
    local profiles = {}
    for _, part in ipairs(util.split(args.personalities, ",")) do
      if pystrip(part) ~= "" then
        personalities_filter[need_int(part)] = true
        profiles[#profiles + 1] = need_int(part)
      end
    end
    -- Profiles reported in summary/rules follow the exported set (was the fixed five-card lineup).
    FEMALE_PROFILES = profiles
    M.FEMALE_PROFILES = profiles
  end
  local completion_mode = (args.mode or "catalogue") == "completion"
  if completion_mode then args.transcode = "none" end
  local voice_table = M.load_voice_table(path.join(abdata, "etcetra", "list", "config"))
  local stepup = M.load_stepup(path.join(abdata, "action", "list", "stepup"))
  local path_table, path_order = M.load_communication_paths(path.join(abdata, "communication"))
  local side_tables = completion_mode and M.load_side_tables(path.join(abdata, "communication")) or nil
  local occurrence = M.load_occurrence(path.join(abdata, "communication"))
  local comm_lines = load_json(path.join(voice_root, "comm_lines.json"))
  local comm_zh = load_json(path.join(voice_root, "translation_comm", "comm_zh.json"))
  local audio_index = load_json(path.join(voice_root, "tools", "audio_index.json"))
  local translation_dir = T(args.translation_text) and args.translation_text or path.join(game, "BepInEx", "Translation", "zh-TW", "Text")
  local exact_translations = M.load_exact_translations(translation_dir)
  local clip_owner = M.clip_owners_from_lines(comm_lines)
  local cache = {}
  local nodes, completion_packs, layout_errors, other_rows, personality_reports = {}, {}, {}, {}, {}
  local option_cache = {}
  local all_gaps = {}

  for _, personality in ipairs(util.sorted(util.sorted_keys(path_table))) do
    if personalities_filter ~= nil and not personalities_filter[personality] then goto continue end
    do
      local entry = path_table[personality]
      local tree, asset_path = M.load_named_tree(abdata, entry.bundle, entry.file, cache)
      local profile = M.resolve_voice_profile(personality, voice_table)
      local report = obj({ { "personality", personality }, { "asset", entry.file }, { "bundle", entry.bundle },
        { "asset_exists", T(tree) }, { "asset_path", winpath(asset_path) }, { "voice", profile } })
      if not T(tree) or not is_list(tree.param) then
        report.status = "missing_table"
        personality_reports[#personality_reports + 1] = report
        layout_errors[#layout_errors + 1] = obj({ { "personality", personality }, { "reason", "missing_communication_table" },
          { "bundle", entry.bundle }, { "file", entry.file } })
        goto continue
      end
      local params = tree.param
      local is_npc = entry.file:sub(1, 16) == "CommunicationNPC" or (#params > 0 and json.is_object(params[1]) and params[1].favor == nil)
      local placements, errors, table_kind
      if is_npc then
        placements, errors = M.place_npc_rows(#params); table_kind = "npc"
      else
        placements, errors = M.place_heroine_rows(#params); table_kind = "heroine"
      end
      report.rows = #params
      report.table_kind = table_kind
      report.layout_errors = #errors
      report.status = #errors == 0 and "ok" or "layout_incomplete"
      for _, e in ipairs(errors) do
        e.personality = personality
        layout_errors[#layout_errors + 1] = e
      end
      local built, other = M.build_nodes_for_table(personality, params, placements, table_kind, entry.file, entry.bundle)
      for _, item in ipairs(other) do item.personality = personality end
      for _, item in ipairs(other) do if item.reason == "unplaced" then other_rows[#other_rows + 1] = item end end
      local option_items = {}
      if T(entry.option_file) then
        local option_key = pystr(entry.option_bundle) .. "\0" .. pystr(entry.option_file)
        if option_cache[option_key] == nil then
          option_cache[option_key] = M.load_option_items(abdata, orv(entry.option_bundle, ""), orv(entry.option_file, ""), cache)
        end
        option_items = option_cache[option_key]
      end
      for _, node in ipairs(built) do
        local label_row = node.source.label_row
        if not none(label_row) and need_int(label_row) >= 0 and need_int(label_row) < #params then
          local condition = int_field(params[need_int(label_row) + 1], "conditions", nil)
          local labels = condition ~= nil and option_items[condition] or nil
          local index = node.source.part_index
          if T(labels) and not none(index) and index < #labels and T(labels[index + 1]) then
            node.source.choice_label = labels[index + 1]
          else
            node.source.choice_label = null
            node.source.choice_label_status = "unsupported"
          end
        end
      end
      local gaps = M.attach_text_and_voice(built, profile, comm_lines, comm_zh, clip_owner, audio_index, audio_root, zh_root, exact_translations)
      report.nodes = #built
      report.audio_gaps = #gaps
      report.counts = count_nodes(built)
      personality_reports[#personality_reports + 1] = report
      for _, n in ipairs(built) do nodes[#nodes + 1] = n end
      if completion_mode then
        local snapshots = {}
        for i, row in ipairs(params) do snapshots[i] = json.is_object(row) and row_snapshot(row) or {} end
        local row_conditions = {}
        for i, item in ipairs(snapshots) do row_conditions[i - 1] = item.conditions end
        completion_packs[#completion_packs + 1] = {
          personality = personality,
          nodes = built,
          branches = M.build_branch_records(personality, placements, row_conditions, table_kind),
          pose_table = side_tables.poses[personality] or {},
          voice_folder = profile.folder ~= null and profile.folder or nil,
          voice_status = N(profile.status),
          preference = side_tables and side_tables.preference[personality] or nil,
        }
        M.decorate_confession_branches(completion_packs[#completion_packs].branches, snapshots,
          profile.folder ~= null and profile.folder or nil, audio_index, audio_root, side_tables.male_condition)
      end
      for _, gap in ipairs(gaps) do gap.personality = personality end
      for _, gap in ipairs(gaps) do all_gaps[#all_gaps + 1] = gap end
    end
    ::continue::
  end

  local audio_gaps = all_gaps
  local samples = M.choose_sample_profiles(nodes, need_int(args.sample_count))
  local ffmpeg = M.find_ffmpeg()
  local manifest_entries, transcode_errors, decode_results = {}, {}, {}
  local transcode_personalities, tp_count = {}, 0
  local function tp_add(p) if not transcode_personalities[p] then transcode_personalities[p] = true; tp_count = tp_count + 1 end end
  if args.transcode == "all" then
    for _, node in ipairs(nodes) do tp_add(node.personality) end
  elseif args.transcode == "sample" then
    for _, item in ipairs(samples) do tp_add(item.personality) end
  end
  if args.transcode ~= "none" and not ffmpeg then
    transcode_errors[#transcode_errors + 1] = obj({ { "reason", "ffmpeg_not_found" }, { "personalities", util.sorted(util.sorted_keys(transcode_personalities)) } })
  elseif ffmpeg and tp_count > 0 then
    local jobs, seen_wav = {}, {}
    for _, node in ipairs(nodes) do
      if transcode_personalities[node.personality] then
        for _, part in ipairs(orv(node.source.voice_sequence, {})) do
          local wav = part.zh_wav
          if T(wav) and not seen_wav[wav] and part.status == "matched" then
            seen_wav[wav] = true
            local folder = orv(node.source.voice_profile, "unknown")
            local dest = path.join(out_content, "zh_ogg", folder, part.token .. ".ogg")
            jobs[#jobs + 1] = { node.personality, wav, dest, part.token, folder }
          end
        end
      end
    end
    for _, job in ipairs(jobs) do
      local personality, wav, dest, token, folder = table.unpack(job)
      local source_hash = sha256_file(wav)
      local output_hash, err = M.transcode_wav(ffmpeg, wav, dest)
      local entry = obj({
        { "personality", personality }, { "token", token }, { "folder", folder }, { "source_wav", winpath(wav) },
        { "source_sha256", source_hash }, { "settings", obj({ { "codec", "libvorbis" }, { "q", 4 }, { "ar", 22050 } }) },
        { "output_ogg", output_hash and winpath(dest) or null }, { "output_sha256", N(output_hash) },
        { "status", output_hash and "ok" or "failed" },
      })
      if T(err) then entry.error = err end
      manifest_entries[#manifest_entries + 1] = entry
      if entry.status ~= "ok" then transcode_errors[#transcode_errors + 1] = entry end
    end
    local by_output = {}
    for _, entry in ipairs(manifest_entries) do by_output[entry.source_wav] = entry end
    for _, node in ipairs(nodes) do
      if transcode_personalities[node.personality] then
        local zh_paths = {}
        for _, part in ipairs(orv(node.source.voice_sequence, {})) do
          local wav = part.zh_wav
          local entry = T(wav) and by_output[wav] or nil
          if entry and T(entry.output_ogg) then
            zh_paths[#zh_paths + 1] = entry.output_ogg
            part.zh_ogg = entry.output_ogg
            part.zh_ogg_sha256 = entry.output_sha256
          else
            part.zh_ogg = null
          end
        end
        node.voice_files_zh = zh_paths
      end
    end
    local decoded = {}
    for _, node in ipairs(nodes) do
      if transcode_personalities[node.personality] then
        local key = node.personality .. "\0" .. node.category
        if not decoded[key] and T(node.voice_files) then
          decoded[key] = true
          local okd, err = M.decode_check(ffmpeg, node.voice_files[1])
          decode_results[#decode_results + 1] = obj({ { "personality", node.personality }, { "category", node.category },
            { "path", node.voice_files[1] }, { "ok", okd }, { "error", err } })
          if #decode_results >= math.max(5, tp_count * 5) then break end
        end
      end
    end
  end
  for _, node in ipairs(nodes) do
    if node.voice_files_zh == nil then node.voice_files_zh = {} end
  end

  local unplaced_by_personality = counter()
  for _, item in ipairs(other_rows) do count(unplaced_by_personality, item.personality) end
  local gap_counts = counter()
  for _, item in ipairs(audio_gaps) do count(gap_counts, item.kind .. ":" .. pystr(item.status)) end
  local catalogue = obj({
    { "schema", SCHEMA },
    { "generator", "Tools/build_street_social_catalogue.lua" },
    { "category_evidence", obj({
      { "daily", "level6 Button Talk 0 text 世間話; TalkScene.buttonTalkContents[0] calls CommandFunc(0); Info.LoadData Group.Talk command 0" },
      { "romance", "level6 Button Talk 1 text コイバナ; buttonTalkContents[1] calls CommandFunc(1); Group.Talk command 1" },
      { "intimacy", "level6 Button Talk 2 text エッチ; buttonTalkContents[2] calls CommandFunc(2); hidden unless Game.isAddH; Group.Talk command 2. This is not Button R18 0 エッチしたい." },
      { "greeting", "Info.GetIntroductionADV reads Group.Introduction. No separate button label was required for the opening line." },
      { "farewell", "Info.GetEndConversationADV reads Group.EndConversation. Button End exists; its TMP text was not extracted." },
      { "not_classified", "Topic, Listen, Tips, Event, See, Touch, Interlude, LeaveAlone, RankUp, and NPC Group.Talk stay out of these five categories." },
    }) },
    { "voice_files_are", "absolute matched original ogg paths, in clip order, with unresolved slots omitted" },
    { "nodes", nodes },
  })
  local voice_negative = {}
  for _, k in ipairs(util.sorted(util.sorted_keys(voice_table))) do if k < 0 then voice_negative[#voice_negative + 1] = k end end
  local ok_count = 0
  for _, entry in ipairs(manifest_entries) do if entry.status == "ok" then ok_count = ok_count + 1 end end
  local summary = obj({
    { "schema", SCHEMA },
    { "personalities", #personality_reports },
    { "counts", count_nodes(nodes) },
    { "layout_errors", #layout_errors },
    { "unplaced_rows", #other_rows },
    { "unplaced_by_personality", unplaced_by_personality },
    { "gap_counts", gap_counts },
    { "sample_profiles", samples },
    { "ffmpeg", N(ffmpeg) },
    { "transcode", obj({ { "mode", args.transcode }, { "ok", ok_count }, { "failed", #transcode_errors } }) },
    { "decode_checks", decode_results },
    { "stepup_limits", stepup.limits },
    { "voice_negative_ids", voice_negative },
    { "path_bundle_order", path_order },
    { "comm_lines", obj_len(comm_lines) },
    { "comm_zh", obj_len(comm_zh) },
    { "audio_index", obj_len(audio_index) },
  })
  local voice_table_out, paths_out = json.object(), json.object()
  for _, k in ipairs(util.sorted(util.sorted_keys(voice_table))) do voice_table_out[tostring(k)] = voice_table[k] end
  for _, k in ipairs(util.sorted(util.sorted_keys(path_table))) do paths_out[tostring(k)] = path_table[k] end
  local occurrence_examples = {}
  for _, v in pairs(occurrence) do
    if #occurrence_examples >= 8 then break end
    occurrence_examples[#occurrence_examples + 1] = v
  end
  local rules = obj({
    { "stepup", stepup },
    { "voice_table", voice_table_out },
    { "paths", paths_out },
    { "speak_support", obj({ { "supported", SPEAK_SUPPORTED }, { "described_unsupported", SPEAK_DESCRIBED_UNSUPPORTED },
      { "other_condition_ids", "unsupported even though ConditionDeterminationSpeak returns true" } }) },
    { "branch_selection", obj({ { "street_support", "unsupported" },
      { "reason", "CreateBranchADV uses attribute, preference, talkTemper, or random weights. This export keeps every branch line and does not pick one." } }) },
    { "occurrence_street_support", "unsupported" },
    { "occurrence_count", obj_len(occurrence) },
    { "occurrence_examples", occurrence_examples },
    { "topic_paths_not_classified", true },
    { "choice_label_fallback_literal_not_used", "ああ" },
  })
  local gaps = obj({
    { "layout_errors", slice(layout_errors, 200) },
    { "layout_error_count", #layout_errors },
    { "unplaced_row_count", #other_rows },
    { "unplaced_examples", slice(other_rows, 30) },
    { "audio_gap_counts", gap_counts },
    { "audio_gap_examples", slice(audio_gaps, 40) },
    { "transcode_errors", slice(transcode_errors, 40) },
    { "personality_reports", personality_reports },
    { "occurrence_not_applied", obj_len(occurrence) },
  })
  local audio_manifest = obj({
    { "settings", obj({ { "codec", "libvorbis" }, { "q", 4 }, { "ar", 22050 } }) },
    { "ffmpeg", N(ffmpeg) },
    { "entries", manifest_entries },
    { "decode_checks", decode_results },
  })
  local completion_headline = null
  if completion_mode then
    local content_root = T(args.audio_content) and args.audio_content or out_content
    local manifest_path = path.join(content_root, "audio_manifest.json")
    local gesture_manifest = T(args.gesture_manifest) and args.gesture_manifest
      or path.join(path.parent(content_root), "RuntimeAnimations", "actions.manifest.json")
    local animation_dir = path.parent(gesture_manifest)
    local gestures = M.load_gesture_clips(gesture_manifest, animation_dir)
    local completion = M.build_completion_catalogue(completion_packs, gestures, content_root, manifest_index(manifest_path), stepup.limits)
    attach_branch_conditions(completion)
    local completion_dir = T(args.completion_out) and args.completion_out or path.join(path.parent(out_data), "CompletionData")
    local rules_completion = completion_rules(stepup, side_tables, gestures, completion)
    local gaps_completion = completion_gaps(completion)
    local summary_completion = completion_summary(completion, content_root, manifest_path)
    local schema1_check = schema1_voice_check(completion, path.join(content_root, "catalogue.json"), content_root)
    gaps_completion.schema1_original_links = schema1_check
    summary_completion.ja_pack = "ja_ogg/<folder>/<token>.ogg"
    summary_completion.ja_slot_status = N(gaps_completion.ja_slot_status)
    summary_completion.nonportable_voice_refs = N(gaps_completion.nonportable_voice_refs)
    summary_completion.schema1_ja_sha_match = N(schema1_check.ja_sha_match)
    summary_completion.schema1_sequence_mismatch_ids = N(schema1_check.sequence_mismatch_ids)
    write_json(path.join(completion_dir, "catalogue.json"), completion)
    write_json(path.join(completion_dir, "catalogue_ja.json"), M.japanese_catalogue(completion))
    write_json(path.join(completion_dir, "rules.json"), rules_completion)
    write_json(path.join(completion_dir, "gaps.json"), gaps_completion)
    write_json(path.join(completion_dir, "summary.json"), summary_completion)
    write_json(path.join(completion_dir, "audio_catalogue.json"), audio_catalogue(completion))
    completion_headline = summary_completion.headline
  else
    local function enc(v) return json.encode(v, { ensure_ascii = false, separators = { ",", ":" } }) end
    local encoded = { catalogue = enc(catalogue), summary = enc(summary), rules = enc(rules), gaps = enc(gaps), audio_manifest = enc(audio_manifest) }
    for _, directory in ipairs({ out_data, out_content }) do
      for _, name in ipairs({ "catalogue", "summary", "rules", "gaps", "audio_manifest" }) do
        write_json(path.join(directory, name .. ".json"), nil, encoded[name])
      end
    end
  end
  local failures = util.filter(decode_results, function(item) return not item.ok end)
  print(json.encode(obj({
    { "nodes", summary.counts },
    { "personalities", summary.personalities },
    { "layout_errors", summary.layout_errors },
    { "unplaced_rows", summary.unplaced_rows },
    { "samples", util.map(samples, function(item) return item.personality end) },
    { "transcode", summary.transcode },
    { "decode_failures", failures },
    { "completion", completion_headline },
  }), { ensure_ascii = false }))
  return #layout_errors == 0 and 0 or 2
end

function M.build_arg_parser()
  local p = ok.args.parser("Export KKS street-social communication catalogue")
  local game, repo = ok.game_root(true, "\\"), ok.root:gsub("/", "\\")
  p:opt("--game", { default = game })
  p:opt("--abdata", { default = "" })
  p:opt("--voice-extract", { default = game .. "\\voice_extract" })
  p:opt("--out-data", { default = repo .. "\\Saved\\StreetSocial\\Data" })
  p:opt("--out-content", { default = repo .. "\\Content\\IndependentResources\\StreetSocialData" })
  p:opt("--personalities", { default = "", help = "Comma-separated personality ids. Empty exports every path-table id." })
  p:opt("--sample-count", { type = "int", default = 5 })
  p:opt("--transcode", { choices = { "sample", "none", "all" }, default = "sample" })
  p:opt("--translation-text", { default = "", help = "Directory of exact Japanese=Chinese txt files. Default is the game zh-TW Text folder." })
  p:opt("--mode", { choices = { "catalogue", "completion" }, default = "catalogue" })
  p:opt("--completion-out", { default = "C:\\portable\\OpenKoikatsu\\Saved\\StreetSocial\\CompletionData" })
  p:opt("--audio-content", { default = "C:\\portable\\OpenKoikatsu\\Content\\IndependentResources\\StreetSocialData" })
  p:opt("--gesture-manifest", { default = "C:\\portable\\OpenKoikatsu\\Content\\IndependentResources\\RuntimeAnimations\\actions.manifest.json" })
  return p
end

function M.main(argv)
  local args = M.build_arg_parser():parse(argv or arg)
  return M.export_catalogue(args)
end

if ok.is_main() then
  ok.main(function() return M.main(arg) end)
end

return M
