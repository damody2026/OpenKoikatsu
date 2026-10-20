"""Assemble actual inn/spa floors exclusively from verified remeshed assets."""
import bpy,json,math,sys,argparse,hashlib
from pathlib import Path
from mathutils import Vector,Matrix
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_large_shop_contract import context,contract_hash
from street_city_building_context import ROOT
from street_city_retail_profiles import COMMON
from street_city_remesh_gate import verify
p=argparse.ArgumentParser();p.add_argument('--building',required=True);a=p.parse_args(sys.argv[sys.argv.index('--')+1:]);C=context(a.building);b=C['building'];OUT=C['out'];BID=C['id'];W,D=b['footprint_m'];floors=b['floors'];S01=ROOT/'output/street-social-city-production/S01-modular-components-v1';S02=ROOT/'output/street-social-city-production/S02-modular-components-v1/assembly-building'
bpy.ops.wm.read_factory_settings(use_empty=True);sc=bpy.context.scene;sc.unit_settings.system='METRIC';sources={};cache={};instances=[];doors=[];roots={}
furniture={q['id']:S02/f'furniture/{q["id"]}/remesh' for q in json.loads((S02/'furniture/sources.json').read_text('utf-8'))}
furniture.update({q['id']:S01/f'assembly-building/furniture/{q["id"]}/remesh' for q in json.loads((S01/'assembly-building/furniture/sources.json').read_text('utf-8'))})
def resolve(aid):
 if aid.startswith('structure-F'):
  base=OUT/f'structure/{aid[-3:]}'
  return base/'remesh-planar' if BID=='S19' and (base/'remesh-planar/remesh-report.json').exists() else base/'remesh'
 if BID=='S19' and (COMMON/aid/'remesh-planar/remesh-report.json').exists():return COMMON/aid/'remesh-planar'
 if (COMMON/aid/'remesh/remesh-report.json').exists():return COMMON/aid/'remesh'
 if aid in ('toilet','sink'):return COMMON/(aid+('-lod4500' if aid=='toilet' else '-lod2500'))/'remesh'
 if aid in furniture:return furniture[aid]
 if aid=='chair':return S01/'generated-components/chair/remesh-production'
 if aid=='bookshelf':return S01/'board-bookshelf/remesh-box'
 return S01/f'local-components/{aid}/remesh'
def load(aid):
 if aid in cache:return cache[aid]
 folder=resolve(aid);receipt=verify(folder);old=set(bpy.data.objects);bpy.ops.import_scene.gltf(filepath=receipt['path']);new=set(bpy.data.objects)-old;bpy.context.view_layer.update();data=[]
 for o in new:
  if o.type=='MESH':o.data.use_fake_user=True;data.append((o.data,o.matrix_world.copy(),o.name,{k:str(o[k]) for k in ('part_kind','source_route') if k in o}))
 for o in new:bpy.data.objects.remove(o,do_unlink=True)
 cache[aid]=data;sources[aid]=receipt;return data
def place(aid,name,no,pos,yaw=0,scale=(1,1,1),parent=None):
 root=bpy.data.objects.new(name,None);sc.collection.objects.link(root);root.parent=parent or roots[no];root.location=pos;root.rotation_euler.z=math.radians(yaw);root.scale=scale;root['asset_id']=aid;root['floor']=no
 for mesh,matrix,n,props in load(aid):
  o=bpy.data.objects.new(name+'/'+n,mesh);sc.collection.objects.link(o);o.parent=root;o.matrix_basis=matrix
  for k,v in props.items():o[k]=v
  o['asset_id']=aid;o['floor']=no;o['instance_id']=name
 instances.append(dict(asset=aid,name=name,floor=no,position_m=list(pos),yaw_deg=yaw,scale=list(scale)));return root
