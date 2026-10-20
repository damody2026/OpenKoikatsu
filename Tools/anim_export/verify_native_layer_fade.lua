-- Independent native isolated-layer CrossFade then layer composition.
local ok = require "ok"
local add = require "verify_native_additive"
local mix = require("verify_native_crossfade").mix
local json, path, util = ok.json, ok.path, ok.util
local quat, multiply, normalize, err, weighted, max2 = add.quat, add.multiply, add.normalize, add.error, add.weighted, add.max2

local ROOT = ok.root
local DATA = ROOT .. "/Saved/Tests/AnimatorLayerFade302"
local XYZ = { "x", "y", "z" }

local function by_name(list) local t = {}; for _, b in ipairs(list) do t[b.name] = b end; return t end
local function vec(t) return { t.x, t.y, t.z } end
local function maxdiff(a, t)
  local m
  for i, k in ipairs(XYZ) do local d = math.abs(a[i] - t[k]); if m == nil or d > m then m = d end end
  return m
end

local function main()
  local controller = add.controller(ok.game_root(true) .. "/abdata/h/anim/female/05_01_00.unity3d", "khr_f_02_00")
  local layers = controller.m_Controller.m_LayerArray
  local modes, masks = {}, {}
  for i = 0, #layers - 1 do
    modes[i] = layers[i + 1].data["(int&)m_LayerBlendingMode"]
    masks[i] = add.layer_mask(layers, i)
  end
  local maximum, isolated_max = { 0.0, 0.0, 0.0 }, { 0.0, 0.0, 0.0 }
  local wrong_final, frames, transforms = 0.0, 0, 0
  for _, file in ipairs(add.glob(DATA, "*.layer-fade.json")) do
    for _, f in ipairs(ok.read_json(file)) do
      local alpha = f.transitionNormalized
      assert(math.abs(alpha - math.min(1.0, (f.elapsed - f.start) / (f.sourceLength * f.blend))) < 2e-5)
      assert(math.abs(f.toNormalized - f.offset - (f.elapsed - f.start) / f.sourceLength) < 2e-5)
      assert(math.abs(f.baseNormalizedTime - f.elapsed / f.baseLength) < 2e-5)
      local base = by_name(f.baseBones)
      local snapshots = {}
      for _, l in ipairs(f.layers) do snapshots[l.layer] = by_name(l.bones) end
      local source, dest = by_name(f.fromBones), by_name(f.toBones)
      local final_source, final_dest = by_name(f.finalFromBones), by_name(f.finalToBones)
      for _, target in ipairs(f.bones) do
        local name = target.name
        local b = base[name]
        local p, s, q = vec(b.position), vec(b.scale), quat(b.rotation)
        for _, l in ipairs(util.sorted(f.layers, function(l) return l.layer end)) do
          local v = snapshots[l.layer][name]
          local vp, vs, vq = vec(v.position), vec(v.scale), quat(v.rotation)
          if l.layer == f.layer then
            local a, c = source[name], dest[name]
            vp, vs = {}, {}
            for i, k in ipairs(XYZ) do
              vp[i] = a.position[k] * (1 - alpha) + c.position[k] * alpha
              vs[i] = a.scale[k] * (1 - alpha) + c.scale[k] * alpha
            end
            vq = mix(quat(a.rotation), quat(c.rotation), alpha)
            isolated_max[1] = max2(isolated_max[1], maxdiff(vp, v.position))
            isolated_max[2] = max2(isolated_max[2], err(vq, quat(v.rotation)))
            isolated_max[3] = max2(isolated_max[3], maxdiff(vs, v.scale))
          end
          local m = add.mask_weight(masks[l.layer], target.path)
          local w = m * l.weight
          if w ~= 0 then
            if modes[l.layer] == 1 then
              for i, k in ipairs(XYZ) do
                p[i] = p[i] + (vp[i] - b.position[k]) * w
                s[i] = s[i] * (1 + (vs[i] / b.scale[k] - 1) * w)
              end
              local ref = quat(b.rotation)
              local delta = normalize(multiply({ -ref[1], -ref[2], -ref[3], ref[4] }, vq))
              q = multiply(q, weighted(delta, w, "source"))
            else
              for i = 1, 3 do p[i] = p[i] * (1 - w) + vp[i] * w; s[i] = s[i] * (1 - w) + vs[i] * w end
              q = mix(q, vq, w)
            end
          end
        end
        maximum[1] = max2(maximum[1], maxdiff(p, target.position))
        maximum[2] = max2(maximum[2], err(q, quat(target.rotation)))
        maximum[3] = max2(maximum[3], maxdiff(s, target.scale))
        wrong_final = max2(wrong_final, err(mix(quat(final_source[name].rotation), quat(final_dest[name].rotation), alpha), quat(target.rotation)))
        transforms = transforms + 1
      end
      frames = frames + 1
    end
  end
  local result = json.object({ { "frames", frames }, { "transform_snapshots", transforms }, { "maximum_trs_error", maximum },
    { "isolated_transition_error", isolated_max }, { "final_pose_mix_error", wrong_final }, { "layer_modes", add.modes_object(modes, { 7, 8, 70, 73 }) } })
  local all_small = true
  for _, v in ipairs(util.extend(util.slice(maximum), isolated_max)) do if not (v < 2e-5) then all_small = false end end
  assert(frames > 0 and all_small, json.encode(result))
  assert(wrong_final > 2e-5, json.encode(result))
  path.write_text(DATA .. "/NativeCompositionAudit.json", json.encode(result, { indent = 2 }))
  print(json.encode(result))
end

ok.main(main)
