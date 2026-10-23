-- Frame times of KStudio scenes in OK Studio once loaded (OKStudio.PerfKStudio), run as SceneEditor.cmd runs the studio.
-- Tools/lua/lua.exe Tools/run_ok_studio_perf.lua --tag <name> [--scenes samples|<folder>] [--count 14] [--seconds 5]
--   [--settle 120] [--width 1920 --height 1080] [--dump <frames>] [--env NAME=VALUE ...]
-- Report: Saved/OKStudio/perf/<tag>.json; log: Saved/Logs/perf_<tag>.log. --dump also prints the engine's averaged
-- game-thread stats (stat dumpave) of the last scene's OK Studio and KK preview counters.
local ok = require "ok"
local fs = require "okfs"
local path, proc = ok.path, ok.proc
local p = ok.args.parser("OK Studio frame times")
p:opt("--tag", { required = true })
p:opt("--engine", { default = ok.engine_root() })
p:opt("--scenes", { default = "samples" })
p:opt("--count", { default = "14" })
p:opt("--settle", { default = "120" })
p:opt("--seconds", { default = "5" })
p:opt("--width", { default = "1920" })
p:opt("--height", { default = "1080" })
p:opt("--dump")
p:opt("--env", { nargs = "*", default = {} })
p:opt("--timeout", { default = "3600" })
local a = p:parse(arg)
assert(a.tag:match("^[%w_%-]+$"), "Invalid tag")
local log = path.join(ok.root, "Saved/Logs/perf_" .. a.tag .. ".log")
local args = { path.join(a.engine, "Engine/Binaries/Win64/UnrealEditor.exe"), path.join(ok.root, "OpenKoikatsu.uproject"),
  "/Engine/Maps/Entry?game=/Script/OKStudio.OKStudioGameMode", "-game", "-windowed",
  "-ResX=" .. a.width, "-ResY=" .. a.height, "-nosound", "-unattended", "-nosplash", "-nop4",
  "-OKStudioPerfTag=" .. a.tag, "-abslog=" .. log,
  ("-ExecCmds=OKStudio.PerfKStudio %s %s %s %s exit"):format(a.scenes, a.count, a.settle, a.seconds) }
local saved = {}
local function setenv(name, value) saved[#saved + 1] = { name, fs.getenv(name) }; fs.setenv(name, value) end
setenv("UE_SKIP_UBT_SDK_SETUP", "1")
if a.dump then setenv("OK_STUDIO_PERF_DUMP", a.dump) end
for _, kv in ipairs(a.env) do
  local k, v = kv:match("^([%w_]+)=(.*)$")
  assert(k, "--env takes NAME=VALUE: " .. kv)
  setenv(k, v)
end
local child = proc.popen(args, { hide = true, stdout = "NUL", stderr = "NUL" })
for i = #saved, 1, -1 do fs.setenv(saved[i][1], saved[i][2]) end
local code = child:wait(tonumber(a.timeout))
if code == nil then child:kill() end -- only the process this run created
local text = path.exists(log) and path.read_text(log) or ""
for line in text:gmatch("[^\n]+") do
  local perf = line:match("OK studio perf: (.*)")
  if perf then print(perf) end
end
if a.dump then
  -- The averaged hierarchy lines of interest (OK Studio, KK preview and top-level game thread counters).
  local block = text:match("%-%-%-%-%-%- %d+ frames, average %-+(.*)") or ""
  for line in block:gmatch("[^\n]+") do
    local entry = line:match("LogStats:%s(.*)")
    if entry and (entry:find("STAT_KK_", 1, true) or entry:find("STAT_OKScene_", 1, true) or entry:find("STAT_OKStudio_", 1, true) or entry:find("UOKStudioGame", 1, true) or entry:find("KKPreviewActor/", 1, true) or entry:find("OKSceneActor", 1, true)
      or entry:find(" GameThread ", 1, true) or entry:find("FrameTime", 1, true) or entry:find("RedrawViewports", 1, true)
      or entry:find("Slate", 1, true) or entry:find("World Tick Time", 1, true)) then
      print((entry:gsub(" %- STATGROUP.*$", "")))
    end
  end
end
print(("exit=%s log=%s"):format(tostring(code), log))
if code ~= 0 then os.exit(1) end
