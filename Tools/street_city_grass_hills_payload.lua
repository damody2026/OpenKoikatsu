return [====[
GPREFIX='/Game/UniversityCity/GrassHillsV1';GMATERIAL='M_GrassWorldTriplanar_v2'
def grass_snapshot():return [mesh_state(a,c) for a,c in component_index().values()]
def grass_texture(channel):
 source=BASE/'world-ground-v1/assets/grass'/({'color':'basecolor','normal':'normal','roughness':'roughness'}[channel]+'-2k.png');name='T_Grass_'+channel;path=GPREFIX+'/Textures/'+name;t=EAL.load_asset(path) if EAL.does_asset_exist(path) else None
 if t and EAL.get_metadata_tag(t,'GrassSourceSHA256')==sha(source) and t.get_editor_property('srgb')==(channel=='color') and t.get_editor_property('max_texture_size')==2048:
  if channel!='normal' or (t.get_editor_property('compression_settings')==unreal.TextureCompressionSettings.TC_NORMALMAP and t.get_editor_property('flip_green_channel')):return t
 if not t:
  task=unreal.AssetImportTask();task.set_editor_property('filename',str(source));task.set_editor_property('destination_path',GPREFIX+'/Textures');task.set_editor_property('destination_name',name);task.set_editor_property('automated',True);task.set_editor_property('save',True);ATS.import_asset_tasks([task]);t=EAL.load_asset(path)
 assert t;t.set_editor_property('srgb',channel=='color');t.set_editor_property('max_texture_size',2048)
 if channel=='normal':t.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_NORMALMAP);t.set_editor_property('flip_green_channel',True)
 elif channel=='roughness':t.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_GRAYSCALE)
 EAL.set_metadata_tag(t,'GrassSourceSHA256',sha(source));save(t);return t

def grass_material():
 path=GPREFIX+'/'+GMATERIAL;m=EAL.load_asset(path) if EAL.does_asset_exist(path) else ATS.create_asset(GMATERIAL,GPREFIX,unreal.Material,unreal.MaterialFactoryNew());assert m;MEL.delete_all_material_expressions(m);m.set_editor_property('tangent_space_normal',False);m.set_editor_property('used_with_instanced_static_meshes',True)
 p=node(m,unreal.MaterialExpressionWorldPosition);n=interpolate(m,node(m,unreal.MaterialExpressionVertexNormalWS));size=node(m,unreal.MaterialExpressionScalarParameter,parameter_name='TileSizeCm',default_value=800);strength=node(m,unreal.MaterialExpressionScalarParameter,parameter_name='NormalStrength',default_value=.6);tintparam=node(m,unreal.MaterialExpressionVectorParameter,parameter_name='GrassTint',default_value=unreal.LinearColor(.78,.82,.74,1));uv=[]
 for expr in ['float2(P.y*sign(N.x+0.0001),P.z)','float2(-P.x*sign(N.y+0.0001),P.z)','float2(P.x*sign(N.z+0.0001),P.y)']:
  uv.append(custom(m,'return '+expr+'/max(S,1.0);',dict(P=(p,''),N=(n,''),S=(size,'')),unreal.CustomMaterialOutputType.CMOT_FLOAT2))
 maps={k:grass_texture(k) for k in ['color','normal','roughness']};colors=[];normals=[]
 for k in range(3):
  for channel,dest in [('color',colors),('normal',normals)]:
   s=node(m,unreal.MaterialExpressionTextureSampleParameter2D,parameter_name='Grass'+channel.title(),texture=maps[channel],sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL if channel=='normal' else unreal.MaterialSamplerType.SAMPLERTYPE_COLOR);coordinate=next(str(v) for v in MEL.get_material_expression_input_names(s) if str(v) in ['UVs','Coordinates']);wire(uv[k],'',s,coordinate);dest.append(s)
 albedo=custom(m,'float3 w=pow(abs(normalize(N)),4);w/=max(dot(w,float3(1,1,1)),0.0001);float macro=0.94+0.035*sin(P.x/2700.0+sin(P.y/1700.0))+0.025*sin(P.y/4300.0);float3 b=X*w.x+Y*w.y+Z*w.z;float l=dot(b,float3(.2126,.7152,.0722));return saturate(lerp(b,float3(l,l,l),.22)*Tint*macro);',dict(X=(colors[0],'RGB'),Y=(colors[1],'RGB'),Z=(colors[2],'RGB'),N=(n,''),P=(p,''),Tint=(tintparam,'RGB')));prop(albedo,'',unreal.MaterialProperty.MP_BASE_COLOR)
 normal=custom(m,'float3 g=normalize(N);float3 w=pow(abs(g),4);w/=max(dot(w,float3(1,1,1)),0.0001);float3 sx=sign(g+0.0001);float3 d=float3(0,X.x*sx.x,X.y)*w.x+float3(-Y.x*sx.y,0,Y.y)*w.y+float3(Z.x*sx.z,Z.y,0)*w.z;d-=g*dot(d,g);return normalize(g+d*Strength);',dict(X=(normals[0],'RGB'),Y=(normals[1],'RGB'),Z=(normals[2],'RGB'),N=(n,''),Strength=(strength,'')));prop(normal,'',unreal.MaterialProperty.MP_NORMAL)
 rough=node(m,unreal.MaterialExpressionTextureSampleParameter2D,parameter_name='GrassRoughness',texture=maps['roughness'],sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE);coordinate=next(str(v) for v in MEL.get_material_expression_input_names(rough) if str(v) in ['UVs','Coordinates']);wire(uv[2],'',rough,coordinate);r=custom(m,'return clamp(R,0.8,1.0);',dict(R=(rough,'R')),unreal.CustomMaterialOutputType.CMOT_FLOAT1);prop(r,'',unreal.MaterialProperty.MP_ROUGHNESS);scalar(m,0,unreal.MaterialProperty.MP_METALLIC)
 EAL.set_metadata_tag(m,'GrassNormalSpace','world-space reoriented triplanar; BC5 normal samples; OpenGL green converted to DirectX');EAL.set_metadata_tag(m,'CityTerrainGrass','grass-hills-v1');MEL.recompile_material(m);save(m);return m,maps

