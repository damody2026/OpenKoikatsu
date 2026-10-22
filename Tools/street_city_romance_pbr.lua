-- Romance resort architectural materials for UniversityCity.
-- Unreal Python stays embedded. This file does not add a .py or .ps1.
-- Codex runs inventory, plan, install, audit, render. Do not treat a syntax check as an editor run.
package.path='Tools/lua/?.lua;'..package.path
local json=require('ok.json')
local mode=arg[1]
if mode=='syntax' then print('syntax-ok') return end
assert(({shader=true,inventory=true,plan=true,install=true,audit=true,render=true,restore=true})[mode],'inventory | plan | install | audit | render | restore')
local p=assert(io.popen('cd'));local root=p:read('*l'):gsub('\\','/');p:close()
local out=root..'/output/street-social-city-production/romance-pbr-v1'
assert(os.execute('if not exist "'..out..'" mkdir "'..out..'"'))
local code=[====[
import unreal,json,hashlib,traceback,shutil,math,re,time
from pathlib import Path
ROOT=Path(PAYLOAD['root']);OUT=Path(PAYLOAD['out']);MODE=PAYLOAD['mode']
MAP='/Game/UniversityCity/Maps/UniversityCity'
PREFIX='/Game/UniversityCity/RomancePBR'
EAL=unreal.EditorAssetLibrary
MEL=unreal.MaterialEditingLibrary
ATS=unreal.AssetToolsHelpers.get_asset_tools()
SUB=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
ROLES=['floor','wall','roof','stairs','facade','trim']
PROTECT=re.compile(r'glass|glaz|window|water|mirror|emiss|light|lamp|sign|logo|brand|print|letter|\btext\b|poster|menu|mural|decal|identity|foliage|leaf|plant|flower|\btree\b|chair|sofa|\btable\b|cushion|mattress|furniture|book|screen|display|monitor|keyboard|appliance|fixture|toilet|basin|\bsink\b|faucet|fabric|linen|curtain|\brug\b|carpet|metal|steel|chrome|brass|alumin|copper|railing|handrail',re.I)
ROLE_RE=[
 ('roof',re.compile(r'roof|parapet|ridge|\beave\b',re.I)),
 ('stairs',re.compile(r'stair|step|tread|riser',re.I)),
 ('floor',re.compile(r'floor|paving|\bdeck\b|terrazzo|ground tile',re.I)),
 ('trim',re.compile(r'trim|cornice|mould|molding|plinth|fascia|soffit|\bsill\b|architrave|\bframe\b',re.I)),
 ('facade',re.compile(r'facade|rainscreen|cladding|exterior|honed limestone|ivory rainscreen|charcoal stone',re.I)),
 ('wall',re.compile(r'\bwall\b|plaster|paint|lime|stucco|interior|washable|porcelain|ceramic',re.I)),
]
# Shared stochastic triangular sampling: identical coordinates/weights for all channels.
# Explicit gradients exclude hash discontinuities from mip selection.
def stochastic_code(kind):
 lines=['float seed=Seed+Inst*19.0;','float period=max(Period,40.0);','float3 an=abs(N);','float3 w=an/max(an.x+an.y+an.z,0.0001);']
 for axis,swizzle in [('X','zy'),('Y','xz'),('Z','xy')]:
  lines += [f'float2 uv{axis}=P.{swizzle}/period;',f'float2 cell{axis}=floor(uv{axis});',f'float2 f{axis}=frac(uv{axis});',f'float t{axis}=step(1.0,f{axis}.x+f{axis}.y);',f'float3 b{axis}=lerp(float3(1.0-f{axis}.x-f{axis}.y,f{axis}.x,f{axis}.y),float3(f{axis}.x+f{axis}.y-1.0,1.0-f{axis}.x,1.0-f{axis}.y),t{axis});',f'b{axis}=max(b{axis},0.0);',f'b{axis}=b{axis}*b{axis}*(3.0-2.0*b{axis});',f'b{axis}/=max(b{axis}.x+b{axis}.y+b{axis}.z,0.0001);']
  corners=[('float2(0,0)','float2(1,1)'),('float2(1,0)','float2(0,1)'),('float2(0,1)','float2(1,0)')]
  for i,(lo,hi) in enumerate(corners):
   key=axis+str(i)
   lines += [f'float2 v{key}=cell{axis}+lerp({lo},{hi},t{axis});',f'float2 h{key}=frac(sin(float2(dot(v{key},float2(127.1,311.7))+seed,dot(v{key},float2(269.5,183.3))+seed*1.37))*43758.5453);',f'float sc{key}=0.85+0.30*frac(sin(dot(v{key},float2(419.2,91.7))+seed*2.11)*24317.31);',f'float2 q{key}=uv{axis}*sc{key}+h{key}*19.73;']
   typ='float2' if kind=='Norm' else 'float3';chan='rg' if kind=='Norm' else 'rgb'
   lines.append(f'{typ} s{key}=Texture2DSampleGrad({kind},{kind}Sampler,q{key},ddx(uv{axis})*sc{key},ddy(uv{axis})*sc{key}).{chan};')
  typ='float2' if kind=='Norm' else 'float3'
  lines.append(f'{typ} r{axis}=s{axis}0*b{axis}.x+s{axis}1*b{axis}.y+s{axis}2*b{axis}.z;')
 if kind=='Norm':
  lines += ['float2 ax=(rX*2.0-1.0)*Strength;float2 ay=(rY*2.0-1.0)*Strength;float2 az=(rZ*2.0-1.0)*Strength;','float3 nx=float3(sqrt(saturate(1.0-dot(ax,ax)))*sign(N.x),ax.y,ax.x);','float3 ny=float3(ay.x,sqrt(saturate(1.0-dot(ay,ay)))*sign(N.y),ay.y);','float3 nz=float3(az.x,az.y,sqrt(saturate(1.0-dot(az,az)))*sign(N.z));','return normalize(nx*w.x+ny*w.y+nz*w.z);']
 elif kind=='Orm':lines += ['float3 orm=rX*w.x+rY*w.y+rZ*w.z;orm.b=0.0;return orm;']
 else:
  lines += ['float3 color=rX*w.x+rY*w.y+rZ*w.z;','float2 m=P.xy/2400.0;float2 c=floor(m);float2 f=frac(m);f=f*f*(3.0-2.0*f);','float m0=frac(sin(dot(c,float2(127.1,311.7))+seed)*43758.5453);','float m1=frac(sin(dot(c+float2(1,0),float2(127.1,311.7))+seed)*43758.5453);','float m2=frac(sin(dot(c+float2(0,1),float2(127.1,311.7))+seed)*43758.5453);','float m3=frac(sin(dot(c+float2(1,1),float2(127.1,311.7))+seed)*43758.5453);','float macro=lerp(lerp(m0,m1,f.x),lerp(m2,m3,f.x),f.y);','return color*lerp(TintA,TintB,macro);']
 return '\n'.join(lines)

COLOR_CODE=stochastic_code('Base')
NORM_CODE=stochastic_code('Norm')
ORM_CODE=stochastic_code('Orm')

def write(name,data):
 data['run_id']=PAYLOAD['run_id']
 (OUT/(name+'.json')).write_text(json.dumps(data,ensure_ascii=False,indent=2),'utf8')

def read(name):
 return json.loads((OUT/(name+'.json')).read_text('utf-8-sig'))

def sha(path):
 h=hashlib.sha256()
 with open(path,'rb') as f:
  for chunk in iter(lambda:f.read(1024*1024),b''):h.update(chunk)
 return h.hexdigest()

def num(v):
 return [round(float(v.x),3),round(float(v.y),3),round(float(v.z),3)]

def actor_xform(a):
 rot=a.get_actor_rotation()
 return dict(location=num(a.get_actor_location()),rotation=[round(rot.pitch,3),round(rot.yaw,3),round(rot.roll,3)],scale=num(a.get_actor_scale3d()))

def identity(label):
 if label.startswith('City_') or label.startswith('Campus_'):return label.split('_')[1]
 if label.startswith('Mall_'):return 'M1'
 return label.split('_')[0]

def floor_no(label):
 if '_F' not in label:return None
 tail=label.rsplit('_F',1)[1]
 return int(tail) if tail.isdigit() else None

def is_building_floor(label):
 return re.fullmatch(r'(?:S\d+|City_[A-Za-z0-9]+|Campus_[A-Za-z0-9]+|Mall_[A-Za-z0-9]+|[A-Z][A-Z0-9]*)_F\d+',label) is not None

def base_material(m):
 seen=set()
 while m and not isinstance(m,unreal.Material):
  key=m.get_path_name()
  if key in seen:return None
  seen.add(key)
  try:m=m.get_editor_property('parent')
  except Exception:return None
 return m

def blend_of(m,cache):
 if not m:return None,None
 key=m.get_path_name()
 if key in cache:return cache[key]
 b=base_material(m)
 if not b:
  cache[key]=(None,None);return cache[key]
 try:cache[key]=(str(b.get_editor_property('blend_mode')),str(b.get_editor_property('shading_model')))
 except Exception:cache[key]=(None,None)
 return cache[key]

def classify(imported,slot_name,mat_path,text_id,blend,shading):
 blob='%s %s %s'%(imported or '',slot_name or '',mat_path or '')
 if text_id:return 'protect','CityTextID localized or authored text'
 if 'localization' in blob.lower() or (mat_path and '/Localization/' in mat_path):return 'protect','localization asset'
 if shading and 'THIN_TRANSLUCENT' in shading:return 'protect','thin translucent; glass or water retained'
 if blend and ('TRANSLUCENT' in blend or 'ADDITIVE' in blend or 'MODULATE' in blend):return 'protect','non-opaque blend retained: '+blend
 if blend and 'MASKED' in blend:return 'protect','masked blend retained (cutout, foliage or text)'
 if PROTECT.search(blob):return 'protect','signage, text, glass, light, vegetation, furnishing, fixture or metal'
 blob=blob.replace('_',' ').replace('-',' ')
 for role,pat in ROLE_RE:
  if pat.search(blob):return role,'architectural slot name matches '+pat.pattern
 return 'exclude','not a floor/wall/roof/stairs/facade/trim envelope slot; left unchanged'

def instance_record(comp):
 if not isinstance(comp,unreal.InstancedStaticMeshComponent):return None,0
 n=comp.get_instance_count();h=hashlib.sha256()
 for i in range(n):
  t=comp.get_instance_transform(i,True);r=t.rotation
  h.update(('%0.5f,%0.5f,%0.5f,%0.6f,%0.6f,%0.6f,%0.6f,%0.5f,%0.5f,%0.5f\n'%(t.translation.x,t.translation.y,t.translation.z,r.x,r.y,r.z,r.w,t.scale3d.x,t.scale3d.y,t.scale3d.z)).encode())
 return h.hexdigest(),n

def component_row(actor,comp,cache):
 mesh=comp.get_editor_property('static_mesh') if isinstance(comp,unreal.StaticMeshComponent) else None
 mesh_slots=[];mesh_mats=[]
 if mesh:
  for s in mesh.get_editor_property('static_materials'):
   imported=str(s.get_editor_property('imported_material_slot_name'));slot_name=str(s.get_editor_property('material_slot_name'))
   mi=s.get_editor_property('material_interface');mesh_slots.append(dict(imported=imported,slot=slot_name));mesh_mats.append(mi.get_path_name() if mi else None)
 overrides=[];materials=[]
 for i in range(comp.get_num_materials()):
  m=comp.get_material(i);path=m.get_path_name() if m else None;materials.append(path)
  imported=mesh_slots[i]['imported'] if i<len(mesh_slots) else ''
  slot_name=mesh_slots[i]['slot'] if i<len(mesh_slots) else ''
  text=EAL.get_metadata_tag(m,'CityTextID') if m else ''
  blend,shading=blend_of(m,cache)
  role,reason=classify(imported,slot_name,path,text,blend,shading)
  overrides.append(dict(index=i,imported=imported,slot=slot_name,material=path,text=text or None,blend=blend,shading=shading,class_role=role,reason=reason))
 inst_sha,inst_n=instance_record(comp)
 rel_rot=comp.get_editor_property('relative_rotation')
 return dict(component=comp.get_name(),object_class=comp.get_class().get_name(),mesh=mesh.get_path_name() if mesh else None,collision=str(comp.get_collision_profile_name()) if hasattr(comp,'get_collision_profile_name') else None,relative_location=num(comp.get_editor_property('relative_location')),relative_rotation=[round(rel_rot.pitch,3),round(rel_rot.yaw,3),round(rel_rot.roll,3)],relative_scale=num(comp.get_editor_property('relative_scale3d')),mesh_materials=mesh_mats,materials=materials,slots=overrides,instance_count=inst_n,instance_sha256=inst_sha)

def lights_of(actors):
 rows=[]
 for a in actors:
  if isinstance(a,(unreal.PointLight,unreal.DirectionalLight,unreal.SkyLight,unreal.RectLight,unreal.SpotLight)):
   c=a.get_component_by_class(unreal.LightComponent)
   rows.append(dict(label=a.get_actor_label(),object_class=a.get_class().get_name(),xform=actor_xform(a),intensity=c.get_editor_property('intensity') if c else None))
 rows.sort(key=lambda r:r['label']);return rows

def inventory():
 actors=list(SUB.get_all_level_actors());cache={};rows=[];buildings=[]
 plan=json.loads((ROOT/'Docs/StreetSocialCity/city-plan.json').read_text('utf-8'))['buildings']
 scene_path=ROOT/'output/street-social-city-production/scene-integration-v1/inventory.json'
 scene=json.loads(scene_path.read_text('utf-8'))['rows'] if scene_path.is_file() else []
 scene_ids={q['id'] for q in scene if q.get('kind') in ['shop','campus','mall','city_building']}
 by_label={a.get_actor_label():a for a in actors}
 for a in actors:
  label=a.get_actor_label();comps=[]
  for comp in a.get_components_by_class(unreal.MeshComponent):comps.append(component_row(a,comp,cache))
  floor=is_building_floor(label) or label in scene_ids
  row=dict(label=label,object_class=a.get_class().get_name(),xform=actor_xform(a),building_floor=floor,building_id=identity(label) if floor else None,floor=floor_no(label) if floor else None,components=comps)
  rows.append(row)
  if floor:
   slots=[]
   for c in comps:
    for s in c['slots']:
     item=dict(s);item['component']=c['component'];slots.append(item)
   buildings.append(dict(label=label,building_id=row['building_id'],floor=row['floor'],mesh=[c['mesh'] for c in comps],slots=slots))
 seen={b['building_id'] for b in buildings}
 plan_ids=[b['id'] for b in plan]
 missing=[i for i in plan_ids if i not in seen]
 extra=sorted(i for i in seen if i not in set(plan_ids))
 write('inventory',dict(status='passed',map=MAP,actor_count=len(rows),building_floor_actors=len(buildings),plan_buildings=len(plan_ids),buildings_seen=len(seen),missing_from_map=missing,on_map_not_in_plan=extra,scene_inventory_ids=len(scene_ids),scene_ids_missing_on_map=sorted(scene_ids-set(by_label)),lights=lights_of(actors),buildings=buildings,actors=rows,map_sha256=sha(ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap')))

def load_catalog():
 path=OUT/'catalog.json'
 assert path.is_file(),'missing catalog.json; run romance-pbr catalog first'
 cat=json.loads(path.read_text('utf-8'))
 assert cat['set_count']==48 and len(cat['sets'])==48
 return cat

def assignments(plan_buildings,extras):
 cat=load_catalog();pools={role:[s for s in cat['sets'] if role in s['roles']] for role in ROLES}
 for role,pool in pools.items():assert pool,role
 used={};rows=[]
 ordered=[dict(id=b['id'],name=b.get('name'),category=b.get('category','unplanned'),source='city-plan') for b in plan_buildings]
 for bid in extras:ordered.append(dict(id=bid,name=bid,category='unplanned',source='map-only'))
 for b in ordered:
  salt=0;choice=None
  while salt<500:
   choice={}
   for role in ROLES:
    h=int(hashlib.sha256(('%s|%s|%s|%s'%(b['id'],b['category'],role,salt)).encode()).hexdigest()[:8],16)
    choice[role]=pools[role][h%len(pools[role])]['id']
   key=tuple(choice[r] for r in ROLES)
   if key not in used:
    used[key]=b['id'];break
   salt+=1
  else:raise RuntimeError('no unique palette for '+b['id'])
  seed=int(hashlib.sha256(b['id'].encode()).hexdigest()[:8],16)
  def channel(shift):return round(0.93+((seed>>shift)&255)/255.0*0.12,4)
  scale=0.92+(seed%7)*0.02
  periods={}
  for role in ROLES:
   meta=next(s for s in cat['sets'] if s['id']==choice[role]);periods[role]=round(meta['period_m']*100.0*scale,3)
  rows.append(dict(id=b['id'],name=b['name'],category=b['category'],source=b['source'],sets=choice,salt=salt,seed=round((seed%10000)/100.0,4),strength=round(0.92+(seed%5)*0.04,4),tint_a=[channel(0),channel(8),channel(16)],tint_b=[channel(4),channel(12),channel(20)],period_cm=periods))
 keys=[tuple(r['sets'][role] for role in ROLES) for r in rows]
 assert len(keys)==len(set(keys)),'palette tuples are not unique'
 return rows

def plan():
 inv=read('inventory');city=json.loads((ROOT/'Docs/StreetSocialCity/city-plan.json').read_text('utf-8'))['buildings']
 cat=load_catalog();ready={s['id']:(OUT/s['basecolor']).is_file() and (OUT/s['normal']).is_file() and (OUT/s['orm']).is_file() for s in cat['sets']}
 rows=assignments(city,inv['on_map_not_in_plan']);by_id={r['id']:r for r in rows}
 decisions=[];counts={k:0 for k in ['protect','exclude']+ROLES};covered=set()
 for building in inv['buildings']:
  spec=by_id.get(building['building_id']);covered.add(building['building_id'])
  for slot in building['slots']:
   role=slot['class_role'];action='preserve'
   if spec and role in ROLES:action='override'
   counts[role if role in counts else 'exclude']=counts.get(role,0)+1
   decisions.append(dict(actor=building['label'],building=building['building_id'],floor=building['floor'],component=slot['component'],slot=slot['index'],imported=slot['imported'],slot_name=slot['slot'],role=role,action=action,reason=slot['reason'],material=slot['material'],set=spec['sets'][role] if action=='override' else None,textures_ready=ready.get(spec['sets'][role],False) if action=='override' else None))
 missing=[b['id'] for b in city if b['id'] not in covered]
 write('plan',dict(status='passed',buildings=len(rows),assignments=rows,decisions=decisions,counts=counts,missing_from_map=missing,on_map_not_in_plan=inv['on_map_not_in_plan'],textures_ready=sum(1 for v in ready.values() if v),textures_expected=48,note='Plan does not require the 48 images to exist. Install does.'))
 html=['<!doctype html><meta charset="utf-8"><title>Romance coverage</title><style>body{font:15px/1.4 sans-serif;margin:24px;background:#f6f1ea} td{padding:4px 8px;border-bottom:1px solid #ddd}</style>','<h1>建築覆蓋</h1>','<p>計畫 %d 棟，地圖看到 %d 棟，缺 %d。</p>'%(len(city),len(covered),len(missing)),'<p>槽位統計：%s</p>'%json.dumps(counts,ensure_ascii=False),'<h2>地圖上缺少的計畫建築</h2><ul>']
 html+=['<li>%s</li>'%i for i in missing];html.append('</ul><h2>指派</h2><table><tr><th>建築</th><th>立面</th><th>牆</th><th>地</th><th>屋頂</th><th>樓梯</th><th>線腳</th></tr>')
 for r in rows:html.append('<tr><td>%s %s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td></tr>'%(r['id'],r['category'],r['sets']['facade'],r['sets']['wall'],r['sets']['floor'],r['sets']['roof'],r['sets']['stairs'],r['sets']['trim']))
 html.append('</table>');(OUT/'coverage.html').write_text(''.join(html),'utf8')

def node(mat,cls,x,y):
 return MEL.create_material_expression(mat,cls,x,y)

def names(fn,obj):
 try:return [str(x) for x in fn(obj)]
 except Exception:return []

def link(src,dst,inp):
 ins=names(MEL.get_material_expression_input_names,dst);outs=names(MEL.get_material_expression_output_names,src) or ['']
 use=inp if inp in ins else ('' if inp not in ins else inp)
 if use not in ins and ins:use='' if '' in ins else ins[0]
 for out in ['',*outs]:
  if MEL.connect_material_expressions(src,out,dst,use):return
 raise RuntimeError('link %s -> %s inp=%s ins=%s outs=%s'%(src.get_class().get_name(),dst.get_class().get_name(),use,ins,outs))

def scalar(mat,name,value,x,y):
 n=node(mat,unreal.MaterialExpressionScalarParameter,x,y);n.set_editor_property('parameter_name',name);n.set_editor_property('default_value',float(value));return n

def vector(mat,name,value,x,y):
 n=node(mat,unreal.MaterialExpressionVectorParameter,x,y);n.set_editor_property('parameter_name',name);n.set_editor_property('default_value',unreal.LinearColor(value[0],value[1],value[2],1));return n

def const(mat,value,x,y):
 n=node(mat,unreal.MaterialExpressionConstant,x,y);n.set_editor_property('r',float(value));return n

def custom(mat,pairs,code,x,y):
 n=node(mat,unreal.MaterialExpressionCustom,x,y);defs=[]
 for name,_src in pairs:
  q=unreal.CustomInput();q.set_editor_property('input_name',name);defs.append(q)
 n.set_editor_property('inputs',defs);n.set_editor_property('code',code);n.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT3)
 for name,src in pairs:link(src,n,name)
 return n

def mask(mat,src,r,g,b,a,x,y):
 n=node(mat,unreal.MaterialExpressionComponentMask,x,y)
 for key,val in (('r',r),('g',g),('b',b),('a',a)):n.set_editor_property(key,val)
 link(src,n,'');return n

def compile_errors(mat):
 err=MEL.recompile_material(mat)
 return [] if not err else [str(e) for e in err]

def save_asset(asset):
 for attempt in range(5):
  if EAL.save_loaded_asset(asset):return
  time.sleep(1)
 raise RuntimeError('asset save still blocked: '+asset.get_path_name())

def import_texture(set_id,kind,file,srgb,compression):
 unreal.SystemLibrary.execute_console_command(unreal.EditorLevelLibrary.get_editor_world(),'Editor.AsyncTextureCompilation 0')
 name='T_%s_%s'%(set_id,kind);path=PREFIX+'/Textures/'+name;digest=sha(file)
 asset=EAL.load_asset(path) if EAL.does_asset_exist(path) else None
 if asset and EAL.get_metadata_tag(asset,'RomanceSourceSHA')==digest and asset.get_editor_property('max_texture_size')==2048 and bool(asset.get_editor_property('srgb'))==srgb and asset.get_editor_property('compression_settings')==compression and (kind!='normal' or not asset.get_editor_property('flip_green_channel')):
  assert asset.blueprint_get_size_x()==2048 and asset.blueprint_get_size_y()==2048,('cached size',path,asset.blueprint_get_size_x(),asset.blueprint_get_size_y())
  return asset
 if asset is None or EAL.get_metadata_tag(asset,'RomanceSourceSHA')!=digest:
  task=unreal.AssetImportTask();task.set_editor_property('filename',str(file));task.set_editor_property('destination_path',PREFIX+'/Textures');task.set_editor_property('destination_name',name);task.set_editor_property('automated',True);task.set_editor_property('replace_existing',True);task.set_editor_property('save',True);ATS.import_asset_tasks([task]);asset=EAL.load_asset(path)
 assert isinstance(asset,unreal.Texture2D),path
 asset.set_editor_property('srgb',srgb);asset.set_editor_property('compression_settings',compression);asset.set_editor_property('max_texture_size',2048);asset.set_editor_property('lod_bias',0)
 if kind=='normal':
  asset.set_editor_property('lod_group',unreal.TextureGroup.TEXTUREGROUP_WORLD_NORMAL_MAP);asset.set_editor_property('flip_green_channel',False)
 EAL.set_metadata_tag(asset,'RomanceSourceSHA',digest);EAL.set_metadata_tag(asset,'RomanceSet',set_id);EAL.set_metadata_tag(asset,'RomanceKind',kind)
 save_asset(asset)
 if asset.blueprint_get_size_x()!=2048 or asset.blueprint_get_size_y()!=2048:unreal.AutomationLibrary.finish_loading_before_screenshot()
 assert asset.blueprint_get_size_x()==2048 and asset.blueprint_get_size_y()==2048,('imported size',name,asset.blueprint_get_size_x(),asset.blueprint_get_size_y())
 assert bool(asset.get_editor_property('srgb'))==srgb
 return asset

def fallback_master(mat,textures):
 mat.set_editor_property('tangent_space_normal',True)
 MEL.delete_all_material_expressions(mat)
 P=node(mat,unreal.MaterialExpressionWorldPosition,-1800,0)
 N=node(mat,unreal.MaterialExpressionVertexNormalWS,-1800,200)
 seed=scalar(mat,'RomanceSeed',0,-1800,400);period=scalar(mat,'RomancePeriod',200,-1800,520);strength=scalar(mat,'RomanceStrength',1,-1800,640)
 tint_a=vector(mat,'RomanceTintA',[1,0.98,0.94],-1800,760);tint_b=vector(mat,'RomanceTintB',[0.96,0.98,0.95],-1800,900)
 try:inst=node(mat,unreal.MaterialExpressionPerInstanceRandom,-1800,1040)
 except Exception:inst=const(mat,0,-1800,1040)
 seed=add(mat,seed,inst,-1600,400)
 xy=mask(mat,P,True,True,False,False,-1500,0);xz=mask(mat,P,True,False,True,False,-1500,120)
 div_xy=node(mat,unreal.MaterialExpressionDivide,-1300,0);link(xy,div_xy,'A');link(period,div_xy,'B')
 div_xz=node(mat,unreal.MaterialExpressionDivide,-1300,120);link(xz,div_xz,'A');link(period,div_xz,'B')
 nz=mask(mat,N,False,False,True,False,-1500,240);ab=node(mat,unreal.MaterialExpressionAbs,-1300,240);link(nz,ab,'')
 def offset_uv(base_uv,ox,oy,y):
  ax=node(mat,unreal.MaterialExpressionMultiply,-1100,y);link(seed,ax,'A');link(const(mat,ox,-1250,y),ax,'B')
  ay=node(mat,unreal.MaterialExpressionMultiply,-1100,y+80);link(seed,ay,'A');link(const(mat,oy,-1250,y+80),ay,'B')
  app=node(mat,unreal.MaterialExpressionAppendVector,-900,y);link(ax,app,'A');link(ay,app,'B')
  added=add(mat,base_uv,app,-700,y);return added
 samples=[]
 for i,(ox,oy) in enumerate(((0.17,0.31),(0.53,0.11),(0.07,0.67))):
  uv=node(mat,unreal.MaterialExpressionLinearInterpolate,-520,i*180)
  link(offset_uv(div_xy,ox,oy,-200+i*40),uv,'A');link(offset_uv(div_xz,ox,oy,40+i*40),uv,'B');link(ab,uv,'Alpha')
  s=node(mat,unreal.MaterialExpressionTextureSampleParameter2D,-240,i*180)
  s.set_editor_property('parameter_name','RomanceBase');s.set_editor_property('texture',textures['basecolor']);s.set_editor_property('sampler_type',unreal.MaterialSamplerType.SAMPLERTYPE_COLOR);link(uv,s,'UVs');samples.append(s)
 acc=add(mat,samples[0],samples[1],80,0);acc=add(mat,acc,samples[2],200,0)
 third=node(mat,unreal.MaterialExpressionMultiply,340,0);link(acc,third,'A');link(const(mat,1/3,220,80),third,'B')
 sx=mask(mat,P,True,False,False,False,-200,520);scaled=node(mat,unreal.MaterialExpressionMultiply,0,520);link(sx,scaled,'A');link(const(mat,0.0035,-80,600),scaled,'B')
 sine=node(mat,unreal.MaterialExpressionSine,180,520);link(scaled,sine,'')
 waved=add(mat,sine,const(mat,1,180,640),340,520);half=node(mat,unreal.MaterialExpressionMultiply,500,520);link(waved,half,'A');link(const(mat,0.5,420,640),half,'B')
 tint=node(mat,unreal.MaterialExpressionLinearInterpolate,680,200);link(tint_a,tint,'A');link(tint_b,tint,'B');link(half,tint,'Alpha')
 color=node(mat,unreal.MaterialExpressionMultiply,860,0);link(third,color,'A');link(tint,color,'B')
 assert MEL.connect_material_property(color,'',unreal.MaterialProperty.MP_BASE_COLOR)
 norms=[]
 for i,(ox,oy) in enumerate(((0.17,0.31),(0.53,0.11),(0.07,0.67))):
  uv=offset_uv(div_xz,ox,oy,800+i*30)
  s=node(mat,unreal.MaterialExpressionTextureSampleParameter2D,200,700+i*140)
  s.set_editor_property('parameter_name','RomanceNormal');s.set_editor_property('texture',textures['normal']);s.set_editor_property('sampler_type',unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL);link(uv,s,'UVs');norms.append(s)
 nacc=add(mat,norms[0],norms[1],520,760);nacc=add(mat,nacc,norms[2],680,760)
 nmean=node(mat,unreal.MaterialExpressionMultiply,840,760);link(nacc,nmean,'A');link(const(mat,1/3,760,860),nmean,'B')
 nrm=node(mat,unreal.MaterialExpressionNormalize,1000,760);link(nmean,nrm,'')
 assert MEL.connect_material_property(nrm,'',unreal.MaterialProperty.MP_NORMAL)
 orm=node(mat,unreal.MaterialExpressionTextureSampleParameter2D,200,1200)
 orm.set_editor_property('parameter_name','RomanceORM');orm.set_editor_property('texture',textures['orm']);orm.set_editor_property('sampler_type',unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR);link(offset_uv(div_xz,0.17,0.31,1160),orm,'UVs')
 def chan(r,g,b,prop,y):
  m=mask(mat,orm,r,g,b,False,520,y);assert MEL.connect_material_property(m,'',prop)
 chan(True,False,False,unreal.MaterialProperty.MP_AMBIENT_OCCLUSION,1180)
 chan(False,True,False,unreal.MaterialProperty.MP_ROUGHNESS,1280)
 chan(False,False,True,unreal.MaterialProperty.MP_METALLIC,1380)
 _=strength

def add(mat,a,b,x,y):
 n=node(mat,unreal.MaterialExpressionAdd,x,y);link(a,n,'A');link(b,n,'B');return n

def build_master(textures):
 path=PREFIX+'/Materials/M_RomancePBR'
 mat=EAL.load_asset(path) if EAL.does_asset_exist(path) else ATS.create_asset('M_RomancePBR',PREFIX+'/Materials',unreal.Material,unreal.MaterialFactoryNew())
 if EAL.get_metadata_tag(mat,'RomancePBRVersion')=='1' and not mat.get_editor_property('tangent_space_normal'):
  current=MEL.get_material_property_input_node(mat,unreal.MaterialProperty.MP_NORMAL)
  if current and current.get_class().get_name()=='MaterialExpressionCustom' and current.get_editor_property('code')==NORM_CODE:
   assert not compile_errors(mat);return mat,'custom'
 MEL.delete_all_material_expressions(mat)
 mat.set_editor_property('blend_mode',unreal.BlendMode.BLEND_OPAQUE)
 mat.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
 mat.set_editor_property('two_sided',False)
 mat.set_editor_property('tangent_space_normal',False)
 mat.set_editor_property('used_with_instanced_static_meshes',True)
 P=node(mat,unreal.MaterialExpressionWorldPosition,-1600,0)
 N=node(mat,unreal.MaterialExpressionVertexNormalWS,-1600,180)
 try:inst=node(mat,unreal.MaterialExpressionPerInstanceRandom,-1600,360)
 except Exception:inst=const(mat,0,-1600,360)
 seed=scalar(mat,'RomanceSeed',0,-1600,480);period=scalar(mat,'RomancePeriod',200,-1600,600);strength=scalar(mat,'RomanceStrength',1,-1600,720)
 tint_a=vector(mat,'RomanceTintA',[1,0.98,0.94],-1600,860);tint_b=vector(mat,'RomanceTintB',[0.96,0.98,0.95],-1600,1000)
 base=node(mat,unreal.MaterialExpressionTextureObjectParameter,-1600,1160);base.set_editor_property('parameter_name','RomanceBase');base.set_editor_property('texture',textures['basecolor']);base.set_editor_property('sampler_type',unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
 norm=node(mat,unreal.MaterialExpressionTextureObjectParameter,-1600,1320);norm.set_editor_property('parameter_name','RomanceNormal');norm.set_editor_property('texture',textures['normal']);norm.set_editor_property('sampler_type',unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
 orm=node(mat,unreal.MaterialExpressionTextureObjectParameter,-1600,1480);orm.set_editor_property('parameter_name','RomanceORM');orm.set_editor_property('texture',textures['orm']);orm.set_editor_property('sampler_type',unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
 common=[('P',P),('N',N),('Inst',inst),('Seed',seed),('Period',period)]
 color=custom(mat,common+[('TintA',tint_a),('TintB',tint_b),('Base',base)],COLOR_CODE,-400,0)
 normal=custom(mat,common+[('Strength',strength),('Norm',norm)],NORM_CODE,-400,400)
 packed=custom(mat,common+[('Orm',orm)],ORM_CODE,-400,800)
 assert MEL.connect_material_property(color,'',unreal.MaterialProperty.MP_BASE_COLOR)
 assert MEL.connect_material_property(normal,'',unreal.MaterialProperty.MP_NORMAL)
 for mask_rgba,prop,y in (((True,False,False),unreal.MaterialProperty.MP_AMBIENT_OCCLUSION,900),((False,True,False),unreal.MaterialProperty.MP_ROUGHNESS,1000),((False,False,True),unreal.MaterialProperty.MP_METALLIC,1100)):
  m=mask(mat,packed,*mask_rgba,False,0,y);assert MEL.connect_material_property(m,'',prop)
 errors=compile_errors(mat);mode='custom'
 if errors:
  unreal.log_warning('Romance custom shader failed, using node fallback: '+str(errors[:4]))
  fallback_master(mat,textures);errors=compile_errors(mat);mode='fallback'
  assert not errors,errors
 EAL.set_metadata_tag(mat,'RomancePBRVersion','1');EAL.set_metadata_tag(mat,'RomancePBRShader',mode)
 save_asset(mat)
 assert mat.get_editor_property('used_with_instanced_static_meshes')
 return mat,mode

def make_instance(master,spec,role,textures):
 sid=spec['sets'][role];name='MI_%s_%s'%(spec['id'],role);path=PREFIX+'/Materials/'+name
 mi=EAL.load_asset(path) if EAL.does_asset_exist(path) else ATS.create_asset(name,PREFIX+'/Materials',unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew())
 if EAL.get_metadata_tag(mi,'RomanceSet')==sid and mi.get_editor_property('parent')==master:
  values=[('RomanceBase','basecolor'),('RomanceNormal','normal'),('RomanceORM','orm')]
  ok=all(MEL.get_material_instance_texture_parameter_value(mi,p)==textures[sid][k] for p,k in values)
  ok=ok and abs(MEL.get_material_instance_scalar_parameter_value(mi,'RomancePeriod')-spec['period_cm'][role])<.001 and abs(MEL.get_material_instance_scalar_parameter_value(mi,'RomanceSeed')-spec['seed'])<.001
  if ok:return mi
 MEL.set_material_instance_parent(mi,master)
 MEL.set_material_instance_texture_parameter_value(mi,'RomanceBase',textures[sid]['basecolor'])
 MEL.set_material_instance_texture_parameter_value(mi,'RomanceNormal',textures[sid]['normal'])
 MEL.set_material_instance_texture_parameter_value(mi,'RomanceORM',textures[sid]['orm'])
 MEL.set_material_instance_scalar_parameter_value(mi,'RomanceSeed',float(spec['seed']))
 MEL.set_material_instance_scalar_parameter_value(mi,'RomancePeriod',float(spec['period_cm'][role]))
 MEL.set_material_instance_scalar_parameter_value(mi,'RomanceStrength',float(spec['strength']))
 MEL.set_material_instance_vector_parameter_value(mi,'RomanceTintA',unreal.LinearColor(*spec['tint_a'],1))
 MEL.set_material_instance_vector_parameter_value(mi,'RomanceTintB',unreal.LinearColor(*spec['tint_b'],1))
 EAL.set_metadata_tag(mi,'RomancePBRVersion','1');EAL.set_metadata_tag(mi,'RomanceBuilding',spec['id']);EAL.set_metadata_tag(mi,'RomanceRole',role);EAL.set_metadata_tag(mi,'RomanceSet',sid)
 save_asset(mi)
 got=MEL.get_material_instance_texture_parameter_value(mi,'RomanceBase');assert got and got.get_path_name()==textures[sid]['basecolor'].get_path_name()
 return mi

def backup():
 folder=OUT/'backups';folder.mkdir(parents=True,exist_ok=True);rows=[]
 umap=ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap'
 dest=folder/('UniversityCity-'+sha(umap)[:16]+'.umap')
 if not dest.exists():shutil.copy2(umap,dest)
 assert sha(dest)==sha(umap);rows.append(dict(kind='map',source=str(umap),backup=str(dest),sha256=sha(dest)))
 for name in ['__ExternalActors__','__ExternalObjects__']:
  src=ROOT/'StreetSocialCity/Content'/name;dst=folder/name
  if not src.exists():rows.append(dict(kind=name,present=False));continue
  if not dst.exists():shutil.copytree(src,dst)
  rows.append(dict(kind=name,present=True,backup=str(dst)))
 return rows

def install():
 inventory();plan()
 inv=read('inventory');plan_doc=read('plan');spec={r['id']:r for r in plan_doc['assignments']}
 map_path=ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap'
 expected=[inv['map_sha256']]
 assert sha(map_path)==expected[0],'map changed after inventory; refresh inventory before install'
 def save_map():
  assert sha(map_path)==expected[0],'another session saved the map during install; refusing overwrite'
  for attempt in range(5):
   assert sha(map_path)==expected[0],'another session saved the map during retry'
   if unreal.EditorLevelLibrary.save_current_level():
    expected[0]=sha(map_path);return
   time.sleep(1)
  raise RuntimeError('map save still blocked after five bounded attempts')
 needed=sorted({d['set'] for d in plan_doc['decisions'] if d['action']=='override'})
 missing=[s for s in needed if not (OUT/'sets'/s/'basecolor.png').is_file() or not (OUT/'sets'/s/'normal.png').is_file() or not (OUT/'sets'/s/'orm.png').is_file()]
 assert not missing,('textures not ready',missing[:12])
 textures={}
 for sid in needed:
  textures[sid]=dict(
   basecolor=import_texture(sid,'basecolor',OUT/'sets'/sid/'basecolor.png',True,unreal.TextureCompressionSettings.TC_DEFAULT),
   normal=import_texture(sid,'normal',OUT/'sets'/sid/'normal.png',False,unreal.TextureCompressionSettings.TC_NORMALMAP),
   orm=import_texture(sid,'orm',OUT/'sets'/sid/'orm.png',False,unreal.TextureCompressionSettings.TC_MASKS))
 master,shader=build_master(textures[needed[0]])
 copies=backup();manifest_path=OUT/'restore-manifest.json'
 previous=json.loads(manifest_path.read_text('utf-8'))['slots'] if manifest_path.is_file() else []
 known={(q['actor'],q['component'],q['slot']):q for q in previous};manifest=[]
 original_actors={a['label']:a for a in inv['actors']}
 actors={a.get_actor_label():a for a in SUB.get_all_level_actors()}
 applied=[];covered=set();instances={}
 for decision in plan_doc['decisions']:
  if decision['action']!='override':continue
  src=original_actors[decision['actor']]
  comp_src=next(c for c in src['components'] if c['component']==decision['component'])
  slot=next(s for s in comp_src['slots'] if s['index']==decision['slot'])
  key=(decision['actor'],decision['component'],decision['slot'])
  row=known.get(key) or dict(actor=decision['actor'],component=decision['component'],slot=decision['slot'],material=slot['material'],mesh=comp_src['mesh'],mesh_materials=comp_src['mesh_materials'],xform=src['xform'],instance_count=comp_src['instance_count'],instance_sha256=comp_src['instance_sha256'],collision=comp_src['collision'])
  manifest.append(row);known[key]=row
 write('restore-manifest',dict(status='original_component_overrides',slots=manifest,note='Apply with the restore mode. Geometry and transforms are not part of the override.'))
 done=0
 for decision in plan_doc['decisions']:
  if decision['action']!='override':continue
  actor=actors[decision['actor']]
  row=known[(decision['actor'],decision['component'],decision['slot'])]
  comp=next(c for c in actor.get_components_by_class(unreal.MeshComponent) if c.get_name()==decision['component'])
  before=actor_xform(actor);inst=instance_record(comp);mesh=comp.get_editor_property('static_mesh');collision=str(comp.get_collision_profile_name());mesh_before=[mesh.get_material(i).get_path_name() if mesh and mesh.get_material(i) else None for i in range(comp.get_num_materials())]
  instance_key=(decision['building'],decision['role'])
  if instance_key not in instances:instances[instance_key]=make_instance(master,spec[decision['building']],decision['role'],textures)
  mi=instances[instance_key]
  actor.modify();comp.modify();comp.set_material(decision['slot'],mi)
  assert comp.get_material(decision['slot'])==mi
  assert actor_xform(actor)==before and instance_record(comp)==inst and comp.get_editor_property('static_mesh')==mesh and str(comp.get_collision_profile_name())==collision
  mesh_after=[mesh.get_material(i).get_path_name() if mesh and mesh.get_material(i) else None for i in range(comp.get_num_materials())]
  assert mesh_after==mesh_before,'shared mesh material was mutated'
  applied.append(dict(actor=decision['actor'],component=decision['component'],building=decision['building'],slot=decision['slot'],role=decision['role'],set=decision['set'],material=mi.get_path_name(),original=row['material']))
  covered.add(decision['building']);done+=1
  if done%200==0:
   write('install-progress',dict(status='running',applied=done,buildings=len(covered)))
 save_map()
 write('install',dict(status='passed',phase='saved',next_step='reopen the saved map and run audit; this receipt is the save, not the audit',shader=shader,master=master.get_path_name(),applied=len(applied),buildings=len(covered),slots=applied,backups=copies,restore_manifest=str(manifest_path),map_sha256=sha(ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap'),geometry_transforms_collision_lights_unchanged=True))

def same_xform(a,b):
 return a['location']==b['location'] and a['rotation']==b['rotation'] and a['scale']==b['scale']

def audit():
 unreal.SystemLibrary.execute_console_command(unreal.EditorLevelLibrary.get_editor_world(),'Editor.AsyncTextureCompilation 0')
 unreal.AutomationLibrary.finish_loading_before_screenshot()
 inv=read('inventory');plan_doc=read('plan');inst=read('install');actors={a.get_actor_label():a for a in SUB.get_all_level_actors()}
 assert sha(ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap')==inst['map_sha256'],'opened map does not match the saved install'
 issues=[];checks=0
 original={a['label']:a for a in inv['actors']}
 applied={(s['actor'],s['component'],s['slot']):s for s in inst['slots']}
 live_lights={r['label']:r for r in lights_of(list(actors.values()))}
 for old in inv['lights']:
  checks+=1
  now=live_lights.get(old['label'])
  if not now or now['intensity']!=old['intensity'] or now['xform']!=old['xform']:issues.append('light changed '+old['label'])
 for label,src in original.items():
  actor=actors.get(label)
  checks+=1
  if not actor:issues.append('missing actor '+label);continue
  if not same_xform(actor_xform(actor),src['xform']):issues.append('actor transform changed '+label)
  comps={c.get_name():c for c in actor.get_components_by_class(unreal.MeshComponent)}
  for src_comp in src['components']:
   comp=comps.get(src_comp['component'])
   checks+=1
   if not comp:issues.append('missing component '+label+' '+src_comp['component']);continue
   rel_rot=comp.get_editor_property('relative_rotation');rel=dict(location=num(comp.get_editor_property('relative_location')),rotation=[round(rel_rot.pitch,3),round(rel_rot.yaw,3),round(rel_rot.roll,3)],scale=num(comp.get_editor_property('relative_scale3d')))
   if rel['location']!=src_comp['relative_location'] or rel['rotation']!=src_comp['relative_rotation'] or rel['scale']!=src_comp['relative_scale']:issues.append('component transform changed '+label)
   if (comp.get_editor_property('static_mesh').get_path_name() if comp.get_editor_property('static_mesh') else None)!=src_comp['mesh']:issues.append('mesh changed '+label)
   if str(comp.get_collision_profile_name())!=src_comp['collision']:issues.append('collision changed '+label)
   inst_sha,inst_n=instance_record(comp)
   if inst_n!=src_comp['instance_count'] or inst_sha!=src_comp['instance_sha256']:issues.append('instances changed '+label+' '+src_comp['component'])
   mesh=comp.get_editor_property('static_mesh')
   if mesh:
    now=[mesh.get_material(i).get_path_name() if mesh.get_material(i) else None for i in range(len(src_comp['mesh_materials']))]
    checks+=1
    if now!=src_comp['mesh_materials']:issues.append('shared mesh material changed '+src_comp['mesh'])
   for slot in src_comp['slots']:
    checks+=1
    current=comp.get_material(slot['index']);path=current.get_path_name() if current else None
    key=(label,src_comp['component'],slot['index'])
    if key not in applied:
     if path!=slot['material']:issues.append('protected or excluded material changed %s %s slot %s'%(label,src_comp['component'],slot['index']))
    else:
     row=applied[key]
     if path!=row['material']:issues.append('override missing '+label)
     if not path.startswith(PREFIX):issues.append('override left the romance folder '+label)
     tex=MEL.get_material_instance_texture_parameter_value(current,'RomanceBase');nrm=MEL.get_material_instance_texture_parameter_value(current,'RomanceNormal');orm=MEL.get_material_instance_texture_parameter_value(current,'RomanceORM')
     checks+=3
     if not tex or tex.blueprint_get_size_x()!=2048 or tex.blueprint_get_size_y()!=2048 or not tex.get_editor_property('srgb'):issues.append('basecolor texture '+label)
     if not nrm or nrm.blueprint_get_size_x()!=2048 or nrm.get_editor_property('srgb') or nrm.get_editor_property('compression_settings')!=unreal.TextureCompressionSettings.TC_NORMALMAP:issues.append('normal texture '+label)
     if nrm and nrm.get_editor_property('flip_green_channel'):issues.append('DirectX green channel unexpectedly flipped '+label)
     if not orm or orm.blueprint_get_size_x()!=2048 or orm.get_editor_property('srgb') or orm.get_editor_property('compression_settings')!=unreal.TextureCompressionSettings.TC_MASKS:issues.append('orm texture '+label)
     if EAL.get_metadata_tag(current,'RomanceSet')!=row['set']:issues.append('set metadata '+label)
 by_building={}
 for d in plan_doc['decisions']:
  if d['action']!='override':continue
  by_building.setdefault(d['building'],[]).append(d)
  checks+=1
  if (d['actor'],d['component'],d['slot']) not in applied:issues.append('planned override missing %s %s slot %s'%(d['actor'],d['component'],d['slot']))
 for bid,decisions in by_building.items():
  checks+=1
  if not any((d['actor'],d['component'],d['slot']) in applied for d in decisions):issues.append('planned building has no override '+bid)
 master=EAL.load_asset(inst['master']);checks+=1
 if not master or not master.get_editor_property('used_with_instanced_static_meshes'):issues.append('master instancing flag')
 if master:
  checks+=4
  if compile_errors(master):issues.append('master shader compilation errors')
  if master.get_editor_property('tangent_space_normal'):issues.append('triplanar normal must use world space')
  normal_node=MEL.get_material_property_input_node(master,unreal.MaterialProperty.MP_NORMAL)
  if not normal_node or normal_node.get_class().get_name()!='MaterialExpressionCustom' or normal_node.get_editor_property('code')!=NORM_CODE:issues.append('world normal projection graph mismatch')
  if EAL.get_metadata_tag(master,'RomancePBRShader')!='custom':issues.append('required coherent triplanar shader not active')
 checks+=1
 if set(actors)!=set(original):issues.append('actor identities changed')
 write('audit',dict(status='passed' if not issues else 'failed',checks=checks,issues=issues[:80],issue_count=len(issues),reopened_map=MAP,applied=len(applied),buildings=inst['buildings']))
 assert not issues,str(issues[:8])

def restore():
 doc=read('restore-manifest');actors={a.get_actor_label():a for a in SUB.get_all_level_actors()};done=[]
 for row in doc['slots']:
  actor=actors[row['actor']];comp=next(c for c in actor.get_components_by_class(unreal.MeshComponent) if c.get_name()==row['component'])
  before=actor_xform(actor);inst=instance_record(comp)
  mat=EAL.load_asset(row['material']) if row['material'] else None
  comp.set_material(row['slot'],mat)
  assert actor_xform(actor)==before and instance_record(comp)==inst
  done.append(dict(actor=row['actor'],slot=row['slot'],material=row['material']))
 assert unreal.EditorLevelLibrary.save_current_level()
 write('restore',dict(status='passed',restored=len(done),map_sha256=sha(ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap')))

def render():
 inv=read('inventory');actors={a.get_actor_label():a for a in SUB.get_all_level_actors()}
 floors={}
 for b in inv['buildings']:floors.setdefault(b['building_id'],[]).append(b)
 for rows in floors.values():rows.sort(key=lambda r:(r['floor'] is None,r['floor'] or 0,r['label']))
 baseline=json.loads((ROOT/'output/street-social-city-production/resort-native-v1/camera-plan.json').read_text('utf8'))['views'][:6]
 views=[]
 for q in baseline:
  def ue(p):return [p[1]*100,p[0]*100,p[2]*100]
  views.append(dict(name=q['name'],building='context',actor='existing validated streetscape camera',eye=ue(q['eye']),target=ue(q['target']),fov=65))
 city={b['id']:b for b in json.loads((ROOT/'Docs/StreetSocialCity/city-plan.json').read_text('utf8'))['buildings']}
 for name,bid,close in [('campus-teaching','E1',False),('dorm','D1',False),('hotel-wall-close','H1',True),('shop-wall-close','S01',True)]:
  rows=floors.get(bid) or []
  if not rows:continue
  a=actors[rows[0]['label']];loc=a.get_actor_location();yaw=math.radians(a.get_actor_rotation().yaw)
  fwd=unreal.Vector(math.cos(yaw),math.sin(yaw),0);right=unreal.Vector(-math.sin(yaw),math.cos(yaw),0)
  width,depth=city[bid]['footprint_m'];half=width*50.;height=city[bid]['floors']*350.
  if close:
   target=loc-fwd*half+right*(depth*10.)+unreal.Vector(0,0,240);eye=target-fwd*500.+unreal.Vector(0,0,10.)
  else:
   boxes=[actors[r['label']].get_actor_bounds(False) for r in rows]
   lo=[min(getattr(o,k)-getattr(e,k) for o,e in boxes) for k in ('x','y','z')]
   hi=[max(getattr(o,k)+getattr(e,k) for o,e in boxes) for k in ('x','y','z')]
   cx=(lo[0]+hi[0])*.5;cy=(lo[1]+hi[1])*.5;span=max(hi[0]-lo[0],hi[1]-lo[1]);h=hi[2]-lo[2]
   target=unreal.Vector(cx,cy,lo[2]+h*.5);eye=unreal.Vector(cx-span*.4,lo[1]-span*1.6,hi[2]+span*.45)
  views.append(dict(name=name,building=bid,actor=rows[0]['label'],eye=[eye.x,eye.y,eye.z],target=[target.x,target.y,target.z],fov=50))
 assert len(views)>=8,'representative cameras missing'
 world=unreal.EditorLevelLibrary.get_editor_world();folder=OUT/'renders';folder.mkdir(exist_ok=True)
 state={'i':0,'phase':'setup','wait':60,'images':[],'busy':False};refs=[]
 def tick(dt):
  if state['busy']:return
  if state['wait']>0:state['wait']-=1;return
  state['busy']=True
  try:
   if state['phase']=='setup':
    q=views[state['i']];eye=unreal.Vector(*q['eye']);target=unreal.Vector(*q['target']);rot=unreal.MathLibrary.find_look_at_rotation(eye,target)
    unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(eye,rot)
    a=SUB.spawn_actor_from_class(unreal.SceneCapture2D,eye,rot);c=a.capture_component2d
    c.set_editor_property('capture_every_frame',False);c.set_editor_property('capture_on_movement',False)
    rt=unreal.RenderingLibrary.create_render_target2d(world,1600,1000,unreal.TextureRenderTargetFormat.RTF_RGBA8);refs.append(rt)
    c.set_editor_property('texture_target',rt);c.set_editor_property('capture_source',unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR);c.set_editor_property('fov_angle',q['fov']);c.set_editor_property('max_view_distance_override',100000.)
    state['current']=(q,a,c,rt);state['phase']='capture';state['wait']=60
   elif state['phase']=='capture':
    unreal.AutomationLibrary.finish_loading_before_screenshot();state['current'][2].capture_scene();state['phase']='export';state['wait']=10
   else:
    q,a,c,rt=state['current'];dest=folder/(q['name']+'.png')
    unreal.RenderingLibrary.export_render_target(world,rt,str(folder),dest.name)
    assert dest.exists() and dest.stat().st_size>5000
    state['images'].append(dict(name=q['name'],building=q['building'],actor=q['actor'],path=str(dest),sha256=hashlib.sha256(dest.read_bytes()).hexdigest(),fov=q['fov']))
    c.set_editor_property('texture_target',None);SUB.destroy_actor(a);unreal.RenderingLibrary.release_render_target2d(rt);refs.remove(rt);state['current']=None;state['i']+=1;state['phase']='setup';state['wait']=10
    if state['i']==len(views):
     unreal.unregister_slate_post_tick_callback(render_handle)
     write('render',dict(status='passed',images=state['images'],count=len(state['images']),map_sha256=sha(ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap'),note='SceneCapture only. Capture actors are destroyed. The level is not saved.'))
     unreal.SystemLibrary.quit_editor()
  except BaseException:
   unreal.unregister_slate_post_tick_callback(render_handle);write('render-error',dict(status='failed',error=traceback.format_exc()));unreal.log_error(traceback.format_exc());unreal.SystemLibrary.quit_editor()
  finally:state['busy']=False
 global render_handle,RENDER_REFS
 RENDER_REFS=(state,refs);render_handle=unreal.register_slate_post_tick_callback(tick)

def shader_probe():
 cat=load_catalog()
 sid=next(s['id'] for s in cat['sets'] if (OUT/'sets'/s['id']/'orm.png').is_file())
 textures=dict(basecolor=import_texture(sid,'basecolor',OUT/'sets'/sid/'basecolor.png',True,unreal.TextureCompressionSettings.TC_DEFAULT),normal=import_texture(sid,'normal',OUT/'sets'/sid/'normal.png',False,unreal.TextureCompressionSettings.TC_NORMALMAP),orm=import_texture(sid,'orm',OUT/'sets'/sid/'orm.png',False,unreal.TextureCompressionSettings.TC_MASKS))
 master,shader=build_master(textures)
 checked=[]
 if (OUT/'install.json').is_file():
  sets=sorted({r['set'] for r in read('install')['slots']})
  for used in sets:
   n=EAL.load_asset(PREFIX+'/Textures/T_'+used+'_normal');assert n
   assert not n.get_editor_property('flip_green_channel'),used+' has flipped DirectX green'
   assert not n.get_editor_property('srgb') and n.get_editor_property('compression_settings')==unreal.TextureCompressionSettings.TC_NORMALMAP,used+' normal settings'
   checked.append(used)
 write('shader',dict(status='passed',master=master.get_path_name(),shader=shader,test_set=sid,normal_settings_checked=checked))

def work():
 try:
  if MODE=='shader':shader_probe()
  elif MODE=='inventory':inventory();plan()
  elif MODE=='plan':plan()
  elif MODE=='install':install()
  elif MODE=='audit':audit()
  elif MODE=='restore':restore()
  elif MODE=='render':audit();render();return
 except BaseException:
  write(MODE+'-error',dict(status='failed',error=traceback.format_exc()));unreal.log_error(traceback.format_exc())
 unreal.SystemLibrary.quit_editor()

# Map loading pumps Slate; register the callback only after it completes.
assert unreal.EditorLevelLibrary.load_level('/Engine/Maps/Entry' if MODE=='shader' else MAP)
frames=0

def tick(dt):
 global frames
 frames+=1
 if frames<120:return
 unreal.unregister_slate_post_tick_callback(handle);work()
handle=unreal.register_slate_post_tick_callback(tick)
]====]
local payload={root=root,out=out,mode=mode,run_id=mode..'-'..os.date('%Y%m%d-%H%M%S'),source=root..'/Tools/street_city_romance_pbr.lua::UnrealPayload',code=code}
local payloadpath=out..'/'..mode..'-payload.json'
local f=assert(io.open(payloadpath,'wb'));f:write(json.encode(payload));f:close()
local function q(s)return "'"..s:gsub("'","''").."'" end
local function read(p)local f=assert(io.open(p,'rb'));local s=f:read('*a');f:close();return s end
local engine=json.decode(read('Tools/engine.local.json')).engine
local args={root..'/StreetSocialCity/StreetSocialCity.uproject','/Engine/Maps/Entry','-ExecutePythonScript='..root..'/Tools/run_street_city_unreal_deferred.py','-UserDir='..root..'/Saved/StreetSocialCityRomanceUser','-unattended','-RenderOffscreen','-NoAsyncLoadingThread','-NoAsyncPostLoad','-nosplash','-nosound','-nop4','-d3d12','-abslog='..out..'/'..mode..'-editor.log'}
for i,v in ipairs(args)do args[i]=q(v)end
local cmd="$env:STREET_CITY_DEFERRED_SCRIPT="..q(root..'/Tools/run_street_city_unreal_deferred.py').."; $env:STREET_CITY_DEFERRED_JSON="..q(payloadpath).."; $env:TEMP='E:/StreetSocialCityProduction/unreal-scratch'; $env:TMP=$env:TEMP; [Environment]::SetEnvironmentVariable('UE-LocalDataCachePath','E:/StreetSocialCityProduction/unreal-ddc','Process'); $p=Start-Process -FilePath "..q(engine..'/Engine/Binaries/Win64/UnrealEditor.exe')..' -ArgumentList @('..table.concat(args,',')..') -WindowStyle Hidden -RedirectStandardOutput '..q(out..'/'..mode..'-process.log')..' -RedirectStandardError '..q(out..'/'..mode..'-stderr.log')..' -PassThru; $null=$p.Handle; $p.WaitForExit(); $p.Refresh(); $nativeExit=$p.ExitCode; @{exit_code=$nativeExit;has_exited=$p.HasExited}|ConvertTo-Json|Set-Content -Encoding UTF8 -LiteralPath '..q(out..'/'..mode..'-exit.json')..'; exit $nativeExit'
assert(os.execute('powershell.exe -NoProfile -Command "'..cmd..'"'))
local result=json.decode(read(out..'/'..mode..'.json'))
assert(result.run_id==payload.run_id and (result.status=='passed' or (mode=='install' and result.status=='saved_requires_reopen_audit')),'Missing fresh successful receipt')
print('Romance PBR '..mode..' passed')