def door(name,no,pos,yaw=0,width=1.2,height=2.4,kind='hinged'):
 wide=width>1.5;zs=height/2.4;place('entryframe' if wide else 'doorframe',name+'-frame',no,pos,yaw,scale=(1,1,zs))
 for sign in ([-1,1] if wide else [-1]):
  hinge=bpy.data.objects.new(name+f'-hinge-{sign}',None);sc.collection.objects.link(hinge);hinge.parent=roots[no];hinge.location=Vector(pos)+Matrix.Rotation(math.radians(yaw),4,'Z')@Vector((sign*(1.23 if wide else .63),0,0));hinge.rotation_euler.z=math.radians(yaw)+(-sign)*math.pi/2;hinge['floor']=no;hinge['motion']='hinge';hinge['closed_rotation_z']=math.radians(yaw)
  place('entryleaf' if wide else 'door',name+f'-leaf-{sign}',no,(-sign*.63,0,.02),scale=(1 if wide else .9966666667,1,zs),parent=hinge)
 doors.append(dict(id=name,floor=no,position_m=list(pos),yaw_deg=yaw,width_m=width,height_m=height,kind=kind))
mapping={'COMMON-WC-toilet':'toilet','COMMON-WC-basin':'sink','COMMON-WC-mirror':'wall-mirror','COMMON-WC-grab_rails':'large-grab-rails','COMMON-WC-bin':'large-bin','COMMON-WC-standard_cubicle':'large-standard-cubicle','COMMON-WC-accessible_cubicle':'large-accessible-cubicle','COMMON-CITY-locker':'large-locker','COMMON-CITY-wardrobe':'large-wardrobe','COMMON-CITY-campus_wardrobe':'large-campus-wardrobe','COMMON-CITY-console':'large-console','COMMON-CITY-king':'bed','COMMON-CITY-sofa':'sofa','COMMON-CITY-shower':'shower','COMMON-CITY-basin':'sink','COMMON-CITY-shelf':'retail-rack','COMMON-CITY-seat':'chair','COMMON-CITY-study_desk':'table','COMMON-CITY-workstation':'table','COMMON-CITY-checkout':'steel-counter','COMMON-CITY-spa_basin':'large-spa-basin','COMMON-CITY-bunk':'large-bunk','COMMON-CITY-lounge':'large-lounger'}
mapping['COMMON-CITY-spa_basin']='large-spa-basin-v2'
for f in floors:
 no=f['floor'];z=f['z_m'];root=bpy.data.objects.new(f'{BID}-F{no:02d}',None);sc.collection.objects.link(root);root['floor']=no;roots[no]=root;place(f'structure-F{no:02d}',f'F{no:02d}-structure',no,(0,0,0))
 structure=json.loads((OUT/f'structure/F{no:02d}/source.json').read_text('utf-8'))
 for i,q in enumerate(structure['windows']):
  side=q['side'];pos=[q['along_m'],-D/2+.1,z+q['sill_m']] if side=='south' else [q['along_m'],D/2-.1,z+q['sill_m']] if side=='north' else [W/2-.1,q['along_m'],z+q['sill_m']];place('window',f'F{no:02d}-window-{i}',no,pos,90 if side=='east' else 0)
 for q in structure['doors']:
  if q['id'].startswith('entrance-'):continue # explicit source door instances below
  if q.get('kind')=='lift':continue
  door(q['id'],no,q['position_m'],{'south':0,'north':180,'east':90,'west':-90}[q['side']])
 for j,q in enumerate(C['instances']):
  if q['floor']!=no:continue
  asset=C['assets'][q['asset_id']];role=asset['role'];aid=asset['id'];pos=q['position_m'];yaw=q['rotation_z_deg'];label=f'F{no:02d}-fixture-{j}'
  if role=='door':
   frame=C['door_frames'].get(aid)
   assert frame,('missing actual aperture for door',aid)
   door(aid,no,frame['position_m'],frame['yaw_deg'],width=frame['width_m'],height=frame['height_m']);bpy.data.objects[aid+'-frame']['source_instance_id']=q['id'];continue
  if role not in ('annex_furniture','sanitary_fixture','service_fixture'):continue
  if role=='service_fixture':resolved='handwash' if '-A1-fixture1' in aid else 'large-service-tall' if '-A1-fixture2' in aid else 'large-service-cabinet'
  else:
   assert aid in mapping,('unmapped fixture',aid);resolved=mapping[aid]
  scale=(1,1,1)
  if aid=='COMMON-CITY-king':scale=(2/2.2,1,1)
  if aid=='COMMON-CITY-sofa':scale=(1.25,.85/.9,.9)
  if aid=='COMMON-CITY-shelf':scale=(1.5,.6/.45,1)
  if aid=='COMMON-CITY-study_desk':scale=(.8/1.2,.6/.8,1)
  if aid=='COMMON-CITY-checkout':scale=(2.4/1.3,1,1)
  fixture=place(resolved,label,no,pos,yaw,scale);fixture['planned_interactions']=['use'];fixture['runtime_connected']=False;fixture['source_instance_id']=q['id']
  if resolved in ('large-standard-cubicle','large-accessible-cubicle'):
   d=1.9 if resolved=='large-standard-cubicle' else 2.3;dp=Vector(pos)+Matrix.Rotation(math.radians(yaw),4,'Z')@Vector((0,-d/2+.025,0));door(label+'-cubicle-door',no,list(dp),yaw,height=2.2)
  if aid=='COMMON-CITY-workstation':
   for k,offset in enumerate((-.75,.75)):
    pt=Vector(pos)+Matrix.Rotation(math.radians(yaw),4,'Z')@Vector((0,offset,0));place('chair',label+f'-chair-{k}',no,list(pt),yaw+(180 if offset<0 else 0))
  if aid=='COMMON-CITY-spa_basin':
   for k,offset in enumerate((-.6,.6)):
    pt=Vector(pos)+Matrix.Rotation(math.radians(yaw),4,'Z')@Vector((offset,-4,0));lounger=place('large-lounger',label+f'-relax-{k}',no,list(pt),yaw+180);lounger['planned_interactions']=['sit','rest'];lounger['runtime_connected']=False
   pt=Vector(pos)+Matrix.Rotation(math.radians(yaw),4,'Z')@Vector((0,-4,0));place('table',label+'-relax-table',no,list(pt),yaw,scale=(.35,.5,.65))
 # Lighting is placed over routes/open rooms and checked against actual geometry.
 for j,room in enumerate([q for q in C['rooms'].values() if q['floor']==no]):place('ceiling-light',f'F{no:02d}-room-light-{j}',no,[*room['position_m'][:2],z+f['height_m']-.36])