def grass_prepare():
 m,maps=grass_material();unreal.AutomationLibrary.finish_loading_before_screenshot();stats=MEL.get_statistics(m);assert stats.num_pixel_shader_instructions>0,'Grass shader compilation failed';write(OUT/'grass_prepare.json',dict(status='passed',material=m.get_path_name(),texture_paths={k:t.get_path_name() for k,t in maps.items()},pixel_instructions=stats.num_pixel_shader_instructions))

def grass_install():
 assert not (OUT/'grass_install.json').exists(),'Do not overwrite a completed installation';mapfile=ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap';startsha=sha(mapfile);assert startsha==WORLD_LOAD_SHA,'Another editor changed the map during loading; retry with latest world'
 if not (OUT/'baseline.json').exists():write(OUT/'baseline.json',dict(status='passed',rows=grass_snapshot(),map_sha256=startsha));shutil.copy2(mapfile,OUT/'UniversityCity-before-grass.umap')
 baseline=read(OUT/'baseline.json')
 if startsha!=baseline['map_sha256']:
  current=grass_snapshot();old={(r['actor'],r['component']):r for r in baseline['rows']};delta=[dict(actor=r['actor'],component=r['component'],old=old.get((r['actor'],r['component'])),new=r) for r in current if old.get((r['actor'],r['component']))!=r];live=component_index();tree_issues=[]
  approved=read(BASE/'tree-landscaping-v3/land_install.json')
  for q in approved['trees']:
   matches=[(a,c) for (actor,name),(a,c) in live.items() if actor==q['actor']]
   if not matches:tree_issues.append(dict(actor=q['actor'],reason='missing actor'));continue
   a,c=matches[0]
   if c.get_instance_count()<=q['index']:tree_issues.append(dict(actor=q['actor'],reason='missing instance'));continue
   t=c.get_instance_transform(q['index'],True);p=[t.translation.y/100,t.translation.x/100,t.translation.z/100]
   if math.dist(p,q['position'])>.001:tree_issues.append(dict(actor=q['actor'],index=q['index'],expected=q['position'],actual=p))
  write(OUT/'concurrent-drift.json',dict(status='recorded',old_sha256=baseline['map_sha256'],current_sha256=startsha,changes=delta,tree_issues=tree_issues));assert not tree_issues,'Concurrent map update reverted approved tree state; inspect concurrent-drift.json'
  write(OUT/('baseline-before-rebase-'+baseline['map_sha256'][:16]+'.json'),baseline);backup=OUT/('UniversityCity-before-grass-'+startsha[:16]+'.umap');shutil.copy2(mapfile,backup);baseline=dict(status='passed',rows=current,map_sha256=startsha,backup=str(backup),rebased_preserving_concurrent_changes=True);write(OUT/'baseline.json',baseline)
 prepared=read(OUT/'grass_prepare.json');assert prepared['status']=='passed';m=EAL.load_asset(prepared['material']);maps={k:EAL.load_asset(v) for k,v in prepared['texture_paths'].items()};unreal.AutomationLibrary.finish_loading_before_screenshot();stats=MEL.get_statistics(m);assert stats.num_pixel_shader_instructions>0,'Terrain shader did not compile';rows=[]
 for a,c in component_index().values():
  mesh=c.static_mesh;label=a.get_actor_label();slots=mesh.get_editor_property('static_materials')
  if not re.fullmatch(r'G\d+',label) and not any(k in label.lower() for k in ['mountain','hill','mound']):continue
  desired=[c.get_material(i) for i in range(c.get_num_materials())];changed=False
  for i,s in enumerate(slots):
   name=str(s.get_editor_property('imported_material_slot_name')).lower();old=c.get_material(i);original=EAL.get_metadata_tag(old,'CityArtOriginal') if old else '';source=(original or (old.get_path_name() if old else '')).lower()
   if any(k in name or k in source for k in ['ground-forest','ground-grass']):rows.append(dict(actor=label,component=c.get_name(),slot=i,name=name,old=old.get_path_name(),new=m.get_path_name()));desired[i]=m;changed=True
  if changed:set_tree_binding(a,c.get_name(),mesh,desired)
 assert len(rows)>=16,('Missing natural terrain slots',rows);assert sha(mapfile)==startsha,'Concurrent modification';assert unreal.EditorLevelLibrary.save_current_level();write(OUT/'grass_install.json',dict(status='passed',slots=rows,texture_paths={k:t.get_path_name() for k,t in maps.items()},material=m.get_path_name(),triangles_added=0,map_sha256=sha(mapfile)))

