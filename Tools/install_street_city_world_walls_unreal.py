"""Metric, world-aligned wall override layer; preserve text, floors and actual meshes."""
import unreal,json,sys,hashlib,os,traceback,time,shutil
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_world_wall_art import ROOT,OUT,sha
from street_city_world_wall_rules import identity,floor_no,target,maps_for,bounds,read_materials
EAL=unreal.EditorAssetLibrary;MEL=unreal.MaterialEditingLibrary
BASE='/Game/UniversityCity/WorldWallGrok';DATA=read_materials();BOUNDS=bounds();CACHE={};TEXTURES={};ROWS=[]
SCRIPT_SHA=sha(__file__)

def texture(key,kind):
 q=DATA[key];path=q['maps'][kind]['path'];digest=sha(path);dest=BASE+'/Textures/T_'+digest[:24]+'_'+kind
 if dest in TEXTURES:return TEXTURES[dest]
 if EAL.does_asset_exist(dest):asset=EAL.load_asset(dest);assert EAL.get_metadata_tag(asset,'CityWallImageSHA')==digest
 else:
  task=unreal.AssetImportTask();task.filename=path;task.destination_path=BASE+'/Textures';task.destination_name=dest.rsplit('/',1)[1];task.automated=True;task.save=True;unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task]);asset=EAL.load_asset(dest);assert isinstance(asset,unreal.Texture2D)
  asset.set_editor_property('srgb',kind=='basecolor')
  if kind=='normal':asset.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_NORMALMAP)
  EAL.set_metadata_tag(asset,'CityWallImageSHA',digest);EAL.set_metadata_tag(asset,'CityWallTextureID',key+'/'+kind);assert EAL.save_loaded_asset(asset)
 expected=unreal.TextureCompressionSettings.TC_NORMALMAP if kind=='normal' else unreal.TextureCompressionSettings.TC_DEFAULT
 if asset.get_editor_property('compression_settings')!=expected or bool(asset.get_editor_property('srgb'))!=(kind=='basecolor'):
  asset.set_editor_property('compression_settings',expected);asset.set_editor_property('srgb',kind=='basecolor');assert EAL.save_loaded_asset(asset)
 assert max(asset.blueprint_get_size_x(),asset.blueprint_get_size_y())<=2048;TEXTURES[dest]=asset;return asset

def node(mat,cls):return MEL.create_material_expression(mat,cls)
def constant(mat,value):
 if isinstance(value,(list,tuple)):
  n=node(mat,unreal.MaterialExpressionConstant3Vector);n.set_editor_property('constant',unreal.LinearColor(*value,1));return n
 n=node(mat,unreal.MaterialExpressionConstant);n.set_editor_property('r',value);return n
def custom(mat,inputs,code,scalar=False):
 n=node(mat,unreal.MaterialExpressionCustom);defs=[]
 for name,(source,pin) in inputs.items():
  q=unreal.CustomInput();q.set_editor_property('input_name',name);defs.append(q)
 n.set_editor_property('inputs',defs);n.set_editor_property('code',code);n.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT1 if scalar else unreal.CustomMaterialOutputType.CMOT_FLOAT3)
 for name,(source,pin) in inputs.items():assert MEL.connect_material_expressions(source,pin,n,name),(name,code)
 return n
def color_pin(n):return 'RGB' if isinstance(n,(unreal.MaterialExpressionTextureSample,unreal.MaterialExpressionTextureSampleParameter2D)) else ''

