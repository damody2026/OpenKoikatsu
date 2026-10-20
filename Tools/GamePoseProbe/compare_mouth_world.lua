-- Compare actual native LBS and extractor geometry in authored FaceRoot space.
--
-- The coordinate frame comes from each loaded skeleton, never a fitted affine.
-- Numbers are diagnostic until the full loaded state has been reconciled.
-- numpy's inv/matmul rounding is reproduced by `ok-luakit probe linalg`.
local ok = require "ok"
local G = require "gpp"
local json, util = ok.json, ok.util
local check = G.check

local M = {}
local ROOT = G.ROOT

local function split(line) return util.split(line, "\t") end
local function floats(f, from)
  local out = {}
  for i = from, #f do out[#out + 1] = G.F(f[i]) end
  return out
end
local function mat4(v)
  return { { v[1], v[2], v[3], v[4] }, { v[5], v[6], v[7], v[8] }, { v[9], v[10], v[11], v[12] }, { v[13], v[14], v[15], v[16] } }
end
local function transpose(m)
  local t = {}
  for i = 1, #m[1] do t[i] = {}; for j = 1, #m do t[i][j] = m[j][i] end end
  return t
end
local function trunc(x) if x >= 0 then return math.floor(x) else return math.ceil(x) end end
local function norm3(a, b)
  local d0, d1, d2 = a[1] - b[1], a[2] - b[2], a[3] - b[3]
  return math.sqrt(d0 * d0 + d1 * d1 + d2 * d2)
