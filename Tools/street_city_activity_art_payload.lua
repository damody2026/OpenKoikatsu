-- Art direction, shader animation and reusable scenic dressing; embedded UE API.
return [====[
ART_VERSION='20261021-art-v2-04';ART_MATS={}

def art_constant(m,value):
 cls=unreal.MaterialExpressionConstant3Vector if isinstance(value,(list,tuple)) else unreal.MaterialExpressionConstant
 n=MEL.create_material_expression(m,cls)
 n.set_editor_property('constant',unreal.LinearColor(*value,1)) if isinstance(value,(list,tuple)) else n.set_editor_property('r',value)
 return n

def art_custom(m,code,inputs,vector=True):
 n=MEL.create_material_expression(m,unreal.MaterialExpressionCustom);n.set_editor_property('code',code);n.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT3 if vector else unreal.CustomMaterialOutputType.CMOT_FLOAT1);n.set_editor_property('inputs',[custom_input(k) for k in inputs])
 for key,src in inputs.items():assert MEL.connect_material_expressions(src,'',n,key),key
 return n

def art_clock(m):
 t=MEL.create_material_expression(m,unreal.MaterialExpressionTime)
 c=art_collection();p=MEL.create_material_expression(m,unreal.MaterialExpressionCollectionParameter);p.set_editor_property('collection',c);p.set_editor_property('parameter_name','PreviewTime')
 return art_custom(m,'return Preview < 0 ? Clock : Preview;',{'Clock':t,'Preview':p},False)

def art_collection():
 path=PREFIX+'/ArtV2/MPC_ActivityFX'
 if EAL.does_asset_exist(path):return EAL.load_asset(path)
 c=ATS.create_asset('MPC_ActivityFX',PREFIX+'/ArtV2',unreal.MaterialParameterCollection,unreal.MaterialParameterCollectionFactoryNew());p=unreal.CollectionScalarParameter();p.set_editor_property('parameter_name','PreviewTime');p.set_editor_property('default_value',-1.);c.set_editor_property('scalar_parameters',[p]);c.post_edit_change() if hasattr(c,'post_edit_change') else None;save(c);return c

def art_material(key):
 if key in ART_MATS:return ART_MATS[key]
 name='M_'+key.replace('-','_');path=PREFIX+'/ArtV2/'+name;m=EAL.load_asset(path) if EAL.does_asset_exist(path) else None
 if m and EAL.get_metadata_tag(m,'ArtVersion')==ART_VERSION:ART_MATS[key]=m;return m
 if not m:m=ATS.create_asset(name,PREFIX+'/ArtV2',unreal.Material,unreal.MaterialFactoryNew())
 MEL.delete_all_material_expressions(m);m.set_editor_property('used_with_instanced_static_meshes',True)
 def output(n,prop):assert MEL.connect_material_property(n,'',getattr(unreal.MaterialProperty,'MP_'+prop))
 if key in ['ocean','pond','fountain-water']:
  ocean=key=='ocean';m.set_editor_property('blend_mode',unreal.BlendMode.BLEND_OPAQUE);m.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_SINGLE_LAYER_WATER);m.set_editor_property('two_sided',True);m.set_editor_property('tangent_space_normal',False)
  wp=MEL.create_material_expression(m,unreal.MaterialExpressionWorldPosition);t=art_clock(m);inputs={'P':wp,'T':t}
  # Analytic derivatives of four directional waves + two capillary bands.
  normal='float2 p=P.xy*0.01; float2 d=0;float footprint=max(length(ddx(p)),length(ddy(p)));'
  for direction,k,amp,speed in [((.94,.34),.78,.055,1.0),((-.4,.92),1.65,.033,1.45),((.6,-.8),5.1,.018,2.1),((.2,.98),13,.007,3.0),((-.7,.7),31,.003,4.3)]:
   dx,dy=direction;normal+=f'd+=float2({dx},{dy})*{k*amp}*exp(-{k*k}*footprint*footprint*2)*cos(dot(p,float2({dx},{dy}))*{k}-T*{speed});'
  normal+='return normalize(float3(-d*'+('1.15' if ocean else '.9')+',1));';output(art_custom(m,normal,inputs),'NORMAL')
  # Volume scattering remains subtle enough for koi to be seen beneath the surface.
  output(art_constant(m,[.015,.045,.055] if ocean else [.025,.06,.055]),'BASE_COLOR');output(art_constant(m,.17 if ocean else .1),'ROUGHNESS');output(art_constant(m,.0),'METALLIC');output(art_constant(m,.5),'SPECULAR');output(art_constant(m,.12 if ocean else .12),'OPACITY')
  volume=MEL.create_material_expression(m,unreal.MaterialExpressionSingleLayerWaterMaterialOutput)
  for pin,value in [('ScatteringCoefficients',[.00008,.00035,.00042] if ocean else [.00012,.0002,.00017]),('AbsorptionCoefficients',[.0035,.00085,.00045] if ocean else [.0004,.00015,.0001]),('PhaseG',.25)]:assert MEL.connect_material_expressions(art_constant(m,value),'',volume,pin),(pin,MEL.get_material_expression_input_names(volume))
  caustic=art_custom(m,'float2 p=P.xy*.018;float a=pow(saturate(1-abs(sin(p.x+sin(p.y*.8+T*.35))*sin(p.y+sin(p.x*.7-T*.2)))),14);return float3(1,1,1)*(1+a*.25);',inputs)
  assert MEL.connect_material_expressions(caustic,'',volume,'ColorScaleBehindWater')
  if ocean:
   # WPO uses existing water tessellation; no new terrain or high-poly surface.
   output(art_custom(m,'float2 p=P.xy*.01;float coast=saturate((13800-P.x)/1500);float h=(sin(dot(p,float2(.94,.34))*.78-T)*10+sin(dot(p,float2(-.4,.92))*1.65-T*1.45)*3);return float3(0,0,h*coast);',inputs),'WORLD_POSITION_OFFSET')
 elif key=='jet':
  m.set_editor_property('blend_mode',unreal.BlendMode.BLEND_TRANSLUCENT);m.set_editor_property('two_sided',True);wp=MEL.create_material_expression(m,unreal.MaterialExpressionWorldPosition);t=art_clock(m)
  n=art_custom(m,'float flow=.55+.45*pow(saturate(sin(P.z*.11-T*9)),3);return float3(.1,.38,.52)*flow;',{'P':wp,'T':t});output(n,'EMISSIVE_COLOR');output(art_constant(m,[.55,.8,.88]),'BASE_COLOR');output(art_constant(m,.65),'OPACITY');output(art_constant(m,.06),'ROUGHNESS')
 elif key.startswith('firework-'):
  index=int(key.split('-')[1]);centres=[[600,75,73],[636,83,87],[662,92,65],[615,60,107]];cx,cy,cz=centres[index];centre=point([cx,cy,cz]);offset=index*1.3
  m.set_editor_property('blend_mode',unreal.BlendMode.BLEND_ADDITIVE);m.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT);m.set_editor_property('two_sided',True);wp=MEL.create_material_expression(m,unreal.MaterialExpressionWorldPosition);t=art_clock(m);inputs={'P':wp,'T':t}
  life=f'float age=frac((T+{offset})/7.0)*7.0;float e=saturate(age/3.5);'
  output(art_custom(m,life+f'float3 r=P-float3({centre.x},{centre.y},{centre.z});return r*(e-.65)+float3(0,0,-80*age*age);',inputs),'WORLD_POSITION_OFFSET')
  colours=[[5,1.3,.25],[1.1,2.4,6],[6,.4,1.4],[3.2,5,1]];c=colours[index]
  output(art_custom(m,life+f'float blink=.75+.25*sin(P.x*.2+P.z*.13+T*24);float glow=smoothstep(0,.12,age)*(1-smoothstep(4.2,6.3,age));return float3({c[0]},{c[1]},{c[2]})*glow*blink*65;',inputs),'EMISSIVE_COLOR');output(art_constant(m,1.),'OPACITY')
 else:
  values={'pool-stone':([.025,.065,.07],.48,0),'rose':([.52,.23,.25],.65,0),'rose-rug':([.35,.15,.2],.94,0),'walnut':([.12,.055,.025],.53,0),'brass':([.46,.27,.085],.27,.78),'black':([.018,.022,.025],.65,.5),'cream':([.73,.65,.5],.84,0),'sage':([.14,.25,.17],.8,0),'ivory':([.75,.69,.58],.7,0),'warm-lamp':([1,.65,.3],.3,0)}
  c,rough,metal=values[key];output(art_constant(m,c),'BASE_COLOR');output(art_constant(m,rough),'ROUGHNESS');output(art_constant(m,metal),'METALLIC')
  if key=='warm-lamp':output(art_constant(m,[2.5,1.6,.8]),'EMISSIVE_COLOR')
  if key=='walnut':
   wp=MEL.create_material_expression(m,unreal.MaterialExpressionWorldPosition);n=art_custom(m,'float2 p=P.xy;float grain=.9+.1*sin(p.x*.5+sin(p.y*.012)*3)+.035*sin(p.x*4);return float3(.12,.055,.025)*grain;',{'P':wp});output(n,'BASE_COLOR')
  if key!='warm-lamp':
   family='wood' if key=='walnut' else 'metal' if key in ['black','brass'] else 'fabric' if key in ['cream','rose-rug'] else 'stone' if key=='pool-stone' else 'plaster'
   tex=EAL.load_asset('/Game/UniversityCity/PBRGrokV1/Textures/T_'+family+'_normal');assert tex,(key,family)
   n=MEL.create_material_expression(m,unreal.MaterialExpressionTextureSample);n.set_editor_property('texture',tex);n.set_editor_property('sampler_type',unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL);MEL.connect_material_property(n,'RGB',unreal.MaterialProperty.MP_NORMAL)
   orm=EAL.load_asset('/Game/UniversityCity/PBRGrokV1/Textures/T_'+family+'_orm');assert orm
   n=MEL.create_material_expression(m,unreal.MaterialExpressionTextureSample);n.set_editor_property('texture',orm);n.set_editor_property('sampler_type',unreal.MaterialSamplerType.SAMPLERTYPE_MASKS);MEL.connect_material_property(n,'G',unreal.MaterialProperty.MP_ROUGHNESS);MEL.connect_material_property(n,'R',unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
 errors=MEL.recompile_material(m);assert not errors,(key,list(errors));EAL.set_metadata_tag(m,'ArtVersion',ART_VERSION);save(m);ART_MATS[key]=m;return m

def art_expected(q,mesh,index):
 key=q.get('art_material')
 if not key:return None
 if key in ['boutique','library','stage']:
  palette={0:'walnut',1:'brass',2:'cream',3:'rose',4:'sage',5:'rose',6:'ivory',7:'cream'}
  if key=='stage':palette={0:'walnut',1:'black',2:'ivory',3:'black',4:'sage',5:'rose',6:'brass',7:'black'}
  slot=mesh.get_editor_property('static_materials')[index];match=re.search(r'surface[-_](\d+)',str(slot.get_editor_property('material_slot_name')))
  return art_material(palette[int(match.group(1))]) if match else None
 return art_material(key)

def art_apply(a,q):
 c=a.static_mesh_component
 for i in range(c.get_num_materials()):
  m=art_expected(q,c.static_mesh,i)
  if m:c.set_material(i,m)

def art_install():
 before=OUT/'backups/art-v2-before.umap'
 if not before.exists():shutil.copy2(ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap',before)
 install()
 actors={a.get_actor_label():a for a in SUB.get_all_level_actors()};r=registry();changed=[]
 for q in PLAN['instances']:art_apply(actors['Activity_'+q['id']],q)
 for aid,key in [('fountain','fountain-water'),('fish-pond','pond')]:
  mesh=EAL.load_asset(r[aid]['asset'])
  for i,slot in enumerate(mesh.get_editor_property('static_materials')):
   name=str(slot.get_editor_property('material_slot_name'))
   if name=='surface-3':mesh.set_material(i,art_material(key))
   if name=='surface-7' and aid=='fish-pond':mesh.set_material(i,art_material('pool-stone'))
   if name=='surface-6' and aid=='fountain':mesh.set_material(i,art_material('jet'))
  save(mesh)
  for q in PLAN['instances']:
   if q['asset']==aid:
    c=actors['Activity_'+q['id']].static_mesh_component
    for i in range(c.get_num_materials()):c.set_material(i,mesh.get_material(i))
 for label,a in actors.items():
  if re.fullmatch(r'G\d\d_Water',label):
   c=a.static_mesh_component
   for i in range(c.get_num_materials()):c.set_material(i,art_material('ocean'))
   changed.append(label)
 # Only this shop's wood surfaces receive the darker library finish; localized signs remain untouched.
 a=actors['S07_F01'];c=a.static_mesh_component
 for i in range(c.get_num_materials()):
  m=c.get_material(i)
  if m and 'Pale_oak' in m.get_path_name():c.set_material(i,art_material('walnut'))
 art_effects(actors)
 assert unreal.EditorLevelLibrary.save_current_level();mapsha=sha(ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap');receipt=read(OUT/'install.json');receipt['map_sha256']=mapsha;receipt['art_revision']=ART_VERSION;write(OUT/'install.json',receipt)
 write(OUT/'art_install.json',dict(status='passed',revision=ART_VERSION,water_actors=changed,materials={k:v.get_path_name() for k,v in ART_MATS.items()},map_sha256=mapsha))

def art_effects(actors):
 mesh=EAL.load_asset(registry()['platform']['asset']);rows=[];centres=[[600,75,73],[636,83,87],[662,92,65],[615,60,107]]
 for j,centre in enumerate(centres):
  label='ActivityArt_Fireworks_'+str(j);a=actors.get(label)
  if not a:
   bp=EAL.load_asset('/Game/UniversityCity/StreetScenery/BP_StreetCluster');assert bp;a=SUB.spawn_actor_from_class(bp.generated_class(),unreal.Vector());a.set_actor_label(label)
  a.tags=[unreal.Name('ActivityArt-v2')];a.set_folder_path('UniversityCity/ActivityScenesV1/AnimatedFireworks');c=a.get_component_by_class(unreal.HierarchicalInstancedStaticMeshComponent)
  assert c;c.set_editor_property('bounds_scale',4.0)
  # PostEditChange can reconstruct a blueprint component. Reacquire it before populating overrides.
  c=a.get_component_by_class(unreal.HierarchicalInstancedStaticMeshComponent);c.set_mobility(unreal.ComponentMobility.STATIC);c.set_static_mesh(mesh);c.clear_instances();c.set_collision_profile_name('NoCollision');c.set_cast_shadow(False);c.set_cull_distances(0,60000);c.set_visibility(True,True);c.set_hidden_in_game(False,True)
  for slot in range(len(mesh.get_editor_property('static_materials'))):c.set_material(slot,art_material('firework-'+str(j)))
  for i in range(96):
   v=1-2*(i+.5)/96;radius=math.sqrt(1-v*v);angle=i*2.399963;direction=[radius*math.cos(angle),radius*math.sin(angle),v];rr=15+(i%9)*.65;pos=[centre[k]+direction[k]*rr*.65 for k in range(3)];p=point(pos);d=point(direction);rotation=unreal.MathLibrary.find_look_at_rotation(p,p+d)
   c.add_instance(unreal.Transform(location=p,rotation=rotation,scale=unreal.Vector(1.8+(i%4)*.35,.09,.7)),True)
  rows.append(dict(label=label,count=c.get_instance_count(),material=c.get_material(0).get_path_name(),centre_m=centre))
 write(OUT/'effects.json',dict(status='passed',fireworks=rows,method='Four persistent 3D instanced bursts; shader-driven expansion, gravity, flicker and fade; seven-second loop; no runtime Python required'))

def art_audit():
 # Base checks compare the intended art override rather than incorrectly requiring old mesh defaults.
 audit();actors={a.get_actor_label():a for a in SUB.get_all_level_actors()};checks=[]
 for key,path in read(OUT/'art_install.json')['materials'].items():
  m=EAL.load_asset(path);assert m and not MEL.recompile_material(m),(key,'compile');checks.append(key)
 for row in read(OUT/'effects.json')['fireworks']:
  c=actors[row['label']].get_component_by_class(unreal.HierarchicalInstancedStaticMeshComponent);assert c.get_instance_count()==96 and c.is_visible() and not actors[row['label']].get_editor_property('hidden');assert c.get_material(0) and c.get_material(0).get_path_name()==row['material'];assert API.get_lod_count(c.static_mesh)>=3
 for label in read(OUT/'art_install.json')['water_actors']:
  c=actors[label].static_mesh_component
  for i in range(c.get_num_materials()):assert c.get_material(i)==art_material('ocean'),label
 for key in ['ocean','pond','fountain-water']:assert art_material(key).get_editor_property('shading_model')==unreal.MaterialShadingModel.MSM_SINGLE_LAYER_WATER
 c=actors['S07_F01'].static_mesh_component
 for i in range(c.get_num_materials()):
  original=c.static_mesh.get_material(i)
  if original and 'Pale_oak' in original.get_path_name():assert c.get_material(i)==art_material('walnut')
 assert art_collection().get_editor_property('scalar_parameters')[0].get_editor_property('default_value')==-1
 r=read(OUT/'audit.json');r['art_materials_verified']=checks;r['firework_sparks']=384;write(OUT/'audit.json',r);write(OUT/'art_audit.json',dict(status='passed',materials=checks,firework_sparks=384,map_sha256=r['map_sha256']))

def art_probe():
 rows=[]
 for a in SUB.get_all_level_actors():
  label=a.get_actor_label()
  if any(s in label.lower() for s in ['water','ocean','bay','s07_','sky']):
   components=[]
   for c in a.get_components_by_class(unreal.StaticMeshComponent):
    components.append(dict(mesh=c.static_mesh.get_path_name() if c.static_mesh else None,materials=[c.get_material(i).get_path_name() if c.get_material(i) else None for i in range(c.get_num_materials())]))
   p=a.get_actor_location();rows.append(dict(label=label,position_cm=[p.x,p.y,p.z],components=components))
 write(OUT/'art_probe.json',dict(status='passed',actors=rows,custom_class=str(getattr(unreal,'MaterialExpressionCustom',None)),singlewater_class=str(getattr(unreal,'MaterialExpressionSingleLayerWaterMaterialOutput',None))))

def art_materials():
 for key in ['rose','rose-rug','walnut','brass','black','cream','sage','ivory','warm-lamp','ocean','pond','fountain-water','jet','firework-0','firework-1','firework-2','firework-3']:art_material(key)
 write(OUT/'art_materials.json',dict(status='passed',revision=ART_VERSION,materials={k:v.get_path_name() for k,v in ART_MATS.items()}))
]====]
