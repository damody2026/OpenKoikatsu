-- Summarize authored additive layers and their clip bindings without dumping masks.
local ok = require "ok"
local json, path, util = ok.json, ok.path, ok.util

local ROOT = ok.root
local GAME = ok.game_root(true)

local function unwrap(value)
  local d = value.data
  if d == nil then return value end
  return d
end

-- dict(tree['m_TOS']): first occurrence keeps its position, later ones replace the value.
local function tos_dict(pairs_list)
  local o = json.object()
  for _, kv in ipairs(pairs_list) do o[kv[1]] = kv[2] end
  return o
end

local function isfinite(v) return v == v and v ~= math.huge and v ~= -math.huge end

local function main()
  local counts = json.object()
  local function bump(key, by) counts[key] = (counts[key] or 0) + by end
  local layers, sources = json.array({}), json.array({})
  local controllers = 0
  for _, row in ipairs(ok.csv.read_dicts(ROOT .. "/Saved/anim_export/animation_sources.tsv", { delimiter = "\t" })) do
    if row.origin == "base" and math.tointeger(tonumber(row.animator_controllers)) ~= 0 then
      local file = path.join(GAME, row.source)
      sources[#sources + 1] = json.object({ { "source", row.source }, { "sha256", ok.hash.sha256_file(file) } })
      local clips, references = {}, {}
      local clip_objs = ok.kit.unity_objects(file, { type = "AnimationClip",
        fields = { "m_Name", "m_MuscleClip.m_ValueArrayReferencePose", "m_ClipBindingConstant.genericBindings" } })
      for _, clip_obj in ipairs(clip_objs) do
        local clip_tree = clip_obj.tree
        clips[clip_obj.path_id] = clip_tree.m_Name
        local values = clip_tree.m_MuscleClip.m_ValueArrayReferencePose
        local count = 0
        for _, b in ipairs(clip_tree.m_ClipBindingConstant.genericBindings) do
          if b.typeID == 0 or b.typeID == 4 then
            local per = ({ [1] = 3, [2] = 4, [3] = 3, [4] = 3 })[b.attribute] or 1
            count = count + per
          else
            count = count + 1
          end
        end
        local valid = #values == 0
        if not valid and #values == count then
          valid = true
          for _, v in ipairs(values) do if not isfinite(v) then valid = false break end end
        end
        references[clip_obj.path_id] = { { "reference_count", #values }, { "curve_count", count }, { "reference_valid", valid } }
      end
      for _, obj in ipairs(ok.kit.unity_objects(file, { type = "AnimatorController" })) do
        controllers = controllers + 1
        local tree = obj.tree
        local tos = tos_dict(tree.m_TOS)
        local ctrl = tree.m_Controller
        for i0, raw_layer in ipairs(ctrl.m_LayerArray) do
          local i = i0 - 1
          local layer = unwrap(raw_layer)
          local mode = layer["(int&)m_LayerBlendingMode"]
          bump("mode" .. util.str(mode), 1)
          bump("timing_true", layer.m_SyncedLayerAffectsTiming and 1 or 0)
          bump("active_extra", (i > 0 and layer.m_DefaultWeight ~= 0) and 1 or 0)
          if mode == 1 then
            local sm = unwrap(ctrl.m_StateMachineArray[layer.m_StateMachineIndex + 1])
            local states = json.array({})
            for _, raw_state in ipairs(sm.m_StateConstantArray) do
              local state = unwrap(raw_state)
              local nodes = json.array({})
              for _, motion in ipairs(state.m_BlendTreeConstantArray) do
                for _, raw_node in ipairs(unwrap(motion).m_NodeArray) do
                  local node = unwrap(raw_node)
                  local clip_index = node.m_ClipID
                  local pointer = clip_index < #tree.m_AnimationClips and tree.m_AnimationClips[clip_index + 1] or {}
                  local pid = pointer.m_PathID
                  local entry = json.object({ { "clip_index", clip_index }, { "clip", pid ~= nil and clips[pid] or json.null },
                    { "type", node.m_BlendType }, { "children", #node.m_ChildIndices }, { "duration", node.m_Duration } })
                  for _, kv in ipairs(pid ~= nil and references[pid] or {}) do entry[kv[1]] = kv[2] end
                  nodes[#nodes + 1] = entry
                end
              end
              local name = tos[state.m_FullPathID]
              states[#states + 1] = json.object({ { "state", name == nil and json.null or name }, { "speed", state.m_Speed }, { "loop", state.m_Loop }, { "nodes", nodes } })
            end
            local motion_set = layer.m_StateMachineMotionSetIndex
            if motion_set == nil then motion_set = layer.m_StateMachineSynchronizedLayerIndex end
            if motion_set == nil then motion_set = json.null end
            local lname = tos[layer.m_Binding]
            layers[#layers + 1] = json.object({ { "source", row.source }, { "controller", tree.m_Name }, { "layer", i },
              { "name", lname == nil and json.null or lname }, { "weight", layer.m_DefaultWeight },
              { "ik", layer.m_IKPass }, { "timing", layer.m_SyncedLayerAffectsTiming }, { "motion_set", motion_set },
              { "default_state", sm.m_DefaultState }, { "mask_entries", #layer.m_SkeletonMask.data.m_Data }, { "states", states } })
          end
        end
      end
    end
  end
  local out = ROOT .. "/Saved/Tests/AnimatorAdditive297/SourceAudit.json"
  path.mkdir(path.parent(out))
  path.write_text(out, json.encode(json.object({ { "controllers", controllers }, { "counts", counts }, { "layers", layers }, { "sources", sources } }), { indent = 2 }))
  print(json.encode(json.object({ { "controllers", controllers }, { "counts", counts }, { "additive_layers", #layers },
    { "examples", json.array(util.slice(layers, 1, 3)) } }), { ensure_ascii = false }))
end

ok.main(main)
