-- Read-only native probe for the world edge / grass pass (Docs/STREET_CITY_WORLD_GAP_INVENTORY_20261029.md §3).
--   world_edge_probe: every large ground-like actor (bounds extent >= 15 m, flat: z extent < 1/4 of xy): label, class,
--   bounds, mesh, per-slot materials (with parent chain); every fog / sky / sky light / directional light / post process
--   volume with its key settings; the union of the city ground. Nothing is saved to the map.
-- Output: output/street-social-city-production/world-edge-v1/probe.json
-- Queued with: lua Tools/street_city_motel_job.lua world_edge_probe Tools/street_city_world_edge_payload.lua
return [====[
EDGE_OUT=ROOT/'output/street-social-city-production/world-edge-v1'
def motel_world_edge_probe():
 def mchain(m):
  out=[];k=0
  while m is not None and k<6:
   out.append(m.get_path_name());k+=1
   try:m=m.get_editor_property('parent') if isinstance(m,unreal.MaterialInstance) else None
   except Exception:m=None
  return out
 def props(o,names):
  r={}
  for n in names:
   try:v=o.get_editor_property(n)
   except Exception:continue
   if isinstance(v,(int,float,bool,str)):r[n]=v
   elif hasattr(v,'r') and hasattr(v,'g'):r[n]=[v.r,v.g,v.b,getattr(v,'a',1.)]
   elif hasattr(v,'x') and hasattr(v,'y'):r[n]=[v.x,v.y,getattr(v,'z',0.)]
   else:r[n]=str(v)
  return r
 ground=[];env=[]
 for a in SUB.get_all_level_actors():
  label=a.get_actor_label();cls=a.get_class().get_name()
  try:o,e=a.get_actor_bounds(False)
  except Exception:continue
  if any(k in cls for k in ('Fog','SkyAtmosphere','SkyLight','DirectionalLight','PostProcessVolume','VolumetricCloud')):
   comps=[]
   for c in a.get_components_by_class(unreal.ActorComponent):
    comps.append(dict(cls=c.get_class().get_name(),props=props(c,['fog_density','fog_height_falloff','start_distance','fog_max_opacity','fog_cutoff_distance','fog_inscattering_luminance','fog_inscattering_color','directional_inscattering_luminance','second_fog_data','volumetric_fog','intensity','light_color','source_type','cubemap_resolution','sky_distance_threshold','real_time_capture','b_lower_hemisphere_is_black','lower_hemisphere_color','mobility','temperature','use_temperature','atmosphere_sun_light'])))
   env.append(dict(label=label,cls=cls,loc=[o.x,o.y,o.z],ext=[e.x,e.y,e.z],hidden=a.is_hidden_ed(),comps=comps))
   continue
  if max(e.x,e.y)<1500 or e.z*4>max(e.x,e.y) or e.x>2000000 or e.y>2000000:continue
  slots=[]
  for c in a.get_components_by_class(unreal.StaticMeshComponent):
   sm=c.static_mesh
   if sm is None:continue
   for i in range(c.get_num_materials()):
    m=c.get_material(i);slots.append(dict(comp=c.get_name(),mesh=sm.get_path_name(),slot=i,chain=mchain(m),instanced=isinstance(c,unreal.InstancedStaticMeshComponent)))
  ground.append(dict(label=label,cls=cls,folder=str(a.get_folder_path()),min=[o.x-e.x,o.y-e.y,o.z-e.z],max=[o.x+e.x,o.y+e.y,o.z+e.z],slots=slots))
 city=[g for g in ground if re.match(r'^G\d\d$',g['label'])]
 union=[min(g['min'][0] for g in city),min(g['min'][1] for g in city),max(g['max'][0] for g in city),max(g['max'][1] for g in city)] if city else None
 # ground height along the union border (3 m inside, every 20 m): the outer skirt's inner rim follows these so it meets
 # the city ground without a step. Small actors (extent < 15 m: trees, lamps) are passed through like outdoor_scan.
 world=unreal.EditorLevelLibrary.get_editor_world();info={}
 for a in SUB.get_all_level_actors():
  try:info[a.get_actor_label()]=a.get_actor_bounds(False)
  except Exception:pass
 edge=[]
 if union:
  x0,y0,x1,y1=union;top=max(g['max'][2] for g in city)+5000;bot=min(g['min'][2] for g in city)-5000
  def down(x,y):
   ign=[]
   for _ in range(8):
    r=unreal.SystemLibrary.line_trace_single(world,unreal.Vector(x,y,top),unreal.Vector(x,y,bot),unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,ign,unreal.DrawDebugTrace.NONE,True)
    t=r.to_tuple() if r is not None else (False,)
    if not t[0] or t[9] is None:return None
    lab=t[9].get_actor_label();b=info.get(lab)
    if b is not None and max(b[1].x,b[1].y)<1500:ign.append(t[9]);continue
    return dict(z=round(t[4].z,1),label=lab)
   return None
  for side,(ax,ay,bx,by,nx,ny) in dict(north=(x0,y0,x1,y0,0,1),south=(x0,y1,x1,y1,0,-1),west=(x0,y0,x0,y1,1,0),east=(x1,y0,x1,y1,-1,0)).items():
   L=math.hypot(bx-ax,by-ay);n=max(1,int(L//2000))
   for i in range(n+1):
    x=ax+(bx-ax)*i/n+nx*300;y=ay+(by-ay)*i/n+ny*300;h=down(x,y)
    edge.append(dict(side=side,x=round(x),y=round(y),hit=h))
 assert sha(MAPFILE)==LOAD_SHA
 EDGE_OUT.mkdir(parents=True,exist_ok=True)
 (EDGE_OUT/'probe.json').write_text(json.dumps(dict(status='read_only_probe',map_sha256=LOAD_SHA,city_ground_union=union,ground=ground,environment=env,edge_samples=edge),ensure_ascii=False,indent=1),'utf8')

def motel_world_edge_install():
 # world_edge_install: import world-edge-v1/skirt/skirt.glb (Tools/build_street_city_world_edge.lua) and place it as one
 # NoCollision, no-shadow actor WorldEdge_Skirt with the grass-hills triplanar material. The skirt is authored in UE world
 # metres; the importer's axis convention is found by tracing the authored probe vertices for each yaw x mirror, then the
 # component is set to NoCollision and the placed bounds are checked against the authored ones.
 global PREFIX,KEEP,save
 b=json.loads((EDGE_OUT/'skirt/build.json').read_text('utf8'));assert sha(b['path'])==b['sha256']
 # the city ground union must still be the one the skirt was authored around (other installs change the map SHA)
 G=[a for a in SUB.get_all_level_actors() if re.match(r'^G\d\d$',a.get_actor_label())];bx=[a.get_actor_bounds(False) for a in G]
 U=[min(o.x-e.x for o,e in bx),min(o.y-e.y for o,e in bx),max(o.x+e.x for o,e in bx),max(o.y+e.y for o,e in bx)]
 assert all(abs(u-v*100)<100 for u,v in zip(U,b['union_m'])),('city ground union changed since the skirt was authored',U,b['union_m'])
 PREFIX='/Game/UniversityCity/WorldEdgeV1';KEEP=[];save=save_asset
 src=(ROOT/'Tools/street_city_activity_native_payload.lua').read_text('utf8').split('def import_one(',1)[1].split('def simple_water(',1)[0];ns=dict(globals());exec('def import_one('+src,ns);imp=ns['import_one']
 mesh=imp(Path(b['path']),'world-edge-skirt',0,False)
 grass=EAL.load_asset('/Game/UniversityCity/GrassHillsV1/M_GrassWorldTriplanar_v2');assert grass,'grass hills material missing'
 for k in range(len(mesh.get_editor_property('static_materials'))):mesh.set_material(k,grass)
 bs=mesh.get_editor_property('body_setup')
 if bs:
  bs.set_editor_property('collision_trace_flag',unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX)
  bs.set_editor_property('agg_geom',unreal.KAggregateGeom())
 assert EAL.save_loaded_asset(mesh,False)
 B0=[v*100 for v in b['bounds_m'][0]];B1=[v*100 for v in b['bounds_m'][1]];probes=[[v*100 for v in p] for p in b['probes_m']];assert len(probes)>=4
 def near(a,c):return abs(a-c)<=max(200.,abs(c)*.01)
 actors={a.get_actor_label():a for a in SUB.get_all_level_actors()}
 backup=EDGE_OUT/('UniversityCity-before-world-edge-'+LOAD_SHA[:16]+'.umap')
 if not backup.exists():shutil.copy2(MAPFILE,backup)
 if 'WorldEdge_Skirt' in actors:SUB.destroy_actor(actors['WorldEdge_Skirt'])
 a=SUB.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(0,0,0),unreal.Rotator(0,0,0));a.set_actor_label('WorldEdge_Skirt')
 a.tags=[unreal.Name('WorldEdgeV1')];a.set_folder_path('UniversityCity/WorldEdgeV1')
 c=a.static_mesh_component;c.set_static_mesh(mesh);c.set_editor_property('cast_shadow',False);c.set_editor_property('mobility',unreal.ComponentMobility.STATIC)
 # the imported axis convention is checked on the mesh's own vertices (MeshDescription), not by physics traces (the
 # editor physics scene does not see a fresh collision setup) nor by bounds (a y-mirrored skirt passed a bounds check and
 # covered the city in round 12): each yaw x mirror candidate maps the local vertices to world, and every authored probe
 # vertex must land within 5 cm of one of them
 md=mesh.get_static_mesh_description(0);loc=[md.get_vertex_position(unreal.VertexID(i)) for i in range(md.get_vertex_count())]
 assert loc,'no vertices in the imported skirt'
 pick=None;tried=[]
 for yaw in (0,90,180,270):
  for sy in (1,-1):
   cs,sn=round(math.cos(math.radians(yaw))),round(math.sin(math.radians(yaw)))
   W=[(cs*v.x-sn*sy*v.y,sn*v.x+cs*sy*v.y,v.z) for v in loc]
   miss=[p for p in probes if min(abs(w[0]-p[0])+abs(w[1]-p[1])+abs(w[2]-p[2]) for w in W)>5]
   tried.append(dict(yaw=yaw,mirror_y=sy,missing=len(miss)))
   if not miss and pick is None:pick=(yaw,sy)
 assert pick,('no orientation puts the skirt on its authored vertices',tried)
 a.set_actor_rotation(unreal.Rotator(0,0,pick[0]),False);a.set_actor_scale3d(unreal.Vector(1,pick[1],1))
 # visual only: no collision at all (the round-12 skirt answered the audit's visibility traces with its convex hull)
 c.set_collision_profile_name('NoCollision');c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION);c.set_editor_property('can_character_step_up_on',unreal.CanBeCharacterBase.ECB_NO)
 c.set_editor_property('generate_overlap_events',False)
 o,e=a.get_actor_bounds(False);got=[o.x-e.x,o.y-e.y,o.x+e.x,o.y+e.y];want=[B0[0],B0[1],B1[0],B1[1]]
 assert all(near(g,w) for g,w in zip(got,want)),('placed bounds differ from the authored world bounds',got,want)
 actors.get('CityEnvironmentController') and actors['CityEnvironmentController'].prepare_for_save()
 assert unreal.EditorLevelLibrary.save_current_level();new=sha(MAPFILE)
 installed=read('motel-install');installed.update(parent_map_sha256=LOAD_SHA,map_sha256=new);write('motel-install',installed)  # the motel audit keys on the current map
 (EDGE_OUT/('install-'+time.strftime('%Y%m%d-%H%M%S')+'.json')).write_text(json.dumps(dict(status='saved_requires_capture_review',map_sha256=new,parent_map_sha256=LOAD_SHA,backup=str(backup),asset=mesh.get_path_name(),orientation_trials=tried,actor_yaw=pick[0],actor_mirror_y=pick[1],placed_bounds=got,triangles=mesh.get_num_triangles(0)),ensure_ascii=False,indent=1),'utf8')
def motel_world_shore_fix():
 # world_shore_fix (rule, whole map): every open-sea plane continues the city's shared water surface (see below).
 # A sea plane is any StaticMeshActor whose material path contains 'ocean' and whose footprint reaches outside the city
 # ground union (in-city ponds / rivers are separate *_Water actors and keep their level). The land level is the lowest
 # border ground height from world-edge-v1/probe.json edge samples (3 m inside the union, every 20 m on all sides), so the
 # rule follows the ground wherever the sea touches it. Visible planes get their top at that level - 5 cm; hidden planes
 # are only recorded. (10-30: west sea planes ACTSPACE-04-351 / -345 stood 0.9 / 5 m above the z 1000 beach, A03.)
 # 10-30 correction: the z 1000 'beach' is the sea bed of the flat-campus water map; the in-city sea / river / pond
 # actors G??_Water share one water surface (z 1100, prepare_street_city_flat_campus.lua) and cover it, so lowering the
 # ocean to 995 opened a 1 m water step at the west edge (shore-views SH02-SH10). The open sea must continue that shared
 # level: TOP = the common top of the *_Water actors; the border-ground rule is only the fallback when there are none.
 P=json.loads((EDGE_OUT/'probe.json').read_text('utf8'));x0,y0,x1,y1=P['city_ground_union']
 actors={a.get_actor_label():a for a in SUB.get_all_level_actors()};rows=[]
 wt=sorted(round(o.z+e.z) for o,e in (a.get_actor_bounds(False) for l,a in actors.items() if re.match(r'^G\d\d_Water$',l)))
 if wt:
  assert wt[-1]-wt[0]<=1,('in-city water actors disagree on the water level',wt);TOP=float(wt[0])
 else:
  land=[s['hit']['z'] for s in P['edge_samples'] if s['hit']];assert land,'no border ground samples';TOP=min(land)-5.
 def is_sea(a):
  if not isinstance(a,unreal.StaticMeshActor):return False
  c=a.static_mesh_component
  try:mats=[(c.get_material(k).get_path_name() if c.get_material(k) else '') for k in range(c.get_num_materials())]
  except Exception:return False
  if not any('ocean' in m.lower() for m in mats):return False
  o,e=a.get_actor_bounds(False);return o.x-e.x<x0-100 or o.y-e.y<y0-100 or o.x+e.x>x1+100 or o.y+e.y>y1+100
 sea=[l for l,a in actors.items() if is_sea(a)];assert sea,'no sea plane found'
 backup=EDGE_OUT/('UniversityCity-before-shore-fix-'+LOAD_SHA[:16]+'.umap')
 if not backup.exists():shutil.copy2(MAPFILE,backup)
 for lab in sea:
  a=actors[lab];c=a.static_mesh_component;o,e=a.get_actor_bounds(False);pos=a.get_actor_location()
  hidden=bool(a.is_hidden_ed()) or bool(a.get_editor_property('hidden')) or not bool(c.get_editor_property('visible')) or bool(c.get_editor_property('hidden_in_game'))
  r=dict(label=lab,hidden=hidden,before_loc_z=pos.z,before_top_z=o.z+e.z)
  if not hidden and abs(o.z+e.z-TOP)>=1:
   a.set_actor_location(unreal.Vector(pos.x,pos.y,pos.z+(TOP-(o.z+e.z))),False,True);o2,e2=a.get_actor_bounds(False)
   assert abs(o2.z+e2.z-TOP)<1,(lab,o2.z+e2.z);r.update(after_loc_z=a.get_actor_location().z,after_top_z=o2.z+e2.z)
  rows.append(r)
 if not any('after_top_z' in r for r in rows):
  (EDGE_OUT/'shore-fix-noop.json').write_text(json.dumps(dict(status='already_satisfied',map_sha256=LOAD_SHA,sea_top_z=TOP,planes=rows),ensure_ascii=False,indent=1),'utf8');return
 actors.get('CityEnvironmentController') and actors['CityEnvironmentController'].prepare_for_save()
 assert unreal.EditorLevelLibrary.save_current_level();new=sha(MAPFILE)
 installed=read('motel-install');installed.update(parent_map_sha256=LOAD_SHA,map_sha256=new);write('motel-install',installed)
 (EDGE_OUT/('shore-fix-'+time.strftime('%Y%m%d-%H%M%S')+'.json')).write_text(json.dumps(dict(status='saved_requires_capture_review',map_sha256=new,parent_map_sha256=LOAD_SHA,backup=str(backup),sea_top_z=TOP,planes=rows),ensure_ascii=False,indent=1),'utf8')
def motel_world_ground_tex_probe():
 # world_ground_tex_probe (read-only): for every material used on the city ground chunks G01-G16 (the Tripo-ground-urban /
 # Tripo-ground-campus masters and the M_GroundNatural instances), the textures it uses with their source file (asset
 # import data), size and sRGB flag; each texture is also exported as PNG into world-edge-v1/ground-tex/ so the grass pass
 # can measure average colours offline and tell lawn slots from paving.
 MEL=unreal.MaterialEditingLibrary;dst=EDGE_OUT/'ground-tex';dst.mkdir(parents=True,exist_ok=True);mats={};tex={}
 for a in SUB.get_all_level_actors():
  if not re.match(r'^G\d\d$',a.get_actor_label()):continue
  for c in a.get_components_by_class(unreal.StaticMeshComponent):
   for k in range(c.get_num_materials()):
    m=c.get_material(k)
    if m is None:continue
    r=mats.setdefault(m.get_path_name(),dict(actors=[],slots=0,textures=[]));r['slots']+=1
    if a.get_actor_label() not in r['actors']:r['actors'].append(a.get_actor_label())
 for p,r in mats.items():
  m=EAL.load_asset(p.split('.')[0]);used=[]
  # GetUsedTextures takes a base Material only: an instance contributes its overridden texture parameters plus the
  # parent's used textures
  if isinstance(m,unreal.MaterialInstance):
   r['parent']=m.get_base_material().get_path_name();r['texture_params']={}
   for n in MEL.get_texture_parameter_names(m):
    tx=MEL.get_material_instance_texture_parameter_value(m,n);r['texture_params'][str(n)]=tx.get_path_name() if tx else None
    if tx:used.append(tx)
   used+=list(MEL.get_used_textures(m.get_base_material()))
  else:used=list(MEL.get_used_textures(m))
  for t in used:
   tp=t.get_path_name();r['textures'].append(tp)
   if tp in tex:continue
   src=''
   try:
    aid=t.get_editor_property('asset_import_data');src=aid.get_first_filename() if aid else ''
   except Exception:pass
   out=dst/(re.sub(r'[^A-Za-z0-9_.-]','_',tp.split('.')[-1])+'.png')
   ok=False
   try:
    task=unreal.AssetExportTask();task.object=t;task.filename=str(out);task.automated=True;task.prompt=False;task.replace_identical=True
    task.exporter=unreal.TextureExporterPNG();ok=bool(unreal.Exporter.run_asset_export_task(task))
   except Exception as e:ok=repr(e)
   tex[tp]=dict(source=src,png=str(out) if ok is True else None,export=ok,size=[t.blueprint_get_size_x(),t.blueprint_get_size_y()],srgb=bool(t.get_editor_property('srgb')))
 assert sha(MAPFILE)==LOAD_SHA
 (EDGE_OUT/'ground-tex-probe.json').write_text(json.dumps(dict(status='read_only_probe',map_sha256=LOAD_SHA,materials=mats,textures=tex),ensure_ascii=False,indent=1),'utf8')
def motel_world_ground_master_probe():
 # world_ground_master_probe (read-only): the Tripo-ground-urban / Tripo-ground-campus masters use no textures
 # (ground-tex-probe, 10-30), so their colour comes from expressions (constants, vector parameters, vertex colour).
 # Record every expression of both masters: class, editor position, and the colour / scalar / parameter-name fields, plus
 # the scalar / vector parameter values of every instance of them used on G01-G16, so the grass rule can compare their
 # lawn colour against the meadow texture (sRGB mean 86,115,49).
 MEL=unreal.MaterialEditingLibrary;out={}
 for name in ('Tripo-ground-urban','Tripo-ground-campus'):
  hits=[d for d in EAL.list_assets('/Game',True,False) if d.split('/')[-1].split('.')[0]==name]
  rows=[]
  for path in hits:
   m=EAL.load_asset(path.split('.')[0])
   if not isinstance(m,unreal.Material):continue
   ex=[]
   for e in unreal.MaterialEditingLibrary.get_material_expressions(m) if hasattr(unreal.MaterialEditingLibrary,'get_material_expressions') else []:
    r=dict(cls=e.get_class().get_name())
    for f in ('parameter_name','constant','r','r_value','default_value','coordinate_index','texture'):
     try:v=e.get_editor_property(f)
     except Exception:continue
     r[f]=[v.r,v.g,v.b,v.a] if hasattr(v,'r') and hasattr(v,'a') else (v.get_path_name() if hasattr(v,'get_path_name') else (str(v) if not isinstance(v,(int,float,bool,str)) else v))
     # the texture's import source tells the ground kind (world-ground-v1/assets/<kind>/basecolor-2k.png)
     if f=='texture' and hasattr(v,'get_editor_property'):
      try:aid=v.get_editor_property('asset_import_data');r['texture_source']=aid.get_first_filename() if aid else ''
      except Exception:pass
    ex.append(r)
   props={}
   for p in ('MP_BASE_COLOR','MP_ROUGHNESS','MP_NORMAL'):
    try:n=MEL.get_material_property_input_node(m,getattr(unreal.MaterialProperty,p));props[p]=n.get_class().get_name() if n else None
    except Exception as e:props[p]=repr(e)
   rows.append(dict(path=path,expressions=ex,inputs=props,scalars=[str(n) for n in MEL.get_scalar_parameter_names(m)],vectors=[str(n) for n in MEL.get_vector_parameter_names(m)]))
  out[name]=rows
 inst={}
 for a in SUB.get_all_level_actors():
  if not re.match(r'^G\d\d$',a.get_actor_label()):continue
  for c in a.get_components_by_class(unreal.StaticMeshComponent):
   sm=c.static_mesh
   for k in range(c.get_num_materials()):
    m=c.get_material(k)
    if m is None or 'Tripo-ground' not in m.get_base_material().get_name():continue
    r=inst.setdefault(m.get_path_name(),dict(base=m.get_base_material().get_path_name(),scalars={},vectors={},uses=[]))
    r['uses'].append(dict(actor=a.get_actor_label(),mesh=sm.get_path_name() if sm else None,slot=k))
    for n in MEL.get_scalar_parameter_names(m):r['scalars'][str(n)]=MEL.get_material_instance_scalar_parameter_value(m,n)
    for n in MEL.get_vector_parameter_names(m):v=MEL.get_material_instance_vector_parameter_value(m,n);r['vectors'][str(n)]=[v.r,v.g,v.b,v.a]
 # which ground meshes carry vertex colour (the Tripo bake stores its albedo there when the master reads VertexColor)
 vc={}
 for a in SUB.get_all_level_actors():
  if not re.match(r'^G\d\d$',a.get_actor_label()):continue
  for c in a.get_components_by_class(unreal.StaticMeshComponent):
   sm=c.static_mesh
   if sm is None or sm.get_path_name() in vc:continue
   try:md=sm.get_static_mesh_description(0);vc[sm.get_path_name()]=dict(vertices=md.get_vertex_count(),has_colors=bool(sm.get_editor_property('has_vertex_colors')) if hasattr(sm,'get_editor_property') else None)
   except Exception as e:vc[sm.get_path_name()]=repr(e)
 assert sha(MAPFILE)==LOAD_SHA
 (EDGE_OUT/'ground-master-probe.json').write_text(json.dumps(dict(status='read_only_probe',map_sha256=LOAD_SHA,masters=out,instances=inst,meshes=vc),ensure_ascii=False,indent=1),'utf8')
def motel_world_view_ground_probe():
 # world_view_ground_probe (read-only): for each view in world-edge-v1/view-ground-views.json ({id,eye,target}), trace
 # straight down at points along the view direction (5-400 m ahead, plus +-20 / 40 m to the sides) and record every
 # hit on the way down (actor label, class, component, mesh, material, z), so a colour patch seen in a capture can be
 # traced to the surface that draws it. Visibility traces with complex collision; small actors are passed through.
 V=json.loads((EDGE_OUT/'view-ground-views.json').read_text('utf8'));world=unreal.EditorLevelLibrary.get_editor_world();rows=[]
 def hits(x,y):
  out=[];ign=[]
  for _ in range(6):
   r=unreal.SystemLibrary.line_trace_single(world,unreal.Vector(x,y,30000),unreal.Vector(x,y,-5000),unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,ign,unreal.DrawDebugTrace.NONE,True)
   t=r.to_tuple() if r is not None else (False,)
   if not t[0] or t[9] is None:break
   a=t[9];c=t[10];mat=''
   try:m=c.get_material(t[11]) if c is not None and t[11]>=0 else (c.get_material(0) if c is not None else None);mat=m.get_path_name() if m else ''
   except Exception:pass
   mesh=''
   try:mesh=c.static_mesh.get_path_name()
   except Exception:pass
   out.append(dict(z=round(t[4].z,1),label=a.get_actor_label(),cls=a.get_class().get_name(),comp=c.get_name() if c else '',mesh=mesh.split('/')[-1],mat=mat.split('/')[-1]));ign.append(a)
  return out
 for v in V:
  ex,ey=v['eye'][0],v['eye'][1];dx,dy=v['target'][0]-ex,v['target'][1]-ey;L=math.hypot(dx,dy) or 1;dx,dy=dx/L,dy/L
  pts=[]
  for d in (500,1500,3000,6000,10000,20000,40000):
   for s in (-4000,-2000,0,2000,4000):pts.append(dict(ahead_cm=d,side_cm=s,x=round(ex+dx*d-dy*s),y=round(ey+dy*d+dx*s)))
  for p in pts:p['hits']=hits(p['x'],p['y'])
  rows.append(dict(id=v['id'],points=pts))
 assert sha(MAPFILE)==LOAD_SHA
 (EDGE_OUT/'view-ground-probe.json').write_text(json.dumps(dict(status='read_only_probe',map_sha256=LOAD_SHA,views=rows),ensure_ascii=False,indent=1),'utf8')
def motel_world_grass_unify():
 # world_grass_unify (rule, whole map): every lawn in the city must look like one lawn. The grass patches (A13, G086,
 # G148) are different M_GroundNatural instances side by side: the city chunks G01-G16 use one meadow instance, while
 # Outdoor_annex_ground / parks / stadium / track each got their own MI_Detail_* with other scalar / vector values.
 # Canonical lawn = the M_GroundNatural instance on the most G?? slots whose base-colour texture parameter is a grass
 # texture (name contains 'grass'). Every other M_GroundNatural instance used anywhere in the level with that same
 # base-colour texture gets the canonical scalar and vector parameter values (textures untouched). Asset edits only;
 # the previous values are written to world-edge-v1/grass-unify-*.json so the pass can be reverted.
 MEL=unreal.MaterialEditingLibrary
 def tex(m):
  r={}
  for n in MEL.get_texture_parameter_names(m):t=MEL.get_material_instance_texture_parameter_value(m,n);r[str(n)]=t.get_path_name() if t else None
  return r
 def is_gn(m):return isinstance(m,unreal.MaterialInstance) and m.get_base_material().get_name()=='M_GroundNatural'
 used={};count={}
 for a in SUB.get_all_level_actors():
  for c in a.get_components_by_class(unreal.StaticMeshComponent):
   for k in range(c.get_num_materials()):
    m=c.get_material(k)
    if not is_gn(m):continue
    used[m.get_path_name()]=m
    if re.match(r'^G\d\d$',a.get_actor_label()):count[m.get_path_name()]=count.get(m.get_path_name(),0)+1
 def grass_key(m):
  t=tex(m);return next((v for n,v in sorted(t.items()) if v and 'grass' in v.lower() and 'basecolor' in v.lower()),None)
 lawn=[(n,p) for p,n in count.items() if grass_key(used[p])];assert lawn,'no grass instance on the city chunks'
 canon=used[max(lawn)[1]];key=grass_key(canon)
 sv={str(n):MEL.get_material_instance_scalar_parameter_value(canon,n) for n in MEL.get_scalar_parameter_names(canon)}
 vv={str(n):MEL.get_material_instance_vector_parameter_value(canon,n) for n in MEL.get_vector_parameter_names(canon)}
 # lawn family: base colour named *grass* / *meadow* / *lawn* (T_lawn_turf_v2/v3 on Outdoor_annex_ground, the parks
 # and World_PK_* made the A13 / G086 / G148 patches, 10-30 inventory). Sports turf keeps its own look: an instance is
 # unified only when none of its users is a sports facility (stadium / track / court / field).
 def lawn(m):return any(w in (v or '').lower() for v in tex(m).values() if 'basecolor' in (v or '').lower() for w in ('grass','meadow','lawn'))
 users={}
 for a in SUB.get_all_level_actors():
  for c in a.get_components_by_class(unreal.StaticMeshComponent):
   for k in range(c.get_num_materials()):
    m=c.get_material(k)
    if is_gn(m):users.setdefault(m.get_path_name(),set()).add(a.get_actor_label())
 sports=re.compile(r'stadium|track|court|field|pitch',re.I)
 ctex=tex(canon)
 rows=[]
 for p,m in sorted(used.items()):
  if m==canon or not lawn(m) or any(sports.search(u) for u in users.get(p,())):continue
  before=dict(scalars={},vectors={},textures=tex(m));changed=[]
  for n,v in ctex.items():
   if before['textures'].get(n)!=v and v:MEL.set_material_instance_texture_parameter_value(m,n,EAL.load_asset(v.split('.')[0]));changed.append(n)
  for n,v in sv.items():
   old=MEL.get_material_instance_scalar_parameter_value(m,n);before['scalars'][n]=old
   if abs(old-v)>1e-6:MEL.set_material_instance_scalar_parameter_value(m,n,v);changed.append(n)
  for n,v in vv.items():
   old=MEL.get_material_instance_vector_parameter_value(m,n);before['vectors'][n]=[old.r,old.g,old.b,old.a]
   if max(abs(old.r-v.r),abs(old.g-v.g),abs(old.b-v.b),abs(old.a-v.a))>1e-6:MEL.set_material_instance_vector_parameter_value(m,n,v);changed.append(n)
  if changed:MEL.update_material_instance(m);assert EAL.save_loaded_asset(m,False)
  rows.append(dict(path=p,changed=changed,before=before))
 others=sorted({grass_key(m) for m in used.values()}-{key,None})
 # every M_GroundNatural instance in the level with its textures / parameters / users (before this pass's edits are
 # visible only in rows[].before)
 inventory=[dict(path=p,textures=tex(m),scalars={str(n):MEL.get_material_instance_scalar_parameter_value(m,n) for n in MEL.get_scalar_parameter_names(m)},vectors={str(n):(lambda v:[v.r,v.g,v.b,v.a])(MEL.get_material_instance_vector_parameter_value(m,n)) for n in MEL.get_vector_parameter_names(m)},actors=sorted(users.get(p,()))[:12],actor_count=len(users.get(p,()))) for p,m in sorted(used.items())]
 assert sha(MAPFILE)==LOAD_SHA
 (EDGE_OUT/('grass-unify-'+time.strftime('%Y%m%d-%H%M%S')+'.json')).write_text(json.dumps(dict(status='saved_assets_requires_capture_review',map_sha256=LOAD_SHA,canonical=canon.get_path_name(),grass_texture=key,canonical_scalars=sv,canonical_vectors={n:[v.r,v.g,v.b,v.a] for n,v in vv.items()},instances=rows,other_grass_textures=others,inventory=inventory),ensure_ascii=False,indent=1),'utf8')
def motel_world_atmosphere():
 # world_atmosphere: the whitish far haze (inventory section 3 item 5) is the map's SkyAtmosphere aerial perspective
 # (CityEnvironmentController only drives TransmittanceMinLightElevationAngle). Records every SkyAtmosphereComponent
 # property listed below, then applies world-edge-v1/atmosphere-target.json ({prop: value}; colours as [r,g,b,a]) when
 # it exists and is non-empty, saving the map with a backup. Without targets it is a read-only probe.
 names=['mie_scattering_scale','mie_absorption_scale','mie_anisotropy','mie_exponential_distribution','rayleigh_scattering_scale','rayleigh_exponential_distribution','multi_scattering_factor','aerial_pespective_view_distance_scale','aerial_perspective_start_depth','height_fog_contribution','sky_luminance_factor','atmosphere_height','bottom_radius','mie_scattering','mie_absorption','rayleigh_scattering','ground_albedo','transmittance_min_light_elevation_angle']
 def val(v):return [v.r,v.g,v.b,v.a] if hasattr(v,'r') and hasattr(v,'a') else (v if isinstance(v,(int,float,bool,str)) else str(v))
 comps=[]
 for a in SUB.get_all_level_actors():
  for c in a.get_components_by_class(unreal.SkyAtmosphereComponent):comps.append((a,c))
 assert comps,'no SkyAtmosphere'
 before=[]
 for a,c in comps:
  p={}
  for n in names:
   try:p[n]=val(c.get_editor_property(n))
   except Exception:pass
  before.append(dict(actor=a.get_actor_label(),props=p))
 tp=EDGE_OUT/'atmosphere-target.json';target=json.loads(tp.read_text('utf8')) if tp.exists() else {}
 out=dict(status='read_only_probe',map_sha256=LOAD_SHA,before=before)
 if target:
  backup=EDGE_OUT/('UniversityCity-before-atmosphere-'+LOAD_SHA[:16]+'.umap')
  if not backup.exists():shutil.copy2(MAPFILE,backup)
  for a,c in comps:
   for n,v in target.items():
    c.set_editor_property(n,unreal.LinearColor(*v) if isinstance(v,list) else v)
  actors={x.get_actor_label():x for x in SUB.get_all_level_actors()}
  actors.get('CityEnvironmentController') and actors['CityEnvironmentController'].prepare_for_save()
  assert unreal.EditorLevelLibrary.save_current_level();new=sha(MAPFILE)
  installed=read('motel-install');installed.update(parent_map_sha256=LOAD_SHA,map_sha256=new);write('motel-install',installed)
  out.update(status='saved_requires_capture_review',map_sha256=new,parent_map_sha256=LOAD_SHA,backup=str(backup),target=target)
 else:assert sha(MAPFILE)==LOAD_SHA
 (EDGE_OUT/('atmosphere-'+time.strftime('%Y%m%d-%H%M%S')+'.json')).write_text(json.dumps(out,ensure_ascii=False,indent=1),'utf8')
def motel_world_coast_edge_probe():
 # world_coast_edge_probe (read-only): where the walkable land top actually ends along the west sea coast. Every 1 m in
 # y over the city ground union, march x from 200 m inland toward the sea at 0.5 m steps with downward traces that pass
 # through water / ocean surfaces and small actors, and record the first x (from inland) where the ground top falls
 # below 1150 cm, plus the top z there. The stepped edge this measures (ground-sheet polygons over a 2.5 m 1000/1200
 # height grid) is what the beach-edge strip must cover. Output world-edge-v1/coast-edge-probe.json.
 P=json.loads((EDGE_OUT/'probe.json').read_text('utf8'));x0,y0,x1,y1=P['city_ground_union']
 world=unreal.EditorLevelLibrary.get_editor_world();info={}
 for a in SUB.get_all_level_actors():
  try:info[a.get_actor_label()]=a.get_actor_bounds(False)
  except Exception:pass
 def wetlabel(a):
  l=a.get_actor_label()
  return bool(re.match(r'^G\d\d_Water$',l)) or l.startswith('Activity_ACTSPACE')
 def top(x,y):
  ign=[]
  for _ in range(10):
   r=unreal.SystemLibrary.line_trace_single(world,unreal.Vector(x,y,5000),unreal.Vector(x,y,-5000),unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,ign,unreal.DrawDebugTrace.NONE,True)
   t=r.to_tuple() if r is not None else (False,)
   if not t[0] or t[9] is None:return None
   a=t[9];b=info.get(a.get_actor_label())
   if wetlabel(a) or (b is not None and max(b[1].x,b[1].y)<1500):ign.append(a);continue
   return t[4].z,a.get_actor_label()
  return None
 rows=[];y=y0+50
 while y<y1:
  edge=None;x=x0+20000;last=None
  while x>x0-2000:
   h=top(x,y)
   if h is None or h[0]<1150:edge=dict(x=round(x),below=None if h is None else round(h[0],1),label=None if h is None else h[1],land_x=last);break
   last=round(x);x-=50
  rows.append(dict(y=round(y),edge=edge));y+=100
 assert sha(MAPFILE)==LOAD_SHA
 (EDGE_OUT/'coast-edge-probe.json').write_text(json.dumps(dict(status='read_only_probe',map_sha256=LOAD_SHA,step_y_cm=100,step_x_cm=50,rows=rows),ensure_ascii=False,indent=1),'utf8')
def motel_world_shore_grid_probe():
 # world_shore_grid_probe (read-only): a 1 m raster of the original ground under the west coast and the river mouth, so
 # the beach strip can follow the real waterline in 2D (the per-row coast_edge_probe only knows "first land from the
 # east" and cut the beach square at the river mouth, A03 2026-10-31). Region x0-30 m .. x0+300 m over the whole union
 # y. Per cell: ground top z (cm, int; -1 none) under any water / ocean surface, ignoring small actors and our own
 # WorldEdge_/Beach_/Park_ dressing, and whether a water surface lies over it. Output world-edge-v1/shore-grid.json
 # (rows along y; z as space separated ints, wet as a 0/1 string).
 P=json.loads((EDGE_OUT/'probe.json').read_text('utf8'));x0,y0,x1,y1=P['city_ground_union']
 world=unreal.EditorLevelLibrary.get_editor_world();info={}
 for a in SUB.get_all_level_actors():
  try:info[a.get_actor_label()]=a.get_actor_bounds(False)
  except Exception:pass
 def wetlabel(l):return bool(re.match(r'^G\d\d_Water$',l)) or l.startswith('Activity_ACTSPACE')
 def cell(x,y):
  ign=[];wet=False
  for _ in range(12):
   r=unreal.SystemLibrary.line_trace_single(world,unreal.Vector(x,y,5000),unreal.Vector(x,y,-5000),unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,ign,unreal.DrawDebugTrace.NONE,True)
   t=r.to_tuple() if r is not None else (False,)
   if not t[0] or t[9] is None:return -1,wet
   a=t[9];l=a.get_actor_label();b=info.get(l)
   if wetlabel(l):wet=True;ign.append(a);continue
   if l.startswith(('WorldEdge_','Beach_','Park_','Landmark_')) or (b is not None and max(b[1].x,b[1].y)<1500):ign.append(a);continue
   return int(round(t[4].z)),wet
  return -1,wet
 xs=list(range(int(x0)-3000,int(x0)+30000,100));rows=[];y=int(y0)+50
 while y<y1:
  cs=[cell(x,y) for x in xs];rows.append(dict(y=y,z=' '.join(str(c[0]) for c in cs),wet=''.join('1' if c[1] else '0' for c in cs)));y+=100
 assert sha(MAPFILE)==LOAD_SHA
 (EDGE_OUT/'shore-grid.json').write_text(json.dumps(dict(status='read_only_probe',map_sha256=LOAD_SHA,step_cm=100,x_cm=xs,rows=rows)),'utf8')
def motel_world_water_cover_probe():
 # world_water_cover_probe (read-only): which 1 m cells of the shore grid (world-edge-v1/shore-grid.json, same x/y
 # samples) are covered by a G??_Water surface mesh. Those planes have no collision, so traces cannot see them; their
 # triangles (static mesh description, actor transform) are rasterised instead. A water bed cell (ground <= 1060) that
 # no surface covers is a hole that shows the bare bed (the dark blue squares off the river mouth, A03 2026-10-31).
 # Output world-edge-v1/water-cover.json (rows: cover = how many surface meshes cover the cell (0-9; stacked layers
 # darken the translucent water), low = the lowest covering surface z, 9999 none).
 G=json.loads((EDGE_OUT/'shore-grid.json').read_text('utf8'));xs=G['x_cm'];ys=[r['y'] for r in G['rows']];st=G['step_cm']
 cov=[bytearray(len(xs)) for _ in ys];low=[[9999]*len(xs) for _ in ys];last=[[-1]*len(xs) for _ in ys];cid=0;zs=set()
 for a in SUB.get_all_level_actors():
  if not re.match(r'^G\d\d_Water$',a.get_actor_label()):continue
  T=a.get_actor_transform()
  for c in a.get_components_by_class(unreal.StaticMeshComponent):
   md=c.static_mesh.get_static_mesh_description(0) if c.static_mesh else None
   if md is None:continue
   cid+=1  # count each surface once per cell, not each triangle (shared diagonals)
   for t in range(md.get_triangle_count()):
    vs=md.get_triangle_vertices(unreal.TriangleID(t))
    Q=[T.transform_location(md.get_vertex_position(v)) for v in vs]
    if len(Q)<3:continue
    zq=round(sum(p.z for p in Q)/len(Q));zs.add(zq)
    # the description may hand back the polygon's vertices (more than three): fan it
    for P in [(Q[0],Q[m],Q[m+1]) for m in range(1,len(Q)-1)]:
     x0,x1=min(p.x for p in P),max(p.x for p in P);y0,y1=min(p.y for p in P),max(p.y for p in P)
     i0=max(0,int((x0-xs[0])//st));i1=min(len(xs)-1,int((x1-xs[0])//st)+1);k0=max(0,int((y0-ys[0])//st));k1=min(len(ys)-1,int((y1-ys[0])//st)+1)
     (ax,ay),(bx,by),(cx,cy)=[(p.x,p.y) for p in P];den=(by-cy)*(ax-cx)+(cx-bx)*(ay-cy)
     if abs(den)<1e-6:continue
     for k in range(k0,k1+1):
      y=ys[k]
      for i in range(i0,i1+1):
       x=xs[i];l1=((by-cy)*(x-cx)+(cx-bx)*(y-cy))/den;l2=((cy-ay)*(x-cx)+(ax-cx)*(y-cy))/den
       if l1>=-1e-6 and l2>=-1e-6 and 1-l1-l2>=-1e-6:low[k][i]=min(low[k][i],zq);cov[k][i]=cov[k][i] if last[k][i]==cid else min(9,cov[k][i]+1);last[k][i]=cid
 assert sha(MAPFILE)==LOAD_SHA
 (EDGE_OUT/'water-cover.json').write_text(json.dumps(dict(status='read_only_probe',map_sha256=LOAD_SHA,grid_map_sha256=G['map_sha256'],surface_z=sorted(zs),x_cm=xs,rows=[dict(y=y,cover=''.join(str(v) for v in r),low=' '.join(str(v) for v in lz)) for y,r,lz in zip(ys,cov,low)])),'utf8')
def motel_world_coast_strip_install():
 # world_coast_strip_install: import world-edge-v1/coast-strip/coast-strip.glb (Tools/build_street_city_coast_strip.lua)
 # as one actor WorldEdge_CoastStrip with the city beach sand (the M_GroundNatural instance on G01-G04 whose base colour
 # is beach sand). It is walkable (complex-as-simple collision: it lies 1 cm over the land top and slopes into the
 # water), casts no shadow, and is placed by the same vertex-matched yaw x mirror search as the edge skirt.
 global PREFIX,KEEP,save
 b=json.loads((EDGE_OUT/'coast-strip/build.json').read_text('utf8'));assert sha(b['path'])==b['sha256']
 PREFIX='/Game/UniversityCity/WorldEdgeV1';KEEP=[];save=save_asset
 src=(ROOT/'Tools/street_city_activity_native_payload.lua').read_text('utf8').split('def import_one(',1)[1].split('def simple_water(',1)[0];ns=dict(globals());exec('def import_one('+src,ns);imp=ns['import_one']
 mesh=imp(Path(b['path']),'world-edge-coast-strip',0,False)
 MEL=unreal.MaterialEditingLibrary;sand=None
 for a in SUB.get_all_level_actors():
  if not re.match(r'^G0[1-4]$',a.get_actor_label()):continue
  for c in a.get_components_by_class(unreal.StaticMeshComponent):
   for k in range(c.get_num_materials()):
    m=c.get_material(k)
    if isinstance(m,unreal.MaterialInstance) and any('beach_sand' in (t.get_path_name() if t else '') for t in [MEL.get_material_instance_texture_parameter_value(m,n) for n in MEL.get_texture_parameter_names(m)]):sand=m
 assert sand,'beach sand instance not found on G01-G04'
 for k in range(len(mesh.get_editor_property('static_materials'))):mesh.set_material(k,sand)
 bs=mesh.get_editor_property('body_setup')
 if bs:bs.set_editor_property('collision_trace_flag',unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
 assert EAL.save_loaded_asset(mesh,False)
 probes=[[v*100 for v in p] for p in b['probes_m']];B0=[v*100 for v in b['bounds_m'][0]];B1=[v*100 for v in b['bounds_m'][1]]
 def near(a,c):return abs(a-c)<=max(200.,abs(c)*.01)
 actors={a.get_actor_label():a for a in SUB.get_all_level_actors()}
 backup=EDGE_OUT/('UniversityCity-before-coast-strip-'+LOAD_SHA[:16]+'.umap')
 if not backup.exists():shutil.copy2(MAPFILE,backup)
 if 'WorldEdge_CoastStrip' in actors:SUB.destroy_actor(actors['WorldEdge_CoastStrip'])
 a=SUB.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(0,0,0),unreal.Rotator(0,0,0));a.set_actor_label('WorldEdge_CoastStrip')
 a.tags=[unreal.Name('WorldEdgeV1')];a.set_folder_path('UniversityCity/WorldEdgeV1')
 c=a.static_mesh_component;c.set_static_mesh(mesh);c.set_editor_property('cast_shadow',False);c.set_editor_property('mobility',unreal.ComponentMobility.STATIC)
 md=mesh.get_static_mesh_description(0);loc=[md.get_vertex_position(unreal.VertexID(i)) for i in range(md.get_vertex_count())];assert loc
 pick=None;tried=[]
 for yaw in (0,90,180,270):
  for sy in (1,-1):
   cs,sn=round(math.cos(math.radians(yaw))),round(math.sin(math.radians(yaw)))
   W=[(cs*v.x-sn*sy*v.y,sn*v.x+cs*sy*v.y,v.z) for v in loc]
   miss=[p for p in probes if min(abs(w[0]-p[0])+abs(w[1]-p[1])+abs(w[2]-p[2]) for w in W)>5]
   tried.append(dict(yaw=yaw,mirror_y=sy,missing=len(miss)))
   if not miss and pick is None:pick=(yaw,sy)
 assert pick,('no orientation puts the strip on its authored vertices',tried)
 a.set_actor_rotation(unreal.Rotator(0,0,pick[0]),False);a.set_actor_scale3d(unreal.Vector(1,pick[1],1))
 c.set_collision_profile_name('BlockAll');c.set_editor_property('can_character_step_up_on',unreal.CanBeCharacterBase.ECB_YES)
 o,e=a.get_actor_bounds(False);got=[o.x-e.x,o.y-e.y,o.x+e.x,o.y+e.y];want=[B0[0],B0[1],B1[0],B1[1]]
 assert all(near(g,w) for g,w in zip(got,want)),('placed bounds differ from the authored world bounds',got,want)
 actors.get('CityEnvironmentController') and actors['CityEnvironmentController'].prepare_for_save()
 assert unreal.EditorLevelLibrary.save_current_level();new=sha(MAPFILE)
 installed=read('motel-install');installed.update(parent_map_sha256=LOAD_SHA,map_sha256=new);write('motel-install',installed)
 (EDGE_OUT/('coast-strip-install-'+time.strftime('%Y%m%d-%H%M%S')+'.json')).write_text(json.dumps(dict(status='saved_requires_capture_review',map_sha256=new,parent_map_sha256=LOAD_SHA,backup=str(backup),asset=mesh.get_path_name(),sand=sand.get_path_name(),orientation_trials=tried,actor_yaw=pick[0],actor_mirror_y=pick[1],placed_bounds=got,triangles=mesh.get_num_triangles(0)),ensure_ascii=False,indent=1),'utf8')
def motel_world_beach_props_install():
 # world_beach_props_install: world-edge-v1/beach-props/placements.json (Tools/build_street_city_beach_props.lua) ->
 # one StaticMeshActor per placement, label Beach_<kind>_<n>, folder UniversityCity/WorldEdgeV1/Beach. Models keep their
 # authored glTF materials. Each is dropped onto the real ground top under it (trace passing water and small actors) and
 # is visual only (NoCollision, like the edge skirt: props must not answer the audit's visibility traces). Re-running
 # replaces the previous set. placements.json may cap a kind (limits{kind:n}): candidates beyond the planned count are
 # spares tried in order when earlier ones do not fit (landmarks on open lawns).
 global PREFIX,KEEP,save
 L=json.loads((EDGE_OUT/'beach-props/placements.json').read_text('utf8'))
 PREFIX='/Game/UniversityCity/WorldEdgeV1';KEEP=[];save=save_asset
 src=(ROOT/'Tools/street_city_activity_native_payload.lua').read_text('utf8').split('def import_one(',1)[1].split('def simple_water(',1)[0];ns=dict(globals());exec('def import_one('+src,ns);imp=ns['import_one']
 meshes={}
 for k,b in L['models'].items():assert sha(b['path'])==b['sha256'],k;meshes[k]=imp(Path(b['path']),'beach-'+k.replace('_','-'),0,True)
 world=unreal.EditorLevelLibrary.get_editor_world();info={}
 for a in SUB.get_all_level_actors():
  try:info[a.get_actor_label()]=a.get_actor_bounds(False)
  except Exception:pass
 def ground(x,y):
  ign=[]
  for _ in range(10):
   r=unreal.SystemLibrary.line_trace_single(world,unreal.Vector(x,y,5000),unreal.Vector(x,y,-5000),unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,ign,unreal.DrawDebugTrace.NONE,True)
   t=r.to_tuple() if r is not None else (False,)
   if not t[0] or t[9] is None:return None
   a=t[9];l=a.get_actor_label();bb=info.get(l)
   if re.match(r'^G\d\d_Water$',l) or l.startswith(('Activity_ACTSPACE','Beach_')) or (bb is not None and max(bb[1].x,bb[1].y)<1500 and l!='WorldEdge_CoastStrip'):ign.append(a);continue
   return t[4].z
  return None
 backup=EDGE_OUT/('UniversityCity-before-beach-props-'+LOAD_SHA[:16]+'.umap')
 if not backup.exists():shutil.copy2(MAPFILE,backup)
 for a in list(SUB.get_all_level_actors()):
  if a.get_actor_label().startswith('Beach_') and 'WorldEdgeV1' in [str(t) for t in a.tags]:SUB.destroy_actor(a)
 placed=[];skipped=[]
 for i,p in enumerate(L['placements']):
  x,y,z=p['cm'];g=ground(x,y)
  if g is None or g<1150:skipped.append(dict(i=i,kind=p['kind'],ground=g));continue
  a=SUB.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(x,y,g),unreal.Rotator(0,0,p['yaw']));a.set_actor_label('Beach_'+p['kind']+'_'+str(i))
  a.tags=[unreal.Name('WorldEdgeV1')];a.set_folder_path('UniversityCity/WorldEdgeV1/Beach')
  c=a.static_mesh_component;c.set_static_mesh(meshes[p['kind']]);c.set_editor_property('mobility',unreal.ComponentMobility.STATIC)
  c.set_collision_profile_name('NoCollision');c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION);c.set_editor_property('generate_overlap_events',False)
  placed.append(dict(label=a.get_actor_label(),kind=p['kind'],z=g))
 assert placed,'nothing placed'
 actors={a.get_actor_label():a for a in SUB.get_all_level_actors()}
 actors.get('CityEnvironmentController') and actors['CityEnvironmentController'].prepare_for_save()
 assert unreal.EditorLevelLibrary.save_current_level();new=sha(MAPFILE)
 installed=read('motel-install');installed.update(parent_map_sha256=LOAD_SHA,map_sha256=new);write('motel-install',installed)
 (EDGE_OUT/('beach-props-install-'+time.strftime('%Y%m%d-%H%M%S')+'.json')).write_text(json.dumps(dict(status='saved_requires_capture_review',map_sha256=new,parent_map_sha256=LOAD_SHA,backup=str(backup),placed=len(placed),skipped=skipped,meshes={k:m.get_path_name() for k,m in meshes.items()}),ensure_ascii=False,indent=1),'utf8')
def motel_world_anchor_probe():
 # world_anchor_probe (read-only): world transforms of the installed street dressing that the park props are placed
 # against (Tools/place_street_city_park_props.lua): every instance (ISM/HISM) or plain component whose static mesh is
 # one of the streetscape-v1 assets below. Output world-blender-v1/anchors.json as {id: [[x,y,z,yaw], ...]} in cm/deg.
 ids=['bench_oak','bench_stone','lamp_lantern','lamp_modern','cycle_rack','recycling','bus_shelter','tactile','curb','paving','planter_long','planter_square','tree_grate','utility_box']
 out={k:[] for k in ids};seen=set()
 def row(t):
  l=t.translation;return [round(l.x,1),round(l.y,1),round(l.z,1),round(t.rotation.rotator().yaw,1)]
 for a in SUB.get_all_level_actors():
  for c in a.get_components_by_class(unreal.StaticMeshComponent):
   m=c.get_editor_property('static_mesh')
   if m is None:continue
   k=next((i for i in ids if m.get_name().startswith(i)),None)
   if k is None or c.get_path_name() in seen:continue
   seen.add(c.get_path_name())
   if isinstance(c,unreal.InstancedStaticMeshComponent):
    for i in range(c.get_instance_count()):
     t=c.get_instance_transform(i,True);t=t[-1] if isinstance(t,tuple) else t
     if t is not None:out[k].append(row(t))
   else:out[k].append(row(c.get_world_transform()))
 assert sha(MAPFILE)==LOAD_SHA
 D=EDGE_OUT.parent/'world-blender-v1';D.mkdir(parents=True,exist_ok=True)
 (D/'anchors.json').write_text(json.dumps(dict(status='read_only_probe',map_sha256=LOAD_SHA,counts={k:len(v) for k,v in out.items()},anchors=out),ensure_ascii=False),'utf8')
def motel_world_park_props_install():_blender_props_install('park-props','Park_','Park','park')
def motel_world_landmarks_install():_blender_props_install('landmarks','Landmark_','Landmark','landmark')
def _blender_props_install(SET,LABEL,FOLDER,TAG):
 # world_park_props_install / world_landmarks_install: world-blender-v1/<set>/placements.json (Tools/place_street_city_park_props.lua) ->
 # one StaticMeshActor per placement, label Park_/Landmark_<kind>_<n>, folder UniversityCity/WorldBlenderV1/Park|Landmark, visual only
 # (NoCollision, like the beach props). Placements give the Blender-frame yaw; each imported mesh's own axis mapping is
 # found by matching the build's asymmetric probe vertices (the coast strip rule), so a sign never faces away from its
 # path. A placement is skipped when the ground under its footprint is missing, water or uneven (>15 cm over the four
 # corners and centre), or when any actor overlaps the box from 20 cm above the ground to the model's top. Re-running
 # replaces the previous set. placements.json may cap a kind (limits{kind:n}): candidates beyond the planned count are
 # spares tried in order when earlier ones do not fit (landmarks on open lawns).
 global PREFIX,KEEP,save
 D=EDGE_OUT.parent/'world-blender-v1';B=json.loads((D/SET/'build.json').read_text('utf8'));L=json.loads((D/SET/'placements.json').read_text('utf8'))
 assert L['anchors_map_sha256']==LOAD_SHA,'placements were computed on another map; rerun world_anchor_probe and the placement'
 PREFIX='/Game/UniversityCity/WorldBlenderV1';KEEP=[];save=save_asset
 src=(ROOT/'Tools/street_city_activity_native_payload.lua').read_text('utf8').split('def import_one(',1)[1].split('def simple_water(',1)[0];ns=dict(globals());exec('def import_one('+src,ns);imp=ns['import_one']
 meshes={};calib={};kinds={p['kind'] for p in L['placements']}
 for k in sorted(kinds):
  b=B['models'][k];assert sha(b['path'])==b['sha256'],k;m=imp(Path(b['path']),TAG+'-'+k.replace('_','-'),0,True);meshes[k]=m
  md=m.get_static_mesh_description(0);loc=[md.get_vertex_position(unreal.VertexID(i)) for i in range(md.get_vertex_count())]
  probes=[[v*100 for v in p] for p in b['probes_m']];pick=None
  for yaw in (0,90,180,270):
   for sy in (1,-1):
    cs,sn=round(math.cos(math.radians(yaw))),round(math.sin(math.radians(yaw)))
    W=[(cs*v.x-sn*sy*v.y,sn*v.x+cs*sy*v.y,v.z) for v in loc]
    if pick is None and all(min(abs(w[0]-p[0])+abs(w[1]-p[1])+abs(w[2]-p[2]) for w in W)<=2 for p in probes):pick=(yaw,sy)
  assert pick,('no axis mapping puts the probes on the imported vertices',k);calib[k]=pick
 world=unreal.EditorLevelLibrary.get_editor_world();info={}
 for a in SUB.get_all_level_actors():
  try:info[a.get_actor_label()]=a.get_actor_bounds(False)
  except Exception:pass
 # every surface under a point, top down (canopies, tree trunk colliders, roofs, then the ground); the first water hit
 # ends the list. The first install traced only the top surface and skipped 30 placements under a 5.16 m canopy.
 def surfaces(x,y):
  ign=[];out=[]
  for _ in range(10):
   r=unreal.SystemLibrary.line_trace_single(world,unreal.Vector(x,y,8000),unreal.Vector(x,y,-5000),unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,ign,unreal.DrawDebugTrace.NONE,True)
   t=r.to_tuple() if r is not None else (False,)
   if not t[0] or t[9] is None:break
   a=t[9];l=a.get_actor_label()
   if re.match(r'^G\d\d_Water$',l) or l.startswith('Activity_ACTSPACE'):out.append(None);break
   ign.append(a)
   if not l.startswith(('Beach_','Park_','Landmark_')):out.append(t[4].z)
  return out
 # the footprint stands on the lowest level every point reaches within 15 cm; whatever is above it is left to the
 # box overlap check (a canopy over a low table is fine, over a 5 m pole it is not)
 def footing(pts):
  S=[surfaces(a,b) for a,b in pts]
  if any(not s or s[0] is None for s in S):return None,'no_ground_or_water'
  for cz in sorted(z for z in S[0] if z is not None):
   zs=[min((z for z in s if z is not None),key=lambda z:abs(z-cz),default=None) for s in S]
   if all(z is not None and abs(z-cz)<=15 for z in zs):return zs,None
  return None,'uneven'
 backup=D/('UniversityCity-before-'+SET+'-'+LOAD_SHA[:16]+'.umap')
 if not backup.exists():shutil.copy2(MAPFILE,backup)
 for a in list(SUB.get_all_level_actors()):
  if a.get_actor_label().startswith(LABEL) and 'WorldBlenderV1' in [str(t) for t in a.tags]:SUB.destroy_actor(a)
 OT=[unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY1,unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY2]
 placed=[];skipped=[];spots=[]
 # a planned spot that collides with existing street furniture (merged lamp / planter / recycling cells) or lands on
 # uneven ground is moved to the nearest clear spot within 3 m in the prop's own frame instead of being dropped (31 of
 # 41 skips on 10-31 were a lamp or planter 0.5-1.5 m away); props already placed here count as obstacles too
 NUDGE=[(0,0)]+sorted({(du,dv) for du in range(-300,301,75) for dv in range(-300,301,75) if (du,dv)!=(0,0) and du*du+dv*dv<=300*300},key=lambda d:d[0]*d[0]+d[1]*d[1])
 def clear_at(x,y,sz,th,kind):
  hx,hy=sz[0]*50,sz[1]*50
  pts=[(x,y)]+[(x+u*math.cos(th)-v*math.sin(th),y+u*math.sin(th)+v*math.cos(th)) for u,v in ((-hx,-hy),(hx,-hy),(hx,hy),(-hx,hy))]
  zs,why=footing(pts)
  if why:return None,dict(why=why)
  r=max(hx,hy)+20;top=sz[2]*100
  if any(math.hypot(x-sx,y-sy)<r+sr for sx,sy,sr in spots):return None,dict(why='overlap',actors=['Park_ (placed here)'])
  hit=unreal.SystemLibrary.box_overlap_actors(world,unreal.Vector(x,y,max(zs)+20+top/2),unreal.Vector(r,r,top/2),OT,None,[])
  hit=hit[-1] if isinstance(hit,tuple) else hit
  # volumes (post process, audio, nav) are not obstacles
  # a bike shelter is meant to stand over its cycle racks and parked bicycles
  hit=[h for h in (hit or []) if not h.get_actor_label().startswith('Beach_') and 'Volume' not in h.get_class().get_name() and not (kind=='bike_shelter' and re.search('cycle_rack|bicycle',h.get_actor_label()))]
  if hit:return None,dict(why='overlap',actors=sorted({h.get_actor_label() for h in hit})[:4])
  return zs,None
 lim=L.get('limits',{});cnt={}
 for i,p in enumerate(L['placements']):
  if cnt.get(p['kind'],0)>=lim.get(p['kind'],10**9):continue
  x0,y0=p['cm'];sz=B['models'][p['kind']]['size_m'];th=math.radians(p['yaw']);first=None
  for du,dv in NUDGE:
   x=x0+du*math.cos(th)-dv*math.sin(th);y=y0+du*math.sin(th)+dv*math.cos(th)
   zs,bad=clear_at(x,y,sz,th,p['kind'])
   if zs:break
   first=first or bad
  if not zs:skipped.append(dict(i=i,kind=p['kind'],**first));continue
  cnt[p['kind']]=cnt.get(p['kind'],0)+1;g=zs[0];spots.append((x,y,max(sz[0],sz[1])*50+20))
  yaw0,sy=calib[p['kind']]
  a=SUB.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(x,y,g),unreal.Rotator(0,0,p['yaw']+yaw0));a.set_actor_scale3d(unreal.Vector(1,sy,1));a.set_actor_label(LABEL+p['kind']+'_'+str(i))
  a.tags=[unreal.Name('WorldBlenderV1')];a.set_folder_path('UniversityCity/WorldBlenderV1/'+FOLDER)
  c=a.static_mesh_component;c.set_static_mesh(meshes[p['kind']]);c.set_editor_property('mobility',unreal.ComponentMobility.STATIC)
  c.set_collision_profile_name('NoCollision');c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION);c.set_editor_property('generate_overlap_events',False)
  placed.append(dict(label=a.get_actor_label(),kind=p['kind'],z=round(g,1),moved_cm=round(math.hypot(x-x0,y-y0))))
 assert placed,'nothing placed'
 actors={a.get_actor_label():a for a in SUB.get_all_level_actors()}
 actors.get('CityEnvironmentController') and actors['CityEnvironmentController'].prepare_for_save()
 assert unreal.EditorLevelLibrary.save_current_level();new=sha(MAPFILE)
 installed=read('motel-install');installed.update(parent_map_sha256=LOAD_SHA,map_sha256=new);write('motel-install',installed)
 by={}
 for q in placed:by[q['kind']]=by.get(q['kind'],0)+1
 (D/(SET+'-install-'+time.strftime('%Y%m%d-%H%M%S')+'.json')).write_text(json.dumps(dict(status='saved_requires_capture_review',map_sha256=new,parent_map_sha256=LOAD_SHA,backup=str(backup),placed=len(placed),placed_by_kind=by,skipped=skipped,calibration=calib,meshes={k:m.get_path_name() for k,m in meshes.items()},labels=[q['label'] for q in placed]),ensure_ascii=False,indent=1),'utf8')
