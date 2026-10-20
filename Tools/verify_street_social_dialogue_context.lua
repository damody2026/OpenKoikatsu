-- Independently compare scene questions and confession dialogue to local original assets.
local ok = require "ok"
local source = require "build_street_social_catalogue"
local json, path, util = ok.json, ok.path, ok.util
local T = util.truthy
local M = {}

local function sha(p) return ok.hash.file(p, "sha256") end
local function read(p) return json.decode(path.read_text(p)) end
local function first(list, pred)
  for _, v in ipairs(list) do if pred(v) then return v end end
  error("StopIteration", 2)
end

-- str.split() on Unicode whitespace.
local function split_ws(s)
  s = s:gsub("\227\128\128", " "):gsub("\194\160", " "):gsub("\226\128[\128-\138\168\169\175]", " "):gsub("\226\129\159", " "):gsub("[\28-\31]", " ")
  local out = {}
  for w in s:gmatch("%S+") do out[#out + 1] = w end
  return out
end
local function plain_replace(s, a, b) return util.replace(s, a, b) end

-- Same rule as OKStreetFillNames: [H...] heroine, [P...] player; full / given (あだ名) / family name.
local function fill(text, prefix, card)
  local parts = split_ws(card)
  local cjk = false
  for _, cp in utf8.codes(card) do if cp >= 0x3000 then cjk = true break end end
  local full = (#parts > 1 and cjk) and table.concat(parts) or card
  text = plain_replace(text, "[" .. prefix .. "名]", full)
  text = plain_replace(text, "[" .. prefix .. "あだ名]", #parts > 0 and parts[#parts] or card)
  text = plain_replace(text, "[" .. prefix .. "]", #parts > 0 and parts[1] or card)
  return plain_replace(text, "[" .. prefix .. "姓]", #parts > 0 and parts[1] or card)
end

local function same_tuple(a, b) return a[1] == b[1] and a[2] == b[2] and a[3] == b[3] end

function M.verify(directory)
  local report = read(path.join(directory, "social.validation.json"))
  local manifest = read(path.join(directory, "selection.json"))
  assert(T(report.passed) and not T(report.errors))
  assert(report.dialogue_context_checks == 20 and report.source_confessions_verified == 5)
  assert(report.relationship_voice_lines >= 17)
  local catalogue = read(manifest.dialogue_catalogue)
  local nodes, branches = {}, {}
  for _, n in ipairs(catalogue.nodes) do nodes[n.id] = n end
  for _, b in ipairs(catalogue.branches) do branches[b.id] = b end
  local function names(text, slot)
    return fill(fill(text, "H", manifest.characters[slot + 1].name), "P", manifest.characters[1].name)
  end
  for _, pair in ipairs(report.dialogue_context_evidence) do
    local node = nodes[pair.node]
    assert(node, "KeyError")
    local slot, meta = pair.slot, node.source
    assert(node.personality == manifest.characters[slot + 1].personality)
    local intro
    if T(pair.branch) then
      local branch = branches[pair.branch]
      assert(branch, "KeyError")
      assert(util.contains_value(util.map(branch.indexer, function(x) return x.row end), meta.row))
      local intro_row = first(branch.members, function(m) return m.role == "introduction" end).row
      intro = first(catalogue.nodes, function(n)
        return n.personality == node.personality and n.source.stage == meta.stage and n.source.group == meta.group
          and n.source.command == meta.command and n.source.row == intro_row
      end)
    else
      local containers = util.set(meta.containers)
      local choices = util.filter(catalogue.nodes, function(n)
        return n.personality == node.personality and not T(n.voice_files) and n.source.stage == meta.stage and n.source.group == meta.group
          and n.source.command == meta.command and n.source.part_index < meta.part_index
          and util.any(n.source.containers, function(cn) return containers[cn] end) and util.contains_value(n.source.roles, "generic")
      end)
      assert(#choices > 0, "max() arg is an empty sequence")
      intro = choices[1]
      for _, n in ipairs(choices) do if n.source.part_index > intro.source.part_index then intro = n end end
    end
    assert(not T(intro.voice_files) and pair.player == names(intro.text_zh, slot))
    local female, line = pair.female, names(node.text_zh, slot)
    if type(female) == "string" then assert(female:find(line, 1, true)) else assert(util.contains_value(female, line)) end
  end
  local original = ok.game_root()
  local data_path = manifest.source_relationship_catalogue
  local data = read(data_path)
  for _, profile in ipairs(data.profiles) do
    local table_path = path.join(original, "abdata", profile.table_source)
    local scenario_path = path.join(original, "abdata", profile.scenario_source)
    assert(sha(table_path) == profile.table_sha256 and sha(scenario_path) == profile.scenario_sha256)
    local wanted = string.format("Communication_%02d", profile.personality)
    local tbl = first(source._mono_trees(table_path), function(t) return t.m_Name == wanted end)
    local intro = tbl.param[profile.accept[1].row + 1]
    local refusal = tbl.param[profile.refuse[#profile.refuse].row + 1]
    assert(profile.accept[1].text_jp == profile.refuse[1].text_jp and profile.refuse[1].text_jp == intro.text)
    assert(profile.refuse[#profile.refuse].text_jp == refusal.text)
    assert(path.stem(profile.refuse[#profile.refuse].voices[1]) == refusal.file)
    local scenario = first(source._mono_trees(scenario_path), function(t) return t.m_Name == "1" end)
    local expected = {}
    for _, cmd in ipairs(scenario.list) do
      if cmd._command == 18 then expected[#expected + 1] = { cmd._args[1] == "[H名]" and "female" or "male", cmd._args[2] } end
    end
    local actual = {}
    for i = 2, #profile.accept do actual[#actual + 1] = { profile.accept[i].speaker, profile.accept[i].text_jp } end
    assert(util.equal(actual, expected))
    local parts = util.extend(util.copy(profile.accept), profile.refuse)
    for _, part in ipairs(parts) do
      assert(T(part.text_zh) and (T(part.voices) == (part.speaker == "female")))
    end
  end
  for _, relative in ipairs(json.keys(data.source_voice_sha256)) do
    local digest = data.source_voice_sha256[relative]
    assert(sha(path.join(path.parent(path.parent(data_path)), relative)) == digest)
    local rel = path.rel(path.join("C:/x", relative), "C:/x/ja_ogg")
    assert(sha(path.join(original, "voice_extract/audio", rel)) == digest)
  end
  local log = path.read_text(path.join(directory, "game.log"))
  local played = {}
  for actor, per, node, clips in log:gmatch("Street dialogue selected: actor=(%d+) node=relationship%-p(%d+)%-(%d+) category=relationship clips=(%d+)") do
    played[#played + 1] = { tonumber(actor), tonumber(per), tonumber(node), tonumber(clips) }
  end
  local by_personality = {}
  for _, p in ipairs(data.profiles) do by_personality[p.personality] = p end
  for slot = 1, #manifest.characters - 1 do
    -- The relationship data may cover more personalities than the five selected actors.
    local profile = by_personality[manifest.characters[slot + 1].personality]
    assert(profile, "KeyError")
    local turns = {}
    for _, p in ipairs(played) do if p[1] == slot then turns[#turns + 1] = { p[2], p[3], p[4] } end end
    local female = 0
    for _, p in ipairs(profile.accept) do if p.speaker == "female" then female = female + 1 end end
    local extra = report.source_confession_refusals_verified == 5 and 2 or 0
    assert(#turns == female + 1 + extra and same_tuple(turns[1], turns[2]))
    if extra > 0 then assert(same_tuple(turns[#turns], turns[#turns - 1]) and turns[#turns][2] == turns[2][2] + female) end
    assert(util.all(turns, function(tr) return tr[1] == profile.personality and tr[3] == 1 end))
    for i = 1, female do assert(turns[i + 1][2] == turns[2][2] + i - 1) end
  end
  local base = log:match("LogInit: Base Directory: ([^\n]+)")
  assert(base, "'NoneType' object has no attribute 'group'")
  local executable = path.join(util.strip(base), "OpenKoikatsu.exe")
  local result = json.object({
    { "passed", true }, { "source_refusals", report.source_confession_refusals_verified or 0 }, { "source_pairs", 20 },
    { "source_confessions", 5 }, { "source_voices", util.len(data.source_voice_sha256) }, { "executable_sha256", sha(executable) },
    { "relationships_sha256", sha(data_path) }, { "catalogue_sha256", sha(manifest.dialogue_catalogue) },
  })
  path.write_text(path.join(directory, "dialogue-context.independent.validation.json"), json.encode(result, { indent = 2 }) .. "\n")
  print(json.encode(result))
end

if ok.is_main() then
  ok.main(function()
    local p = ok.args.parser()
    p:arg("directory")
    M.verify(p:parse(arg).directory)
  end)
end

return M
