return [====[
DESTBATCH=int(PAYLOAD.get('destination_batch',2));DESTCOUNT=40 if DESTBATCH==3 else 30
DESTOUT=BASE/('city-destinations-v'+str(DESTBATCH));DESTPREFIX='/Game/UniversityCity/DestinationsV'+str(DESTBATCH);DESTTAG='CityDestinations-v'+str(DESTBATCH)

def destination_assets():
 return read(DESTOUT/'plan.json')['assets']

def destination_terrain_height(east,north):
 global DEST_HEIGHT
 if 'DEST_HEIGHT' not in globals():
  import zipfile,array
  with zipfile.ZipFile(BASE/'world-ground-v1/world-field.npz') as z:raw=z.read('height_map.npy')
  start=10 if raw[6]==1 else 12;length=int.from_bytes(raw[8:start],'little');assert "'<f4'" in raw[start:start+length].decode();DEST_HEIGHT=array.array('f');DEST_HEIGHT.frombytes(raw[start+length:]);assert len(DEST_HEIGHT)==1101*1101
 return DEST_HEIGHT[max(0,min(1100,round(1100-north)))*1101+max(0,min(1100,round(east)))]

def dest_import():
 global PREFIX,KEEP
 PREFIX=DESTPREFIX;KEEP=[]
 helper=(ROOT/'Tools/street_city_activity_native_payload.lua').read_text('utf8');exec(helper[helper.index('def import_one('):helper.index('def simple_water(')],globals())
 from street_city_remesh_gate import verify
 specs=destination_assets();jobs=read(DESTOUT/'jobs.json');assert len(specs)==DESTCOUNT and len(set(jobs['jobs'].values()))==DESTCOUNT;rows={}
 for q in specs:
  aid=q['id'];folder=DESTOUT/'components'/aid;proof=verify(folder/'remesh');report=read(folder/'remesh/remesh-report.json');normalized=read(folder/'normalize-status.json');assert report['triangle_cap']<=30000
  mesh=import_one(proof['path'],aid,0,True);settings=[];options=unreal.EditorScriptingMeshReductionOptions();options.set_editor_property('auto_compute_lod_screen_size',False)
  for percent,screen in [(1.,1.),(.35,.22),(.1,.06)]:
   s=unreal.EditorScriptingMeshReductionSettings();s.set_editor_property('percent_triangles',percent);s.set_editor_property('screen_size',screen);settings.append(s)
  options.set_editor_property('reduction_settings',settings);API.set_lods(mesh,options);body=mesh.get_editor_property('body_setup');body.set_editor_property('collision_trace_flag',unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE);mesh.set_editor_property('lod_for_collision',0)
  materials=[]
  for i in range(len(mesh.get_editor_property('static_materials'))):
   m=mesh.get_material(i);assert m;materials.append(m.get_path_name());parent=m
   while not isinstance(parent,unreal.Material):parent=parent.get_editor_property('parent')
   parent.set_editor_property('used_with_instanced_static_meshes',True)
   if aid in ['D001','D002','D003','D016'] or q.get('two_sided'):parent.set_editor_property('two_sided',True)
   save(parent);save(m)
  save(mesh);bb=mesh.get_bounding_box();counts=[mesh.get_num_triangles(i) for i in range(API.get_lod_count(mesh))];assert len(counts)>=3 and counts[0]<=report['triangle_cap'];assert abs((bb.max.z-bb.min.z)/100-q['height_m'])<.03,(aid,bb,q['height_m'])
  rows[aid]=dict(asset=mesh.get_path_name(),lod_triangles=counts,bounds_cm=dict(min=vec(bb.min),max=vec(bb.max)),source_sha256=proof['sha256'],remesh_report=proof['remesh_report'],original_download_sha256=normalized['source_sha256'],job_url='https://studio.tripo3d.ai/workspace/generate/'+jobs['jobs'][aid],materials=materials,height_m=q['height_m'],zone=q['zone'])
  write(DESTOUT/'import-progress.json',dict(status='running',assets=rows));unreal.log('DEST IMPORT '+aid+' '+str(counts))
 write(DESTOUT/'imports.json',dict(status='passed',assets=rows,new_unique_jobs=DESTCOUNT));write(OUT/'dest_import.json',dict(status='passed',assets=len(rows)))

def destination_position(q,mesh,actors,placed):
 if q.get('floor'):return destination_mall_position(q,mesh,actors,placed)
 if q.get('facility'):return destination_facility_position(q,mesh,actors,placed)
 world=unreal.EditorLevelLibrary.get_editor_world();buildings=read(OUT/'plan.json')['buildings'];floor_labels={q['id'] for q in FLOORS};cx,cy,z=q['search_center_m'];bb=mesh.get_bounding_box();radius=max(.65,math.hypot(bb.max.x-bb.min.x,bb.max.y-bb.min.y)/200);attempts=[]
 corridors=read(OUT/'plan.json')['corridors']
 def door_strip(p):
  for c in corridors:
   a,b=c['a'],c['b'];dx,dy=b[0]-a[0],b[1]-a[1];l=dx*dx+dy*dy;t=max(0,min(1,((p[0]-a[0])*dx+(p[1]-a[1])*dy)/max(l,1e-8)))
   if math.hypot(p[0]-a[0]-t*dx,p[1]-a[1]-t*dy)<radius+c['width_m']/2+.5:return True
  return False
 # Ring search remains local to its planned destination; it cannot spill into an unrelated district.
 offsets=[(0.,0.)]+([(r*math.cos(t*math.pi/6),r*math.sin(t*math.pi/6)) for r in [5,9,14,20,27,35] for t in range(12)] if q.get('search_radius_m',35)>0 else [])
 for dx,dy in offsets:
  p=[cx+dx,cy+dy,destination_terrain_height(cx+dx,cy+dy)];z=p[2];reason='outside intended destination elevation' if abs(z-q['search_center_m'][2])>4 else None
  for sx,sy in [(0,0),(radius,0),(-radius,0),(0,radius),(0,-radius)]:
   reason=reason or site_exclusion([p[0]+sx,p[1]+sy,z],.5)
   if reason:break
  if not reason:
   for b in buildings.values():
    x,y,bz=b['position_m'];w,d=b['footprint_m']
    if abs(p[0]-x)<w/2+radius+1 and abs(p[1]-y)<d/2+radius+1:reason='building clearance';break
  if not reason and door_strip(p):reason='entrance approach reserved'
  if not reason and any(math.hypot(p[0]-r['position_m'][0],p[1]-r['position_m'][1])<radius+r['radius_m']+2 for r in placed):reason='other new landmark and viewing space'
  heights=[]
  if not reason:
   for sx,sy in [(0,0),(radius*.7,0),(-radius*.7,0),(0,radius*.7),(0,-radius*.7)]:
    h=hit_data(unreal.SystemLibrary.line_trace_single(world,point([p[0]+sx,p[1]+sy,z+1.8]),point([p[0]+sx,p[1]+sy,z-2]),unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,[],unreal.DrawDebugTrace.NONE,True))
    if not h or not h[0]:reason='no ground';break
    a=h[9];label=a.get_actor_label() if a else ''
    if label in floor_labels or label.startswith(('CityArt_','Resort_','Street_','Activity_','Destination_')):reason='existing building or furnishing';break
    heights.append(h[4].z/100)
   if not reason and (max(heights)-min(heights)>.15 or abs(heights[0]-z)>1):reason='terrain support uneven'
  if not reason:
   centre=point([p[0],p[1],heights[0]+.8]);h=hit_data(unreal.SystemLibrary.box_trace_single(world,centre,centre,unreal.Vector(radius*85,radius*85,65),unreal.Rotator(pitch=0,yaw=0,roll=0),unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,[],unreal.DrawDebugTrace.NONE,True))
   if h and h[0]:reason='existing geometry occupies destination'
  if not reason:return [p[0],p[1],heights[0]+.01],radius,attempts
  attempts.append(dict(offset=[dx,dy],reason=reason))
 raise AssertionError(('No supported clear site in destination',q['id'],q['zone'],attempts))

def dest_install():
 if DESTBATCH==3:assert read(DESTOUT/'gap-probe.json')['status']=='passed','Native gap and circulation baseline required'
 models=read(DESTOUT/'imports.json')['assets'];actors={a.get_actor_label():a for a in SUB.get_all_level_actors()};mapfile=ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap';backup=DESTOUT/'backups'/('UniversityCity-before-'+sha(mapfile)[:16]+'.umap');backup.parent.mkdir(exist_ok=True)
 if not backup.exists():shutil.copy2(mapfile,backup)
 for a in actors.values():
  if DESTTAG in [str(t) for t in a.tags]:
   for c in a.get_components_by_class(unreal.PrimitiveComponent):c.set_collision_profile_name('NoCollision');c.set_visibility(False,True)
 placed=[]
 for q in destination_assets():
  aid=q['id'];r=models[aid];mesh=EAL.load_asset(r['asset']);p,radius,rejected=destination_position(q,mesh,actors,placed);label='Destination_'+aid;a=actors.get(label)
  if not a:bp=EAL.load_asset('/Game/UniversityCity/StreetScenery/BP_StreetCluster');a=SUB.spawn_actor_from_class(bp.generated_class(),unreal.Vector());a.set_actor_label(label)
  assert not a.tags or DESTTAG in [str(t) for t in a.tags];a.tags=[unreal.Name(DESTTAG)];a.set_folder_path('UniversityCity/DestinationsV'+str(DESTBATCH)+'/'+q['zone']);a.set_actor_hidden_in_game(False);c=a.get_component_by_class(unreal.HierarchicalInstancedStaticMeshComponent);c.set_mobility(unreal.ComponentMobility.STATIC);c.set_static_mesh(mesh);c.clear_instances();c.set_visibility(True,True);c.set_hidden_in_game(False,True);c.set_collision_profile_name('BlockAll');c.set_cull_distances(11000,18000);c.set_cast_shadow(True)
  for i in range(len(r['materials'])):c.set_material(i,mesh.get_material(i))
  paths=read(OUT/'plan.json')['corridors'];nearest=min(paths,key=lambda r:math.hypot(r['a'][0]-p[0],r['a'][1]-p[1]));yaw=math.degrees(math.atan2(nearest['a'][1]-p[1],nearest['a'][0]-p[0]));c.add_instance(unreal.Transform(location=point(p),rotation=unreal.Rotator(pitch=0,yaw=90-yaw,roll=0),scale=unreal.Vector(1,1,1)),True)
  placed.append(dict(id=aid,zone=q['zone'],name=q['name'],floor=q.get('floor'),facility=q.get('facility'),actor=label,asset=r['asset'],position_m=p,yaw=yaw,radius_m=radius,rejected_sites=rejected,instance_sha256=instance_hash(c),materials=r['materials'],lod_triangles=r['lod_triangles'],collision=str(c.get_collision_profile_name())))
 companions=destination_companions(placed,actors)
 for q in placed:
  label=q['actor']+'_Light';a=actors.get(label);p=list(q['position_m']);p[2]+=1.5
  if not a:a=SUB.spawn_actor_from_class(unreal.PointLight,point(p));a.set_actor_label(label)
  a.tags=[unreal.Name(DESTTAG)];a.set_folder_path('UniversityCity/DestinationsV2/'+q['zone']);a.set_actor_location(point(p),False,True);c=a.point_light_component;c.set_mobility(unreal.ComponentMobility.STATIONARY);c.set_editor_property('intensity_units',unreal.LightUnits.LUMENS);c.set_intensity(180 if q['id']!='D021' else 350);c.set_attenuation_radius(550);c.set_cast_shadows(False);c.set_light_color(unreal.LinearColor(1,.8,.6,1))
 assert len(placed)==DESTCOUNT;assert unreal.EditorLevelLibrary.save_current_level();mapsha=sha(mapfile)
 # City-art receipt follows the same saved map; all original component contracts remain unchanged.
 receipt=read(OUT/'install.json');receipt['map_sha256']=mapsha;receipt['additive_destinations']=dict(instances=70 if DESTBATCH==3 else DESTCOUNT,latest_batch=DESTCOUNT,receipt=str(DESTOUT/'install.json'));write(OUT/'install.json',receipt)
 write(DESTOUT/'install.json',dict(status='passed',instances=placed,companions=companions,landmark_lights=DESTCOUNT,unique_models=DESTCOUNT,unique_zones=len(set(q['zone'] for q in placed)),wall_edits=0,recaptured_images=0,map_sha256=mapsha));write(OUT/'dest_install.json',dict(status='passed',instances=DESTCOUNT,map_sha256=mapsha))

def destination_companions(landmarks,actors):
 source=read(BASE/'resort-native-v1/imports.json')['assets'];groups={};placed=list(landmarks);skipped=[]
 for main in landmarks:
  if main.get('floor'):continue
  zone=main['zone'];ids=['R009','R042','R057'] if zone.startswith('campus') else ['R045','R009','R049','R057'] if zone=='hotel-garden' else ['R049','R009','R057'] if zone in ['bay-garden','bay-camp','riverside'] else ['R045','R042','R009'] if zone.startswith('shopping') else ['R045','R009','R057']
  for i,aid in enumerate(ids):
   mesh=EAL.load_asset(source[aid]['asset']);bb=mesh.get_bounding_box();r=math.hypot(bb.max.x-bb.min.x,bb.max.y-bb.min.y)/200;angle=(i*2*math.pi/len(ids))+.45;distance=main['radius_m']+r+2.3;mp=main['position_m'];q=dict(id=main['id']+'_'+aid,zone=zone,search_radius_m=0,search_center_m=[mp[0]+distance*math.cos(angle),mp[1]+distance*math.sin(angle),mp[2]])
   try:p,radius,rejected=destination_position(q,mesh,actors,placed)
   except AssertionError:skipped.append(dict(id=q['id'],reason='No clear supported companion position; landmark and walking space preserved'));continue
   label='Destination_Companion_'+aid+('_v3' if DESTBATCH==3 else '')
   if aid not in groups:
    a=actors.get(label)
    if not a:bp=EAL.load_asset('/Game/UniversityCity/StreetScenery/BP_StreetCluster');a=SUB.spawn_actor_from_class(bp.generated_class(),unreal.Vector());a.set_actor_label(label)
    a.tags=[unreal.Name(DESTTAG)];a.set_folder_path('UniversityCity/DestinationsV2/SharedGardenFurniture');a.set_actor_hidden_in_game(False);c=a.get_component_by_class(unreal.HierarchicalInstancedStaticMeshComponent);c.set_mobility(unreal.ComponentMobility.STATIC);c.set_static_mesh(mesh);c.clear_instances();c.set_visibility(True,True);c.set_hidden_in_game(False,True);c.set_collision_profile_name('BlockAll');c.set_cull_distances(6500,10000)
    for j in range(len(mesh.get_editor_property('static_materials'))):c.set_material(j,mesh.get_material(j))
    groups[aid]=dict(actor=a,component=c,instances=[])
   yaw=math.degrees(angle)+180;g=groups[aid];g['component'].add_instance(unreal.Transform(location=point(p),rotation=unreal.Rotator(pitch=0,yaw=90-yaw,roll=0),scale=unreal.Vector(1,1,1)),True);record=dict(id=q['id'],landmark=main['id'],asset=aid,zone=zone,position_m=p,yaw=yaw,radius_m=radius);placed.append(record);g['instances'].append(record)
 return dict(groups=[dict(actor=g['actor'].get_actor_label(),asset=aid,instances=g['instances'],count=g['component'].get_instance_count(),sha256=instance_hash(g['component'])) for aid,g in groups.items()],skipped=skipped,total=sum(len(g['instances']) for g in groups.values()))

def dest_audit():
 receipt=read(DESTOUT/'install.json');models=read(DESTOUT/'imports.json')['assets'];actors={a.get_actor_label():a for a in SUB.get_all_level_actors()};issues=[];checks=0;shaders={}
 def check(ok,name,aid):
  nonlocal checks
  checks+=1
  if not ok:issues.append(dict(check=name,id=aid))
 from street_city_remesh_gate import verify
 for q in receipt['instances']:
  aid=q['id'];r=models[aid];verify(DESTOUT/'components'/aid/'remesh',r['source_sha256']);a=actors.get(q['actor']);check(a is not None,'Saved destination actor',aid)
  if not a:continue
  c=a.get_component_by_class(unreal.HierarchicalInstancedStaticMeshComponent);mesh=c.static_mesh;check(mesh.get_path_name()==q['asset'],'Correct model',aid);check(c.get_instance_count()==1 and instance_hash(c)==q['instance_sha256'],'Exact saved placement',aid);check(c.is_visible() and not a.get_editor_property('hidden'),'Visible',aid);check(str(c.get_collision_profile_name())==q['collision'],'Collision',aid);check(API.get_lod_count(mesh)>=3 and [mesh.get_num_triangles(i) for i in range(API.get_lod_count(mesh))]==r['lod_triangles'],'Three LODs',aid);check(c.get_editor_property('forced_lod_model')==0 and c.get_editor_property('instance_end_cull_distance')==18000,'Automatic LOD and distance culling',aid);check([c.get_material(i).get_path_name() for i in range(c.get_num_materials())]==q['materials'],'Original PBR preserved',aid);check(abs((mesh.get_bounding_box().max.z-mesh.get_bounding_box().min.z)/100-r['height_m'])<.03,'Metric height',aid)
  for i in range(c.get_num_materials()):
   parent=c.get_material(i)
   while parent and not isinstance(parent,unreal.Material):parent=parent.get_editor_property('parent')
   if parent:shaders[parent.get_path_name()]=parent
 for name,m in shaders.items():check(m.get_editor_property('used_with_instanced_static_meshes') and not MEL.recompile_material(m),'Instancing PBR shader compiles',name)
 for g in receipt['companions']['groups']:
  a=actors[g['actor']];c=a.get_component_by_class(unreal.HierarchicalInstancedStaticMeshComponent);check(c.get_instance_count()==g['count'] and instance_hash(c)==g['sha256'],'Saved scenic companion composition',g['asset']);check(API.get_lod_count(c.static_mesh)>=3 and c.is_visible(),'Companion LOD and visibility',g['asset'])
 for q in receipt['instances']:
  a=actors.get(q['actor']+'_Light');check(a is not None and a.point_light_component.get_editor_property('intensity')>0,'Landmark accent lighting',q['id'])
 city=read(OUT/'audit.json');check(city['status']=='passed','Original city preservation audit','all');check(len(set(q['job_url'] for q in models.values()))==DESTCOUNT,'Distinct new Tripo jobs','all');check(not city['new_obstructions'],'No additional blocked original entrance','all');mapsha=sha(ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap');check(receipt['map_sha256']==mapsha==city['map_sha256'],'Same saved map','all')
 result=dict(status='passed' if not issues else 'failed',checks=checks,issues=issues,models=DESTCOUNT,zones=receipt['unique_zones'],recaptured_images=0,map_sha256=mapsha);write(DESTOUT/'audit.json',result);write(OUT/'dest_audit.json',result);assert not issues,str(issues)

def dest_verify():
 audit();dest_audit();write(OUT/'dest_verify.json',dict(status='passed',scope='Independent reopen; original city and thirty new destination models; no map captures'))
]====]
