"""Build a complete campus building from certified primitive/Tripo sources."""
import bpy,sys,json,math,argparse,hashlib
from pathlib import Path
from mathutils import Vector,Matrix
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_remaining_contract import context,digest,OUT as PROJECT
CAMPUS=Path(__file__).resolve().parents[1]/'output/street-social-city-production/campus-v1'
from street_city_remaining_structure import plan
from street_city_remaining_recipes import recipe
from street_city_campus_recipes import p,shift,cabinet
from street_city_campus_sports import recipe as sporting
from street_city_remesh_gate import verify
code_files=['assemble_street_city_remaining.py','street_city_remaining_contract.py','street_city_remaining_recipes.py','street_city_remaining_structure.py','street_city_remaining_finishes.py','street_city_campus_recipes.py','street_city_campus_sports.py','street_city_surface_materials.py']
recipe_sources={str(Path(__file__).parent/name):hashlib.sha256((Path(__file__).parent/name).read_bytes()).hexdigest() for name in code_files}
p_arg=argparse.ArgumentParser();p_arg.add_argument('--building',required=True);a=p_arg.parse_args(sys.argv[sys.argv.index('--')+1:]);C=context(a.building);OUT=C['out'];OUT.mkdir(parents=True,exist_ok=True);B=C['building'];BID=C['id'];geometry=plan(C)
bpy.ops.wm.read_factory_settings(use_empty=True);sc=bpy.context.scene;sc.unit_settings.system='METRIC';sources={};cache={};mesh_cache={};instances=[];roots={};materials={};mesh_records=[]
colors={'wood':(.48,.31,.17),'paint':(.78,.77,.72),'tile':(.68,.72,.71),'steel':(.38,.4,.41),'black':(.025,.03,.032),'ivory':(.83,.82,.76),'fabric':(.42,.48,.45),'glass':(.65,.8,.82),'screen':(.1,.25,.32),'book0':(.17,.3,.27),'book1':(.46,.21,.15),'book2':(.72,.63,.4),'court':(.08,.3,.22),'drafting':(.38,.46,.47),'water':(.08,.4,.48),'orange':(.82,.24,.04),'net':(.8,.8,.75),'rubber':(.025,.03,.03),'light':(.94,.93,.82),'mirror':(.85,.88,.9)}
for key,color in colors.items():
 m=bpy.data.materials.new('Campus '+key);m.use_nodes=True;bs=m.node_tree.nodes['Principled BSDF'];bs.inputs['Base Color'].default_value=(*color,1);bs.inputs['Roughness'].default_value=.13 if key in ('water','glass','mirror') else .65
 if key in ('steel','mirror'):bs.inputs['Metallic'].default_value=.9
 if key=='glass':bs.inputs['Alpha'].default_value=.22;m.surface_render_method='DITHERED'
 if key=='water':bs.inputs['Transmission Weight'].default_value=.65
 if key=='light':bs.inputs['Emission Color'].default_value=(*color,1);bs.inputs['Emission Strength'].default_value=3
 materials[key]=m
wood_image=CAMPUS/'materials/oak-2k.png'
net_path=CAMPUS/'materials/net-grid-2k.png'
if not net_path.exists():
 from PIL import Image,ImageDraw
 im=Image.new('RGBA',(2048,2048),(220,225,220,0));draw=ImageDraw.Draw(im)
 for k in range(11):
  n=round(k*204.8);draw.line((n,0,n,2048),fill=(220,225,220,255),width=3);draw.line((0,n,2048,n),fill=(220,225,220,255),width=3)
 im.save(net_path)
net=bpy.data.materials.new('Campus true transparent sports net');net.use_nodes=True;net.surface_render_method='DITHERED';tex=net.node_tree.nodes.new('ShaderNodeTexImage');tex.image=bpy.data.images.load(str(net_path));tex.image.pack();bs=net.node_tree.nodes['Principled BSDF'];net.node_tree.links.new(tex.outputs['Color'],bs.inputs['Base Color']);net.node_tree.links.new(tex.outputs['Alpha'],bs.inputs['Alpha']);materials['netgrid']=net
if wood_image.exists():
 m=materials['wood'];node=m.node_tree.nodes.new('ShaderNodeTexImage');node.image=bpy.data.images.load(str(wood_image));node.image.pack();m.node_tree.links.new(node.outputs['Color'],m.node_tree.nodes['Principled BSDF'].inputs['Base Color'])
