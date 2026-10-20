-- Targeted source-bloom cook. Explicit packages avoid editor startup assets / unrelated maps.
-- Tools/lua/lua.exe Tools/verify_ok_studio_bloom_cook.lua --tag <fresh-name>
local ok = require "ok"
local fs = require "okfs"
local path = ok.path
local parser = ok.args.parser("Verify source bloom cook")
parser:opt("--tag", { required = true })
parser:opt("--engine", { default = ok.engine_root() })
parser:opt("--root", { default = "/Game/OKStudio/Post/SourceBloom1" })
local a = parser:parse(arg)
assert(a.tag:match("^[%w_%-]+$"), "Invalid evidence tag")
assert(a.root:match("^/Game/[%w_/%-]+$") and a.root:sub(-1) ~= "/", "Invalid asset root")
local out = path.join(ok.root, "Saved/OKStudio/Verify-CookBloom-" .. a.tag)
assert(not path.exists(out), "Use a fresh evidence tag")
path.mkdir(out)
local packages = {}
for i = 0, 7 do packages[#packages + 1] = a.root .. "/M_SourceBloom" .. i end
local log = path.join(out, "Cook.log")
local args = { path.join(a.engine, "Engine/Binaries/Win64/UnrealEditor-Cmd.exe"), path.join(ok.root, "OpenKoikatsu.uproject"),
  "-run=Cook", "-TargetPlatform=Windows", "-PACKAGE=" .. table.concat(packages, "+"), "-cooksinglepackage",
  "-CookSkipRequests", "-SkipZenStore", "-OutputDir=" .. path.join(out, "Cooked/[Platform]"),
  "-unattended", "-nop4", "-stdout", "-abslog=" .. log }
local prior = fs.getenv("UE_SKIP_UBT_SDK_SETUP")
fs.setenv("UE_SKIP_UBT_SDK_SETUP", "1")
local launched, child = pcall(ok.proc.popen, args, { hide = true, stdout = "NUL", stderr = "NUL" })
fs.setenv("UE_SKIP_UBT_SDK_SETUP", prior)
local errors, files, code = {}, {}, nil
if launched then
  path.write_text(path.join(out, "Cook.pid"), tostring(child:pid()) .. "\n")
  code = child:wait(900)
  if code == nil then child:kill(); errors[#errors + 1] = "cook timed out" end
else errors[#errors + 1] = tostring(child) end
local text = path.exists(log) and path.read_text(log) or ""
if code ~= 0 then errors[#errors + 1] = "cook exit code: " .. tostring(code) end
if text:find("Error:", 1, true) or text:find("Fatal error:", 1, true) then errors[#errors + 1] = "cook log contains errors" end
for _, package in ipairs(packages) do
  local rel = "Cooked/Windows/OpenKoikatsu/Content/" .. package:sub(7) .. ".uasset"
  local file = path.join(out, rel)
  if path.is_file(file) and path.size(file) > 0 then
    files[#files + 1] = { package = package, file = rel, bytes = path.size(file), sha256 = ok.hash.sha256_file(file) }
  else errors[#errors + 1] = "missing cooked target: " .. package end
end
local passed = #errors == 0 and #files == 8
path.write_text(path.join(out, "results.json"), ok.json.encode({ passed = passed, finished = true, exit_code = code,
  packages = files, errors = errors, log = log }, { indent = 2 }) .. "\n")
print(("source bloom cook: %s (%d / 8 cooked targets); %s"):format(passed and "PASS" or "FAIL", #files, out))
if not passed then for _, err in ipairs(errors) do print(err) end; os.exit(1) end
