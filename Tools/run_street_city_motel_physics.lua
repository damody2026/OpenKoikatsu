package.path='Tools/lua/?.lua;'..package.path
local ok=require('ok');local j=require('ok.json');local h=require('ok.hash');local root='C:/portable/OpenKoikatsu';local out=root..'/output/street-social-city-production/motel-v1';local source=arg[1]=='source';local seconds=tonumber(arg[2] or '0');assert(seconds and seconds>=0 and seconds<=4000)
-- ok-luakit can fail to start right after a 30-minute d3d12 game exits (0xC0000142, DLL init); retry so a passed walk is not lost.
do local raw=h.sha256_file;h={sha256_file=function(p)for i=1,4 do local good,v=pcall(raw,p);if good then return v end;if i==4 then error(v)end;ok.proc.run({'powershell.exe','-NoProfile','-Command','Start-Sleep -Seconds 10'},{hide=true,timeout=40})end end}end
local function read(p)local f=assert(io.open(p,'rb'));local r=j.decode(f:read('*a'));f:close();return r end
local function write(p,r)local f=assert(io.open(p,'wb'));f:write(j.encode(r));f:close()end
local plan=read(out..'/game-physics-plan.json');local map=root..'/StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap';local sha=h.sha256_file(map);assert(plan.map_sha256==sha)
if arg[3]=='stairs' then local subset={};for _,r in ipairs(plan.walk_routes)do if r.kind=='motel_stair'then subset[#subset+1]=r end end;assert(#subset==6);plan.walk_routes=subset end
-- 'slice <k> <n>': walk only routes whose 1-based index i has (i-1)%n==k (k in 0..n-1), so a low-priority session can
-- finish the full walk in n short runs between other sessions' Unreal turns; acceptance = all n slices passed with 0
-- assists on the same package and map
local sliced=arg[3]=='slice'
if arg[3]=='slice' then local k,n=tonumber(arg[4]),tonumber(arg[5]);assert(k and n and n>=1 and k>=0 and k<n,'slice <k> <n>')
 local subset={};for i,r in ipairs(plan.walk_routes)do if (i-1)%n==k then subset[#subset+1]=r end end;assert(#subset>0);plan.walk_routes=subset;arg[3]='all'
end
-- 'shops': walk every refitted shop door instead (2.5 m outside -> door -> 3 m in -> aisle before the core), same route
-- frame as Tools/street_city_shop_survey_payload.lua shop_walk_sweep; floor heights from its accepted floor rays.
if arg[3]=='shops' then
 local sv=root..'/output/street-social-city-production/shop-survey-v1';local probe,sweep={},{}
 for _,a in ipairs(read(sv..'/shop-probe.json').actors)do probe[a.actor]=a end
 for _,r in ipairs(read(sv..'/shop-walk-sweep.json').rows)do assert(r.ok,r.id);sweep[r.id]=r end
 local routes={}
 for _,p in ipairs({'shop-refits-v1/refits-build.json','shop-samples-v1/samples-build.json'})do for _,b in ipairs(read(root..'/output/street-social-city-production/'..p).shops)do if not b.special and sweep[b.id]then
  local a=probe[b.id..'_F01'];local yaw=math.rad(a.yaw);local c,s=math.cos(yaw),math.sin(yaw);local dz=sweep[b.id].floor_dz_cm
  local path={{b.door_x,-b.H-2.5},{b.door_x,-b.H+.5},{b.door_x,-b.H+3.0},{b.door_x,b.core_y-1.0}};local pts={}
  local function W(x,y,z)local lx,ly=x*100,-y*100;return {a.location[1]+lx*c-ly*s,a.location[2]+lx*s+ly*c,z}end
  for i=2,#path do local p0,p1=path[i-1],path[i];local z0,z1=a.location[3]+dz[i-1]+6,a.location[3]+dz[i]+6;local n=math.max(1,math.ceil(math.abs(p1[2]-p0[2])/.5))
   for k=(i==2 and 0 or 1),n do local t=k/n;pts[#pts+1]=W(p0[1],p0[2]+(p1[2]-p0[2])*t,z0+(z1-z0)*t)end end
  routes[#routes+1]={id='shop-'..b.id..'-entry',kind='shop_entry',points=pts}
 end end end
 assert(#routes==94,#routes);plan.walk_routes=routes
end
if arg[3]=='shops' then arg[3]='all' end
local package=source and {id='uncooked-source',engine='E:/PhysX/UE5.8',host='E:/StreetSocialCityProduction/environment-packages/20261023-145531/HostProject'} or read(out..'/package.json');if not source then assert(package.map_sha256==sha and package.exit_code==0)end
local paks={}
-- pak SHA-256 cache keyed by path + size + mtime: hashing the 19 GB pak took ~17 min before and again after every walk,
-- holding an Unreal slot with nothing running; an unchanged file (same size and mtime) reuses its recorded hash
local lfs=require('lfs');local cachepath=out..'/pak-sha-cache.json'
local cache=(function()local f=io.open(cachepath,'rb');if not f then return {}end;local r=j.decode(f:read('*a'));f:close();return r end)()
local function pak_sha(path)local a=assert(lfs.attributes(path));local key=path..'|'..a.size..'|'..a.modification
 if not cache[key] then cache[key]=h.sha256_file(path);write(cachepath,cache)end;return cache[key]end
if not source then
 local listing=ok.proc.run({'rg','--files','-uuu',package.target..'/Stage/Windows/StreetSocialCity/Content/Paks'},{capture=true,hide=true});assert(listing.code==0)
 for path in listing.stdout:gmatch('[^\r\n]+')do path=path:gsub('\\','/');assert(path:sub(1,#package.target)==package.target);if path:match('%.pak$')then paks[#paks+1]={path=path,sha256=pak_sha(path)}end end;assert(#paks>0)
end
local length=0;for _,r in ipairs(plan.walk_routes)do for i=2,#r.points do local a,b=r.points[i-1],r.points[i];length=length+math.sqrt((a[1]-b[1])^2+(a[2]-b[2])^2)end end
-- a slice keeps only the full-traversal margin (300 s + 10) instead of the 650 s soak base, so n slices cost about one walk
if arg[2]=='0'and arg[3]=='all' then seconds=math.ceil(length/140+(sliced and 310 or 650))end
local exe=source and package.engine..'/Engine/Binaries/Win64/UnrealEditor.exe' or package.target..'/Stage/Windows/StreetSocialCity/Binaries/Win64/StreetSocialCity.exe';local exesha=h.sha256_file(exe)
-- Frames per second over the first >=3 s after the control channel opens (log line frame counter / elapsed time).
local function opening_fps(p)local f=io.open(p,'rb');if not f then return nil end;local t0
 for l in f:lines()do local H,M,S,ms,fr=l:match('^%[%d+%.%d+%.%d+%-(%d+)%.(%d+)%.(%d+):(%d+)%]%[%s*(%d+)%]')
  if H then local t=H*3600+M*60+S+ms/1000;if t0 then if t-t0>=3 then f:close();return tonumber(fr)/(t-t0)end elseif l:find('Control listening',1,true)then t0=t end end end
 f:close();return nil end
-- The native input smoke test waits wall-clock time (0.3 s after releasing W, etc.) and starts 2 s after load, while
-- PSOs still compile and other Unreal games share the GPU; at 5-8 fps one frame passes and the release is checked
-- before it takes effect (STREET_ERRORS 2026-11-01). Relaunch up to 3 times for those failures at a low opening rate.
local timing_errors={walk_did_not_brake=true,Space_did_not_jump=true};local attempts={}
local id,prefix,planpath,plansha,receipt,log,result,report
for attempt=1,4 do
 id=os.date('%Y%m%d-%H%M%S');prefix=out..'/'..(source and 'source-' or '')..'physics-'..id;planpath=prefix..'-plan.json';write(planpath,plan);plansha=h.sha256_file(planpath);receipt=prefix..'.json';log=prefix..'.log';local args={exe}
 if source then args[#args+1]=package.host..'/StreetSocialCity.uproject';args[#args+1]='-game'end
 for _,v in ipairs({'/Game/UniversityCity/Maps/UniversityCity',seconds>0 and '-d3d12' or '-nullrhi','-CityImmersionAudit='..receipt,'-CityImmersionPlan='..planpath,'-CityImmersionSoakSeconds='..seconds,'-unattended','-nosplash','-nosound','-windowed','-ResX=1920','-ResY=1080','-NoVSync','-norhithread','-UserDir='..root..'/Saved/StreetSocialCityMotelPhysicsUser-'..id,'-abslog='..log,'-DDC=(Local)','-LocalDataCachePath=E:/StreetSocialCityProduction/unreal-ddc/FileSystem','-ExecCmds=t.MaxFPS 60'})do args[#args+1]=v end
 write(out..'/physics-running.json',{status='running',source=source,package_id=package.id,map_sha256=sha,native_receipt=receipt,log=log,soak_seconds=seconds,expected_walk_routes=#plan.walk_routes,length_m=length/100,attempt=attempt})
 result=ok.proc.run(args,{hide=true,cwd=root,stdout_file=log..'.stdout',stderr_file=log..'.stderr',timeout=seconds+1500});report=read(receipt)
 local fps=opening_fps(log);local low=fps and fps<12;attempts[#attempts+1]={receipt=receipt,opening_fps=fps,error=report.error,input_error=report.normal_input_error}
 print(('attempt %d: opening %.1f fps, %s %s'):format(attempt,fps or -1,tostring(report.error or report.status),tostring(report.normal_input_error or '')))
 if not(report.error=='normal_player_input_smoke_failed' and timing_errors[report.normal_input_error] and low and attempt<4)then break end
end
local unchanged=sha==h.sha256_file(map)and plansha==h.sha256_file(planpath)and exesha==h.sha256_file(exe);for _,p in ipairs(paks)do unchanged=unchanged and pak_sha(p.path)==p.sha256 end;local full=seconds>=length/140+300
-- Full traversal: every planned route id must appear in the native completed records (a count alone can hide repeats).
local done={};for _,r in ipairs(report.completed_route_records or {})do done[r.route]=true end;local missing={}
for _,r in ipairs(plan.walk_routes)do if not done[r.id]then missing[#missing+1]=r.id end end
local coverage=not full or #missing==0;local assists=report.walking_step_assists or 0
local images={};for _,v in ipairs(report.soak_images or {})do local f=assert(io.open(v.path,'rb'),'Missing native soak image '..tostring(v.path));local header=f:read(24);f:close();assert(header:sub(1,8)=='\137PNG\r\n\26\n');local w,hgt=string.unpack('>I4I4',header,17);assert(w==1920 and hgt==1080);images[#images+1]={path=v.path,sha256=h.sha256_file(v.path),width=w,height=hgt}end
local passed=result.code==0 and unchanged and report.world_type=='Game' and report.status=='passed' and report.cooked_physics_passed and report.supplied_plan_map_sha256==sha and report.normal_input_smoke_passed and report.player_start_passed and coverage and (not full or assists==0)
local launch={status=passed and 'passed' or 'failed',source=source,package_id=package.id,map_sha256=sha,native_receipt=receipt,log=log,exit_code=result.code,attempts=attempts,source_unchanged=unchanged,immutable_stage_unchanged=unchanged,paks=paks,soak_seconds=seconds,expected_walk_routes=#plan.walk_routes,completed_walk_routes=report.completed_walk_routes,unique_completed_routes=#plan.walk_routes-#missing,missing_routes=missing,walking_step_assists=assists,native_images_manifest=images,full_route_expected=full,coverage_passed=coverage,executable_sha256=h.sha256_file(exe),scope=source and 'Uncooked native Game source iteration; final staged replay remains required.' or 'Actual packaged Game physics, normal input smoke test and optional CharacterMovement walking. Full traversal claimed only when route count passes.'};write(prefix..'-launch.json',launch);write(out..(source and '/source-physics-latest.json' or '/physics-latest.json'),launch);assert(passed,'Motel physics requires repair: '..receipt);print('Native motel physics passed: '..receipt)
