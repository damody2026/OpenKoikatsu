-- Validate all declared stage/group/command/role placements against Info.LoadData.
local ok = require "ok"
local c = require "build_street_social_catalogue"
local json, path, util = ok.json, ok.path, ok.util
local M = {}

function M.verify(catalogue, output)
  local data = json.decode(path.read_text(catalogue))
  local places, errors = c.place_heroine_rows(436)
  assert(#errors == 0)
  local bad = {}
  for _, node in ipairs(data.nodes) do
    local s = node.source
    local found = false
    for _, p in ipairs(places[s.row] or {}) do
      if p.stage == s.stage and p.group == s.group and p.command == s.command and util.contains_value(s.roles, p.role) then found = true break end
    end
    if not found then bad[#bad + 1] = node.id end
  end
  assert(#bad == 0, util.repr(util.slice(bad, 1, 10)))
  local topics = json.decode(path.read_text(path.with_name(catalogue, "topics.json")))
  local touch_rows = 0
  for _, profile in ipairs(topics.profiles) do
    touch_rows = touch_rows + #profile.touch
    for _, touch in ipairs(profile.touch) do
      local group = touch.mode
      local indices = {}
      for row, ps in pairs(places) do
        for _, p in ipairs(ps) do
          if p.stage == touch.stage and p.group == group and p.command == touch.part then indices[#indices + 1] = row break end
        end
      end
      table.sort(indices)
      local pick = indices[touch["repeat"] + 1]
      assert(pick ~= nil, "IndexError: list index out of range")
      assert(touch.source_row == pick)
    end
  end
  local result = json.object({ { "passed", true }, { "dialogue_rows", #data.nodes }, { "touch_rows", touch_rows }, { "layout_mismatches", 0 } })
  path.write_text(output, json.encode(result, { indent = 2 }))
  print(json.encode(result))
end

if ok.is_main() then
  ok.main(function()
    local p = ok.args.parser()
    p:arg("catalogue")
    p:arg("output")
    local a = p:parse(arg)
    M.verify(a.catalogue, a.output)
  end)
end

return M
