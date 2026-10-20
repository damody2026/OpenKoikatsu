"""Assemble S01's actual three floors from accepted reusable metric assets."""
import bpy,json,math,hashlib,sys,argparse
from pathlib import Path
from mathutils import Vector,Matrix
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'Tools'))
from street_city_building_context import context,s02_placements
from street_city_retail_profiles import COMMON,placements as retail_placements
from street_city_remesh_gate import verify as verify_remesh
parser=argparse.ArgumentParser();parser.add_argument('--building',default='S01');parser.add_argument('--quick-preview',action='store_true');args=parser.parse_args(sys.argv[sys.argv.index('--')+1:]);C=context(args.building);BID=C['id']
BASE=ROOT/'output/street-social-city-production/S01-modular-components-v1';OUT=C['out'];OUT.mkdir(exist_ok=True)
manifest=json.loads((ROOT/'Docs/StreetSocialCity/production/assembly-manifest.json').read_text('utf-8'));b=C['building'];floors=b['small_shop_design']['floors']
newassets={q['id'] for q in json.loads((OUT/'furniture/sources.json').read_text('utf-8'))}
bpy.ops.wm.read_factory_settings(use_empty=True);sc=bpy.context.scene;sc.unit_settings.system='METRIC';cache={};sources={};instances=[];doors=[];roots={}
def folder(aid):
 if BID not in ('S01','S02') and aid in ('toilet','sink','bicycle'):
  lod=COMMON/(aid+'-lod')/'remesh'
  if (lod/'remesh-report.json').exists() and json.loads((lod/'remesh-report.json').read_text('utf-8'))['status']=='passed':return lod
 if aid.startswith('structure-F'):
  index=OUT/'structure-library.json'
  if index.exists():return Path(json.loads(index.read_text('utf-8'))[aid])
  return OUT/f'structure/{aid[-3:]}/remesh'
 if (COMMON/aid/'remesh/remesh-report.json').exists():return COMMON/aid/'remesh'
 if aid in newassets:return OUT/f'furniture/{aid}/remesh'
 if aid in {q['id'] for q in json.loads((ROOT/'output/street-social-city-production/S02-modular-components-v1/assembly-building/furniture/sources.json').read_text('utf-8'))}:return ROOT/f'output/street-social-city-production/S02-modular-components-v1/assembly-building/furniture/{aid}/remesh'
 if aid in {q['id'] for q in json.loads((BASE/'assembly-building/furniture/sources.json').read_text('utf-8'))}:return BASE/f'assembly-building/furniture/{aid}/remesh'
 if aid in ('chair','toilet','sink'):return BASE/f'generated-components/{aid}/remesh-production'
 if aid=='bookshelf':return BASE/'board-bookshelf/remesh-box'
 return BASE/f'local-components/{aid}/remesh'
def load(aid):
 if aid in cache:return cache[aid]
 path=folder(aid);receipt=verify_remesh(path)
 glb=path/'remeshed-candidate.glb';old=set(bpy.data.objects);bpy.ops.import_scene.gltf(filepath=str(glb));new=set(bpy.data.objects)-old;bpy.context.view_layer.update();data=[]
 for o in new:
  if o.type=='MESH':
   o.data.use_fake_user=True;data.append((o.data,o.matrix_world.copy(),o.name,{k:o.get(k) for k in ('part_kind','source_route') if k in o}))
 for o in new:bpy.data.objects.remove(o,do_unlink=True)
 cache[aid]=data;sources[aid]=receipt;return data
def place(aid,name,floor,pos=(0,0,0),yaw=0,scale=(1,1,1),parent=None):
 root=bpy.data.objects.new(name,None);sc.collection.objects.link(root);root.parent=parent or roots[floor];root.location=pos;root.rotation_euler.z=yaw;root.scale=scale;root['asset_id']=aid;root['floor']=floor
 for data,matrix,n,props in load(aid):
  o=bpy.data.objects.new(name+'/'+n,data);sc.collection.objects.link(o);o.parent=root;o.matrix_basis=matrix;o['asset_id']=aid;o['floor']=floor;o['instance_id']=name
  for k,v in props.items():o[k]=v
 instances.append(dict(asset=aid,name=name,floor=floor,position_m=list(pos),yaw_rad=yaw,scale=list(scale)));return root
