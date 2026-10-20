"""Reimport entire exported building and inspect actual neighbouring floors."""
import bpy,json,math,hashlib,struct,argparse,sys
from pathlib import Path
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'Tools'))
from street_city_building_context import context
from street_city_retail_profiles import placements as retail_placements
from street_city_remesh_gate import verify as verify_remesh
from collections import Counter
p=argparse.ArgumentParser();p.add_argument('--building',default='S01');p.add_argument('--report-dir',type=Path);args=p.parse_args(sys.argv[sys.argv.index('--')+1:]);C=context(args.building);BID=C['id']
BASE=ROOT/'output/street-social-city-production/S01-modular-components-v1';OUT=C['out'];source=OUT/f'{BID}-complete-2k.glb';plan=json.loads((OUT/'assembly-report.json').read_text('utf-8'))
REVIEW=args.report_dir or OUT;REVIEW.mkdir(parents=True,exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=str(source));sc=bpy.context.scene;sc.frame_set(0);bpy.context.view_layer.update();meshes=[o for o in sc.objects if o.type=='MESH'];checks=[];errors=[]
def check(ok,name,**value):
 checks.append(dict(name=name,passed=bool(ok),**value))
 if not ok:errors.append(name)
def pts(os):return [o.matrix_world@v.co for o in os for v in o.data.vertices]
from street_city_shop_wall_styles import verify_export as verify_wall_export
verify_wall_export(sc,BID,plan,check)
allpts=pts(meshes)
for k,target in [(0,C['width']),(1,C['depth'])]:check(abs(max(q[k] for q in allpts)-min(q[k] for q in allpts)-target)<.002,f'building envelope axis{k}',actual=max(q[k] for q in allpts)-min(q[k] for q in allpts))
check(abs(max(q.z for q in allpts)-11)<.002,'sealed roof top Z11')
totals={}
for floor in [1,2,3]:
 os=[o for o in meshes if o.get('floor')==floor];n=0
 for o in os:o.data.calc_loop_triangles();n+=len(o.data.loop_triangles)
 totals[floor]=n;check(n<=100000,f'F{floor:02d} actual triangle limit',triangles=n)
 expected_assets=[('toilet',1),('sink',2 if floor in (1,3) else 1),('table',4 if floor<3 else 1),('chair',12 if floor==1 else 16 if floor==2 else 1),('stool',4 if floor==1 else 0),('ceiling-light',6),('vent-grille',1)] if BID=='S01' else [('toilet',1),('sink',1),('table',1 if floor==3 else 0),('chair',1 if floor==3 else 0),('ceiling-light',6),('vent-grille',1),('double-sink',1 if floor<3 else 0),('handwash',1 if floor in (1,3) else 0),('cup-sealer',1 if floor==1 else 0),('bed',1 if floor==3 else 0)]
 if BID not in ('S01','S02'):
  counter=Counter(q['asset'] for q in retail_placements(C)[0] if q['floor']==floor);counter.update({'toilet':1,'sink':1,'ceiling-light':6,'vent-grille':1});expected_assets=list(counter.items())
 for aid,expected in expected_assets:
  roots=[o for o in sc.objects if o.type=='EMPTY' and o.get('floor')==floor and o.get('asset_id')==aid];check(len(roots)==expected,f'F{floor:02d} {aid} count',actual=len(roots),expected=expected)
 for root in [o for o in sc.objects if o.type=='EMPTY' and o.get('floor')==floor and o.get('asset_id') in ('chair','toilet','sink')]:
  local=[root.matrix_world.inverted()@q for q in pts([c for c in root.children_recursive if c.type=='MESH'])];size=[max(q[k] for q in local)-min(q[k] for q in local) for k in range(3)];target={'chair':[.48,.52,.85],'toilet':[.4,.72,1],'sink':[.6,.5,1]}[root['asset_id']]
  check(max(abs(x-y) for x,y in zip(size,target))<.002,root.name+' dimensions',size=size)
for image in bpy.data.images:
 if image.size[0]:check(max(image.size)<=2048,'2K image '+image.name,pixels=list(image.size))
