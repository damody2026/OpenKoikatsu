"""Idempotent facade signs, using the existing global language collection."""
import sys,json,traceback,os,math
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
import unreal
from street_city_localization import OUT,LOCALES,sha
from install_street_city_localization_unreal import material,collection,EAL,MEL,MAP
from street_city_unreal_import import import_mesh,ensure_mesh_materials
from street_city_remesh_gate import verify
DEST=OUT/'campus-signs'

def audit(rows,c):
 sub=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);actors={a.get_actor_label():a for a in sub.get_all_level_actors()}
 for r in rows:
  a=actors[r['actor']];comp=a.static_mesh_component;m=comp.get_material(0)
  assert comp.static_mesh.get_path_name()==r['mesh']
  assert m.get_path_name()==r['material'] and EAL.get_metadata_tag(m,'CityTextID')==r['key']
  assert str(comp.get_collision_profile_name())=='NoCollision'
  assert all(abs(v-w)<.01 for v,w in zip([a.get_actor_location().x,a.get_actor_location().y,a.get_actor_location().z],r['location_cm']))
  origin,extent=a.get_actor_bounds(False)
  assert all(abs(v-w)<.03 for v,w in zip(sorted([extent.x*.02,extent.y*.02]),sorted(r['size_m'][:2]))),(r['actor'],'Physical sign width/thickness mismatch')
  assert abs(extent.z*.02-r['size_m'][2])<.03,(r['actor'],'Physical sign height mismatch')
  assert abs((a.get_actor_rotation().yaw-r['yaw_deg']+180)%360-180)<.01
  samples={str(n.get_editor_property('parameter_name')):n for n in MEL.get_material_expressions(m) if isinstance(n,unreal.MaterialExpressionTextureSampleParameter2D)}
  for l in LOCALES:
   t=samples['CityText_'+l.replace('-','_')].get_editor_property('texture')
   assert EAL.get_metadata_tag(t,'CityLanguageSourceSHA256')==r['texture_sha256'][l]
  assert isinstance(MEL.get_material_property_input_node(m,unreal.MaterialProperty.MP_BASE_COLOR),unreal.MaterialExpressionIf)
 world=unreal.EditorLevelLibrary.get_editor_world()
 for value in range(3):
  unreal.MaterialLibrary.set_scalar_parameter_value(world,c,'LanguageIndex',value)
  assert unreal.MaterialLibrary.get_scalar_parameter_value(world,c,'LanguageIndex')==value
 unreal.MaterialLibrary.set_scalar_parameter_value(world,c,'LanguageIndex',0)

def main():
 assert unreal.EditorLevelLibrary.load_level(MAP)
 c=collection();sub=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);actors={a.get_actor_label():a for a in sub.get_all_level_actors()}
 if os.environ.get('STREET_CITY_LANGUAGE_GROUP')=='signs-audit':
  r=json.loads((DEST/'unreal.json').read_text('utf8'));assert r['manifest_sha256']==sha(DEST/'manifest.json');audit(r['actors'],c)
  r['status']='passed_reopened';r['runtime_values_tested']=[0,1,2];r['map_sha256']=sha(Path(__file__).resolve().parents[1]/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap')
  (DEST/'unreal-acceptance.json').write_text(json.dumps(r,ensure_ascii=False,indent=2),'utf8');return
 manifest=json.loads((DEST/'manifest.json').read_text('utf8'));rows=[]
 original={name:(a.static_mesh_component.static_mesh.get_path_name(),a.get_actor_transform()) for name,a in actors.items() if name.startswith('Campus_') and '_F' in name}
 assert len(original)==89
 for b in manifest['buildings']:
  verify(Path(b['remesh']['remesh_report']).parent,b['remesh']['sha256'])
  source=Path(b['portable']['source']);assert sha(source)==b['portable']['source_sha256']
  mesh=import_mesh(source,'/Game/UniversityCity/Localization/CampusSigns',b['building']+'_FacadeSign')
  original_mat=ensure_mesh_materials(mesh)[0];binding=b['portable']['bindings'][0];mat=material(original_mat,binding,c)
  floor=actors['Campus_'+b['building']+'_F01'];transform=floor.get_actor_transform()
  for p in b['placements']:
   name='CampusSign_'+p['id'];x,y,z=p['position_m'];point=transform.transform_location(unreal.Vector(x*100,-y*100,z*100))
   rot=unreal.Rotator(pitch=0,yaw=floor.get_actor_rotation().yaw-p['yaw_deg'],roll=0)
   a=actors.get(name)
   if a is None:a=sub.spawn_actor_from_object(mesh,point,rot);a.set_actor_label(name)
   a.static_mesh_component.set_static_mesh(mesh);a.set_actor_location_and_rotation(point,rot,False,True);a.static_mesh_component.set_material(0,mat);a.static_mesh_component.set_collision_profile_name('NoCollision');a.set_folder_path('Campus/'+b['building']+'/LocalizedSigns');a.set_editor_property('tags',['StreetCityManaged','CampusLocalizedSign',b['key']])
   rows.append(dict(actor=name,building=b['building'],key=b['key'],mesh=mesh.get_path_name(),material=mat.get_path_name(),location_cm=[point.x,point.y,point.z],size_m=b['size_m'],yaw_deg=rot.yaw,texture_sha256={l:b['variants'][l]['sha256'] for l in LOCALES}))
  unreal.log('CAMPUS SIGNS '+b['building'])
 assert len(rows)==manifest['sign_count']==34
 for name,(meshpath,transform) in original.items():
  assert actors[name].static_mesh_component.static_mesh.get_path_name()==meshpath and actors[name].get_actor_transform()==transform,(name,'Existing campus floor changed')
 audit(rows,c);assert unreal.EditorLevelLibrary.save_current_level()
 (DEST/'unreal.json').write_text(json.dumps(dict(status='passed_saved',manifest_sha256=sha(DEST/'manifest.json'),actors=rows,building_count=17,sign_count=34,existing_campus_floors_unchanged=89,runtime_values_tested=[0,1,2]),ensure_ascii=False,indent=2),'utf8')
 if (DEST/'unreal-error.txt').exists():(DEST/'unreal-error.txt').replace(DEST/'previous-install-error.txt')

if __name__=='__main__':
 try:main()
 except Exception:
  (DEST/'unreal-error.txt').write_text(traceback.format_exc(),'utf8');raise
 finally:unreal.SystemLibrary.quit_editor()
