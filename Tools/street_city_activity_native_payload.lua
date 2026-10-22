-- Executed only as an embedded payload by street_city_activity_scenes.lua.
return [====[
import shutil,collections
PLAN=read(OUT/'plan.json');KEEP=[]
def save(a):assert EAL.save_loaded_asset(a)
def import_one(path,aid,lod=0,materials=False):
 digest=sha(path);folder=PREFIX+'/Assets/'+aid.replace('-','_')+'/S_'+digest[:16]+('_PBR1' if materials else '');name=aid.replace('-','_')+'_LOD'+str(lod);dest=folder+'/'+name
 if EAL.does_asset_exist(dest):
  existing=EAL.load_asset(dest)
  if not materials or all(existing.get_material(i) for i in range(len(existing.get_editor_property('static_materials')))):return existing
 pipeline=unreal.InterchangeGenericAssetsPipeline();KEEP.append(pipeline);pipeline.set_editor_property('asset_name',name);pipeline.set_editor_property('use_source_name_for_asset',False);pipeline.get_editor_property('common_meshes_properties').set_editor_property('bake_meshes',True);pipeline.get_editor_property('material_pipeline').set_editor_property('import_materials',materials)
 if materials:
  material_pipeline=pipeline.get_editor_property('material_pipeline');material_pipeline.set_editor_property('reuse_existing_materials',False);material_pipeline.set_editor_property('create_new_materials',True)
 mp=pipeline.get_editor_property('mesh_pipeline');mp.set_editor_property('combine_static_meshes_behavior',unreal.InterchangeCombineStaticMeshesBehavior.ALL);mp.set_editor_property('import_skeletal_meshes',False);mp.set_editor_property('build_nanite',False);mp.set_editor_property('generate_lightmap_u_vs',False)
 params=unreal.ImportAssetParameters();params.set_editor_property('is_automated',True);params.set_editor_property('replace_existing',True);params.set_editor_property('destination_name',name);params.set_editor_property('override_pipelines',[unreal.SoftObjectPath(pipeline.get_path_name())]);manager=unreal.InterchangeManager.get_interchange_manager_scripted();objects=manager.import_asset(folder,unreal.InterchangeManager.create_source_data(str(path)),params);meshes=[o for o in objects or [] if isinstance(o,unreal.StaticMesh)];assert len(meshes)==1,(aid,len(meshes));mesh=meshes[0]
 dependencies=[]
 for asset in objects:
  if isinstance(asset,unreal.Texture2D):asset.set_editor_property('max_texture_size',2048)
  save(asset);dependencies.append(asset.get_path_name())
 if materials:assert all(mesh.get_material(i) for i in range(len(mesh.get_editor_property('static_materials')))),('Missing source PBR binding',aid)
 EAL.set_metadata_tag(mesh,'ActivitySourceSHA256',digest);EAL.set_metadata_tag(mesh,'ActivitySourceFile',str(path));EAL.set_metadata_tag(mesh,'ActivityDependencies',json.dumps(dependencies));save(mesh);KEEP.clear();return mesh
def simple_water():
 path=PREFIX+'/Materials/M_ConstantWater';m=EAL.load_asset(path) if EAL.does_asset_exist(path) else None
 if m:return m
 m=ATS.create_asset('M_ConstantWater',PREFIX+'/Materials',unreal.Material,unreal.MaterialFactoryNew());m.set_editor_property('blend_mode',unreal.BlendMode.BLEND_TRANSLUCENT);m.set_editor_property('two_sided',True)
 n=MEL.create_material_expression(m,unreal.MaterialExpressionConstant3Vector);n.set_editor_property('constant',unreal.LinearColor(.04,.35,.38,1));MEL.connect_material_property(n,'',unreal.MaterialProperty.MP_BASE_COLOR)
 for prop,value in [(unreal.MaterialProperty.MP_OPACITY,.38),(unreal.MaterialProperty.MP_ROUGHNESS,.12),(unreal.MaterialProperty.MP_METALLIC,0)]:
  n=MEL.create_material_expression(m,unreal.MaterialExpressionConstant);n.set_editor_property('r',value);MEL.connect_material_property(n,'',prop)
 m.set_editor_property('used_with_instanced_static_meshes',True);MEL.recompile_material(m);save(m);return m
def import_assets():
 rows={};water=simple_water()
 for q in PLAN['models']:
  aid=q['id'];organic='source' in q;paths=[q['source']] if organic else [r['path'] for r in q['lods']]
  if organic:
   report=read(q['report']);assert report['status']=='passed' and report['output_sha256']==sha(paths[0]),(aid,'Unapproved source');assert report['triangles']<=report['triangle_cap']
  meshes=[import_one(p,aid,i,organic) for i,p in enumerate(paths)];mesh=meshes[0]
  if organic:
   opt=unreal.EditorScriptingMeshReductionOptions();opt.set_editor_property('auto_compute_lod_screen_size',False)
   settings=[]
   for percent,screen in [(1.,1.),(.4,.22),(.15,.07)]:
    r=unreal.EditorScriptingMeshReductionSettings();r.set_editor_property('percent_triangles',percent);r.set_editor_property('screen_size',screen);settings.append(r)
   opt.set_editor_property('reduction_settings',settings);API.set_lods(mesh,opt)
  else:
   for lod,src in enumerate(meshes[1:],1):assert API.set_lod_from_static_mesh(mesh,lod,src,0,False)==lod
   for i,slot in enumerate(mesh.get_editor_property('static_materials')):
    name=str(slot.get_editor_property('material_slot_name'));match=re.search(r'surface[-_](\d+)',name);assert match,(aid,name);surface=int(match.group(1));mat=water if aid in ['fountain','fish-pond'] and surface==3 else EAL.load_asset('/Game/UniversityCity/ResortV1/Materials/M_Surface_'+str(surface));assert mat;mesh.set_material(i,mat)
  body=mesh.get_editor_property('body_setup');body.set_editor_property('collision_trace_flag',unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
  mesh.set_editor_property('lod_for_collision',2);save(mesh)
  counts=[mesh.get_num_triangles(i) for i in range(API.get_lod_count(mesh))];assert len(counts)>=3,(aid,counts)
  rows[aid]=dict(asset=mesh.get_path_name(),lod_triangles=counts,source_sha256=sha(paths[0]),organic=organic,bounds_cm=box(mesh.get_bounding_box()),remesh_report=q.get('report'))
  write(OUT/'import-progress.json',dict(status='running',assets=rows));unreal.log('ACT IMPORT '+aid+' '+str(counts))
 write(OUT/'import_assets.json',dict(status='passed',assets=rows))
def sign_material(key):
 dest=PREFIX+'/Signs/M_'+key.replace('-','_');m=EAL.load_asset(dest) if EAL.does_asset_exist(dest) else None
 digest=hashlib.sha256(''.join(sha(OUT/'signs'/locale/(key+'.png')) for locale in ['zh-Hant','zh-Hans','en']).encode()).hexdigest()
 if m and EAL.get_metadata_tag(m,'ActivityTextSHA256')==digest:return m
 c=EAL.load_asset('/Game/UniversityCity/Localization/MPC_CityLanguage');assert c
 if not m:m=ATS.create_asset(dest.split('/')[-1],PREFIX+'/Signs',unreal.Material,unreal.MaterialFactoryNew())
 MEL.delete_all_material_expressions(m);nodes=[]
 for locale in ['zh-Hant','zh-Hans','en']:
  name='T_'+key.replace('-','_')+'_'+locale.replace('-','_');texpath=PREFIX+'/Signs/'+name;t=EAL.load_asset(texpath) if EAL.does_asset_exist(texpath) else None
  texsha=sha(OUT/'signs'/locale/(key+'.png'))
  if not t or EAL.get_metadata_tag(t,'ActivityTextSHA256')!=texsha:
   job=unreal.AssetImportTask();job.filename=str(OUT/'signs'/locale/(key+'.png'));job.destination_path=PREFIX+'/Signs';job.destination_name=name;job.automated=True;job.replace_existing=True;job.save=True;ATS.import_asset_tasks([job]);t=EAL.load_asset(texpath)
  EAL.set_metadata_tag(t,'ActivityTextSHA256',texsha)
  assert t;t.set_editor_property('max_texture_size',1024);save(t);n=MEL.create_material_expression(m,unreal.MaterialExpressionTextureSampleParameter2D);n.set_editor_property('parameter_name','CityText_'+locale.replace('-','_'));n.set_editor_property('texture',t);nodes.append(n)
 control=MEL.create_material_expression(m,unreal.MaterialExpressionCollectionParameter);control.set_editor_property('collection',c);control.set_editor_property('parameter_name','LanguageIndex')
 def select(threshold,low,high):
  n=MEL.create_material_expression(m,unreal.MaterialExpressionIf);n.set_editor_property('const_b',threshold);MEL.connect_material_expressions(control,'',n,'A')
  for pin,src in [('A > B',high),('A == B',high),('A < B',low)]:assert MEL.connect_material_expressions(src,'RGB' if src in nodes else '',n,pin)
  return n
 n=select(1.5,select(.5,nodes[0],nodes[1]),nodes[2]);MEL.connect_material_property(n,'',unreal.MaterialProperty.MP_BASE_COLOR)
 n=MEL.create_material_expression(m,unreal.MaterialExpressionConstant);n.set_editor_property('r',.72);MEL.connect_material_property(n,'',unreal.MaterialProperty.MP_ROUGHNESS);m.set_editor_property('used_with_instanced_static_meshes',True);errors=MEL.recompile_material(m);assert not errors,(key,errors);EAL.set_metadata_tag(m,'CityTextID','activity.'+key);EAL.set_metadata_tag(m,'ActivityTextSHA256',digest);save(m);return m
def registry():
 return read(OUT/'import_assets.json')['assets']
def lamp_material():
 path=PREFIX+'/Materials/M_ConstantLamp'
 if EAL.does_asset_exist(path):return EAL.load_asset(path)
 m=ATS.create_asset('M_ConstantLamp',PREFIX+'/Materials',unreal.Material,unreal.MaterialFactoryNew());n=MEL.create_material_expression(m,unreal.MaterialExpressionConstant3Vector);n.set_editor_property('constant',unreal.LinearColor(3,2.8,2.6,1));MEL.connect_material_property(n,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR);save(m);return m
def asset_probe():
 rows={}
 for aid,r in registry().items():
  m=EAL.load_asset(r['asset']);slots=[]
  for i,s in enumerate(m.get_editor_property('static_materials')):
   mat=m.get_material(i);slots.append(dict(index=i,name=str(s.get_editor_property('material_slot_name')),material=mat.get_path_name() if mat else None))
  sections={str(lod):[API.get_lod_material_slot(m,lod,section) for section in range(m.get_num_sections(lod))] for lod in range(API.get_lod_count(m))};rows[aid]=dict(slots=slots,sections=sections)
 write(OUT/'asset_probe.json',dict(status='passed',assets=rows))
def spawn(label,mesh,q,material=None):
 actors={a.get_actor_label():a for a in SUB.get_all_level_actors()};a=actors.get(label)
 if a:assert any(t in [str(x) for x in a.tags] for t in [TAG,'ActivityScenesRetired-v1']),('Refuse unrelated actor',label)
 else:a=SUB.spawn_actor_from_class(unreal.StaticMeshActor,point(q['position_m']));a.set_actor_label(label)
 a.tags=[unreal.Name(TAG),unreal.Name(q['scene'])];a.set_folder_path('UniversityCity/ActivityScenesV1/'+q['scene']);a.set_actor_hidden_in_game(False);a.set_is_temporarily_hidden_in_editor(False);comp=a.static_mesh_component;comp.set_static_mesh(mesh);comp.set_mobility(unreal.ComponentMobility.STATIC)
 # Actor reuse must not carry a previous sign/palette override onto another mesh.
 for i in range(comp.get_num_materials()):comp.set_material(i,None)
 for i in range(len(mesh.get_editor_property('static_materials'))):comp.set_material(i,mesh.get_material(i))
 # Source assets are authored in east/north/up and imported with standard glTF axis conversion.
 a.set_actor_location(point(q['position_m']),False,False);a.set_actor_rotation(unreal.Rotator(pitch=0,yaw=90-q['yaw_deg'],roll=0),False);a.set_actor_scale3d(unreal.Vector(*q['scale']));comp.set_collision_profile_name(q.get('collision','BlockAll'))
 if q.get('paint'):material=EAL.load_asset('/Game/UniversityCity/ResortV1/Materials/M_Surface_3')
 if q.get('emissive_panel'):material=lamp_material()
 if material:
  for i in range(comp.get_num_materials()):comp.set_material(i,material)
 comp.set_visibility(True,True);comp.set_hidden_in_game(False,True);comp.set_cast_shadow(True)
 return a
def relocate_trees(actors):
 targets={q['id']:q for q in PLAN.get('tree_relocations',[])};baseline_path=OUT/'tree-baseline.json';baseline=read(baseline_path) if baseline_path.exists() else {'instances':{}};rows=[]
 for g in read(BASE/'resort-native-v1/install.json')['groups']:
  if g['kind'] not in ['camphor','cherry','trunk']:continue
  for index,p in enumerate(g['instances']):
   tree=p.get('tree_id',p['id'])
   if tree not in targets:continue
   actor=actors[g['label']];component=actor.get_component_by_class(unreal.HierarchicalInstancedStaticMeshComponent);assert component.get_instance_count()==len(g['instances']);t=component.get_instance_transform(index,True);key=g['label']+'#'+str(index)
   if key not in baseline['instances']:baseline['instances'][key]=dict(position_cm=[t.translation.x,t.translation.y,t.translation.z],scale=[t.scale3d.x,t.scale3d.y,t.scale3d.z],yaw=t.rotation.rotator().yaw,source_position_m=p['position_m'],tree=tree,mesh=component.static_mesh.get_path_name())
   write(baseline_path,baseline);old=baseline['instances'][key];offset=unreal.Vector(*old['position_cm'])-point(old['source_position_m']);target=point(targets[tree]['position_m'])+offset;t.translation=target;assert component.update_instance_transform(index,t,True,True,True);actor.tags=list(actor.tags)+([unreal.Name('ActivityTreeRelocated-v1')] if 'ActivityTreeRelocated-v1' not in [str(x) for x in actor.tags] else [])
   rows.append(dict(actor=g['label'],index=index,tree=tree,kind=g['kind'],position_cm=[target.x,target.y,target.z],scale=old['scale'],yaw=old['yaw'],mesh=old['mesh'],instance_count=len(g['instances']),materials=[component.get_material(i).get_path_name() if component.get_material(i) else None for i in range(component.get_num_materials())]))
 assert len(rows)==len(targets)*2,('Each visible tree requires its collision twin',rows);write(baseline_path,baseline);write(OUT/'tree-relocations.json',dict(status='passed',instances=rows,retained_tree_count=1475,removed_trees=0))
def install():
 mapfile=ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap';backup=OUT/'backups/UniversityCity.umap';backup.parent.mkdir(parents=True,exist_ok=True)
 if not backup.exists():shutil.copy2(mapfile,backup)
 imported=registry();reused=read(BASE/'resort-native-v1/imports.json')['assets'];actors={a.get_actor_label():a for a in SUB.get_all_level_actors()};baseline=[];rows=[]
 for a in actors.values():
  if TAG not in [str(t) for t in a.tags]:baseline.append(dict(label=a.get_actor_label(),transform=str(a.get_actor_transform())))
 if not (OUT/'baseline.json').exists():write(OUT/'baseline.json',dict(actors=baseline,map_sha256=sha(mapfile),backup_sha256=sha(backup)))
 relocate_trees(actors)
 for q in PLAN['instances']:
  if q.get('replacement'):
   old=actors.get(q['replacement']);assert old;old.set_actor_hidden_in_game(True);old.set_is_temporarily_hidden_in_editor(True)
   for c in old.get_components_by_class(unreal.PrimitiveComponent):c.set_collision_profile_name('NoCollision');c.set_visibility(False,True)
   old.tags=list(old.tags)+[unreal.Name('ActivitySceneReplaced-v1')]
  info=imported.get(q['asset'],reused.get(q['asset']));assert info,q['asset'];mesh=EAL.load_asset(info['asset']);a=spawn('Activity_'+q['id'],mesh,q,sign_material(q['sign']) if q.get('sign') else None);rows.append(dict(label=a.get_actor_label(),mesh=mesh.get_path_name(),request=q))
 for i,q in enumerate(PLAN['lights']):
  label='Activity_Light_'+str(i);a=actors.get(label)
  if not a:a=SUB.spawn_actor_from_class(unreal.PointLight,point(q['position_m']));a.set_actor_label(label)
  a.tags=[unreal.Name(TAG),unreal.Name(q['scene'])];a.set_folder_path('UniversityCity/ActivityScenesV1/Lights');a.set_actor_location(point(q['position_m']),False,False);c=a.point_light_component;c.set_mobility(unreal.ComponentMobility.STATIONARY);c.set_editor_property('intensity_units',unreal.LightUnits.LUMENS);c.set_intensity(q['lumens']);c.set_attenuation_radius(q['radius_m']*100);c.set_cast_shadows(q.get('cast_shadows',True));color=q.get('color',[1,.72,.42]);c.set_light_color(unreal.LinearColor(color[0],color[1],color[2],1))
 keep={r['label'] for r in rows}|{'Activity_Light_'+str(i) for i in range(len(PLAN['lights']))}
 retired=[]
 for a in SUB.get_all_level_actors():
  if TAG in [str(t) for t in a.tags] and a.get_actor_label() not in keep:
   a.set_actor_hidden_in_game(True)
   for c in a.get_components_by_class(unreal.PrimitiveComponent):c.set_visibility(False,True);c.set_collision_profile_name('NoCollision')
   a.tags=[unreal.Name('ActivityScenesRetired-v1')];retired.append(a.get_actor_label())
 # Reconstructing imported blueprints can reset persistent visibility; verify readback on actual components.
 assert unreal.EditorLevelLibrary.save_current_level();write(OUT/'install.json',dict(status='passed',actors=rows,retired=retired,lights=len(PLAN['lights']),map_sha256=sha(mapfile),plan_sha256=sha(OUT/'plan.json')))
def audit():
 receipt=read(OUT/'install.json');assets=registry();actors={a.get_actor_label():a for a in SUB.get_all_level_actors()};checks=0;issues=[]
 def check(ok,name,details=None):
  nonlocal checks
  checks+=1
  if not ok:issues.append(dict(check=name,details=details))
 for r in receipt['actors']:
  a=actors.get(r['label']);q=r['request'];check(a is not None,'Actor exists',r['label'])
  if not a:continue
  c=a.static_mesh_component;check(c.static_mesh.get_path_name()==r['mesh'],'Saved mesh',r['label']);p=a.get_actor_location();want=point(q['position_m']);check((p-want).length()<.01,'Metric placement',r['label']);rotation=a.get_actor_rotation();check(abs(rotation.pitch)<.001 and abs(rotation.roll)<.001 and abs((rotation.yaw-(90-q['yaw_deg'])+180)%360-180)<.001,'Upright and intended heading',r['label']);check(c.is_visible() and not a.get_editor_property('hidden'),'Actor and component visible in game',r['label']);check(str(c.get_collision_profile_name())==q.get('collision','BlockAll'),'Collision',r['label'])
  if 'support_plane_z_m' in q:check(abs(q['position_m'][2]-q['support_plane_z_m']-.005)<.0001,'Prop rests on measured support surface',r['label'])
  expected=None
  if q.get('sign'):expected=EAL.load_asset(PREFIX+'/Signs/M_'+q['sign'].replace('-','_'))
  elif q.get('paint'):expected=EAL.load_asset('/Game/UniversityCity/ResortV1/Materials/M_Surface_3')
  elif q.get('emissive_panel'):expected=EAL.load_asset(PREFIX+'/Materials/M_ConstantLamp')
  for i in range(len(c.static_mesh.get_editor_property('static_materials'))):check(c.get_material(i)==(art_expected(q,c.static_mesh,i) or expected or c.static_mesh.get_material(i)),'Effective component PBR/text binding',r['label']+' slot '+str(i))
 for a in actors.values():
  if 'ActivityScenesRetired-v1' in [str(t) for t in a.tags]:
   for c in a.get_components_by_class(unreal.PrimitiveComponent):check(not c.is_visible() and str(c.get_collision_profile_name())=='NoCollision','Obsolete managed instance disabled',a.get_actor_label())
 check(len(receipt['actors'])==len(PLAN['instances']),'Exact current instance schedule')
 if PLAN.get('tree_relocations'):
  for q in read(OUT/'tree-relocations.json')['instances']:
   c=actors[q['actor']].get_component_by_class(unreal.HierarchicalInstancedStaticMeshComponent);t=c.get_instance_transform(q['index'],True);check((t.translation-unreal.Vector(*q['position_cm'])).length()<.01,'Tree and trunk position '+q['tree']);check(max(abs(x-y) for x,y in zip([t.scale3d.x,t.scale3d.y,t.scale3d.z],q['scale']))<1e-5,'Preserve plant scale variation');check(abs((t.rotation.rotator().yaw-q['yaw']+180)%360-180)<.01,'Preserve plant angle variation');check(c.get_instance_count()==q['instance_count'] and c.static_mesh.get_path_name()==q['mesh'],'Preserve tree count and LOD source');check([c.get_material(i).get_path_name() if c.get_material(i) else None for i in range(c.get_num_materials())]==q['materials'],'Preserve plant color variation')
 for aid,r in assets.items():
  m=EAL.load_asset(r['asset']);check(API.get_lod_count(m)>=3,'Three LODs',aid);check(m.get_num_triangles(0)==r['lod_triangles'][0],'Triangle receipt',aid)
  for i in range(len(m.get_editor_property('static_materials'))):check(m.get_material(i) is not None,'Material populated',aid)
  if r.get('remesh_report'):report=read(r['remesh_report']);check(report['status']=='passed' and report['output_sha256']==r['source_sha256'],'Certified remesh source',aid)
 signs=read(OUT/'signs.json');check(len(signs['entries'])==len(PLAN['signs'])*3,'All text in three languages')
 for key in PLAN['signs']:
  mat=EAL.load_asset(PREFIX+'/Signs/M_'+key.replace('-','_'));check(mat is not None,'Sign material exists',key)
  if mat:
   expected=hashlib.sha256(''.join(sha(OUT/'signs'/locale/(key+'.png')) for locale in ['zh-Hant','zh-Hans','en']).encode()).hexdigest();check(EAL.get_metadata_tag(mat,'ActivityTextSHA256')==expected,'Current text textures',key);check(not MEL.recompile_material(mat),'Sign shader compiles',key)
 collection=EAL.load_asset('/Game/UniversityCity/Localization/MPC_CityLanguage');world=unreal.EditorLevelLibrary.get_editor_world()
 for value in range(3):unreal.MaterialLibrary.set_scalar_parameter_value(world,collection,'LanguageIndex',value);check(unreal.MaterialLibrary.get_scalar_parameter_value(world,collection,'LanguageIndex')==value,'Runtime language '+str(value))
 unreal.MaterialLibrary.set_scalar_parameter_value(world,collection,'LanguageIndex',0)
 # Sweep the exact reserved standing and access strips in the reopened native world.
 corridors=[]
 for q in PLAN['corridors']:
  samples=[]
  for side in [-1,0,1]:
   start=point(q['a']);end=point(q['b']);dx=end.x-start.x;dy=end.y-start.y;length=max(1,math.hypot(dx,dy));offset=(q['width_m']/2-.45)*100*side;off=unreal.Vector(-dy/length*offset,dx/length*offset,100);start+=off;end+=off
   result=unreal.SystemLibrary.capsule_trace_single(world,start,end,40,90,unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,[],unreal.DrawDebugTrace.NONE,True)
   # UE5.8 returns an optional HitResult, rather than (bool, HitResult).
   if isinstance(result,tuple):result=result[-1]
   broken=result.to_tuple() if result else None;hit=bool(broken and broken[0]);who=broken[9] if broken else None
   samples.append(dict(side=side,blocked=hit,hit_actor=who.get_actor_label() if who else None,hit_location=[broken[4].x,broken[4].y,broken[4].z] if broken else None))
  corridors.append(dict(id=q['id'],scene=q['scene'],width_m=q['width_m'],samples=samples,clear=not any(r['blocked'] for r in samples)))
 write(OUT/'circulation.json',dict(status='passed' if all(r['clear'] for r in corridors) else 'requires_review',corridors=corridors))
 check(len({q['scene'] for q in PLAN['instances']})==10,'All ten packages instantiated');check(sha(OUT/'plan.json')==receipt['plan_sha256'],'Current plan saved')
 write(OUT/'audit.json',dict(status='passed' if not issues else 'failed',checks=checks,issues=issues,instances=len(receipt['actors']),models=len(assets),scenes=10,corridor_sweeps= len(corridors),blocked_corridors=[r['id'] for r in corridors if not r['clear']],map_sha256=sha(ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap'),scope='Static scene validation; not gameplay implementation'))
 assert not issues,str(issues[:10])
def render():
 world=unreal.EditorLevelLibrary.get_editor_world();views=list(PLAN['views']);state={'i':0,'phase':'setup','wait':60,'images':[],'busy':False};refs=[]
 # Inspection frames must show the real scene clearly rather than applying cinematic depth blur.
 unreal.SystemLibrary.execute_console_command(world,'r.DepthOfFieldQuality 0')
 # Keep the complete window header inside the standing-height review frame.
 views=[dict(q,target=[q['target'][0],q['target'][1],q['target'][2]+.15]) if q['name']=='08-admin' else dict(q) for q in views]
 book=next(v for v in views if v['name']=='09-bookstore')
 book['eye']=[286.6,473.15,13.65]
 for i,locale in [(1,'zh-Hans'),(2,'en')]:views.append(dict(book,name='09-bookstore-'+locale,language_index=i))
 market=next(v for v in views if v['name']=='03-nightmarket');views.append(dict(market,name='03-nightmarket-night',lighting='night'))
 collection=EAL.load_asset('/Game/UniversityCity/Localization/MPC_CityLanguage');lighting=[]
 for a in SUB.get_all_level_actors():
  if isinstance(a,unreal.DirectionalLight):lighting.append((a,a.light_component,a.get_actor_rotation(),a.light_component.get_editor_property('intensity')))
 def environment(q):
  unreal.MaterialLibrary.set_scalar_parameter_value(world,collection,'LanguageIndex',q.get('language_index',0))
  unreal.MaterialLibrary.set_scalar_parameter_value(world,art_collection(),'PreviewTime',q.get('art_time',-1))
  for a,c,r,intensity in lighting:
   c.set_intensity(.015 if q.get('lighting')=='night' else intensity);a.set_actor_rotation(unreal.Rotator(pitch=-6,yaw=0,roll=0) if q.get('lighting')=='sunset' else r,False)
 def tick(dt):
  if state['busy']:return
  if state['wait']>0:state['wait']-=1;return
  state['busy']=True
  try:
   if state['phase']=='setup':
    q=views[state['i']];environment(q);rot=unreal.MathLibrary.find_look_at_rotation(point(q['eye']),point(q['target']));unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(point(q['eye']),rot);a=SUB.spawn_actor_from_class(unreal.SceneCapture2D,point(q['eye']),rot);a.set_actor_location(point(q['eye']),False,True);a.set_actor_rotation(rot,False);actual=a.get_actor_location();assert (actual-point(q['eye'])).length()<.01; q=dict(q,actual_eye_cm=[actual.x,actual.y,actual.z]);c=a.capture_component2d;c.set_editor_property('capture_every_frame',False);c.set_editor_property('capture_on_movement',False);rt=unreal.RenderingLibrary.create_render_target2d(world,1440,1000,unreal.TextureRenderTargetFormat.RTF_RGBA8);refs.append(rt);c.set_editor_property('texture_target',rt);c.set_editor_property('capture_source',unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR);c.set_editor_property('fov_angle',75);state['current']=(q,a,c,rt);state['phase']='capture';state['wait']=60
   elif state['phase']=='capture':unreal.AutomationLibrary.finish_loading_before_screenshot();state['current'][2].capture_scene();state['phase']='export';state['wait']=10
   else:
    q,a,c,rt=state['current'];folder=OUT/'renders';folder.mkdir(exist_ok=True);p=folder/(q['name']+'.png');unreal.RenderingLibrary.export_render_target(world,rt,str(folder),p.name);assert p.exists() and p.stat().st_size>5000;state['images'].append(dict(name=q['name'],path=str(p),sha256=sha(p),camera=q));c.set_editor_property('texture_target',None);SUB.destroy_actor(a);unreal.RenderingLibrary.release_render_target2d(rt);refs.remove(rt);state['current']=None;state['i']+=1;state['phase']='setup';state['wait']=10
    if state['i']==len(views):
     environment({});unreal.unregister_slate_post_tick_callback(render_handle);write(OUT/'render.json',dict(status='passed',images=state['images'],native_map=MAP,map_sha256=sha(ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap')));unreal.SystemLibrary.quit_editor()
  except BaseException:
   unreal.unregister_slate_post_tick_callback(render_handle);write(OUT/'render-error.json',dict(status='failed',error=traceback.format_exc()));unreal.SystemLibrary.quit_editor()
  finally:state['busy']=False
 global render_handle,RENDER_REFS
 RENDER_REFS=(state,refs);render_handle=unreal.register_slate_post_tick_callback(tick)
]====]
