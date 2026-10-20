-- Verify UE face pass ordering against an installed native material fixture.
--
-- Usage: Tools/lua/lua.exe verify_face_layer_order.lua NATIVE_CARD_JSON OUTPUT_JSON UE_LOG ...
-- This checks ordering, not complete native/UE image parity.
local ok = require "ok"
local G = require "gpp"
local json, path, util, re = ok.json, ok.path, ok.util, ok.re
local O, A, I, check = G.O, G.A, G.I, G.check
local acr = require "audit_capture_regression"

local M = {}

local function has(s, sub) return s:find(sub, 1, true) ~= nil end
-- str(Path(p)) on Windows.
local pystr = require("compare").pystr

local function main()
  if #arg < 2 then error("ValueError: not enough values to unpack (expected at least 2, got " .. #arg .. ")") end
  local native, output = arg[1], arg[2]
  local logs = {}
  for i = 3, #arg do logs[#logs + 1] = arg[i] end
  local materials = pystr(native) .. ".materials.tsv"
  local queues = json.object()
  for _, line in ipairs(acr.splitlines(G.read(materials))) do
    local row = util.split(line, "\t")
    if row[1] == "material" and util.endswith(row[2], "/cf_O_eyeline") then
      local key = has(row[3]:lower(), "eyeline_kage") and "shadow" or "stroke"
      queues[key] = I(util.strip(row[5]))
    end
  end
  check(G.set_eq(json.keys(queues), { "shadow", "stroke" }) and queues.shadow < queues.stroke, util.repr(queues))
  local cases = {}
  for _, log in ipairs(logs) do
    local pending = json.object()
    local count = 0
    for _, line in ipairs(acr.splitlines(acr.decode_replace(G.read(log)))) do
      if has(line, "OK face layer part=") then
        local found = re.search([[OK face layer part=(.*?) priority=(\d+)]], line)
        if found then pending[found[1]:lower()] = I(found[2]) end
      end
      local task = has(line, "ShotPreview task ") and re.search([[ok.ShotPreview task \d+/\d+ card=(.*?) variant=]], line) or nil
      if task then
        local pairs_ = {}
        for _, name in ipairs(json.keys(pending)) do
          local priority = pending[name]
          if util.endswith(name, "_kage") and has(name, "eyeline") then
            local stroke = name:sub(1, #name - 5)
            check(pending[stroke] ~= nil and priority < pending[stroke], "(" .. log .. ", " .. task[1] .. ")")
            pairs_[#pairs_ + 1] = O("shadow", name, "shadow_priority", priority,
              "stroke", stroke, "stroke_priority", pending[stroke])
          end
        end
        check(#pairs_ > 0, "('No split eyeline passes', " .. log .. ", " .. task[1] .. ")")
        cases[#cases + 1] = O("log", G.winpath(path.abs(log)), "card", task[1], "pairs", A(pairs_))
        pending = json.object()
        count = count + 1
      end
    end
    check(count > 0, "('No completed face-order controls', " .. log .. ")")
  end
  check(#cases > 0)
  G.write_json(output, O("result", "pass", "scope", "relative native shadow/stroke queue; not final-pixel parity",
    "native_queues", queues, "native_fixture_sha256", G.digest(materials), "cases", A(cases)))
  print(("PASS native queues %s, %d UE face-layer controls"):format(util.repr(queues), #cases))
end
M.main = main

if ok.is_main() then ok.main(main) end
return M
