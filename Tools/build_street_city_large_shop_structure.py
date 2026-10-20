"""Exact large-building floor source. Mandatory Rust remesh precedes adoption."""
import bpy,json,math,sys,argparse,copy
from pathlib import Path
from mathutils import Vector
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_large_shop_contract import context
from street_city_parts import asset_geometry,walls,slab,stairs,transform
from street_city_small_shop import stair_slab_opening
p=argparse.ArgumentParser();p.add_argument('--building',required=True);p.add_argument('--floor',type=int,required=True);a=p.parse_args(sys.argv[sys.argv.index('--')+1:]);C=context(a.building);b=C['building'];f=b['floors'][a.floor-1];z=f['z_m'];h=f['height_m'];w,d=b['footprint_m'];out=C['out']/f'structure/F{a.floor:02d}';out.mkdir(parents=True,exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True);parts=[];seen=set()
def mat(name,color,metal=0):
 m=bpy.data.materials.new(name);m.use_nodes=True;bs=m.node_tree.nodes['Principled BSDF'];bs.inputs['Base Color'].default_value=(*color,1);bs.inputs['Metallic'].default_value=metal;bs.inputs['Roughness'].default_value=.6;return m
paint=mat('Warm washable plaster',(.78,.75,.7));floor_mat=mat('Floor finish pending metric PBR adoption',(.65,.62,.52));steel=mat('Handrail steel',(.34,.36,.38),.8)
def box(name,pos,size,kind='wall',yaw=0):
 if min(size)<1e-6:return
 key=tuple(round(v,5) for v in [*pos,*size,yaw])
 if key in seen:return
 seen.add(key);bpy.ops.mesh.primitive_cube_add(size=1,location=Vector(pos)+Vector((0,0,size[2]/2)));o=bpy.context.object;o.name=name;o.dimensions=size;bpy.ops.object.transform_apply(location=False,rotation=False,scale=True);o.rotation_euler.z=math.radians(yaw);o.data.materials.append(steel if kind in ('handrail','void_guard','stringer','threshold') else floor_mat if kind in ('floor','stair_tread','stair_landing') else paint);o['part_kind']=kind;o['floor']=a.floor;o['source_route']='metric large-shop contract'
 parts.append(dict(name=name,position_m=pos,size_m=size,yaw_deg=yaw,kind=kind))
def add_parts(prefix,ps,origin=(0,0,0),yaw=0,skip_boundary=False):
 for i,part in enumerate(ps):
  q=transform(part['position_m'],origin,yaw);size=part['size_m'][:];kind=part['kind']
  if kind=='wall_corner':kind='wall'
  if skip_boundary:
   # Global envelope below owns exterior walls exactly once.
   sx,sy=(size[1],size[0]) if round(yaw)%180 else size[:2]
   if abs(q[0])+sx/2>w/2-.199 or abs(q[1])+sy/2>d/2-.199:continue
  box(prefix+f'-{i}',q,size,kind,yaw)
cuts=[];doors=[];windows=[]
for e in b['entrances'] if a.floor==1 else []:
 pos=e.get('position_m',e.get('local_position_m'));assert pos is not None,e
 side=('west' if pos[0]<-w/2+.3 else 'east' if pos[0]>w/2-.3 else 'south' if pos[1]<0 else 'north')
 cut=dict(side=side,local_position_m=pos,width_m=2.56,height_m=2.48);cuts.append(cut);doors.append(dict(id=f'entrance-{len(doors)}',side=side,position_m=pos,width_m=2.4,height_m=2.4))
