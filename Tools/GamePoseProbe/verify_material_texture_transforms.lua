-- Compare changed cached texture ST inputs with independent native material TSV.
local ok = require "ok"
local G = require "gpp"
local json, path, util, re = ok.json, ok.path, ok.util, ok.re
local O, A, check = G.O, G.A, G.check

local M = {}
local DOC = "Compare changed cached texture ST inputs with independent native material TSV."

function M.sha(p) return G.digest(p) end
-- re.sub(r'\s*\(Instance\)', '', name).strip().lower()
function M.material_key(name)
  return util.strip(re.sub([[\s*\(Instance\)]], "", name)):lower()
end
local material_key = M.material_key

local function tkey(...) return table.concat({ ... }, "\31") end
local function nums(text)
  local out = {}
  for _, n in ipairs(util.split(util.strip(text, "()"), ",")) do out[#out + 1] = util.float(n) end
  return out
end
local function list_eq(a, b)
  if #a ~= #b then return false end
  for i = 1, #a do if a[i] ~= b[i] then return false end end
  return true
end
local function max_abs_error(a, b)
  local best
  for i = 1, math.min(#a, #b) do
    local e = math.abs(a[i] - b[i])
    if best == nil or e > best then best = e end
  end
  if best == nil then error("ValueError: max() arg is an empty sequence") end
  return best
end
M.max_abs_error = max_abs_error

local function main()
  local p = ok.args.parser(DOC)
  p:arg("cache"); p:arg("native"); p:arg("output")
  p:opt("--card", { required = true })
  local args = p:parse(arg)
  local report = G.read_json(args.cache); check(report.passed)
  local card
  for _, row in ipairs(report.cards) do if row.card == args.card then card = row; break end end
  if card == nil then error("StopIteration") end
  local native = {}
  for _, line in ipairs(require("audit_capture_regression").splitlines(G.read(args.native))) do
    local fields = util.split(line, "\t")
    if fields[1] == "texture" and #fields >= 7 then
      local key = tkey(fields[2]:match("([^/]*)$"), material_key(fields[3]), fields[4])
      local value = nums(fields[6])
      for _, v in ipairs(nums(fields[7])) do value[#value + 1] = v end
      if native[key] ~= nil then check(list_eq(native[key], value), "('ambiguous native renderer/material state', " .. key .. ")") end
      native[key] = value
    end
  end
  local rows = {}
  for _, part in ipairs(card.changes) do
    for _, prop in ipairs(json.keys(part.st)) do
      local values = part.st[prop]
      if G.any(part.properties, function(kv) return util.startswith(kv[1]:lower(), prop:lower() .. "_st_") end) then
        local key = tkey(part.name, material_key(part.material), prop)
        if native[key] == nil then error("AssertionError: ('missing native material property', " .. key .. ")") end
        local expected = native[key]
        local err = max_abs_error(values, expected)
        check(#values == #expected and #expected == 4 and err <= 1e-6, part.name .. " " .. prop)
        rows[#rows + 1] = O("mesh", part.name, "material", part.material, "property", prop,
          "cached", values, "native", A(expected), "max_error", err)
      end
    end
  end
  check(#rows > 0, "No actual changed texture transform covered")
  local inputs = json.object()
  inputs[G.winpath(path.abs(args.cache))] = M.sha(args.cache)
  inputs[G.winpath(path.abs(args.native))] = M.sha(args.native)
  G.write_json(args.output, O("passed", true,
    "scope", "Changed texture coordinates against native material state; not RGB/complete shader parity",
    "card", args.card, "rows", A(rows), "inputs", inputs))
  print(("PASS native texture transforms=%d max=%s"):format(#rows, util.num(G.max(rows, function(r) return r.max_error end))))
end
M.main = main

if ok.is_main() then ok.main(main) end
return M
