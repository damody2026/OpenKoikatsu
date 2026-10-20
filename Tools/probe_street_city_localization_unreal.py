import unreal,json,traceback
from pathlib import Path
out=Path('C:/portable/OpenKoikatsu/output/street-social-city-production/localization-v1')
try:
 names=['get_material_property_input_node','get_texture_parameter_names','get_material_instance_texture_parameter_value','get_material_default_texture_parameter_value','get_material_expressions','get_material_expression_input_names','get_material_expression_input_types','connect_material_expressions','get_scalar_parameter_names']
 data={n:str(getattr(unreal.MaterialEditingLibrary,n,None).__doc__) for n in names};data['outer_api']=str(getattr(unreal,'get_objects_with_outer',None));data['collection']=str(unreal.MaterialParameterCollection.__doc__);data['scalar']=str(unreal.CollectionScalarParameter.__doc__)
 assert unreal.EditorLevelLibrary.load_level('/Game/UniversityCity/Maps/UniversityCity')
 actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors();samples={}
 for a in actors:
  if not isinstance(a,unreal.StaticMeshActor):continue
  label=a.get_actor_label()
  if label not in ['S01_F01','S02_F01','AD_F01','M1_F01']:continue
  c=a.static_mesh_component
  samples[label]=[]
  for i in range(c.get_num_materials()):
   mat=c.get_material(i)
   if not mat:continue
   row={'path':mat.get_path_name(),'class':mat.get_class().get_name(),'name':mat.get_name()}
   if isinstance(mat,unreal.MaterialInstanceConstant):
    row['parent']=mat.parent.get_path_name();row['textures']=[dict(name=str(t.parameter_info.name),texture=t.parameter_value.get_path_name() if t.parameter_value else None) for t in mat.get_editor_property('texture_parameter_values')]
   samples[label].append(row)
 data['samples']=samples
 (out/'unreal-api.json').write_text(json.dumps(data,indent=2),'utf8')
except Exception:(out/'unreal-api-error.txt').write_text(traceback.format_exc(),'utf8');raise
finally:unreal.SystemLibrary.quit_editor()
