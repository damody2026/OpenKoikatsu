"""Build actual blackboards, stepped lecture seating, office computers and opaque mall infill."""
import bpy,sys,json,math,argparse,hashlib
from pathlib import Path
from mathutils import Matrix,Vector
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_functional_revision import ROOT,OUT,sha,read
from street_city_remesh_gate import verify
from street_city_campus_recipes import p,shift
P=argparse.ArgumentParser();P.add_argument('--building',required=True);A=P.parse_args(sys.argv[sys.argv.index('--')+1:]);BID=A.building
baseline=read(OUT/'baseline.json');row=next(r for r in baseline['buildings'] if r['building']==BID);source=Path(row['source']);assert sha(source)==row['source_sha256']
signature=hashlib.sha256(json.dumps([sha(__file__),sha(ROOT/'Tools/street_city_functional_revision.py'),row['source_sha256']],sort_keys=True).encode()).hexdigest();folder=OUT/'buildings'/BID/signature[:16];folder.mkdir(parents=True,exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=str(source));sc=bpy.context.scene;sc.unit_settings.system='METRIC';bpy.context.view_layer.update();original=set(sc.objects);sc.frame_set(1)
if BID=='M1':
 from street_city_mall import contract
 C=None;contract=contract();floors=contract['floors']
else:
 from street_city_campus_contract import IDS,context as campus
 from street_city_remaining_contract import context as remaining
 C=campus(BID) if BID in IDS else remaining(BID);floors=C['building']['floors']
panel=verify(ROOT/'output/street-social-city-production/campus-v1/components/unit-panel/remesh');old=set(sc.objects);bpy.ops.import_scene.gltf(filepath=panel['path']);bpy.context.view_layer.update();loaded=set(sc.objects)-old;basis=[(o.data.copy(),o.matrix_world.copy()) for o in loaded if o.type=='MESH']
for o in loaded:bpy.data.objects.remove(o,do_unlink=True)
def mat(key,color):
 m=bpy.data.materials.new('Functional '+key);m.use_nodes=True;bs=m.node_tree.nodes['Principled BSDF'];bs.inputs['Base Color'].default_value=(*color,1);bs.inputs['Roughness'].default_value=.55;return m
mats={'board':mat('blackboard',(.025,.105,.07)),'screen':mat('projection fabric',(.88,.88,.83)),'frame':mat('aluminium',(.22,.24,.25)),'wood':mat('tier oak',(.36,.22,.12)),'dark':mat('computer chassis',(.025,.03,.035)),'display':mat('computer display',(.08,.22,.3)),'key':mat('keyboard',(.19,.2,.21)),'edge':mat('step nosing',(.65,.57,.32))}
records=[];moves=[];removed=[];admin=[];classrooms=[];opaque=[]
def add(name,parts,origin,yaw,no,kind,owner):
 vertices=[];faces=[];mi=[];keys=list(mats)
 for q in parts:
  t=Matrix.Translation(Vector(q['pos']))@Matrix.Rotation(math.radians(q.get('yaw',0)),4,'Z')@Matrix.Diagonal(Vector((*q['size'],1)))
  for mesh,b in basis:
   off=len(vertices);vertices.extend(t@b@v.co for v in mesh.vertices);faces.extend([off+i for i in poly.vertices] for poly in mesh.polygons);mi.extend([keys.index(q['finish'])]*len(mesh.polygons))
 mesh=bpy.data.meshes.new(name);mesh.from_pydata(vertices,[],faces);mesh.update()
 for key in keys:mesh.materials.append(mats[key])
 uv=mesh.uv_layers.new(name='Metric UV')
 for poly,k in zip(mesh.polygons,mi):
  poly.material_index=k;axis=max(range(3),key=lambda j:abs(poly.normal[j]));axes=[j for j in range(3) if j!=axis]
  for li in poly.loop_indices:
   v=mesh.vertices[mesh.loops[li].vertex_index].co;uv.data[li].uv=(v[axes[0]],v[axes[1]])
 obj=bpy.data.objects.new(name,mesh);sc.collection.objects.link(obj);obj.location=origin;obj.rotation_euler.z=math.radians(yaw);obj['floor']=no;obj['part_kind']=kind;obj['owner_room']=owner;obj['asset_id']='functional-'+kind;obj['remesh_source_sha256']=panel['sha256'];obj['revision']='functional-interiors-v1'
 records.append(dict(name=name,floor=no,kind=kind,owner=owner,parts=parts,position_m=list(origin),yaw=yaw));return obj
