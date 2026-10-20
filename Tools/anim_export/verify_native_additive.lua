-- Independent native snapshot composition and wrong-algorithm controls.
-- Module use (sibling verifiers): quat, multiply, normalize, error, weighted,
-- controller(bundle, name), layer_mask(masks_of_layer, path), crc32.
local ok = require "ok"
local json, path = ok.json, ok.path
local M = {}

local ROOT = ok.root
local DATA = ROOT .. "/Saved/Tests/AnimatorAdditive297"
M.BUNDLE = ok.game_root(true) .. "/abdata/h/anim/female/05_01_00.unity3d"

function M.quat(value) return { value.x, value.y, value.z, value.w } end

function M.multiply(a, b)
  local x, y, z, w = a[1], a[2], a[3], a[4]
  local X, Y, Z, W = b[1], b[2], b[3], b[4]
  return { w * X + x * W + y * Z - z * Y, w * Y - x * Z + y * W + z * X, w * Z + x * Y - y * X + z * W, w * W - x * X - y * Y - z * Z }
end

function M.normalize(q)
  local sq = {}
  for i, v in ipairs(q) do sq[i] = v * v end
  local n = math.sqrt(ok.util.pysum(sq))
  if n > 1e-8 then
    local out = {}
    for i, v in ipairs(q) do out[i] = v / n end
    return out
  end
  return { 0, 0, 0, 1 }
end

