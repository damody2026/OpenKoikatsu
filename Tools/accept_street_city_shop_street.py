"""Reimport the combined world; test placement, bounds, topology counts and provenance."""
import bpy,json,sys,math,argparse
from mathutils import Vector
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_shop_street import OUT
from street_city_remesh_gate import sha,verify
from street_city_ray_index import RayIndex
p=argparse.ArgumentParser();p.add_argument('--smoke',action='store_true');p.add_argument('--output');a=p.parse_args(sys.argv[sys.argv.index('--')+1:]);OUT=Path(a.output) if a.output else OUT/'smoke' if a.smoke else OUT;r=json.loads((OUT/'world-assembly.json').read_text('utf-8'));path=OUT/'shop-street-complete.glb';checks=[];errors=[]
def check(ok,name,**extra):
 checks.append(dict(name=name,passed=bool(ok),**extra))
 if not ok:errors.append(name)
check(sha(path)==r['glb_sha256'],'exact combined GLB version')
check(len(r['buildings'])==(2 if a.smoke else 96),'required building count')
bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=str(path));sc=bpy.context.scene;sc.frame_set(1);bpy.context.view_layer.update();roots={o['building']:o for o in sc.objects if o.get('building')};check(set(roots)=={b['building'] for b in r['buildings']},'all canonical building roots imported');bounds=[];source_plans={}
check(not any(o.type in ('CAMERA','LIGHT') for o in sc.objects),'overview camera/light excluded from game GLB')
for b in r['buildings']:
 bid=b['building'];root=roots[bid];source=Path(b['source']);check(sha(source)==b['sha256'],bid+' accepted source version');source_plan=json.loads((source.parent/'assembly-report.json').read_text('utf-8'));acc=json.loads((source.parent/'acceptance.json').read_text('utf-8'));check(source_plan.get('approved') and acc['status']=='passed' and acc['sha256']==b['sha256'],bid+' source remains approved')
 source_plans[bid]=source_plan
 for aid,s in source_plan['sources'].items():
  try:verify(Path(s['remesh_report']).parent,s['sha256']);check(True,bid+'/'+aid+' remesh provenance')
  except Exception as error:check(False,bid+'/'+aid+' remesh provenance',error=str(error))
 check(max(abs(x-y) for x,y in zip(root.matrix_world.translation,b['position_m']))<.002,bid+' world position')
 yaw=root.matrix_world.to_euler().z;delta=(yaw-math.radians(b['rotation_z_deg'])+math.pi)%(2*math.pi)-math.pi;check(abs(delta)<1e-5,bid+' world rotation')
 meshes=[o for o in root.children_recursive if o.type=='MESH'];inverse=root.matrix_world.inverted();points=[inverse@(o.matrix_world@v.co) for o in meshes for v in o.data.vertices]
 for k,target in enumerate(b['footprint_m']):check(abs(max(v[k] for v in points)-min(v[k] for v in points)-target)<.002,bid+f' indoor/outdoor exact envelope axis{k}')
 counts={}
 for floor,expected in b['floor_triangles'].items():
  total=0
  for obj in [o for o in meshes if o.get('floor')==int(floor)]:obj.data.calc_loop_triangles();total+=len(obj.data.loop_triangles)
  counts[floor]=total;check(total==expected and total<=100000,bid+f'/F{floor} unchanged geometry budget',actual=total,expected=expected)
 world=[o.matrix_world@v.co for o in meshes for v in o.data.vertices];bounds.append(dict(id=bid,rect=[min(v.x for v in world),min(v.y for v in world),max(v.x for v in world),max(v.y for v in world)]));print(bid,'world geometry verified',flush=True)
for i,b in enumerate(bounds):
 for other in bounds[i+1:]:
  aa=b['rect'];bb=other['rect'];overlap=min(aa[2],bb[2])-max(aa[0],bb[0])>.002 and min(aa[3],bb[3])-max(aa[1],bb[1])>.002;check(not overlap,b['id']+'/'+other['id']+' physical envelopes do not overlap')
for image in bpy.data.images:
 if image.size[0]:check(max(image.size)<=2048,'world 2K '+image.name,pixels=list(image.size))
# Test entry through the wall thickness, in addition to each local door-width
# test. Three lanes must be clear with the elevator parked at that storey.
for floor,frame in [(1,1),(2,80),(3,160)]:
 print('Checking actual door traversals on floor',floor,flush=True)
 sc.frame_set(frame);bpy.context.view_layer.update();dg=bpy.context.evaluated_depsgraph_get();ray_index=RayIndex(sc,dg)
 for bid,plan in source_plans.items():
  root=roots[bid]
  for door in [d for d in plan['doors'] if d['floor']==floor]:
   if 'position_m' in door:
    center=Vector(door['position_m']);yaw=math.radians(door['yaw_deg']);tangent=Vector((math.cos(yaw),math.sin(yaw),0));height=door['height_m']
   else:
    center=Vector([*door['center_m'],plan['lift']['stops'][floor-1]]);tangent=Vector((1,0,0) if door['axis']==0 else (0,1,0));height=2.4
   normal=Vector((-tangent.y,tangent.x,0));direction=(root.matrix_world.to_3x3()@normal).normalized()
   for lane in [-door['width_m']/4,0,door['width_m']/4]:
    for h in [.15,1,min(2.1,height-.1)]:
     local=center+tangent*lane-normal*.45+Vector((0,0,h));origin=root.matrix_world@local;obj=ray_index.cast(origin,direction,.9)
     if a.smoke:
      hit,*_=sc.ray_cast(dg,origin,direction,distance=.9);assert bool(obj)==hit,'Spatial filter differs from complete-scene ray cast'
     check(obj is None,bid+'/'+door['id']+f' walk through actual opening lane{lane}@{h}',blocker=obj.name if obj else None)
result=dict(status='passed_smoke_only' if a.smoke and not errors else 'passed' if not errors else 'failed',checks=len(checks),errors=errors,glb_sha256=sha(path),building_count=len(roots),results=checks,scope='Actual combined GLB reimport; source/remesh identity, positions, rotations, indoor/outdoor bounds, per-floor triangle counts, texture sizes and building overlap')
(OUT/'world-acceptance.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),'utf-8');print(result['status'],len(checks),len(errors))
if not errors and not a.smoke:
 r['status']='passed_world_export_and_reimport';(OUT/'world-assembly.json').write_text(json.dumps(r,ensure_ascii=False,indent=2),'utf-8')
if errors:raise RuntimeError(f'{len(errors)} combined-world checks failed')
