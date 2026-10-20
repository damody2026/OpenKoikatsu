-- Check Studio footwear visibility against native hierarchy and capture inputs.
local ok = require "ok"
local G = require "gpp"
local json, path, util, re = ok.json, ok.path, ok.util, ok.re
local O, A, check = G.O, G.A, G.check

local M = {}
local DOC = "Check Studio footwear visibility against native hierarchy and capture inputs."

local function digest(p) return G.digest(p) end
M.digest = digest
local function resolved(p) return G.winpath(path.abs(p)) end
local function has(s, sub) return s:find(sub, 1, true) ~= nil end
local function truthy(v)
  if v == nil or v == json.null or v == false or v == 0 or v == "" then return false end
  if type(v) == "table" then return next(v) ~= nil end
  return true
end

-- str.rfind(sub, start, end) with Python slice semantics; returns a 0-based index or -1.
function M.rfind(s, sub, start, stop)
  local n = #s
  start = start or 0; stop = stop or n
  if start < 0 then start = math.max(0, n + start) end
  if stop < 0 then stop = math.max(0, n + stop) end
  if stop > n then stop = n end
  local best = -1
  local i = start + 1
  while true do
    local a, b = s:find(sub, i, true)
    if not a or b > stop then break end
    best = a - 1; i = a + 1
  end
  return best
end
local rfind = M.rfind

-- Path.glob order on NTFS: case-insensitive name order.
local function ntfs_sorted(list)
  return util.sorted(list, function(p) return path.name(p):upper() end)
end

local function main()
  local p = ok.args.parser(DOC)
  for _, name in ipairs({ "native", "dump", "log", "output" }) do p:arg(name) end
  p:opt("--card", { required = true })
  local args = p:parse(arg)
  local native = G.read_json(args.native)
  local footwear = G.filter(native, function(r) return has(r.path, "/ct_shoes_inner/") or has(r.path, "/ct_shoes_outer/") end)
  local function on(r) return truthy(r.active) and truthy(r.enabled) end
  check(#footwear > 0 and G.any(footwear, on))
  local active = G.filter(footwear, on)
  check(G.all(active, function(r) return has(r.path, "/ct_shoes_outer/") end), "Fixture is not Studio default outdoor mode")
  check(G.all(G.filter(footwear, function(r) return has(r.path, "/ct_shoes_inner/") end), function(r) return not truthy(r.active) end))
  local dump_rows = {}
  local files = json.object()
  files[resolved(args.native)] = digest(args.native)
  files[resolved(args.log)] = digest(args.log)
  for _, mp in ipairs(ntfs_sorted(path.glob(args.dump, "mesh_*.json"))) do
    if not util.endswith(path.name(mp), ".materials.json") then
      local row = G.read_json(mp)
      local source = row.sourcePart
      if not truthy(source) then source = {} end
      if source.field == "clothes" and (source.slot == 7 or source.slot == 8) then
        files[resolved(mp)] = digest(mp)
        dump_rows[#dump_rows + 1] = { mp, row }
      end
    end
  end
  check(#dump_rows > 0)
  local twins = {}
  local fields = { "vertices", "normals", "uv", "triangles", "bindposes", "boneWeights" }
  for _, a in ipairs(dump_rows) do
    for _, b in ipairs(dump_rows) do
      local ra, rb = a[2], b[2]
      if ra.sourcePart.slot == 7 and rb.sourcePart.slot == 8 then
        if G.all(fields, function(key) return util.equal(ra.mesh[key], rb.mesh[key]) end) then
          twins[#twins + 1] = O("indoor", path.name(a[1]), "outdoor", path.name(b[1]),
            "vertex_count", ra.mesh.vertexCount, "identical_fields", A({ table.unpack(fields) }))
        end
      end
    end
  end
  local log = G.read(args.log)
  if not utf8.len(log) then error("UnicodeDecodeError: 'utf-8' codec can't decode " .. G.winpath(args.log)) end
  local marker = "card=" .. path.stem(args.card) .. " variant="
  local found = log:find(marker, 1, true)
  if not found then error("ValueError: substring not found") end
  local stop = found - 1
  local start = math.max(0, rfind(log, "ok.ShotPreview task ", 0, rfind(log, "ok.ShotPreview task ", 0, stop)))
  start = rfind(log, "OK material input identity part=0 role=body ", start, stop)
  check(start >= 0, "Missing actual load input block")
  local inputs = re.findall([[OK material input identity part=(\d+) role=clothes_shoes name=(.*?) png_crc=([0-9a-f]+)]], log:sub(start + 1, stop))
  -- First-card bootstrap may load twice. Use the last complete body/parts
  -- input block, and reject extra shoe draws rather than truncating them.
  check(#inputs == #active, ("(%d, %d, 'extra/missing footwear')"):format(#inputs, #active))
  local a_names = util.sorted(G.map(inputs, function(r) return r[2] end))
  local b_names = util.sorted(G.map(active, function(r) return r.mesh end))
  check(util.equal(a_names, b_names))
  local report = O("passed", true,
    "scope", "Studio default footwear hierarchy selection and source geometry; not final RGB/lighting parity",
    "card", args.card,
    "native_footwear", A(G.map(footwear, function(r) return O("path", r.path, "mesh", r.mesh, "active", r.active, "enabled", r.enabled) end)),
    "coincident_indoor_outdoor_meshes", A(twins),
    "ue_active_footwear_inputs", A(G.map(inputs, function(r) return A({ r[1], r[2], r[3] }) end)),
    "source_files", files)
  G.write_json(args.output, report)
  print(("PASS native active footwear=%d coincident source pairs=%d"):format(#active, #twins))
end
M.main = main

if ok.is_main() then ok.main(main) end
return M
