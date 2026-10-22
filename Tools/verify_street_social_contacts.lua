-- Independently verify contact travel and the actual private-scene evidence.
local ok = require "ok"
local json, path, util = ok.json, ok.path, ok.util
local T = util.truthy
local M = {}

local function read(p) return json.decode(path.read_text(p)) end
local function same_path(a, b) return path.abs(a):lower() == path.abs(b):lower() end
local function isabs(p) p = path.norm(p); return p:match("^%a:/") ~= nil or p:match("^//") ~= nil end
local function voice_id(p) return "catalogue-" .. ok.hash.sha1(p):upper() end
local function in_range(v, a, b) return type(v) == "number" and v >= a and v < b and v == math.floor(v) end

function M.verify(directory)
  local root = ok.root
  local report = read(path.join(directory, "social.validation.json"))
  local manifest = read(path.join(directory, "selection.json"))
  local catalogue_path = manifest.source_dialogue_catalogue
  if catalogue_path == nil then catalogue_path = path.join(root, "Content/IndependentResources/StreetSocialData/RuntimeV2/catalogue.json") end
  local catalogue = read(catalogue_path)
  assert(T(report.passed) and not T(report.errors) and T(report.contacts_checked))
  assert(report.contacts_routes == 5)
  if T(report.private_high_model) then
    assert(report.private_any_verified == 15 and report.contacts_private == 5)
    local cases, ncases = {}, 0
    for _, row in ipairs(report.private_any_evidence) do
      local slot, stage = row.slot, row.stage
      local key = util.num(slot) .. "," .. util.num(stage)
      assert(in_range(slot, 1, 6) and in_range(stage, 0, 3) and not cases[key]); cases[key] = true; ncases = ncases + 1
      local result = read(row.report)
      assert(util.equal(result, row.high_model) and result.state == "returned")
      assert(same_path(result.card, manifest.cards[slot + 1]))
      local report_dir = path.parent(row.report)
      if manifest.interaction_mode == "in_process" then
        assert(T(result.same_process) and result.process_id == math.tointeger(tonumber(util.strip(path.read_text(path.join(directory, "game.pid"))))))
        assert(same_path(result.partner, manifest.cards[1]))
        assert(result.playback_end_seconds > result.playback_start_seconds and result.playback_start_seconds > 0)
        assert(not path.exists(path.join(report_dir, "request.json")))
      else
        local request = read(path.join(report_dir, "request.json"))
        assert(same_path(request.card, result.card) and same_path(request.partner, manifest.cards[1]))
      end
      assert(manifest.characters[slot + 1].id == "apng-" .. require("prepare_street_social_selection").card_identity(result.card))
      assert(result.scene_class == "/Script/OpenKoikatsu.OKInteractionScene" and result.widget_class == "/Script/OpenKoikatsu.OKInteractionWidget")
      assert(T(result.source_geometry) and result.source_parts > 0 and T(result.partner_ready))
      assert(result.render_vertices > 0 and result.render_components > 0)
      assert(path.is_file(path.join(report_dir, "high-interaction.png")))
      if manifest.interaction_mode == "in_process" then
        local log = path.read_text(path.join(directory, "game.log"))
        assert(log:find("OK interaction entered actor=", 1, true) and log:find("Shared interaction ready pid=" .. util.num(result.process_id), 1, true))
      else
        local log = path.read_text(path.join(report_dir, "host.log"))
        assert(log:find("OK interaction entered actor=", 1, true) and log:find("Hosted high interaction ready card=", 1, true))
      end
    end
    local result = json.object({ { "passed", true }, { "private_high_model", true }, { "private_any_verified", ncases } })
    path.write_text(path.join(directory, "contacts.independent.validation.json"), json.encode(result, { indent = 2 }))
    print(json.encode(result))
    return
  end
  assert(report.contacts_routes == 5 and report.contacts_private == 5 and report.contacts_voices == 5)
  local nodes = {}
  for _, n in ipairs(catalogue.nodes) do nodes[n.id] = n end
  local seen, nseen, records = {}, 0, {}
  local audio = catalogue.audio_root
  if not isabs(audio) then audio = path.join(root, audio) end
  local function slot_paths(node, field)
    local paths, slots = {}, {}
    for _, a in ipairs(node.audio_slots) do
      if T(a[field]) then paths[#paths + 1] = path.abs(path.join(audio, a[field])); slots[#slots + 1] = a end
    end
    return paths, slots
  end
  for _, row in ipairs(report.contacts_voice_evidence) do
    local slot = row.slot
    assert(in_range(slot, 1, 6) and not seen[slot]); seen[slot] = true; nseen = nseen + 1
    local node = nodes[row.node]
    assert(node, "KeyError")
    assert(node.category == "greeting" and node.source.stage == 2 and row.stage == 2)
    assert(node.personality == manifest.characters[slot + 1].personality)
    local matches = {}
    for _, field in ipairs({ "zh_ref", "ja_ref" }) do
      local paths = slot_paths(node, field)
      local ids = {}
      for i, p in ipairs(paths) do ids[i] = voice_id(p) end
      if util.equal(ids, row.voice_ids) then
        assert(#paths > 0 and util.all(paths, function(p) return path.is_file(p) end)); matches[#matches + 1] = field
      end
    end
    assert(#matches > 0, util.repr(row))
    records[#records + 1] = json.object({ { "slot", slot }, { "node", row.node }, { "matches", matches } })
  end
  assert(nseen == 5)
  if report.private_any_verified ~= nil then
    assert(report.private_any_verified == 15)
    local cases, ncases = {}, 0
    for _, row in ipairs(report.private_any_evidence) do
      local slot, stage = row.slot, row.stage
      local key = util.num(slot) .. "," .. util.num(stage)
      assert(in_range(slot, 1, 6) and in_range(stage, 0, 3) and not cases[key])
      cases[key] = true; ncases = ncases + 1
      local node = nodes[row.node]
      assert(node, "KeyError")
      assert(node.personality == manifest.characters[slot + 1].personality and node.source.stage == stage and node.category == "greeting")
      local found = false
      for _, field in ipairs({ "zh_ref", "ja_ref" }) do
        local paths = slot_paths(node, field)
        local ids = {}
        for i, p in ipairs(paths) do ids[i] = voice_id(p) end
        if util.equal(ids, row.voice_ids) then
          assert(#paths > 0)
          assert(#node.audio_slots == #paths, "zip() argument 2 is shorter than argument 1")
          local hash_field = field:gsub("_ref", "_sha256")
          for i, a in ipairs(node.audio_slots) do assert(ok.hash.file(paths[i], "sha256") == a[hash_field]) end
          found = true
        end
      end
      assert(found)
    end
    assert(ncases == 15)
  end
  local result = json.object({ { "passed", true }, { "private_source_voices", records },
    { "private_any_verified", report.private_any_verified or 0 } })
  path.write_text(path.join(directory, "contacts.independent.validation.json"), json.encode(result, { ensure_ascii = false, indent = 2 }))
  print(json.encode(result, { ensure_ascii = false }))
end

if ok.is_main() then
  ok.main(function()
    local p = ok.args.parser()
    p:arg("directory")
    M.verify(path.abs(p:parse(arg).directory))
  end)
end

return M