for room in C['rooms'].values():
 path=PROJECT/f'materials/{BID}/{room["room_id"]}.png';m=bpy.data.materials.new('Room sign '+room['room_id']);m.use_nodes=True;node=m.node_tree.nodes.new('ShaderNodeTexImage');node.image=bpy.data.images.load(str(path));node.image.pack();m.node_tree.links.new(node.outputs['Color'],m.node_tree.nodes['Principled BSDF'].inputs['Base Color']);materials['sign-'+room['room_id']]=m
 from street_city_localization import register_material
 register_material(m,'room.'+room['room_id'])
matkeys=list(materials)

def load(aid):
 if aid in cache:return cache[aid]
 from street_city_remaining_recipes import component_folder
 folder=component_folder(aid);receipt=verify(folder);sources[aid]=receipt;old=set(bpy.data.objects);bpy.ops.import_scene.gltf(filepath=receipt['path']);new=set(bpy.data.objects)-old;bpy.context.view_layer.update();result=[]
 for o in new:
  if o.type=='MESH':
   mesh=o.data.copy()
   if aid.startswith(('tripo-','lod-')):
    # Shading only: retain exactly the certified vertices and connectivity.
    mesh.normals_split_custom_set([(0,0,0)]*len(mesh.loops))
    for poly in mesh.polygons:poly.use_smooth=True
    mesh.set_sharp_from_angle(angle=math.radians(60))
   result.append((mesh,o.matrix_world.copy()))
 for o in new:bpy.data.objects.remove(o,do_unlink=True)
 cache[aid]=result;return result

def mesh_for(key,parts):
 if key in mesh_cache:return mesh_cache[key]
 vertices=[];faces=[];indices=[];smooth=[]
 for q in parts:
  aid='unit-'+q.get('primitive','panel');matrix=Matrix.Translation(Vector(q['pos']))@Matrix.Rotation(math.radians(q.get('yaw',0)),4,'Z')@Matrix.Rotation(math.radians(q.get('pitch',0)),4,'X')@Matrix.Diagonal(Vector((*q['size'],1)))
  for mesh,basis in load(aid):
   offset=len(vertices);vertices.extend(matrix@basis@v.co for v in mesh.vertices);faces.extend([offset+i for i in f.vertices] for f in mesh.polygons);indices.extend([matkeys.index(q['finish'])]*len(mesh.polygons));smooth.extend([aid!='unit-panel']*len(mesh.polygons))
 # Float32 matrix arithmetic can place the two copies of a shared board edge
 # a fraction of a micrometre apart. Snap all procedural metric coordinates
 # to a common 10-micrometre grid before export; apertures stay within 5mm.
 vertices=[tuple(round(float(v[k]),5) for k in range(3)) for v in vertices]
 mesh=bpy.data.meshes.new(key);mesh.from_pydata(vertices,[],faces);mesh.update()
 used_material_indices=sorted(set(indices));local_material_index={global_index:i for i,global_index in enumerate(used_material_indices)}
 for index in used_material_indices:mesh.materials.append(materials[matkeys[index]])
 uv=mesh.uv_layers.new(name='Metric PBR UV')
 for f,index,sm in zip(mesh.polygons,indices,smooth):
  f.material_index=local_material_index[index];f.use_smooth=sm;axis=max(range(3),key=lambda k:abs(f.normal[k]));axes=[k for k in range(3) if k!=axis]
  for li in f.loop_indices:
   co=mesh.vertices[mesh.loops[li].vertex_index].co;finish=matkeys[index]
   uv.data[li].uv=((co.x+.4)/.8,(co.z-1.8)/.2) if finish.startswith('sign-') else (co[axes[0]],co[axes[1]]/.25) if finish=='wood' else (co[axes[0]],co[axes[1]])
 mesh_cache[key]=mesh;return mesh

def obj(name,no,mesh,pos=(0,0,0),yaw=0,kind='furniture',parent=None,asset=None):
 o=bpy.data.objects.new(name,mesh);sc.collection.objects.link(o);o.parent=parent or roots[no];o.location=pos;o.rotation_euler.z=math.radians(yaw);o['floor']=no;o['part_kind']=kind;o['asset_id']=asset or name;o['remesh_basis']='certified source meshes; declared metric instance transforms';return o

for f in B['floors']:
 no=f['floor'];root=bpy.data.objects.new(f'{BID}-F{no:02d}',None);sc.collection.objects.link(root);root['floor']=no;roots[no]=root
 for kind in sorted({q['kind'] for q in geometry['parts'] if q['floor']==no}):
  parts=[dict(size=q['size'],pos=q['pos'],finish='tile' if kind in ('floor','stair_tread','stair_landing') else 'steel' if kind in ('handrail','void_guard') else 'paint') for q in geometry['parts'] if q['floor']==no and q['kind']==kind]
  if parts:obj(f'F{no:02d}-{kind}',no,mesh_for(f'F{no:02d}-{kind}',parts),kind=kind,asset=f'structure-F{no:02d}')

