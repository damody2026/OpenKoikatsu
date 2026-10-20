-- Compare a live Unity BakeMesh/transform probe with the Rust session diagnostic.
local ok = require "ok"
local G = require "gpp"
local json, path, util = ok.json, ok.path, ok.util
local O = G.O

local M = {}
local DOC = "Compare a live Unity BakeMesh/transform probe with the Rust session diagnostic."

-- Python 3.12 sum(): exact integer prefix, then Neumaier-compensated floats.
function M.pysum(list)
  local i, n, acc = 1, #list, 0
  while i <= n and math.type(list[i]) == "integer" do acc = acc + list[i]; i = i + 1 end
  if i > n then return acc end
  local f, c = acc + 0.0, 0.0
  for k = i, n do
    local x = list[k]
    if math.type(x) == "integer" then f = f + x
    else
      local t = f + x
      if math.abs(f) >= math.abs(x) then c = c + ((f - t) + x) else c = c + ((x - t) + f) end
      f = t
    end
  end
  if c ~= 0 and c == c and c ~= math.huge and c ~= -math.huge then f = f + c end
  return f
end
local pysum = M.pysum

-- Python float ** 2 calls the C runtime pow(), which differs from d * d in
-- the last bit for ~0.05% of values; squares go through ok-luakit (powf).
local function pow2_batch(values)
  if #values == 0 then return {} end
  return ok.kit.call({ "probe", "linalg", "-" }, { input = json.encode({ ops = { { op = "pow", a = values, b = 2.0 } } }) })[1]
end

-- distances({{a, b}, ...}) -> {math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b))), ...} in one batch
function M.distances(pairs_list)
  local diffs, floats = {}, {}
  for k, ab in ipairs(pairs_list) do
    local a, b, d = ab[1], ab[2], {}
    for i = 1, math.min(#a, #b) do
      local x = a[i] - b[i]
      if math.type(x) == "integer" then d[i] = x * x else floats[#floats + 1] = x; d[i] = -#floats end
    end
    diffs[k] = d
  end
  local sq = pow2_batch(floats)
  local out = {}
  for k, d in ipairs(diffs) do
    local terms = {}
    for i, v in ipairs(d) do
      if math.type(v) == "integer" and v < 0 then terms[i] = sq[-v] else terms[i] = v end
    end
    out[k] = math.sqrt(pysum(terms))
  end
  return out
end
-- math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))
function M.distance(a, b) return M.distances({ { a, b } })[1] end
local distance = M.distance

-- [float(v) for v in text.strip("()").split(",")]
function M.vector(text)
  local out = {}
  for _, v in ipairs(util.split(util.strip(text, "()"), ",")) do out[#out + 1] = util.float(v) end
  return out
end
local vector = M.vector

-- str(Path(p)) on Windows: backslashes, collapsed separators, no "." parts.
local function pystr(p)
  local s = tostring(p):gsub("/", "\\")
  local prefix = s:match("^\\\\") and "\\\\" or ""
  local rest = s:sub(#prefix + 1)
  local lead = rest:match("^%a:\\") or rest:match("^%a:") or rest:match("^\\") or ""
  rest = rest:sub(#lead + 1)
  local parts = {}
  for part in rest:gmatch("[^\\]+") do if part ~= "." then parts[#parts + 1] = part end end
  local out = prefix .. lead .. table.concat(parts, "\\")
  if out == "" then return "." end
  return out
end
M.pystr = pystr

-- read_text(encoding="utf-8"): keeps a BOM (json.loads then rejects it, as in Python).
local function read_utf8(p) return path.read_text(p, { keep_bom = true }) end

local function main()
  local p = ok.args.parser(DOC)
  p:opt("--game", { required = true })
  p:opt("--rust", { required = true })
  p:opt("--variant")
  p:opt("--output")
  local args = p:parse(arg)
  local game = json.decode(read_utf8(args.game))
  local rust = json.decode(read_utf8(args.rust))
  if args.variant then
    local v = rust[args.variant]
    if v == nil then error("KeyError: '" .. args.variant .. "'") end
    rust = v
  end
  local actual = {}
  for i, pt in ipairs(game.vertices) do actual[i] = { pt.x, pt.y, pt.z } end
  local expected = rust.cpu
  if expected == nil then expected = rust.positions end
  if expected == nil or expected == json.null then error("TypeError: object of type 'NoneType' has no len()") end
  if #actual ~= #expected then
    error("ValueError: Different renderer vertex counts; pointwise comparison is invalid")
  end
  local jobs = {}
  for i = 1, #actual do jobs[i] = { actual[i], expected[i] } end
  local errs = util.sorted(M.distances(jobs))
  local total = pysum(errs)
  local n = #errs
  if n == 0 then error("ZeroDivisionError: division by zero") end
  local report = O("game", pystr(args.game), "rust", pystr(args.rust), "variant", args.variant or json.null,
    "vertices", n, "error_m", O("mean", total / n,
      "p95", errs[math.floor((n - 1) * 0.95) + 1], "max", errs[n]))
  local live = {}
  for _, line in ipairs(util.splitlines(path.read_text(pystr(args.game) .. ".bones.tsv"))) do
    if util.startswith(line, "all_transform\t") then
      local r = util.split(line, "\t")
      live[r[2]] = { vector(r[4]), vector(r[5]), vector(r[6]) }
    end
  end
  local skin_names = {}
  for _, name in ipairs(game.skinBones or {}) do skin_names[name:match("([^/]*)$")] = true end
  local pending, bjobs = {}, {}
  local bones = rust.all_bones
  if bones == nil then bones = rust.bones end
  if bones == nil then bones = {} end
  for _, bone in ipairs(bones) do
    local tf = bone.tf
    if tf == nil then tf = bone end
    local name = tf.name
    if live[name] and skin_names[name] then
      local pos, rot, scale = live[name][1], live[name][2], live[name][3]
      local neg = {}
      for i, v in ipairs(tf.rot) do neg[i] = -v end
      pending[#pending + 1] = { name = name, at = #bjobs }
      bjobs[#bjobs + 1] = { pos, tf.pos }
      bjobs[#bjobs + 1] = { scale, tf.scale }
      bjobs[#bjobs + 1] = { rot, tf.rot }
      bjobs[#bjobs + 1] = { rot, neg }
    end
  end
  local dist = M.distances(bjobs)
  local diffs = {}
  for _, b in ipairs(pending) do
    local pe, se = dist[b.at + 1], dist[b.at + 2]
    local qe = math.min(dist[b.at + 3], dist[b.at + 4])
    diffs[#diffs + 1] = O("name", b.name, "position_m", pe, "quaternion", qe, "scale", se)
  end
  report.skin_bone_local_differences = G.A(util.sorted(diffs, function(d)
    return d.position_m + d.quaternion + d.scale end, true))
  local output = json.encode(report, { indent = 2, ensure_ascii = false })
  if args.output then path.write_text(args.output, output .. "\n") end
  local summary = json.object()
  for _, k in ipairs(json.keys(report)) do
    if k ~= "skin_bone_local_differences" then summary[k] = report[k] end
  end
  print(json.encode(summary, { ensure_ascii = false }))
  for i = 1, math.min(10, #report.skin_bone_local_differences) do
    print(util.repr(report.skin_bone_local_differences[i]))
  end
end
M.main = main

if ok.is_main() then ok.main(main) end
return M
