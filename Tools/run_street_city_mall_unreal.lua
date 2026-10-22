-- Imports the StreetSocialCity mall and its collision through Tools/assemble_street_city_unreal_mall.py inside
-- UnrealEditor (headless, -NullRHI) and checks output/street-social-city-production/mall-v1/unreal-task.json.
--   Tools\lua\lua.exe Tools\run_street_city_mall_unreal.lua [--audit-only]
local ok = require "ok"
local json, path = ok.json, ok.path
local args = ok.args.parser("Run the StreetSocialCity mall import"):flag("--audit-only"):parse(arg)
local root = ok.root
local out = root .. "/output/street-social-city-production/mall-v1"
local status_path = out .. "/unreal-task.json"
path.mkdir(out, true)
path.write_text(status_path, json.encode({ status = "launching" }))
local cmd = { ok.engine_root() .. "/Engine/Binaries/Win64/UnrealEditor.exe", root .. "/StreetSocialCity/StreetSocialCity.uproject",
  "/Engine/Maps/Entry",
  "-ExecutePythonScript=" .. root .. "/Tools/assemble_street_city_unreal_mall.py", "-UserDir=E:/StreetSocialCityProduction/unreal-userdata",
  "-unattended", "-nosplash", "-nosound", "-nop4", "-NullRHI", "-abslog=" .. out .. "/unreal-editor.log" }
if args.audit_only then cmd[#cmd + 1] = "-MallAuditOnly" end
-- Temp files and the DDC on the production drive.
local scratch = "E:/StreetSocialCityProduction/unreal-scratch"
path.mkdir(scratch, true)
local saved, env = {}, { TEMP = scratch, TMP = scratch, ["UE-LocalDataCachePath"] = "E:/StreetSocialCityProduction/unreal-ddc" }
for k, v in pairs(env) do saved[k] = ok.fs.getenv(k) or false; ok.fs.setenv(k, v) end
local child = ok.proc.popen(cmd, { cwd = root, stdout = "NUL", stderr = "NUL", hide = true })
for k, v in pairs(saved) do ok.fs.setenv(k, v or nil) end
local pid = child:pid()
local code = child:wait()
path.write_text(out .. "/unreal-process.json", json.encode({ processId = pid, exitCode = code, endedUtc = ok.time.now_utc_z() }))
local result = json.decode(path.read_text(status_path))
if code ~= 0 or result.status ~= "succeeded" then error("Mall native failed: " .. tostring(result.error), 0) end
print("Mall native import and collision verified")
