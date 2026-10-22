package.path='Tools/lua/?.lua;'..package.path
local json=require('ok.json')
local root='C:/portable/OpenKoikatsu'
local batch=tonumber(arg[3]) or 2
local base=root..'/output/street-social-city-production/city-destinations-v'..batch
local function read(p)local f=assert(io.open(p,'rb'));local s=f:read('*a');f:close();return json.decode(s)end
local function write(p,v)local f=assert(io.open(p,'wb'));f:write(json.encode(v));f:close()end
local plan=read(base..'/plan.json');local first=tonumber(arg[1]) or 1;local last=tonumber(arg[2]) or #plan.assets
local normalize=[====[
import bpy,bmesh,json,hashlib,traceback
from pathlib import Path
from mathutils import Vector
q=TASK['spec'];folder=Path(TASK['folder']);source=Path(TASK['source']);status=folder/'normalize-status.json'
try:
 bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=str(source));bpy.context.view_layer.update();objects=[o for o in bpy.context.scene.objects if o.type=='MESH'];assert objects
 points=[o.matrix_world@v.co for o in objects for v in o.data.vertices];lo=Vector(tuple(min(p[k] for p in points) for k in range(3)));hi=Vector(tuple(max(p[k] for p in points) for k in range(3)));assert hi.z>lo.z
 scale=q['height_m']/(hi.z-lo.z);origin=Vector(((lo.x+hi.x)/2,(lo.y+hi.y)/2,lo.z));materials=set()
 for o in objects:
  matrix=o.matrix_world.copy();o.parent=None;o.matrix_world.identity()
  for v in o.data.vertices:v.co=(matrix@v.co-origin)*scale
  bm=bmesh.new();bm.from_mesh(o.data);bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=1e-6);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(o.data);bm.free()
  for p in o.data.polygons:p.use_smooth=True
  for m in o.data.materials:
   if m:materials.add(m)
 for m in materials:
  if m.use_nodes:
   for n in m.node_tree.nodes:
    if n.type=='TEX_IMAGE' and n.image:
     image=n.image;w,h=image.size
     if max(w,h)>2048:image.scale(round(w*2048/max(w,h)),round(h*2048/max(w,h)))
     image.pack()
 target=folder/'normalized-source.glb';bpy.ops.export_scene.gltf(filepath=str(target),export_format='GLB',export_extras=True)
 report=dict(status='passed',id=q['id'],source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),source=str(source),normalized_size_m=list((hi-lo)*scale),uniform_scale=scale,height_m=q['height_m'],material_count=len(materials),output_sha256=hashlib.sha256(target.read_bytes()).hexdigest())
 status.write_text(json.dumps(report,ensure_ascii=False,indent=2),'utf8')
except BaseException:
 status.write_text(json.dumps(dict(status='failed',error=traceback.format_exc())),'utf8');raise
]====]
local blender='C:/Program Files/Blender Foundation/Blender 5.2/blender.exe'
for i=first,math.min(last,#plan.assets)do
 local q=plan.assets[i];local folder=base..'/components/'..q.id
 local source='C:/Users/Damody/Downloads/'..q.id..'.glb'
 if batch==2 and i<=2 then source='C:/Users/Damody/Downloads/'..q.id..'-'..q.slug..'.glb' end
 local f=io.open(source,'rb');assert(f,'Download missing: '..source);local magic=f:read(4);f:close();assert(magic=='glTF')
 assert(os.execute('if not exist "'..folder..'" mkdir "'..folder..'"'))
 local done=io.open(folder..'/remesh/remesh-report.json','rb');local report
 if done then report=json.decode(done:read('*a'));done:close()end
 if report and report.status=='passed' then print(q.id..' already remeshed')
 else
  local task={spec=q,folder=folder,source=source,code=normalize};write(folder..'/normalize-payload.json',task)
  local expr="import json;TASK=json.load(open('"..folder.."/normalize-payload.json',encoding='utf8'));exec(TASK['code'])"
  assert(os.execute('""'..blender..'" --background --factory-startup --threads 8 --python-expr "'..expr..'" > "'..folder..'/normalize.log" 2>&1"'))
  assert(read(folder..'/normalize-status.json').status=='passed')
  local cap=q.triangle_cap;local quad=true
  local function remesh(extra,tag)
   local cmd='""'..blender..'" --background --factory-startup --threads 8 --python Tools/run_street_city_blender_task.py -- --script Tools/remesh_street_city_asset.py --status-file "'..folder..'/'..tag..'-status.json" -- --source "'..folder..'/normalized-source.glb" --output "'..folder..'/remesh" --triangle-cap '..cap..' --tolerance 0.015 '..(quad and '--quad-mode ' or '')..'--project-source --dense-pass 3 --threads 8 '..extra..' > "'..folder..'/'..tag..'.log" 2>&1"'
   local success=os.execute(cmd)
   if not success and not extra:find('--recover-source-topology',1,true) then
    local f=assert(io.open(folder..'/'..tag..'.log','rb'));local log=f:read('*a');f:close()
    if log:find('EXCEPTION_ACCESS_VIOLATION',1,true) then
     local archive=assert(io.open(folder..'/'..tag..'-blender-crash-'..os.date('%Y%m%d-%H%M%S')..'.log','wb'));archive:write(log);archive:close()
     write(folder..'/'..tag..'-recovery-fallback.json',{reason='Blender projected thin-surface subdivision crashed; explicit reference connectivity with mandatory Rust field sampling',original_process_failed=true})
     success=os.execute(cmd:gsub('%-%-project%-source','--recover-source-topology --project-source',1))
    end
   end
   assert(success,q.id..' remesh process failed');assert(read(folder..'/'..tag..'-status.json').status=='succeeded')
  end
  remesh('','remesh');report=read(folder..'/remesh/remesh-report.json')
  if report.status~='passed' then write(folder..'/remesh/first-rejected-report.json',report);remesh('--recover-source-topology','remesh-repair');report=read(folder..'/remesh/remesh-report.json')end
  if report.status~='passed' and report.triangles>cap and report.triangles<=30000 then
   write(folder..'/remesh/rejected-preferred-budget.json',report)
   cap=math.ceil(report.triangles/1000)*1000
   write(folder..'/budget-adjustment.json',{preferred=q.triangle_cap,approved_maximum=30000,effective=cap,reason='Preserve measured thin floral/organic features; three native LODs remain required; not accepted without geometric validation'})
   remesh('--recover-source-topology','remesh-budget-repair');report=read(folder..'/remesh/remesh-report.json')
  end
  if report.status~='passed' then
   local failed=false;for _,r in ipairs(report.records)do if r.status=='tool_failed' then failed=true end end
   if failed then write(folder..'/remesh/rejected-quad-extraction.json',report);quad=false;cap=30000;remesh('--recover-source-topology','remesh-triangle-extraction');report=read(folder..'/remesh/remesh-report.json')end
  end
  assert(report.status=='passed',q.id..' needs geometric repair')
  print(q.id..' remesh passed '..report.triangles)
 end
 write(base..'/preparation-progress-'..first..'-'..last..'.json',{status='running',last=q.id,index=i,preferred_triangle_cap=q.triangle_cap,actual_triangles=report.triangles})
end
print('Prepared requested destination assets')
