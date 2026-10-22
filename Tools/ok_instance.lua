-- This checkout's ports and voice servers, so two clones (e.g. C:\portable\OpenKoikatsu on master and E:\OpenKoikatsu on
-- duo, Docs/DUAL_LINE_DEVELOPMENT.md) can run their games side by side. Each checkout may have a local-instance.json at
-- its root (not in git; outside Saved/ so wiping Saved/ keeps it):
--   {"port_base": 47960, "voice": false}
-- port_base: TTS = base, ASR = base+1, StreetSocial control = base+10, interaction pair control = base+11,
-- song jobs (ok-song) = base+20.
--   Without the file the base is 47860 (47860 / 47861 / 47870 / 47871, the long-standing ports).
-- voice false: this checkout neither starts nor stops ok-tts / ok-asr (built-in voices, no voice input).
--
--   local instance = require "ok_instance"; local ports = instance.ports()
--   Tools\lua\lua.exe Tools\ok_instance.lua port street_control     (prints the number, for PowerShell callers)
local ok = require "ok"
local json, path = ok.json, ok.path

local M = {}

-- The ports for a given base (a sandboxed run picks its own; see run_street_social_development.lua -Sandbox).
function M.ports_at(base, voice)
  if base < 1024 or base > 65400 then error("port base out of range: " .. base) end
  return { base = base, tts = base, asr = base + 1, street_control = base + 10, pair_control = base + 11, song = base + 20, voice = voice }
end

function M.ports(root)
  root = root or ok.root
  local base, voice = 47860, true
  local file = path.join(root, "local-instance.json")
  if path.is_file(file) then
    local cfg = json.decode(path.read_text(file))
    if cfg.port_base ~= nil then base = math.tointeger(cfg.port_base) or error("local-instance.json port_base must be an integer") end
    if cfg.voice ~= nil then voice = cfg.voice and true or false end
    if base < 1024 or base > 65400 then error("local-instance.json port_base out of range: " .. base) end
  end
  return { base = base, tts = base, asr = base + 1, street_control = base + 10, pair_control = base + 11, song = base + 20, voice = voice }
end

-- Server pids of sandboxed games that are still running (Saved/StreetSocial/Sandbox/<name>/running.json, written by
-- run_street_social_development.lua -Sandbox): a normal launch must not stop them.
local function sandbox_servers(root)
  local keep = {}
  local dir = path.join(root, "Saved/StreetSocial/Sandbox")
  if not path.is_dir(dir) then return keep end
  local games = {}
  for _, p in ipairs(require("ok.kit").call({ "proc-list", "OpenKoikatsu" })) do games[p.pid] = true end
  for _, name in ipairs(path.list(dir)) do
    local file = path.join(dir, name, "running.json")
    local okr, r = pcall(function() return json.decode(path.read_text(file)) end)
    if path.is_file(file) and okr and type(r) == "table" and games[r.game_pid] then
      for _, pid in ipairs(r.server_pids or {}) do keep[pid] = true end
    end
  end
  return keep
end

-- Stop only the ok-tts / ok-asr / ok-song processes started from this checkout's Build/ (another clone's servers stay
-- up, and so do the servers of a sandboxed game that is still running).
function M.stop_own_voice_servers(root)
  root = root or ok.root
  local own = path.abs(path.join(root, "Build")):lower() .. "/"
  local keep = sandbox_servers(root)
  local stopped = 0
  for _, p in ipairs(require("ok.kit").call({ "proc-list", "ok-asr", "ok-tts", "ok-song" })) do
    if type(p.path) == "string" and p.path:gsub("\\", "/"):lower():sub(1, #own) == own and not keep[p.pid] then
      if require("ok.kit").call({ "proc-kill", tostring(p.pid) }).killed then stopped = stopped + 1 end
    end
  end
  return stopped
end

if ok.is_main() then
  ok.main(function()
    if arg[1] ~= "port" or not arg[2] then error("usage: ok_instance.lua port tts|asr|street_control|pair_control|base", 0) end
    local v = M.ports()[arg[2]]
    if type(v) ~= "number" then error("unknown port: " .. arg[2], 0) end
    print(v)
  end)
end

return M
