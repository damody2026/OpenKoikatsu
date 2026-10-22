return [====[
import shutil,collections
from street_city_world_wall_rules import identity,floor_no,target,bounds as building_bounds
CITYPREFIX='/Game/UniversityCity/CityArtV1';CITYTAG='CityArt-v1';VERSION='20261021-city-art-01'
API=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
FLOORS=read(BASE/'scene-integration-v1/inventory.json')['rows']
FLOORS=[q for q in FLOORS if q['kind'] in ['shop','campus','mall','city_building']]

def save(a):assert EAL.save_loaded_asset(a)

def vec(v):return [v.x,v.y,v.z]

def probe():
 actors={a.get_actor_label():a for a in SUB.get_all_level_actors()};floors=[];mats={};lights=[]
 for q in FLOORS:
  a=actors[q['id']];c=a.static_mesh_component;mesh=c.static_mesh;slots=[];bid=identity(q['id']);no=floor_no(q['id'])
  for i,s in enumerate(mesh.get_editor_property('static_materials')):
   m=c.get_material(i);name=str(s.get_editor_property('imported_material_slot_name'));role=target(bid,name,no)
   if role or 'wall' in name.lower() or 'campus paint' in name.lower():
    slots.append(dict(index=i,name=name,role=role,material=m.get_path_name() if m else None,text=EAL.get_metadata_tag(m,'CityTextID') if m else None))
   if role and m and not EAL.get_metadata_tag(m,'CityTextID'):
    path=m.get_path_name()
    if path not in mats:
     mats[path]=dict(path=path,cls=m.get_class().get_name(),building=bid,role=role,examples=[],worldwall=EAL.get_metadata_tag(m,'CityWallIdentity'))
    mats[path]['examples'].append(dict(actor=q['id'],slot=i,name=name))
  p=a.get_actor_location();floors.append(dict(label=q['id'],bid=bid,floor=no,position_cm=vec(p),rotation=str(a.get_actor_rotation()),slots=slots,mesh=mesh.get_path_name(),lods=API.get_lod_count(mesh)))
 for a in actors.values():
  if isinstance(a,(unreal.PointLight,unreal.DirectionalLight,unreal.SkyLight)):
   c=a.get_component_by_class(unreal.LightComponent);lights.append(dict(label=a.get_actor_label(),cls=a.get_class().get_name(),position_cm=vec(a.get_actor_location()),intensity=c.get_editor_property('intensity') if c else None))
 write(OUT/'probe.json',dict(status='passed',floors=floors,wall_materials=list(mats.values()),lights=lights,building_bounds=building_bounds(),map_sha256=sha(ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap')))

def capture_city():
 plan=read(OUT/'plan.json');world=unreal.EditorLevelLibrary.get_editor_world();folder=OUT/('before' if MODE=='render_before' else 'renders');folder.mkdir(exist_ok=True);views=plan['views'];state={'i':0,'phase':'setup','wait':60,'images':[],'busy':False};refs=[]
 if MODE=='render_detail':
  folder=OUT/'details';folder.mkdir(exist_ok=True)
  views=[dict(name=n,eye=e,target=t) for n,e,t in [('campus-sports',[402,730,23],[300,770,28]),('shop-south',[235,322,13.65],[300,326,15]),('mall-front',[555,385,65],[655,460,25]),('mall-offices',[770,372,50],[835,430,24])]]
 unreal.SystemLibrary.execute_console_command(world,'r.DepthOfFieldQuality 0')
 lighting=[(a,a.light_component,a.get_actor_rotation(),a.light_component.get_editor_property('intensity')) for a in SUB.get_all_level_actors() if isinstance(a,unreal.DirectionalLight)]
 collection=EAL.load_asset('/Game/UniversityCity/Localization/MPC_CityLanguage')
 def environment(q):
  unreal.MaterialLibrary.set_scalar_parameter_value(world,collection,'LanguageIndex',q.get('language_index',0))
  for a,c,r,intensity in lighting:
   c.set_intensity(.015 if q.get('lighting')=='night' else intensity);a.set_actor_rotation(unreal.Rotator(pitch=-6,yaw=0,roll=0) if q.get('lighting')=='sunset' else r,False)
 def tick(dt):
  if state['busy']:return
  if state['wait']>0:state['wait']-=1;return
  state['busy']=True
  try:
   if state['phase']=='setup':
    q=views[state['i']];environment(q);p=point(q['eye']);rot=unreal.MathLibrary.find_look_at_rotation(p,point(q['target']));unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(p,rot);a=SUB.spawn_actor_from_class(unreal.SceneCapture2D,p,rot);a.set_actor_location(p,False,True);a.set_actor_rotation(rot,False);assert (a.get_actor_location()-p).length()<.01
    c=a.capture_component2d;c.set_editor_property('capture_every_frame',False);c.set_editor_property('capture_on_movement',False);rt=unreal.RenderingLibrary.create_render_target2d(world,1440,1000,unreal.TextureRenderTargetFormat.RTF_RGBA8);refs.append(rt);c.set_editor_property('texture_target',rt);c.set_editor_property('capture_source',unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR);c.set_editor_property('fov_angle',70);state['current']=(q,a,c,rt);state['phase']='capture';state['wait']=60
   elif state['phase']=='capture':unreal.AutomationLibrary.finish_loading_before_screenshot();state['current'][2].capture_scene();state['phase']='export';state['wait']=10
   else:
    q,a,c,rt=state['current'];p=folder/(q['name']+'.png');unreal.RenderingLibrary.export_render_target(world,rt,str(folder),p.name);assert p.exists() and p.stat().st_size>5000;state['images'].append(dict(name=q['name'],path=str(p),sha256=sha(p),camera=q));c.set_editor_property('texture_target',None);SUB.destroy_actor(a);unreal.RenderingLibrary.release_render_target2d(rt);refs.remove(rt);state['current']=None;state['i']+=1;state['phase']='setup';state['wait']=10
    if state['i']==len(views):
     environment({});unreal.unregister_slate_post_tick_callback(city_render_handle);write(OUT/(MODE+'.json'),dict(status='passed',images=state['images'],map_sha256=sha(ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap')));unreal.SystemLibrary.quit_editor()
  except BaseException:
   unreal.unregister_slate_post_tick_callback(city_render_handle);write(OUT/(MODE+'-error.json'),dict(status='failed',error=traceback.format_exc()));unreal.SystemLibrary.quit_editor()
  finally:state['busy']=False
 global city_render_handle,CITY_RENDER_REFS
 CITY_RENDER_REFS=(state,refs);city_render_handle=unreal.register_slate_post_tick_callback(tick)

def render_before():capture_city()
def render():capture_city()
def render_detail():capture_city()

def theme(bid):
 from street_city_shop_wall_styles import FAMILIES
 # Retail families retain their original identity, rather than receiving one city-wide paint.
 from street_city_shop_wall_styles import GROUPS
 if re.fullmatch(r'S\d+',bid):
  family=next(k for k,v in GROUPS.items() if int(bid[1:]) in v);p=FAMILIES[family];return [linear(v/255) for v in p[1]],[linear(v/255) for v in p[2]]
 palettes={
 'E1':([.53,.5,.42],[.3,.13,.08]),'EE':([.45,.52,.54],[.06,.2,.21]),'MA':([.57,.53,.45],[.12,.25,.19]),
 'AD':([.65,.6,.5],[.26,.18,.09]),'LI':([.49,.41,.3],[.12,.21,.15]),'SP':([.56,.6,.57],[.04,.29,.3]),
 'SC':([.63,.52,.43],[.35,.14,.09]),'M1':([.64,.59,.49],[.28,.19,.09]),'H1':([.62,.54,.42],[.1,.24,.2]),
 'R1':([.58,.55,.49],[.25,.31,.26]),'R2':([.6,.55,.5],[.35,.21,.16]),'CV':([.62,.59,.49],[.13,.29,.21]),
 'TR':([.55,.6,.6],[.04,.26,.3]),'O01':([.46,.51,.53],[.12,.2,.25]),'O02':([.54,.52,.46],[.28,.2,.12]),'O03':([.5,.54,.51],[.12,.25,.2])}
 if bid.startswith('D'):return [.62,.58,.5],[[.31,.17,.1],[.23,.3,.2],[.29,.22,.17],[.16,.26,.27]][int(bid[1:])%4]
 return palettes.get(bid,([.58,.55,.48],[.19,.26,.22]))

def grade_wall(old,bid):
 if EAL.get_metadata_tag(old,'CityArtOriginal'):old=EAL.load_asset(EAL.get_metadata_tag(old,'CityArtOriginal'))
 policy=read(ROOT/'Docs/StreetSocialCity/production/city-wall-policy.json')
 assert policy['default']=='preserve_original_material'
 overrides=[q for q in policy['approved_material_overrides'] if q['building']==bid and q['original']==old.get_path_name()]
 assert len(overrides)<=1
 if overrides:
  q=overrides[0];assert q.get('preserves_existing_identity') and (ROOT/q['visual_review']).is_file()
  replacement=EAL.load_asset(q['material']);assert replacement and not EAL.get_metadata_tag(old,'CityTextID');return replacement
 return old

def transform_data(a):
 t=a.get_actor_transform();r=t.rotation.rotator();return dict(location=vec(t.translation),scale=vec(t.scale3d),rotation=[r.pitch,r.yaw,r.roll])

def instance_hash(c):
 data=[]
 for i in range(c.get_instance_count()):
  t=c.get_instance_transform(i,True);r=t.rotation;data.append([round(x,5) for x in vec(t.translation)+[r.x,r.y,r.z,r.w]+vec(t.scale3d)])
 return hashlib.sha256(json.dumps(data).encode()).hexdigest()

def baseline_snapshot():
 rows=[]
 for a in SUB.get_all_level_actors():
  if CITYTAG in [str(t) for t in a.tags]:continue
  for c in a.get_components_by_class(unreal.StaticMeshComponent):
   mesh=c.static_mesh
   if not mesh:continue
   q=dict(actor=a.get_actor_label(),component=c.get_name(),mesh=mesh.get_path_name(),lod_triangles=[mesh.get_num_triangles(i) for i in range(API.get_lod_count(mesh))],transform=transform_data(a),collision=str(c.get_collision_profile_name()),visible=c.is_visible(),hidden=a.get_editor_property('hidden'),materials=[c.get_material(i).get_path_name() if c.get_material(i) else None for i in range(c.get_num_materials())])
   if isinstance(c,unreal.InstancedStaticMeshComponent):q['instance_count']=c.get_instance_count();q['instances_sha256']=instance_hash(c)
   rows.append(q)
 return rows

def sweep_strips():
 world=unreal.EditorLevelLibrary.get_editor_world();rows=[]
 for q in read(OUT/'plan.json')['corridors']:
  hits=[]
  for side in [-1,0,1]:
   a=point(q['a']);b=point(q['b']);dx,dy=b.x-a.x,b.y-a.y;l=max(1,math.hypot(dx,dy));offset=side*(q['width_m']/2-.45)*100;v=unreal.Vector(-dy/l*offset,dx/l*offset,100);a+=v;b+=v
   hit=unreal.SystemLibrary.capsule_trace_single(world,a,b,40,90,unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,[],unreal.DrawDebugTrace.NONE,True);broken=hit.to_tuple() if hit else None;who=broken[9] if broken else None;hits.append(dict(side=side,blocked=bool(broken and broken[0]),actor=who.get_actor_label() if who else None))
  rows.append(dict(id=q['id'],building=q['building'],clear=not any(h['blocked'] for h in hits),samples=hits))
 return rows

def tinted_surface(surface,bid):
 main,accent=theme(bid);palette={0:[.42,.25,.13],1:[.26,.28,.25],2:[.73,.66,.53],3:accent,4:[.16,.3,.19],5:[.42,.24,.21],6:[.74,.64,.44],7:[.58,.55,.47]}
 source=EAL.load_asset('/Game/UniversityCity/ResortV1/Materials/M_Surface_'+str(surface));assert isinstance(source,unreal.Material)
 parentpath=CITYPREFIX+'/Dressing/M_Dressing_v2_'+str(surface)
 if EAL.does_asset_exist(parentpath):parent=EAL.load_asset(parentpath)
 else:
  parent=EAL.duplicate_asset(source.get_path_name(),parentpath);base=MEL.get_material_property_input_node(parent,unreal.MaterialProperty.MP_BASE_COLOR);pin=MEL.get_material_property_input_node_output_name(parent,unreal.MaterialProperty.MP_BASE_COLOR);assert base
  tint=MEL.create_material_expression(parent,unreal.MaterialExpressionVectorParameter);tint.set_editor_property('parameter_name','CityArtTint');tint.set_editor_property('default_value',color([1,1,1]));n=MEL.create_material_expression(parent,unreal.MaterialExpressionCustom);n.set_editor_property('inputs',[custom_input('Base'),custom_input('Tint')]);n.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT3);n.set_editor_property('code','float l=dot(Base,float3(.2126,.7152,.0722));return lerp(Base,Tint*(.65+.35*saturate(l)),.8);');assert MEL.connect_material_expressions(base,pin,n,'Base');assert MEL.connect_material_expressions(tint,'RGB',n,'Tint');assert MEL.connect_material_property(n,'',unreal.MaterialProperty.MP_BASE_COLOR);parent.set_editor_property('used_with_instanced_static_meshes',True);assert not MEL.recompile_material(parent);save(parent)
 name='MI_v2_'+str(surface)+'_'+hashlib.sha256(json.dumps(palette[surface]).encode()).hexdigest()[:12];path=CITYPREFIX+'/Dressing/'+name
 if EAL.does_asset_exist(path):return EAL.load_asset(path)
 m=ATS.create_asset(name,CITYPREFIX+'/Dressing',unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew());MEL.set_material_instance_parent(m,parent);MEL.set_material_instance_vector_parameter_value(m,'CityArtTint',color(palette[surface]));got=MEL.get_material_instance_vector_parameter_value(m,'CityArtTint');assert max(abs(x-y) for x,y in zip([got.r,got.g,got.b],palette[surface]))<1e-5,(name,got,palette[surface]);save(m);return m

def hit_data(hit):
 if isinstance(hit,tuple):hit=hit[-1]
 return hit.to_tuple() if hit else None

def site_exclusion(p,radius=.35):
 global SITE_WATER,SITE_ROADS
 if 'SITE_WATER' not in globals():
  import zipfile
  with zipfile.ZipFile(BASE/'world-ground-v1/world-field.npz') as archive:
   raw=archive.read('water_map.npy');assert raw[:6]==b'\x93NUMPY';start=10 if raw[6]==1 else 12;length=int.from_bytes(raw[8:start],'little');header=raw[start:start+length].decode();assert 'u1' in header;SITE_WATER=raw[start+length:];assert len(SITE_WATER)==1101*1101
  SITE_ROADS=[q for q in read(BASE/'world-ground-v1/ground-plan.json')['roads'] if q['kind']=='asphalt']
 ix=max(0,min(1100,round(p[0])));iy=max(0,min(1100,round(1100-p[1])))
 if SITE_WATER[iy*1101+ix]:return 'water surface is not a furnishing site'
 for road in SITE_ROADS:
  points=road['points_map_m']
  for a,b in zip(points,points[1:]):
   ax,ay=a[0],1100-a[1];bx,by=b[0],1100-b[1];dx,dy=bx-ax,by-ay;l=max(1e-6,dx*dx+dy*dy);t=max(0,min(1,((p[0]-ax)*dx+(p[1]-ay)*dy)/l));distance=math.hypot(p[0]-ax-t*dx,p[1]-ay-t*dy)
   if distance<road['width_m']/2+radius:return 'carriageway and clearance reserved'
 return None

def dress(actors):
 plan=read(OUT/'plan.json');reused=read(BASE/'resort-native-v1/imports.json')['assets'];activity=read(BASE/'activity-scenes-v1/import_assets.json')['assets'];models=dict(reused);models['rose-bouquet']=activity['rose-bouquet'];models['entry-strip']=activity['platform'];models['canopy']=activity['platform'];world=unreal.EditorLevelLibrary.get_editor_world();groups={};accepted=[];skipped=[]
 floor_labels={q['id'] for q in FLOORS}
 # Existing owned instances must not reject their own replacement on a repeat pass.
 for a in actors.values():
  if CITYTAG in [str(t) for t in a.tags]:
   for c in a.get_components_by_class(unreal.PrimitiveComponent):c.set_collision_profile_name('NoCollision')
 for q in plan['decor']:
  aid=q['asset'];mesh=EAL.load_asset(models[aid]['asset']);assert API.get_lod_count(mesh)>=3
  q=dict(q);p=q['position_m'];reason=None
  if q.get('mount')=='canopy':
   for bid,b in plan['buildings'].items():
    if bid==q['building']:continue
    x,y,z=b['position_m'];w,d=b['footprint_m']
    if abs(p[0]-x)<w/2+1.0 and abs(p[1]-y)<d/2+1.0:reason='canopy would overlap the neighbouring building';break
  if q.get('mount') not in ['lintel','canopy']:
   reason=site_exclusion(p)
   for bid,b in plan['buildings'].items():
    x,y,z=b['position_m'];w,d=b['footprint_m']
    if abs(p[0]-x)<w/2+.1 and abs(p[1]-y)<d/2+.1:reason='inside another building footprint';break
   hit=hit_data(unreal.SystemLibrary.line_trace_single(world,point([p[0],p[1],p[2]+1.6]),point([p[0],p[1],p[2]-2]),unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,[],unreal.DrawDebugTrace.NONE,True))
   if not hit or not hit[0]:reason='no ground support'
   elif abs(hit[4].z/100-p[2])>1:reason='surface differs from intended terrain level'
   else:
    who=hit[9];label=who.get_actor_label() if who else ''
    if label in floor_labels:reason='building floor is not exterior ground'
    elif label.startswith(('CityArt_','Resort_','Street_','Activity_')):reason='existing furnishing occupies this position'
    else:q['position_m']=[p[0],p[1],hit[4].z/100+.008]
   if not reason:
    bb=mesh.get_bounding_box();size=[(bb.max.x-bb.min.x)/100*q['scale'][0],(bb.max.y-bb.min.y)/100*q['scale'][1]];small=min(.6,max(.2,min(size)/2));centre=point([p[0],p[1],q['position_m'][2]+.7]);h=hit_data(unreal.SystemLibrary.box_trace_single(world,centre,centre,unreal.Vector(small*100,small*100,45),unreal.Rotator(pitch=0,yaw=0,roll=0),unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,[],unreal.DrawDebugTrace.NONE,True))
    if h and h[0]:reason='reserved space collides with existing geometry'
  if reason:skipped.append(dict(request=q,reason=reason));continue
  # Share identical assets and palettes across buildings; do not create one draw group per door.
  style='shared' if aid=='entry-strip' else hashlib.sha256(json.dumps(theme(q['building'])).encode()).hexdigest()[:12]
  key=aid+'_'+style;label='CityArt_'+key.replace('-','_')
  if key not in groups:
   a=actors.get(label)
   if not a:bp=EAL.load_asset('/Game/UniversityCity/StreetScenery/BP_StreetCluster');a=SUB.spawn_actor_from_class(bp.generated_class(),unreal.Vector());a.set_actor_label(label)
   assert not a.tags or CITYTAG in [str(t) for t in a.tags];a.tags=[unreal.Name(CITYTAG)];a.set_folder_path('UniversityCity/CityArtV1/'+q['building']);a.set_actor_hidden_in_game(False);a.set_is_temporarily_hidden_in_editor(False);c=a.get_component_by_class(unreal.HierarchicalInstancedStaticMeshComponent);c.set_mobility(unreal.ComponentMobility.STATIC);c.set_static_mesh(mesh);c.clear_instances();c.set_collision_profile_name('NoCollision' if aid in ['entry-strip','canopy'] else 'BlockAll');c.set_visibility(True,True);c.set_hidden_in_game(False,True);c.set_cull_distances(5500,9000);c.set_cast_shadow(aid!='entry-strip')
   for i,slot in enumerate(mesh.get_editor_property('static_materials')):
    material=mesh.get_material(i);match=re.search(r'surface[-_](\d+)',str(slot.get_editor_property('material_slot_name')))
    if aid=='entry-strip':material=EAL.load_asset('/Game/UniversityCity/ActivityScenesV1/ArtV2/M_warm_lamp')
    elif aid=='canopy':material=tinted_surface(3,q['building'])
    elif match:material=tinted_surface(int(match.group(1)),q['building'])
    assert material;c.set_material(i,material)
   groups[key]=dict(actor=a,component=c,requests=[])
  g=groups[key];c=g['component'];rot=unreal.Rotator(pitch=0,yaw=90-q['yaw'],roll=0);c.add_instance(unreal.Transform(location=point(q['position_m']),rotation=rot,scale=unreal.Vector(*q['scale'])),True);g['requests'].append(q);accepted.append(q)
 for a in SUB.get_all_level_actors():
  if CITYTAG in [str(t) for t in a.tags] and a.get_actor_label().startswith('CityArt_') and a.get_actor_label() not in [g['actor'].get_actor_label() for g in groups.values()]:
   a.set_actor_hidden_in_game(True)
   for c in a.get_components_by_class(unreal.PrimitiveComponent):c.set_visibility(False,True);c.set_collision_profile_name('NoCollision')
 rows=[]
 for g in groups.values():
  c=g['component'];rows.append(dict(label=g['actor'].get_actor_label(),mesh=c.static_mesh.get_path_name(),count=c.get_instance_count(),requests=g['requests'],instances_sha256=instance_hash(c),materials=[c.get_material(i).get_path_name() for i in range(c.get_num_materials())],collision=str(c.get_collision_profile_name())))
 return dict(groups=rows,accepted=accepted,skipped=skipped)

def install():
 plan=read(OUT/'plan.json');mapfile=ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap';backup=OUT/'backups'/('UniversityCity-before-'+sha(mapfile)[:16]+'.umap');backup.parent.mkdir(exist_ok=True)
 if not backup.exists():shutil.copy2(mapfile,backup)
 actors={a.get_actor_label():a for a in SUB.get_all_level_actors()}
 if not (OUT/'baseline.json').exists():write(OUT/'baseline.json',dict(status='recorded',components=baseline_snapshot(),corridors=sweep_strips(),map_sha256=sha(mapfile),backup=str(backup)))
 rows=[];covered=set()
 for floor in read(OUT/'probe.json')['floors']:
  bid=floor['bid'];a=actors[floor['label']];c=a.static_mesh_component
  for slot in floor['slots']:
   if not slot['role'] or slot.get('text'):continue
   i=slot['index'];m=c.get_material(i);assert m and not EAL.get_metadata_tag(m,'CityTextID');new=grade_wall(m,bid);c.set_material(i,new);rows.append(dict(actor=floor['label'],slot=i,material=new.get_path_name(),original=EAL.get_metadata_tag(m,'CityArtOriginal') or m.get_path_name(),bid=bid));covered.add(bid)
  write(OUT/'install-progress.json',dict(status='running',last=floor['label'],buildings=len(covered),wall_slots=len(rows)))
 assert len(covered)==122,len(covered)
 dressing=dress(actors)
 lights=[]
 for bid,b in plan['buildings'].items():
  q=next((q for q in plan['decor'] if q['building']==bid and q.get('mount')=='lintel'),None)
  if not q:continue
  label='CityArtLight_'+bid;a=actors.get(label)
  if not a:a=SUB.spawn_actor_from_class(unreal.PointLight,point(q['position_m']));a.set_actor_label(label)
  a.tags=[unreal.Name(CITYTAG)];a.set_folder_path('UniversityCity/CityArtV1/EntranceLights');a.set_actor_location(point(q['position_m']),False,True);c=a.point_light_component;c.set_mobility(unreal.ComponentMobility.STATIONARY);c.set_editor_property('intensity_units',unreal.LightUnits.LUMENS);c.set_intensity(260);c.set_attenuation_radius(420);c.set_cast_shadows(False);c.set_light_color(unreal.LinearColor(1,.76,.52,1));lights.append(label)
 for label,intensity in [('CitySun',5.0),('CityStreetFill',1.8)]:actors[label].light_component.set_intensity(intensity)
 assert unreal.EditorLevelLibrary.save_current_level();write(OUT/'install.json',dict(status='passed',version=VERSION,buildings=sorted(covered),wall_slots=rows,dressing=dressing,entrance_lights=lights,map_sha256=sha(mapfile),plan_sha256=sha(OUT/'plan.json'),wall_policy='preserve_original_material',wall_policy_sha256=sha(ROOT/'Docs/StreetSocialCity/production/city-wall-policy.json')))

def restore_walls():
 receipt=read(OUT/'install.json');actors={a.get_actor_label():a for a in SUB.get_all_level_actors()};baseline={q['actor']:q['materials'] for q in read(OUT/'baseline.json')['components']};mapfile=ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap'
 backup=OUT/'backups'/('UniversityCity-before-identity-restore-'+sha(mapfile)[:16]+'.umap');backup.parent.mkdir(exist_ok=True)
 if not backup.exists():shutil.copy2(mapfile,backup)
 old_receipt=OUT/'backups'/('install-before-identity-restore-'+sha(OUT/'install.json')[:16]+'.json')
 if not old_receipt.exists():shutil.copy2(OUT/'install.json',old_receipt)
 rows=[]
 for q in receipt['wall_slots']:
  c=actors[q['actor']].static_mesh_component;current=c.get_material(q['slot']);assert current and current.get_path_name()==q['material'],('Unexpected intervening material edit',q['actor'],q['slot'])
  original=baseline[q['actor']][q['slot']];assert original==q['original'];m=EAL.load_asset(original);assert m and not EAL.get_metadata_tag(m,'CityTextID');c.set_material(q['slot'],m)
  assert c.get_material(q['slot']).get_path_name()==original;rows.append(dict(actor=q['actor'],slot=q['slot'],bid=q['bid'],original=original,material=original,rejected_material=q['material']))
 receipt['wall_slots']=rows;receipt['wall_policy']='preserve_original_material';receipt['wall_policy_sha256']=sha(ROOT/'Docs/StreetSocialCity/production/city-wall-policy.json');receipt['localization_receipts']=refresh_language_receipts(actors,receipt)
 assert unreal.EditorLevelLibrary.save_current_level();receipt['map_sha256']=sha(mapfile);write(OUT/'install.json',receipt)
 write(OUT/'restore_walls.json',dict(status='passed',restored_slots=len(rows),buildings=len(set(q['bid'] for q in rows)),source='immutable pre-city-art native baseline',retained=['dressing','entrance lights','nature materials','water','localized text','mesh geometry and LOD'],map_sha256=receipt['map_sha256']))

def audit():
 receipt=read(OUT/'install.json');baseline=read(OUT/'baseline.json');actors={a.get_actor_label():a for a in SUB.get_all_level_actors()};allowed={(q['actor'],q['slot']):q['material'] for q in receipt['wall_slots']+receipt.get('special_materials',[])};checks=0;issues=[];shader=set()
 def check(ok,name,details=None):
  nonlocal checks
  checks+=1
  if not ok:issues.append(dict(check=name,details=details))
 for q in baseline['components']:
  a=actors.get(q['actor']);check(a is not None,'Original actor retained',q['actor'])
  if not a:continue
  c=next((c for c in a.get_components_by_class(unreal.StaticMeshComponent) if c.get_name()==q['component']),None);check(c is not None,'Original component retained',q['actor'])
  if not c:continue
  check(c.static_mesh.get_path_name()==q['mesh'],'Original mesh unchanged',q['actor']);check(transform_data(a)==q['transform'],'Dimensions and world transform unchanged',q['actor']);check([c.static_mesh.get_num_triangles(i) for i in range(API.get_lod_count(c.static_mesh))]==q['lod_triangles'],'Original LODs unchanged',q['actor']);check(str(c.get_collision_profile_name())==q['collision'],'Original collision unchanged',q['actor']);check(c.is_visible()==q['visible'] and a.get_editor_property('hidden')==q['hidden'],'Original visibility unchanged',q['actor'])
  for i,old in enumerate(q['materials']):
   actual=c.get_material(i);actual=actual.get_path_name() if actual else None;want=allowed.get((q['actor'],i),old);check(actual==want,'Wall grade or untouched source material',q['actor']+'#'+str(i))
   if (q['actor'],i) in allowed and '/CityArtV1/' in want:shader.add(want)
  if 'instance_count' in q:check(c.get_instance_count()==q['instance_count'] and instance_hash(c)==q['instances_sha256'],'Preserve vehicles, tree count, size and angle variation',q['actor'])
 for path in shader:
  m=EAL.load_asset(path);check(EAL.get_metadata_tag(m,'CityArtVersion')==VERSION and not MEL.recompile_material(m),'Current wall shader compiles',path)
 for g in receipt['dressing']['groups']:
  a=actors.get(g['label']);check(a is not None,'Dressing cluster retained',g['label'])
  if not a:continue
  c=a.get_component_by_class(unreal.HierarchicalInstancedStaticMeshComponent);check(c.get_instance_count()==g['count'] and instance_hash(c)==g['instances_sha256'],'Saved dressing placement',g['label']);check(API.get_lod_count(c.static_mesh)>=3,'Dressing has three LODs',g['label']);check(c.is_visible() and not a.get_editor_property('hidden'),'Dressing visible in game',g['label']);check([c.get_material(i).get_path_name() if c.get_material(i) else None for i in range(c.get_num_materials())]==g['materials'],'Dressing PBR materials persist',g['label']);check(str(c.get_collision_profile_name())==g['collision'],'Dressing collision persists',g['label'])
  check(c.get_editor_property('instance_start_cull_distance')==5500 and c.get_editor_property('instance_end_cull_distance')==9000 and c.get_editor_property('forced_lod_model')==0,'Automatic LOD and distance culling',g['label'])
 before={q['id']:q for q in baseline['corridors']};after=sweep_strips();regressions=[q for q in after if before[q['id']]['clear'] and not q['clear']];check(not regressions,'No new entrance obstruction',regressions)
 collection=EAL.load_asset('/Game/UniversityCity/Localization/MPC_CityLanguage');world=unreal.EditorLevelLibrary.get_editor_world()
 for value in range(3):unreal.MaterialLibrary.set_scalar_parameter_value(world,collection,'LanguageIndex',value);check(unreal.MaterialLibrary.get_scalar_parameter_value(world,collection,'LanguageIndex')==value,'Three-language switching '+str(value))
 unreal.MaterialLibrary.set_scalar_parameter_value(world,collection,'LanguageIndex',0)
 check(receipt['plan_sha256']==sha(OUT/'plan.json'),'Installed current placement rules')
 if receipt.get('wall_policy')=='preserve_original_material':
  check(receipt['wall_policy_sha256']==sha(ROOT/'Docs/StreetSocialCity/production/city-wall-policy.json'),'Current preservation policy')
  for q in receipt['wall_slots']:check(q['material']==q['original'],'Original building wall identity restored',q['actor']+'#'+str(q['slot']))
 write(OUT/'audit.json',dict(status='passed' if not issues else 'failed',checks=checks,issues=issues,buildings=len(receipt['buildings']),wall_slots=len(receipt['wall_slots']),unique_shaders=len(shader),dressing_instances=len(receipt['dressing']['accepted']),skipped_candidates=len(receipt['dressing']['skipped']),corridors=after,baseline_blocked=[q['id'] for q in baseline['corridors'] if not q['clear']],new_obstructions=regressions,map_sha256=sha(ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap')))
 assert not issues,str(issues[:6])

def nature_material(old):
 original=EAL.get_metadata_tag(old,'CityArtOriginal')
 if original:old=EAL.load_asset(original)
 digest=hashlib.sha256((VERSION+old.get_path_name()+'nature').encode()).hexdigest();dest=CITYPREFIX+'/Nature/M_'+digest[:16]
 if EAL.does_asset_exist(dest):return EAL.load_asset(dest)
 assert isinstance(old,unreal.Material);m=EAL.duplicate_asset(old.get_path_name(),dest);source=MEL.get_material_property_input_node(m,unreal.MaterialProperty.MP_BASE_COLOR);pin=MEL.get_material_property_input_node_output_name(m,unreal.MaterialProperty.MP_BASE_COLOR);assert source
 p=MEL.create_material_expression(m,unreal.MaterialExpressionWorldPosition);n=MEL.create_material_expression(m,unreal.MaterialExpressionCustom);n.set_editor_property('inputs',[custom_input('Base'),custom_input('P')]);n.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT3);n.set_editor_property('code','float l=dot(Base,float3(.2126,.7152,.0722));float patch=.94+.045*sin(P.x/6200.0)+.025*sin(P.y/4900.0);return lerp(Base,float3(l,l,l),.24)*float3(.76,.8,.7)*patch;');assert MEL.connect_material_expressions(source,pin,n,'Base');assert MEL.connect_material_expressions(p,'',n,'P');assert MEL.connect_material_property(n,'',unreal.MaterialProperty.MP_BASE_COLOR);assert not MEL.recompile_material(m)
 EAL.set_metadata_tag(m,'CityArtOriginal',old.get_path_name());EAL.set_metadata_tag(m,'CityArtVersion',VERSION);save(m);return m

def refresh_language_receipts(actors,receipt):
 allowed={(q['actor'],q['slot']) for q in receipt['wall_slots']};rows=[];baseline={q['actor']:q['materials'] for q in read(OUT/'baseline.json')['components']};adopted=[]
 for group in ['shops-all','campus-all','mall','remaining-all']:
  path=BASE/'localization-v1'/('unreal-'+group+'.json');old=path.read_bytes();r=json.loads(old);backup=OUT/'localization-baselines'/(group+'-'+hashlib.sha256(old).hexdigest()[:16]+'.json');backup.parent.mkdir(exist_ok=True)
  if not backup.exists():backup.write_bytes(old)
  for q in r['actors']:
   c=actors[q['actor']].static_mesh_component;current=[c.get_material(i).get_path_name() if c.get_material(i) else None for i in range(c.get_num_materials())];assert len(current)==len(q['all_materials'])
   for i,(before,after) in enumerate(zip(q['all_materials'],current)):
    if before!=after and (q['actor'],i) not in allowed:
     assert baseline[q['actor']][i]==after,('Unexpected change while refreshing localization receipt',q['actor'],i,before,after)
     adopted.append(dict(actor=q['actor'],slot=i,old_receipt=before,preexisting_native_material=after))
   for s in q['slots']:assert c.get_material(s['slot']).get_path_name()==s['material'],('Localized text changed',q['actor'])
   q['all_materials']=current
  r['city_art_refresh']=dict(version=VERSION,original_sha256=hashlib.sha256(old).hexdigest(),localized_materials_unchanged=True);path.write_text(json.dumps(r,ensure_ascii=False,indent=2),'utf8');rows.append(dict(group=group,actors=len(r['actors']),sha256=sha(path)))
 write(OUT/'localization-preexisting-drift.json',dict(status='recorded',adopted=adopted,scope='Only differences already present in the immutable pre-install native baseline; text slots must still match exactly'))
 return rows

def finish():
 receipt=read(OUT/'install.json');actors={a.get_actor_label():a for a in SUB.get_all_level_actors()};rows=[]
 receipt['dressing']=dress(actors)
 for label,a in actors.items():
  for c in a.get_components_by_class(unreal.StaticMeshComponent):
   if not c.static_mesh:continue
   for i in range(c.get_num_materials()):
    old=c.get_material(i)
    if not old:continue
    name=old.get_path_name().lower();source=EAL.get_metadata_tag(old,'CityArtOriginal');name=(source or name).lower();new=None;kind=None
    if label in ['Campus_SP_Water','City_H1_Water','City_CV_Water']:new=EAL.load_asset('/Game/UniversityCity/ActivityScenesV1/ArtV2/M_pond');kind='single-layer pool water'
    elif ('grass' in name or 'ground-forest' in name) and isinstance(old,unreal.Material):new=nature_material(old);kind='natural lawn and forest floor'
    if new:c.set_material(i,new);rows.append(dict(actor=label,slot=i,original=source or old.get_path_name(),material=new.get_path_name(),kind=kind))
 receipt['special_materials']=rows;receipt['localization_receipts']=refresh_language_receipts(actors,receipt)
 parents={}
 for a in actors.values():
  for c in a.get_components_by_class(unreal.InstancedStaticMeshComponent):
   for i in range(c.get_num_materials()):
    m=c.get_material(i)
    while m and not isinstance(m,unreal.Material):m=m.get_editor_property('parent')
    if m and m.get_path_name().startswith('/Game/UniversityCity/'):parents[m.get_path_name()]=m
 for m in parents.values():m.set_editor_property('used_with_instanced_static_meshes',True);save(m)
 receipt['instancing_materials_saved']=list(parents)
 assert unreal.EditorLevelLibrary.save_current_level();receipt['map_sha256']=sha(ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap');receipt['plan_sha256']=sha(OUT/'plan.json');write(OUT/'install.json',receipt);write(OUT/'finish.json',dict(status='passed',special_material_slots=len(rows),localization=receipt['localization_receipts'],map_sha256=receipt['map_sha256']))
]====]
