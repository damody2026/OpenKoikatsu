-- Verify actual shared-mode watergun and metadata/demand-loading audit evidence.
-- Usable as a module: require("verify_interaction_features").verify(directory, executable)
local ok = require "ok"
local json, P, U = ok.json, ok.path, ok.util
local M = {}

local function read(path) return json.decode(P.read_text(path)) end
local function fail(msg) error("ValueError: " .. msg, 0) end
local function same_path(a, b) return P.abs(a):lower() == P.abs(b):lower() end
local function get(t, k, d) local v = t[k]; if v == nil then return d end; return v end

local NIAGARA = { "niagaraworldmanager.*not.*initial", "niagara.*worldinit.*not" }
local NIAGARA_PLAIN = { "worldinit never happened", "niagara has never encountered", "assertion failed", "fatal error:" }
local SHADER_PLAIN = { "Missing shader resource", "uncooked shader map", "Default Material will be used", "Invalid shader map ID" }

local function log_has(log, patterns, plain, lower)
  for line in (log .. "\n"):gmatch("([^\n]*)\n") do
    local l = lower and line:lower() or line
    for _, p in ipairs(patterns) do if l:find(p) then return true end end
    for _, p in ipairs(plain) do if l:find(p, 1, true) then return true end end
  end
  return false
end

function M.verify(directory, executable)
  directory, executable = P.abs(directory), P.abs(executable)
  local log = P.read_text(P.join(directory, "game.log"))
  local base = log:match("LogInit: Base Directory: ([^\n]+)")
  if not base or not same_path(P.join(U.strip(base), "OpenKoikatsu.exe"), executable) then
    fail("Feature evidence must use the published executable")
  end
  if log_has(log, NIAGARA, NIAGARA_PLAIN, true) then fail("Niagara lifecycle errors or crashes invalidate feature acceptance") end
  if log_has(log, {}, SHADER_PLAIN, false) then fail("Feature effects must load their cooked shaders") end
  local pid = math.tointeger(tonumber(U.strip(P.read_text(P.join(directory, "game.pid"))))) or fail("invalid literal for int()")
  local rows = {}
  for _, p in ipairs(P.glob(directory, "HighInteraction/**/result.json")) do
    if read(p).watergun_hits ~= nil then rows[#rows + 1] = p end
  end
  if #rows ~= 1 then fail("One actual watergun/catalogue audit required") end
  local path = rows[1]
  local result = read(path)
  if U.truthy(result.error) or result.state ~= "returned" or
      not U.truthy(result.same_process) or result.process_id ~= pid or
      get(result, "catalogue_total", 0) < 2 or get(result, "catalogue_categories", 0) < 2 or
      result.unprepared_selection_accepted ~= true or
      result.demand_motion_played ~= true or
      get(result, "watergun_hits", 0) <= 0 or get(result, "watergun_deposited", 0) <= 0 or
      result.watergun_clear ~= true then
    fail("Actual demand playback and water hits/deposit/clear must all succeed")
  end
  local out = json.object({ { "passed", true }, { "evidence", path }, { "sha256", ok.hash.file(path, "sha256") },
    { "catalogue_total", result.catalogue_total }, { "catalogue_categories", result.catalogue_categories },
    { "watergun_hits", result.watergun_hits }, { "watergun_deposited", result.watergun_deposited } })
  if result.clothing_visibility_preserves_high_model ~= nil then
    local ms = get(result, "clothing_visibility_max_callback_ms", -1)
    if result.clothing_visibility_preserves_high_model ~= true or
        result.clothing_visibility_switches ~= 32 or
        not (0 <= ms and ms < 100) then
      fail("Instant clothing visibility must preserve the high model for 32 switches")
    end
    for _, key in ipairs({ "clothing_visibility_preserves_high_model", "clothing_visibility_switches", "clothing_visibility_max_callback_ms" }) do
      out[key] = result[key]
    end
  end
  return out
end

if ok.is_main() then
  local p = ok.args.parser("Verify actual shared-mode watergun and metadata/demand-loading audit evidence.")
  p:arg("directory")
  p:opt("--executable", { required = true })
  local args = p:parse(arg)
  local result = M.verify(args.directory, args.executable)
  P.write_text(P.join(args.directory, "interaction.features.validation.json"), json.encode(result, { indent = 2 }) .. "\n")
  print(json.encode(result))
end

return M
