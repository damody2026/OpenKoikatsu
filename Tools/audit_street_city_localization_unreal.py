"""Reopen the saved city and independently verify every localized native slot."""
import unreal,json,sys,re,traceback
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_localization import OUT,LOCALES,sha,language_group_signature
EAL=unreal.EditorAssetLibrary;MEL=unreal.MaterialEditingLibrary

def norm(s):return re.sub('[^A-Za-z0-9_]','_',s)

def main():
 (OUT/'native-reopen-task.json').write_text(json.dumps(dict(status='in_progress')),'utf8')
 groups=['campus-all','mall','shops-all'];receipts=[]
 for group in groups:
  row=json.loads((OUT/('unreal-'+group+'.json')).read_text('utf8'))
  assert row['status']=='passed' and row['inputs_signature']==language_group_signature(group),group
  receipts.append(row)
 expected={r['actor']:r for receipt in receipts for r in receipt['actors']};assert len(expected)==387
 assert unreal.EditorLevelLibrary.load_level('/Game/UniversityCity/Maps/UniversityCity')
 actors={a.get_actor_label():a for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors() if a.get_actor_label() in expected}
 assert set(actors)==set(expected)
 models=json.loads((OUT/'models.json').read_text('utf8'))['buildings'];bindings={r['building']:{norm(b['material']):b for b in r['bindings']} for r in models}
 checked=set();texture_checks=set();result=[]
 for label,row in expected.items():
  actor=actors[label];comp=actor.static_mesh_component;assert comp.static_mesh.get_path_name()==row['mesh'],label
  assert [comp.get_material(i).get_path_name() for i in range(comp.get_num_materials())]==row['all_materials'],label
  slots=comp.static_mesh.get_editor_property('static_materials');required={}
  for i,slot in enumerate(slots):
   names=[norm(str(slot.get_editor_property(k))) for k in ['imported_material_slot_name','material_slot_name']]
   binding=next((bindings[row['building']][n] for n in names if n in bindings[row['building']]),None)
   if binding:required[i]=binding
  assert set(required)=={s['slot'] for s in row['slots']},(label,'Unlocalized text slot')
  for slot in row['slots']:
   mat=comp.get_material(slot['slot']);key=slot['semantic_id'];assert mat.get_path_name()==slot['material']
   assert EAL.get_metadata_tag(mat,'CityTextID')==key
   binding=required[slot['slot']];assert slot['variants']==binding['variants']
   if mat.get_path_name() in checked:continue
   expressions=MEL.get_material_expressions(mat)
   samples={str(n.get_editor_property('parameter_name')):n for n in expressions if isinstance(n,unreal.MaterialExpressionTextureSampleParameter2D) and str(n.get_editor_property('parameter_name')).startswith('CityText_')}
   assert set(samples)=={'CityText_'+l.replace('-','_') for l in LOCALES},mat.get_path_name()
   for locale in LOCALES:
    texture=samples['CityText_'+locale.replace('-','_')].get_editor_property('texture');source=binding['variants'][locale]
    assert EAL.get_metadata_tag(texture,'CityLanguageSourceSHA256')==sha(source),(label,key,locale)
    assert max(texture.blueprint_get_size_x(),texture.blueprint_get_size_y())<=2048
    texture_checks.add((texture.get_path_name(),sha(source)))
   controls=[n for n in expressions if isinstance(n,unreal.MaterialExpressionCollectionParameter)]
   assert any(str(n.get_editor_property('parameter_name'))=='LanguageIndex' and n.get_editor_property('collection').get_path_name().split('.')[0]=='/Game/UniversityCity/Localization/MPC_CityLanguage' for n in controls)
   assert isinstance(MEL.get_material_property_input_node(mat,unreal.MaterialProperty.MP_BASE_COLOR),unreal.MaterialExpressionIf)
   assert len([n for n in expressions if isinstance(n,unreal.MaterialExpressionIf)])>=2
   checked.add(mat.get_path_name())
  result.append(row)
 c=EAL.load_asset('/Game/UniversityCity/Localization/MPC_CityLanguage');world=unreal.EditorLevelLibrary.get_editor_world()
 for index in range(3):
  unreal.MaterialLibrary.set_scalar_parameter_value(world,c,'LanguageIndex',index)
  assert unreal.MaterialLibrary.get_scalar_parameter_value(world,c,'LanguageIndex')==index
 unreal.MaterialLibrary.set_scalar_parameter_value(world,c,'LanguageIndex',0)
 map_path=Path(__file__).resolve().parents[1]/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap'
 report=dict(status='passed_reopened_native_materials_and_runtime_parameter',locales=list(LOCALES),collection=c.get_path_name(),parameter='LanguageIndex',runtime_values_tested=[0,1,2],actors=result,actor_count=len(result),unique_materials=len(checked),unique_textures=len(texture_checks),geometry_reimported=False,models_manifest_sha256=sha(OUT/'models.json'),assets_manifest_sha256=sha(OUT/'assets.json'),native_map_sha256=sha(map_path),groups=groups)
 (OUT/'native-bindings.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),'utf8')
 report['auditor_sha256']=sha(__file__);(OUT/'native-bindings.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),'utf8')
 (OUT/'native-reopen-task.json').write_text(json.dumps(dict(status='passed',actors=len(result))),'utf8')
 unreal.log('CITY LOCALIZATION REOPEN PASSED '+str(len(result))+' actors')

if __name__=='__main__':
 try:main()
 except Exception:
  (OUT/'native-reopen-error.txt').write_text(traceback.format_exc(),'utf8');(OUT/'native-reopen-task.json').write_text(json.dumps(dict(status='failed',error=traceback.format_exc())),'utf8');raise
 finally:unreal.SystemLibrary.quit_editor()