# Windows are explicit openings, kept clear of the service core on the west.
for side in ('south','north','east'):
 length=w if side in ('south','north') else d
 for j in range(1,int(length//4)):
  along=-length/2+j*4
  if any(c['side']==side and abs(c['local_position_m'][0 if side in ('south','north') else 1]-along)<2.5 for c in cuts):continue
  pos=[along,0,1] if side in ('south','north') else [0,along,1]
  cuts.append(dict(side=side,local_position_m=pos,width_m=2,height_m=1.6));windows.append(dict(side=side,along_m=along,sill_m=1,width_m=2,height_m=1.6))
add_parts('exterior',walls([w-.4,d-.4,h-.3],cuts),origin=(0,0,z))
cores=[c for c in f['vertical_circulation'] if c['kind'] in ('stairs','lift')];holes=[]
for c in cores:
 if c['kind']=='stairs':
  prev=b['floors'][max(0,a.floor-2)];pc=next(x for x in prev['vertical_circulation'] if x['kind']=='stairs' and x['core']==c['core'])
  holes.append(stair_slab_opening(c,c['walkable_geometry'],pc['walkable_geometry']))
 else:holes.append(dict(center_m=c['position_m'][:2],size_m=c['clear_size_m'][:2],kind='lift'))
add_parts('floor',slab([w,d,.3],holes if a.floor>1 else [],z=z-.3))
if a.floor==len(b['floors']):add_parts('roof',slab([w,d,.3],z=z+h-.3,kind='ceiling'))
for inst in C['instances']:
 if inst['floor']!=a.floor:continue
 asset=copy.deepcopy(C['assets'][inst['asset_id']]);role=asset['role'];aid=asset['id'];origin=inst['position_m'];yaw=inst['rotation_z_deg']
 if role not in ('room_shell','annex_room','support_room','public_restroom','internal_partition','circulation'):continue
 if aid.endswith('-car'):continue
 is_stair=role=='circulation' and 'riser_count' in asset.get('walkable_geometry',{})
 if is_stair:
  clear=[asset['size_m'][0]-.4,asset['size_m'][1]-.4,h-.3];ps=walls(clear,[dict(side='south',local_position_m=[0,0,0],width_m=1.36,height_m=2.48)])
  if a.floor<len(b['floors']):
   flights=stairs(asset['size_m'],asset['walkable_geometry'])
   for q in flights:
    if q['kind']=='stair_tread':q['position_m'][2]+=q['size_m'][2]-.12;q['size_m'][2]=.12
    if q['kind']=='stair_landing' and q['position_m'][2]<0:continue # prior flight owns shared landing
   ps+= [q for q in flights if not(q['kind']=='stair_landing' and (q['position_m'][2]<0 or abs(q['position_m'][2]+q['size_m'][2]-h)<.001))]
 elif role=='circulation' and '-shaft' in aid:
  ps=walls([asset['size_m'][0]-.4,asset['size_m'][1]-.4,h-.3],[dict(side='north',local_position_m=[0,0,0],width_m=1.36,height_m=2.48)])
 else:
  openings=[asset[k] for k in ('opening','entrance_aperture') if k in asset]+asset.get('additional_wall_openings',[])
  for cut in openings:
   if cut.get('kind','').startswith('open_'):continue
   wk='width_m' if 'width_m' in cut else 'clear_width_m';hk='height_m' if 'height_m' in cut else 'clear_height_m';cut[wk]+=.16;cut[hk]+=.08
  ps,_=asset_geometry(asset)
  for q in ps:
   if q['kind']=='wall' and q['position_m'][2]+q['size_m'][2]>=asset.get('clear_size_m',asset['size_m'])[2]-.01:q['size_m'][2]=h-.3-q['position_m'][2]
 add_parts(aid,ps,origin,yaw,skip_boundary=True)
 if is_stair:
  g=asset['walkable_geometry'];run=g['flight_run_m'];cx,cy,_=origin
  if a.floor<len(b['floors']):
   for flight in (0,1):
    x=cx+(-.9 if flight==0 else .9)
    for sign in (-1,1):
     start=Vector((x+sign*.77,cy-run/2,z+(g['riser_height_m'] if flight==0 else h)+.95));end=Vector((x+sign*.77,cy+run/2,z+h/2+.95));delta=end-start
     o=box(aid+f'-rail-{flight}-{sign}',[0,0,0],[.04,.04,delta.length],'handrail')
     obj=bpy.context.object;obj.location=(start+end)/2;obj.rotation_euler=delta.to_track_quat('Z','Y').to_euler()
     for k,t in enumerate((0,.5,1)):
      pt=start.lerp(end,t);box(aid+f'-railpost-{flight}-{sign}-{k}',[pt.x,pt.y,pt.z-.95],[.04,.04,.95],'handrail')
  doors.append(dict(id=aid+'-door',side='south',position_m=[cx,cy-asset['size_m'][1]/2+.1,z],width_m=1.2,height_m=2.4))
  if a.floor>1:
   hole=next(q for q in holes if q.get('kind')=='stair_flights' and abs(q['center_m'][0]-cx)<.1);hx,hy=hole['center_m'];hw,hd=hole['size_m'];x0=hx-hw/2-.02;x1=hx+hw/2+.02;y0=hy-hd/2-.02;y1=hy+hd/2+.02
   lines=[((x0,y0),(x0,y1)),((x1,y0),(x1,y1)),((x0,y1),(x1,y1))]
   if a.floor==len(b['floors']):lines.append(((x0,y0),(cx+.15,y0)))
   for i,(aa,bb) in enumerate(lines):
    length=math.dist(aa,bb);box(aid+f'-guard-{i}',[(aa[0]+bb[0])/2,(aa[1]+bb[1])/2,z+.93],[length,.04,.04] if aa[1]==bb[1] else [.04,length,.04],'void_guard')
    for k in range(math.ceil(length/1.2)+1):
     t=k/math.ceil(length/1.2);box(aid+f'-guardpost-{i}-{k}',[aa[0]+(bb[0]-aa[0])*t,aa[1]+(bb[1]-aa[1])*t,z],[.04,.04,.93],'void_guard')
 elif role=='circulation' and '-shaft' in aid:
  # Frames occupy enlarged real aperture, not a nominal 1.2m cut.
  doors.append(dict(id=aid+'-door',side='north',position_m=[origin[0],origin[1]+asset['size_m'][1]/2-.1,z],width_m=1.2,height_m=2.4,kind='lift'))
for o in bpy.context.scene.objects:o.select_set(True)
bpy.ops.export_scene.gltf(filepath=str(out/'source.glb'),export_format='GLB',export_extras=True)
(out/'source.json').write_text(json.dumps(dict(building=a.building,floor=a.floor,parts=parts,doors=doors,windows=windows,holes=holes,status='source_requires_remesh'),ensure_ascii=False,indent=2),'utf-8')
