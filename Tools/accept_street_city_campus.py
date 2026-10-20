"""Independent reimport: source identity, real doors, metric shells and stairs."""
import bpy,sys,json,argparse,hashlib,math,struct
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_campus_contract import context,digest
from street_city_remesh_gate import verify
from street_city_atomic import write_text
p=argparse.ArgumentParser();p.add_argument('--building',required=True);a=p.parse_args(sys.argv[sys.argv.index('--')+1:]);C=context(a.building);OUT=C['out'];report=json.loads((OUT/'assembly-report.json').read_text('utf8'));checks=[];errors=[]
def check(ok,name,**data):
 row=dict(name=name,passed=bool(ok),**data);checks.append(row)
 if not ok:errors.append(row)
check(report['contract_sha256']==digest(C),'current dimensioned programme')
for path,sha in report['recipe_sources'].items():
 check(hashlib.sha256(Path(path).read_bytes()).hexdigest()==sha,'recorded recipe '+Path(path).name)
 # The complete effective contract above pins every input from this adapter.
 # Its implementation may change for a different building without changing
 # this building's effective data. Geometry generators remain byte-pinned.
 if Path(path).name!='street_city_campus_contract.py':check(hashlib.sha256((Path(__file__).resolve().parent/Path(path).name).read_bytes()).hexdigest()==sha,'current canonical recipe '+Path(path).name)
for aid,source in report['sources'].items():verify(Path(source['remesh_report']).parent,source['sha256']);check(True,aid+' mandatory Rust receipt')
model=OUT/f'{C["id"]}-complete-2k.glb';bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=str(model));sc=bpy.context.scene;sc.frame_set(1);bpy.context.view_layer.update();meshes=[o for o in sc.objects if o.type=='MESH'];counts={}
for f in C['building']['floors']:
 no=f['floor'];local=[o for o in meshes if o.get('floor')==no];total=0;dry=0
 for o in local:
  o.data.calc_loop_triangles();n=len(o.data.loop_triangles);total+=n
  if o.get('part_kind')!='water':dry+=n
 check(total<=100000,f'F{no:02d} complete visual triangle budget',triangles=total);check(dry==report['floor_triangles'][str(no)],f'F{no:02d} actual triangle count');counts[no]=dry
 # An outward-opening entrance leaf is movable and can legitimately cross
 # the wall plane. Measure the immutable floor/wall envelope separately.
 points=[o.matrix_world@v.co for o in local if o.get('part_kind') in ('floor','wall') for v in o.data.vertices];W,D=C['building']['footprint_m'];bounds=[[min(v[k] for v in points) for k in range(3)],[max(v[k] for v in points) for k in range(3)]]
 check(abs(bounds[0][0]+W/2)<.006 and abs(bounds[1][0]-W/2)<.006 and abs(bounds[0][1]+D/2)<.006 and abs(bounds[1][1]-D/2)<.006,f'F{no:02d} exact exterior footprint',bounds=bounds)
