-- Bounded 2D controller audit; preserve full constants to disk, print only summaries.
local ok = require "ok"
local json, path, util = ok.json, ok.path, ok.util

local ROOT = ok.root

local function unwrap(value)
  if type(value) == "table" and json.is_object(value) then
    local d = value.data
    if d ~= nil then return d end
  end
  return value
end

-- Python repr() of decoded typetree values (floats print inf/nan like Python).
local function pyrepr(v)
  if v == nil or v == json.null then return "None" end
  local t = type(v)
  if t == "number" then
    if math.type(v) == "float" then
      if v ~= v then return "nan" end
      if v == math.huge then return "inf" end
      if v == -math.huge then return "-inf" end
    end
    return util.num(v)
  end
  if t == "boolean" or t == "string" then return util.repr(v) end
  if json.is_object(v) then
    local parts = {}
    for _, k in ipairs(json.keys(v)) do parts[#parts + 1] = pyrepr(k) .. ": " .. pyrepr(v[k]) end
    return "{" .. table.concat(parts, ", ") .. "}"
  end
  local parts = {}
  for i = 1, #v do parts[i] = pyrepr(v[i]) end
  return "[" .. table.concat(parts, ", ") .. "]"
end

local function tos_dict(pairs_list)
  local o = json.object()
  for _, kv in ipairs(pairs_list) do o[kv[1]] = kv[2] end
  return o
end

local WANTED = { khs3_o_base = true, khh_f_base = true, khh3_f_base = true, khs3_f_base3 = true }

local function main()
  local records = json.array({})
  for _, obj in ipairs(ok.kit.unity_objects(ok.game_root(true) .. "/abdata/studio/anime/00.unity3d", { type = "AnimatorController" })) do
    local tree = obj.tree
    if WANTED[tree.m_Name] then
      local ctrl = tree.m_Controller
      local tos = tos_dict(tree.m_TOS)
      records[#records + 1] = json.object({ { "name", tree.m_Name }, { "path_id", obj.path_id }, { "tos", tos }, { "controller", ctrl } })
      print("controller " .. tree.m_Name)
      for layer_index0, raw_layer in ipairs(ctrl.m_LayerArray) do
        local layer_index = layer_index0 - 1
        local layer = unwrap(raw_layer)
        local machine = unwrap(ctrl.m_StateMachineArray[layer.m_StateMachineIndex + 1])
        local states = {}
        for i, s in ipairs(machine.m_StateConstantArray) do states[i] = unwrap(s) end
        local kinds = {}
        for _, state in ipairs(states) do
          for _, raw_motion in ipairs(state.m_BlendTreeConstantArray) do
            for _, n in ipairs(unwrap(raw_motion).m_NodeArray) do
              if #unwrap(n).m_ChildIndices > 0 then kinds[#kinds + 1] = unwrap(n).m_BlendType end
            end
          end
        end
        local has3 = false
        for _, k in ipairs(kinds) do if k == 3 then has3 = true end end
        if layer_index == 0 or has3 then
          local mask = unwrap(layer.m_SkeletonMask).m_Data
          local enabled = 0
          for _, v in ipairs(mask) do if v.m_Weight > 0 then enabled = enabled + 1 end end
          local names = {}
          for i, s in ipairs(states) do local nm = tos[s.m_FullPathID]; names[i] = nm == nil and json.null or nm end
          local kind_set = util.sorted(util.unique(kinds))
          local binding = tos[layer.m_Binding]
          print(table.concat({ " layer", util.str(layer_index), binding == nil and "None" or binding, "mode", pyrepr(layer["(int&)m_LayerBlendingMode"]),
            "weight", pyrepr(layer.m_DefaultWeight), "default", pyrepr(machine.m_DefaultState), "states", pyrepr(names),
            "mask", util.str(#mask), "enabled", util.str(enabled), "kinds", pyrepr(kind_set) }, " "))
          if layer_index == 0 then
            local base = states[machine.m_DefaultState + 1]
            local copy = json.object()
            for _, k in ipairs(json.keys(base)) do if k ~= "m_BlendTreeConstantArray" then copy[k] = base[k] end end
            print(" base default " .. pyrepr(copy))
          end
        end
      end
    end
  end
  local output = ROOT .. "/Saved/Tests/Animator290/Controllers.json"
  path.mkdir(path.parent(output))
  path.write_text(output, json.encode(records, { indent = 2 }))
  print("saved " .. output:gsub("/", "\\"))
end

ok.main(main)
