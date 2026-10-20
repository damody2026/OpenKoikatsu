-- Starts the one-to-one interaction scene with PhysX secondary dynamics (AOKInteractionPairGameMode):
-- loads Content/CharacterCards/Demo/C050.png (female) + Male01.png unless --female/--male are given.
--   --self-test              pick a few motions from the merged list, log whether each one plays ("SelfTest done")
--   --dump-motion-catalogue  write Saved/MotionCatalogue.tsv (input of `ok-extract motion-catalogue`)
--   --control [--port N]     runtime control on http://127.0.0.1:N (default: this checkout's pair port, local-instance.json /
--                            Tools/ok_instance.lua, 47871 without it), see Tools/pair_ctl.lua
-- Prints the pid and the log path. Engine: ok.engine_root() (Tools/ok_engine.lua).
--   Tools\lua\lua.exe Tools\run_interaction_pair.lua [--female <png>] [--male <png>] [--width 2560] [--height 1400] [--exec-cmds "..."]
local ok = require "ok"

local args = ok.args.parser("Start the one-to-one interaction pair scene")
  :opt("--female", { default = "" }):opt("--male", { default = "" })
  :opt("--width", { type = "int", default = 2560 }):opt("--height", { type = "int", default = 1400 })
  :flag("--dump-motion-catalogue"):flag("--self-test"):flag("--control")
  :opt("--port", { type = "int", default = 0 }):opt("--exec-cmds", { default = "" })
  :parse(arg)
ok.util.check(args.width >= 640 and args.width <= 7680, "--width must be 640..7680")
ok.util.check(args.height >= 480 and args.height <= 4320, "--height must be 480..4320")

local root = ok.root
local log = root .. "/Saved/Logs/InteractionPair-" .. ok.time.stamp() .. ".log"
ok.path.mkdir(ok.path.parent(log), true)
local map = "/Engine/Maps/Templates/Template_Default?game=/Script/OpenKoikatsu.OKInteractionPairGameMode"
local cmd = { ok.engine_root() .. "/Engine/Binaries/Win64/UnrealEditor.exe", root .. "/OpenKoikatsu.uproject", map, "-game", "-windowed",
  "-ResX=" .. args.width, "-ResY=" .. args.height, "-NoSplash", "-nop4", "-abslog=" .. log }
if args.female ~= "" then cmd[#cmd + 1] = "-PairFemale=" .. args.female end
if args.male ~= "" then cmd[#cmd + 1] = "-PairMale=" .. args.male end
if args.dump_motion_catalogue then cmd[#cmd + 1] = "-DumpMotionCatalogue" end
if args.self_test then cmd[#cmd + 1] = "-PairSelfTest" end
if args.control then
  local port = args.port
  if port == 0 then port = math.tointeger(tonumber(ok.util.strip(ok.proc.output({ ok.lua_exe, root .. "/Tools/ok_instance.lua", "port", "pair_control" })))) end
  cmd[#cmd + 1] = "-PairControl=" .. port
end
if args.exec_cmds ~= "" then cmd[#cmd + 1] = "-ExecCmds=" .. args.exec_cmds end
-- Same renderer switches the card reader uses: GPU skinning on, no automatic 3D card load.
local pid = ok.proc.spawn(cmd, { cwd = root, env = { OK_AUTO_3D = "0", OK_AUTO_CARD = false, OK_CPU_SKIN = "0", OK_GPU_SKIN = "1", OK_SKELMESH = "1" } })
print("pid: " .. pid)
print("log: " .. log:gsub("/", "\\"))
