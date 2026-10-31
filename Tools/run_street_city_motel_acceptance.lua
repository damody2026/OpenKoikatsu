-- Full motel/shop-street acceptance on the current map, E engine only, unattended:
--   native worker (nullrhi) audit -> routes -> physics_plan -> quit
--   -> wait for any other AutomationTool on the machine (re-check every 10 min) -> full cook (repackage)
--   -> packaged captures (optional modes) -> mirror-ray migration -> quick cooked physics -> full walk.
-- Usage: lua Tools/run_street_city_motel_acceptance.lua [--reuse] [--no-walk] [--from-cook] [--yield <agent>] [--rooms id ...] [--capture v2|focus|samples [room id ...]]
--   --yield: give the Unreal slot back between steps when Tools/unreal_slot.lua says a higher-priority agent waits (exit 3)
--   --no-walk: stop after the packaged captures (trial iterations; final acceptance must walk)
--   --rooms: first install those rooms-v2 builds (Tools/street_city_motel_sample_payload.lua rooms_install)
package.path='Tools/lua/?.lua;'..package.path
local ok=require('ok');local j=require('ok.json')
local root='C:/portable/OpenKoikatsu';local m=root..'/output/street-social-city-production/motel-v1'
local rooms,captures,mode={},{},nil
local skip=0
for i=1,#arg do if skip>0 then skip=skip-1 elseif arg[i]=='--yield' then skip=1;mode=nil elseif arg[i]=='--reuse' or arg[i]=='--no-walk' or arg[i]=='--from-cook' then elseif arg[i]=='--rooms' or arg[i]=='--capture' then mode=arg[i] elseif mode=='--rooms' then rooms[#rooms+1]=arg[i] elseif mode=='--capture' then captures[#captures+1]=arg[i] end end
local function read(p)local f=io.open(p,'rb');if not f then return nil end;local r=f:read('*a');f:close();return r end
local function lua(args,timeout)local a={root..'/Tools/lua/lua.exe'};for _,v in ipairs(args)do a[#a+1]=v end;local r=ok.proc.run(a,{cwd=root,hide=true,capture=true,timeout=timeout or 7200});return r.code==0,(r.stdout or '')..(r.stderr or '')end
local function sleep(s)ok.proc.run({'powershell.exe','-NoProfile','-Command','Start-Sleep -Seconds '..s},{hide=true,timeout=s+30})end
local function log(s)print(os.date('%H:%M ')..s);io.stdout:flush()end
-- '--yield <agent>': before every step ask the shared slot tool (Tools/unreal_slot.lua yield); when an agent of higher
-- priority waits, quit our worker and exit 3 so the caller releases its slot; the next run resumes from the map state
-- (installs are saved) or with --from-cook.
local yagent;for i=1,#arg do if arg[i]=='--yield' then yagent=arg[i+1] end end
local function queued()
 if not yagent then return false end
 local r=ok.proc.run({root..'/Tools/lua/lua.exe','Tools/unreal_slot.lua','yield',yagent},{cwd=root,hide=true,capture=true})
 return r.code==3 and ((r.stdout or ''):match('[^\r\n]+') or 'higher priority')
end
local worker_up=false
local function step(name,args,want,timeout)
 local q=name~='quit' and queued()
 if q then log('YIELD before '..name..': '..q..' is queued');if worker_up then lua({'Tools/street_city_motel_job.lua','quit'},600)end;os.exit(3)end
 log('== '..name);local good,out=lua(args,timeout);if not good or (want and not out:find(want,1,true))then log('FAILED '..name..'\n'..out:sub(-1500));os.exit(1)end;log((out:match('([^\r\n]+)%s*$') or ''))end
local function editors()local r=ok.proc.run({'powershell.exe','-NoProfile','-Command',[[(Get-Process UnrealEditor,UnrealEditor-Cmd -ErrorAction SilentlyContinue | ? { $_.Path -like 'E:\PhysX\*' } | Measure-Object).Count]]},{hide=true});return tonumber((r.stdout or ''):match('%d+')) or 0 end
local function automation()local r=ok.proc.run({'powershell.exe','-NoProfile','-Command',[[(Get-CimInstance Win32_Process | ? { $_.CommandLine -match 'AutomationTool.dll|BuildCookRun' } | Measure-Object).Count]]},{hide=true});return tonumber((r.stdout or ''):match('%d+')) or 0 end
-- '--reuse': an already-ready worker of ours is reused instead of starting one
local reuse=false;for i=1,#arg do if arg[i]=='--reuse' then reuse=true end end
-- '--from-cook': resume an interrupted run at the cook. Only when the current map is the one audit/routes/physics_plan
-- last passed on (game-physics-plan.json map_sha256), i.e. nothing was installed since.
local from_cook=false;for i=1,#arg do if arg[i]=='--from-cook' then from_cook=true end end
if from_cook then
 local sha=require('ok.hash').sha256_file(root..'/StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap')
 local plan=j.decode(read(m..'/game-physics-plan.json') or '{}');local audit=j.decode(read(m..'/audit.json') or '{}')
 assert(plan.map_sha256==sha and audit.status=='passed','--from-cook: the map changed since the last audit/physics_plan; run the full acceptance')
 log('resuming at cook on map '..sha:sub(1,12))
end
if not reuse and not from_cook then assert(editors()==0,'an E-engine UnrealEditor is already running; stop it first') end
if not from_cook then
-- native worker: start detached, wait for THIS run's ready receipt (payload is rewritten by the new launcher)
local function start_worker(fresh)
 local before=(j.decode(read(m..'/serve-payload.json') or '{}')).run_id
 -- a fresh log name per start: a worker that is still shutting down keeps the previous log open ("file in use")
 if not fresh then before=nil else os.execute('start "" /B "'..root..'/Tools/lua/lua.exe" Tools/street_city_motel_native.lua serve > "'..m..'/serve-acceptance-'..os.date('%H%M%S')..'.log" 2>&1') end
 local ready=false
 for _=1,900 do
  local r,p=j.decode(read(m..'/serve-ready.json') or '{}'),j.decode(read(m..'/serve-payload.json') or '{}')
  if p.run_id and (before==nil or p.run_id~=before) and r.status=='passed' and r.run_id==p.run_id then ready=true;break end
  sleep(2)
 end
 assert(ready,'native worker did not become ready');worker_up=true
end
-- private memory (GB) of our E-engine worker; installs accumulate (46 Tripo-furnished rooms reached ~60 GB)
local function worker_gb()local r=ok.proc.run({'powershell.exe','-NoProfile','-Command',[[(Get-Process UnrealEditor,UnrealEditor-Cmd -ErrorAction SilentlyContinue | ? { $_.Path -like 'E:\PhysX\*' } | Measure-Object PrivateMemorySize64 -Sum).Sum/1GB]]},{hide=true,capture=true});return tonumber((r.stdout or ''):match('[%d%.]+')) or 0 end
start_worker(not reuse)
if #rooms>0 then
 -- one install job per 10 rooms (the job GCs per room; small batches keep a failure cheap to resume);
 -- above 40 GB the worker is restarted before the next batch so the 60 GB stop line is never reached
 for b=1,#rooms,10 do
  local ids={};for k=b,math.min(b+9,#rooms)do ids[#ids+1]=rooms[k] end
  local f=assert(io.open(m..'/sample-rooms/rooms-v2-batch.json','wb'));f:write(j.encode({ids=ids}));f:close()
  step('rooms_install '..table.concat(ids,' '),{'Tools/street_city_motel_job.lua','rooms_install','Tools/street_city_motel_sample_payload.lua'},'Completed',2000)
  local gb=worker_gb()
  if gb>40 and b+10<=#rooms then
   log(string.format('worker at %.1f GB: restarting before the next batch',gb))
   step('quit',{'Tools/street_city_motel_job.lua','quit'},'Completed',600);worker_up=false
   for _=1,120 do if editors()==0 then break end;sleep(5)end
   start_worker(true)
  end
 end
end
step('audit',{'Tools/street_city_motel_job.lua','audit','Tools/street_city_motel_audit_payload.lua'},'Completed',2000)
local audit=j.decode(read(m..'/audit.json'));if audit.status~='passed' then log('audit requires repair: '..j.encode(audit.issues):sub(1,1500));lua({'Tools/street_city_motel_job.lua','quit'},600);os.exit(1)end
step('routes',{'Tools/street_city_motel_job.lua','routes','Tools/street_city_motel_routes_payload.lua'},'Completed',2000)
step('physics_plan',{'Tools/street_city_motel_job.lua','physics_plan','Tools/street_city_motel_routes_payload.lua'},'Completed',2000)
step('quit',{'Tools/street_city_motel_job.lua','quit'},'Completed',600);worker_up=false
end
for _=1,120 do if editors()==0 then break end;sleep(5)end
while automation()>0 do log('another AutomationTool is running; re-check in 10 minutes');sleep(600)end
step('cook',{'Tools/repackage_street_city_motel.lua'},'Built independent',7300)
if #captures>0 then local a={'Tools/capture_street_city_motel.lua','package'};for _,c in ipairs(captures)do a[#a+1]=c end;step('capture package '..table.concat(captures,' '),a,'Captured',7300)end
-- the mirror-ray migration only rewrites the physics plan (no Unreal), so it runs before the --no-walk exit: a later
-- separate walk (run_street_city_motel_physics.lua, e.g. sliced) then uses a migrated plan (10-30: a --no-walk cook
-- left the plan unmigrated and slice 0 failed on a mirror-front glazing expectation 3.25 cm off)
step('mirror migration',{'Tools/migrate_street_city_motel_mirror_rays.lua'},nil,1800)
for i=1,#arg do if arg[i]=='--no-walk' then log('TRIAL DONE (no walk)');os.exit(0)end end
step('quick physics',{'Tools/run_street_city_motel_physics.lua','package','0'},'passed',3600)
-- The opening normal-input smoke (W / brake / S / Shift / jump) checks positions after fixed wall-clock waits (0.3-1.2 s);
-- one long start-up frame lets a key release land a frame late and fails it ("walk_did_not_brake",
-- "Shift_did_not_increase_walk_speed") before any route is walked. Only that failure gets one more run; both receipts stay.
do
 log('== full walk');local good,out=lua({'Tools/run_street_city_motel_physics.lua','package','0','all'},7300)
 if not (good and out:find('passed',1,true)) then
  local latest=j.decode(read(m..'/physics-latest.json') or '{}');local native=latest.native_receipt and j.decode(read(latest.native_receipt) or '{}') or {}
  if native.error=='normal_player_input_smoke_failed' and (native.completed_walk_routes or 0)==0 then
   log('input smoke failed before walking ('..tostring(native.normal_input_error)..', '..latest.native_receipt..'); one more run')
   good,out=lua({'Tools/run_street_city_motel_physics.lua','package','0','all'},7300)
  end
 end
 if not (good and out:find('passed',1,true)) then log('FAILED full walk\n'..out:sub(-1500));os.exit(1) end
 log((out:match('([^\r\n]+)%s*$') or ''))
end
log('ACCEPTANCE DONE')
