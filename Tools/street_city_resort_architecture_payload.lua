return [====[
ARCH='/Game/UniversityCity/ResortArchitectureV1'
TAG='ResortArchitecture-v1'
MAPFILE=ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap'
LOAD_SHA=sha(MAPFILE)
KEEP=[]
API=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
def jread(p):return json.loads(Path(p).read_text('utf-8-sig'))
def store(name,data):write(name,data)
def save_asset(a):assert EAL.save_loaded_asset(a)

def import_arch_mesh(path,name):
 digest=sha(path);folder=ARCH+'/Meshes/'+name+'/S_'+digest[:16];dest=folder+'/'+name
 if EAL.does_asset_exist(dest):return EAL.load_asset(dest)
 pipeline=unreal.InterchangeGenericAssetsPipeline();KEEP.append(pipeline)
 pipeline.set_editor_property('asset_name',name);pipeline.set_editor_property('use_source_name_for_asset',False)
 pipeline.get_editor_property('common_meshes_properties').set_editor_property('bake_meshes',True)
 pipeline.get_editor_property('material_pipeline').set_editor_property('import_materials',False)
 mp=pipeline.get_editor_property('mesh_pipeline');mp.set_editor_property('combine_static_meshes_behavior',unreal.InterchangeCombineStaticMeshesBehavior.ALL)
 mp.set_editor_property('import_skeletal_meshes',False);mp.set_editor_property('build_nanite',False);mp.set_editor_property('generate_lightmap_u_vs',False)
 params=unreal.ImportAssetParameters();params.set_editor_property('is_automated',True);params.set_editor_property('destination_name',name)
 params.set_editor_property('override_pipelines',[unreal.SoftObjectPath(pipeline.get_path_name())])
 objs=unreal.InterchangeManager.get_interchange_manager_scripted().import_asset(folder,unreal.InterchangeManager.create_source_data(str(path)),params)
 meshes=[x for x in objs or [] if isinstance(x,unreal.StaticMesh)];assert len(meshes)==1,(name,len(meshes))
 mesh=meshes[0];EAL.set_metadata_tag(mesh,'ResortArchitectureSourceSHA256',digest);save_asset(mesh);KEEP.clear();return mesh

def sign_face_file(b):
 # Independent, correctly UV mapped sign face. Original trilingual materials are reused.
 import struct
 q=b['sign_board'];cx,cy,cz=q['center_m'];w=q['width_m']*.94;h=q['height_m']*.84
 # Follow the public entrance facade; keep lettering upright in every direction.
 angle=math.radians(q.get('yaw_deg',0));ux,uy=math.cos(angle),math.sin(angle);nx,ny=math.sin(angle),-math.cos(angle)
 def vertex(x,z):
  px=cx+ux*x+nx*.21;py=cy+uy*x+ny*.21
  return (px,cz+z,-py)
 p=[vertex(-w/2,-h/2),vertex(w/2,-h/2),vertex(w/2,h/2),vertex(-w/2,h/2)]
 idx=[0,1,2,0,2,3];positions=[v for i in idx for v in p[i]];uvs=[v for i in idx for v in [(0,1),(1,1),(1,0),(0,0)][i]]
 normals=[v for i in idx for v in (nx,0,-ny)]
 binary=struct.pack('<'+'f'*(len(positions)+len(normals)+len(uvs)),*(positions+normals+uvs));n=len(positions)*4
 g=dict(asset=dict(version='2.0',generator='Codex sign face, names from existing localization'),scene=0,scenes=[dict(nodes=[0])],nodes=[dict(mesh=0)],meshes=[dict(name=b['id']+'_Name',primitives=[dict(attributes=dict(POSITION=0,NORMAL=1,TEXCOORD_0=2),material=0)])],materials=[dict(name='name_sign',doubleSided=True,pbrMetallicRoughness=dict(baseColorFactor=[1,1,1,1],metallicFactor=0,roughnessFactor=.65))],buffers=[dict(byteLength=len(binary))],bufferViews=[dict(buffer=0,byteOffset=0,byteLength=n),dict(buffer=0,byteOffset=n,byteLength=n),dict(buffer=0,byteOffset=n*2,byteLength=len(uvs)*4)],accessors=[dict(bufferView=0,componentType=5126,count=6,type='VEC3',min=[min(v[k] for v in p) for k in range(3)],max=[max(v[k] for v in p) for k in range(3)]),dict(bufferView=1,componentType=5126,count=6,type='VEC3'),dict(bufferView=2,componentType=5126,count=6,type='VEC2')])
 data=json.dumps(g,separators=(',',':')).encode();data+=b' '*((-len(data))%4);binary+=b'\0'*((-len(binary))%4)
 path=OUT/'sign-faces'/(b['id']+'.glb');path.parent.mkdir(exist_ok=True)
 path.write_bytes(struct.pack('<III',0x46546C67,2,12+8+len(data)+8+len(binary))+struct.pack('<II',len(data),0x4E4F534A)+data+struct.pack('<II',len(binary),0x004E4942)+binary);return path

def signs_map():
 result={};old=jread(OUT/'inventory.json')
 for a in old['actors']:
  for c in a['components']:
   for s in c['slots']:
    if s['text'] and s['text'].startswith('building.'):result[s['text'][9:]]=s['material']
 return result

def name_material(bid,existing):
 if bid in existing:return EAL.load_asset(existing[bid])
 # A few older source models used wall graphics instead of a building name slot.
 dest=ARCH+'/Signs/M_Name_'+bid
 if EAL.does_asset_exist(dest):return EAL.load_asset(dest)
 m=ATS.create_asset('M_Name_'+bid,ARCH+'/Signs',unreal.Material,unreal.MaterialFactoryNew());m.set_editor_property('two_sided',True)
 col=EAL.load_asset('/Game/UniversityCity/Localization/MPC_CityLanguage');assert col,'Language collection absent'
 nodes=[]
 for locale in ['zh-Hant','zh-Hans','en']:
  path=ROOT/'output/street-social-city-production/localization-v1/textures'/locale/('building.'+bid+'.png');assert path.exists(),str(path)
  task=unreal.AssetImportTask();task.filename=str(path);task.destination_path=ARCH+'/Signs';task.destination_name='T_'+bid+'_'+locale.replace('-','_');task.automated=True;task.save=True;ATS.import_asset_tasks([task]);t=EAL.load_asset(task.imported_object_paths[0]);assert t
  n=MEL.create_material_expression(m,unreal.MaterialExpressionTextureSampleParameter2D,-700,len(nodes)*240);n.set_editor_property('parameter_name','CityText_'+locale.replace('-','_'));n.set_editor_property('texture',t);nodes.append(n)
 control=MEL.create_material_expression(m,unreal.MaterialExpressionCollectionParameter,-1000,0);control.set_editor_property('collection',col);control.set_editor_property('parameter_name','LanguageIndex')
 def choose(threshold,low,high,x):
  n=MEL.create_material_expression(m,unreal.MaterialExpressionIf,x,0);n.set_editor_property('const_b',threshold);assert MEL.connect_material_expressions(control,'',n,'A')
  for pin,src in [('A > B',high),('A == B',high),('A < B',low)]:assert MEL.connect_material_expressions(src,'RGB' if src in nodes else '',n,pin)
  return n
 first=choose(.5,nodes[0],nodes[1],-350);last=choose(1.5,first,nodes[2],-100)
 assert MEL.connect_material_property(last,'',unreal.MaterialProperty.MP_BASE_COLOR)
 errs=MEL.recompile_material(m);assert not errs,errs;EAL.set_metadata_tag(m,'CityTextID','building.'+bid);save_asset(m);return m

def architecture_material(bid,role,accent=None):
 name='MI_'+bid+'_'+role;dest=ARCH+'/Materials/'+name
 existing=EAL.load_asset(dest) if EAL.does_asset_exist(dest) else None
 if existing and (role!='roof' or not accent or EAL.get_metadata_tag(existing,'GrokRoofAccent')==accent):return existing
 seed=int(hashlib.sha256(bid.encode()).hexdigest()[:6],16)%10000
 if role in ['roof','wood']:
  family=({'terracotta':'terracotta_tile','lagoon':'lagoon_ceramic','ivory':'pale_resort_stucco'}.get(accent,'terracotta_tile' if seed%5 else 'lagoon_ceramic')) if role=='roof' else ('teak_wood' if seed%2 else 'whitewashed_wood')
  available=jread(ROOT/'output/street-social-city-production/romance-pbr-v1/install.json')['slots'];sets=sorted({s['set'] for s in available if s['set'].startswith(family+'_')});sid=sets[seed%len(sets)]
  master=EAL.load_asset('/Game/UniversityCity/RomancePBR/Materials/M_RomancePBR');assert master
  mi=existing or ATS.create_asset(name,ARCH+'/Materials',unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew());MEL.set_material_instance_parent(mi,master)
  for param,kind in [('RomanceBase','basecolor'),('RomanceNormal','normal'),('RomanceORM','orm')]:
   t=EAL.load_asset('/Game/UniversityCity/RomancePBR/Textures/T_'+sid+'_'+kind);assert t,(bid,sid,kind);MEL.set_material_instance_texture_parameter_value(mi,param,t)
  MEL.set_material_instance_scalar_parameter_value(mi,'RomanceSeed',float(seed));MEL.set_material_instance_scalar_parameter_value(mi,'RomancePeriod',180. if role=='roof' else 150.)
  MEL.set_material_instance_scalar_parameter_value(mi,'RomanceStrength',.4)
  MEL.set_material_instance_vector_parameter_value(mi,'RomanceTintA',unreal.LinearColor(1,.97,.93,1));MEL.set_material_instance_vector_parameter_value(mi,'RomanceTintB',unreal.LinearColor(.94,.97,1,1))
  EAL.set_metadata_tag(mi,'RomanceSet',sid)
  if accent:EAL.set_metadata_tag(mi,'GrokRoofAccent',accent)
  save_asset(mi);return mi
 pbr_role={'sign_panel':'facade','metal':'trim'}.get(role,role)
 for fallback in [pbr_role,'trim','wall']:
  candidate='/Game/UniversityCity/RomancePBR/Materials/MI_'+bid+'_'+fallback
  mat=EAL.load_asset(candidate) if EAL.does_asset_exist(candidate) else None
  if mat:return mat
 raise RuntimeError('No architectural material for '+bid+' '+role)

def validate_geometry():
 import struct
 def overlaps_courtyard(tri,rect):
  cx,cy,cw,cd=rect;poly=[(x,y) for x,y,z in tri]
  for axis,value,greater in [(0,cx-cw/2+.015,True),(0,cx+cw/2-.015,False),(1,cy-cd/2+.015,True),(1,cy+cd/2-.015,False)]:
   result=[]
   for i,p in enumerate(poly):
    q=poly[(i+1)%len(poly)];ip=p[axis]>=value if greater else p[axis]<=value;iq=q[axis]>=value if greater else q[axis]<=value
    if ip:result.append(p)
    if ip!=iq:
     t=(value-p[axis])/(q[axis]-p[axis]);result.append(tuple(p[k]+t*(q[k]-p[k]) for k in (0,1)))
   poly=result
   if not poly:return False
  return abs(sum(poly[i][0]*poly[(i+1)%len(poly)][1]-poly[(i+1)%len(poly)][0]*poly[i][1] for i in range(len(poly))))>.00002
 inputs={b['id']:b for b in jread(OUT/'building-input.json')['buildings']};manifest=jread(OUT/'geometry-manifest.json');rows=[];issues=[];total=0
 assert len(manifest['buildings'])==122 and {b['id'] for b in manifest['buildings']}==set(inputs)
 for b in manifest['buildings']:
  bid=b['id'];spec=inputs[bid];path=OUT/b['glb'];raw=path.read_bytes();magic,version,length=struct.unpack_from('<III',raw);assert magic==0x46546c67 and version==2 and length==len(raw)
  size,typ=struct.unpack_from('<II',raw,12);g=json.loads(raw[20:20+size]);boff=20+size+8;binary=raw[boff:];pts=[];roof_pts=[];low_vertices=0;tris=0;courtyard_faces=0
  # The agreed contract bakes [x,z,-y] into every mesh, with identity nodes.
  for n in g.get('nodes',[]):
   assert n.get('translation',[0,0,0])==[0,0,0] and n.get('rotation',[0,0,0,1])==[0,0,0,1] and 'matrix' not in n,'Unbaked node transform: '+bid
  for mesh in g['meshes']:
   for p in mesh['primitives']:
    a=g['accessors'][p['attributes']['POSITION']];v=g['bufferViews'][a['bufferView']];offset=v.get('byteOffset',0)+a.get('byteOffset',0);stride=v.get('byteStride',12);assert a['componentType']==5126
    q=[struct.unpack_from('<fff',binary,offset+i*stride) for i in range(a['count'])];local=[(x,-z,y) for x,y,z in q];pts.extend(local)
    role=g['materials'][p['material']]['name'].lower();minimum=2.65 if role in ['sign_panel','metal'] else 3.
    low_vertices+=sum(1 for x,y,z in local if z<minimum-.01)
    if 'roof' in g['materials'][p['material']]['name'].lower():roof_pts.extend(local)
    if spec.get('courtyard'):
     if 'indices' in p:
      ia=g['accessors'][p['indices']];iv=g['bufferViews'][ia['bufferView']];ioff=iv.get('byteOffset',0)+ia.get('byteOffset',0);fmt={5121:'B',5123:'H',5125:'I'}[ia['componentType']];ix=struct.unpack_from('<'+fmt*ia['count'],binary,ioff)
     else:ix=range(len(local))
     for k in range(0,len(ix),3):
      if overlaps_courtyard([local[ix[k+j]] for j in range(3)],spec['courtyard']):courtyard_faces+=1
    tris+=(g['accessors'][p['indices']]['count'] if 'indices' in p else a['count'])//3
  assert all(math.isfinite(x) for p in pts for x in p);assert tris>0
  w=spec['width_m'];d=spec['depth_m'];h=spec['height_m'];bad=low_vertices;court=0
  for x,y,z in pts:
   if abs(x)<w/2-.015 and abs(y)<d/2-.015 and z<h-.015:bad+=1
   if spec.get('courtyard'):
    cx,cy,cw,cd=spec['courtyard']
    if abs(x-cx)<cw/2-.015 and abs(y-cy)<cd/2-.015:court+=1
  relief=max(p[2] for p in roof_pts)-min(p[2] for p in roof_pts) if roof_pts else 0
  overhang=max(max(abs(p[0])-w/2,abs(p[1])-d/2) for p in pts);limit=.65 if bid.startswith('S') and bid not in ['SP'] else 1.2
  if bad or court or courtyard_faces or relief<.4 or overhang>limit+.015:issues.append(dict(id=bid,occupied_vertices=bad,courtyard_vertices=court,courtyard_faces=courtyard_faces,roof_relief_m=relief,overhang_m=overhang,limit=limit))
  total+=tris;rows.append(dict(id=bid,triangles=tris,vertices=len(pts),roof_relief_m=round(relief,3),glb_sha256=sha(path)))
  board=b['sign_board']
  assert board['placement']=='ground_floor_horizontal' and board['top_m']<spec['floors'][0]['height_m'],bid+' sign is not in the ground storey'
  assert board['door_clearance_m']>=.05 and board['window_clearance_m']>=.015,bid+' sign does not clear openings'
 assert not issues,str(issues[:12]);assert total<=200000,('Triangle budget',total)
 write('geometry-review',dict(status='passed',buildings=122,original_floors=sum(len(b['floors']) for b in inputs.values()),triangles=total,issues=issues,rows=rows,scope='Independent binary GLB vertex and format checks: finite positions, baked coordinates, occupied box, minimum headroom, courtyard exclusion, roof relief and total triangle budget. Original native model comparison is separately required.'))

def import_arch():
 validate_geometry();manifest=jread(OUT/'geometry-manifest.json');existing=signs_map();rows=[]
 for b in manifest['buildings']:
  bid=b['id'];path=OUT/b['glb'];mesh=import_arch_mesh(path,'Exterior_'+bid)
  mats=[]
  for i,s in enumerate(mesh.get_editor_property('static_materials')):
   role=str(s.get_editor_property('imported_material_slot_name')).lower()
   role=next((r for r in ['sign_panel','facade','metal','wood','trim','roof'] if r in role),None);assert role,(bid,str(s))
   mat=architecture_material(bid,role,b.get('recipe',{}).get('accent'))
   assert mat,(bid,role);mesh.set_material(i,mat);mats.append(dict(slot=i,role=role,material=mat.get_path_name()))
  save_asset(mesh)
  face=import_arch_mesh(sign_face_file(b),'Name_'+bid);mat=name_material(bid,existing);face.set_material(0,mat);save_asset(face)
  rows.append(dict(id=bid,mesh=mesh.get_path_name(),sign_mesh=face.get_path_name(),materials=mats,sign_material=mat.get_path_name(),glb_sha256=sha(path)))
  if len(rows)%12==0:unreal.log('RESORT ARCH IMPORT '+str(len(rows))+'/122')
 assert len(rows)==122
 write('shader',dict(status='passed',buildings=rows,manifest_sha256=sha(OUT/'geometry-manifest.json')))

def prepare_arch_materials():
 existing=signs_map();rows=[]
 for b in jread(OUT/'building-input.json')['buildings']:
  bid=b['id'];roof=architecture_material(bid,'roof');wood=architecture_material(bid,'wood');sign=name_material(bid,existing)
  assert EAL.get_metadata_tag(sign,'CityTextID')=='building.'+bid
  rows.append(dict(id=bid,roof=roof.get_path_name(),wood=wood.get_path_name(),name=sign.get_path_name()))
 assert len(rows)==122;write('plan',dict(status='passed',materials=rows,note='Prepared native PBR and trilingual name bindings in Entry map; city map untouched.'))

def old_scene_check():
 # Only this pass's two labelled actors per building may differ from the baseline.
 before=jread(OUT/'inventory.json');actors={a.get_actor_label():a for a in SUB.get_all_level_actors()};cache={};issues=[];checks=0
 for row in before['actors']:
  a=actors.get(row['label']);checks+=1
  if not a:issues.append(row['label']+' missing');continue
  if actor_xform(a)!=row['xform']:issues.append(row['label']+' transform changed')
  actual=[component_row(a,c,cache) for c in a.get_components_by_class(unreal.MeshComponent)]
  if actual!=row['components']:issues.append(row['label']+' mesh/material/collision/instance state changed')
  checks+=len(actual)
 assert not issues,str(issues[:20]);return checks

def spawn_arch(label,mesh,xform):
 a=SUB.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(*xform['location']))
 a.set_actor_label(label);a.set_folder_path('UniversityCity/ResortArchitectureV1');a.set_editor_property('tags',[TAG])
 a.static_mesh_component.set_static_mesh(mesh);a.static_mesh_component.set_collision_profile_name('NoCollision');apply_arch_xform(a,xform);return a

def apply_arch_xform(a,xf):
 a.set_actor_location(unreal.Vector(*xf['location']),False,True)
 p,y,r=xf['rotation'];a.set_actor_rotation(unreal.Rotator(pitch=p,yaw=y,roll=r),False);a.set_actor_scale3d(unreal.Vector(*xf['scale']))
 assert actor_xform(a)==xf,(a.get_actor_label(),actor_xform(a),xf)

def repair_arch_transforms():
 receipt=jread(OUT/'install.json');actors={a.get_actor_label():a for a in SUB.get_all_level_actors()};diagnostic=[]
 assets=jread(OUT/'shader.json');assert assets['manifest_sha256']==sha(OUT/'geometry-manifest.json')
 by={r['id']:r for r in assets['buildings']}
 old_scene_check()
 backup=OUT/'backups'/('UniversityCity-before-signage-'+PAYLOAD['run_id']+'.umap');shutil.copy2(MAPFILE,backup);assert sha(backup)==LOAD_SHA
 for row in receipt['buildings']:
  fresh=by[row['id']]
  for label,path in [(row['geometry_actor'],row['geometry_mesh']),(row['sign_actor'],row['sign_mesh'])]:
   a=actors[label];assert TAG in [str(t) for t in a.tags]
   diagnostic.append(dict(actor=label,actual=actor_xform(a),expected=row['xform'],mesh=a.static_mesh_component.static_mesh.get_path_name(),expected_mesh=path))
   assert a.static_mesh_component.static_mesh.get_path_name()==path
   a.modify();a.static_mesh_component.modify()
   newpath=fresh['mesh'] if label==row['geometry_actor'] else fresh['sign_mesh'];newmesh=EAL.load_asset(newpath);assert newmesh
   a.static_mesh_component.set_static_mesh(newmesh);a.static_mesh_component.set_collision_profile_name('NoCollision');apply_arch_xform(a,row['xform'])
  row['geometry_mesh']=fresh['mesh'];row['sign_mesh']=fresh['sign_mesh'];row['sign_material']=fresh['sign_material']
 write('transform-diagnostic',dict(status='recorded',rows=diagnostic))
 old_scene_check();assert sha(MAPFILE)==LOAD_SHA;assert unreal.EditorLevelLibrary.save_current_level()
 receipt['map_sha256']=sha(MAPFILE);receipt['signage_revision']='Ground-floor horizontal fascia at public entrance facade; all former rooftop boards removed';receipt['revision_backup']=str(backup);write('install',receipt)

def install_arch():
 if (OUT/'install.json').is_file() and any(TAG in [str(t) for t in a.tags] for a in SUB.get_all_level_actors()):
  repair_arch_transforms();return
 assets=jread(OUT/'shader.json');assert assets['manifest_sha256']==sha(OUT/'geometry-manifest.json')
 baseline=jread(OUT/'baseline.json');assert sha(MAPFILE)==LOAD_SHA==baseline['map_sha256'],'Map changed since baseline; refresh inventory before modifying it'
 old_scene_check();actors={a.get_actor_label():a for a in SUB.get_all_level_actors()};first={r['building_id']:r for r in baseline['rows'] if r['floor']==1}
 assert len(first)==122
 assert not any(TAG in [str(t) for t in a.tags] for a in actors.values()),'Pass already installed; audit or restore first'
 backup=OUT/'backups'/('UniversityCity-before-'+PAYLOAD['run_id']+'.umap');backup.parent.mkdir(exist_ok=True);shutil.copy2(MAPFILE,backup);assert sha(backup)==LOAD_SHA
 added=[]
 for b in assets['buildings']:
  bid=b['id'];xf=first[bid]['xform'];mesh=EAL.load_asset(b['mesh']);face=EAL.load_asset(b['sign_mesh']);assert mesh and face
  a=spawn_arch('RomanceArch_'+bid,mesh,xf);s=spawn_arch('RomanceName_'+bid,face,xf)
  added.append(dict(id=bid,geometry_actor=a.get_actor_label(),sign_actor=s.get_actor_label(),geometry_mesh=mesh.get_path_name(),sign_mesh=face.get_path_name(),xform=xf,sign_material=b['sign_material']))
 old_scene_check();assert sha(MAPFILE)==LOAD_SHA,'Concurrent map write; no save performed'
 # Exactly one guarded save; never overwrite a changed map during a retry.
 assert unreal.EditorLevelLibrary.save_current_level(),'Save failed; leave map on disk intact and wait for lock release'
 assert sha(MAPFILE)!=LOAD_SHA
 write('restore-manifest',dict(status='passed',backup=str(backup),before_sha256=LOAD_SHA,tag=TAG,added=added,note='Restore removes only tagged exterior/name actors; never replace a map containing newer work with the backup.'))
 write('install',dict(status='saved_requires_reopen_audit',buildings=added,building_count=122,actor_count=len(added)*2,existing_scene_checks=old_scene_check(),map_sha256=sha(MAPFILE)))

def audit_arch():
 before=jread(OUT/'baseline.json');added=jread(OUT/'install.json')['buildings'];actors={a.get_actor_label():a for a in SUB.get_all_level_actors()}
 checks=old_scene_check();issues=[]
 for row in added:
  for label,path in [(row['geometry_actor'],row['geometry_mesh']),(row['sign_actor'],row['sign_mesh'])]:
   a=actors.get(label);checks+=1
   if not a:issues.append(label+' missing');continue
   c=a.static_mesh_component
   if c.static_mesh.get_path_name()!=path or actor_xform(a)!=row['xform']:issues.append(label+' geometry/transform mismatch')
   if str(c.get_collision_profile_name())!='NoCollision':issues.append(label+' blocks original routes')
   if not all(c.get_material(i) for i in range(c.get_num_materials())):issues.append(label+' missing material')
   if label==row['sign_actor'] and c.get_material(0).get_path_name()!=row['sign_material']:issues.append(label+' incorrect name material')
  checks+=4
 assert len([a for a in actors.values() if TAG in [str(t) for t in a.tags]])==244
 assert len([a for a in actors.values() if is_building_floor(a.get_actor_label())])==427
 assert not issues,issues
 write('audit',dict(status='passed',buildings=122,original_floors=427,geometry_actors=122,large_name_signs=122,checks=checks,issues=issues,map_sha256=sha(MAPFILE),unchanged='Every original actor transform, mesh, material slot, collision profile and instanced transform signature.'))

def restore_arch():
 old_scene_check();rows=[]
 for a in list(SUB.get_all_level_actors()):
  if TAG in [str(t) for t in a.tags]:rows.append(a.get_actor_label());SUB.destroy_actor(a)
 assert len(rows)==244;old_scene_check();assert sha(MAPFILE)==LOAD_SHA;assert unreal.EditorLevelLibrary.save_current_level()
 write('restore',dict(status='passed',removed=rows,map_sha256=sha(MAPFILE)))

def render_arch():
 # Native captures show actual generated buildings, not illustrative concept images.
 baseline=jread(OUT/'baseline.json');actors={a.get_actor_label():a for a in SUB.get_all_level_actors()};groups={}
 for r in baseline['rows']:groups.setdefault(r['building_id'],[]).append(r)
 views=[]
 for bid in ['S01','S03','S09','S18','S19','E1','LI','D1','H1','M1','TR','O01']:
  rows=groups[bid];boxes=[actors[r['label']].get_actor_bounds(False) for r in rows]
  lo=[min(getattr(o,k)-getattr(e,k) for o,e in boxes) for k in ('x','y','z')];hi=[max(getattr(o,k)+getattr(e,k) for o,e in boxes) for k in ('x','y','z')]
  # Derive the front from the imported sign's actual world bounds; glTF handedness
  # must not be guessed from yaw or from the symmetric footprint dimensions.
  span=max(hi[0]-lo[0],hi[1]-lo[1]);cx=(lo[0]+hi[0])*.5;cy=(lo[1]+hi[1])*.5;h=hi[2]-lo[2]
  sign=actors['RomanceName_'+bid];so,se=sign.get_actor_bounds(False)
  dx=so.x-cx;dy=so.y-cy;ln=math.hypot(dx,dy);assert ln>20,(bid,'name face has no exterior direction')
  dx/=ln;dy/=ln
  target=[cx,cy,lo[2]+h*.6+200];eye=[cx+dx*span*1.5-dy*span*.4,cy+dy*span*1.5+dx*span*.4,lo[2]+h*.85+span*.25]
  views.append(dict(name=bid,eye=eye,target=target,fov=55))
  if bid=='E1':views.append(dict(name='E1-courtyard',eye=[cx-dx*span*1.5-dy*span*.4,cy-dy*span*1.5+dx*span*.4,lo[2]+h*.85+span*.25],target=target,fov=55))
 world=unreal.EditorLevelLibrary.get_editor_world();folder=OUT/'renders';folder.mkdir(exist_ok=True);state=dict(i=0,phase='setup',wait=60,images=[],busy=False);refs=[]
 def tick_render_arch(dt):
  if state['busy']:return
  if state['wait']>0:state['wait']-=1;return
  state['busy']=True
  try:
   if state['phase']=='setup':
    q=views[state['i']];eye=unreal.Vector(*q['eye']);rot=unreal.MathLibrary.find_look_at_rotation(eye,unreal.Vector(*q['target']))
    a=SUB.spawn_actor_from_class(unreal.SceneCapture2D,eye,rot);c=a.capture_component2d;c.set_editor_property('capture_every_frame',False);c.set_editor_property('capture_on_movement',False)
    rt=unreal.RenderingLibrary.create_render_target2d(world,1600,1000,unreal.TextureRenderTargetFormat.RTF_RGBA8);refs.append(rt);c.set_editor_property('texture_target',rt);c.set_editor_property('capture_source',unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR);c.set_editor_property('fov_angle',q['fov']);c.set_editor_property('max_view_distance_override',160000.)
    state['current']=(q,a,c,rt);state['phase']='capture';state['wait']=80
   elif state['phase']=='capture':unreal.AutomationLibrary.finish_loading_before_screenshot();state['current'][2].capture_scene();state['phase']='export';state['wait']=10
   else:
    q,a,c,rt=state['current'];dest=folder/(q['name']+'.png');unreal.RenderingLibrary.export_render_target(world,rt,str(folder),dest.name);assert dest.exists() and dest.stat().st_size>5000
    state['images'].append(dict(name=q['name'],path=str(dest),sha256=sha(dest),camera=q));c.set_editor_property('texture_target',None);SUB.destroy_actor(a);unreal.RenderingLibrary.release_render_target2d(rt);refs.remove(rt);state['current']=None;state['i']+=1;state['phase']='setup';state['wait']=10
    if state['i']==len(views):
     unreal.unregister_slate_post_tick_callback(arch_render_handle);write('render',dict(status='passed',images=state['images'],map_sha256=sha(MAPFILE)));unreal.SystemLibrary.quit_editor()
  except BaseException:
   unreal.unregister_slate_post_tick_callback(arch_render_handle);write('render-error',dict(status='failed',error=traceback.format_exc()));unreal.log_error(traceback.format_exc());unreal.SystemLibrary.quit_editor()
  finally:state['busy']=False
 global arch_render_handle,ARCH_RENDER_REFS
 ARCH_RENDER_REFS=(state,refs);arch_render_handle=unreal.register_slate_post_tick_callback(tick_render_arch)

def snapshot():
 inventory()
 rows=[]
 for a in SUB.get_all_level_actors():
  if not is_building_floor(a.get_actor_label()):continue
  c=a.get_component_by_class(unreal.StaticMeshComponent);b=c.static_mesh.get_bounding_box();o,e=a.get_actor_bounds(False)
  rows.append(dict(label=a.get_actor_label(),building_id=identity(a.get_actor_label()),floor=floor_no(a.get_actor_label()),xform=actor_xform(a),local_min=num(b.min),local_max=num(b.max),world_center=num(o),world_extent=num(e)))
 write('baseline',dict(status='passed',rows=rows,map_sha256=LOAD_SHA))

def run_arch():
 try:
  if MODE=='inventory':snapshot()
  elif MODE=='plan':prepare_arch_materials()
  elif MODE=='shader':import_arch()
  elif MODE=='install':install_arch()
  elif MODE=='audit':audit_arch()
  elif MODE=='restore':restore_arch()
  elif MODE=='render':audit_arch();render_arch();return
  else:raise RuntimeError('Mode not yet implemented: '+MODE)
 except BaseException:
  write(MODE+'-error',dict(status='failed',error=traceback.format_exc()));unreal.log_error(traceback.format_exc())
 unreal.SystemLibrary.quit_editor()

assert unreal.EditorLevelLibrary.load_level('/Engine/Maps/Entry' if MODE in ['shader','plan'] else MAP)
frames=0
def tick_arch(dt):
 global frames
 frames+=1
 if frames<120:return
 unreal.unregister_slate_post_tick_callback(handle_arch);run_arch()
handle_arch=unreal.register_slate_post_tick_callback(tick_arch)
]====]
