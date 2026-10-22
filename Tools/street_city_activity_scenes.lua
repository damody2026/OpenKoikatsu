-- Activity scene production. Lua owns orchestration; Unreal API code is embedded.
package.path='Tools/lua/?.lua;'..package.path
local json=require('ok.json')
local mode=arg[1] or 'probe'
local function read(p)local f=assert(io.open(p,'rb'));local s=f:read('*a');f:close();return s end
local p=assert(io.popen('cd'));local root=p:read('*l'):gsub('\\','/');p:close()
local out=root..'/output/street-social-city-production/activity-scenes-v1'
assert(os.execute('if not exist "'..out..'" mkdir "'..out..'"'))
local old=read('Tools/street_city_resort_native.lua')
local code=assert(old:match('local code=%[====%[\n(.-)CAT=read'))
code=code:gsub("PREFIX='/Game/UniversityCity/ResortV1';TAG='ResortNative%-v1'","PREFIX='/Game/UniversityCity/ActivityScenesV1';TAG='ActivityScenes-v1'")
code=code..[====[
API=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
def box(b):return {'min':[b.min.x,b.min.y,b.min.z],'max':[b.max.x,b.max.y,b.max.z]}
def probe():
 rows=[]
 for a in SUB.get_all_level_actors():
  label=a.get_actor_label()
  if label.startswith(('Campus_AD_','Campus_E1_','Campus_EE_','Campus_MA_','Mall_M1_F08','S07_','World_PK_','World_Scenery_bay')):
   p=a.get_actor_location();r=a.get_actor_rotation();o,e=a.get_actor_bounds(False);rows.append(dict(label=label,position_cm=[p.x,p.y,p.z],yaw=r.yaw,bounds_cm=dict(min=[o.x-e.x,o.y-e.y,o.z-e.z],max=[o.x+e.x,o.y+e.y,o.z+e.z])))
 from street_city_campus_contract import context
 from street_city_mall import contract
 campus={k:{'building':context(k)['building'],'rooms':context(k)['rooms']} for k in ['AD','E1','EE','MA']}
 assets=[]
 for path in EAL.list_assets('/Game/UniversityCity',True,False):
  if any('/'+k+'/' in path.lower() or '/'+k+'_' in path.lower() for k in ['art-kit','books','plush','laptop','sneaker','sushi','tea','cake']):
   a=EAL.load_asset(path)
   if isinstance(a,unreal.StaticMesh):assets.append(dict(path=path,bounds_cm=box(a.get_bounding_box()),lods=API.get_lod_count(a)))
 write(OUT/'probe.json',dict(status='passed',actors=rows,campus=campus,mall=contract(),products=assets))
]====]
local extra='Tools/street_city_activity_native_payload.lua'
local f=io.open(extra,'rb');if f then f:close();code=code..dofile(extra)end
code=code..dofile('Tools/street_city_activity_art_payload.lua')
code=code..[====[
def work():
 try:
  globals()[MODE]()
  if MODE=='render':return
 except BaseException:
  write(OUT/(MODE+'-error.json'),dict(status='failed',error=traceback.format_exc()));unreal.log_error(traceback.format_exc())
 unreal.SystemLibrary.quit_editor()
if MODE not in ['import_assets','asset_probe','art_materials']:assert unreal.EditorLevelLibrary.load_level(MAP)
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
frames=0
def settled(dt):
 global frames
 frames+=1
 if frames<60:return
 unreal.unregister_slate_post_tick_callback(start_handle);work()
start_handle=unreal.register_slate_post_tick_callback(settled)
]====]
local payload={root=root,out=out,mode=mode,run_id=mode..'-'..os.date('%Y%m%d-%H%M%S'),source=root..'/Tools/street_city_activity_scenes.lua::UnrealPayload',code=code}
local path=out..'/'..mode..'-payload.json';f=assert(io.open(path,'wb'));f:write(json.encode(payload));f:close()
local function q(s)return "'"..s:gsub("'","''").."'"end
local engine=json.decode(read('Tools/engine.local.json')).engine
local args={root..'/StreetSocialCity/StreetSocialCity.uproject','/Engine/Maps/Entry','-ExecutePythonScript='..root..'/Tools/run_street_city_unreal_deferred.py','-UserDir=E:/StreetSocialCityProduction/activity-scenes-user','-unattended','-RenderOffscreen','-NoAsyncLoadingThread','-NoAsyncPostLoad','-nosplash','-nosound','-nop4','-d3d12','-abslog='..out..'/'..mode..'-editor.log'}
for i,v in ipairs(args)do args[i]=q(v)end
local cmd="$env:STREET_CITY_DEFERRED_SCRIPT="..q(root..'/Tools/run_street_city_unreal_deferred.py').."; $env:STREET_CITY_DEFERRED_JSON="..q(path).."; $env:TEMP='E:/StreetSocialCityProduction/unreal-scratch'; $env:TMP=$env:TEMP; [Environment]::SetEnvironmentVariable('UE-LocalDataCachePath','E:/StreetSocialCityProduction/unreal-ddc','Process'); $p=Start-Process -FilePath "..q(engine..'/Engine/Binaries/Win64/UnrealEditor.exe')..' -ArgumentList @('..table.concat(args,',')..') -WindowStyle Hidden -PassThru; while(-not $p.WaitForExit(1000)){}; exit $p.ExitCode'
local ok,reason,exitcode=os.execute('powershell.exe -NoProfile -Command "'..cmd..'"')
local receipt=assert(io.open(out..'/'..mode..'-launcher.json','wb'));receipt:write(json.encode({run_id=payload.run_id,process_success=not not ok,reason=reason,exit_code=exitcode or 0}));receipt:close()
local result=json.decode(read(out..'/'..mode..'.json'));assert(result.run_id==payload.run_id and result.status=='passed','Missing fresh successful receipt')
if not ok then
 local log=read(out..'/'..mode..'-editor.log')
 assert(reason=='exit' and log:find('LogExit: Editor shut down',1,true) and log:find('Log file closed',1,true) and not log:find('Fatal error:',1,true),'Abnormal editor termination')
 print('Editor launcher returned '..tostring(exitcode)..' after fully logged normal shutdown; fresh receipt passed; independent reopen audit still required')
end
receipt=assert(io.open(out..'/'..mode..'-launcher.json','wb'));receipt:write(json.encode({run_id=payload.run_id,process_success=not not ok,reason=reason,exit_code=exitcode or 0,fresh_receipt_verified=true}));receipt:close()
print('Activity scenes '..mode..' passed')