for core in [c for c in floors[0]['vertical_circulation'] if c['kind']=='lift']:
 cx,cy,_=core['position_m'];door_y=cy+core['size_m'][1]/2-.1;cab=place('large-lift-car',BID+'-single-cab',1,(cx,door_y-1.45,0));stops=[f['z_m'] for f in floors];frames=[1,80,160]
 for frame,stop in [(1,stops[0]),(20,stops[0]),(65,stops[1]),(100,stops[1]),(145,stops[2]),(160,stops[2])]:cab.location.z=stop;cab.keyframe_insert(data_path='location',frame=frame)
 for f in floors:
  no=f['floor'];z=f['z_m'];place('doorframe',f'F{no:02d}-lift-frame',no,(cx,door_y,z),180);place('large-lift-threshold',f'F{no:02d}-lift-threshold',no,(cx,door_y,z))
  for sign in (-1,1):
   panel=place('liftpanel',f'F{no:02d}-landing-door-panel-{sign}',no,(cx+sign*.31,door_y,z))
   keys={1:[(1,True),(10,True),(20,False),(160,False)],2:[(1,False),(65,False),(75,True),(90,True),(100,False),(160,False)],3:[(1,False),(145,False),(155,True),(160,True)]}[no]
   for frame,opened in keys:panel.location.x=cx+sign*(.95 if opened else .31);panel.keyframe_insert(data_path='location',frame=frame)
  doors.append(dict(id=f'F{no:02d}-lift-door',floor=no,position_m=[cx,door_y,z],yaw_deg=180,width_m=1.2,height_m=2.4,kind='lift'))
 for sign in (-1,1):
  panel=place('liftpanel',BID+f'-car-door-{sign}',1,(sign*.31,1.45,0),parent=cab)
  for frame,opened in [(1,True),(10,True),(20,False),(65,False),(75,True),(90,True),(100,False),(145,False),(155,True),(160,True)]:panel.location.x=sign*(.95 if opened else .31);panel.keyframe_insert(data_path='location',frame=frame)
