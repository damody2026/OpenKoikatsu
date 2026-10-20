-- Read controller constants for the runtime graph implementation audit.
local ok = require "ok"
local json, path, util = ok.json, ok.path, ok.util

local function unwrap(value)
  if type(value) == "table" and json.is_object(value) then
    local d = value.data
    if d ~= nil then return d end
  end
  return value
end

local function tos_dict(pairs_list)
  local o = json.object()
  for _, kv in ipairs(pairs_list) do o[kv[1]] = kv[2] end
  return o
end

local function main()
  local parser = ok.args.parser()
  parser:arg("bundles", { nargs = "+" })
  parser:opt("--output", { required = true })
  local args = parser:parse(arg)
  local records, kinds = json.array({}), json.object()
  for _, bundle in ipairs(args.bundles) do
    for _, obj in ipairs(ok.kit.unity_objects(bundle, { type = "AnimatorController" })) do
      local tree = obj.tree
      local ctrl = tree.m_Controller
      local states = json.array({})
      local tos = tos_dict(tree.m_TOS)
      for _, machine in ipairs(ctrl.m_StateMachineArray) do
        for _, raw in ipairs(unwrap(machine).m_StateConstantArray) do
          local state = unwrap(raw)
          for _, raw_tree in ipairs(state.m_BlendTreeConstantArray) do
            local motion = unwrap(raw_tree)
            local nodes = {}
            for i, n in ipairs(motion.m_NodeArray) do nodes[i] = unwrap(n) end
            for _, node in ipairs(nodes) do
              if util.truthy(node.m_ChildIndices) then
                local k = util.str(node.m_BlendType)
                kinds[k] = (kinds[k] or 0) + 1
              end
            end
            if #nodes > 1 then
              local name = tos[state.m_FullPathID]
              states[#states + 1] = json.object({ { "name", name == nil and json.null or name }, { "state", state } })
            end
          end
        end
      end
      records[#records + 1] = json.object({ { "bundle", bundle }, { "name", tree.m_Name }, { "path_id", obj.path_id }, { "tos", tos },
        { "layers", ctrl.m_LayerArray }, { "values", ctrl.m_Values }, { "defaults", ctrl.m_DefaultValues }, { "states", states } })
    end
  end
  path.mkdir(path.parent(args.output))
  path.write_text(args.output, json.encode(json.object({ { "blend_types", kinds }, { "controllers", records } }), { indent = 2 }))
  local blended = 0
  for _, r in ipairs(records) do blended = blended + #r.states end
  local parts = {}
  for _, k in ipairs(json.keys(kinds)) do parts[#parts + 1] = util.repr(k) .. ": " .. util.str(kinds[k]) end
  print(("controllers=%d blended_states=%d types={%s}"):format(#records, blended, table.concat(parts, ", ")))
end

ok.main(main)