for f in C['building']['floors']:
 no=f['floor'];data=(OUT/f'{C["id"]}-F{no:02d}-complete-2k.glb').read_bytes();check(data[:4]==b'glTF' and len(data)==struct.unpack_from('<I',data,8)[0],f'F{no:02d} complete GLB bytes');size=struct.unpack_from('<I',data,12)[0];gltf=json.loads(data[20:20+size]);mesh_counts=[sum(gltf['accessors'][p['indices']]['count']//3 for p in mesh['primitives']) for mesh in gltf['meshes']];actual=sum(mesh_counts[n['mesh']] for n in gltf['nodes'] if 'mesh' in n);check(actual==counts[no],f'F{no:02d} standalone floor actual emitted triangle count',triangles=actual)
for o in meshes:
 check(o.data.uv_layers.active is not None,o.name+' UV')
for image in bpy.data.images:
 if image.type=='IMAGE':check(max(image.size)<=2048,image.name+' <=2K')
def bvh(objects):
 vv=[];ff=[]
 for o in objects:
  offset=len(vv);vv.extend(o.matrix_world@v.co for v in o.data.vertices);ff.extend([offset+i for i in f.vertices] for f in o.data.polygons)
 return BVHTree.FromPolygons(vv,ff)
wall_tree=bvh([o for o in meshes if o.get('part_kind')=='wall']);solid_tree=bvh([o for o in meshes if o.get('part_kind') not in ('water','light','sign')])
for inst in C['instances']:
 asset=C['assets'][inst['asset_id']];name=asset['name'].lower()
 if asset['role']!='circulation' or not any(word in name for word in ['corridor','aisle','走廊','通道']):continue
 w,d,_=asset['size_m'];width=min(w,d);length=max(w,d);check(width>=1.999,asset['id']+' planned 2m corridor width',width_m=width)
 if width<1.999:continue
 angle=math.radians(inst['rotation_z_deg']);co,si=math.cos(angle),math.sin(angle);x,y,z=inst['position_m']
 for step in range(max(1,math.ceil(length))):
  along=-length/2+(step+.5)*length/max(1,math.ceil(length))
  for lane in [-.9,0,.9]:
   xx,yy=(along,lane) if w>=d else (lane,along);pt=Vector((x+co*xx-si*yy,y+si*xx+co*yy,z+.05))
   check(solid_tree.ray_cast(pt,Vector((0,0,1)),2.13)[0] is None,asset['id']+f' physical 2m standing corridor {step}/{lane}')
   check(solid_tree.ray_cast(pt,Vector((0,0,-1)),.08)[0] is not None,asset['id']+f' continuous floor support {step}/{lane}')
for d in report['doors']:
 x,y,z=d['position_m'];angle=math.radians(d['yaw_deg']);along=Vector((math.cos(angle),math.sin(angle),0));normal=Vector((-math.sin(angle),math.cos(angle),0));center=Vector((x,y,z));half=d['width_m']/2
 for sign in [-1,1]:
  p=center+along*sign*(half+.12)+Vector((0,0,1.2));check(wall_tree.ray_cast(p-normal*.8,normal,1.6)[0] is not None,d['id']+f' mounted jamb {sign}')
 p=center+Vector((0,0,d['height_m']+.14));check(wall_tree.ray_cast(p-normal*.8,normal,1.6)[0] is not None,d['id']+' mounted lintel')
 closed=d.get('kind')=='lift' and d['floor']>1
 for lane in ([-d['width_m']/3,d['width_m']/3] if closed else [-d['width_m']/3,0,d['width_m']/3]):
  for height in [.2,1.1,2]:
   p=center+along*lane+Vector((0,0,height));hit=solid_tree.ray_cast(p-normal*.4,normal,.8)[0]
   check(hit is not None if closed else hit is None,d['id']+f' {"closed barrier" if closed else "clear opening"} {lane}/{height}')
for f in C['building']['floors']:
 if f['floor']==len(C['building']['floors']):continue
 for core in [c for c in f['vertical_circulation'] if c['kind']=='stairs']:
  g=core['walkable_geometry'];per=g['riser_count']//g['flight_count'];rise=f['height_m']/g['riser_count'];run=g['flight_run_m'];cx,cy,z=core['position_m']
  for flight in range(g['flight_count']):
   xx=cx+(-.9 if flight%2==0 else .9);sign=1 if flight%2==0 else -1
   for step in range(per-1):
    yy=cy+sign*(-run/2+(step+.5)*g['tread_depth_m']);zz=z+(flight*per+step+1)*rise
    for edge in [-.6,0,.6]:
     start=Vector((xx+edge,yy,zz+.02));hit=solid_tree.ray_cast(start,Vector((0,0,-1)),.04)[0];check(hit is not None,core['id']+f' walkable tread {flight}/{step}/{edge}')
     hit=solid_tree.ray_cast(start,Vector((0,0,1)),2.18)[0];check(hit is None,core['id']+f' standing headroom {flight}/{step}/{edge}')
for lift in report['lifts']:
 car=bpy.data.objects[lift['root']]
 for frame,stop in zip(lift['frames'],lift['stops_m']):
  sc.frame_set(frame);bpy.context.view_layer.update();check(abs(car.matrix_world.translation.z-stop)<.002,lift['root']+f' actual animated stop {frame}')
  no=lift['frames'].index(frame)+1
  doors_at_stop=[d for d in report['doors'] if d.get('kind')=='lift' and d['floor']==no and f'-E{lift["core"]}1-' in d['id']]
  check(len(doors_at_stop)==1,lift['root']+f' exactly one landing at stop {frame}')
  for door in doors_at_stop:
   angle=math.radians(door['yaw_deg']);along=Vector((math.cos(angle),math.sin(angle),0));center=Vector(door['position_m'])
   for sign in [-1,1]:
    panel=bpy.data.objects[door['id']+f'-panel{sign}'];points=[panel.matrix_world@v.co for v in panel.data.vertices];check(min(abs((pt-center).dot(along)) for pt in points)>=door['width_m']/2-.002,door['id']+f' actual landing panels open at stop {frame}/{sign}')
sc.frame_set(1)
result=dict(status='passed' if not errors else 'failed',building=C['id'],checks=len(checks),errors=errors,sha256=hashlib.sha256(model.read_bytes()).hexdigest(),verifier_source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),floor_triangles=counts,results=checks)
write_text(OUT/'acceptance.json',json.dumps(result,ensure_ascii=False,indent=2));print(C['id'],result['status'],len(checks),len(errors))
if not errors:
 report.update(status='passed',approved=True,acceptance_sha256=hashlib.sha256((OUT/'acceptance.json').read_bytes()).hexdigest());write_text(OUT/'assembly-report.json',json.dumps(report,ensure_ascii=False,indent=2))
if errors:raise RuntimeError(str(errors[:12]))