def meshes(inst):
 prefix=inst['id'].replace(':','-')
 return [o for o in original if o.type=='MESH' and (o.name==prefix or o.name.startswith(prefix+'-mesh'))]
if C:
 for room in C['rooms'].values():
  rid=room['room_id'];name=room['display_name'];no=room['floor'];w,d,h=room['clear_size_m'];pos=room['position_m'];yaw=room['rotation_z_deg'];kind=room.get('functional_programme',{}).get('kind');items=[i for i in C['instances'] if i['owner']==rid]
  teaching=room.get('room_type')=='class' or '教室' in name
  if teaching:
   # One continuous board on the front wall; the lowered fabric overlaps its
   # centre, toward the students. Keep exposed writing wings on both sides.
   bw=min(8,w-4);sw=min(3.6,bw*.6);by=d/2-.08;sy=by-.16
   board=[p((bw,.06,1.5),(0,by,1.05),'board'),
          p((bw+.12,.10,.06),(0,by,1.0),'frame'),
          p((bw+.12,.10,.06),(0,by,2.55),'frame'),
          p((.06,.10,1.5),(-bw/2-.03,by,1.05),'frame'),
          p((.06,.10,1.5),(bw/2+.03,by,1.05),'frame'),
          p((bw+.12,.18,.035),(0,by-.055,1.0),'frame'),
          p((sw,.025,1.8),(0,sy,1.05),'screen'),
          p((sw+.16,.15,.12),(0,sy,2.85),'frame'),
          p((sw+.08,.065,.045),(0,sy,1.015),'frame'),
          p((.36,.28,.13),(0,1,h-.45),'frame'),
          p((.06,.06,.32),(0,1,h-.32),'frame')]
   add(rid+'-teaching-board-screen',board,pos,yaw,no,'teaching_display',rid)
   stations=[i for i in items if i['asset_id'].rsplit('-',1)[-1] in ('student_station','campus_language','campus_business')];tiered=kind=='teaching' and w>=14 and len(stations)>=15
   classroom=dict(room=rid,building=BID,floor=no,name=name,board=True,projection_screen=True,display_arrangement='continuous_centered_blackboard_with_lowered_retractable_screen_in_front',blackboard_size_m=[bw,.06,1.5],screen_size_m=[sw,.025,1.8],screen_front_offset_m=.16,screen_state='lowered',tiered=tiered,clear_size_m=[w,d,h],seats_before=2*len(stations),seats_after=2*len(stations))
   if tiered:
    inv=Matrix.Rotation(math.radians(-yaw),4,'Z');kept=[]
    for inst in stations:
     local=inv@(Vector(inst['position_m'])-Vector(pos));objects=meshes(inst);assert objects,inst['id']
     if abs(local.x)>4.6:
      removed.extend(dict(name=o.name,room=rid,reason='reserve continuous 2m flat side bypass') for o in objects)
      for o in objects:original.remove(o);bpy.data.objects.remove(o,do_unlink=True)
      continue
     lift=.48 if local.y<-.9 else .24 if local.y<1.3 else 0
     for o in objects:
      before=list(o.location);o.location.z+=lift;o['lecture_tier_m']=lift;o['owner_room']=rid;moves.append(dict(name=o.name,room=rid,before=before,after=list(o.location),lift_m=lift))
     kept.append(inst)
    parts=[]
    for yy,depth,height in [(-1.9,2,.48),(.2,2.2,.24)]:
     for xx in [-2.8,2.8]:parts.append(p((3.6,depth,height),(xx,yy,0),'wood'))
    # 2m-wide central access: rear four 120mm rises, then two 120mm descents per row.
    for y,dep,height in [(-2.85,.3,.12),(-2.55,.3,.24),(-2.25,.3,.36),(-1.65,.9,.48),(-1.05,.3,.36),(-.025,1.75,.24),(1.0,.3,.12)]:
     if dep>0:parts.append(p((2,dep,height),(0,y,0),'wood'));parts.append(p((2,.035,.008),(0,y-dep/2,height),'edge'))
    for sign in [-1,1]:
     for yy,dep,height in [(-1.9,2,.48),(.2,2.2,.24)]:
      parts+=[p((.04,dep,.04),(sign*4.58,yy,height+.9),'frame')]+[p((.04,.04,.9),(sign*4.58,yy+s*(dep/2-.06),height),'frame') for s in [-1,1]]
    add(rid+'-lecture-tiers',parts,pos,yaw,no,'lecture_tiers',rid)
    classroom.update(seats_after=2*len(kept),row_rise_m=.24,step_rise_m=.12,central_aisle_width_m=2,flat_side_bypass_m=w/2-4.62,maximum_tier_m=.48,minimum_headroom_m=h-.48,accessible_front_seats=True)
   classrooms.append(classroom)
  administrative=kind in ('admin','reception') or room.get('room_type') in ('office','administration','admin') or any(t in name for t in ('辦公','系辦','站務','售票','旅遊資訊','志工服務','銀行服務'))
  if administrative:
   work=[]
   for inst in items:
    key=inst['asset_id'].rsplit('-',1)[-1]
    if key not in ('workstation','computer','lectern','checkout','console','record','study_desk','table','worktop','desk'):continue
    objects=meshes(inst);assert objects,inst['id'];has_screen=any('screen' in m.name.lower() for o in objects for m in o.data.materials if m)
    top=.75 if key in ('workstation','computer','study_desk','table','desk') else min(.95,C['assets'][inst['asset_id']]['size_m'][2]);parts=[]
    if has_screen:
     parts=[p((.36,.13,.018),(0,-.13,top+.005),'key'),p((.055,.085,.023),(.25,-.13,top+.005),'dark'),p((.12,.1,.025),(.3,.16,top+.005),'dark')]
    else:
     parts=[p((.34,.23,.018),(0,0,top+.005),'dark'),p((.34,.018,.22),(0,.105,top+.023),'dark'),p((.30,.005,.18),(0,.092,top+.045),'display'),p((.28,.1,.004),(0,-.015,top+.025),'key')]
    add(inst['id'].replace(':','-')+'-computer-kit',parts,inst['position_m'],inst['rotation_z_deg'],no,'office_computer',rid);work.append(dict(instance=inst['id'],existing_monitor=has_screen,added='keyboard_mouse_mini_pc' if has_screen else 'laptop',desk_top_m=top))
   if not work:
    # A reception is not equipped merely because its programme says reception.
    # Reuse an already clear seating bay rather than blocking its entrance/corridor.
    bay=next((i for i in items if i['asset_id'].rsplit('-',1)[-1]=='sofa'),None)
    assert bay,('Administrative room has no usable workstation or safe furniture bay',rid)
    for o in meshes(bay):
     removed.append(dict(name=o.name,room=rid,reason='reception sofa bay replaced by staffed desk'));original.remove(o);bpy.data.objects.remove(o,do_unlink=True)
    deskparts=[p((1.6,.65,.04),(0,0,.71),'wood')]+[p((.04,.04,.71),(x,y,0),'frame') for x in [-.72,.72] for y in [-.25,.25]]
    add(rid+'-reception-workdesk',deskparts,bay['position_m'],bay['rotation_z_deg'],no,'office_desk',rid)
    parts=[p((.34,.23,.018),(0,0,.755),'dark'),p((.34,.018,.22),(0,.105,.773),'dark'),p((.3,.005,.18),(0,.092,.795),'display')]
    add(rid+'-reception-laptop',parts,bay['position_m'],bay['rotation_z_deg'],no,'office_computer',rid);work.append(dict(instance=bay['id'],added='reception desk and laptop in verified former sofa bay',desk_top_m=.75))
   admin.append(dict(room=rid,building=BID,floor=no,name=name,workstations=work))
