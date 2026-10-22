return [====[
import shutil,statistics
SERIES=PAYLOAD.get('series','v1');PREFIX='/Game/UniversityCity/TreeUpgrade'+SERIES.upper();TAG='TreeUpgrade-'+SERIES;IDS=[q[0] for q in PAYLOAD.get('species',[])] or ['camphor-a','camphor-b','camphor-c','flower-a']
def vec(v):return [v.x,v.y,v.z]
def tf(t):
 if isinstance(t,unreal.Actor):return tf(t.get_actor_transform())
 r=t.rotation;return dict(location=vec(t.translation),rotation=[r.x,r.y,r.z,r.w],scale=vec(t.scale3d))
def mesh_state(a,c,instances=False):
 m=c.static_mesh;v=dict(actor=a.get_actor_label(),component=c.get_name(),mesh=m.get_path_name(),actor_transform=tf(a),component_transform=tf(c.get_relative_transform()),visible=c.is_visible(),hidden=c.get_editor_property('hidden_in_game'),collision=str(c.get_collision_profile_name()),materials=[c.get_material(i).get_path_name() if c.get_material(i) else None for i in range(c.get_num_materials())],cast_shadow=c.get_editor_property('cast_shadow'))
 if isinstance(c,unreal.InstancedStaticMeshComponent):
  ts=[tf(c.get_instance_transform(i,True)) for i in range(c.get_instance_count())];v.update(count=len(ts),instances_sha256=hashlib.sha256(json.dumps(ts,sort_keys=True).encode()).hexdigest(),cull_start=c.get_editor_property('instance_start_cull_distance'),cull_end=c.get_editor_property('instance_end_cull_distance'))
  if instances:v['instances']=ts
 return v
def tree_kind(m):
 p=m.get_path_name().lower()
 if '/assets/camphor/' in p:return 'camphor'
 if '/assets/cherry/' in p:return 'cherry'
 if '/treeupgradev1/assets/' in p or '/treeupgradev2/assets/' in p:return 'cherry' if EAL.get_metadata_tag(m,'TreeUpgradeId') in ['flower-a','golden-flower','white-flower'] else 'camphor'
 return None
def tree_probe():
 trees=[];other=[];materials={}
 for a in SUB.get_all_level_actors():
  for c in a.get_components_by_class(unreal.StaticMeshComponent):
   if not c.static_mesh:continue
   kind=tree_kind(c.static_mesh);r=mesh_state(a,c,bool(kind))
   if kind:
    r.update(kind=kind,lod_triangles=[c.static_mesh.get_num_triangles(i) for i in range(API.get_lod_count(c.static_mesh))]);trees.append(r)
    for i in range(c.get_num_materials()):
     m=c.get_material(i)
     if not m:continue
     if m.get_path_name() not in materials:
      names=MEL.get_vector_parameter_names(m) if isinstance(m,unreal.MaterialInstance) else [];values={}
      for n in names:
       v=MEL.get_material_instance_vector_parameter_value(m,n);values[str(n)]=[v.r,v.g,v.b,v.a]
      materials[m.get_path_name()]=dict(vectors=values)
   else:other.append(r)
 assert trees and not (OUT/'install.json').exists(),'Baseline may only be recorded before installation'
 write(OUT/'baseline.json',dict(status='passed',trees=trees,other=other,materials=materials,map_sha256=sha(ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap')))
 write(OUT/'tree_probe.json',dict(status='passed',tree_components=len(trees),tree_instances=sum(r.get('count',1) for r in trees if r['visible'] and not r['hidden']),non_tree_components=len(other)))
def capture_trees():
 baseline=read(OUT/'baseline.json');planpath=OUT/'views.json'
 if planpath.exists():views=read(planpath)['views']
 else:
  candidates=[]
  for r in baseline['trees']:
   if not r['visible'] or r['hidden']:continue
   for t in r.get('instances',[r['actor_transform']]):
    p=t['location'];candidates.append(([p[1]/100,p[0]/100,p[2]/100],t['scale'][2],r['kind']))
  views=[]
  zones=[('campus',[600,800]),('shops',[370,400]),('forest',[350,990])]+([('resort',[600,200]),('waterfront',[900,450])] if SERIES=='v2' else [])
  for name,xy in zones:
   p,s,k=min(candidates,key=lambda t:(t[0][0]-xy[0])**2+(t[0][1]-xy[1])**2)
   for label,d in [('near',16),('middle',65),('far',170)]:
    d*=max(.65,s);target=[p[0],p[1],p[2]+4*s];options=[]
    for angle in range(0,360,30):
     rad=math.radians(angle);eye=[p[0]+d*math.cos(rad),p[1]+d*math.sin(rad),p[2]+4*s+d*.15];hit=unreal.SystemLibrary.line_trace_single(unreal.EditorLevelLibrary.get_editor_world(),point(eye),point(target),unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,[],unreal.DrawDebugTrace.NONE,True);blocked=bool(hit and hit.to_tuple()[0]);options.append((blocked,angle,eye))
    blocked,angle,eye=min(options,key=lambda t:(t[0],abs(t[1]-300)));views.append(dict(name=name+'-'+label,eye=eye,target=target,kind=k,occluded=blocked))
  write(planpath,dict(views=views))
 full_views=views;selected=set((PAYLOAD.get('capture_ids') or '').split(','))-{''}
 if selected:views=[q for q in views if q['name'] in selected];assert len(views)==len(selected),'Unknown capture id'
 paired=MODE=='tree_compare';current=component_index();installed=read(OUT/'install.json') if paired else None;adopted={key:mesh_state(*current[key]) for key in current} if paired else {};original=views;views=[dict(q,variant=variant) for q in original for variant in ['before','after']] if paired else views
 def switch(variant):
  live=component_index()
  for old in baseline['trees']:
   key=(old['actor'],old['component']);a,c=live[key];row=old if variant=='before' else adopted[key];set_tree_binding(a,key[1],EAL.load_asset(row['mesh']),[EAL.load_asset(p) if p else None for p in row['materials']])
  unreal.EditorLevelLibrary.editor_invalidate_viewports()
 world=unreal.EditorLevelLibrary.get_editor_world();state=dict(i=0,phase='setup',wait=1,images=[]);folder=OUT/('before' if MODE=='tree_before' else 'after');folder.mkdir(exist_ok=True)
 for cmd in ['r.GPUCsvStatsEnabled 1','r.VSync 0','t.MaxFPS 0']:unreal.SystemLibrary.execute_console_command(world,cmd)
 def tick(dt):
  try:
   state['wait']-=1
   if state['wait']>0:return
   if state['phase']=='setup':
    if state['i']==len(views):
     if paired:
      compare_preserved(baseline,installed)
      for variant in ['before','after']:
       file=OUT/('tree_compare_'+variant+'.json');images=[r for r in state['images'] if r['variant']==variant]
       if selected and file.exists():
        merged={r['camera']['name']:r for r in read(file)['images']};merged.update({r['camera']['name']:r for r in images});images=[merged[q['name']] for q in full_views]
       write(file,dict(status='passed',images=images,profiling_dir=unreal.Paths.profiling_dir(),repaired_capture_ids=sorted(selected),map_sha256=sha(ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap')))
     write(OUT/(MODE+'.json'),dict(status='passed',images=state['images'],profiling_dir=unreal.Paths.profiling_dir(),map_sha256=sha(ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap')));unreal.unregister_slate_post_tick_callback(state['handle']);unreal.SystemLibrary.quit_editor();return
    q=views[state['i']]
    if paired:switch(q['variant'])
    eye=point(q['eye']);rot=unreal.MathLibrary.find_look_at_rotation(eye,point(q['target']));unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(eye,rot);a=SUB.spawn_actor_from_class(unreal.SceneCapture2D,eye,rot);a.set_actor_location(eye,False,True);a.set_actor_rotation(rot,False);c=a.capture_component2d;c.set_editor_property('capture_every_frame',True);c.set_editor_property('capture_on_movement',False);c.set_editor_property('always_persist_rendering_state',True);rt=unreal.RenderingLibrary.create_render_target2d(world,1440,1000,unreal.TextureRenderTargetFormat.RTF_RGBA8);KEEP.append(rt);c.set_editor_property('texture_target',rt);c.set_editor_property('capture_source',unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR);c.set_editor_property('fov_angle',65)
    c.set_editor_property('capture_every_frame',False);pp=c.get_editor_property('post_process_settings')
    if unreal.SystemLibrary.get_console_variable_int_value('r.DynamicGlobalIlluminationMethod')==1:pp.set_editor_property('override_dynamic_global_illumination_method',True);pp.set_editor_property('dynamic_global_illumination_method',unreal.DynamicGlobalIlluminationMethod.LUMEN)
    if unreal.SystemLibrary.get_console_variable_int_value('r.ReflectionMethod')==1:pp.set_editor_property('override_reflection_method',True);pp.set_editor_property('reflection_method',unreal.ReflectionMethod.LUMEN)
    c.set_editor_property('post_process_settings',pp);state.update(current=(q,a,c,rt),phase='warm',wait=90)
   elif state['phase']=='warm':
    filename=MODE+'-'+views[state['i']]['name']+'-'+views[state['i']].get('variant','')+'-'+PAYLOAD['run_id']+'.csv';unreal.SystemLibrary.execute_console_command(world,'CsvProfile STARTFILE='+filename);unreal.SystemLibrary.execute_console_command(world,'CsvProfile START');state.update(phase='measure',wait=90,times=[],csv=filename)
   elif state['phase']=='measure':
    unreal.SystemLibrary.execute_console_command(world,'CsvProfile STOP');q,a,c,rt=state['current'];c.set_editor_property('capture_every_frame',False);c.capture_scene();state.update(phase='export',wait=12)
   elif state['phase']=='export':
    q,a,c,rt=state['current'];dest=OUT/(q['variant']+'-paired') if paired else folder;dest.mkdir(exist_ok=True);p=dest/(q['name']+'.png');unreal.RenderingLibrary.export_render_target(world,rt,str(dest),p.name);assert p.exists() and p.stat().st_size>5000;times=state['times'];state['images'].append(dict(camera={k:v for k,v in q.items() if k!='variant'},variant=q.get('variant'),path=str(p),sha256=sha(p),csv=state['csv'],callback_ms_p50=statistics.median(times) if times else None));c.set_editor_property('texture_target',None);SUB.destroy_actor(a);unreal.RenderingLibrary.release_render_target2d(rt);KEEP.remove(rt);state.update(i=state['i']+1,phase='setup',wait=10)
  except BaseException:
   write(OUT/(MODE+'-error.json'),dict(status='failed',error=traceback.format_exc()));unreal.log_error(traceback.format_exc());unreal.unregister_slate_post_tick_callback(state['handle']);unreal.SystemLibrary.quit_editor()
 def wrapper(dt):
  if state.get('current') and state['phase'] in ['warm','measure']:state['current'][2].capture_scene()
  if state['phase']=='measure':state['times'].append(dt*1000)
  tick(dt)
 state['handle']=unreal.register_slate_post_tick_callback(wrapper)

def tree_before():capture_trees()
def tree_after():tree_audit();capture_trees()
def tree_compare():tree_audit();capture_trees()

def save(a):assert EAL.save_loaded_asset(a)

_import_source=(ROOT/'Tools/street_city_activity_native_payload.lua').read_text('utf8').split('def import_one(',1)[1].split('def simple_water(',1)[0]
exec('def import_one('+_import_source.replace("pipeline.get_editor_property('material_pipeline').set_editor_property('import_materials',materials)","pipeline.get_editor_property('material_pipeline').set_editor_property('import_materials',materials);pipeline.get_editor_property('material_pipeline').get_editor_property('texture_pipeline').set_editor_property('import_textures',False)"))

def tex(path,name,data=False):
 dest=PREFIX+'/Textures/'+name;t=EAL.load_asset(dest) if EAL.does_asset_exist(dest) else None
 if not t or EAL.get_metadata_tag(t,'TreeSourceSHA256')!=sha(path):
  task=unreal.AssetImportTask();task.set_editor_property('filename',str(path));task.set_editor_property('destination_path',PREFIX+'/Textures');task.set_editor_property('destination_name',name);task.set_editor_property('automated',True);task.set_editor_property('replace_existing',True);task.set_editor_property('save',True);ATS.import_asset_tasks([task]);t=EAL.load_asset(dest)
 assert t;t.set_editor_property('srgb',not data);t.set_editor_property('max_texture_size',2048);t.set_editor_property('compression_no_alpha',False);t.set_editor_property('power_of_two_mode',unreal.TexturePowerOfTwoSetting.STRETCH_TO_POWER_OF_TWO)
 if data:t.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_MASKS if name.endswith('_AtlasNormal') else unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP)
 EAL.set_metadata_tag(t,'TreeSourceSHA256',sha(path));save(t);return t

def mat(name):
 path=PREFIX+'/Materials/'+name;m=EAL.load_asset(path) if EAL.does_asset_exist(path) else ATS.create_asset(name,PREFIX+'/Materials',unreal.Material,unreal.MaterialFactoryNew());assert m;MEL.delete_all_material_expressions(m);m.set_editor_property('used_with_instanced_static_meshes',True);m.set_editor_property('dithered_lod_transition',True);return m

def node(m,cls,**props):
 n=MEL.create_material_expression(m,cls)
 for k,v in props.items():n.set_editor_property(k,v)
 return n

def wire(a,out,b,inp):assert MEL.connect_material_expressions(a,out,b,'' if inp=='Input' else inp),(a,out,b,inp,MEL.get_material_expression_input_names(b))
def prop(a,out,p):assert MEL.connect_material_property(a,out,p)
def scalar(m,value,p):prop(node(m,unreal.MaterialExpressionConstant,r=value),'',p)
def sample(m,t,data=False):return node(m,unreal.MaterialExpressionTextureSample,texture=t,sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR if data else unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
def custom(m,code,inputs,typ=None):
 n=node(m,unreal.MaterialExpressionCustom,code=code,output_type=typ or unreal.CustomMaterialOutputType.CMOT_FLOAT3,inputs=[custom_input(k) for k in inputs])
 for k,(src,out) in inputs.items():wire(src,out,n,k)
 return n

def tint(m,base,out='RGB'):
 p=node(m,unreal.MaterialExpressionVectorParameter,parameter_name='LeafTint',default_value=unreal.LinearColor(1,1,1,1));random=node(m,unreal.MaterialExpressionPerInstanceRandom);n=custom(m,'return Base*Tint*lerp(0.92,1.06,R);',dict(Base=(base,out),Tint=(p,'RGB'),R=(random,'')));prop(n,'',unreal.MaterialProperty.MP_BASE_COLOR);return p

def near_materials():
 bark=mat('M_Bark');color=sample(bark,tex(OUT/'textures/bark-color.png','BarkColor'));prop(color,'RGB',unreal.MaterialProperty.MP_BASE_COLOR);rough=sample(bark,tex(OUT/'textures/bark-roughness.png','BarkRoughness',True),True);prop(rough,'R',unreal.MaterialProperty.MP_ROUGHNESS);normal=sample(bark,tex(OUT/'textures/bark-normal.png','BarkNormal',True),True);decoded=custom(bark,'return normalize(N*2-1);',dict(N=(normal,'RGB')));prop(decoded,'',unreal.MaterialProperty.MP_NORMAL);scalar(bark,0,unreal.MaterialProperty.MP_METALLIC);MEL.recompile_material(bark);save(bark)
 leaves=mat('M_Leaves');leaves.set_editor_property('blend_mode',unreal.BlendMode.BLEND_MASKED);leaves.set_editor_property('two_sided',True);leaves.set_editor_property('opacity_mask_clip_value',.33);leaves.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE);t=tex(BASE/'streetscape-v1/textures/camphor-leaves.png','LeafRGBA');t.set_editor_property('do_scale_mips_for_alpha_coverage',True);t.set_editor_property('alpha_coverage_thresholds',unreal.Vector4(0,0,0,.33));save(t);s=sample(leaves,t);tint(leaves,s);prop(s,'A',unreal.MaterialProperty.MP_OPACITY_MASK);sub=custom(leaves,'return C*0.28;',dict(C=(s,'RGB')));prop(sub,'',unreal.MaterialProperty.MP_SUBSURFACE_COLOR);scalar(leaves,.88,unreal.MaterialProperty.MP_ROUGHNESS);scalar(leaves,0,unreal.MaterialProperty.MP_METALLIC);MEL.recompile_material(leaves);save(leaves)
 flower=mat('M_Flowers');base=node(flower,unreal.MaterialExpressionConstant3Vector,constant=unreal.LinearColor(.78,.38,.47,1));tint(flower,base,'');scalar(flower,.88,unreal.MaterialProperty.MP_ROUGHNESS);scalar(flower,0,unreal.MaterialProperty.MP_METALLIC);MEL.recompile_material(flower);save(flower);return bark,leaves,flower

def worldpoint(m,p):
 n=node(m,unreal.MaterialExpressionConstant3Vector,constant=unreal.LinearColor(*p,1));trans=node(m,unreal.MaterialExpressionTransformPosition,transform_source_type=unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_INSTANCE,transform_type=unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD);wire(n,'',trans,'Input');return trans

def interpolate(m,n):
 v=node(m,unreal.MaterialExpressionVertexInterpolator);wire(n,'',v,'Input');return v

def impostor_material(aid,spec):
 m=mat('M_Impostor_'+aid.replace('-','_'));m.set_editor_property('blend_mode',unreal.BlendMode.BLEND_MASKED);m.set_editor_property('opacity_mask_clip_value',.33);m.set_editor_property('two_sided',True);m.set_editor_property('tangent_space_normal',False);m.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
 height=spec['height_m']*100;extent=spec['extent_m']*100;origin=worldpoint(m,[0,0,0]);center=worldpoint(m,[0,0,height*.5]);axisx=worldpoint(m,[100,0,0]);axisy=worldpoint(m,[0,100,0]);axisz=worldpoint(m,[0,0,100]);uv=node(m,unreal.MaterialExpressionTextureCoordinate);cam=node(m,unreal.MaterialExpressionCameraPositionWS);pos=node(m,unreal.MaterialExpressionWorldPosition)
 wpo=custom(m,'float3 V=normalize(Cam-Center);float3 R=-normalize(cross(float3(0,0,1),V));float3 U=normalize(cross(R,V));float sx=length(AX-O)/100;float sz=length(AZ-O)/100;return Center+R*(UV.x-.5)*'+str(extent)+'*sx+U*(.5-UV.y)*'+str(extent)+'*sz-P;',dict(Cam=(cam,''),Center=(center,''),O=(origin,''),AX=(axisx,''),AZ=(axisz,''),UV=(uv,''),P=(pos,'')));prop(wpo,'',unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
 view=interpolate(m,custom(m,'return normalize(Cam-C);',dict(Cam=(cam,''),C=(center,''))))
 framecalc=custom(m,'float3 V=normalize(Cam-C);float3 ax=normalize(X-O),ay=normalize(Y-O);float a=frac(atan2(-dot(V,ay),dot(V,ax))/6.28318530718+1)*8;return fmod(floor(a+.5),8)+step(.3826834,V.z)*8;',dict(Cam=(cam,''),C=(center,''),O=(origin,''),X=(axisx,''),Y=(axisy,'')),unreal.CustomMaterialOutputType.CMOT_FLOAT1);frame=interpolate(m,framecalc)
 ct=node(m,unreal.MaterialExpressionTextureObjectParameter,parameter_name='AtlasColor',texture=tex(spec['color'],aid+'_AtlasColor'));nt=node(m,unreal.MaterialExpressionTextureObjectParameter,parameter_name='AtlasNormal',texture=tex(spec['normal_mask'],aid+'_AtlasNormal',True),sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
 inputs=dict(View=(view,''),Frame=(frame,''),UV=(uv,''),ColorTex=(ct,''),NormalTex=(nt,''))
 code="float3 V=normalize(View);float i=floor(Frame+.5);float2 uv=(float2(fmod(i,4),3-floor(i/4))+clamp(UV,.002,.998))/4;float4 c=Texture2DSample(ColorTex,ColorTexSampler,uv);float4 normal=Texture2DSample(NormalTex,NormalTexSampler,uv);float3 rgb=c.rgb;float alpha=c.a;"
 rgba=custom(m,code+'return float4(rgb,alpha);',inputs,unreal.CustomMaterialOutputType.CMOT_FLOAT4);rgbmask=node(m,unreal.MaterialExpressionComponentMask,r=True,g=True,b=True,a=False);alphamask=node(m,unreal.MaterialExpressionComponentMask,r=False,g=False,b=False,a=True);wire(rgba,'',rgbmask,'Input');wire(rgba,'',alphamask,'Input');leafmask=custom(m,code+'return normal.a;',inputs,unreal.CustomMaterialOutputType.CMOT_FLOAT1);p=node(m,unreal.MaterialExpressionVectorParameter,parameter_name='LeafTint',default_value=unreal.LinearColor(1,1,1,1));random=node(m,unreal.MaterialExpressionPerInstanceRandom);base=custom(m,'return Base*lerp(float3(1,1,1),Tint*lerp(.92,1.06,R),Mask);',dict(Base=(rgbmask,''),Tint=(p,'RGB'),Mask=(leafmask,''),R=(random,'')));prop(base,'',unreal.MaterialProperty.MP_BASE_COLOR);prop(alphamask,'',unreal.MaterialProperty.MP_OPACITY_MASK);sub=custom(m,'return C*.28*Mask;',dict(C=(base,''),Mask=(leafmask,'')));prop(sub,'',unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
 normal=custom(m,code+'float3 N=normalize(normal.rgb*2-1);float3 right=-normalize(cross(float3(0,0,1),V));float3 up=normalize(cross(right,V));return normalize(right*N.x+up*N.y+V*N.z);',inputs);prop(normal,'',unreal.MaterialProperty.MP_NORMAL);scalar(m,.88,unreal.MaterialProperty.MP_ROUGHNESS);scalar(m,0,unreal.MaterialProperty.MP_METALLIC);MEL.recompile_material(m);save(m);return m

def tree_import():
 from street_city_remesh_gate import verify
 bark,leaves,flower=near_materials();rows={}
 reused=read(BASE/'tree-upgrade-v1/tree_import.json')['assets'] if SERIES=='v2' else {}
 flower.set_editor_property('two_sided',True);MEL.recompile_material(flower);save(flower)
 for aid in IDS:
  if aid in reused:rows[aid]=dict(reused[aid],reused=True);continue
  folder=OUT/'assets'/aid;spec=read(folder/'atlas.json');impostor=impostor_material(aid,spec);meshes=[];receipts=[]
  for lod in range(4):
   r=verify(folder/('LOD'+str(lod)+'-remesh'));receipts.append(r);mesh=import_one(r['path'],aid,lod,False)
   if lod==0 and API.get_lod_count(mesh)>1:
    assert API.remove_lods(mesh);mesh.set_editor_property('static_materials',[s for s in mesh.get_editor_property('static_materials') if 'Impostor' not in str(s.get_editor_property('material_slot_name'))])
   slots=mesh.get_editor_property('static_materials')
   for i,s in enumerate(slots):
    name=str(s.get_editor_property('material_slot_name'));material=impostor if lod==3 else bark if 'Bark' in name else flower if spec.get('flower',aid=='flower-a') else leaves;mesh.set_material(i,material)
   save(mesh);meshes.append(mesh)
  mesh=meshes[0]
  for lod,source in enumerate(meshes[1:],1):assert API.set_lod_from_static_mesh(mesh,lod,source,0,True)==lod
  assert API.set_lod_screen_sizes(mesh,[1.,.60,.32,.18]);mesh.set_editor_property('positive_bounds_extension',unreal.Vector(250,250,250));mesh.set_editor_property('negative_bounds_extension',unreal.Vector(250,250,250));EAL.set_metadata_tag(mesh,'TreeUpgradeId',aid);save(mesh);counts=[mesh.get_num_triangles(i) for i in range(API.get_lod_count(mesh))];assert len(counts)==4 and counts[-1]<=16,(aid,counts);rows[aid]=dict(asset=mesh.get_path_name(),triangles=counts,materials=[mesh.get_material(i).get_path_name() for i in range(len(mesh.get_editor_property('static_materials')))],remesh=receipts,spec=spec)
  write(OUT/'tree_import-progress.json',dict(status='running',assets=rows))
 write(OUT/'tree_import.json',dict(status='passed',assets=rows))

def component_index():return {(a.get_actor_label(),c.get_name()):(a,c) for a in SUB.get_all_level_actors() for c in a.get_components_by_class(unreal.StaticMeshComponent) if c.static_mesh}

def set_tree_binding(actor,name,mesh,materials):
 c=next(q for q in actor.get_components_by_class(unreal.StaticMeshComponent) if q.get_name()==name);c.set_static_mesh(mesh);c.set_editor_property('override_materials',materials);c=next(q for q in actor.get_components_by_class(unreal.StaticMeshComponent) if q.get_name()==name)
 assert c.static_mesh==mesh,(actor.get_actor_label(),'reconstructed mesh mismatch');assert [c.get_material(i) for i in range(c.get_num_materials())]==materials,(actor.get_actor_label(),'reconstructed material mismatch');return c

def species_region(r):
 ts=r.get('instances',[r['actor_transform']]);east=sum(t['location'][1] for t in ts)/len(ts)/100;north=sum(t['location'][0] for t in ts)/len(ts)/100
 if north>=970:return 'forest'
 if north>=600:return 'campus'
 if east>=680:return 'waterfront'
 if north<330:return 'resort'
 return 'shopping'

def select_species(r):
 region=species_region(r);key=r['actor']+'|'+r['component'];seed=int(hashlib.sha256(key.encode()).hexdigest()[:12],16)
 if r['kind']=='cherry':pool=['flower-a','golden-flower','white-flower']
 else:pool={'forest':['camphor-a','banyan','cypress','columnar','camphor-c'],'campus':['camphor-a','camphor-b','banyan','terminalia','columnar','camphor-c'],'waterfront':['willow','willow','palm','terminalia','cypress'],'resort':['palm','palm','banyan','terminalia','willow'],'shopping':['terminalia','columnar','camphor-b','banyan','palm']}[region]
 return pool[seed%len(pool)]

def compare_preserved(baseline,installed=None):
 current=component_index();issues=[];checked=0
 for r in baseline['other']:
  key=(r['actor'],r['component']);assert key in current,key;a,c=current[key];now=mesh_state(a,c);checked+=1
  if now!=r:issues.append(dict(key=key,expected=r,actual=now))
 for r in baseline['trees']:
  key=(r['actor'],r['component']);assert key in current,key;a,c=current[key];now=mesh_state(a,c);old={k:v for k,v in r.items() if k not in ['mesh','materials','kind','lod_triangles','instances']};actual={k:v for k,v in now.items() if k not in ['mesh','materials']};checked+=1
  if old!=actual:issues.append(dict(key=key,expected=old,actual=actual))
  if installed:
   expected=installed['components']['|'.join(key)];assert now['mesh']==expected['asset'],(key,'mesh',now['mesh'],expected['asset'])
   if 'materials' in expected:assert now['materials']==expected['materials'],(key,'materials',now['materials'],expected['materials'])
 assert len(current)==len(baseline['other'])+len(baseline['trees']),('Component count changed',len(current));assert not issues,str(issues[:2]);return checked

def tree_install():
 baseline=read(OUT/'baseline.json');imports=read(OUT/'tree_import.json');assert imports['status']=='passed';idx=component_index();records={};cache={};mapfile=ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap';backup=OUT/'UniversityCity-before-trees.umap'
 for row in imports['assets'].values():
  if row.get('reused'):continue
  mesh=EAL.load_asset(row['asset']);assert API.set_lod_screen_sizes(mesh,[1.,.60,.32,.18]);save(mesh)
  for i in range(len(mesh.get_editor_property('static_materials'))):
   m=mesh.get_material(i);m.set_editor_property('dithered_lod_transition',True);MEL.recompile_material(m);save(m)
 leaf=EAL.load_asset(PREFIX+'/Textures/LeafRGBA');leaf.set_editor_property('power_of_two_mode',unreal.TexturePowerOfTwoSetting.STRETCH_TO_POWER_OF_TWO);save(leaf)
 if not backup.exists():assert sha(mapfile)==baseline['map_sha256'];shutil.copy2(mapfile,backup)
 for r in baseline['trees']:
  key=(r['actor'],r['component']);a,c=idx[key];aid=select_species(r) if SERIES=='v2' else 'flower-a' if r['kind']=='cherry' else ['camphor-a','camphor-b','camphor-c'][int(hashlib.sha256('|'.join(key).encode()).hexdigest()[:8],16)%3];mesh=EAL.load_asset(imports['assets'][aid]['asset']);t=[1,1,1,1]
  for p in r['materials']:
   if p and 'LeafTint' in baseline['materials'][p]['vectors']:t=baseline['materials'][p]['vectors']['LeafTint'];break
  bindings=[]
  for i in range(len(mesh.get_editor_property('static_materials'))):
   parent=mesh.get_material(i)
   if parent.get_name()=='M_Bark':bindings.append(parent);continue
   spec=imports['assets'][aid]['spec'];effective=list(t)
   if 'Impostor' not in parent.get_name():
    factor=[x/y for x,y in zip(spec.get('canopy_color',[.78,.38,.47]),[.78,.38,.47])] if spec.get('flower',aid=='flower-a') else spec.get('leaf_factor',[1,1,1]);effective=[t[i]*factor[i] for i in range(3)]+[t[3]]
   mid=parent.get_name()+'_'+hashlib.sha256((parent.get_path_name()+json.dumps(effective)).encode()).hexdigest()[:10]
   if mid not in cache:
    path=PREFIX+'/Materials/MI_'+mid;m=EAL.load_asset(path) if EAL.does_asset_exist(path) else ATS.create_asset('MI_'+mid,PREFIX+'/Materials',unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew());MEL.set_material_instance_parent(m,parent);set_vector(m,'LeafTint',unreal.LinearColor(*effective));save(m);cache[mid]=m
   bindings.append(cache[mid])
  c=set_tree_binding(a,key[1],mesh,bindings)
  records['|'.join(key)]=dict(asset=mesh.get_path_name(),materials=[c.get_material(i).get_path_name() if c.get_material(i) else None for i in range(c.get_num_materials())],variant=aid,tint=t,region=species_region(r),visible_instances=r.get('count',1) if r['visible'] and not r['hidden'] else 0)
 result=dict(status='passed',components=records,tree_components=len(records),visible_instances=sum(r.get('count',1) for r in baseline['trees'] if r['visible'] and not r['hidden']));result['preservation_checks']=compare_preserved(baseline,result)
 if SERIES=='v2':assert all(any(q['variant']==aid and q['visible_instances']>0 for q in records.values()) for aid in IDS),'Unused tree family'
 assert unreal.EditorLevelLibrary.save_current_level();result['map_sha256']=sha(mapfile);write(OUT/'install.json',result);write(OUT/'tree_install.json',result)

def tree_audit():
 baseline=read(OUT/'baseline.json');installed=read(OUT/'install.json');imports=read(OUT/'tree_import.json');checks=compare_preserved(baseline,installed)
 unreal.AutomationLibrary.finish_loading_before_screenshot();stats={}
 from street_city_remesh_gate import verify
 for aid,r in imports['assets'].items():
  mesh=EAL.load_asset(r['asset']);assert API.get_lod_count(mesh)==4;counts=[mesh.get_num_triangles(i) for i in range(4)];assert counts==r['triangles'] and all(a>b for a,b in zip(counts,counts[1:]));assert len(r['spec']['frames'])==16
  for level in range(4):verify(Path(r['remesh'][level]['remesh_report']).parent);checks+=1
  for i in range(len(mesh.get_editor_property('static_materials'))):
   m=mesh.get_material(i);assert m.get_editor_property('used_with_instanced_static_meshes');s=MEL.get_statistics(m);stats[m.get_path_name()]=dict(pixel_instructions=s.num_pixel_shader_instructions,vertex_instructions=s.num_vertex_shader_instructions,samplers=s.num_samplers);assert s.num_pixel_shader_instructions>0,(m.get_path_name(),'Missing compiled shader');checks+=1
 distribution={aid:sum(r.get('visible_instances',0) for r in installed['components'].values() if r['variant']==aid) for aid in IDS}
 if SERIES=='v2':assert all(n>0 for n in distribution.values()),distribution
 assert installed['visible_instances']==1475;write(OUT/'tree_audit.json',dict(status='passed',checks=checks,materials=stats,distribution=distribution,non_tree_components_preserved=len(baseline['other']),tree_components=installed['tree_components'],visible_instances=installed['visible_instances'],map_sha256=sha(ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap')))

def tree_refine():
 rows=read(OUT/'tree_import.json')['assets']
 for aid,row in rows.items():
  if row.get('reused'):continue
  m=impostor_material(aid,row['spec']);t=EAL.load_asset(PREFIX+'/Textures/'+aid+'_AtlasNormal');t.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_MASKS);save(t)
 write(OUT/'tree_refine.json',dict(status='passed',assets=list(rows),method='Nearest of sixteen directions, shared normal and leaf mask; two sided foliage response; packed normal atlas BC masks'))

def tree_asset_audit():
 from street_city_remesh_gate import verify
 imports=read(OUT/'tree_import.json');materials={};assets={}
 for aid,row in imports['assets'].items():
  mesh=EAL.load_asset(row['asset']);assert API.get_lod_count(mesh)==4;assert [mesh.get_num_triangles(i) for i in range(4)]==row['triangles'];assets[aid]=row['asset']
  for lod in range(4):verify(Path(row['remesh'][lod]['remesh_report']).parent)
  for i in range(len(mesh.get_editor_property('static_materials'))):
   m=mesh.get_material(i);MEL.recompile_material(m);stats=MEL.get_statistics(m);assert stats.num_pixel_shader_instructions>0;path=ROOT/'StreetSocialCity/Content'/(m.get_path_name().split('.')[0].removeprefix('/Game/')+'.uasset');materials[m.get_path_name()]=dict(asset=m.get_path_name(),sha256=sha(path),pixel_instructions=stats.num_pixel_shader_instructions)
 write(OUT/'tree_asset_audit.json',dict(status='passed',assets=assets,materials=materials,remesh_lods=len(imports['assets'])*4,map_sha256=sha(ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap')))

def tree_preview():
 imports=read(OUT/'tree_import.json');world=unreal.EditorLevelLibrary.get_editor_world();fill=SUB.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,2000),unreal.Rotator(pitch=-35,yaw=135,roll=0));fill.light_component.set_mobility(unreal.ComponentMobility.MOVABLE);fill.light_component.set_intensity(2.5);fill.light_component.set_cast_shadows(False);light=SUB.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,2000),unreal.Rotator(pitch=-50,yaw=-45,roll=0));light.light_component.set_mobility(unreal.ComponentMobility.MOVABLE);light.light_component.set_intensity(4);sky=SUB.spawn_actor_from_class(unreal.SkyLight,unreal.Vector());sky.light_component.set_mobility(unreal.ComponentMobility.MOVABLE);sky.light_component.set_editor_property('source_type',unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP);sky.light_component.set_editor_property('cubemap',EAL.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap'));sky.light_component.set_intensity(1.3);sky.light_component.recapture_sky();a=SUB.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector());c=a.static_mesh_component;views=[(aid,lod) for aid in imports['assets'] for lod in [0,3]]+[('camphor-a',1),('camphor-a',2)];eye=unreal.Vector(1300,-1600,950);rot=unreal.MathLibrary.find_look_at_rotation(eye,unreal.Vector(0,0,400));capture=SUB.spawn_actor_from_class(unreal.SceneCapture2D,eye,rot);capture.set_actor_location(eye,False,True);capture.set_actor_rotation(rot,False);cc=capture.capture_component2d;cc.set_editor_property('capture_every_frame',False);cc.set_editor_property('capture_on_movement',False);cc.set_editor_property('always_persist_rendering_state',True);cc.set_editor_property('capture_source',unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR);cc.set_editor_property('fov_angle',38);rt=unreal.RenderingLibrary.create_render_target2d(world,1000,1000,unreal.TextureRenderTargetFormat.RTF_RGBA8);KEEP.append(rt);cc.set_editor_property('texture_target',rt);folder=OUT/'preview';folder.mkdir(exist_ok=True);state=dict(i=0,wait=0,phase='setup',images=[])
 def tick(dt):
  try:
   state['wait']-=1
   if state['phase']=='warm':state['capture_component'].capture_scene()
   if state['wait']>0 or (state['phase']=='warm' and time.perf_counter()<state['until']):return
   if state['phase']=='setup':
    if state['i']==len(views):
     state['capture_component'].set_editor_property('texture_target',None);SUB.destroy_actor(state['capture']);SUB.destroy_actor(state['tree']);unreal.RenderingLibrary.release_render_target2d(rt);KEEP.remove(rt);write(OUT/'tree_preview.json',dict(status='passed',images=state['images']));unreal.unregister_slate_post_tick_callback(state['handle']);unreal.SystemLibrary.quit_editor();return
    aid,lod=views[state['i']];SUB.destroy_actor(state.get('tree',a));tree=SUB.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector());tree.set_actor_location(unreal.Vector(),False,True);component=tree.static_mesh_component;component.set_mobility(unreal.ComponentMobility.MOVABLE);component.set_static_mesh(EAL.load_asset(imports['assets'][aid]['asset']));component.set_forced_lod_model(lod+1);SUB.destroy_actor(state.get('capture',capture));cap=SUB.spawn_actor_from_class(unreal.SceneCapture2D,eye,rot);cap.set_actor_location(eye,False,True);cap.set_actor_rotation(rot,False);fresh=cap.capture_component2d;fresh.set_editor_property('capture_every_frame',False);fresh.set_editor_property('capture_on_movement',False);fresh.set_editor_property('capture_source',unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR);fresh.set_editor_property('fov_angle',38);fresh.set_editor_property('texture_target',rt);unreal.EditorLevelLibrary.editor_invalidate_viewports();state.update(tree=tree,capture=cap,capture_component=fresh,phase='warm',wait=90,until=time.perf_counter()+3)
   else:
    aid,lod=views[state['i']];name=aid+'-LOD'+str(lod)+'.png';unreal.RenderingLibrary.export_render_target(world,rt,str(folder),name);state['images'].append(dict(id=aid,lod=lod,path=str(folder/name),sha256=sha(folder/name)));state.update(i=state['i']+1,phase='setup',wait=1)
  except BaseException:write(OUT/'tree_preview-error.json',dict(status='failed',error=traceback.format_exc()));unreal.unregister_slate_post_tick_callback(state['handle']);unreal.SystemLibrary.quit_editor()
 state['handle']=unreal.register_slate_post_tick_callback(tick)
]====]