def material(old,bid,no,role,origin):
 if EAL.get_metadata_tag(old,'CityWallOriginal'):old=EAL.load_asset(EAL.get_metadata_tag(old,'CityWallOriginal'))
 assert not EAL.get_metadata_tag(old,'CityTextID'),('Text material must never be painted',bid,old.get_path_name())
 ik,ek=maps_for(bid,no,role)
 if role=='facade':ik=ek
 identity_hash=hashlib.sha256(json.dumps([SCRIPT_SHA,bid,old.get_path_name(),ik,ek,DATA[ik]['identity'],DATA[ek]['identity'],origin,BOUNDS[bid]],sort_keys=True).encode()).hexdigest()
 if identity_hash in CACHE:return CACHE[identity_hash]
 dest=BASE+'/Materials/M_'+bid+'_'+identity_hash[:20]
 if EAL.does_asset_exist(dest):m=EAL.load_asset(dest);assert EAL.get_metadata_tag(m,'CityWallIdentity')==identity_hash;CACHE[identity_hash]=m;return m
 m=EAL.duplicate_asset(old.get_path_name(),dest);assert isinstance(m,unreal.Material)
 p=node(m,unreal.MaterialExpressionWorldPosition);n=node(m,unreal.MaterialExpressionVertexNormalWS)
 inputs={'P':(p,''),'N':(n,'')}
 for label,key in [('I',ik),('E',ek)]:
  for suffix,kind in [('C','basecolor'),('R','roughness'),('B','normal')]:
   t=node(m,unreal.MaterialExpressionTextureObjectParameter);t.set_editor_property('parameter_name',label+suffix);t.set_editor_property('texture',texture(key,kind));t.set_editor_property('sampler_type',unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL if kind=='normal' else unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR if kind=='roughness' else unreal.MaterialSamplerType.SAMPLERTYPE_COLOR);inputs[label+suffix]=(t,'')
 w,d=BOUNDS[bid];north,east=origin[0]/100,origin[1]/100
 # Source units and native positions are verified elsewhere; WorldPosition is cm.
 header=f'float2 uv=(abs(N.x)>abs(N.y)?P.yz:P.xz)/200.0; float edge=max(abs(P.x-{north*100:.6f})-{d*50-35:.6f},abs(P.y-{east*100:.6f})-{w*50-35:.6f}); float e=step(0.0,edge); float v=1.0-step(0.65,abs(N.z)); '
 properties=[('Color',unreal.MaterialProperty.MP_BASE_COLOR,[.7,.7,.7]),('Rough',unreal.MaterialProperty.MP_ROUGHNESS,.6),('Normal',unreal.MaterialProperty.MP_NORMAL,[0,0,1])]
 for label,prop,default in properties:
  original=MEL.get_material_property_input_node(m,prop);pin=MEL.get_material_property_input_node_output_name(m,prop) if original else '';original=original or constant(m,default);args=dict(inputs);args['Old']=(original,pin)
  if label=='Color':code='float3 value=lerp(Texture2DSample(IC,ICSampler,uv).rgb,Texture2DSample(EC,ECSampler,uv).rgb,e); return lerp(Old,value,v);'
  elif label=='Rough':code='float value=lerp(Texture2DSample(IR,IRSampler,uv).r,Texture2DSample(ER,ERSampler,uv).r,e); return lerp(Old,value,v);'
  else:code='float2 xy=lerp(Texture2DSample(IB,IBSampler,uv).rg,Texture2DSample(EB,EBSampler,uv).rg,e)*2.0-1.0; float3 value=float3(xy,sqrt(saturate(1.0-dot(xy,xy)))); return normalize(lerp(Old,value,v));'
  output=custom(m,args,header+code,scalar=label=='Rough');assert MEL.connect_material_property(output,'',prop)
 MEL.recompile_material(m)
 for key,value in dict(CityWallIdentity=identity_hash,CityWallOriginal=old.get_path_name(),CityWallBuilding=bid,CityWallInterior=ik,CityWallExterior=ek,CityWallRecipeSHA=SCRIPT_SHA,CityWallPeriodM='2',CityWallOrientation='vertical only; original horizontal surfaces preserved').items():EAL.set_metadata_tag(m,key,value)
 assert EAL.save_loaded_asset(m);CACHE[identity_hash]=m;return m

def refresh_locale_receipts(actors):
 folder=OUT/'before-localization-receipts';folder.mkdir(exist_ok=True);allowed={(q['actor'],q['slot']):q for q in ROWS}
 for group in ['shops-all','campus-all','mall','remaining-all']:
  path=OUT.parent/'localization-v1'/('unreal-'+group+'.json');old=path.read_bytes();r=json.loads(old);archive=folder/(group+'-'+hashlib.sha256(old).hexdigest()[:16]+'.json')
  if not archive.exists():archive.write_bytes(old)
  for row in r['actors']:
   comp=actors[row['actor']].static_mesh_component;current=[comp.get_material(i).get_path_name() for i in range(comp.get_num_materials())]
   assert len(current)==len(row['all_materials'])
   for i,(before,after) in enumerate(zip(row['all_materials'],current)):
    if before!=after:assert (row['actor'],i) in allowed,('Unexpected non-wall material mutation',row['actor'],i,before,after)
   for slot in row['slots']:assert comp.get_material(slot['slot']).get_path_name()==slot['material'],'Localized text was changed'
   row['all_materials']=current
  r['wall_override_refresh']=dict(original_receipt_sha256=hashlib.sha256(old).hexdigest(),wall_installer_sha256=SCRIPT_SHA,localized_slots_unchanged=True);path.write_text(json.dumps(r,ensure_ascii=False,indent=2),'utf8')