def grass_audit():
 baseline=read(OUT/'baseline.json');install=read(OUT/'grass_install.json');live=component_index();expected={(r['actor'],r['component'],r['slot']):r for r in install['slots']};checks=0
 for r in baseline['rows']:
  a,c=live[(r['actor'],r['component'])];now=mesh_state(a,c);want=dict(r,materials=r['materials'][:])
  for i in range(len(want['materials'])):
   q=expected.get((r['actor'],r['component'],i))
   if q:want['materials'][i]=q['new']
  assert now==want,('Unexpected terrain pass drift',r['actor'],r['component']);checks+=1
 m=EAL.load_asset(install['material']);normal=MEL.get_material_property_input_node(m,unreal.MaterialProperty.MP_NORMAL);assert normal and isinstance(normal,unreal.MaterialExpressionCustom);assert not m.get_editor_property('tangent_space_normal');stats=MEL.get_statistics(m);assert stats.num_pixel_shader_instructions>0
 for channel,path in install['texture_paths'].items():
  t=EAL.load_asset(path);assert t.blueprint_get_size_x()==2048 and t.blueprint_get_size_y()==2048;assert t.get_editor_property('srgb')==(channel=='color')
  if channel=='normal':assert t.get_editor_property('compression_settings')==unreal.TextureCompressionSettings.TC_NORMALMAP and t.get_editor_property('flip_green_channel')
  checks+=1
 assert len(live)==len(baseline['rows']);write(OUT/'grass_audit.json',dict(status='passed',checks=checks,changed_slots=len(install['slots']),normal_connected=True,normal_compression='BC5 / TC_NORMALMAP',normal_space='world',texture_resolution=2048,triangles_added=0,material_sha256=sha(ROOT/'StreetSocialCity/Content'/(m.get_path_name().split('.')[0].replace('/Game/','')+'.uasset')),shader=dict(pixel=stats.num_pixel_shader_instructions,vertex=stats.num_vertex_shader_instructions,samplers=stats.num_samplers),map_sha256=sha(ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap')))

def grass_render():grass_audit();unreal.SystemLibrary.execute_console_command(unreal.EditorLevelLibrary.get_editor_world(),'r.DepthOfFieldQuality 0');capture_trees()

def grass_merge():
 # Reconcile only owned terrain slots after a concurrent editor save; preserve its complete current world.
 mapfile=ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap';start=sha(mapfile);assert start==WORLD_LOAD_SHA,'Map changed during load';install=read(OUT/'grass_install.json');current=grass_snapshot();live=component_index();approved=read(BASE/'tree-landscaping-v3/land_install.json')
 for q in approved['trees']:
  a,c=next(pair for key,pair in live.items() if key[0]==q['actor']);assert c.get_instance_count()>q['index'];t=c.get_instance_transform(q['index'],True);assert math.dist([t.translation.y/100,t.translation.x/100,t.translation.z/100],q['position'])<.001,'Concurrent tree regression'
 stamp=start[:16];write(OUT/('baseline-before-merge-'+stamp+'.json'),read(OUT/'baseline.json'));write(OUT/('grass_install-before-merge-'+stamp+'.json'),install);backup=OUT/('UniversityCity-before-merge-'+stamp+'.umap');shutil.copy2(mapfile,backup);write(OUT/'baseline.json',dict(status='passed',rows=current,map_sha256=start,backup=str(backup),concurrent_merge=True));m=EAL.load_asset(install['material']);changed=[];grouped={}
 for q in install['slots']:grouped.setdefault((q['actor'],q['component']),[]).append(q)
 for key,requests in grouped.items():
  a,c=live[key];mesh=c.static_mesh;materials=[c.get_material(i) for i in range(c.get_num_materials())]
  for q in requests:
   old=materials[q['slot']];q['old']=old.get_path_name();q['new']=m.get_path_name()
   if old!=m:changed.append(dict(actor=q['actor'],slot=q['slot'],old=q['old'],new=q['new']));materials[q['slot']]=m
  if any(q['actor']==key[0] for q in changed):set_tree_binding(a,key[1],mesh,materials)
 assert sha(mapfile)==start,'Concurrent map modification while merging'
 if changed:
  saved=False
  for attempt in range(8):
   if sha(mapfile)!=start:raise RuntimeError('Concurrent editor saved while waiting; reload before merging')
   if unreal.EditorLevelLibrary.save_current_level():saved=True;break
   time.sleep(4)
  assert saved,'Another editor still holds the map'
 install.update(map_sha256=sha(mapfile),latest_merge=dict(previous_sha256=start,rebound_slots=changed));write(OUT/'grass_install.json',install);grass_audit();unreal.SystemLibrary.execute_console_command(unreal.EditorLevelLibrary.get_editor_world(),'r.DepthOfFieldQuality 0');capture_trees()

def grass_current():
 # Global umap bytes may change while other users work. Verify the owned bindings themselves, read-only.
 mapfile=ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap';loaded=WORLD_LOAD_SHA
 for attempt in range(3):
  if sha(mapfile)==loaded:break
  assert unreal.EditorLevelLibrary.load_level('/Engine/Maps/Entry');unreal.SystemLibrary.collect_garbage();loaded=sha(mapfile);assert unreal.EditorLevelLibrary.load_level(MAP)
 assert sha(mapfile)==loaded,'Map kept changing during read; no stale verification'
 receipt=read(OUT/'grass_install.json');live=component_index();bindings=[]
 for q in receipt['slots']:
  a,c=live[(q['actor'],q['component'])];m=c.get_material(q['slot']);assert m and m.get_path_name()==receipt['material'],('Terrain material reverted',q['actor'],q['slot']);bindings.append([q['actor'],q['component'],q['slot'],m.get_path_name()])
 m=EAL.load_asset(receipt['material']);assert MEL.get_material_property_input_node(m,unreal.MaterialProperty.MP_NORMAL);assert not m.get_editor_property('tangent_space_normal');t=EAL.load_asset(receipt['texture_paths']['normal']);assert not t.get_editor_property('srgb') and t.get_editor_property('compression_settings')==unreal.TextureCompressionSettings.TC_NORMALMAP and t.get_editor_property('flip_green_channel');assert t.blueprint_get_size_x()==t.blueprint_get_size_y()==2048
 material_sha=sha(ROOT/'StreetSocialCity/Content'/(m.get_path_name().split('.')[0].replace('/Game/','')+'.uasset'));assert material_sha==read(OUT/'grass_audit.json')['material_sha256'];assert sha(mapfile)==loaded,'Map changed during owned-state verification'
 write(OUT/'grass_current.json',dict(status='passed',owned_slots=len(bindings),normal_connected=True,normal_resolution=2048,material_sha256=material_sha,owned_bindings_sha256=hashlib.sha256(json.dumps(sorted(bindings)).encode()).hexdigest(),map_sha256=loaded,read_only=True,scope='Latest on-disk world: exact terrain material bindings and normal; unrelated concurrent saves do not require re-rendering equivalent terrain'))
]====]
