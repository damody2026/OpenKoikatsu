-- Audit serialized motion durations without printing complete controller masks.
local ok = require "ok"
local json, path, util = ok.json, ok.path, ok.util

local ROOT = ok.root
local GAME = ok.game_root(true)

local function unwrap(value)
  local d = value.data
  if d == nil then return value end
  return d
end

local function tos_dict(pairs_list)
  local o = json.object()
  for _, kv in ipairs(pairs_list) do o[kv[1]] = kv[2] end
  return o
end

-- Python repr of a {str: int} dict.
local function dict_repr(o)
  local parts = {}
  for _, k in ipairs(json.keys(o)) do parts[#parts + 1] = util.repr(k) .. ": " .. util.str(o[k]) end
  return "{" .. table.concat(parts, ", ") .. "}"
end

local function main()
  local counts = json.object()
  local scaled, sources = json.array({}), json.array({})
  local controllers = 0
  for _, row in ipairs(ok.csv.read_dicts(ROOT .. "/Saved/anim_export/animation_sources.tsv", { delimiter = "\t" })) do
    if row.origin == "base" and math.tointeger(tonumber(row.animator_controllers)) ~= 0 then
      local file = path.join(GAME, row.source)
      sources[#sources + 1] = json.object({ { "source", row.source }, { "sha256", ok.hash.sha256_file(file) } })
      for _, obj in ipairs(ok.kit.unity_objects(file, { type = "AnimatorController" })) do
        controllers = controllers + 1
        local tree = obj.tree
        local tos = tos_dict(tree.m_TOS)
        for _, machine in ipairs(tree.m_Controller.m_StateMachineArray) do
          for _, raw_state in ipairs(unwrap(machine).m_StateConstantArray) do
            local state = unwrap(raw_state)
            for tree_index, raw_motion in ipairs(state.m_BlendTreeConstantArray) do
              for node_index, raw_node in ipairs(unwrap(raw_motion).m_NodeArray) do
                local node = unwrap(raw_node)
                if #node.m_ChildIndices > 0 then
                  local k = util.str(node.m_BlendType)
                  counts[k] = (counts[k] or 0) + 1
                elseif node.m_Duration ~= 1 then
                  local name = tos[state.m_FullPathID]
                  if name == nil then error("KeyError: " .. tostring(state.m_FullPathID), 0) end
                  scaled[#scaled + 1] = json.object({ { "source", row.source }, { "controller", tree.m_Name },
                    { "controller_path_id", obj.path_id }, { "state", name }, { "motion_tree", tree_index - 1 },
                    { "node_index", node_index - 1 }, { "node", node } })
                end
              end
            end
          end
        end
      end
    end
  end
  local output = ROOT .. "/Saved/Tests/MotionTimeScale296/SourceAudit.json"
  path.mkdir(path.parent(output))
  path.write_text(output, json.encode(json.object({ { "controllers", controllers }, { "blend_types", counts },
    { "scaled_leaves", scaled }, { "sources", sources } }), { indent = 2 }))
  print(("controllers=%d scaled_leaves=%d blend_types=%s"):format(controllers, #scaled, dict_repr(counts)))
end

ok.main(main)
