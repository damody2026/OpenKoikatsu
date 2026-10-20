-- Minimal unittest replacement.
--   local T = ok.test.suite("street city parts")
--   T:case("joins walls", function(t) t:eq(1 + 1, 2); t:truthy(x); t:raises(fn) end)
--   T:run()   -- prints results, exits 1 on failure (like unittest.main())
local util = require "ok.util"
local M = {}

local Ctx = {}
Ctx.__index = Ctx
local function show(v) return util.repr(v) end
function Ctx:fail(msg) error({ test_failure = true, msg = msg }, 0) end
function Ctx:eq(a, b, msg) if not util.equal(a, b) then self:fail((msg and msg .. ": " or "") .. show(a) .. " != " .. show(b)) end end
function Ctx:ne(a, b, msg) if util.equal(a, b) then self:fail((msg and msg .. ": " or "") .. show(a) .. " == " .. show(b)) end end
function Ctx:truthy(v, msg) if not util.truthy(v) then self:fail(msg or (show(v) .. " is not true")) end end
function Ctx:falsy(v, msg) if util.truthy(v) then self:fail(msg or (show(v) .. " is not false")) end end
function Ctx:is_nil(v, msg) if v ~= nil then self:fail(msg or (show(v) .. " is not None")) end end
function Ctx:close(a, b, places_or_tol, msg)
  local tol = places_or_tol and (places_or_tol >= 1 and 0.5 * 10 ^ -places_or_tol or places_or_tol) or 0.5e-7
  if math.abs(a - b) > tol then self:fail((msg and msg .. ": " or "") .. show(a) .. " !~ " .. show(b)) end
end
function Ctx:less(a, b, msg) if not (a < b) then self:fail((msg and msg .. ": " or "") .. show(a) .. " not < " .. show(b)) end end
function Ctx:less_eq(a, b, msg) if not (a <= b) then self:fail((msg and msg .. ": " or "") .. show(a) .. " not <= " .. show(b)) end end
function Ctx:greater(a, b, msg) if not (a > b) then self:fail((msg and msg .. ": " or "") .. show(a) .. " not > " .. show(b)) end end
function Ctx:greater_eq(a, b, msg) if not (a >= b) then self:fail((msg and msg .. ": " or "") .. show(a) .. " not >= " .. show(b)) end end
function Ctx:contains(container, item, msg)
  local found = false
  if type(container) == "string" then found = container:find(item, 1, true) ~= nil
  else for k, v in pairs(container) do if v == item or (k == item and type(k) == "string") then found = true break end end end
  if not found then self:fail((msg and msg .. ": " or "") .. show(item) .. " not found in " .. show(container)) end
end
function Ctx:not_contains(container, item, msg)
  local found = false
  if type(container) == "string" then found = container:find(item, 1, true) ~= nil
  else for k, v in pairs(container) do if v == item or (k == item and type(k) == "string") then found = true break end end end
  if found then self:fail((msg and msg .. ": " or "") .. show(item) .. " unexpectedly found") end
end
-- raises(fn, pattern?) : fn must error; optional Lua pattern matched against the message.
function Ctx:raises(fn, pattern, msg)
  local okv, err = pcall(fn)
  if okv then self:fail(msg or "expected an error") end
  if pattern and not tostring(type(err) == "table" and err.msg or err):find(pattern) then
    self:fail("error " .. show(tostring(err)) .. " does not match " .. show(pattern))
  end
  return err
end
function Ctx:count_eq(a, b, msg)
  local ca, cb = util.counter(a, util.repr), util.counter(b, util.repr)
  if not util.equal(ca, cb) then self:fail((msg and msg .. ": " or "") .. show(a) .. " vs " .. show(b)) end
end

local Suite = {}
Suite.__index = Suite
function M.suite(name) return setmetatable({ name = name, cases = {} }, Suite) end
function Suite:case(name, fn) self.cases[#self.cases + 1] = { name = name, fn = fn } end
function Suite:setup(fn) self.setup_fn = fn end
function Suite:teardown(fn) self.teardown_fn = fn end

-- run({exit=false}) returns ok, failures; by default exits like unittest.main().
function Suite:run(opts)
  opts = opts or {}
  local filter = opts.filter or (arg and arg[1] and not arg[1]:find("^%-") and arg[1]) or nil
  local failures, ran = {}, 0
  local started = require("okfs").clock()
  for _, c in ipairs(self.cases) do
    if not filter or c.name:find(filter, 1, true) then
      ran = ran + 1
      local ctx = setmetatable({ name = c.name }, Ctx)
      local okv, err = xpcall(function()
        if self.setup_fn then self.setup_fn(ctx) end
        c.fn(ctx)
      end, function(e) if type(e) == "table" and e.test_failure then return e end return debug.traceback(tostring(e), 2) end)
      if self.teardown_fn then pcall(self.teardown_fn, ctx) end
      if okv then io.write(".") else
        io.write(type(err) == "table" and "F" or "E")
        failures[#failures + 1] = { name = c.name, err = type(err) == "table" and err.msg or err, kind = type(err) == "table" and "FAIL" or "ERROR" }
      end
      io.stdout:flush()
    end
  end
  io.write("\n")
  for _, f in ipairs(failures) do
    io.write(string.rep("=", 70), "\n", f.kind, ": ", f.name, "\n", string.rep("-", 70), "\n", tostring(f.err), "\n")
  end
  io.write(string.rep("-", 70), "\n", string.format("Ran %d test%s in %.3fs\n\n", ran, ran == 1 and "" or "s", require("okfs").clock() - started))
  io.write(#failures == 0 and "OK\n" or string.format("FAILED (failures=%d)\n", #failures))
  if opts.exit ~= false then os.exit(#failures == 0 and 0 or 1) end
  return #failures == 0, failures
end

return M