end
-- numpy.linalg via ok-luakit (one process for all operations).
local function linalg(ops) return ok.kit.call({ "probe", "linalg", "-" }, { input = json.encode({ ops = ops }) }) end
local function median(values)
  local s = {}
  for i, v in ipairs(values) do s[i] = v end
  table.sort(s)
  local n = #s
  if n % 2 == 1 then return s[(n + 1) // 2] end
  return (s[n // 2] + s[n // 2 + 1]) / 2
end

function M.compare(verify, verify_all)
  local native = ROOT .. "/Saved/Tests/MouthWorld277/GameMouthWorldMarch.json"
  check(json.decode(ok.path.read_text(native))["error"] == "")
  local names, cases = {}, {}
  for _, line in ipairs(util.splitlines(G.read(native .. ".mouth-shapes.tsv"))) do
    local f = split(line)
    if f[1] == "target" then names[G.I(f[2])] = f[3] end
    if f[1] == "case" then
      local v = G.F(f[4]) * 100
      if v < 0 then v = 0.0 elseif v > 100 then v = 100.0 end
      cases[G.I(f[2])] = { G.I(f[3]), math.tointeger(trunc(v)) }
    end
  end
  -- game_vertices: ordered dict keyed by (case, name)
  local game_keys, game_vertices = {}, {}
  local game_bones, bone_order = {}, {}
  local world_rows = util.splitlines(G.read(native .. ".mouth-world.tsv"))
  check(world_rows[#world_rows] == "cases\t10")
  for _, line in ipairs(world_rows) do
    local f = split(line)
    if f[1] == "world_vertex" then
      local case, name = G.I(f[2]), names[G.I(f[3])]
      if name == nil then error("KeyError: " .. f[3]) end
      local k = case .. "\0" .. name
      if not game_vertices[k] then game_vertices[k] = {}; game_keys[#game_keys + 1] = { case, name, k } end
      local t = game_vertices[k]
      t[#t + 1] = floats(f, 5)
    end
    if f[1] == "bone_world" and f[2] == "0" then
      local matrix = mat4(floats(f, 6))
      local prev = game_bones[f[5]]
      if prev then
        local worst = 0
        for i = 1, 4 do for j = 1, 4 do worst = math.max(worst, math.abs(prev[i][j] - matrix[i][j])) end end
        check(worst < 1e-6)
      else
        bone_order[#bone_order + 1] = f[5]
      end
      game_bones[f[5]] = matrix
    end
  end
  local rust_vertices = {}
  local rust_bones = {}
  for _, line in ipairs(util.splitlines(ok.path.read_text(ROOT .. "/Saved/Tests/MouthShapes276/RustMouthMarch.tsv"))) do
    local f = split(line)
    if f[1] == "vertex" then
      local k = f[2] .. "\0" .. G.I(f[3]) .. "\0" .. G.I(f[4])
      rust_vertices[k] = rust_vertices[k] or {}
      table.insert(rust_vertices[k], floats(f, 6))
    end
    if f[1] == "bone_world" then rust_bones[f[2]] = mat4(floats(f, 3)) end
  end
  local anchor = "cf_J_FaceRoot"
  if not game_bones[anchor] or not rust_bones[anchor] then error("KeyError: " .. anchor) end
  local frames = linalg({ { op = "inv", a = game_bones[anchor] }, { op = "inv", a = rust_bones[anchor] } })
  local game_frame, rust_frame = frames[1], frames[2]

  -- Collect every points() / matmul request, run them in one batch.
  local ops, jobs = {}, {}
  local function with_one(vertices)
    local out = {}
    for i, v in ipairs(vertices) do out[i] = { v[1], v[2], v[3], 1.0 } end
    return out
  end
  for _, gk in ipairs(game_keys) do
    local case, name = gk[1], gk[2]
    local pc = cases[case]
    if pc == nil then error("KeyError: " .. case) end
    local rk = name .. "\0" .. pc[1] .. "\0" .. pc[2]
    if rust_vertices[rk] then
      jobs[#jobs + 1] = { gk = gk, rk = rk, pattern = pc[1], percent = pc[2], gi = #ops + 1, ri = #ops + 2 }
      ops[#ops + 1] = { op = "matmul", a = with_one(game_vertices[gk[3]]), b = transpose(game_frame) }
      ops[#ops + 1] = { op = "matmul", a = with_one(rust_vertices[rk]), b = transpose(rust_frame) }
    end
  end
  local common = {}
  for _, name in ipairs(bone_order) do
    if rust_bones[name] then
      common[#common + 1] = { name = name, gi = #ops + 1, ri = #ops + 2 }
      ops[#ops + 1] = { op = "matmul", a = game_frame, b = game_bones[name] }
      ops[#ops + 1] = { op = "matmul", a = rust_frame, b = rust_bones[name] }
    end
  end
  local results = #ops > 0 and linalg(ops) or {}

  local report = G.O("coordinate_frame", anchor, "geometry", json.object(), "bone_max_component_errors", json.object())
  for _, job in ipairs(jobs) do
    local name = job.gk[2]
    local game, rust = results[job.gi], results[job.ri]
    check(#game == #rust)
    local e = {}
    for i = 1, #game do e[i] = norm3(game[i], rust[i]) end
    local function raw(case)
      local v = game_vertices[case .. "\0" .. name]
      if v == nil then error("KeyError: (" .. case .. ", " .. name .. ")") end
      return v
    end
    local g3, g0, g8, g5 = raw(3), raw(0), raw(8), raw(5)
    local emax, arg, mouth_n, mouth_max = nil, nil, 0, nil
    for i = 1, #e do
      if emax == nil or e[i] > emax then emax, arg = e[i], i end
      if norm3(g3[i], g0[i]) > 1e-6 or norm3(g8[i], g5[i]) > 1e-6 then
        mouth_n = mouth_n + 1
        if mouth_max == nil or e[i] > mouth_max then mouth_max = e[i] end
      end
    end
    if emax == nil or mouth_max == nil then error("ValueError: zero-size array to reduction operation maximum which has no identity") end
    local function first3(r) return { r[1], r[2], r[3] } end
    report.geometry[name .. ":" .. job.pattern .. ":" .. job.percent] = G.O(
      "max", emax, "median", median(e),
      "worst_vertex", arg - 1, "worst_native", first3(game[arg]),
      "worst_extractor", first3(rust[arg]),
      "mouth_vertices", mouth_n, "mouth_max", mouth_max)
  end
  local bones = {}
  for _, c in ipairs(common) do
    local a, b = results[c.gi], results[c.ri]
    local worst = 0.0
    for i = 1, 4 do for j = 1, 4 do worst = math.max(worst, math.abs(a[i][j] - b[i][j])) end end
    bones[#bones + 1] = { c.name, worst }
  end
  -- Python iterates a set intersection here (hash order); ties keep game order.
  local sorted = util.sorted(bones, function(item) return item[2] end, true)
  local errs = json.object()
  for _, item in ipairs(sorted) do errs[item[1]] = item[2] end
  report.bone_max_component_errors = errs
  if verify or verify_all then
    check(G.set_len(report.geometry) == 40, "all five targets, both patterns and four distinct sampled rates")
    check(G.max(util.values(errs)) < 1e-6)
    check(G.all(util.values(report.geometry), function(item) return item.mouth_vertices > 0 and item.mouth_max < 2e-5 end),
      "native mouth support vertices must match")
    for _, k in ipairs(json.keys(report.geometry)) do
      local item = report.geometry[k]
      if util.startswith(k, "cf_O_tooth:") or util.startswith(k, "cf_O_canine:") or util.startswith(k, "o_tang:") then
        check(item.max < 2e-5, "every native teeth/tongue vertex must match")
      end
    end
  end
  if verify_all then
    check(G.all(util.values(report.geometry), function(item) return item.max < 2e-5 end),
      "all native face vertices, including eyes, must match")
  end
  local out = ROOT .. "/Saved/Logs/MouthWorld277Comparison.json"
  local text = json.encode(report, { indent = 2 })
  G.write_text(out, text)
  print(text)
end

if ok.is_main() then
  ok.main(function() M.compare(ok.args.has(arg, "--verify"), ok.args.has(arg, "--verify-all")) end)
end
return M
