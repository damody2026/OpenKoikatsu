-- Verify actual ray/short GPU body responses against common authored resources.
local ok = require "ok"
local json, path, util = ok.json, ok.path, ok.util
local T = util.truthy
local M = {}

local function read(p) return json.decode(path.read_text(p)) end

function M.verify(folder)
  local root = ok.root
  local report = read(path.join(folder, "social.validation.json"))
  local selection = read(path.join(folder, "selection.json"))
  local runtime = read(path.join(folder, "runtime.json"))
  local resources = read(path.join(root, "Content/IndependentResources/RuntimeAnimations/touch.manifest.json"))
  assert(T(report.passed) and not T(report.errors) and report.touch_reaction_verified == 20)
  assert(T(runtime.gpu_ready) and runtime.gpu_failures == 0 and not T(runtime.cooked_character_loaded) and runtime.complete == 6)
  local actions, count = {}, 0
  for _, a in ipairs(resources.actions) do
    if actions[a.file] == nil then count = count + 1 end
    actions[a.file] = a
  end
  assert(count == 4 and resources.card_specific == false)
  for _, file in ipairs(util.sorted_keys(actions)) do
    local a = actions[file]
    assert(a.loop == false and a.play_seconds == .75)
    assert(ok.hash.file(path.join(root, "Content/IndependentResources/RuntimeAnimations", a.file), "sha256") == a.sha256)
    local admitted
    for _, v in ipairs(selection.actions) do
      if path.name(v.curve) == a.file then admitted = v break end
    end
    assert(admitted, "StopIteration")
    assert(admitted.play_seconds == .75 and not T(admitted.loop))
  end
  local cases, ncases = {}, 0
  local regions = { [0] = "head", [1] = "cheek", [2] = "hand", [4] = "chest" }
  for _, row in ipairs(report.touch_reaction_evidence) do
    local case = tostring(row.slot) .. "," .. tostring(row.region)
    assert(not cases[case]); cases[case] = true; ncases = ncases + 1
    assert(math.type(row.slot) and row.slot >= 1 and row.slot < 6 and row.slot == math.floor(row.slot)
      and (row.region == 0 or row.region == 1 or row.region == 2 or row.region == 4))
    local region = regions[math.tointeger(row.region)]
    assert(region, "KeyError")
    assert(row.clip == "touch_" .. region .. ".okanim" and actions[row.clip] ~= nil)
    assert(row.head_displacement_cm > .1 and T(row.root_preserved) and T(row.repeat_blocked))
    assert(path.is_file(path.join(folder, ("touch-reaction.%s.%s.png"):format(util.num(row.slot), util.num(row.region)))))
  end
  assert(ncases == 20)
  local result = json.object({ { "passed", true }, { "ray_and_gpu_cases", 20 }, { "common_source_clips", 4 },
    { "root_preserved", true }, { "repeat_blocked", true } })
  path.write_text(path.join(folder, "touch-reaction.independent.validation.json"), json.encode(result, { indent = 2 }) .. "\n")
  return result
end

if ok.is_main() then
  ok.main(function()
    local p = ok.args.parser()
    p:arg("folder")
    local a = p:parse(arg)
    print(json.encode(M.verify(path.abs(a.folder))))
  end)
end

return M
