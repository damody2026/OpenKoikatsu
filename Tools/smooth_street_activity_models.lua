-- Normals-only organic finishing. Geometry/UV fingerprints are verified on reimport.
package.path='Tools/lua/?.lua;'..package.path
local json=require('ok.json')
local base='C:/portable/OpenKoikatsu/output/street-social-city-production/activity-scenes-v1'
local code=[====[
import bpy,bmesh,sys,json,hashlib,shutil
from pathlib import Path
folder=Path('OUTDIR');path=folder/'remesh/remeshed-candidate.glb';report_path=folder/'remesh/remesh-report.json'
report=json.loads(report_path.read_text('utf8'))
assert report['status']=='passed' and hashlib.sha256(path.read_bytes()).hexdigest()==report['output_sha256']
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=str(path))
def signature():
 rows=[]
 for o in bpy.context.scene.objects:
  if o.type!='MESH':continue
  o.data.calc_loop_triangles()
  for t in o.data.loop_triangles:
   pts=sorted(tuple(round(float(v),5) for v in o.matrix_world@o.data.vertices[i].co) for i in t.vertices)
   uv=sorted(tuple(round(float(v),5) for v in o.data.uv_layers.active.data[i].uv) for i in t.loops) if o.data.uv_layers.active else []
   rows.append((pts,uv,t.material_index))
 return hashlib.sha256(json.dumps(sorted(rows)).encode()).hexdigest()
before=signature()
for o in bpy.context.scene.objects:
 if o.type=='MESH':
  bm=bmesh.new();bm.from_mesh(o.data);bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=0.000001);bm.to_mesh(o.data);bm.free()
  for polygon in o.data.polygons:polygon.use_smooth=True
  o.data.normals_split_custom_set([(0,0,0)]*len(o.data.loops))
assert signature()==before
target=path.with_name('smooth-candidate.glb')
bpy.ops.export_scene.gltf(filepath=str(target),export_format='GLB',export_extras=True)
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=str(target))
assert signature()==before,'Normals finishing changed geometry, UV or material assignments'
backup=path.with_name('before-smooth.glb')
if not backup.exists():shutil.copy2(path,backup)
shutil.copy2(target,path)
report['pre_normal_finishing_sha256']=report['output_sha256']
report['output_sha256']=hashlib.sha256(path.read_bytes()).hexdigest()
report['normal_finishing']={'operation':'smooth organic vertex normals','geometry_uv_material_signature_before':before,'geometry_uv_material_signature_after':signature(),'verified_unchanged':True}
report_path.write_text(json.dumps(report,ensure_ascii=False,indent=2),'utf8')
print('Normals-only finishing passed')
]====]
for _,id in ipairs({'bear','koi'})do
 local folder=base..'/components/'..id
 local script=code:gsub('OUTDIR',folder):gsub('\r?\n','\n')
 local payload=folder..'/smooth-expression.json';local f=assert(io.open(payload,'wb'));f:write(json.encode({code=script}));f:close()
 local expr="import json;exec(json.load(open('"..payload.."',encoding='utf8'))['code'])"
 assert(os.execute('""C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" --background --factory-startup --threads 8 --python-expr "'..expr..'" > "'..folder..'/smooth.log" 2>&1"'))
end