if BID=='M1':
 # Source windows are consolidated per-floor meshes; replace each transparent bay with solid 180mm infill.
 for o in list(original):
  if o.type=='MESH' and o.get('part_kind')=='window':original.remove(o);bpy.data.objects.remove(o,do_unlink=True)
 from street_city_mall import plan
 g=plan()
 for i,q in enumerate(q for q in g['parts'] if q['kind']=='window'):
  size=q['size'][:];size[1 if size[0]>size[1] else 0]=.18;no=q['floor'];finish='M1 honed limestone' if no<=3 else 'M1 upper ivory rainscreen';m=next(m for m in bpy.data.materials if m.name.startswith(finish));key='opaque'+str(no);mats[key]=m
  bs=m.node_tree.nodes.get('Principled BSDF');assert bs;bs.inputs['Alpha'].default_value=1;bs.inputs['Transmission Weight'].default_value=0;m.surface_render_method='DITHERED';m['opaque_mall_envelope']=True
  o=add(f'M1-F{no:02d}-opaque-bay-{i}',[p(size,(0,0,0),key)],q['pos'],q['yaw'],no,'opaque_envelope','M1');opaque.append(dict(name=o.name,floor=no,size_m=size,position_m=q['pos']))
 # Office/management rooms: actual manager tables are present in the source, but lacked computers.
 for f in floors:
  no=f['floor'];z=f['z_m'];add(f'M1-F{no:02d}-management-laptop',[p((.34,.23,.018),(0,0,.755),'dark'),p((.34,.018,.22),(0,.105,.773),'dark'),p((.3,.005,.18),(0,.092,.795),'display')],[15,45.2,z],0,no,'office_computer',f'M1-F{no:02d}-management');admin.append(dict(room=f'M1-F{no:02d}-management',building=BID,floor=no,workstations=[dict(added='laptop')]))