if BID!='S01':
 for role in ['floor','wet-wall','paint']:
  matches=[m for m in bpy.data.materials if m.get('finish_role')==role]
  check(bool(matches),'exported PBR finish '+role)
  for mat in matches:
   nodes=mat.node_tree.nodes;bs=next(n for n in nodes if n.type=='BSDF_PRINCIPLED')
   for socket in ['Base Color','Roughness','Normal']:
    check(bool(bs.inputs[socket].links),mat.name+' exported '+socket)
 for obj in [o for o in meshes if o.get('metric_pbr_surface')]:
  uv=obj.data.uv_layers.active;bad=0
  for poly in obj.data.polygons:
   period=obj.data.materials[poly.material_index].get('repeat_m')
   check(period is not None,obj.name+' metric repeat metadata')
   if period is None:continue
   normal=(obj.matrix_world.to_3x3().inverted().transposed()@poly.normal).normalized()
   axes=[a for a in range(3) if a!=max(range(3),key=lambda a:abs(normal[a]))]
   loops=list(poly.loop_indices);first=loops[0];p0=obj.matrix_world@obj.data.vertices[obj.data.loops[first].vertex_index].co;u0=uv.data[first].uv
   for li in loops[1:]:
    pt=obj.matrix_world@obj.data.vertices[obj.data.loops[li].vertex_index].co;u=uv.data[li].uv
    if any(abs((u[k]-u0[k])*period[k]-(pt[axes[k]]-p0[axes[k]]))>.002 for k in [0,1]):bad+=1
  check(bad==0,obj.name+' actual exported metric UV',bad_edges=bad)
for root in [o for o in sc.objects if o.type=='EMPTY' and o.get('asset_id')=='ceiling-light']:
 pp=pts([o for o in root.children_recursive if o.type=='MESH']);lo=[min(q[k] for q in pp) for k in range(3)];hi=[max(q[k] for q in pp) for k in range(3)];hits=[]
 for wall in [o for o in meshes if o.get('part_kind')=='wall']:
  vv=pts([wall]);wl=[min(q[k] for q in vv) for k in range(3)];wh=[max(q[k] for q in vv) for k in range(3)]
  if all(min(hi[k],wh[k])-max(lo[k],wl[k])>.001 for k in range(3)):hits.append(wall.name)
 check(not hits,root.name+' avoids partition walls',blockers=hits)
measured=[]
from mathutils.bvhtree import BVHTree
wall_vertices=[];wall_faces=[]
for obj in [o for o in meshes if o.get('part_kind')=='wall']:
 matrix=obj.matrix_world.copy();base=len(wall_vertices);wall_vertices.extend(matrix@v.co for v in obj.data.vertices);wall_faces.extend([base+i for i in p.vertices] for p in obj.data.polygons)
wall_tree=BVHTree.FromPolygons(wall_vertices,wall_faces)
for door in plan['doors']:
 x,y=door['center_m'];z=(door['floor']-1)*3.8;axis=door['axis'];normal=Vector((0,1,0) if axis==0 else (1,0,0));jambs=[]
 for sign in [-1,1]:
  point=Vector((x,y,z+1.2));point[axis]+=sign*(door['width_m']/2+.15);jambs.append(wall_tree.ray_cast(point-normal*.6,normal,1.2)[0] is not None)
 top=Vector((x,y,z+2.54));lintel=wall_tree.ray_cast(top-normal*.6,normal,1.2)[0] is not None
 check(lintel and (all(jambs) or 'entry-front' in door['id']),door['id']+' physically mounted in wall',jamb_wall_hits=jambs,lintel_wall_hit=lintel)
for floor in C['building']['small_shop_design']['floors']:
 for room in floor['private_rooms']:
  a,bb,c,d=room['bounds_m'];center=Vector(((a+c)/2,(bb+d)/2,floor['z_m']+2.8))
  for direction,distance in [(Vector((1,0,0)),(c-a)/2+.4),(Vector((-1,0,0)),(c-a)/2+.4),(Vector((0,1,0)),(d-bb)/2+.4),(Vector((0,-1,0)),(d-bb)/2+.4)]:
   check(wall_tree.ray_cast(center,direction,distance)[0] is not None,room['id']+' physical enclosed wall '+str(tuple(direction)))
if BID=='S01':
 root=bpy.data.objects.get('F03-kitchen-sink');front=-(root.matrix_world.to_3x3()@Vector((0,1,0))).normalized()
 check(front.dot(Vector((0,1,0)))>.99,'S01 kitchen basin front faces the room',front=list(front))