def point(pos,yaw,local):return Vector(pos)+Matrix.Rotation(yaw,4,'Z')@Vector(local)
def books(shelf,floor,prefix,n=14):
 for bay in range(5):
  for j in range(n):place('book',f'{prefix}-book-{bay}-{j}',floor,(-.43+j*.06,-.16,.027+bay*.395),parent=shelf)
for f in floors:
 no=f['floor'];z=f['z_m'];root=bpy.data.objects.new(f'{BID}-F{no:02d}',None);sc.collection.objects.link(root);root['floor']=no;root['z_m']=z;roots[no]=root
 place(f'structure-F{no:02d}',f'F{no:02d}-structure',no)
 for i,q in enumerate(f['exterior_openings']):
  if q['local_position_m'][2]==0:continue
  x,_,sill=q['local_position_m'];yy=-C['depth']/2+.1 if q['side']=='south' else C['depth']/2-.1
  place('window',f'F{no:02d}-window-{i}',no,(x,yy,z+sill),scale=(q['width_m']/2,1,q['height_m']/1.6))
 place('toilet',f'F{no:02d}-toilet',no,(-C['width']/2+.57,C['depth']/2-4.1,z),math.pi/2)
 place('sink',f'F{no:02d}-sink',no,(-C['width']/2+.46,C['depth']/2-6.1,z),math.pi/2)
 light_positions=[(-2,-5),(3,-3 if no==3 else -5),(0,1),(.8,5.5),(-3.1,3.9),(3.9,3.2)] if BID=='S01' else [(-1.6,-4.5),(2,-5),(0,0),(-.2,4.5),(-3.1,2.9),(2.9,2.2)]
 if BID not in ('S01','S02'):
  activity=next(c['bounds_m'] for c in f['cells'] if c['kind']=='activity');light_positions=[(-C['width']/4,(activity[1]+activity[3])/2),(C['width']/4,(activity[1]+activity[3])/2)]
  for kind in ['corridor','restroom','stairs']:
   cell=next(c['bounds_m'] for c in f['cells'] if c['kind']==kind);light_positions.append(((cell[0]+cell[2])/2,cell[1]+.65 if kind=='stairs' else (cell[1]+cell[3])/2))
  light_positions.append((C['cab'][0],C['cab'][1]))
 if f['private_rooms']:
  aa,bb,cc,dd=f['private_rooms'][0]['bounds_m'];light_positions[1]=((aa+cc)/2,(bb+dd)/2)
 for j,(xx,yy) in enumerate(light_positions):place('ceiling-light',f'F{no:02d}-light-{j}',no,(xx,yy,z+f['height_m']-.36))
 place('vent-grille',f'F{no:02d}-bath-vent',no,(-C['width']/2+.23,C['depth']/2-5.1,z+2.65),math.pi/2)
 # Leaf hinges and moving elevator panels are separate transform nodes.
 for q in f['doors']:
  x,y=q['center_m'];side=q['outward_side'];yaw={'south':0,'east':math.pi/2,'west':-math.pi/2,'north':math.pi}[side]
  if side in ('south','north'):y+=.1
  else:x+=.1 if side=='west' else -.1
  pos=(x,y,z);wide=q['width_m']>1.5
  place('entryframe' if wide else 'doorframe',q['id']+'-frame',no,pos,yaw)
  doorinfo=dict(id=q['id'],floor=no,center_m=[x,y],axis=0 if side in ('south','north') else 1,width_m=q['width_m'],kind=q['kind'],leaves=[])
  if q['kind']=='sliding':
   for sign in [-1,1]:
    panel=place('liftpanel',q['id']+f'-panel-{sign}',no,(x,y+sign*.31,z),math.pi/2)
    keys={1:[(1,True),(10,True),(20,False),(160,False)],2:[(1,False),(65,False),(75,True),(90,True),(100,False),(160,False)],3:[(1,False),(145,False),(155,True),(160,True)]}[no]
    for frame,opened in keys:
     panel.location.y=y+sign*(.95 if opened else .31);panel.keyframe_insert(data_path='location',frame=frame)
    doorinfo['leaves'].append(panel.name)
  else:
   flip_hinge=BID=='S02' and q['id'].endswith('bedroom-door')
   doorinfo['hinge_side_override']='right to keep locker approach clear' if flip_hinge else None
   for sign in ([-1,1] if wide else [1] if flip_hinge else [-1]):
    hinge=bpy.data.objects.new(q['id']+f'-hinge-{sign}',None);sc.collection.objects.link(hinge);hinge.parent=root;hinge.location=point(pos,yaw,(sign*(1.23 if wide else .63),0,0));hinge.rotation_euler.z=yaw+(-sign)*math.pi/2;hinge['floor']=no;hinge['closed_rotation_z']=yaw;hinge['motion']='hinge'
    # A mirrored leaf opens each main entrance half away from its aperture.
    leaf=place('entryleaf' if wide else 'door',q['id']+f'-leaf-{sign}',no,(-sign*.63,0,.02),scale=((1 if wide else .9966666667),1,1),parent=hinge)
    doorinfo['leaves'].append(hinge.name)
  doors.append(doorinfo)
 if BID=='S01':
  if no in (1,2):
   groups=[q for q in f['equipment'] if '桌椅組' in q['name']]
   for i,q in enumerate(groups):
    x0,y0,x1,y1=q['bounds_m'];x=(x0+x1)/2;y=(y0+y1)/2
    place('table',f'F{no:02d}-table-{i}',no,(x,y,z))
    for j,(dx,dy,angle) in enumerate([(0,.78,0),(0,-.78,math.pi),(.93,0,-math.pi/2),(-.93,0,math.pi/2)]):
     aid='stool' if no==1 and j==2 else 'chair'
     place(aid,f'F{no:02d}-{aid}-{i}-{j}',no,(x+dx,y+dy,z),angle)
  if no==1:
   place('service-counter','F01-order-counter',no,(-4.125,-6.8,z))
   # Reserve the real basin void; do not put a solid countertop through it.
   place('prep-counter','F01-back-counter-south',no,(-5.45,-4.31,z),scale=(1,1.38/2.5,1))
   place('prep-counter','F01-back-counter-north',no,(-5.45,-2.74,z),scale=(1,.48/2.5,1))
   place('prep-counter','F01-coffee-back-counter',no,(-5.45,-6.8,z),scale=(1,2.4/2.5,1))
   place('coffee-machine','F01-espresso-machine',no,(-5.45,-6.7,z+.9),math.pi/2)
   # Reuse the actual basin mesh and cabinetry rather than a fake sink box.
   place('sink','F01-prep-sink',no,(-5.43,-3.3,z+.1),math.pi/2)
   for i in range(6):
    s=place('bookshelf',f'F01-low-shelf-{i}',no,(-.4+i,-1.9,z),scale=(1,1,.8));books(s,no,f'F01-low-shelf-{i}')
  if no==2:
   for i in range(6):
    s=place('bookshelf',f'F02-shelf-{i}',no,(-5.2,-7.5+i,z),math.pi/2);books(s,no,f'F02-shelf-{i}')
  if no==3:
   place('bed','F03-double-bed',no,(3.9,-3.45,z))
   place('wardrobe','F03-wardrobe',no,(5.35,f['private_rooms'][0]['bounds_m'][3]-1.0,z))
   place('sofa','F03-sofa',no,(-4.7,-7.95,z))
   place('table','F03-desk',no,(-4.7,-6.35,z),scale=(1.25,1,1))
   place('chair','F03-study-chair',no,(-4.7,-5.55,z),math.pi)
   place('kitchen','F03-kitchen-left',no,(-.485,-8.0,z),scale=(.73/4.5,1,1))
   place('kitchen','F03-kitchen-right',no,(2.085,-8.0,z),scale=(3.13/4.5,1,1))
   place('sink','F03-kitchen-sink',no,(.2,-8.0,z+.1),math.pi)
   place('hob','F03-induction-hob',no,(2.4,-8.0,z+.9))
   place('fridge','F03-fridge',no,(4.7,-8.0,z),math.pi)
   place('shower','F03-shower',no,(-4.95,3.75,z))
   s=place('bookshelf','F03-study-shelf',no,(-5.15,-4.1,z),math.pi/2);books(s,no,'F03-study-shelf')
 if BID=='S02':
  for spec in s02_placements():
   if spec['floor']!=no:continue
   obj=place(spec['asset'],f"F{no:02d}-{spec['name']}",no,spec['pos'],math.radians(spec['yaw']),spec['scale']);obj['planned_interactions']=spec['actions'];obj['runtime_connected']=False
 if BID not in ('S01','S02'):
  for spec in retail_placements(C)[0]:
   if spec['floor']!=no:continue
   obj=place(spec['asset'],f"F{no:02d}-{spec['name']}",no,spec['pos'],math.radians(spec['yaw']),spec['scale']);obj['planned_interactions']=spec['actions'];obj['runtime_connected']=False;obj['programme']=spec.get('programme') or ''