# Regulation-sized playing-line rectangles come from the same dimensioned
# sports layout which positions the nets and hoops, not from the illustration.
for room in C['rooms'].values():
 lines=[]
 for court in room['sport_geometry'].get('court_layouts',[]):
  x,y=court['center_m'];w,d=court['playing_lines_m'];lines.append(p((w+1,d+1,.008),(x,y,.002),'wood' if court['kind']=='basket' else 'court'))
  for xx in [-w/2,w/2]:lines.append(p((.05,d,.004),(x+xx,y,.011),'ivory'))
  for yy in [-d/2,0,d/2]:lines.append(p((w,.05,.004),(x,y+yy,.011),'ivory'))
 if lines:obj(room['room_id']+'-court-markings',room['floor'],mesh_for(room['room_id']+'-court-markings',lines),room['position_m'],room['rotation_z_deg'],kind='court')

def curved(aid,name,no,pos,yaw):
 source_id=aid if aid.startswith('product-') else ('lod-' if aid in ('toilet-compact','urinal') else 'tripo-')+aid
 for i,(mesh,basis) in enumerate(load(source_id)):
  o=obj(name+f'-mesh{i}',no,mesh,pos,yaw,asset=aid);o.matrix_basis=Matrix.Translation(Vector(pos))@Matrix.Rotation(math.radians(yaw),4,'Z')@basis

laundry_counts={}
for inst in C['instances']:
 asset=C['assets'][inst['asset_id']];role=asset['role'];aid=asset['id'];size=asset['size_m'];no=inst['floor'];pos=inst['position_m'][:];yaw=inst['rotation_z_deg'];key=aid.rsplit('-',1)[-1];name=inst['id'].replace(':','-')
 if role not in ('annex_furniture','sanitary_fixture','service_fixture','sports_annex_furniture','furniture'):continue
 owner_room=C['rooms'].get(inst['owner']);laundry_console=key=='console' and owner_room and '洗衣' in owner_room['display_name']
 if asset['name'] in ('洗衣機','烘衣機') or laundry_console:
  group=inst['owner'];index=laundry_counts.get(group,0);laundry_counts[group]=index+1
  if laundry_console and index>=4:obj(name,no,mesh_for('laundry-fold-table',recipe('COMMON-CITY-study_desk',[1,.6,.75])),pos,yaw,asset='laundry-fold-table')
  else:curved('laundry-appliance',name,no,pos,yaw)
 elif asset['name']=='摺衣檯' or (key=='console' and '洗衣' in asset['name']):
  obj(name,no,mesh_for('laundry-fold-table',recipe('COMMON-CITY-study_desk',[1,.6,.75])),pos,yaw,asset='laundry-fold-table')
 elif key=='king':
  curved('product-bed',name,no,pos,yaw);parts=None
 elif key in ('toilet','basin','sink','urinal') or asset['name']=='拖布池':
  prop='toilet-compact' if key=='toilet' else 'urinal' if key=='urinal' else 'basin-economy'
  if prop=='basin-economy':pos[2]+=.45 # bottom of exposed trap; basin lip approximately 0.85m
  if prop=='urinal':pos[2]+=.4
  curved(prop,name,no,pos,yaw);parts=None
 elif asset['name']=='飛輪車':curved('exercise-bike',name,no,pos,yaw);parts=None
 else:
  parts=sporting(asset['name'],size) if role=='furniture' else recipe(aid,size)
  if parts is None:raise ValueError('Missing actual curved asset '+aid)
  dry=[q for q in parts if q['finish']!='water'];wet=[q for q in parts if q['finish']=='water']
  if dry:obj(name,no,mesh_for(aid,dry),pos,yaw,kind='furniture',asset=aid)
  if wet:obj(name+'-water',no,mesh_for(aid+'-water',wet),pos,yaw,kind='water',asset=aid)
  if key=='tank':
   for fish_index in range(3):curved('fish',name+'-fish-'+str(fish_index),no,[pos[0]-.9+fish_index*.85,pos[1]+(-.4 if fish_index%2 else .4),pos[2]+.8+fish_index*.35],30+fish_index*80)
  if key=='microscope':curved('microscope',name+'-optical-microscope',no,[pos[0],pos[1],pos[2]+.9],yaw)
 instances.append(dict(name=name,asset=aid,role=role,floor=no,position_m=pos,yaw_deg=yaw,owner=inst['owner'],source_instance=inst['id']))

