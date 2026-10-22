"""Install global-MPC language switching on existing native material slots.

Duplicate the actual adopted material so all non-color PBR connections survive.
No mesh imports, transforms, collision or geometry are changed by this script.
"""
import unreal,json,os,re,hashlib,traceback,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_localization import OUT,LOCALES,sha,language_group_signature
MEL=unreal.MaterialEditingLibrary;EAL=unreal.EditorAssetLibrary
BASE='/Game/UniversityCity/Localization';MAP='/Game/UniversityCity/Maps/UniversityCity'
GROUP=os.environ.get('STREET_CITY_LANGUAGE_GROUP','shops-01');CACHE={};ROWS=[]

def normalized(s):return re.sub('[^A-Za-z0-9_]','_',s)

def texture(path,key,locale):
 name='T_'+normalized(key)+'_'+locale.replace('-','_');dest=BASE+'/Textures/'+name;digest=sha(path)
 if EAL.does_asset_exist(dest):
  asset=EAL.load_asset(dest)
  if EAL.get_metadata_tag(asset,'CityLanguageSourceSHA256')==digest:return asset
 task=unreal.AssetImportTask();task.filename=str(path);task.destination_path=BASE+'/Textures';task.destination_name=name;task.automated=True;task.replace_existing=True;task.save=True
 unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task]);asset=EAL.load_asset(dest);assert isinstance(asset,unreal.Texture2D),(dest,task.imported_object_paths)
 assert max(asset.blueprint_get_size_x(),asset.blueprint_get_size_y())<=2048
 EAL.set_metadata_tag(asset,'CityLanguageSourceSHA256',digest);assert EAL.save_loaded_asset(asset);return asset