sc.frame_end=160;sc.frame_set(1);bpy.context.view_layer.update()
from street_city_surface_materials import apply
finish=apply(sc,S02/'materials');(OUT/'material-report.json').write_text(json.dumps(finish,ensure_ascii=False,indent=2),'utf-8')
from street_city_shop_wall_styles import apply as apply_wall_style
wall_style=apply_wall_style(sc,BID,b)
meshes=[o for o in sc.objects if o.type=='MESH'];counts={}
for no,root in roots.items():
 count=0
 for o in [x for x in meshes if x['floor']==no]:o.data.calc_loop_triangles();count+=len(o.data.loop_triangles)
 counts[no]=count
 if count>100000:
  breakdown={}
  for obj in [x for x in meshes if x['floor']==no]:obj.data.calc_loop_triangles();aid=obj['asset_id'];breakdown[aid]=breakdown.get(aid,0)+len(obj.data.loop_triangles)
  (OUT/'rejected-budget.json').write_text(json.dumps(dict(building=BID,floor=no,triangles=count,asset_triangles=breakdown,status='rejected_not_adopted'),indent=2),'utf-8')
 assert count<=100000,(BID,no,count)
 for o in sc.objects:o.select_set(False)
 for o in [root,*root.children_recursive]:o.select_set(True)
 bpy.ops.export_scene.gltf(filepath=str(OUT/f'{BID}-F{no:02d}-complete-2k.glb'),export_format='GLB',use_selection=True,export_extras=True,export_animations=False)
for o in sc.objects:o.select_set(True)
bpy.ops.export_scene.gltf(filepath=str(OUT/f'{BID}-complete-2k.glb'),export_format='GLB',export_extras=True,export_animation_mode='SCENE',export_anim_scene_split_object=False);bpy.ops.wm.save_as_mainfile(filepath=str(OUT/f'{BID}-complete-2k.blend'))
report=dict(interior_adjustments=C.get('interior_adjustments',[]),building=BID,status='assembled_requires_acceptance',approved=False,footprint_m=[W,D],height_m=floors[-1]['z_m']+floors[-1]['height_m'],triangles=sum(counts.values()),floor_triangles=counts,sources=sources,instances=instances,doors=doors,lift=dict(cab_root=cab.name,center_m=[cx,door_y-1.45],door_center_m=[cx,door_y],stops=stops,frames=frames),world_placement=dict(position_m=b['world_position_m'],rotation_z_deg=0),limitations=['Game controllers/navigation/collision not integrated','Board fixtures are local construction; curved sanitary assets reuse accepted Tripo geometry'])
report['wall_style']=wall_style
report['contract_sha256']=contract_hash(C);(OUT/'assembly-report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),'utf-8')
sc.render.engine='CYCLES';sc.cycles.samples=8;sc.render.resolution_x=900;sc.render.resolution_y=900;sc.render.resolution_percentage=100;sc.world=bpy.data.worlds.new('Studio');sc.world.use_nodes=True;sc.world.node_tree.nodes['Background'].inputs[0].default_value=(.6,.6,.6,1);extent=max(W,D)
for location in [(-extent,-extent,extent*1.7),(extent,extent,extent*1.5)]:
 bpy.ops.object.light_add(type='AREA',location=location);o=bpy.context.object;o.data.energy=9000;o.data.size=extent;o.rotation_euler=(Vector((0,0,2))-o.location).to_track_quat('-Z','Y').to_euler()
bpy.ops.object.camera_add(location=(extent,-extent*1.3,extent));camera=bpy.context.object;camera.data.type='ORTHO';camera.data.ortho_scale=extent*1.65;camera.rotation_euler=(Vector((0,0,4))-camera.location).to_track_quat('-Z','Y').to_euler();sc.camera=camera;sc.render.filepath=str(OUT/f'{BID}-exterior.png');bpy.ops.render.render(write_still=True)
for f in floors:
 no=f['floor'];z=f['z_m']
 for o in meshes:
  o.hide_render=o['floor']!=no or o.get('part_kind')=='ceiling' or o.get('asset_id')=='ceiling-light'
  if o.get('part_kind')=='wall':
   pts=[o.matrix_world@v.co for v in o.data.vertices]
   if max(q.y for q in pts)<-D/2+.3 or min(q.x for q in pts)>W/2-.3:o.hide_render=True
 camera.location=(extent,-extent*1.3,extent*1.3+z);camera.rotation_euler=(Vector((0,0,z+1))-camera.location).to_track_quat('-Z','Y').to_euler();sc.render.filepath=str(OUT/f'{BID}-F{no:02d}-cutaway.png');bpy.ops.render.render(write_still=True)
print(BID,counts)