for root in [o for o in sc.objects if o.type=='EMPTY' and o.get('asset_id')]:
 children=[o for o in root.children_recursive if o.type=='MESH']
 if not children:continue
 world=pts(children);local=[root.matrix_world.inverted()@q for q in world];lo=[min(q[k] for q in world) for k in range(3)];hi=[max(q[k] for q in world) for k in range(3)]
 record=dict(name=root.name,asset=root['asset_id'],floor=root['floor'],world_bounds_m=[lo,hi],world_size_m=[hi[k]-lo[k] for k in range(3)],local_size_m=[max(q[k] for q in local)-min(q[k] for q in local) for k in range(3)],planned_interactions=list(root.get('planned_interactions',[])),runtime_connected=False)
 measured.append(record)
 if root['asset_id'] in ('chair','toilet','sink','steel-counter','steel-table','double-sink','handwash','cup-sealer','tea-dispenser','syrup-doser','ice-maker','ingredient-box','bed','wardrobe','sofa','fridge','table','bookshelf'):
  check(lo[0]>=-C['width']/2+.199 and hi[0]<=C['width']/2-.199 and lo[1]>=-C['depth']/2+.199 and hi[1]<=C['depth']/2-.199,root.name+' fits interior footprint',bounds=[lo,hi])
(REVIEW/'measured-instances.json').write_text(json.dumps(dict(building=BID,units='metres',instances=measured),ensure_ascii=False,indent=2),'utf-8')
for aid,s in plan['sources'].items():
 try:
  verify_remesh(Path(s['remesh_report']).parent,s['sha256']);check(True,aid+' mandatory remesh receipt')
 except Exception as error:check(False,aid+' mandatory remesh receipt',error=str(error))
 r=json.loads(Path(s['remesh_report']).read_text('utf-8'));p=Path(s['path']);q=Path(r['source']);q=q if q.is_absolute() else ROOT/q
 check(r['status']=='passed' and hashlib.sha256(q.read_bytes()).hexdigest()==r['source_sha256'],aid+' remesh source identity')
 check(hashlib.sha256(p.read_bytes()).hexdigest()==s['sha256'],aid+' assembled asset identity')
def refresh():
 bpy.context.view_layer.update();return bpy.context.evaluated_depsgraph_get()
dg=refresh()
def ray(origin,direction,length):
 hit,p,n,index,obj,matrix=sc.ray_cast(dg,Vector(origin),Vector(direction),distance=length);return (p,obj) if hit else (None,None)
if BID=='S02':
 zones=[(1,'cashier work area',[-4.8,-7.5,-3.5,-4.95]),(3,'locker approach',[.15,-3.5,1.65,-2.25]),(3,'staff fridge approach',[3.45,-6.5,4.15,-5.4]),(3,'pantry basin approach',[1.375,-6.65,1.925,-5.6])]
 for floor,name,(x0,y0,x1,y1) in zones:
  z=[0,3.8,7.6][floor-1];blockers=[]
  for o in meshes:
   pp=pts([o]);lo=[min(q[k] for q in pp) for k in range(3)];hi=[max(q[k] for q in pp) for k in range(3)]
   if hi[2]<=z+.02 or lo[2]>=z+2.2:continue
   if min(hi[0],x1)-max(lo[0],x0)>.001 and min(hi[1],y1)-max(lo[1],y0)>.001:blockers.append(o.name)
  check(not blockers,'S02 '+name+' usable standing area',bounds=[x0,y0,x1,y1],blockers=blockers)
 for name,direction in [('F01-order-counter',(1,0,0)),('F01-cash-register',(1,0,0)),('F01-cup-sealer',(0,1,0)),('F01-tea-dispenser',(0,1,0)),('F01-syrup-doser',(0,1,0)),('F01-ice-maker',(-1,0,0)),('F02-ingredients-fridge',(0,1,0)),('F03-staff-fridge',(0,1,0)),('F03-pantry-basin',(0,1,0)),('F03-staff-locker',(0,-1,0))]:
  o=bpy.data.objects.get(name);front=(o.matrix_world.to_3x3()@Vector((0,-1,0))).normalized() if o else Vector()
  check(o is not None and front.dot(Vector(direction))>.99,name+' controls face usable approach',front=list(front))
 for floor,z in [(1,0),(2,3.8),(3,7.6)]:
  for root in [o for o in sc.objects if o.type=='EMPTY' and o.get('floor')==floor and o.get('asset_id') in ('double-sink','handwash')]:
   x,y,_=root.matrix_world.translation;offsets=[-.325,.325] if root['asset_id']=='double-sink' else [0]
   for offset in offsets:
    h=.9 if root['asset_id']=='double-sink' else .85;p,o=ray((x+offset,y,z+h+.05),(0,0,-1),.5)
    check(p is not None and z+.5<p.z<z+h-.15,root.name+f' actual open basin {offset}',first_surface_z=p.z if p else None)
