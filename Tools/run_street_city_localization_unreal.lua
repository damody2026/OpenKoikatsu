-- Runs one StreetSocialCity localization editor script headless (UnrealEditor -ExecutePythonScript, -NullRHI);
-- STREET_CITY_LANGUAGE_GROUP selects the batch. Log: output/street-social-city-production/localization-v1/<script>.log.
--   Tools\lua\lua.exe Tools\run_street_city_localization_unreal.lua [--script probe_street_city_localization_unreal.py] [--group smoke]
local ok = require "ok"
local args = ok.args.parser("Run a StreetSocialCity localization editor script")
  :opt("--script", { default = "probe_street_city_localization_unreal.py" }):opt("--group", { default = "smoke" })
  :parse(arg)
local root = ok.root
local script = root .. "/Tools/" .. args.script
if not ok.path.is_file(script) then error("Missing localization script: " .. script, 0) end
local log = root .. "/output/street-social-city-production/localization-v1/" .. args.script .. ".log"
ok.path.mkdir(ok.path.parent(log), true)
-- Temp files and the DDC on the production drive.
local scratch = "E:/StreetSocialCityProduction/unreal-scratch"
ok.path.mkdir(scratch, true)
local r = ok.proc.run({ ok.engine_root() .. "/Engine/Binaries/Win64/UnrealEditor.exe", root .. "/StreetSocialCity/StreetSocialCity.uproject",
  "/Engine/Maps/Entry",
  "-ExecutePythonScript=" .. root .. "/Tools/run_street_city_unreal_deferred.py", "-UserDir=E:/StreetSocialCityProduction/unreal-userdata",
  "-unattended", "-nosplash", "-nosound", "-nop4", "-NullRHI", "-NoAsyncLoadingThread", "-NoAsyncPostLoad", "-abslog=" .. log },
  { cwd = root, hide = true, env = { STREET_CITY_DEFERRED_SCRIPT = script, STREET_CITY_LANGUAGE_GROUP = args.group, TEMP = scratch, TMP = scratch,
    ["UE-LocalDataCachePath"] = "E:/StreetSocialCityProduction/unreal-ddc" } })
if r.code ~= 0 then error("Localization editor failed: " .. tostring(r.code), 0) end
