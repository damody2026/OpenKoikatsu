-- Independent native CrossFade composition; no Rust sampler is used.
local ok = require "ok"
local add = require "verify_native_additive"
local json, path = ok.json, ok.path
local quat, normalize, err, max2 = add.quat, add.normalize, add.error, add.max2
local M = {}

local ROOT = ok.root
local DATA = ROOT .. "/Saved/Tests/AnimatorCrossFade301"

function M.mix(a, b, w, spherical)
  local prods = {}
  for i = 1, math.min(#a, #b) do prods[i] = a[i] * b[i] end
  local dot = ok.util.pysum(prods)
  if dot < 0 then b = { -b[1], -b[2], -b[3], -b[4] }; dot = -dot end
  if spherical and dot < 0.999999 then
    local theta = math.acos(math.max(-1.0, math.min(1.0, dot)))
    local out = {}
    for i = 1, 4 do out[i] = a[i] * math.sin((1 - w) * theta) / math.sin(theta) + b[i] * math.sin(w * theta) / math.sin(theta) end
    return normalize(out)
  end
  local out = {}
  for i = 1, 4 do out[i] = a[i] * (1 - w) + b[i] * w end
  return normalize(out)
end

local function main()
  local mix = M.mix
  local frames, transforms = 0, 0
  local maximum = { 0.0, 0.0, 0.0 }
  local wrong_slerp, wrong_ease = 0.0, 0.0
  for _, p in ipairs(add.glob(DATA, "*.crossfade.json")) do
    for _, f in ipairs(ok.read_json(p)) do
      local w = f.transitioning and f.transitionNormalized or 1.0
      local expected = f.blend == 0 and 1.0 or math.min(1.0, (f.elapsed - f.start) / (f.fromLength * f.blend))
      assert(math.abs(w - expected) < 2e-5)
      assert(math.abs(f.offset + (f.elapsed - f.start) / f.toLength - f.toNormalized) < 2e-5)
      local source, dest = {}, {}
      for _, b in ipairs(f.fromBones) do source[b.name] = b end
      for _, b in ipairs(f.toBones) do dest[b.name] = b end
      for _, b in ipairs(f.bones) do
        local a, c = source[b.name], dest[b.name]
        for _, ik in ipairs({ { 1, "position" }, { 3, "scale" } }) do
          local i, key = ik[1], ik[2]
          local m
          for _, k in ipairs({ "x", "y", "z" }) do
            local d = math.abs(a[key][k] * (1 - w) + c[key][k] * w - b[key][k])
            if m == nil or d > m then m = d end
          end
          maximum[i] = max2(maximum[i], m)
        end
        local qa, qc, qb = quat(a.rotation), quat(c.rotation), quat(b.rotation)
        maximum[2] = max2(maximum[2], err(mix(qa, qc, w), qb))
        wrong_slerp = max2(wrong_slerp, err(mix(qa, qc, w, true), qb))
        wrong_ease = max2(wrong_ease, err(mix(qa, qc, w * w * (3 - 2 * w)), qb))
        transforms = transforms + 1
      end
      frames = frames + 1
    end
  end
  local result = json.object({ { "frames", frames }, { "transform_snapshots", transforms }, { "maximum_trs_error", maximum },
    { "slerp_error", wrong_slerp }, { "smoothstep_error", wrong_ease } })
  assert(frames > 0 and maximum[1] < 2e-5 and maximum[2] < 2e-5 and maximum[3] < 2e-5, json.encode(result))
  assert(wrong_slerp > 2e-5 and wrong_ease > 2e-5, json.encode(result))
  path.write_text(DATA .. "/NativeCompositionAudit.json", json.encode(result, { indent = 2 }))
  print(json.encode(result))
end

if ok.is_main() then ok.main(main) end
return M