for floor,z in [(1,0),(2,3.8),(3,7.6)]:
 for route in plan['routes'][str(floor)]:
  name=f'F{floor:02d} '+route['id'];x0,y0,x1,y1=route['bounds'];blockers=[]
  for o in meshes:
   pp=pts([o]);lo=[min(q[k] for q in pp) for k in range(3)];hi=[max(q[k] for q in pp) for k in range(3)]
   if hi[2]<=z+.02 or lo[2]>=z+2.2:continue
   if min(hi[0],x1)-max(lo[0],x0)>.001 and min(hi[1],y1)-max(lo[1],y0)>.001:blockers.append(o.name)
  check(not blockers,name+' standing corridor volume',blockers=blockers)
  axis=0 if x1-x0<y1-y0 else 1;width=x1-x0 if axis==0 else y1-y0;check(width>=2-1e-6,name+' >=2m width',width=width)
  length=y1-y0 if axis==0 else x1-x0;steps=math.ceil(length/.25)
  for i in range(steps+1):
   t=(i+.001)/(steps+.002);x=(x0+x1)/2 if axis==0 else x0+(x1-x0)*t;y=y0+(y1-y0)*t if axis==0 else (y0+y1)/2
   p,o=ray((x,y,z+.02),(0,0,-1),.1);check(p is not None and abs(p.z-z)<.002,name+f' floor{i}')
   for h in [.15,1,1.8,2.19]:
    origin=(x0+.001,y,z+h) if axis==0 else (x,y0+.001,z+h);direction=(1,0,0) if axis==0 else (0,1,0)
    p,o=ray(origin,direction,width-.002);check(p is None,name+f' width{i}@{h}',blocker=o.name if o else None)
# Independent actual stairs. There is no copied F01 test context here.
cx,cy=C['stair_center'];run=3.08;start=cy-run/2;end=cy+run/2;r=3.8/24
for floor,z in [(1,0),(2,3.8)]:
 samples=[(cx-.9,start+(j+.5)*.28,z+(j+1)*r) for j in range(11)]+[(cx,(end+C['stair_far'])/2,z+1.9)]+[(cx+.9,end-(j+.5)*.28,z+(12+j+1)*r) for j in range(11)]+[(cx+.9,start-.5,z+3.8)]
 for i,(x,y,h) in enumerate(samples):
  for offset in [-.7,-.35,0,.35,.7]:
   p,o=ray((x+offset,y,h+.02),(0,0,-1),.1);check(p is not None and abs(p.z-h)<.002,f'F{floor} actual tread{i} lane{offset}',z=p.z if p else None)
   p,o=ray((x+offset,y,h+.01),(0,0,1),2.19);check(p is None,f'F{floor} actual stair headroom{i} lane{offset}',blocker=o.name if o else None)
 for x,y,h in samples[:11]+samples[12:23]:
  p,o=ray((x-.749,y,h+.4),(1,0,0),1.498);check(p is None,f'F{floor} 1.5m flight width {x}-{y}',blocker=o.name if o else None)
check(not any('F03-stair_tread' in o.name for o in meshes),'no F03 flight into closed roof')
check(any(o.get('part_kind')=='void_guard' and o.get('floor')==3 for o in meshes),'F03 stair void guarded')
for floor,z in [(1,0),(2,3.8),(3,7.6)]:
 # Inspect horizontal slabs under a clear lobby, not furniture or stairs.
 lx,ly=C['lobby'];pp,o=ray((lx,ly,z+.02),(0,0,-1),.1);check(pp is not None and abs(pp.z-z)<.002,f'F{floor} lobby floor Z')
 slabs=[o for o in meshes if o.get('part_kind')=='floor' and min(q.x for q in pts([o]))<=lx<=max(q.x for q in pts([o])) and min(q.y for q in pts([o]))<=ly<=max(q.y for q in pts([o])) and abs(max(q.z for q in pts([o]))-z)<.002]
 check(len(slabs)==1,f'F{floor} shared slab only once',count=len(slabs))