def motel_world_grass_probe():
 # world_grass_probe (read-only): every material instance under GroundPBRV2/M_GroundNatural used by the city ground
 # chunks (lawn slots: the A13 / G086 / G148 tint patchwork): scalar / vector / texture parameter values, plus the
 # master's parameter list, so the grass pass can unify tint and add world-space macro variation.
 P=json.loads((EDGE_OUT/'probe.json').read_text('utf8'))
 paths=sorted({s['chain'][0] for g in P['ground'] if re.match(r'^G\d\d$',g['label']) for s in g['slots'] if len(s['chain'])>1 and s['chain'][-1].endswith('M_GroundNatural')})
 MEL=unreal.MaterialEditingLibrary;rows=[]
 for p in paths:
  mi=EAL.load_asset(p.split('.')[0]);r=dict(path=p,scalars={},vectors={},textures={})
  for n in MEL.get_scalar_parameter_names(mi):r['scalars'][str(n)]=MEL.get_material_instance_scalar_parameter_value(mi,n)
  for n in MEL.get_vector_parameter_names(mi):c=MEL.get_material_instance_vector_parameter_value(mi,n);r['vectors'][str(n)]=[c.r,c.g,c.b,c.a]
  for n in MEL.get_texture_parameter_names(mi):tx=MEL.get_material_instance_texture_parameter_value(mi,n);r['textures'][str(n)]=tx.get_path_name() if tx else None
  rows.append(r)
 assert sha(MAPFILE)==LOAD_SHA
 (EDGE_OUT/'grass-probe.json').write_text(json.dumps(dict(status='read_only_probe',map_sha256=LOAD_SHA,instances=rows),ensure_ascii=False,indent=1),'utf8')
