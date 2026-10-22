package.path='Tools/lua/?.lua;'..package.path
local json=require('ok.json')
local id=assert(arg[1],'bear or koi')
local base='C:/portable/OpenKoikatsu/output/street-social-city-production/activity-scenes-v1'
local spec={bear={size={2.25,1.9,2.8},triangle_cap=30000},koi={size={.45,.14,.18},triangle_cap=5000}}
local s=assert(spec[id]);s.id=id
local function write(p,v)local f=assert(io.open(p,'wb'));f:write(json.encode(v));f:close()end
local folder=base..'/components/'..id
assert(os.execute('if not exist "'..folder..'" mkdir "'..folder..'"'))
write(folder..'/catalog.json',{components={s}})
local blender='C:/Program Files/Blender Foundation/Blender 5.2/blender.exe'
local source='C:/Users/Damody/Downloads/ACT-'..(id=='bear' and 'giant-teddy' or 'koi')..'-source.glb'
local function run(script,args,tag)
 local cmd='""'..blender..'" --background --factory-startup --threads 8 --python Tools/run_street_city_blender_task.py -- --script Tools/'..script..' --status-file "'..folder..'/'..tag..'-status.json" -- '..args..' > "'..folder..'/'..tag..'.log" 2>&1"'
 assert(os.execute(cmd),tag..' failed')
end
run('prepare_street_city_generated_component.py','--id '..id..' --source "'..source..'" --catalog "'..folder..'/catalog.json" --output "'..folder..'" --lod-triangles '..s.triangle_cap,'normalize')
run('remesh_street_city_asset.py','--source "'..folder..'/normalized-source.glb" --output "'..folder..'/remesh" --triangle-cap '..s.triangle_cap..' --quad-mode --project-source --dense-pass 3 --threads 8','remesh')
local f=assert(io.open(folder..'/remesh/remesh-report.json','rb'));local r=json.decode(f:read('*a'));f:close()
if r.status~='passed' then
 -- Keep the failed solver output and disclose reference-topology reconstruction.
 local f=assert(io.open(folder..'/remesh/first-rejected-report.json','wb'));f:write(json.encode(r));f:close()
 run('remesh_street_city_asset.py','--source "'..folder..'/normalized-source.glb" --output "'..folder..'/remesh" --triangle-cap '..s.triangle_cap..' --quad-mode --project-source --dense-pass 3 --threads 8 --recover-source-topology','remesh-repair')
 f=assert(io.open(folder..'/remesh/remesh-report.json','rb'));r=json.decode(f:read('*a'));f:close()
end
assert(r.status=='passed','Remesh candidate requires repair: '..folder)
print(id..' remesh passed '..r.triangles)