function M.error(a, b)
  local d, e
  for i = 1, math.min(#a, #b) do
    local x, y = math.abs(a[i] - b[i]), math.abs(a[i] + b[i])
    if d == nil or x > d then d = x end
    if e == nil or y > e then e = y end
  end
  return math.min(d, e)
end

function M.weighted(delta, w, mode)
  if mode == "source" then
    return M.normalize({ delta[1] * w, delta[2] * w, delta[3] * w, delta[4] })
  end
  if delta[4] < 0 then delta = { -delta[1], -delta[2], -delta[3], -delta[4] } end
  if mode == "nlerp" then
    return M.normalize({ delta[1] * w, delta[2] * w, delta[3] * w, 1 + (delta[4] - 1) * w })
  end
  local theta = math.acos(math.max(-1, math.min(1, M.normalize(delta)[4])))
  local sine = math.sin(theta)
  local f = math.abs(sine) > 1e-6 and math.sin(theta * w) / sine or w
  return { delta[1] * f, delta[2] * f, delta[3] * f, math.cos(theta * w) }
end

-- zlib.crc32(s.encode()) with a cache (the same bone paths repeat every frame).
local crc_cache = {}
function M.crc32(s)
  local v = crc_cache[s]
  if v == nil then v = ok.hash.crc32(s); crc_cache[s] = v end
  return v
end

-- Python max() over a list (first maximum wins).
function M.max(list)
  local best = list[1]
  for i = 2, #list do if list[i] > best then best = list[i] end end
  return best
end
function M.max2(a, b) if b > a then return b end return a end

-- Mask weight of a bone path: m = mask.get(0, 1.); then each path prefix hash overrides.
function M.mask_weight(mask, bone_path)
  local tail = bone_path
  local marker = "/p_cf_body_bone/"
  local last
  local i = 1
  while true do
    local a, b = tail:find(marker, i, true)
    if not a then break end
    last = b; i = a + 1
  end
  if last then tail = tail:sub(last + 1) end
  local parts = ok.util.split(tail, "/")
  local m = mask[0]
  if m == nil then m = 1.0 end
  local prefix
  for n = 1, #parts do
    prefix = n == 1 and parts[1] or (prefix .. "/" .. parts[n])
    local v = mask[M.crc32(prefix)]
    if v ~= nil then m = v end
  end
  return m
end

-- AnimatorController typetrees named `name` in a bundle (UnityPy object order).
function M.controllers(bundle, name)
  local out = {}
  for _, o in ipairs(ok.kit.unity_objects(bundle, { type = "AnimatorController" })) do
    if o.tree.m_Name == name then out[#out + 1] = o.tree end
  end
  return out
end
function M.controller(bundle, name)
  local c = M.controllers(bundle, name)[1]
  if not c then error("StopIteration", 0) end
  return c
end

-- {m_PathHash: m_Weight} of layer index i (0-based).
function M.layer_mask(layers, i)
  local t = {}
  for _, v in ipairs(layers[i + 1].data.m_SkeletonMask.data.m_Data) do t[v.m_PathHash] = v.m_Weight end
  return t
end

-- Layer modes {i: mode} for i in a list.
function M.modes_object(modes, list)
  local o = json.object()
  for _, i in ipairs(list) do o[i] = modes[i] end
  return o
end

-- sorted(DATA.glob(pattern)) with Windows path ordering.
function M.glob(dir, pattern)
  local files = path.glob(dir, pattern)
  table.sort(files, function(a, b) return a:lower() < b:lower() end)
  return files
end

local function main()
  local masks
  for _, tree in ipairs(M.controllers(M.BUNDLE, "khr_f_02_00")) do
    local layers = tree.m_Controller.m_LayerArray
    masks = {}
    for _, i in ipairs({ 7, 8, 70 }) do masks[i] = M.layer_mask(layers, i) end
  end
  assert(masks ~= nil)
  local maximum = { 0.0, 0.0, 0.0 }
  local wrong = json.object({ { "slerp", 0.0 }, { "nlerp", 0.0 }, { "absolute_override", 0.0 } })
  local frames, transforms = 0, 0
  local quat, multiply, normalize, err, weighted, max2 = M.quat, M.multiply, M.normalize, M.error, M.weighted, M.max2
  for _, file in ipairs(M.glob(DATA, "*.json.additive.json")) do
    local data = ok.read_json(file)
    assert(#data == 36)
    for _, frame in ipairs(data) do
      local base, ref, raw = {}, {}, {}
      for _, b in ipairs(frame.baseBones) do base[b.name] = b end
      for _, b in ipairs(frame.referenceBones) do ref[b.name] = b end
      for _, b in ipairs(frame.rawBones) do raw[b.name] = b end
      local mask = masks[frame.layer]
      for _, target in ipairs(frame.bones) do
        local name = target.name
        local b, r, v = base[name], ref[name], raw[name]
        local m = M.mask_weight(mask, target.path)
        local w = m * frame.weight
        for _, k in ipairs({ "x", "y", "z" }) do
          local p = b.position[k] + (v.position[k] - r.position[k]) * w
          local s
          if r.scale[k] ~= 0 then s = b.scale[k] * (1 + (v.scale[k] / r.scale[k] - 1) * w) else s = b.scale[k] end
          maximum[1] = max2(maximum[1], math.abs(p - target.position[k]))
          maximum[3] = max2(maximum[3], math.abs(s - target.scale[k]))
          local old = b.position[k] * (1 - w) + v.position[k] * w
          wrong.absolute_override = max2(wrong.absolute_override, math.abs(old - target.position[k]))
        end
        local q = quat(r.rotation)
        local inverse = { -q[1], -q[2], -q[3], q[4] }
        local delta = normalize(multiply(inverse, quat(v.rotation)))
        for _, mode in ipairs({ "source", "slerp", "nlerp" }) do
          local out = multiply(quat(b.rotation), weighted(delta, w, mode))
          local e = err(quat(target.rotation), out)
          if mode == "source" then maximum[2] = max2(maximum[2], e) else wrong[mode] = max2(wrong[mode], e) end
        end
        transforms = transforms + 1
      end
      frames = frames + 1
    end
  end
  assert(frames == 180 and maximum[1] < 2e-5 and maximum[2] < 2e-5 and maximum[3] < 2e-5)
  assert(wrong.slerp > 1e-4 and wrong.nlerp > 1e-4 and wrong.absolute_override > 1e-3)
  local result = json.object({ { "frames", frames }, { "native_transform_snapshots", transforms }, { "maximum_trs_error", maximum },
    { "wrong_algorithm_maximum", wrong } })
  path.write_text(DATA .. "/NativeCompositionAudit.json", json.encode(result, { indent = 2 }))
  print(json.encode(result))
end

if ok.is_main() then ok.main(main) end
return M