def main():
 inventory=json.loads((OUT.parent/'scene-integration-v1/inventory.json').read_text('utf8'));smoke=os.environ.get('STREET_CITY_LANGUAGE_GROUP')=='wall-smoke'
 if smoke:
  q=next(q for q in inventory['rows'] if q['id']=='City_H1_F01');prepared=json.loads((OUT.parent/'world-completion-v1/native-prepared-remaining-all.json').read_text('utf8'));a=next(a for a in prepared['rows'] if a['id']==q['id']);mesh=EAL.load_asset(a['asset']);slot=next(s for s in mesh.get_editor_property('static_materials') if target('H1',str(s.get_editor_property('material_slot_name'))));m=material(slot.get_editor_property('material_interface'),'H1',1,'interior',q['position_cm']);(OUT/'shader-smoke.json').write_text(json.dumps(dict(status='passed_asset_graph',material=m.get_path_name(),installer_sha256=SCRIPT_SHA),indent=2),'utf8');return
 assert len(DATA)==90 and json.loads((OUT/'visual-material-review.json').read_text('utf8'))['status']=='passed'
 mapfile=ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap';backup=OUT/'backups'/('UniversityCity-before-walls-'+sha(mapfile)[:16]+'.umap');backup.parent.mkdir(exist_ok=True)
 if not backup.exists():shutil.copy2(mapfile,backup)
 assert sha(backup)==sha(mapfile)
 import faulthandler
 trace=(OUT/'native-load-trace.txt').open('w');faulthandler.dump_traceback_later(40,file=trace)
 unreal.log('GROK WALL load saved map')
 assert unreal.EditorLevelLibrary.load_level(inventory['map']);faulthandler.cancel_dump_traceback_later();sub=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);actors={a.get_actor_label():a for a in sub.get_all_level_actors()};floors=[q for q in inventory['rows'] if q['kind'] in ['shop','campus','mall','city_building']];covered=set()
 for q in floors:
  actor=actors[q['id']];comp=actor.static_mesh_component;bid=identity(q['id']);no=floor_no(q['id']);transform=actor.get_actor_transform();mesh=comp.static_mesh;assert EAL.get_metadata_tag(mesh,'StreetCitySourceSHA256')==q['source_sha256'];collision=str(comp.get_collision_profile_name());count=0
  for i,s in enumerate(mesh.get_editor_property('static_materials')):
   old=comp.get_material(i)
   if EAL.get_metadata_tag(old,'CityTextID'):continue
   name=str(s.get_editor_property('imported_material_slot_name'));role=target(bid,name,no)
   if not role:continue
   new=material(old,bid,no,role,q['position_cm']);comp.set_material(i,new);assert comp.get_material(i)==new;ROWS.append(dict(actor=q['id'],building=bid,floor=no,slot=i,source_name=name,role=role,original=EAL.get_metadata_tag(new,'CityWallOriginal'),material=new.get_path_name(),identity=EAL.get_metadata_tag(new,'CityWallIdentity'),interior=EAL.get_metadata_tag(new,'CityWallInterior'),exterior=EAL.get_metadata_tag(new,'CityWallExterior')));count+=1;covered.add(bid)
  assert actor.get_actor_transform()==transform and comp.static_mesh==mesh and str(comp.get_collision_profile_name())==collision
  if len(covered)%8==0:assert unreal.EditorLevelLibrary.save_current_level()
  (OUT/'native-install-progress.json').write_text(json.dumps(dict(status='installing',last=q['id'],covered_buildings=len(covered),wall_slots=len(ROWS))), 'utf8');unreal.log('GROK WALL '+q['id']+' '+str(count))
 assert len(covered)==122,sorted(set(BOUNDS)-covered)
 assert unreal.EditorLevelLibrary.save_current_level();refresh_locale_receipts(actors)
 result=dict(status='passed',buildings=len(covered),floor_actors=len(floors),slots=ROWS,source_inventory_sha256=sha(OUT.parent/'scene-integration-v1/inventory.json'),map_sha256=sha(ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap'),installer_sha256=SCRIPT_SHA,geometry_mesh_transform_collision_unchanged=True,text_materials_unchanged=True)
 (OUT/'native-install.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),'utf8')
if __name__=='__main__':
 unreal.EditorPythonScripting.set_keep_python_script_alive(True);frames=0
 def tick(delta):
  global frames
  frames+=1
  if frames<60:return
  unreal.unregister_slate_post_tick_callback(handle)
  try:main()
  except Exception:(OUT/'native-install-error.txt').write_text(traceback.format_exc(),'utf8');unreal.log_error(traceback.format_exc())
  finally:unreal.SystemLibrary.quit_editor()
 handle=unreal.register_slate_post_tick_callback(tick)