def motel_world_shore_probe():
 # world_shore_probe (read-only): the west sea shore (A03: the sand ends in square steps into the ocean plane). Every
 # 10 m along y over the city ground union, march from 200 m inland out to 50 m past the union edge at 1 m steps with
 # downward traces (small actors passed through) and record where the last land hit is before the first water hit (ocean
 # plane / water material), the land z, label and material there, and the water z.
 P=json.loads((EDGE_OUT/'probe.json').read_text('utf8'));x0,y0,x1,y1=P['city_ground_union']
 world=unreal.EditorLevelLibrary.get_editor_world();info={}
 for a in SUB.get_all_level_actors():
  try:info[a.get_actor_label()]=a.get_actor_bounds(False)
  except Exception:pass
 def down(x,y):
  ign=[]
  for _ in range(8):
   r=unreal.SystemLibrary.line_trace_single(world,unreal.Vector(x,y,20000),unreal.Vector(x,y,-5000),unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,ign,unreal.DrawDebugTrace.NONE,True)
   t=r.to_tuple() if r is not None else (False,)
   if not t[0] or t[9] is None:return None
   lab=t[9].get_actor_label();b=info.get(lab)
   if b is not None and max(b[1].x,b[1].y)<1500:ign.append(t[9]);continue
   mat=''
   try:
    m=t[10].get_material(t[11]) if t[10] is not None else None
    mat=m.get_path_name() if m else ''
   except Exception:
    try:m=t[10].get_material(0);mat=m.get_path_name() if m else ''
    except Exception:pass
   return dict(z=round(t[4].z,1),label=lab,mat=mat)
  return None
 def wet(h):return h is None or 'ocean' in h['mat'].lower() or 'water' in h['mat'].lower() or h['label'].startswith('Activity_ACTSPACE')
 rows=[];y=y0+500
 while y<y1:
  last=None;first_water=None;x=x0+20000
  while x>x0-5000:
   h=down(x,y)
   if wet(h):first_water=dict(x=round(x),hit=h);break
   last=dict(x=round(x),hit=h);x-=100
  # land profile over the first 40 m from the union's west edge (where the sand terraces meet the water level)
  prof=[]
  for k in range(41):
   h=down(x0+k*100,y);prof.append(dict(x=round(x0+k*100),z=h['z'] if h else None,label=h['label'] if h else None,mat=(h['mat'].split('/')[-1] if h else None)))
  rows.append(dict(y=round(y),land=last,water=first_water,profile=prof));y+=1000
 assert sha(MAPFILE)==LOAD_SHA
 (EDGE_OUT/'shore-probe.json').write_text(json.dumps(dict(status='read_only_probe',map_sha256=LOAD_SHA,step_cm=100,rows=rows),ensure_ascii=False,indent=1),'utf8')
