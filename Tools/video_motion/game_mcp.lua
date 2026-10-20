-- Shared helpers for the video-motion / studio game tests: launch the editor game with the
-- CharacterMCP file mailbox, call actions, wait for the character, record checks.
--
--   local G = dofile(ok.root .. "/Tools/video_motion/game_mcp.lua")
--   local game = G.launch({ out = dir, card = png, game = kks_root })
--   game:wait_ready(300); local r = game:call("video_motion", { item = "..." }); game:close()
local ok = require "ok"
local json, path = ok.json, ok.path

local M = {}

local function hex32()
  local t = {}
  for i = 1, 32 do t[i] = string.format("%x", math.random(0, 15)) end
  return table.concat(t)
end

local Game = {}
Game.__index = Game

-- opts: out (dir, required), card (png), game (KKS root override), editor (exe)
function M.launch(opts)
  math.randomseed(math.floor(ok.time.time() * 1e6) % 2147483647)
  local root = ok.root
  local editor = opts.editor or ((os.getenv("OK_ENGINE_ROOT") or "E:/PhysX/UE5.8") .. "/Engine/Binaries/Win64/UnrealEditor.exe")
  local out = path.abs(opts.out)
  local mailbox = out .. "/mailbox"
  for _, d in ipairs({ "requests", "responses", "images", "cards" }) do path.mkdir(mailbox .. "/" .. d, true) end
  local card = path.abs(opts.card or (root .. "/Content/CharacterCards/CrowdAPNG/C000.png"))
  local cmd = { editor, root .. "/OpenKoikatsu.uproject", "/Engine/Maps/Templates/Template_Default?game=/Script/OpenKoikatsu.OpenKoikatsuGameMode",
    "-game", "-NoSplash", "-RenderOffscreen", "-windowed", "-ResX=1600", "-ResY=1000", "-nosound", "-unattended", "-nop4",
    "-CharacterMCP", "-CharacterMCPDir=" .. mailbox, "-CharacterMCPCard=" .. card, "-abslog=" .. out .. "/editor.log" }
  -- The KKS root (validation cards, male partners) without editing Config.
  if opts.game then cmd[#cmd + 1] = "-ini:Game:[/Script/KKSideloader.KKGameRoot]:Root=" .. path.abs(opts.game) end
  local proc = ok.proc.popen(cmd, { stdout = "NUL", stderr = "NUL", hide = true })
  local g = setmetatable({ proc = proc, out = out, mailbox = mailbox, started = ok.time.clock(), checks = {}, failed = 0 }, Game)
  return g
end

function Game:alive()
  local code = self.proc:poll()
  if code ~= nil then error(("Editor exited (%d); see %s/editor.log"):format(code, self.out), 0) end
end

function Game:elapsed() return ok.time.clock() - self.started end

-- Writes requests/<32hex>.json and waits for responses/<id>.json; returns the decoded response.
function Game:call(action, fields, timeout)
  timeout = timeout or 60
  local id = hex32()
  local req = json.object({ { "action", action }, { "deadline", math.floor(os.time() + timeout) } })
  if self.session and action ~= "state" then req.session = self.session end
  for k, v in pairs(fields or {}) do req[k] = v end
  local tmp = self.mailbox .. "/requests/" .. id .. ".json.tmp"
  path.write_text(tmp, json.encode(req), { newline = "" })
  assert(os.rename(tmp, self.mailbox .. "/requests/" .. id .. ".json"))
  local resp = self.mailbox .. "/responses/" .. id .. ".json"
  local start = ok.time.clock()
  while ok.time.clock() - start < timeout + 5 do
    self:alive()
    if path.is_file(resp) then
      local good, r = pcall(json.decode, path.read_text(resp))
      if good and r then
        path.remove(resp, true)
        if r.session then self.session = r.session end
        return r
      end
    end
    ok.time.sleep(0.2)
  end
  error(action .. ": no response in " .. timeout .. "s", 0)
end

function Game:state() return self:call("state", {}, 20).result or {} end

-- Waits until the character is loaded and idle.
function Game:wait_ready(timeout)
  while true do
    local good, st = pcall(function() return self:state() end)
    if good and st.busy == false then return st end
    if self:elapsed() > (timeout or 300) then error("character never became idle", 0) end
    ok.time.sleep(1)
  end
end

function Game:check(label, cond, detail)
  print(("[%5.1fs] %s %s %s"):format(self:elapsed(), cond and "PASS" or "FAIL", label, detail or "")); io.stdout:flush()
  self.checks[#self.checks + 1] = { label = label, ok = cond and true or false }
  if not cond then self.failed = self.failed + 1 end
end

-- Kills the game and prints the summary; returns the process exit code.
function Game:finish(err)
  self.proc:kill()
  if err then print("ERROR " .. tostring(err)); self.failed = self.failed + 1 end
  local failed = json.array()
  for _, c in ipairs(self.checks) do if not c.ok then failed[#failed + 1] = c.label end end
  print(json.encode({ passed = #self.checks - #failed, failed = failed, output = self.out }))
  return self.failed == 0 and 0 or 1
end

function M.near(v, w, tol)
  for i = 1, #w do if math.abs((v[i] or 1e9) - w[i]) > tol then return false end end
  return true
end

function M.list(t)
  local s = {}
  for i, v in ipairs(t or {}) do s[i] = type(v) == "number" and ("%.3f"):format(v) or tostring(v) end
  return "[" .. table.concat(s, ", ") .. "]"
end

return M
