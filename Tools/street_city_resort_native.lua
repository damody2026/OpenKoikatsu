-- Lua owns orchestration; the embedded payload uses Unreal's installed Python API.
-- No additional .py/.ps1 project scripts are created.
package.path='Tools/lua/?.lua;Tools/lua/?/init.lua;'..package.path
local json=require('ok.json')
local mode=arg[1] or 'probe'
local first=tonumber(arg[2]) or 1
local last=tonumber(arg[3]) or 100
assert(({probe=true,import=true,install=true,audit=true,render=true})[mode],'probe | import [first last] | install | audit | render')
local p=assert(io.popen('cd'));local root=assert(p:read('*l')):gsub('\\','/');p:close()
local out=root..'/output/street-social-city-production/resort-native-v1'
assert(os.execute('if not exist "'..out..'" mkdir "'..out..'"'))
local code=[====[
import unreal,sys,json,math,hashlib,traceback,struct,re,time
from pathlib import Path
ROOT=Path(PAYLOAD['root']);OUT=Path(PAYLOAD['out']);BASE=ROOT/'output/street-social-city-production';MODE=PAYLOAD['mode']
sys.path.insert(0,str(ROOT/'Tools'))
EAL=unreal.EditorAssetLibrary;MEL=unreal.MaterialEditingLibrary;ATS=unreal.AssetToolsHelpers.get_asset_tools();API=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem);SUB=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
MAP='/Game/UniversityCity/Maps/UniversityCity';PREFIX='/Game/UniversityCity/ResortV1';TAG='ResortNative-v1';KEEP=[]
def read(p):return json.loads(Path(p).read_text('utf-8-sig'))
def write(p,v):
 if isinstance(v,dict):v['run_id']=PAYLOAD.get('run_id','legacy')
 Path(p).parent.mkdir(parents=True,exist_ok=True);Path(p).write_text(json.dumps(v,ensure_ascii=False,indent=2),'utf8')
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def point(p):return unreal.Vector(p[1]*100,p[0]*100,p[2]*100)
def linear(x):return x/12.92 if x<=.04045 else ((x+.055)/1.055)**2.4
def rgb(h):return [linear(int(h.lstrip('#')[i:i+2],16)/255) for i in (0,2,4)]
def color(v):return unreal.LinearColor(v[0],v[1],v[2],1)
def custom_input(name):
 q=unreal.CustomInput();q.set_editor_property('input_name',name);return q
def set_texture(m,name,value):
 MEL.set_material_instance_texture_parameter_value(m,name,value);assert MEL.get_material_instance_texture_parameter_value(m,name)==value,(m.get_path_name(),name);return True
def set_vector(m,name,value):
 MEL.set_material_instance_vector_parameter_value(m,name,value);got=MEL.get_material_instance_vector_parameter_value(m,name);assert max(abs(x-y) for x,y in zip([got.r,got.g,got.b,got.a],[value.r,value.g,value.b,value.a]))<1e-5,(m.get_path_name(),name);return True
def set_scalar(m,name,value):
 MEL.set_material_instance_scalar_parameter_value(m,name,value);assert abs(MEL.get_material_instance_scalar_parameter_value(m,name)-value)<1e-5;return True
def rotated_offset(pivot,scale,yaw):
 x,y,z=[pivot[k]*scale[k] for k in range(3)];a=math.radians(yaw);return unreal.Vector(x*math.cos(a)-y*math.sin(a),x*math.sin(a)+y*math.cos(a),z)
CAT=read(BASE/'resort-streetscape-v1/catalog.json');LAY=read(BASE/'resort-streetscape-v1/placement.json');VAR=read(BASE/'streetscape-variations-v1/variation-manifest.json');PLANTS=read(BASE/'resort-streetscape-v1/plantings.json')
OLD=read(BASE/'streetscape-v1/native-install.json');OLDASSETS={a['id']:a for a in OLD['assets']}
def save(a):assert EAL.save_loaded_asset(a)
def mi(name,parent):
 path=PREFIX+'/Materials/'+name;a=EAL.load_asset(path) if EAL.does_asset_exist(path) else ATS.create_asset(name,PREFIX+'/Materials',unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew());assert a;MEL.set_material_instance_parent(a,parent);return a
def texture(path,name,normal=False):
 dest=PREFIX+'/Textures/'+name;a=EAL.load_asset(dest) if EAL.does_asset_exist(dest) else None
 if not a:
  t=unreal.AssetImportTask();t.set_editor_property('filename',str(path));t.set_editor_property('destination_path',PREFIX+'/Textures');t.set_editor_property('destination_name',name);t.set_editor_property('automated',True);t.set_editor_property('save',True);ATS.import_asset_tasks([t]);a=EAL.load_asset(dest)
 assert isinstance(a,unreal.Texture2D),str(path);assert max(a.blueprint_get_size_x(),a.blueprint_get_size_y())<=2048
 a.set_editor_property('max_texture_size',2048);a.set_editor_property('power_of_two_mode',unreal.TexturePowerOfTwoSetting.STRETCH_TO_POWER_OF_TWO)
 if normal:a.set_editor_property('srgb',False)
 save(a);return a
def probe():
 rows=[]
 for aid in ['scooter','bicycle','camphor','cherry']:
  mesh=EAL.load_asset(OLDASSETS[aid]['asset']);assert mesh
  mats=[]
  for i in range(mesh.get_num_sections(0)):
   m=mesh.get_material(i)
   if m:mats.append(dict(slot=i,name=str(mesh.get_editor_property('static_materials')[i].get_editor_property('material_slot_name')),material=m.get_path_name(),type=m.get_class().get_name(),textures=[str(n) for n in MEL.get_texture_parameter_names(m)],vectors=[str(n) for n in MEL.get_vector_parameter_names(m)]))
  rows.append(dict(id=aid,path=mesh.get_path_name(),lod_triangles=[mesh.get_num_triangles(i) for i in range(API.get_lod_count(mesh))],materials=mats))
 write(OUT/'probe.json',dict(status='passed',assets=rows,actor_count=len(SUB.get_all_level_actors()),map_sha256=sha(ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap')))
def import_mesh(path,aid,lod):
 digest=sha(path);folder=PREFIX+'/Assets/'+aid+'/S_'+digest[:16];name=aid+'_LOD'+str(lod);dest=folder+'/'+name
 if EAL.does_asset_exist(dest):return EAL.load_asset(dest)
 pipeline=unreal.InterchangeGenericAssetsPipeline();KEEP.append(pipeline);pipeline.set_editor_property('asset_name',name);pipeline.set_editor_property('use_source_name_for_asset',False);pipeline.get_editor_property('common_meshes_properties').set_editor_property('bake_meshes',True);pipeline.get_editor_property('material_pipeline').set_editor_property('import_materials',False)
 mp=pipeline.get_editor_property('mesh_pipeline');mp.set_editor_property('combine_static_meshes_behavior',unreal.InterchangeCombineStaticMeshesBehavior.ALL);mp.set_editor_property('import_skeletal_meshes',False);mp.set_editor_property('build_nanite',False);mp.set_editor_property('generate_lightmap_u_vs',False)
 params=unreal.ImportAssetParameters();params.set_editor_property('is_automated',True);params.set_editor_property('destination_name',name);params.set_editor_property('override_pipelines',[unreal.SoftObjectPath(pipeline.get_path_name())]);manager=unreal.InterchangeManager.get_interchange_manager_scripted();objs=manager.import_asset(folder,unreal.InterchangeManager.create_source_data(str(path)),params);meshes=[a for a in objs or [] if isinstance(a,unreal.StaticMesh)];assert len(meshes)==1,(str(path),len(meshes));mesh=meshes[0];EAL.set_metadata_tag(mesh,'ResortSourceSHA256',digest);EAL.set_metadata_tag(mesh,'ResortSourceFile',str(path));save(mesh);KEEP.clear();return mesh
def finish():unreal.SystemLibrary.quit_editor()
def work():
 try:
  if MODE=='probe':probe()
  elif MODE=='import':import_assets()
  elif MODE=='install':install()
  elif MODE=='audit':audit()
  elif MODE=='render':render();return
 except BaseException:
  write(OUT/(MODE+'-error.json'),dict(error=traceback.format_exc()));unreal.log_error(traceback.format_exc())
 finish()
assert unreal.EditorLevelLibrary.load_level(MAP);unreal.AutomationLibrary.finish_loading_before_screenshot();unreal.EditorPythonScripting.set_keep_python_script_alive(True)
frames=0
def settled(dt):
 global frames
 frames+=1
 if frames<60:return
 unreal.unregister_slate_post_tick_callback(start_handle);work()
start_handle=unreal.register_slate_post_tick_callback(settled)
]====]
-- Functions are extended below without introducing Python source files.
local extension=[====[
def surface_materials():
 materials=[];example=BASE/'resort-streetscape-v1/models/R001-LOD0.glb';data=example.read_bytes();n=struct.unpack_from('<I',data,12)[0];doc=json.loads(data[20:20+n])
 for i in range(8):
  path=PREFIX+'/Materials/M_Surface_'+str(i);m=EAL.load_asset(path) if EAL.does_asset_exist(path) else ATS.create_asset('M_Surface_'+str(i),PREFIX+'/Materials',unreal.Material,unreal.MaterialFactoryNew());MEL.delete_all_material_expressions(m);m.set_editor_property('blend_mode',unreal.BlendMode.BLEND_OPAQUE)
  pbr=doc['materials'][i]['pbrMetallicRoughness'];v=MEL.create_material_expression(m,unreal.MaterialExpressionVectorParameter,-400,0);v.set_editor_property('parameter_name','ResortTint');v.set_editor_property('default_value',color(pbr['baseColorFactor'][:3]));base=v
  texname='teak' if i==0 else 'stone' if i==7 else 'linen' if i in [2,3,5,6] else None
  if texname:
   t=texture(BASE/'resort-streetscape-v1/textures'/(texname+'.png'),'Resort_'+texname);sample=MEL.create_material_expression(m,unreal.MaterialExpressionTextureSample,-600,150);sample.set_editor_property('texture',t);mul=MEL.create_material_expression(m,unreal.MaterialExpressionMultiply,-200,0);assert MEL.connect_material_expressions(sample,'RGB',mul,'A');assert MEL.connect_material_expressions(v,'RGB',mul,'B');base=mul
  assert MEL.connect_material_property(base,'' if base!=v else 'RGB',unreal.MaterialProperty.MP_BASE_COLOR)
  for prop,value in [(unreal.MaterialProperty.MP_ROUGHNESS,pbr['roughnessFactor']),(unreal.MaterialProperty.MP_METALLIC,pbr['metallicFactor'])]:
   node=MEL.create_material_expression(m,unreal.MaterialExpressionConstant,-200,300);node.set_editor_property('r',value);assert MEL.connect_material_property(node,'',prop)
  m.set_editor_property('used_with_instanced_static_meshes',True);MEL.recompile_material(m);save(m);materials.append(m)
 return materials
def bind_surface(mesh,materials):
 rows=[]
 for i,slot in enumerate(mesh.get_editor_property('static_materials')):
  names=[str(slot.get_editor_property(n)) for n in ['material_slot_name','imported_material_slot_name']];match=next((re.search(r'surface[_-](\d+)',s,re.I) for s in names if re.search(r'surface[_-](\d+)',s,re.I)),None)
  assert match,('Unknown native surface slot',mesh.get_path_name(),names);index=int(match.group(1));assert index<8;mesh.set_material(i,materials[index]);rows.append(dict(slot=i,surface=index,name=names[0]))
 return rows
def import_assets():
 assert read(BASE/'resort-streetscape-v1/verification.json')['status']=='passed';materials=surface_materials();path=OUT/'imports.json';result=read(path) if path.exists() else dict(status='in_progress',assets={})
 for a in CAT['assets'][PAYLOAD['first']-1:PAYLOAD['last']]:
  meshes=[];slots=[]
  for l in a['lods']:
   source=BASE/'resort-streetscape-v1'/l['path'];assert sha(source)==l['sha256'];m=import_mesh(source,a['id'],l['level']);slots.append(bind_surface(m,materials));meshes.append(m);save(m)
  m=meshes[0]
  for level,source in enumerate(meshes[1:],1):assert API.set_lod_from_static_mesh(m,level,source,0,True)==level
  assert API.set_lod_screen_sizes(m,[1,.16,.045]);counts=[m.get_num_triangles(i) for i in range(API.get_lod_count(m))];assert counts==[l['triangles'] for l in a['lods']],(a['id'],counts)
  body=m.get_editor_property('body_setup');body.set_editor_property('collision_trace_flag',unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE);EAL.set_metadata_tag(m,'ResortCatalogSHA256',sha(BASE/'resort-streetscape-v1/catalog.json'));save(m)
  bounds=m.get_bounding_box();result['assets'][a['id']]=dict(asset=m.get_path_name(),lod_triangles=counts,slots=bind_surface(m,materials),bounds_cm=dict(min=[bounds.min.x,bounds.min.y,bounds.min.z],max=[bounds.max.x,bounds.max.y,bounds.max.z]),source_sha256=a['lods'][0]['sha256']);write(path,result);unreal.log('RESORT_IMPORTED '+a['id'])
 result['status']='passed' if len(result['assets'])==100 else 'partial';result['count']=len(result['assets']);write(path,result)
def texture_parameter(material):
 names=[str(n) for n in MEL.get_texture_parameter_names(material)];candidate=[n for n in names if 'basecolor' in n.lower().replace('_','') or 'albedo' in n.lower()];assert len(candidate)==1,(material.get_path_name(),names);return candidate[0]
def vehicle_parent(aid):
 source=EAL.load_asset(OLDASSETS[aid]['asset']).get_material(0);assert isinstance(source,unreal.Material)
 path=PREFIX+'/Materials/M_'+aid+'_Paint';m=EAL.load_asset(path) if EAL.does_asset_exist(path) else EAL.duplicate_asset(source.get_path_name(),path);assert m
 if not any(str(n)=='BaseColorTexture' for n in MEL.get_texture_parameter_names(m)):
  # Duplicate the full original PBR graph; replace only base-colour sampling.
  sourcefile=Path(EAL.get_metadata_tag(EAL.load_asset(OLDASSETS[aid]['asset']),'StreetCitySourceFile'));data=sourcefile.read_bytes();n=struct.unpack_from('<I',data,12)[0];doc=json.loads(data[20:20+n]);factor=doc['materials'][0].get('pbrMetallicRoughness',{}).get('baseColorFactor',[1,1,1,1])
  t=texture(BASE/'streetscape-variations-v1/textures'/aid/'paint-0.png',aid+'_Paint_0');sample=MEL.create_material_expression(m,unreal.MaterialExpressionTextureSampleParameter2D,-500,-200);sample.set_editor_property('parameter_name','BaseColorTexture');sample.set_editor_property('texture',t)
  c=MEL.create_material_expression(m,unreal.MaterialExpressionConstant3Vector,-500,-50);c.set_editor_property('constant',color(factor[:3]));mul=MEL.create_material_expression(m,unreal.MaterialExpressionMultiply,-200,-200);assert MEL.connect_material_expressions(sample,'RGB',mul,'A');assert MEL.connect_material_expressions(c,'',mul,'B');assert MEL.connect_material_property(mul,'',unreal.MaterialProperty.MP_BASE_COLOR)
 m.set_editor_property('used_with_instanced_static_meshes',True);MEL.recompile_material(m);save(m);return m
def tree_parent(aid):
 source=EAL.load_asset('/Game/UniversityCity/StreetScenery/Foliage/M_'+aid+'_Cutout');assert source
 path=PREFIX+'/Materials/M_'+aid+'_Tint';m=EAL.load_asset(path) if EAL.does_asset_exist(path) else EAL.duplicate_asset(source.get_path_name(),path);assert m
 # Preserve the existing opacity A connection, texture and roughness.
 base=MEL.get_material_property_input_node(m,unreal.MaterialProperty.MP_BASE_COLOR);baseout=MEL.get_material_property_input_node_output_name(m,unreal.MaterialProperty.MP_BASE_COLOR)
 existing=[e for e in MEL.get_material_expressions(m) if isinstance(e,unreal.MaterialExpressionVectorParameter) and str(e.get_editor_property('parameter_name'))=='LeafTint']
 if not existing:
  param=MEL.create_material_expression(m,unreal.MaterialExpressionVectorParameter,-400,-200);param.set_editor_property('parameter_name','LeafTint');param.set_editor_property('default_value',color([1,1,1]));mul=MEL.create_material_expression(m,unreal.MaterialExpressionMultiply,-100,0);assert MEL.connect_material_expressions(base,baseout,mul,'A');assert MEL.connect_material_expressions(param,'RGB',mul,'B');assert MEL.connect_material_property(mul,'',unreal.MaterialProperty.MP_BASE_COLOR)
 m.set_editor_property('used_with_instanced_static_meshes',True);MEL.recompile_material(m);save(m);return m
def variant_materials():
 result={};surfaces=surface_materials()
 for aid in ['scooter','bicycle']:
  mesh=EAL.load_asset(OLDASSETS[aid]['asset']);source=vehicle_parent(aid);param=texture_parameter(source);rows=next(m for m in VAR['models'] if m['id']==aid and m['level']==0);result[aid]=[]
  for p in rows['palettes']:
   m=mi('MI_'+aid+'_'+str(p['id']),source);t=texture(BASE/'streetscape-variations-v1/textures'/aid/('paint-'+str(p['id'])+'.png'),aid+'_Paint_'+str(p['id']));assert set_texture(m,param,t);save(m);result[aid].append(m)
 for aid in ['camphor','cherry']:
  parent=tree_parent(aid);result[aid]=[];row=next(m for m in VAR['models'] if m['id']==aid and m['level']==0)
  for p in row['palettes']:
   m=mi('MI_'+aid+'_'+str(p['id']),parent);assert set_vector(m,'LeafTint',color(p['linear_rgb']));save(m);result[aid].append(m)
 result['surface']={}
 for p,h in enumerate(CAT['presets']):
  result['surface'][p]=[]
  for i,parent in enumerate(surfaces):
   if i not in [3,5,6]:result['surface'][p].append(parent);continue
   m=mi('MI_Surface_'+str(i)+'_Palette_'+str(p),parent);assert set_vector(m,'ResortTint',color(rgb(h)));save(m);result['surface'][p].append(m)
 return result
def cluster(label,mesh,instances,materials,collision,visible=True,kind='prop',cull=180):
 actors={a.get_actor_label():a for a in SUB.get_all_level_actors()};a=actors.get(label)
 if a:assert TAG in [str(t) for t in a.tags],('Refuse unrelated label',label)
 else:
  bp=EAL.load_asset('/Game/UniversityCity/StreetScenery/BP_StreetCluster');assert bp;a=SUB.spawn_actor_from_class(bp.generated_class(),unreal.Vector());a.set_actor_label(label)
 origin=instances[0]['position_m'];a.set_actor_location(point(origin),False,False);a.tags=[unreal.Name(TAG),unreal.Name('ResortKind-'+kind)];a.set_folder_path('UniversityCity/ResortV1/'+kind);comp=a.get_component_by_class(unreal.HierarchicalInstancedStaticMeshComponent);assert comp
 if a.root_component:a.root_component.set_mobility(unreal.ComponentMobility.STATIC)
 comp.set_mobility(unreal.ComponentMobility.STATIC);comp.set_static_mesh(mesh)
 for i,mat in enumerate(materials):comp.set_material(i,mat)
 comp.clear_instances();comp.set_collision_profile_name(collision);comp.set_visibility(visible,True);comp.set_hidden_in_game(not visible);comp.set_cull_distances(cull*80,cull*100);comp.set_cast_shadow(visible)
 transforms=[]
 for p in instances:
  pos=p['position_m'];sc=p['scale'];rot=unreal.Rotator(pitch=0,yaw=90-p['yaw_deg'],roll=0);loc=unreal.Vector((pos[1]-origin[1])*100,(pos[0]-origin[0])*100,(pos[2]-origin[2])*100)
  if p.get('pivot_cm'):
   pivot=p['pivot_cm'];loc-=rotated_offset(pivot,sc,rot.yaw)
  transforms.append(unreal.Transform(location=loc,rotation=rot,scale=unreal.Vector(*sc)))
 comp.add_instances(transforms,False,False,False);assert comp.get_instance_count()==len(instances)
 return dict(label=label,mesh=mesh.get_path_name(),kind=kind,collision=collision,visible=visible,cull_m=cull,materials=[m.get_path_name() for m in materials],instances=instances)
def install():
 imported=read(OUT/'imports.json');assert imported['status']=='passed';mats=variant_materials();groups=[];retired=[]
 baseline=[]
 for a in SUB.get_all_level_actors():
  if 'StreetScenery-v1' in [str(t) for t in a.tags] or TAG in [str(t) for t in a.tags]:continue
  p=a.get_actor_location();r=a.get_actor_rotation();s=a.get_actor_scale3d();mesh=a.static_mesh_component.static_mesh.get_path_name() if isinstance(a,unreal.StaticMeshActor) and a.static_mesh_component.static_mesh else None
  baseline.append(dict(label=a.get_actor_label(),class_name=a.get_class().get_name(),position_cm=[p.x,p.y,p.z],rotation=[r.pitch,r.yaw,r.roll],scale=[s.x,s.y,s.z],mesh=mesh))
 if not (OUT/'baseline-objects.json').exists():write(OUT/'baseline-objects.json',baseline)
 # Replace managed original tree/vehicle/trunk clusters; never touch buildings or unrelated actors.
 oldgroups={g['label']:g for g in read(BASE/'streetscape-v1/delivery.json')['groups']}
 for a in SUB.get_all_level_actors():
  q=oldgroups.get(a.get_actor_label())
  if q and q['asset'] in ['camphor','cherry','scooter','bicycle','tree-trunk-collision']:
   assert 'StreetScenery-v1' in [str(t) for t in a.tags];c=a.get_component_by_class(unreal.HierarchicalInstancedStaticMeshComponent);c.set_visibility(False,True);c.set_hidden_in_game(True);c.set_collision_profile_name('NoCollision');a.tags=list(a.tags)+[unreal.Name('ResortReplaced-v1')];retired.append(a.get_actor_label())
 bins={}
 for p in native_variation_rows():
  q=dict(p['after']);aid=p['asset'];palette=q['variation']['palette'];x,y,z=q['position_m'];key=(aid,palette,math.floor(x/100),math.floor(y/100));bins.setdefault(key,[]).append(q)
 for (aid,pal,x,y),rows in bins.items():
  mesh=EAL.load_asset(OLDASSETS[aid]['asset']);materials=[mesh.get_material(i) for i in range(len(mesh.get_editor_property('static_materials')))]
  for i,slot in enumerate(mesh.get_editor_property('static_materials')):
   if aid in ['scooter','bicycle'] or 'masked' in str(slot.get_editor_property('material_slot_name')).lower():materials[i]=mats[aid][pal]
  groups.append(cluster('Resort_'+aid+'_'+str(pal)+'_'+str(x)+'_'+str(y),mesh,rows,materials,'NoCollision' if aid in ['camphor','cherry'] else 'BlockAll',True,aid,650 if aid in ['camphor','cherry'] else 160))
 bins={}
 for p in VAR['trunk_colliders']:
  x,y,z=p['position_m'];bins.setdefault((math.floor(x/100),math.floor(y/100)),[]).append(p)
 mesh=EAL.load_asset(OLDASSETS['tree-trunk-collision']['asset'])
 for (x,y),rows in bins.items():groups.append(cluster('Resort_Trunks_'+str(x)+'_'+str(y),mesh,rows,[mesh.get_material(0)],'BlockAll',False,'trunk',650))
 layout=read(OUT/'placement.json') if (OUT/'placement.json').exists() else LAY;bins={}
 for p in layout['instances']:
  q=dict(p);q['yaw_deg']=q.pop('yaw_deg');bins.setdefault((q['asset'],q['preset'],q.get('zone','resort')),[]).append(q)
 for (aid,palette,zone),rows in bins.items():
  mesh=EAL.load_asset(imported['assets'][aid]['asset']);bindings=imported['assets'][aid]['slots'];materials=[mats['surface'][palette][s['surface']] for s in bindings];groups.append(cluster('Resort_'+aid+'_'+str(palette)+'_'+zone,mesh,rows,materials,'BlockAll',True,'prop',220 if aid in ['R013','R014','R016','R017','R019'] else 140))
 groups.extend(install_flowers(layout))
 keep={g['label'] for g in groups}
 for a in SUB.get_all_level_actors():
  if TAG in [str(t) for t in a.tags] and a.get_actor_label() not in keep:
   c=a.get_component_by_class(unreal.HierarchicalInstancedStaticMeshComponent);c.set_visibility(False,True);c.set_collision_profile_name('NoCollision');a.tags=[unreal.Name('ResortRetired-v1')]
 assert unreal.EditorLevelLibrary.save_current_level();write(OUT/'install.json',dict(status='saved_requires_reopen_audit',groups=groups,retired_original_groups=retired,map_sha256=sha(ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap'),catalog_sha256=sha(BASE/'resort-streetscape-v1/catalog.json'),variation_sha256=sha(BASE/'streetscape-variations-v1/variation-manifest.json')))
def native_variation_rows():
 rows=json.loads(json.dumps(VAR['instances']))
 for aid in ['scooter','bicycle']:
  vehicles=[p for p in rows if p['asset']==aid];counts={i:sum(p['after']['variation']['palette']==i for p in vehicles) for i in range(6)};palette=next(m for m in VAR['models'] if m['id']==aid and m['level']==0)['palettes']
  for needed in [i for i in range(6) if not counts[i]]:
   donor=next(p for p in vehicles if counts[p['after']['variation']['palette']]>1);old=donor['after']['variation']['palette'];donor['after']['variation']['palette']=needed;donor['after']['variation']['linear_rgb']=palette[needed]['linear_rgb'];donor['after']['variation']['native_reason']='Ensure all six presets are visible without adding vehicles';counts[old]-=1;counts[needed]+=1
 return rows
def flower_parent():
 source=EAL.load_asset(OLDASSETS['bed-flowers']['asset']).get_material(0)
 while not isinstance(source,unreal.Material):source=source.get_editor_property('parent')
 path=PREFIX+'/Materials/M_FlowerVariation';m=EAL.load_asset(path) if EAL.does_asset_exist(path) else EAL.duplicate_asset(source.get_path_name(),path)
 if not any(str(n)=='FlowerTint' for n in MEL.get_vector_parameter_names(m)):
  base=MEL.get_material_property_input_node(m,unreal.MaterialProperty.MP_BASE_COLOR);output=MEL.get_material_property_input_node_output_name(m,unreal.MaterialProperty.MP_BASE_COLOR);custom=MEL.create_material_expression(m,unreal.MaterialExpressionCustom,-200,-200);custom.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT3);custom.set_editor_property('inputs',[custom_input('Base'),custom_input('Flower'),custom_input('Leaf')]);custom.set_editor_property('code','float hi=max(Base.r,max(Base.g,Base.b)); float lo=min(Base.r,min(Base.g,Base.b)); if(Base.g>Base.r*1.08 && Base.g>Base.b*1.03) return Base*Leaf; if(hi-lo>0.07) return lerp(Base,Flower*max(hi,0.2),0.65); return Base;');assert MEL.connect_material_expressions(base,output,custom,'Base')
  tint=MEL.create_material_expression(m,unreal.MaterialExpressionVectorParameter,-600,-300);tint.set_editor_property('parameter_name','FlowerTint');tint.set_editor_property('default_value',color([1,1,1]));leaf=MEL.create_material_expression(m,unreal.MaterialExpressionScalarParameter,-600,-100);leaf.set_editor_property('parameter_name','LeafFactor');leaf.set_editor_property('default_value',1);assert MEL.connect_material_expressions(tint,'RGB',custom,'Flower');assert MEL.connect_material_expressions(leaf,'',custom,'Leaf');assert MEL.connect_material_property(custom,'',unreal.MaterialProperty.MP_BASE_COLOR)
 m.set_editor_property('used_with_instanced_static_meshes',True);MEL.recompile_material(m);save(m);return m
def install_flowers(layout):
 parent=flower_parent();mesh=EAL.load_asset(OLDASSETS['bed-flowers']['asset']);box=mesh.get_bounding_box();size=[box.max.x-box.min.x,box.max.y-box.min.y,box.max.z-box.min.z];pivot=[(box.min.x+box.max.x)/2,(box.min.y+box.max.y)/2,box.min.z];parents={p['id']:p for p in layout['instances']};groups=[];colors=['#f4e6bd','#f19b85','#b6a1db','#e8a7bc']
 for i,p in enumerate(PLANTS['flowers']):
  mother=parents[p['parent']];x,y,z=mother['position_m'];dx,dy,dz=p['offset_m'];angle=math.radians(mother['yaw_deg']);fit=p['fit_size_m'];nominal=min(fit[2]*100/size[2],fit[1]*100/size[1]);width=size[0]*nominal/100;depth=size[1]*nominal/100;nx=max(1,min(8,math.ceil(fit[0]/width)));ny=max(1,min(2,8//nx,math.ceil(fit[1]/depth)));patches=[(dx,dy,dz)]
  if mother['asset']=='R041':patches=[(0,k*.45,.45+k*.25) for k in range(3)]
  elif mother['asset']=='R044':patches=[(0,0,.155+k*.375) for k in range(4)]
  elif mother['asset']=='R045':patches=[(0,k*.25,.62+k*.55) for k in range(3)]
  rows=[]
  for tier,(ox,oy,oz) in enumerate(patches):
   for ix in range(nx):
    for iy in range(ny):
     sequence=i*97+tier*29+ix*7+iy*3;px=ox+(ix+.5)*fit[0]/nx-fit[0]/2;py=oy+(iy+.5)*fit[1]/ny-fit[1]/2;position=[x+px*math.cos(angle)-py*math.sin(angle),y+px*math.sin(angle)+py*math.cos(angle),z+oz];xy=.94+(sequence*17%61)/1000.;zz=.96+(sequence*13%81)/1000.;yaw=(sequence*37%241)/10.-12.;sc=[nominal*xy,nominal*xy,nominal*zz];rows.append(dict(id='Flower-'+str(i)+'-'+str(len(rows)),position_m=position,scale=sc,yaw_deg=mother['yaw_deg']+yaw,pivot_cm=pivot,parent=p['parent'],variation=dict(palette=p['palette'],leaf_factor=p['leaf_factor'],horizontal_factor=xy,height_factor=zz,yaw_delta_deg=yaw,nominal_uniform_scale=nominal,tier=tier,policy='preserve source proportions; repeat instances to fill beds')))
  m=mi('MI_Flower_'+str(i),parent);assert set_vector(m,'FlowerTint',color(rgb(colors[p['palette']])));assert set_scalar(m,'LeafFactor',p['leaf_factor']);save(m);groups.append(cluster('Resort_Flowers_'+str(i),mesh,rows,[m],'NoCollision',True,'flower',60))
 return groups
def base_material(m):
 while not isinstance(m,unreal.Material):m=m.get_editor_property('parent')
 return m
def pbr_input_signature(m,prop):
 m=base_material(m);root=MEL.get_material_property_input_node(m,prop)
 def visit(e,seen):
  if not e:return None
  if e.get_path_name() in seen:return 'shared'
  seen.add(e.get_path_name());result=dict(type=e.get_class().get_name())
  if isinstance(e,unreal.MaterialExpressionTextureSample):result['texture']=e.get_editor_property('texture').get_path_name()
  for key in ['r','const_a','const_b','constant','default_value']:
   try:
    value=e.get_editor_property(key)
    if isinstance(value,(float,int,bool)):result[key]=value
    elif isinstance(value,unreal.LinearColor):result[key]=[value.r,value.g,value.b,value.a]
   except Exception:pass
  result['inputs']=[visit(n,seen) for n in MEL.get_inputs_for_material_expression(m,e)];return result
 return visit(root,set())
def audit():
 receipt=read(OUT/'install.json');assert receipt['map_sha256']==sha(ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap');imported=read(OUT/'imports.json');actors={a.get_actor_label():a for a in SUB.get_all_level_actors()};checks=0;issues=[];counts={};palettes={};zones={}
 def check(ok,name,details=None):
  nonlocal checks
  checks+=1
  if not ok:issues.append(dict(check=name,details=details))
 for a in CAT['assets']:
  row=imported['assets'][a['id']];m=EAL.load_asset(row['asset']);check(m is not None,'mesh '+a['id']);check(API.get_lod_count(m)==3,'three native LODs '+a['id']);check([m.get_num_triangles(i) for i in range(3)]==[l['triangles'] for l in a['lods']],'measured triangle counts '+a['id']);check(not m.is_lod_screen_size_auto_computed(),'manual LOD screen thresholds '+a['id']);check(all(abs(a-b)<1e-5 for a,b in zip(API.get_lod_screen_sizes(m),[1,.16,.045])),'native LOD thresholds '+a['id']);check(not m.get_editor_property('nanite_settings').get_editor_property('enabled'),'no Nanite requirement '+a['id']);check(EAL.get_metadata_tag(m,'ResortSourceSHA256')==a['lods'][0]['sha256'],'source identity '+a['id']);box=m.get_bounding_box();actual=[box.max.x-box.min.x,box.max.y-box.min.y,box.max.z-box.min.z];want=[(a['bounds_m'][1][i]-a['bounds_m'][0][i])*100 for i in range(3)];check(max(abs(x-y) for x,y in zip(actual,want))<.05,'native metre dimensions '+a['id'],dict(actual_cm=actual,expected_cm=want));check(all(m.get_material(i) for i in range(len(m.get_editor_property('static_materials')))),'all native materials '+a['id'])
 active=[a for a in actors.values() if TAG in [str(t) for t in a.tags]];check(len(active)==len(receipt['groups']),'exact active managed cluster set')
 for g in receipt['groups']:
  a=actors.get(g['label']);check(a is not None,'persistent cluster '+g['label']);c=a.get_component_by_class(unreal.HierarchicalInstancedStaticMeshComponent);check(c is not None,'HISM '+g['label']);check(c.get_instance_count()==len(g['instances']),'instance count '+g['label']);check(c.static_mesh.get_path_name()==g['mesh'],'native shared mesh '+g['label']);check(str(c.get_collision_profile_name())==g['collision'],'collision '+g['label']);check(bool(c.get_editor_property('visible'))==g['visible'],'visibility '+g['label']);check(c.get_editor_property('forced_lod_model')==0,'automatic LOD '+g['label']);check(c.get_editor_property('instance_end_cull_distance')==g['cull_m']*100,'native culling '+g['label']);check([c.get_material(i).get_path_name() for i in range(len(g['materials']))]==g['materials'],'material overrides '+g['label'])
  counts[g['kind']]=counts.get(g['kind'],0)+len(g['instances'])
  for i,p in enumerate(g['instances']):
   t=c.get_instance_transform(i,True);rot=unreal.Rotator(pitch=0,yaw=90-p['yaw_deg'],roll=0);want=point(p['position_m']);scale=p['scale']
   if p.get('pivot_cm'):want-=rotated_offset(p['pivot_cm'],scale,rot.yaw)
   check(sum((x-y)**2 for x,y in zip([t.translation.x,t.translation.y,t.translation.z],[want.x,want.y,want.z]))<.01,'native position '+p['id']);check(max(abs(x-y) for x,y in zip([t.scale3d.x,t.scale3d.y,t.scale3d.z],scale))<1e-5,'native scale '+p['id']);check(abs((t.rotation.rotator().yaw-rot.yaw+180)%360-180)<.01,'native orientation '+p['id'])
   if 'preset' in p:palettes[p['preset']]=palettes.get(p['preset'],0)+1
   if 'zone' in p:zones[p['zone']]=zones.get(p['zone'],0)+1
 check(counts.get('camphor',0)+counts.get('cherry',0)==1475,'exact tree count, no duplicates');check(counts.get('trunk')==1475,'matching tree collision trunks');check(counts.get('scooter')==10 and counts.get('bicycle')==24,'all existing vehicles replaced');check(counts.get('prop')==300,'all regional resort instances');check(sum(g['kind']=='flower' for g in receipt['groups'])==24,'all flower bed groups');check(len(palettes)==6,'six resort color presets represented')
 for g in receipt['groups']:
  if g['kind']=='flower':
   for p in g['instances']:
    v=p['variation'];s=p['scale'];check(abs(s[0]-s[1])<1e-6 and abs(s[0]/v['nominal_uniform_scale']-v['horizontal_factor'])<1e-6,'plants preserve proportions '+p['id']);check(abs(v['yaw_delta_deg'])<=12 and .94<=v['horizontal_factor']<=1. and .96<=v['height_factor']<=1.04,'bounded plant variation '+p['id'])
 check({p['asset'] for g in receipt['groups'] if g['kind']=='prop' for p in g['instances']}=={a['id'] for a in CAT['assets']},'all 100 distinct types placed in the native world')
 mothers={p['id']:p for p in read(OUT/'placement.json')['instances']}
 for g in receipt['groups']:
  if g['kind']=='flower':
   aid=mothers[g['instances'][0]['parent']]['asset'];expected={'R041':3,'R044':4,'R045':3}.get(aid,1);check({p['variation']['tier'] for p in g['instances']}==set(range(expected)),'all planter tiers populated '+g['label'])
 for aid in ['scooter','bicycle']:
  src=EAL.load_asset(OLDASSETS[aid]['asset']).get_material(0);parent=EAL.load_asset(PREFIX+'/Materials/M_'+aid+'_Paint')
  for prop in [unreal.MaterialProperty.MP_NORMAL,unreal.MaterialProperty.MP_ROUGHNESS,unreal.MaterialProperty.MP_METALLIC]:check(pbr_input_signature(src,prop)==pbr_input_signature(parent,prop),'vehicle PBR preserved '+aid+' '+str(prop))
  for pal in range(6):
   m=EAL.load_asset(PREFIX+'/Materials/MI_'+aid+'_'+str(pal));tex=MEL.get_material_instance_texture_parameter_value(m,'BaseColorTexture');check(tex is not None and tex.get_name()==aid+'_Paint_'+str(pal),'native vehicle color '+aid+str(pal));check(max(tex.blueprint_get_size_x(),tex.blueprint_get_size_y())<=2048,'2K vehicle texture '+aid+str(pal))
 for aid in ['camphor','cherry']:
  parent=EAL.load_asset(PREFIX+'/Materials/M_'+aid+'_Tint');check(parent.get_editor_property('blend_mode')==unreal.BlendMode.BLEND_MASKED and parent.get_editor_property('two_sided'),'masked two-sided foliage '+aid);check(MEL.get_material_property_input_node_output_name(parent,unreal.MaterialProperty.MP_OPACITY_MASK)=='A','preserved texture alpha '+aid)
  row=next(m for m in VAR['models'] if m['id']==aid and m['level']==0)
  for pal in row['palettes']:
   m=EAL.load_asset(PREFIX+'/Materials/MI_'+aid+'_'+str(pal['id']));v=MEL.get_material_instance_vector_parameter_value(m,'LeafTint');check(max(abs(x-y) for x,y in zip([v.r,v.g,v.b],pal['linear_rgb']))<1e-5,'native leaf palette '+aid+str(pal['id']))
 for label in receipt['retired_original_groups']:
  c=actors[label].get_component_by_class(unreal.HierarchicalInstancedStaticMeshComponent);check(not c.get_editor_property('visible') and str(c.get_collision_profile_name())=='NoCollision','retired original, no double rendering '+label)
 for old in read(OUT/'baseline-objects.json'):
  a=actors.get(old['label']);check(a is not None,'original environment actor preserved '+old['label'])
  if not a:continue
  p=a.get_actor_location();check(max(abs(x-y) for x,y in zip([p.x,p.y,p.z],old['position_cm']))<.1,'original environment position '+old['label'])
  if old['mesh']:check(isinstance(a,unreal.StaticMeshActor) and a.static_mesh_component.static_mesh.get_path_name()==old['mesh'],'original building/terrain mesh '+old['label'])
 # Native collision traces isolate this new layer, preserving clear shop entries.
 world=unreal.EditorLevelLibrary.get_editor_world();ignore=[a for a in actors.values() if TAG not in [str(t) for t in a.tags]]
 for aa,bb,width,kind,label in read(BASE/'streetscape-v1/plan.json')['entrance_approaches_m']:
  dx,dy=bb[0]-aa[0],bb[1]-aa[1];length=math.hypot(dx,dy)
  if length<.01:continue
  for t in [.2,.5,.8]:
   for lane in [-.9,0,.9]:
    x=aa[0]+dx*t-dy/length*lane;y=aa[1]+dy*t+dx/length*lane;hit=unreal.SystemLibrary.line_trace_single(world,point([x,y,12.15]),point([x,y,14.2]),unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,ignore,unreal.DrawDebugTrace.NONE,True);check(not (hit and hit.to_tuple()[0]),'native 2m entrance clearance '+label)
 result=dict(status='failed' if issues else 'passed',checks=checks,issues=issues,instances=counts,color_palettes=palettes,regions=zones,managed_clusters=len(active),map_sha256=receipt['map_sha256'],reopened_for_phase=MODE,scope='Persistent native mesh dimensions, real LODs, materials/PBR, instances/variation, original environment regression and new-layer entrance collision traces')
 write(OUT/'audit.json',result);assert not issues,issues[:8];return result
def render():
 audit();world=unreal.EditorLevelLibrary.get_editor_world();layout=read(OUT/'placement.json');views=[]
 for zone in ['hotel-garden','campus-lake','shop-dining','bay-camp','sports-entry']:
  rows=[p for p in layout['instances'] if p['zone']==zone];p=rows[0]['position_m'];views.append(dict(name=zone,eye=[p[0]-8,p[1]-12,p[2]+5],target=[p[0]+3,p[1]+4,p[2]+1]))
 views.append(dict(name='world-overview',eye=[600,260,190],target=[720,480,15]))
 scooters=[p for g in read(OUT/'install.json')['groups'] if g['kind']=='scooter' for p in g['instances']];seen=set()
 for p in scooters:
  pal=p['variation']['palette']
  if pal in seen:continue
  seen.add(pal);x,y,z=p['position_m'];views.append(dict(name='scooter-palette-'+str(pal),eye=[x-2.4,y-2.6,z+1.6],target=[x,y,z+.6]))
 flowers=[g for g in read(OUT/'install.json')['groups'] if g['kind']=='flower'];p=flowers[0]['instances'][0]['position_m'];views.append(dict(name='flower-detail',eye=[p[0]-2,p[1]-2,p[2]+1.6],target=[p[0],p[1],p[2]+.1]))
 trees=[p['after'] for p in VAR['instances'] if p['asset']=='cherry'];p=trees[0]['position_m'];views.append(dict(name='foliage-near',eye=[p[0]-8,p[1]-10,p[2]+5],target=[p[0],p[1],p[2]+3]));views.append(dict(name='foliage-far',eye=[p[0]-65,p[1]-90,p[2]+25],target=[p[0],p[1],p[2]+3]));views=safe_cameras(views);write(OUT/'camera-plan.json',dict(status='collision_and_building_envelope_checked',views=views))
 state=dict(index=0,phase='setup',hold=60,current=None,results=[],busy=False);targets=[]
 def tick(dt):
  if state['busy']:return
  if state['hold']>0:state['hold']-=1;return
  state['busy']=True
  try:
   if state['phase']=='setup':
    q=views[state['index']];a=SUB.spawn_actor_from_class(unreal.SceneCapture2D,point(q['eye']),unreal.MathLibrary.find_look_at_rotation(point(q['eye']),point(q['target'])));c=a.capture_component2d;c.set_editor_property('capture_every_frame',False);c.set_editor_property('capture_on_movement',False);rt=unreal.RenderingLibrary.create_render_target2d(world,1600,1000,unreal.TextureRenderTargetFormat.RTF_RGBA8);c.set_editor_property('texture_target',rt);c.set_editor_property('capture_source',unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR);c.set_editor_property('fov_angle',65);c.set_editor_property('max_view_distance_override',100000.);targets.append(rt);state['current']=(q,a,c,rt);state['phase']='capture';state['hold']=35
   elif state['phase']=='capture':
    unreal.AutomationLibrary.finish_loading_before_screenshot();state['current'][2].capture_scene();state['phase']='export';state['hold']=10
   elif state['phase']=='export':
    q,a,c,rt=state['current'];directory=OUT/'renders';directory.mkdir(exist_ok=True);path=directory/(q['name']+'.png');unreal.RenderingLibrary.export_render_target(world,rt,str(directory),path.name);assert path.exists() and path.stat().st_size>5000;state['results'].append(dict(view=q['name'],path=str(path),sha256=sha(path),eye=q['eye'],target=q['target']));SUB.destroy_actor(a);state['index']+=1;state['phase']='setup';state['hold']=10
    if state['index']==len(views):
     unreal.unregister_slate_post_tick_callback(capture_handle);write(OUT/'render.json',dict(status='captured_requires_visual_review',images=state['results'],map_sha256=sha(ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap'),rendering='Saved native Unreal D3D12 lit world; no staged props or lighting changes; capture actors not saved'));finish()
  except BaseException:
   unreal.unregister_slate_post_tick_callback(capture_handle);write(OUT/'render-error.json',dict(error=traceback.format_exc()));unreal.log_error(traceback.format_exc());finish()
  finally:state['busy']=False
 capture_handle=unreal.register_slate_post_tick_callback(tick);KEEP.append(state);KEEP.append(targets)
def safe_cameras(views):
 world=unreal.EditorLevelLibrary.get_editor_world();buildings=read(ROOT/'Docs/StreetSocialCity/production/building-floor-audit.json')['buildings'];receipt=read(OUT/'install.json');actors={a.get_actor_label():a for a in SUB.get_all_level_actors()};result=[]
 for view in views:
  q=dict(view)
  if q['name']=='world-overview':q['camera_check']='overview above buildings';result.append(q);continue
  target=q['target'];initial=q['eye'];distance=math.dist(target,initial);bearing=math.atan2(initial[1]-target[1],initial[0]-target[0]);elevation=initial[2]-target[2];ignore=[]
  # Ignore only the intended focus object, since the camera must look at it.
  for g in receipt['groups']:
   if any(math.hypot(p['position_m'][0]-target[0],p['position_m'][1]-target[1])<2. for p in g['instances']):ignore.append(actors[g['label']])
  chosen=None
  for dh in [elevation,2.5,5.,8.]:
   for multiplier in [1.,.7,1.25]:
    for degrees in [0,45,-45,90,-90,135,-135,180]:
     angle=bearing+math.radians(degrees);horizontal=max(2.,math.sqrt(max(1.,distance*distance-elevation*elevation))*multiplier);eye=[target[0]+math.cos(angle)*horizontal,target[1]+math.sin(angle)*horizontal,target[2]+dh]
     if any(abs(eye[0]-b['world_position_m'][0])<b['footprint_m'][0]/2.+.6 and abs(eye[1]-b['world_position_m'][1])<b['footprint_m'][1]/2.+.6 for b in buildings):continue
     hit=unreal.SystemLibrary.sphere_trace_single(world,point(target),point(eye),35.,unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,ignore,unreal.DrawDebugTrace.NONE,True)
     if hit and hit.to_tuple()[0]:continue
     chosen=eye;break
    if chosen:break
   if chosen:break
  assert chosen,('No unobstructed native review camera',q['name']);q['eye']=chosen;q['camera_check']='outside building envelopes; native 0.35m-radius sweep to focus clear';result.append(q)
 return result
]====]
code=code:gsub('def finish%(%)%:',function()return extension..'\ndef finish():' end,1)
local payload=out..'/'..mode..'-payload.json'
local f=assert(io.open(payload,'wb'));f:write(json.encode({root=root,out=out,mode=mode,first=first,last=last,run_id=mode..'-'..os.date('%Y%m%d-%H%M%S'),source=root..'/Tools/street_city_resort_native.lua::UnrealPayload',code=code}));f:close()
print(payload)