def motel_world_ocean_probe():
 # world_ocean_probe (read-only): the shore probe found the ocean surface (M_ocean, z 1088) 88 cm above the flat west
 # ground (z 1000) with a straight cut at the union's west edge. Record every actor using M_ocean / a water material:
 # label, class, transform, bounds, and each primitive component's mesh, bounds and materials, so the shore pass can
 # decide between lowering the water and adding a submerged sand slope.
 rows=[]
 for a in SUB.get_all_level_actors():
  comps=[]
  for c in a.get_components_by_class(unreal.PrimitiveComponent):
   mats=[]
   try:
    for k in range(c.get_num_materials()):m=c.get_material(k);mats.append(m.get_path_name() if m else '')
   except Exception:pass
   mesh=''
   try:mesh=c.get_editor_property('static_mesh').get_path_name()
   except Exception:pass
   o,e=c.get_world_location(),None
   try:b=c.bounds;e=[b.origin.x-b.box_extent.x,b.origin.y-b.box_extent.y,b.origin.z-b.box_extent.z,b.origin.x+b.box_extent.x,b.origin.y+b.box_extent.y,b.origin.z+b.box_extent.z]
   except Exception:pass
   comps.append(dict(name=c.get_name(),cls=c.get_class().get_name(),mesh=mesh,mats=mats,loc=[o.x,o.y,o.z],bounds=e,collision=str(c.get_collision_enabled())))
  if not any('ocean' in m.lower() or 'water' in m.lower() for cc in comps for m in cc['mats']):continue
  t=a.get_actor_transform();o,e=a.get_actor_bounds(False)
  rows.append(dict(label=a.get_actor_label(),cls=a.get_class().get_name(),loc=[t.translation.x,t.translation.y,t.translation.z],rot=[t.rotation.rotator().roll,t.rotation.rotator().pitch,t.rotation.rotator().yaw],scale=[t.scale3d.x,t.scale3d.y,t.scale3d.z],bounds=[o.x-e.x,o.y-e.y,o.z-e.z,o.x+e.x,o.y+e.y,o.z+e.z],components=comps))
 assert sha(MAPFILE)==LOAD_SHA
 (EDGE_OUT/'ocean-probe.json').write_text(json.dumps(dict(status='read_only_probe',map_sha256=LOAD_SHA,actors=rows),ensure_ascii=False,indent=1),'utf8')