# Real framed doors; their open leaf stays outside the clear aperture.
for door in geometry['doors']:
 no=door['floor'];pos=door['position_m'];yaw=door['yaw_deg'];w=door['width_m'];h=door['height_m'];name=door['id'];frame=[p((.08,.16,h),(-w/2-.04,0,0)),p((.08,.16,h),(w/2+.04,0,0)),p((w+.16,.16,.08),(0,0,h))];obj(name+'-frame',no,mesh_for(f'frame-{w}-{h}',frame),pos,yaw,kind='doorframe')
 if door.get('kind')=='lift':
  for sign in [-1,1]:
   local=[sign*(.95 if no==1 else .31),0,0];pt=Vector(pos)+Matrix.Rotation(math.radians(yaw),4,'Z')@Vector(local);panel=obj(name+f'-panel{sign}',no,mesh_for('lift-panel',[p((.6,.04,2.4),(0,0,0),'steel')]),pt,yaw,kind='doorleaf')
   for stop_index in range(len(B['floors'])):
    frame=1+80*stop_index
    for time,opened in [(max(1,frame-20),False),(max(1,frame-10),stop_index+1==no),(frame+10,stop_index+1==no),(frame+20,False)]:
     xy=Vector(pos)+Matrix.Rotation(math.radians(yaw),4,'Z')@Vector((sign*(.95 if opened else .31),0,0));panel.location=xy;panel.keyframe_insert(data_path='location',frame=time)
 else:
  for sign in ([-1,1] if w>1.5 else [-1]):
   hinge=bpy.data.objects.new(name+f'-hinge{sign}',None);sc.collection.objects.link(hinge);hinge.parent=roots[no];hinge.location=Vector(pos)+Matrix.Rotation(math.radians(yaw),4,'Z')@Vector((sign*(w/2+.03),-.12,0));hinge.rotation_euler.z=math.radians(yaw-sign*90);hinge['floor']=no;hinge['motion']='hinge'
   leafwidth=w/2-.004 if w>1.5 else w-.004;obj(name+f'-leaf{sign}',no,mesh_for(f'leaf-{leafwidth}-{h}',[p((leafwidth,.04,h-.03),(-sign*leafwidth/2,0,0))]),kind='doorleaf',parent=hinge)
 rid=name.removesuffix('-door')
 if rid in C['rooms']:
  pt=Vector(pos)+Matrix.Rotation(math.radians(yaw),4,'Z')@Vector((w/2+.55,-.13,0));obj(rid+'-sign',no,mesh_for(rid+'-sign',[p((.8,.02,.2),(0,0,1.8),'sign-'+rid)]),pt,yaw,kind='sign')

for q in geometry['windows']:
 no=q['floor'];W,D=B['footprint_m'];side=q['side'];pos=[q['along'],-D/2+.1 if side=='south' else D/2-.1,q['z']] if side in ('south','north') else [-W/2+.1 if side=='west' else W/2-.1,q['along'],q['z']];yaw=0 if side in ('south','north') else 90
 if BID=='E1' and side=='north' and abs(q['along'])<16.6:continue
 ww=q['width_m'];hh=q['height_m'];window=[p((ww,.03,hh),(0,0,0),'glass')]+[p((.055,.08,hh),(x,0,0),'wood') for x in [-ww/2+.03,0,ww/2-.03]]+[p((ww,.08,.055),(0,0,z),'wood') for z in [0,hh-.055]];obj(f'F{no:02d}-window-{len(mesh_records)}',no,mesh_for(f'window-{ww}-{hh}',window),pos,yaw,kind='window');mesh_records.append(q)

for f in B['floors']:
 no=f['floor'];z=f['z_m'];h=f['height_m']
 for j,room in enumerate([q for q in C['rooms'].values() if q['floor']==no]):
  xx,yy,_=room['position_m'];obj(f'F{no:02d}-room-light-{j}',no,mesh_for('light',[p((1.2,.3,.04),(0,0,0),'light')]),(xx,yy,z+h-.36),kind='light')

