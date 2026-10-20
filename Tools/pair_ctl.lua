-- Runtime control of the interaction pair scene over http://127.0.0.1:<port> (AOKInteractionPairGameMode -PairControl).
--   start   start the scene once on one pose (--filter) and wait until it measures; keep it running for experiments
--   exec    run console commands:   pair_ctl.lua exec "OKPhysXSecondary.Enabled 1;OKPhysXSecondary.Backend 0"
--   reset / stats                   pair_ctl.lua reset ; (wait 10 s) ; pair_ctl.lua stats
--   measure exec, settle 2 s, reset, wait, stats:   pair_ctl.lua measure "OKPhysXSecondary.Backend 1" --seconds 10
--   pose    switch the paired pose: pair_ctl.lua pose 後背位
--   Tools\lua\lua.exe Tools\pair_ctl.lua <action> [arg] [--port N] [--seconds 10]
--     start only: [--filter 後背位] [--speed 4] [--head-tilt 50] [--female <png>] [--extra "<engine args>"] [--background]
local ok = require "ok"

local args = ok.args.parser("Interaction pair runtime control")
  :arg("action", { nargs = "?", default = "stats", choices = { "start", "exec", "reset", "stats", "measure", "pose" } })
  :arg("arg", { nargs = "?", default = "" })
  :opt("--seconds", { type = "float", default = 10 })
  :opt("--port", { type = "int", default = 0 }) -- 0: this checkout's pair port (local-instance.json, Tools/ok_instance.lua; 47871 without it)
  :opt("--filter", { default = "後背位" }):opt("--speed", { type = "float", default = 4 }):opt("--head-tilt", { type = "int", default = 50 })
  :opt("--female", { default = "" })
  :opt("--extra", { default = "" }) -- start only: more engine arguments, space separated (e.g. "-OKNativeTrace -trace=cpu,frame")
  :flag("--background") -- start without taking focus (a minimized game never gets ready, so it is restored behind other windows)
  :parse(arg)
local root = ok.root
local port = args.port
if port == 0 then port = math.tointeger(tonumber(ok.util.strip(ok.proc.output({ ok.lua_exe, root .. "/Tools/ok_instance.lua", "port", "pair_control" })))) end
local base = "http://127.0.0.1:" .. port
local function get(p) return ok.kit.http_get(base .. p, { timeout = 30 }) end
local function show(s) io.write(s, s:sub(-1) == "\n" and "" or "\n") end

local a = args.action
if a == "exec" then show(get("/exec?cmd=" .. ok.kit.urlencode(args.arg)))
elseif a == "reset" then show(get("/reset"))
elseif a == "stats" then show(get("/stats"))
elseif a == "pose" then show(get("/pose?filter=" .. ok.kit.urlencode(args.arg)))
elseif a == "measure" then
  if args.arg ~= "" then get("/exec?cmd=" .. ok.kit.urlencode(args.arg)) end
  ok.time.sleep(2); get("/reset"); ok.time.sleep(args.seconds); show(get("/stats"))
elseif a == "start" then
  local log = root .. "/Saved/Logs/pair_control.log"
  local map = "/Engine/Maps/Templates/Template_Default?game=/Script/OpenKoikatsu.OKInteractionPairGameMode"
  local cmd = { ok.engine_root() .. "/Engine/Binaries/Win64/UnrealEditor.exe", root .. "/OpenKoikatsu.uproject", map, "-game", "-windowed",
    "-ResX=1280", "-ResY=720", "-NoSplash", "-nop4", "-abslog=" .. log,
    "-PairAudit", "-PairAuditFilter=" .. args.filter, "-PairAuditFirst=1", "-PairAuditLast=1", "-PairAuditSpeed=" .. ("%g"):format(args.speed),
    "-PairControl=" .. port, "-ExecCmds=OKPhysXSecondary.DebugHeadTilt " .. args.head_tilt }
  if args.female ~= "" then cmd[#cmd + 1] = "-PairFemale=" .. args.female end
  for extra in args.extra:gmatch("%S+") do cmd[#cmd + 1] = extra end
  local pid = ok.proc.spawn(cmd, { cwd = root, hide = args.background,
    env = { OK_AUTO_3D = "0", OK_AUTO_CARD = false, OK_CPU_SKIN = "0", OK_GPU_SKIN = "1", OK_SKELMESH = "1" } })
  print(("started pid %d; waiting for the pose ..."):format(pid)); io.stdout:flush()
  local function alive()
    local r = ok.proc.run({ "tasklist", "/FI", "PID eq " .. pid, "/NH", "/FO", "CSV" }, { capture = true })
    return (r.stdout or ""):find('"' .. pid .. '"', 1, true) ~= nil
  end
  local deadline, shown = ok.time.clock() + 360, false
  while alive() and ok.time.clock() < deadline do
    ok.time.sleep(5)
    -- --background: restore without activating (SW_SHOWNOACTIVATE) at the bottom of the z-order.
    if args.background and not shown then shown = ((ok.kit.call({ "window-noactivate", pid }) or {}).windows or 0) > 0 end
    if ok.path.is_file(log) and ok.path.read_text(log):find("PairControl measuring", 1, true) then print("ready"); break end
  end
end