def motel_world_seabed_layers_probe():
 # world_seabed_layers_probe (read-only): the A03 sea (x 80-135 m, y 700-830 m) still shows light and dark rectangles
 # after the sand sheet was raised 1 -> 15 cm (captures 2026-11-01 01:37), so they are not depth fighting. Every 1 m in
 # x and 2 m in y, trace down from 50 m and pass through each hit actor (no size filter: shore_grid_probe skipped actors
 # under 15 m extent, which can include ground tiles), recording up to 8 layers: label, z, material leaf.
 # Output world-edge-v1/seabed-layers.json (rows along y; cells = list of [label, z, material]).
 world=unreal.EditorLevelLibrary.get_editor_world()
 def layers(x,y):
  ign=[];out=[]
  for _ in range(8):
   r=unreal.SystemLibrary.line_trace_single(world,unreal.Vector(x,y,5000),unreal.Vector(x,y,-5000),unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,ign,unreal.DrawDebugTrace.NONE,True)
   t=r.to_tuple() if r is not None else (False,)
   if not t[0] or t[9] is None:break
   mat=''
   try:
    m=t[10].get_material(t[11]) if t[10] is not None else None
    mat=m.get_path_name().split('/')[-1] if m else ''
   except Exception:pass
   out.append([t[9].get_actor_label(),round(t[4].z,1),mat]);ign.append(t[9])
  return out
 xs=list(range(8000,13600,100));rows=[]
 for y in range(70000,83001,200):rows.append(dict(y=y,cells=[layers(x,y) for x in xs]))
 assert sha(MAPFILE)==LOAD_SHA
 (EDGE_OUT/'seabed-layers.json').write_text(json.dumps(dict(status='read_only_probe',map_sha256=LOAD_SHA,x_cm=xs,rows=rows)),'utf8')
]====]