# Single elevator cab parked at F01 by default, with an explicit three-stop action.
cab=place('lift',f'{BID}-EA1-single-cab',1,(*C['cab'],-.05),math.pi/2)
for frame,z in [(1,0),(20,0),(65,3.8),(100,3.8),(145,7.6),(160,7.6)]:cab.location.z=z-.05;cab.keyframe_insert(data_path='location',frame=frame)
for sign in [-1,1]:
 panel=place('liftpanel',f'{BID}-EA1-car-door-{sign}',1,(sign*.31,-.975,.05),parent=cab)
 for frame,opened in [(1,True),(10,True),(20,False),(65,False),(75,True),(90,True),(100,False),(145,False),(155,True),(160,True)]:
  panel.location.x=sign*(.95 if opened else .31);panel.keyframe_insert(data_path='location',frame=frame)
cab['stops_z_m']=[0,3.8,7.6];cab['runtime_controller_connected']=False
sc.frame_end=160;sc.frame_set(1)
# Fascia occupies existing facade planes; width never expands the building block.
for no,z in [(1,3.55),(2,7.35),(3,10.75)]:place('facade-band',f'F{no:02d}-fascia',no,(0,-C['depth']/2+.06,z),scale=(C['width']/12,1,1))
place('signboard',f'{BID}-storefront-sign',1,(.9,-C['depth']/2+.075,2.62))
if BID not in ('S01','S02'):
 # Same remeshed sign geometry, building-specific original lettering.
 sign=bpy.data.objects[f'{BID}-storefront-sign'];image=bpy.data.images.load(str(OUT/'sign-2k.png'));image.pack()
 for obj in sign.children:
  if obj.type!='MESH':continue
  obj.data=obj.data.copy()
  for i,old in enumerate(obj.data.materials):
   if old.use_nodes and any(n.type=='TEX_IMAGE' for n in old.node_tree.nodes):
    mat=old.copy();mat.name=BID+' original storefront lettering'
    for node in mat.node_tree.nodes:
     if node.type=='TEX_IMAGE':node.image=image
    obj.data.materials[i]=mat
