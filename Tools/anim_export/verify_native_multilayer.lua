-- Independent reconstruction from native base and isolated layer poses.
local ok = require "ok"
local add = require "verify_native_additive"
local json, path, util = ok.json, ok.path, ok.util
local quat, multiply, normalize, err, weighted, max2 = add.quat, add.multiply, add.normalize, add.error, add.weighted, add.max2

local ROOT = ok.root
local DATA = ROOT .. "/Saved/Tests/AnimatorMultiLayer298"
local XYZ = { "x", "y", "z" }

local function main()
  local controller = add.controller(ok.game_root(true) .. "/abdata/h/anim/female/05_01_00.unity3d", "khr_f_02_00")
  local layers = controller.m_Controller.m_LayerArray
  local modes, masks = {}, {}
  for i = 0, #layers - 1 do
    modes[i] = layers[i + 1].data["(int&)m_LayerBlendingMode"]
    masks[i] = add.layer_mask(layers, i)
  end
  for _, i in ipairs({ 7, 8, 70, 73, 74 }) do
    for _, w in pairs(masks[i]) do assert(w == 0 or w == 1) end
  end
  local maximum = { 0.0, 0.0, 0.0 }
  local reverse, frames, transforms = 0.0, 0, 0
  for _, file in ipairs(add.glob(DATA, "*.json.multilayer.json")) do
    for _, f in ipairs(ok.read_json(file)) do
      local base, snapshots = {}, {}
      for _, b in ipairs(f.baseBones) do base[b.name] = b end
      for _, l in ipairs(f.layers) do
        local t = {}
        for _, b in ipairs(l.bones) do t[b.name] = b end
        snapshots[l.layer] = t
      end
      for _, target in ipairs(f.bones) do
        local b = base[target.name]
        local function compose(order)
          local p = { b.position.x, b.position.y, b.position.z }
          local s = { b.scale.x, b.scale.y, b.scale.z }
          local q = quat(b.rotation)
          for _, l in ipairs(order) do
            local v = snapshots[l.layer][target.name]
            local m = add.mask_weight(masks[l.layer], target.path)
            local w = m * l.weight
            if w ~= 0 then
              if modes[l.layer] == 1 then
                for i, k in ipairs(XYZ) do
                  p[i] = p[i] + (v.position[k] - b.position[k]) * w
                  s[i] = s[i] * (1 + (v.scale[k] / b.scale[k] - 1) * w)
                end
                local ref = quat(b.rotation)
                local delta = normalize(multiply({ -ref[1], -ref[2], -ref[3], ref[4] }, quat(v.rotation)))
                q = multiply(q, weighted(delta, w, "source"))
              else
                for i, k in ipairs(XYZ) do
                  p[i] = p[i] * (1 - w) + v.position[k] * w
                  s[i] = s[i] * (1 - w) + v.scale[k] * w
                end
                local vq = quat(v.rotation)
                local prods = {}
                for i = 1, math.min(#q, #vq) do prods[i] = q[i] * vq[i] end
                local dot = util.pysum(prods)
                local sign = dot < 0 and -1 or 1
                local mixed = {}
                for i = 1, 4 do mixed[i] = q[i] * (1 - w) + vq[i] * w * sign end
                q = normalize(mixed)
              end
            end
          end
          return p, q, s
        end
        local function maxdiff(vec, key)
          local m
          for i, k in ipairs(XYZ) do
            local d = math.abs(vec[i] - target[key][k])
            if m == nil or d > m then m = d end
          end
          return m
        end
        local rp, rq, rs = compose(util.sorted(f.layers, function(l) return l.layer end))
        maximum[1] = max2(maximum[1], maxdiff(rp, "position"))
        maximum[2] = max2(maximum[2], err(rq, quat(target.rotation)))
        maximum[3] = max2(maximum[3], maxdiff(rs, "scale"))
        local wp, wq = compose(util.sorted(f.layers, function(l) return -l.layer end))
        reverse = max2(max2(reverse, err(wq, quat(target.rotation))), maxdiff(wp, "position"))
        transforms = transforms + 1
      end
      frames = frames + 1
    end
  end
  assert(frames > 0 and maximum[1] < 2e-5 and maximum[2] < 2e-5 and maximum[3] < 2e-5, util.repr(maximum))
  assert(reverse > 2e-5, util.repr(reverse))
  local result = json.object({ { "frames", frames }, { "transform_snapshots", transforms }, { "maximum_trs_error", maximum },
    { "reverse_order_error", reverse }, { "layer_modes", add.modes_object(modes, { 7, 8, 70, 73, 74 }) } })
  path.write_text(DATA .. "/NativeCompositionAudit.json", json.encode(result, { indent = 2 }))
  print(json.encode(result))
end

ok.main(main)
