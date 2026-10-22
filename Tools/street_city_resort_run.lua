-- Run one native Unreal phase in a hidden editor, verify its fresh receipt.
package.path='Tools/lua/?.lua;'..package.path
local json=require('ok.json')
local mode=arg[1] or 'probe';assert(({probe=true,import=true,install=true,audit=true,render=true})[mode])
local first=tonumber(arg[2]) or 1;local last=tonumber(arg[3]) or 100
assert(first>=1 and last<=100 and first<=last)
local function read(p)local f=assert(io.open(p,'rb'));local s=f:read('*a');f:close();return s end
local p=assert(io.popen('cd'));local root=assert(p:read('*l')):gsub('\\','/');p:close()
local out=root..'/output/street-social-city-production/resort-native-v1'
assert(os.execute('Tools\\lua\\lua.exe Tools/street_city_resort_native.lua '..mode..' '..first..' '..last))
local payload=out..'/'..mode..'-payload.json';local run=json.decode(read(payload));local engine=json.decode(read('Tools/engine.local.json')).engine
local function q(s)return "'"..s:gsub("'","''").."'" end
local args={root..'/StreetSocialCity/StreetSocialCity.uproject','/Engine/Maps/Entry','-ExecutePythonScript='..root..'/Tools/run_street_city_unreal_deferred.py','-UserDir=E:/StreetSocialCityProduction/resort-native-user','-unattended','-RenderOffscreen','-NoAsyncLoadingThread','-NoAsyncPostLoad','-nosplash','-nosound','-nop4','-d3d12','-abslog='..out..'/'..mode..'-editor.log'}
for i,v in ipairs(args)do args[i]=q(v)end
local cmd="$env:STREET_CITY_DEFERRED_SCRIPT="..q(root..'/Tools/run_street_city_unreal_deferred.py').."; $env:STREET_CITY_DEFERRED_JSON="..q(payload).."; $env:TEMP='E:/StreetSocialCityProduction/unreal-scratch'; $env:TMP=$env:TEMP; [Environment]::SetEnvironmentVariable('UE-LocalDataCachePath','E:/StreetSocialCityProduction/unreal-ddc','Process'); $p=Start-Process -FilePath "..q(engine..'/Engine/Binaries/Win64/UnrealEditor.exe')..' -ArgumentList @('..table.concat(args,',')..') -WindowStyle Hidden -RedirectStandardOutput '..q(out..'/'..mode..'-process.log')..' -RedirectStandardError '..q(out..'/'..mode..'-stderr.log')..' -PassThru -Wait; exit $p.ExitCode'
assert(os.execute('powershell.exe -NoProfile -Command "'..cmd..'"'),'Unreal process failed')
local result=json.decode(read(out..'/'..({import='imports',install='install',audit='audit',render='render',probe='probe'})[mode]..'.json'))
assert(result.run_id==run.run_id,'Missing fresh Unreal receipt; inspect phase error/log files')
assert(result.status=='passed' or result.status=='partial' or result.status=='saved_requires_reopen_audit' or result.status=='captured_requires_visual_review',result.status)
print('Native '..mode..' completed: '..result.status)