if BID!='S01':
 from street_city_surface_materials import apply as apply_finishes
 bpy.context.view_layer.update()
 finish_folder=OUT/'materials' if BID=='S02' else ROOT/'output/street-social-city-production/S02-modular-components-v1/assembly-building/materials'
 finish_report=apply_finishes(sc,finish_folder)
 (OUT/'material-report.json').write_text(json.dumps(finish_report,ensure_ascii=False,indent=2),'utf-8')
from street_city_shop_wall_styles import apply as apply_wall_style
wall_style=apply_wall_style(sc,BID,b)
bpy.context.view_layer.update();meshes=[o for o in sc.objects if o.type=='MESH'];counts={}
for no in roots:
 os=[o for o in meshes if o['floor']==no];count=0
 for o in os:o.data.calc_loop_triangles();count+=len(o.data.loop_triangles)
 assert count<=100000,(no,count)
 counts[no]=count
 for o in sc.objects:o.select_set(False)
 for o in [roots[no]]+list(roots[no].children_recursive):o.select_set(True)
 bpy.ops.export_scene.gltf(filepath=str(OUT/f'{BID}-F{no:02d}-complete-2k.glb'),export_format='GLB',use_selection=True,export_extras=True,export_animations=False)
for o in sc.objects:o.select_set(True)
bpy.ops.export_scene.gltf(filepath=str(OUT/f'{BID}-complete-2k.glb'),export_format='GLB',export_extras=True,export_animation_mode='SCENE',export_anim_scene_split_object=False)
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/f'{BID}-complete-2k.blend'))
routes={f['floor']:[dict(id='main',bounds=f['internal_route_bounds_m'])]+[dict(id=c['id'],bounds=c['bounds_m']) for c in f['cells'] if c['kind']=='corridor'] for f in floors}
# Route around the apartment entry's inward swing, instead of through it.
if BID=='S01':routes[3].append(dict(id='apartment-cross',bounds=[-5.8,-3.5,-1.6,-1.5]))
elif BID=='S02':
 for no in [2,3]:routes[no].append(dict(id='staff-cross',bounds=[-4.8,-4.5,-.6,-2.5]))
 routes[1].append(dict(id='production-aisle',bounds=[0,-6.2,3.2,-3.8]))
 routes[2].append(dict(id='cold-storage-access',bounds=[2.8,-5.85,4.8,-3.8]))
