-- Independent shared phone curve, actual private actor admission and screenshot evidence.
local ok = require "ok"
local json, path, util = ok.json, ok.path, ok.util
local T = util.truthy
local M = {}

local function read(p) return json.decode(path.read_text(p)) end
local function sha(p) return ok.hash.file(p, "sha256") end
local function same_path(a, b) return path.abs(a):lower() == path.abs(b):lower() end

function M.verify(directory)
  local root = ok.root
  local report = read(path.join(directory, "social.validation.json"))
  local manifest = read(path.join(directory, "selection.json"))
  local pack_path = path.join(root, "Content/IndependentResources/RuntimeAnimations/phone.manifest.json")
  local pack = read(pack_path)
  assert(T(report.passed) and not T(report.errors) and T(report.phone_use_checked))
  assert(report.phone_use_actors_verified == 5 and report.phone_use_voices == 5 and T(report.phone_use_cancel_checked) and report.phone_use_samples > 10)
  assert(not T(pack.card_specific) and T(pack.runtime_private_pose_required))
  assert(pack.prop.parent == "k_f_handL_00" and pack.prop.parent_type == 8)
  assert(manifest.npc_action_preload_count == 0)
  for _, row in ipairs(pack.actions) do
    local curve = path.join(path.parent(pack_path), row.file)
    local data = path.read_bytes(curve)
    assert(data:sub(1, 8) == "OKCLIP01")
    assert(ok.hash.sha256(data) == row.sha256)
    assert(row.source == "action/animator/00.unity3d|f_tachi_00_04")
    local found = false
    for _, a in ipairs(manifest.actions) do
      if same_path(a.curve, curve) and T(a.loop) then found = true break end
    end
    assert(found)
    local source = path.join(ok.game_root() .. "/abdata", util.split(row.source, "|")[1])
    assert(sha(source) == row.source_sha256)
  end
  local log = path.read_text(path.join(directory, "game.log"))
  local actors, list = {}, {}
  for x in log:gmatch("Runtime streamed action slot=%d+ actor=(%d+) clip=phone_standing.okanim") do
    local n = math.tointeger(tonumber(x))
    if not actors[n] then actors[n] = true; list[#list + 1] = n end
  end
  table.sort(list)
  local expected = (#list == 6)
  for i = 0, 5 do if not actors[i] then expected = false end end
  assert(expected, util.repr(list))
  local images = {}
  for slot = 1, 5 do
    for _, phase in ipairs({ "first", "second" }) do
      local p = path.join(directory, ("street.phone-use.%d.%s.png"):format(slot, phase))
      local w, h = ok.image.size(p)
      assert(w == 1920 and h == 1080)
      images[#images + 1] = path.name(p)
    end
  end
  local result = json.object({ { "passed", true }, { "private_actors", list }, { "phone_cases", 5 },
    { "images", images }, { "shared_manifest_sha256", sha(pack_path) } })
  path.write_text(path.join(directory, "phone-use.independent.validation.json"), json.encode(result, { indent = 2 }))
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
