"""Check real exported fixture fronts and usable standing zones, not just counts."""
import bpy,json,sys,math,argparse,hashlib
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_shop_street import ROOT,inventory
from street_city_building_context import context as small_context
p=argparse.ArgumentParser();p.add_argument('--before',action='store_true');p.add_argument('--expanded',action='store_true');p.add_argument('--ids',nargs='*');args=p.parse_args(sys.argv[sys.argv.index('--')+1:]);OUT=ROOT/'output/street-social-city-production/interior-repair-v1';rows=[]
for b in inventory():
 bid=b['building']
 if args.ids and bid not in args.ids:continue
 folder=OUT/'before'/bid if args.before else ROOT/f'output/street-social-city-production/{bid}-modular-components-v1/assembly-building'
 model=folder/f'{bid}-complete-2k.glb';source_sha=hashlib.sha256(model.read_bytes()).hexdigest();bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=str(model));bpy.context.scene.frame_set(1);bpy.context.view_layer.update();meshes=[o for o in bpy.context.scene.objects if o.type=='MESH'];bounds=[]
 for obj in meshes:
  points=[obj.matrix_world@Vector(c) for c in obj.bound_box];bounds.append((obj,[min(p[k] for p in points) for k in range(3)],[max(p[k] for p in points) for k in range(3)]))
 findings=[];fixtures=[];local_design=small_context(bid)['building']['small_shop_design'] if b.get('small_shop_design') else None
 for root in [o for o in bpy.context.scene.objects if o.type=='EMPTY' and o.get('asset_id') in (['sink','handwash','double-sink','toilet','fridge','wardrobe','washing-machine','dryer','large-wardrobe','large-locker','large-service-cabinet','large-service-tall'] if args.expanded else ['sink','handwash','double-sink'])]:
  descendants=set(root.children_recursive);inverse=root.matrix_world.inverted();pp=[]
  for o in descendants:
   if o.type=='MESH':
    transform=inverse@o.matrix_world;pp.extend(transform@v.co for v in o.data.vertices)
  if not pp:continue
  top=max(p.z for p in pp);high=[v for v in pp if v.z>top-.14];back=sum(high,Vector())/len(high);back.z=0
  if root['asset_id'] not in ['sink','handwash','double-sink','toilet']:back=Vector((0,1,0))
  if root['asset_id'] in ['wardrobe','fridge','large-wardrobe','large-locker','large-service-cabinet','large-service-tall']:
   handles=[]
   for o in descendants:
    if o.type!='MESH':continue
    metallic=any(m and m.use_nodes and any(n.type=='BSDF_PRINCIPLED' and n.inputs['Metallic'].default_value>.5 for n in m.node_tree.nodes) for m in o.data.materials)
    if metallic:
     transform=inverse@o.matrix_world;handles.extend(transform@v.co for v in o.data.vertices)
   if handles:
    handle_center=sum(handles,Vector())/len(handles);axis=0 if abs(handle_center.x)>abs(handle_center.y) else 1;back=Vector();back[axis]=-1 if handle_center[axis]>0 else 1

  back.normalize();front=-(root.matrix_world.to_3x3()@back).normalized();front.z=0;front.normalize();depth=max(abs(v.dot(back)) for v in pp);center=root.matrix_world.translation+front*(depth+.35);z=next(f['z_m'] for f in b.get('small_shop_design',{}).get('floors',b.get('floors',[])) if f['floor']==root['floor'])
  # Axis-aligned fixture placements use a 0.5m-wide by 0.6m-deep standing zone.
  half=[.3 if abs(front.x)>.5 else .25,.3 if abs(front.y)>.5 else .25];lo=[center.x-half[0],center.y-half[1],z+.05];hi=[center.x+half[0],center.y+half[1],z+1.8];blockers=[]
  for obj,aa,bb in bounds:
   if obj in descendants:continue
   if all(min(hi[k],bb[k])-max(lo[k],aa[k])>.015 for k in range(3)):blockers.append(obj.name)
  if local_design:
   planned_floor=next(f for f in local_design['floors'] if f['floor']==root['floor']);rx,ry,_=root.matrix_world.translation
   for room in planned_floor['private_rooms']:
    a,bb,c,d=room['bounds_m']
    activity=next(cell for cell in planned_floor['cells'] if cell['kind']=='activity');w,dep=b['footprint_m'];left=a if abs(a-(-w/2+.2))<1e-6 else a+.2;right=c if abs(c-(w/2-.2))<1e-6 else c-.2;bottom=bb if abs(bb-(-dep/2+.2))<1e-6 else bb+.2;top=d if abs(d-(dep/2-.2))<1e-6 or not activity['open'] and abs(d-activity['bounds_m'][3])<1e-6 else d-.2
    if a<rx<c and bb<ry<d and (lo[0]<left-.015 or lo[1]<bottom-.015 or hi[0]>right+.015 or hi[1]>top+.015):blockers.append('operator leaves declared room '+room['id'])
  fixture=dict(name=root.name,asset=root['asset_id'],front=list(front),standing_bounds=[lo,hi],blockers=blockers);fixtures.append(fixture)
  if blockers:findings.append(fixture)
 report=json.loads((folder/'assembly-report.json').read_text('utf-8'));vertices=[];polygons=[]
 for obj in meshes:
  if obj.get('part_kind')!='wall' and obj.get('asset_id') not in ['large-standard-cubicle','large-accessible-cubicle']:continue
  transform=obj.matrix_world.copy();base=len(vertices);vertices.extend(transform@v.co for v in obj.data.vertices);polygons.extend([base+i for i in poly.vertices] for poly in obj.data.polygons)
 wall_tree=BVHTree.FromPolygons(vertices,polygons);door_checks=[]
 for door in report['doors']:
  if 'center_m' in door:
   x,y=door['center_m'];z=(door['floor']-1)*3.8;axis=door['axis'];along=Vector((1,0,0) if axis==0 else (0,1,0));normal=Vector((0,1,0) if axis==0 else (1,0,0))
  else:
   x,y,z=door['position_m'];angle=math.radians(door['yaw_deg']);along=Vector((math.cos(angle),math.sin(angle),0));normal=Vector((-math.sin(angle),math.cos(angle),0))
  center=Vector((x,y,z));offset=.025 if 'cubicle-door' in door['id'] else .15;jambs=[]
  for sign in [-1,1]:
   point=center+Vector((0,0,1.2))+along*sign*(door['width_m']/2+offset);jambs.append(wall_tree.ray_cast(point-normal*.6,normal,1.2)[0] is not None)
  point=center+Vector((0,0,door.get('height_m',2.4)+.14));lintel=wall_tree.ray_cast(point-normal*.6,normal,1.2)[0] is not None;entry='entry-front' in door['id'] or 'entrance-' in door['id'];passed=lintel and (all(jambs) or entry);door_checks.append(dict(id=door['id'],passed=passed,jambs=jambs,lintel=lintel))
 if not args.before:
  import hashlib
  from street_city_interior_svg import write_floorplates,LABELS
  lookup={obj:(lo,hi) for obj,lo,hi in bounds};boxes=[]
  for root in [o for o in bpy.context.scene.objects if o.type=='EMPTY' and o.get('asset_id') in LABELS]:
   children=[lookup[o] for o in root.children_recursive if o in lookup]
   if children:boxes.append(dict(name=root.name,asset=root['asset_id'],floor=root['floor'],bounds=[[min(a[k] for a,b in children) for k in range(3)],[max(b[k] for a,b in children) for k in range(3)]]))
  yaws={door['id']:bpy.data.objects[door['id']+'-frame'].matrix_world.to_euler().z for door in report['doors'] if door['id']+'-frame' in bpy.data.objects}
  room_labels={}
  if local_design:
   for f in local_design['floors']:
    room_labels[str(f['floor'])]=[dict(bounds_m=c['bounds_m'],label={'lift':'電梯井','stairs':'樓梯','restroom':'衛浴'}[c['kind']]) for c in f['cells'] if c['kind'] in ['lift','stairs','restroom']]+[dict(bounds_m=r['bounds_m'],label='廚房' if 'kitchen' in r['id'] else '試衣間' if 'fitting' in r['id'] else '臥室') for r in f['private_rooms']]
  write_floorplates(OUT/'floor-plans',bid,dict(b,review_rooms=room_labels,review_world_yaw=report.get('world_placement',{}).get('rotation_z_deg',0)),[(str(o.get('part_kind','')),o.get('asset_id'),lo,hi,o.get('floor')) for o,lo,hi in bounds],boxes,{f['name']:f['front'] for f in fixtures},report['doors'],yaws,source_sha)
 assert hashlib.sha256(model.read_bytes()).hexdigest()==source_sha,'Model changed during physical audit'
 rows.append(dict(building=bid,source_sha256=source_sha,fixtures=fixtures,findings=findings,door_checks=door_checks));(OUT/(('fixtures-expanded-' if args.expanded else 'fixtures-')+('before.json' if args.before else 'after.json'))).write_text(json.dumps(dict(buildings=rows),indent=2),'utf-8');print(bid,len(findings),flush=True)