def collection():
 path=BASE+'/MPC_CityLanguage'
 c=EAL.load_asset(path) if EAL.does_asset_exist(path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset('MPC_CityLanguage',BASE,unreal.MaterialParameterCollection,unreal.MaterialParameterCollectionFactoryNew())
 if not c.get_editor_property('scalar_parameters'):
  p=unreal.CollectionScalarParameter();p.set_editor_property('parameter_name','LanguageIndex');p.set_editor_property('default_value',0);c.set_editor_property('scalar_parameters',[p]);assert EAL.save_loaded_asset(c)
 assert str(c.get_editor_property('scalar_parameters')[0].get_editor_property('parameter_name'))=='LanguageIndex';return c

def material(original,binding,c):
 key=binding['semantic_id'];paths=binding['variants'];identity=hashlib.sha256(json.dumps([sha(__file__),key,original.get_path_name(),[(l,sha(paths[l])) for l in LOCALES]],sort_keys=True).encode()).hexdigest();cachekey=(original.get_path_name(),identity)
 if cachekey in CACHE:return CACHE[cachekey]
 name='M_'+normalized(key)+'_'+identity[:12];dest=BASE+'/Materials/'+name
 if EAL.does_asset_exist(dest):
  result=EAL.load_asset(dest)
  if EAL.get_metadata_tag(result,'CityLanguageIdentity')==identity:CACHE[cachekey]=result;return result
 # Importer creates Material assets, not instances; never flatten other PBR.
 assert isinstance(original,unreal.Material),('Unsupported material type',original.get_path_name(),original.get_class().get_name())
 result=EAL.load_asset(dest) if EAL.does_asset_exist(dest) else EAL.duplicate_asset(original.get_path_name(),dest);assert result
 nodes=[]
 for locale in LOCALES:
  node=MEL.create_material_expression(result,unreal.MaterialExpressionTextureSampleParameter2D,-700,len(nodes)*240);node.set_editor_property('parameter_name','CityText_'+locale.replace('-','_'));node.set_editor_property('texture',texture(paths[locale],key,locale));nodes.append(node)
 control=MEL.create_material_expression(result,unreal.MaterialExpressionCollectionParameter,-1000,0);control.set_editor_property('collection',c);control.set_editor_property('parameter_name','LanguageIndex')
 def choose(threshold,low,high,x):
  node=MEL.create_material_expression(result,unreal.MaterialExpressionIf,x,0);node.set_editor_property('const_b',threshold)
  assert MEL.connect_material_expressions(control,'',node,'A')
  for pin,source in [('A > B',high),('A == B',high),('A < B',low)]:assert MEL.connect_material_expressions(source,'RGB' if source in nodes else '',node,pin),(key,pin)
  return node
 first=choose(.5,nodes[0],nodes[1],-350);last=choose(1.5,first,nodes[2],-100);assert MEL.connect_material_property(last,'',unreal.MaterialProperty.MP_BASE_COLOR)
 MEL.recompile_material(result);EAL.set_metadata_tag(result,'CityLanguageIdentity',identity);EAL.set_metadata_tag(result,'CityTextID',key);EAL.set_metadata_tag(result,'CityOriginalMaterial',original.get_path_name());EAL.set_metadata_tag(result,'CityLanguageVariants',json.dumps(paths));assert EAL.save_loaded_asset(result);CACHE[cachekey]=result;return result

def selected(label):
 if GROUP=='remaining-all':return bool(re.fullmatch(r'City_[A-Z0-9]+_F\d+',label))
 if GROUP=='smoke':return label=='S01_F01'
 if GROUP=='shops-all':return bool(re.fullmatch(r'S\d+_F\d+',label))
 if GROUP=='campus-all':return bool(re.fullmatch(r'Campus_[A-Z0-9]+_F\d+',label))
 if GROUP.startswith('shops-'):
  n=int(GROUP.split('-')[1]);return bool(re.fullmatch(r'S\d+_F\d+',label)) and n<=int(label.split('_')[0][1:])<n+12
 if GROUP.startswith('campus-'):
  from street_city_campus_contract import IDS
  n=int(GROUP.split('-')[1]);return any(label.startswith('Campus_'+b+'_F') for b in IDS[n:n+4])
 return GROUP=='mall' and label.startswith('Mall_M1_F')

def main():
 models=json.loads((OUT/'models.json').read_text('utf8'));by={r['building']:{normalized(b['material']):b for b in r['bindings']} for r in models['buildings']}
 if GROUP=='shader-smoke':
  samples=json.loads((OUT/'unreal-api.json').read_text('utf8'))['samples']['S01_F01'];binding=next(b for b in by['S01'].values() if b['semantic_id']=='building.S01');source=next(m for m in samples if m['name']==normalized(binding['material']));original=EAL.load_asset(source['path']);c=collection();result=material(original,binding,c);assert EAL.get_metadata_tag(result,'CityTextID')=='building.S01'
  world=unreal.EditorLevelLibrary.get_editor_world()
  for index in range(3):
   unreal.MaterialLibrary.set_scalar_parameter_value(world,c,'LanguageIndex',index);assert unreal.MaterialLibrary.get_scalar_parameter_value(world,c,'LanguageIndex')==index
  unreal.MaterialLibrary.set_scalar_parameter_value(world,c,'LanguageIndex',0)
  (OUT/'unreal-shader-smoke.json').write_text(json.dumps(dict(status='passed',material=result.get_path_name(),runtime_values_tested=[0,1,2]),indent=2),'utf8');return
 assert unreal.EditorLevelLibrary.load_level(MAP);actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors();c=collection();targets=[a for a in actors if selected(a.get_actor_label())];assert targets,('No actors in language group',GROUP)
 for actor in targets:
  label=actor.get_actor_label();bid=label.split('_')[0] if label.startswith('S') else label.split('_')[1];comp=actor.static_mesh_component;mesh=comp.static_mesh;loc=actor.get_actor_transform();collision=str(comp.get_collision_profile_name());slots=mesh.get_editor_property('static_materials');bound=[]
  for i,slot in enumerate(slots):
   names=[str(slot.get_editor_property(k)) for k in ['imported_material_slot_name','material_slot_name']];binding=next((by[bid][normalized(n)] for n in names if normalized(n) in by[bid]),None)
   if not binding:continue
   old=comp.get_material(i);original=old
   source=EAL.get_metadata_tag(old,'CityOriginalMaterial')
   if source:original=EAL.load_asset(source)
   new=material(original,binding,c);comp.set_material(i,new);assert comp.get_material(i)==new
   bound.append(dict(slot=i,semantic_id=binding['semantic_id'],original=original.get_path_name(),material=new.get_path_name(),variants=binding['variants']))
  assert actor.get_actor_transform()==loc and comp.static_mesh==mesh and str(comp.get_collision_profile_name())==collision
  ROWS.append(dict(actor=label,building=bid,mesh=mesh.get_path_name(),slots=bound,all_materials=[comp.get_material(i).get_path_name() for i in range(comp.get_num_materials())]));unreal.log('CITY LANGUAGE '+label+' '+str(len(bound)))
  if len(ROWS)%12==0:
   assert unreal.EditorLevelLibrary.save_current_level();unreal.SystemLibrary.collect_garbage()
 # Runtime MPC API: the same node is callable from a settings Blueprint.
 world=unreal.EditorLevelLibrary.get_editor_world()
 for index,locale in enumerate(LOCALES):
  unreal.MaterialLibrary.set_scalar_parameter_value(world,c,'LanguageIndex',float(index));actual=unreal.MaterialLibrary.get_scalar_parameter_value(world,c,'LanguageIndex');assert actual==index
 unreal.MaterialLibrary.set_scalar_parameter_value(world,c,'LanguageIndex',0)
 assert EAL.save_loaded_asset(c) and unreal.EditorLevelLibrary.save_current_level()
 receipt=dict(status='passed',group=GROUP,locales=list(LOCALES),collection=c.get_path_name(),parameter='LanguageIndex',runtime_values_tested=[0,1,2],geometry_transforms_collision_unchanged=True,actors=ROWS,models_manifest_sha256=sha(OUT/'models.json'),inputs_signature=language_group_signature(GROUP),installer_sha256=sha(__file__))
 (OUT/('unreal-'+GROUP+'.json')).write_text(json.dumps(receipt,ensure_ascii=False,indent=2),'utf8')

if __name__=='__main__':
 try:main()
 except Exception:
  (OUT/('unreal-'+GROUP+'-error.txt')).write_text(traceback.format_exc(),'utf8');raise
 finally:unreal.SystemLibrary.quit_editor()