for door in plan['doors']:
 floor=door['floor'];z=[0,3.8,7.6][floor-1]
 # Inspect actual imported scene animation, without moving cab/doors by hand.
 sc.frame_set([1,80,160][floor-1]);dg=refresh();x,y=door['center_m'];axis=door['axis'];width=door['width_m']
 for h in [.1,1,2.2,2.39]:
  origin=(x-width/2+.001,y,z+h) if axis==0 else (x,y-width/2+.001,z+h);direction=(1,0,0) if axis==0 else (0,1,0)
  p,o=ray(origin,direction,width-.002);check(p is None,door['id']+f' clear opening @{h}',blocker=o.name if o else None)
cab=[o for o in sc.objects if o.type=='EMPTY' and o.get('asset_id')=='lift'];check(len(cab)==1,'one physical elevator cab',count=len(cab))
for floor,z in [(1,0),(2,3.8),(3,7.6)]:
 sc.frame_set([1,80,160][floor-1]);dg=refresh()
 dx,dy=C['lift_door']
 for i,offset in enumerate([.31,.15,0,-.2,-.5,-1.075,-1.7]):
  p,o=ray((dx+offset,dy,z+.02),(0,0,-1),.1);check(p is not None and abs(p.z-z)<.002,f'lift stop{floor} walking surface{i}',actual=p.z if p else None)
 p,o=ray((*C['cab'],z+.01),(0,0,1),3);check(p is not None and p.z-z>=2.39,f'lift stop{floor} headroom',height=p.z-z if p else None)
for frame in [40,120]:
 sc.frame_set(frame);dg=refresh();cab_z=cab[0].matrix_world.translation.z+.05
 check((0<cab_z<3.8) if frame==40 else (3.8<cab_z<7.6),f'lift travel height frame{frame}',actual=cab_z)
 for floor,z in [(1,0),(2,3.8),(3,7.6)]:
  p,o=ray((C['lift_door'][0],C['lift_door'][1]-.599,z+1),(0,1,0),1.198);check(p is not None and 'landing-door-panel' in o.name,f'all landing doors closed in travel frame{frame} F{floor}',hit=o.name if o else None)
 p,o=ray((C['lift_door'][0]-.1,C['lift_door'][1]-.599,cab_z+1),(0,1,0),1.198);check(p is not None and 'car-door' in o.name,f'car door closed in travel frame{frame}',hit=o.name if o else None)
raw=source.read_bytes();length,kind=struct.unpack_from('<II',raw,12);gltf=json.loads(raw[20:20+length]);check(len(gltf.get('animations',[]))==1,'single synchronized glTF animation clip',count=len(gltf.get('animations',[])))
# Ground entrance walking route from outside into building.
sc.frame_set(0);dg=refresh()
for y in [C['entry'][1]+q for q in [.01,.1,.5,1,1.5]]:
 p,o=ray((C['entry'][0],y,.02),(0,0,-1),.1);check(p is not None and abs(p.z)<.002,f'ground entrance floor at{y}')
 p,o=ray((C['entry'][0],y,.01),(0,0,1),2.19);check(p is None,f'ground entrance headroom at{y}',blocker=o.name if o else None)
for floor in [1,2,3]:
 path=OUT/f'{BID}-F{floor:02d}-complete-2k.glb';data=path.read_bytes();length,kind=struct.unpack_from('<II',data,12);g=json.loads(data[20:20+length]);check(not g.get('animations'),f'F{floor} isolated GLB is static')
 bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=str(path));bpy.context.view_layer.update();os=[o for o in bpy.context.scene.objects if o.type=='MESH'];pp=pts(os);n=0
 for o in os:o.data.calc_loop_triangles();n+=len(o.data.loop_triangles)
 check(n==totals[floor],f'F{floor} isolated GLB triangle identity',actual=n)
 for axis,target in [(0,C['width']),(1,C['depth'])]:check(abs(max(q[axis] for q in pp)-min(q[axis] for q in pp)-target)<.002,f'F{floor} isolated GLB envelope axis{axis}')
 check(all(o.get('floor')==floor for o in os),f'F{floor} isolated GLB contains only that floor')
report=dict(interior_schema=2,status='passed' if not errors else 'failed',source=str(source),sha256=hashlib.sha256(source.read_bytes()).hexdigest(),checks=len(checks),errors=errors,triangles=sum(totals.values()),floor_triangles=totals,results=checks,scope='actual exported full three-storey building geometry and elevator animation poses; no synthetic floor context',limitations=plan['limitations'])
(REVIEW/'acceptance.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),'utf-8');print(json.dumps(dict(status=report['status'],checks=len(checks),errors=errors[:30],floors=totals)))
if errors:raise RuntimeError(f'{len(errors)} full building checks failed')