else:
 for f in floors:
  if f['internal_cross_route_bounds_m']:
   a,bb,c,dd=f['internal_cross_route_bounds_m'];routes[f['floor']].append(dict(id='residence-cross',bounds=[a,bb-1.3,c,dd-1.3]))
report=dict(status='assembled_requires_acceptance',building=BID,footprint_m=b['footprint_m'],height_m=11,floor_triangles=counts,triangles=sum(counts.values()),sources=sources,instances=instances,doors=doors,routes=routes,lift=dict(cab_root=cab.name,stops=[0,3.8,7.6],frames=[1,80,160],count=1),layout_adjustments=(['F02/F03 staff cross route adjusted; dry racks shortened to keep clear2m passage; staff desk shifted behind passage'] if BID=='S02' else ['F03 cross route shifted to y[-3.5,-1.5] to avoid inward apartment door swing','Top storey has incoming stair opening only; sealed roof has no outgoing flight','Shared slabs belong to upper floor only; ground slab uncut']),limitations=['Geometry and Blender animation only; game runtime controllers/navigation/collision integration pending','Curved sanitary components use documented source topology recovery; chair/toilet have source open seams'])
report['wall_style']=wall_style
report['world_placement']=dict(position_m=b.get('world_position_m'),rotation_z_deg=C.get('world_yaw_deg',0),local_front='south',units='metres')
(OUT/'assembly-report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),'utf-8')
# Render every changed bedroom, and all views when local enclosed rooms change.
full_review=not args.quick_preview or BID in ('S01','S02') or any(f['private_rooms'] and next(c for c in f['cells'] if c['kind']=='activity')['open'] for f in floors)
def render_previews(full_review):
 # Presentation only; the saved blend and GLB retain full walls and roof.
 sc.render.engine='CYCLES';sc.cycles.samples=24;sc.render.resolution_x=1300;sc.render.resolution_y=1400;sc.render.resolution_percentage=100;sc.world=bpy.data.worlds.new('Studio');sc.world.use_nodes=True;sc.world.node_tree.nodes['Background'].inputs[0].default_value=(.6,.6,.6,1)
 if args.quick_preview:sc.cycles.samples=8;sc.render.resolution_x=700;sc.render.resolution_y=800
 target=Vector((0,0,5))
 for loc,power in [((-8,-15,23),4000),((12,8,24),3500)]:
  bpy.ops.object.light_add(type='AREA',location=loc);o=bpy.context.object;o.data.energy=power;o.data.size=10;o.rotation_euler=(target-o.location).to_track_quat('-Z','Y').to_euler()
 bpy.ops.object.camera_add(location=(25,-35,23));camera=bpy.context.object;camera.rotation_euler=(target-camera.location).to_track_quat('-Z','Y').to_euler();camera.data.type='ORTHO';camera.data.ortho_scale=27;sc.camera=camera
 if full_review:sc.render.filepath=str(OUT/f'{BID}-exterior.png');bpy.ops.render.render(write_still=True)
 for no in roots:
  if not full_review and no!=3:continue
  for o in meshes:
   o.hide_render=o['floor']!=no or o.get('part_kind')=='ceiling' or o.get('asset_id')=='ceiling-light'
   if o.get('part_kind')=='wall':
    pts=[o.matrix_world@v.co for v in o.data.vertices]
    if max(q.y for q in pts)<-C['depth']/2+.3 or min(q.x for q in pts)>C['width']/2-.3:o.hide_render=True
  camera.location=(23,-30,38+floors[no-1]['z_m']);target=Vector((0,0,floors[no-1]['z_m']+1.2));camera.rotation_euler=(target-camera.location).to_track_quat('-Z','Y').to_euler();camera.data.ortho_scale=25
  sc.render.filepath=str(OUT/f'{BID}-F{no:02d}-cutaway.png');bpy.ops.render.render(write_still=True)
 print(json.dumps(dict(triangles=report['triangles'],floors=counts)))

render_previews(full_review)
