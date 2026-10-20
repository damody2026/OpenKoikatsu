-- Verify shared topic audio hashes and independent runtime reward evidence.
local ok = require "ok"
local json, path, util = ok.json, ok.path, ok.util
local T = util.truthy
local M = {}

local function read(p) return json.decode(path.read_text(p)) end
local function clamp(lo, hi, v) return math.min(hi, math.max(lo, v)) end
-- 'catalogue-' + SHA-1 of the resolved forward-slash path, upper case (OKStreet voice ids).
local function voice_id(p) return "catalogue-" .. ok.hash.sha1(path.abs(p)):upper() end
local function committed(save, row)
  for _, r in ipairs(save.relations) do
    if r.id == row.id and util.contains_value(r.committed, row.transaction) then return true end
  end
  return false
end

function M.verify(root, runtime)
  local data_root = path.join(root, "Content/IndependentResources/StreetSocialData")
  local manifest = read(path.join(runtime, "selection.json"))
  local source_path = manifest.source_topic_catalogue
  if source_path == nil then source_path = path.join(data_root, "RuntimeV2/topics.json") end
  local source = read(source_path)
  local refs = json.keys(source.source_voice_sha256)
  local digests = ok.hash.files(util.map(refs, function(ref) return path.join(data_root, ref) end), "sha256")
  for i, ref in ipairs(refs) do
    if not digests[i] then error("FileNotFoundError: " .. path.join(data_root, ref), 0) end
    assert(digests[i] == source.source_voice_sha256[ref], ref)
  end
  manifest = read(path.join(runtime, "selection.json"))
  local data = read(path.join(runtime, "topics.runtime.json"))
  local report = read(path.join(runtime, "social.validation.json"))
  local save = read(path.join(runtime, "collected-topics.relationships.json"))
  assert(T(report.conversation_controls_checked))
  assert(T(report.touch_input_routing_checked) and T(report.touch_missing_subtitle_checked))
  assert(T(report.touch_missing_recovery_checked), "Missing-source touch recovery was not tested")
  local rows = report.collected_topic_evidence
  assert(T(report.collected_topics_checked) and #rows == 4)
  local topics, profiles = {}, {}
  for _, t in ipairs(data.topics) do topics[t.No] = t end
  for _, p in ipairs(data.profiles) do profiles[p.personality] = p end
  local seen, nseen, outcomes = {}, 0, {}
  local hero = manifest.characters[2]
  for _, row in ipairs(rows) do
    assert(row.id == hero.id and row.personality == hero.personality)
    assert(not seen[row.transaction]); seen[row.transaction] = true; nseen = nseen + 1
    assert(committed(save, row))
    local answer = topics[row.used]
    local asked = row.asked ~= json.null and topics[row.asked] or nil
    local profile = profiles[row.personality]
    assert(profile ~= nil, "KeyError")
    local outcome, favor, lewd, choices
    if asked then
      outcome = asked.No == answer.No and "success" or (asked.Category == answer.Category and "normal" or "failure")
      local index = ({ success = 0, normal = 1, failure = 2 })[outcome]
      local point = data.listen_points[((answer.Rarity == 0 or index ~= 1) and index or 2) + 1]
      favor = point[answer.Rarity == 0 and "common" or "rare"]; lewd = 0
      local listen
      for _, l in ipairs(profile.listen) do if l.topic == asked.No then listen = l break end end
      assert(listen, "StopIteration")
      choices = {}
      for _, part in ipairs(listen[outcome]) do choices[#choices + 1] = { part } end
    else
      outcome = answer.Category == 4 and "erotic_offer" or "offer"
      local interests = hero.interest_categories or {}
      local base = data.talk_points[answer.Rarity + 1].favor
      local bonus = util.contains_value(interests, answer.Category) and data.interest[answer.Category + 1] or { favor = 0, lewdness = 0 }
      favor = (answer.Category == 4 and 0 or base) + bonus.favor
      lewd = (answer.Category == 4 and base or 0) + bonus.lewdness
      choices = profile.offer[util.num(answer.No)]
    end
    outcomes[#outcomes + 1] = outcome
    assert(row.favor_after == clamp(0, 75, row.favor_before + favor))
    assert(row.lewd_after == clamp(0, 100, row.lewd_before + lewd))
    assert(row.stock_after == 0)
    local expected = {}
    for _, choice in ipairs(choices) do
      local ids = {}
      for _, part in ipairs(choice) do for _, p in ipairs(part.voices) do ids[#ids + 1] = voice_id(p) end end
      expected[#expected + 1] = ids
    end
    local found = false
    for _, ids in ipairs(expected) do if util.equal(ids, row.voice_ids) then found = true end end
    assert(found, util.repr({ outcome, row.voice_ids, expected }))
  end
  assert(util.equal(outcomes, { "success", "normal", "failure", "erotic_offer" }), util.repr(outcomes))
  local touches = report.touch_evidence or {}
  assert(T(report.touch_checked) and report.touch_commits == 3 and #touches == 3)
  local repeats = {}
  for i, r in ipairs(touches) do repeats[i] = r["repeat"] end
  assert(util.equal(repeats, { 0, 2, 1 }))
  for _, row in ipairs(touches) do
    assert(row.id == hero.id and not seen[row.transaction]); seen[row.transaction] = true; nseen = nseen + 1
    assert(committed(save, row))
    local part = (row.region == 2 or row.region == 3) and 0 or (row.region >= 4 and 1 or (row.region == 0 and 2 or 3))
    local reaction
    for _, t in ipairs(profiles[row.personality].touch) do
      if t.stage == row.stage and t.mode == "Touch" and t.part == part and t["repeat"] == row["repeat"] then reaction = t break end
    end
    assert(reaction, "StopIteration")
    assert(row.favor_after == clamp(0, 75, row.favor_before + reaction.favor))
    assert(row.lewd_after == clamp(0, 100, row.lewd_before + reaction.lewdness))
    local ids = {}
    for _, p in ipairs(reaction.parts) do for _, v in ipairs(p.voices) do ids[#ids + 1] = voice_id(v) end end
    assert(util.equal(row.voice_ids, ids))
  end
  local result = json.object({ { "passed", true }, { "source_voice_hashes", util.len(source.source_voice_sha256) },
    { "runtime_outcomes", outcomes }, { "transactions", nseen }, { "touch_repeats", repeats } })
  path.write_text(path.join(runtime, "collected-topics.independent.validation.json"), json.encode(result, { ensure_ascii = false, indent = 2 }))
  print(json.encode(result, { ensure_ascii = false }))
end

if ok.is_main() then
  ok.main(function()
    local p = ok.args.parser()
    p:arg("runtime")
    M.verify(ok.root, path.abs(p:parse(arg).runtime))
  end)
end

return M