bpy.context.view_layer.update();sc.frame_set(1);floor_rows=[]
for f in floors:
 no=f['floor'];objects=[o for o in sc.objects if o.get('floor')==no];meshes0=[o for o in objects if o.type=='MESH' and o.get('part_kind')!='water'];count=0
 for o in meshes0:o.data.calc_loop_triangles();count+=len(o.data.loop_triangles)
 assert count<=100000,(BID,no,count)
 bpy.ops.object.select_all(action='DESELECT')
 for o in objects:
  if o.get('part_kind')!='water':o.select_set(True)
 path=folder/f'{BID}-F{no:02d}-functional-2k.glb';bpy.ops.export_scene.gltf(filepath=str(path),export_format='GLB',use_selection=True,export_extras=True,export_animations=False)
 label=f'Mall_M1_F{no:02d}' if BID=='M1' else f'Campus_{BID}_F{no:02d}' if BID in __import__('street_city_campus_contract').IDS else f'City_{BID}_F{no:02d}'
 floor_rows.append(dict(id=label,building=BID,floor=no,source=str(path),source_sha256=sha(path),triangles=count))
bpy.ops.object.select_all(action='SELECT');path=folder/f'{BID}-complete-functional-2k.glb';bpy.ops.export_scene.gltf(filepath=str(path),export_format='GLB',export_extras=True,export_animations=True);bpy.ops.wm.save_as_mainfile(filepath=str(path.with_suffix('.blend')))
import shutil
snapshot=folder/'build-generator.py';shutil.copy2(Path(__file__),snapshot)
r=dict(status='built',building=BID,signature=signature,generator_snapshot=str(snapshot),generator_sha256=sha(snapshot),source=str(source),source_sha256=sha(source),model=str(path),model_sha256=sha(path),floors=floor_rows,certified_panel=panel,classrooms=classrooms,administration=admin,opaque_bays=opaque,moved_seats=moves,removed_seats=removed,additions=records)
(folder/'receipt.json').write_text(json.dumps(r,ensure_ascii=False,indent=2),'utf8');(OUT/'buildings'/BID/'latest.json').write_text(json.dumps(dict(receipt=str(folder/'receipt.json')),indent=2),'utf8');print('FUNCTIONAL BUILT',BID,len(classrooms),len(admin),len(opaque),flush=True)