# One dimensioned static ground-floor cabin per shaft; gameplay is deferred.
lifts=[];stops=[f['z_m'] for f in B['floors']]
for core in [c for c in B['floors'][0]['vertical_circulation'] if c['kind']=='lift']:
 cx,cy,_=core['position_m'];cw,cd,ch=core.get('car_clear_size_m',[2.1,2.8,2.4]);sid=core.get('shaft_id',core['id']);front=core['size_m'][1]/2-.28
 car=bpy.data.objects.new(BID+'-lift-'+sid,None);sc.collection.objects.link(car);car.parent=roots[1];car.location=(cx,cy+front-cd/2,0);car['floor']=1;car['stops_m']=stops
 car_parts=[p((cw+.2,cd+.2,.05),(0,0,-.05),'steel'),p((cw+.2,cd+.2,.05),(0,0,ch),'steel'),p((cw+.2,.1,ch),(0,-cd/2-.05,0),'steel')]+[p((.1,cd,ch),(x,0,0),'steel') for x in [-cw/2-.05,cw/2+.05]]
 jamb=(cw-1.2)/2
 car_parts += [p((jamb,.1,ch),(sign*(.6+jamb/2),cd/2,0),'steel') for sign in [-1,1]]
 obj(car.name+'-mesh',1,mesh_for(sid+'-car',car_parts),kind='lift',parent=car)
 for f in B['floors']:
  no=f['floor'];obj(f'F{no:02d}-lift-{sid}-threshold',no,mesh_for(sid+'-threshold',[p((1.2,.7,.05),(0,0,-.05),'steel')]),(cx,cy+core['size_m'][1]/2-.15,f['z_m']),kind='threshold')
 lifts.append(dict(shaft_id=sid,core=core['core'],root=car.name,stops_m=stops,car_clear_size_m=[cw,cd,ch],shaft_clear_size_m=core['clear_size_m'],static_cabin_floor=1))
sc.frame_end=1

# Metric exportable 2K finishes; structural surfaces retain world-scale UVs.
from street_city_surface_materials import apply
finish=apply(sc,Path('C:/portable/OpenKoikatsu/output/street-social-city-production/S02-modular-components-v1/assembly-building/materials'))
from street_city_remaining_finishes import apply as remaining_finish
remaining_finish(C,sc,roots,obj,mesh_for,materials,matkeys)
sc.frame_set(1);bpy.context.view_layer.update();counts={};water_counts={}
for no,root in roots.items():
 meshes=[o for o in root.children_recursive if o.type=='MESH'];count=0;wet=0
 for o in meshes:
  o.data.calc_loop_triangles()
  if o.get('part_kind')=='water':wet+=len(o.data.loop_triangles)
  else:count+=len(o.data.loop_triangles)
 counts[no]=count;water_counts[no]=wet
 if count+wet>100000:
  breakdown={}
  for o in meshes:breakdown[o.get('asset_id','unknown')]=breakdown.get(o.get('asset_id','unknown'),0)+len(o.data.loop_triangles)
  (OUT/'rejected-budget.json').write_text(json.dumps(dict(floor=no,triangles=count,assets=breakdown),indent=2),'utf8')
 assert count+wet<=100000,(BID,no,count+wet)
 bpy.ops.object.select_all(action='DESELECT')
 for o in [root,*root.children_recursive]:
  if o.get('part_kind')!='water':o.select_set(True)
 bpy.ops.export_scene.gltf(filepath=str(OUT/f'{BID}-F{no:02d}-complete-2k.glb'),export_format='GLB',use_selection=True,export_extras=True,export_animations=False)
water=[o for o in sc.objects if o.get('part_kind')=='water']
if water:
 bpy.ops.object.select_all(action='DESELECT')
 for o in water:o.select_set(True)
 bpy.ops.export_scene.gltf(filepath=str(OUT/f'{BID}-water-2k.glb'),export_format='GLB',use_selection=True,export_extras=True,export_animations=False)
bpy.ops.object.select_all(action='SELECT');bpy.ops.export_scene.gltf(filepath=str(OUT/f'{BID}-complete-2k.glb'),export_format='GLB',export_extras=True,export_animation_mode='SCENE',export_anim_scene_split_object=False);bpy.ops.wm.save_as_mainfile(filepath=str(OUT/f'{BID}-complete-2k.blend'))
assert all(hashlib.sha256(Path(path).read_bytes()).hexdigest()==sha for path,sha in recipe_sources.items()),'Recipe changed during production; rerun this building'
report=dict(status='assembled_requires_acceptance',building=BID,contract_sha256=digest(C),footprint_m=B['footprint_m'],floor_triangles=counts,water_triangles=water_counts,sources=sources,recipe_sources=recipe_sources,instances=instances,doors=geometry['doors'],windows=geometry['windows'],lifts=lifts,interior_adjustments=C['interior_adjustments'],units=C['units'],references=C['references'],dimension_basis=C['dimension_basis'],materials=finish,limitations=['Door/elevator gameplay controllers not connected; verify static environment separately'])
(OUT/'assembly-report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),'utf8');(OUT/'structure-plan.json').write_text(json.dumps(geometry,ensure_ascii=False),'utf8');print(BID,'assembled',counts)
