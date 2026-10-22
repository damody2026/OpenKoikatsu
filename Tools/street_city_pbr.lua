-- Native material inventory and repair; Unreal Python remains embedded in Lua.
package.path='Tools/lua/?.lua;'..package.path
local json=require('ok.json')
local mode=arg[1] or 'inventory'
assert(({inventory=true,repair=true,audit=true,render=true})[mode],'inventory | repair | audit | render')
local p=assert(io.popen('cd'));local root=p:read('*l'):gsub('\\','/');p:close()
local out=root..'/output/street-social-city-production/pbr-grok-v1'
assert(os.execute('if not exist "'..out..'" mkdir "'..out..'"'))
local code=[====[
import unreal,json,hashlib,traceback,time,re,sys,shutil
from pathlib import Path
ROOT=Path(PAYLOAD['root']);OUT=Path(PAYLOAD['out']);MODE=PAYLOAD['mode']
EAL=unreal.EditorAssetLibrary;MEL=unreal.MaterialEditingLibrary;ATS=unreal.AssetToolsHelpers.get_asset_tools();SUB=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
MAP='/Game/UniversityCity/Maps/UniversityCity';PREFIX='/Game/UniversityCity/PBRGrokV1'
PROPS={'normal':unreal.MaterialProperty.MP_NORMAL,'metallic':unreal.MaterialProperty.MP_METALLIC,'roughness':unreal.MaterialProperty.MP_ROUGHNESS}
def write(name,data):
 data['run_id']=PAYLOAD['run_id'];(OUT/(name+'.json')).write_text(json.dumps(data,ensure_ascii=False,indent=2),'utf8')
def stable(v):
 if isinstance(v,dict):return {k:stable(x) for k,x in v.items()}
 if isinstance(v,list):return [stable(x) for x in v]
 if isinstance(v,str) and v.startswith("<Struct 'LinearColor'"):
  return [float(re.search(r'\b'+k+r': ([+\-\d.eE]+)',v).group(1)) for k in ['r','g','b','a']]
 return v
def read(name):return stable(json.loads((OUT/(name+'.json')).read_text('utf-8-sig')))
def base(m):
 seen=set()
 while m and not isinstance(m,unreal.Material):
  if m.get_path_name() in seen:return None
  seen.add(m.get_path_name());m=m.get_editor_property('parent')
 return m
def graph(m,prop):
 node=MEL.get_material_property_input_node(m,prop);seen=set();rows=[]
 def walk(n):
  if not n or n.get_path_name() in seen:return
  seen.add(n.get_path_name());r={'class':n.get_class().get_name()}
  for key in ['texture','parameter_name','r','default_value','const_coordinate']:
   try:
    v=n.get_editor_property(key)
    if isinstance(v,unreal.Object):v=v.get_path_name()
    elif isinstance(v,unreal.LinearColor):v=[round(x,6) for x in [v.r,v.g,v.b,v.a]]
    elif not isinstance(v,(str,int,float,bool,type(None))):v=str(v)
    r[key]=v
   except:pass
  rows.append(r)
  for x in MEL.get_inputs_for_material_expression(m,n):walk(x)
 walk(node);return rows
def inventory():
 mats={};meshes={};components=0
 for actor in SUB.get_all_level_actors():
  for comp in actor.get_components_by_class(unreal.MeshComponent):
   components+=1
   mesh=comp.get_editor_property('static_mesh') if isinstance(comp,unreal.StaticMeshComponent) else None
   if mesh:meshes[mesh.get_path_name()]=True
   for i in range(comp.get_num_materials()):
    mat=comp.get_material(i)
    if not mat:continue
    key=mat.get_path_name()
    if key not in mats:mats[key]={'path':key,'class':mat.get_class().get_name(),'uses':0,'examples':[]}
    row=mats[key];row['uses']+=1
    if len(row['examples'])<3:row['examples'].append({'actor':actor.get_actor_label(),'slot':i,'mesh':mesh.get_path_name() if mesh else None})
 parents={}
 for row in mats.values():
  m=base(EAL.load_asset(row['path']));row['parent']=m.get_path_name() if m else None
  if not m:continue
  key=m.get_path_name()
  if key in parents:continue
  gs={k:graph(m,v) for k,v in PROPS.items()};bc=graph(m,unreal.MaterialProperty.MP_BASE_COLOR)
  parents[key]={'path':key,'blend':str(m.get_editor_property('blend_mode')),'shading':str(m.get_editor_property('shading_model')),'two_sided':m.get_editor_property('two_sided'),'inputs':gs,'base_color':bc,'base_color_output':MEL.get_material_property_input_node_output_name(m,unreal.MaterialProperty.MP_BASE_COLOR),'opacity':graph(m,unreal.MaterialProperty.MP_OPACITY_MASK),'missing':[k for k,g in gs.items() if not g],'no_texture':[k for k,g in gs.items() if not any('TextureSample' in n['class'] or 'TextureObject' in n['class'] for n in g)]}
 write('inventory',{'status':'passed','components':components,'meshes':list(meshes),'materials':list(mats.values()),'parents':list(parents.values())})
def work():
 try:
  if MODE=='inventory':inventory()
  elif MODE=='repair':repair()
  elif MODE=='audit':audit()
  elif MODE=='render':render();return
 except BaseException:write(MODE+'-error',{'status':'failed','error':traceback.format_exc()});unreal.log_error(traceback.format_exc())
 unreal.SystemLibrary.quit_editor()
assert unreal.EditorLevelLibrary.load_level('/Engine/Maps/Entry' if MODE=='repair' else MAP)
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
frames=0
def tick(dt):
 global frames
 frames+=1
 if frames<60:return
 unreal.unregister_slate_post_tick_callback(handle);work()
handle=unreal.register_slate_post_tick_callback(tick)
]====]
local extension=[====[
def has_texture(row,channel):
 patterns={'normal':r'normal|normals|_n\.', 'roughness':r'rough|[_-](?:orm|rma|rm|mr)(?:[._-]|$)|occlusionroughness|metallic.*rough', 'metallic':r'metallic|[_-](?:orm|rma|rm|mr)(?:[._-]|$)|occlusionroughness'}
 return any(re.search(patterns[channel],str(n.get('texture','')),re.I) for n in row['inputs'][channel])
def classify(row):
 path=row['path'];name=path.rsplit('/',1)[-1].lower();bc=' '.join(n.get('texture','') or '' for n in row['base_color']).lower();text=name+' '+bc
 if re.search(r'glass|mirror|water|screen|display$|light\.|luminous|diffuser|transparent.*net',name) or 'THIN_TRANSLUCENT' in row['shading']:return None,'physically smooth glass/water/mirror/emissive display or transparent net; original shader retained'
 if '/Localization/' in path:return 'paper','thin printed sign laminate; preserve three-language base colour graph'
 if name.startswith('m_surface_'):
  index=int(re.search(r'm_surface_(\d+)',name).group(1));return ['wood','metal','fabric','fabric','plastic','fabric','fabric','stone'][index],'explicit generated prop surface slot'
 rules=[('foliage',r'leaf|leaves|flower|foliage|cutout|camphor_tint|cherry_tint'),('bark',r'bark'),('asphalt',r'asphalt|road'),('grass',r'grass|pitch'),('soil',r'soil|sand'),('wood',r'wood|oak|walnut|laminate|timber|teak|birch|bamboo'),('metal',r'metal|steel|stainless|alumin|brass|chrome|silver'),('fabric',r'fabric|linen|woven|mattress|net\.'),('leather',r'leather'),('rubber',r'rubber|court|tennis|nosing'),('ceramic',r'ceramic|tile|enamel|porcelain|jade_shop'),('stone',r'stone|marble|walk\.'),('concrete',r'concrete|cement'),('paper',r'paper|book|carton|sign|logo|drafting'),('plastic',r'abs|plastic|keyboard|chassis|equipment|coating|black\.|blue\.|red\.|green\.|orange\.|teal\.|yellow\.')]
 for family,pattern in rules:
  if re.search(pattern,text):return family,'semantic material name / source albedo match: '+pattern
 return 'plaster','painted solid architectural finish; low strength microdetail'
def plan():
 inv=read('inventory');rows=[]
 for row in inv['parents']:
  family,reason=classify(row);normal=not has_texture(row,'normal');rough=not has_texture(row,'roughness');metal=not row['inputs']['metallic']
  # A custom procedural normal is valid without a normal texture.
  if any(n['class']=='MaterialExpressionCustom' for n in row['inputs']['normal']):normal=False
  channels=[k for k,v in [('normal',normal),('roughness',rough),('metallic',metal)] if v]
  action='repair' if family and channels and row['path'].startswith('/Game/UniversityCity/') else 'preserve'
  rows.append(dict(path=row['path'],family=family,reason=reason,channels=channels,action=action,before=row))
 return rows
def texture(family,kind):
 name='T_'+family+'_'+kind;path=PREFIX+'/Textures/'+name
 asset=EAL.load_asset(path) if EAL.does_asset_exist(path) else None
 source=OUT/'maps'/family/(kind+'.png');assert source.is_file(),str(source)
 if not asset:
  task=unreal.AssetImportTask();task.set_editor_property('filename',str(source));task.set_editor_property('destination_path',PREFIX+'/Textures');task.set_editor_property('destination_name',name);task.set_editor_property('automated',True);task.set_editor_property('save',True);ATS.import_asset_tasks([task]);asset=EAL.load_asset(path)
 assert isinstance(asset,unreal.Texture2D)
 asset.set_editor_property('srgb',False);asset.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_NORMALMAP if kind=='normal' else unreal.TextureCompressionSettings.TC_MASKS);asset.set_editor_property('max_texture_size',1024)
 assert EAL.save_loaded_asset(asset);return asset
def node(mat,cls,x,y):return MEL.create_material_expression(mat,cls,x,y)
def const(mat,value,x,y):
 n=node(mat,unreal.MaterialExpressionConstant,x,y);n.set_editor_property('r',value);return n
def link(a,out,b,inp):
 names=list(MEL.get_material_expression_input_names(b))
 if inp in ['Input','VectorInput'] and inp not in names:inp=''
 assert MEL.connect_material_expressions(a,out,b,inp),(a.get_class().get_name(),out,b.get_class().get_name(),inp,names,list(MEL.get_material_expression_output_names(a)))
def protected(mat):
 return {k:graph(mat,v) for k,v in [('base',unreal.MaterialProperty.MP_BASE_COLOR),('opacity',unreal.MaterialProperty.MP_OPACITY_MASK),('emissive',unreal.MaterialProperty.MP_EMISSIVE_COLOR),('opacity_translucent',unreal.MaterialProperty.MP_OPACITY)]}
def repair():
 rows=plan();write('plan',{'status':'passed','rows':rows});changed=[];textures={};backups=read('backups')['assets'] if (OUT/'backups.json').exists() else [];known={r['asset'] for r in backups}
 # Backup every affected package before any original package is changed.
 for row in rows:
  if row['action']!='repair':continue
  relative=row['path'].split('.')[0].replace('/Game/','')+'.uasset';source=ROOT/'StreetSocialCity/Content'/relative;target=OUT/'backups'/relative;assert source.is_file(),str(source);target.parent.mkdir(parents=True,exist_ok=True)
  if not target.exists():shutil.copy2(source,target)
  if row['path'] not in known:backups.append({'asset':row['path'],'source':str(source),'backup':str(target),'sha256':hashlib.sha256(target.read_bytes()).hexdigest()});known.add(row['path'])
 write('backups',{'status':'passed','assets':backups})
 for row in rows:
  if row['action']!='repair':continue
  mat=EAL.load_asset(row['path']);family=row['family'];before=protected(mat);state=(str(mat.get_editor_property('blend_mode')),mat.get_editor_property('two_sided'));baseline_metal=graph(mat,PROPS['metallic'])
  if family not in textures:textures[family]={k:texture(family,k) for k in ['normal','orm']}
  normtex=textures[family]['normal'];ormtex=textures[family]['orm'];meta=json.loads((OUT/'maps'/family/'generation.json').read_text('utf8'))
  # Idempotent: only our marked nodes are removed on a fresh run; an already upgraded material is skipped.
  if EAL.get_metadata_tag(mat,'CityPBRGrokVersion')=='1':
   assert graph(mat,unreal.MaterialProperty.MP_BASE_COLOR)==row['before']['base_color']
   assert graph(mat,unreal.MaterialProperty.MP_OPACITY_MASK)==row['before']['opacity']
   changed.append({'path':row['path'],'family':family,'channels':row['channels'],'cached':True});continue
  uv=node(mat,unreal.MaterialExpressionTextureCoordinate,-1100,650);uv.set_editor_property('u_tiling',4.);uv.set_editor_property('v_tiling',4.)
  def sample(tex,name,y,normal=False):
   s=node(mat,unreal.MaterialExpressionTextureSampleParameter2D,-800,y);s.set_editor_property('parameter_name',name);s.set_editor_property('texture',tex);s.set_editor_property('sampler_type',unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL if normal else unreal.MaterialSamplerType.SAMPLERTYPE_MASKS);link(uv,'',s,'UVs');return s
  if 'normal' in row['channels']:
   s=sample(normtex,'CityPBR_Normal',550,True);flat=node(mat,unreal.MaterialExpressionConstant3Vector,-800,850);flat.set_editor_property('constant',unreal.LinearColor(0,0,1,1));lerp=node(mat,unreal.MaterialExpressionLinearInterpolate,-450,550);link(flat,'',lerp,'A');link(s,'RGB',lerp,'B');lerp.set_editor_property('const_alpha',.12 if family in ['paper','foliage','plastic','ceramic'] else .35);normalize=node(mat,unreal.MaterialExpressionNormalize,-200,550);link(lerp,'',normalize,'VectorInput');assert MEL.connect_material_property(normalize,'',PROPS['normal'])
  orm=sample(ormtex,'CityPBR_ORM',1050) if 'roughness' in row['channels'] or 'metallic' in row['channels'] else None
  if 'roughness' in row['channels']:
   old=MEL.get_material_property_input_node(mat,PROPS['roughness']);oldout=MEL.get_material_property_input_node_output_name(mat,PROPS['roughness'])
   if old:
    delta=node(mat,unreal.MaterialExpressionSubtract,-450,1050);link(orm,'G',delta,'A');link(const(mat,meta['roughness'],-800,1250),'',delta,'B');add=node(mat,unreal.MaterialExpressionAdd,-200,1050);link(old,oldout,add,'A');link(delta,'',add,'B');final=node(mat,unreal.MaterialExpressionClamp,0,1050);link(add,'',final,'Input');assert MEL.connect_material_property(final,'',PROPS['roughness'])
   else:assert MEL.connect_material_property(orm,'G',PROPS['roughness'])
  if 'metallic' in row['channels']:assert MEL.connect_material_property(orm,'B',PROPS['metallic'])
  assert protected(mat)==before,('Base colour, language, opacity or emissive changed',row['path']);assert state==(str(mat.get_editor_property('blend_mode')),mat.get_editor_property('two_sided'))
  if 'metallic' not in row['channels']:assert graph(mat,PROPS['metallic'])==baseline_metal
  errors=MEL.recompile_material(mat);assert not errors,(row['path'],list(errors));EAL.set_metadata_tag(mat,'CityPBRGrokVersion','1');EAL.set_metadata_tag(mat,'CityPBRGrokFamily',family);assert EAL.save_loaded_asset(mat)
  changed.append({'path':row['path'],'family':family,'channels':row['channels'],'protected_before':before,'metallic_preserved':baseline_metal if 'metallic' not in row['channels'] else None});
  if len(changed)%50==0:write('repair-progress',{'status':'running','changed':changed});unreal.log('PBR repaired '+str(len(changed)))
 write('repair',{'status':'passed','changed':changed,'preserved':len(rows)-len(changed),'map_geometry_unchanged':True,'textures':{f:{k:v.get_path_name() for k,v in t.items()} for f,t in textures.items()}})
def audit():
 inv=read('inventory');rows=read('plan')['rows'];changed={r['path']:r for r in read('repair')['changed']};issues=[];checks=0
 for row in rows:
  m=EAL.load_asset(row['path']);checks+=1
  if row['path'] in changed:
   r=changed[row['path']]
   checks+=2
   if graph(m,unreal.MaterialProperty.MP_BASE_COLOR)!=row['before']['base_color'] or graph(m,unreal.MaterialProperty.MP_OPACITY_MASK)!=row['before']['opacity']:issues.append(row['path']+' original base colour / mask changed')
   if 'metallic' not in row['channels']:
    checks+=1
    if graph(m,PROPS['metallic'])!=row['before']['inputs']['metallic']:issues.append(row['path']+' original metallic changed')
   for channel in row['channels']:
    g=graph(m,PROPS[channel]);checks+=1
    if not any(PREFIX+'/Textures/' in str(n.get('texture','')) for n in g):issues.append(row['path']+' missing generated '+channel)
   if 'protected_before' in r:
    checks+=1
    if protected(m)!=r['protected_before']:issues.append(row['path']+' protected graph changed')
    if r.get('metallic_preserved') is not None and graph(m,PROPS['metallic'])!=r['metallic_preserved']:issues.append(row['path']+' original metallic changed')
  else:
   for channel in PROPS:
    checks+=1
    if graph(m,PROPS[channel])!=row['before']['inputs'][channel]:issues.append(row['path']+' existing '+channel+' changed')
 for family in {r['family'] for r in rows if r['action']=='repair'}:
  for kind in ['normal','orm']:
   tex=EAL.load_asset(PREFIX+'/Textures/T_'+family+'_'+kind);checks+=1
   if tex.get_editor_property('srgb') or max(tex.blueprint_get_size_x(),tex.blueprint_get_size_y())>1024:issues.append('Texture colour space / resolution '+family+kind)
   if kind=='normal' and tex.get_editor_property('compression_settings')!=unreal.TextureCompressionSettings.TC_NORMALMAP:issues.append('Normal compression '+family)
 write('audit',{'status':'passed' if not issues else 'failed','checks':checks,'issues':issues,'reopened_map':MAP,'materials':len(inv['materials']),'parents':len(rows),'repaired':len(changed),'preserved':len(rows)-len(changed)})
 assert not issues,str(issues[:10])
def render():
 # The editor auto-enables missing HISM usage flags at world load. Persist them,
 # otherwise a later cooked world can lose the generated material permutation.
 usage={}
 for actor in SUB.get_all_level_actors():
  for comp in actor.get_components_by_class(unreal.InstancedStaticMeshComponent):
   for i in range(comp.get_num_materials()):
    material=comp.get_material(i);m=base(material) if material else None
    if m and m.get_path_name().startswith('/Game/UniversityCity/'):usage[m.get_path_name()]=m
 backups=read('backups');known={r['asset'] for r in backups['assets']}
 for path,m in usage.items():
  if path not in known:
   relative=path.split('.')[0].replace('/Game/','')+'.uasset';src=ROOT/'StreetSocialCity/Content'/relative;dest=OUT/'backups'/relative;dest.parent.mkdir(parents=True,exist_ok=True)
   if not dest.exists():shutil.copy2(src,dest)
   backups['assets'].append({'asset':path,'source':str(src),'backup':str(dest),'sha256':hashlib.sha256(dest.read_bytes()).hexdigest()});known.add(path)
  if not m.get_editor_property('used_with_instanced_static_meshes'):
   m.set_editor_property('used_with_instanced_static_meshes',True);MEL.recompile_material(m)
  assert EAL.save_loaded_asset(m);assert m.get_editor_property('used_with_instanced_static_meshes')
 write('backups',backups);write('instancing',{'status':'passed','saved_materials':list(usage)})
 audit();world=unreal.EditorLevelLibrary.get_editor_world();old=ROOT/'output/street-social-city-production/resort-native-v1/camera-plan.json';views=json.loads(old.read_text('utf8'))['views']
 layout=json.loads((ROOT/'output/street-social-city-production/resort-native-v1/placement.json').read_text('utf8'))['instances']
 for aid,title in [('R009','wood-chair-detail'),('R002','fabric-daybed-detail')]:
  p=next(r for r in layout if r['asset']==aid);x,y,z=p['position_m'];views.append({'name':title,'eye':[x-1.6,y-1.8,z+1.7],'target':[x,y,z+.65]})
 state={'index':0,'phase':'setup','hold':60,'results':[],'current':None,'busy':False};keep=[]
 def point(p):return unreal.Vector(p[1]*100,p[0]*100,p[2]*100)
 def tick(dt):
  if state['busy']:return
  if state['hold']>0:state['hold']-=1;return
  state['busy']=True
  try:
   if state['phase']=='setup':
    q=views[state['index']];rotation=unreal.MathLibrary.find_look_at_rotation(point(q['eye']),point(q['target']));unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(point(q['eye']),rotation);a=SUB.spawn_actor_from_class(unreal.SceneCapture2D,point(q['eye']),rotation);c=a.capture_component2d;c.set_editor_property('capture_every_frame',False);c.set_editor_property('capture_on_movement',False);rt=unreal.RenderingLibrary.create_render_target2d(world,1600,1000,unreal.TextureRenderTargetFormat.RTF_RGBA8);c.set_editor_property('texture_target',rt);c.set_editor_property('capture_source',unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR);c.set_editor_property('fov_angle',65);c.set_editor_property('max_view_distance_override',100000.);keep.append(rt);state['current']=(q,a,c,rt);state['phase']='capture';state['hold']=60
   elif state['phase']=='capture':
    unreal.AutomationLibrary.finish_loading_before_screenshot();state['current'][2].capture_scene();state['phase']='export';state['hold']=10
   elif state['phase']=='export':
    q,a,c,rt=state['current'];folder=OUT/'renders';folder.mkdir(exist_ok=True);p=folder/(q['name']+'.png');unreal.RenderingLibrary.export_render_target(world,rt,str(folder),p.name);assert p.exists() and p.stat().st_size>5000;state['results'].append({'view':q['name'],'path':str(p),'sha256':hashlib.sha256(p.read_bytes()).hexdigest()});SUB.destroy_actor(a);state['index']+=1;state['phase']='setup';state['hold']=10
    if state['index']==len(views):
     unreal.unregister_slate_post_tick_callback(render_handle);write('render',{'status':'passed','images':state['results'],'map_sha256':hashlib.sha256((ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap').read_bytes()).hexdigest(),'native_world':MAP,'note':'native reopened saved world; unchanged original lights; no capture actors saved'});unreal.SystemLibrary.quit_editor()
  except BaseException:
   unreal.unregister_slate_post_tick_callback(render_handle);write('render-error',{'status':'failed','error':traceback.format_exc()});unreal.log_error(traceback.format_exc());unreal.SystemLibrary.quit_editor()
  finally:state['busy']=False
 global render_handle,RENDER_KEEP
 RENDER_KEEP=(state,keep);render_handle=unreal.register_slate_post_tick_callback(tick)
]====]
-- Imports and constants must precede the helper definitions, while the startup callback stays last.
local split=assert(code:find('def work%(%)'))
code=code:sub(1,split-1)..extension..'\n'..code:sub(split)
local payload={root=root,out=out,mode=mode,run_id=mode..'-'..os.date('%Y%m%d-%H%M%S'),source=root..'/Tools/street_city_pbr.lua::UnrealPayload',code=code}
local payloadpath=out..'/'..mode..'-payload.json';local f=assert(io.open(payloadpath,'wb'));f:write(json.encode(payload));f:close()
local function q(s)return "'"..s:gsub("'","''").."'" end
local function read(p)local f=assert(io.open(p,'rb'));local s=f:read('*a');f:close();return s end
local engine=json.decode(read('Tools/engine.local.json')).engine
local args={root..'/StreetSocialCity/StreetSocialCity.uproject','/Engine/Maps/Entry','-ExecutePythonScript='..root..'/Tools/run_street_city_unreal_deferred.py','-UserDir=E:/StreetSocialCityProduction/pbr-grok-user','-unattended','-RenderOffscreen','-NoAsyncLoadingThread','-NoAsyncPostLoad','-nosplash','-nosound','-nop4','-d3d12','-abslog='..out..'/'..mode..'-editor.log'}
for i,v in ipairs(args)do args[i]=q(v)end
local cmd="$env:STREET_CITY_DEFERRED_SCRIPT="..q(root..'/Tools/run_street_city_unreal_deferred.py').."; $env:STREET_CITY_DEFERRED_JSON="..q(payloadpath).."; $env:TEMP='E:/StreetSocialCityProduction/unreal-scratch'; $env:TMP=$env:TEMP; [Environment]::SetEnvironmentVariable('UE-LocalDataCachePath','E:/StreetSocialCityProduction/unreal-ddc','Process'); $p=Start-Process -FilePath "..q(engine..'/Engine/Binaries/Win64/UnrealEditor.exe')..' -ArgumentList @('..table.concat(args,',')..') -WindowStyle Hidden -RedirectStandardOutput '..q(out..'/'..mode..'-process.log')..' -RedirectStandardError '..q(out..'/'..mode..'-stderr.log')..' -PassThru; while(-not $p.WaitForExit(1000)){}; exit $p.ExitCode'
assert(os.execute('powershell.exe -NoProfile -Command "'..cmd..'"'))
local result=json.decode(read(out..'/'..mode..'.json'));assert(result.run_id==payload.run_id and result.status=='passed','Missing fresh successful receipt')
print('Native PBR '..mode..' passed')
