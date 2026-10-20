-- Runs the Studio acceptance commands in separate owned UE processes.
-- Tools/lua/lua.exe Tools/verify_ok_studio.lua --tag <fresh-name> [--compare]
local ok = require "ok"
local fs = require "okfs"
local path, proc = ok.path, ok.proc
local p = ok.args.parser("Verify OK Studio")
p:opt("--tag", { required = true })
p:opt("--engine", { default = ok.engine_root() })
p:flag("--compare")
local a = p:parse(arg)
assert(a.tag:match("^[%w_%-]+$"), "Invalid evidence tag")
local evidence = path.join(ok.root, "Saved/OKStudio/Verify-" .. a.tag)
assert(not path.exists(evidence), "Use a fresh evidence tag")
path.mkdir(evidence)
local executable = path.join(a.engine, "Engine/Binaries/Win64/UnrealEditor.exe")
local project = path.join(ok.root, "OpenKoikatsu.uproject")
local tests = {
  { "GameSelfTest", "OKStudio.GameSelfTest exit", "RESULT PASS", 600 },
  { "PatternTest", "OKStudio.PatternTest exit", "OK studio pattern test: RESULT PASS", 300 },
  { "EyeSetTest", "OKStudio.EyeSetTest exit", "OK studio eye set test: RESULT PASS", 300 },
}
if a.compare then
  tests[#tests + 1] = { "CompareKStudio", "OKStudio.CompareKStudio samples 14 120 exit", "RESULT 14 / 14", 1800 }
end
local failed = 0
local results = {}
for _, test in ipairs(tests) do
  local log = path.join(evidence, test[1] .. ".log")
  local args = { '"' .. executable .. '"', '"' .. project .. '"', "/Engine/Maps/Entry",
    "-game", "-windowed", "-ResX=1280", "-ResY=720", '-ExecCmds="' .. test[2] .. '"',
    '-abslog="' .. log .. '"', "-unattended", "-nosplash" }
  print("Studio verification: starting " .. test[1])
  io.stdout:flush()
  -- Runtime verification uses an already-built editor. SDK discovery starts
  -- another UBT and waits on the build mutex; it is unrelated to these tests.
  -- Use the engine-supported switch only in this child, restoring our environment.
  local sdk = fs.getenv("UE_SKIP_UBT_SDK_SETUP")
  fs.setenv("UE_SKIP_UBT_SDK_SETUP", "1")
  local launched, child = pcall(proc.popen, table.concat(args, " "), { shell = false, hide = true, stdout = "NUL", stderr = "NUL" })
  fs.setenv("UE_SKIP_UBT_SDK_SETUP", sdk)
  assert(launched, child)
  path.write_text(path.join(evidence, test[1] .. ".pid"), tostring(child:pid()) .. "\n")
  local code = child:wait(test[4])
  if code == nil then child:kill() end -- only the process this run created
  local text = path.exists(log) and path.read_text(log) or ""
  local passed = code == 0 and text:find(test[3], 1, true) ~= nil
    and not text:find("RESULT FAIL", 1, true)
    and not text:find("Fatal error:", 1, true)
  if not passed then failed = failed + 1 end
  local memory
  if test[1] == "CompareKStudio" then
    local peak = 0
    for gb in text:gmatch("pictured  %(memory ([%d.]+) GB%)") do peak = math.max(peak, tonumber(gb)) end
    local released = tonumber(text:match("final scene released %(memory ([%d.]+) GB%)"))
    memory = { peak_scene_gb = peak, final_released_gb = released }
    -- The final scene must exercise the same release path as replacement loads.
    if passed and not released then passed = false; failed = failed + 1 end
  end
  results[#results + 1] = { name = test[1], passed = passed, exit_code = code, log = log, memory = memory }
  print("Studio verification: " .. test[1] .. " " .. (passed and "PASS" or "FAIL"))
  io.stdout:flush()
end
path.write_text(path.join(evidence, "results.json"), ok.json.encode({ passed = failed == 0, tests = results }, { indent = 2 }) .. "\n")
print(evidence)
if failed > 0 then os.exit(1) end
