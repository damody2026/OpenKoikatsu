-- Lua orchestrates native Unreal art passes; project assets stay local.
package.path='Tools/lua/?.lua;'..package.path
local json=require('ok.json')
local mode=arg[1] or 'tree_probe'
assert(({tree_probe=true,tree_import=true,tree_install=true,tree_audit=true,tree_before=true,tree_after=true,tree_preview=true,tree_refine=true,tree_compare=true,tree_asset_audit=true})[mode])
local root='C:/portable/OpenKoikatsu'
local series=os.getenv('STREET_CITY_TREE_SERIES') or 'v1';assert(series=='v1' or series=='v2')
local out=root..'/output/street-social-city-production/tree-upgrade-'..series
assert(os.execute('if not exist "'..out..'" mkdir "'..out..'"'))
local function read(p)local f=assert(io.open(p,'rb'));local s=f:read('*a');f:close();return s end
local previous_error=out..'/'..mode..'-error.json';local ef=io.open(previous_error,'rb')
if ef then ef:close();assert(os.rename(previous_error,out..'/'..mode..'-error-history-'..os.date('%Y%m%d-%H%M%S')..'.json'))end
local code=assert(read('Tools/street_city_resort_native.lua'):match('local code=%[====%[\n(.-)CAT=read'))
code=code..dofile('Tools/street_city_tree_upgrade_payload.lua')..[====[
def work():
 try:
  globals()[MODE]()
  if MODE in ['tree_before','tree_after','tree_preview','tree_compare']:return
 except BaseException:
  write(OUT/(MODE+'-error.json'),dict(status='failed',error=traceback.format_exc()));unreal.log_error(traceback.format_exc())
 unreal.SystemLibrary.quit_editor()
if MODE not in ['tree_import','tree_preview','tree_refine','tree_asset_audit']:assert unreal.EditorLevelLibrary.load_level(MAP)
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
frames=0
def settled(dt):
 global frames
 frames+=1
 if frames<60:return
 unreal.unregister_slate_post_tick_callback(start_handle);work()
start_handle=unreal.register_slate_post_tick_callback(settled)
]====]
local species=dofile('Tools/street_city_tree_species.lua')[series]
local payload={root=root,out=out,mode=mode,series=series,species=species,destination_batch=tonumber(arg[2]) or 2,capture_ids=arg[3],run_id=mode..'-'..os.date('%Y%m%d-%H%M%S'),source=root..'/Tools/street_city_tree_upgrade.lua::UnrealPayload',code=code}
local path=out..'/'..mode..'-payload.json';local f=assert(io.open(path,'wb'));f:write(json.encode(payload));f:close()
local function q(s)return "'"..s:gsub("'","''").."'"end
local engine=json.decode(read('Tools/engine.local.json')).engine
local args={root..'/StreetSocialCity/StreetSocialCity.uproject','/Engine/Maps/Entry','-ExecutePythonScript='..root..'/Tools/run_street_city_unreal_deferred.py','-UserDir=E:/StreetSocialCityProduction/city-art-user','-unattended','-RenderOffscreen','-NoAsyncLoadingThread','-NoAsyncPostLoad','-nosplash','-nosound','-nop4','-d3d12','-abslog='..out..'/'..mode..'-editor.log'}
for i,v in ipairs(args)do args[i]=q(v)end
local cmd="$env:STREET_CITY_DEFERRED_SCRIPT="..q(root..'/Tools/run_street_city_unreal_deferred.py').."; $env:STREET_CITY_DEFERRED_JSON="..q(path).."; $env:TEMP='E:/StreetSocialCityProduction/unreal-scratch'; $env:TMP=$env:TEMP; [Environment]::SetEnvironmentVariable('UE-LocalDataCachePath','E:/StreetSocialCityProduction/unreal-ddc','Process'); $p=Start-Process -FilePath "..q(engine..'/Engine/Binaries/Win64/UnrealEditor.exe')..' -ArgumentList @('..table.concat(args,',')..') -WindowStyle Hidden -PassThru; while(-not $p.WaitForExit(1000)){}; exit $p.ExitCode'
local ok,reason,exitcode=os.execute('powershell.exe -NoProfile -Command "'..cmd..'"')
local receipt={run_id=payload.run_id,process_success=not not ok,reason=reason,exit_code=exitcode or 0}
f=assert(io.open(out..'/'..mode..'-launcher.json','wb'));f:write(json.encode(receipt));f:close()
local resultfile=io.open(out..'/'..mode..'.json','rb');local result
if resultfile then result=json.decode(resultfile:read('*a'));resultfile:close()end
if not result or result.run_id~=payload.run_id or result.status~='passed' then
 local failurefile=io.open(out..'/'..mode..'-error.json','rb');local detail='Missing fresh native success receipt'
 if failurefile then local failure=json.decode(failurefile:read('*a'));failurefile:close();if failure.run_id==payload.run_id then detail=failure.error end end
 error('Native '..mode..' failed: '..tostring(detail))
end
if not ok then local log=read(out..'/'..mode..'-editor.log');assert(log:find('LogExit: Editor shut down',1,true) and log:find('Log file closed',1,true) and not log:find('Fatal error:',1,true),'Abnormal editor termination');print('Shutdown anomaly preserved: '..tostring(exitcode)..'; independent reopen required')end
print('Tree upgrade '..mode..' passed')
