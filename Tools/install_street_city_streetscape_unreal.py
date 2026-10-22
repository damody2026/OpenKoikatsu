"""Install certified street assets and real custom LODs as persistent HISM clusters."""
import unreal,sys,math,json,traceback
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_streetscape import ROOT,BASE,OUT,read,write,sha
import street_city_unreal_import as importer
from street_city_native_foliage import foliage_material,apply_to_mesh,configure_daylight
EAL=unreal.EditorAssetLibrary;api=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem);sub=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);delivery=read(OUT/'delivery.json');assert delivery['status']=='accepted_assets_requires_native';assert read(OUT/'asset-acceptance.json')['status']=='passed';deliverysha=sha(OUT/'delivery.json');assets={};rows=[];compounds=[];frames=0
def canonical(mesh):
 bound=importer.ensure_mesh_materials(mesh);slots=mesh.get_editor_property('static_materials')
 for slot,material in zip(slots,bound):slot.set_editor_property('material_interface',material)
 mesh.set_editor_property('static_materials',slots);return bound
try:
 for q in delivery['assets']:
  meshes=[]
  for lod in q['lods']:
   assert sha(lod['path'])==lod['sha256'];mesh=importer.import_mesh(lod['path'],'/Game/UniversityCity/StreetScenery/Assets/'+q['id'].replace('-','_'),q['id'].replace('-','_')+'_LOD'+str(lod['level']));canonical(mesh);meshes.append(mesh)
  mesh=meshes[0]
  if len(meshes)>1:
   for level,source in enumerate(meshes[1:],1):assert api.set_lod_from_static_mesh(mesh,level,source,0,True)==level
   screens=[1,.18,.055] if q['id'] in ('camphor','cherry') else [1,.04,.018];assert api.set_lod_screen_sizes(mesh,screens)
  assert api.get_lod_count(mesh)==len(q['lods']);counts=[mesh.get_num_triangles(i) for i in range(len(q['lods']))];assert counts==[r['triangles'] for r in q['lods']],(q['id'],counts)
  EAL.set_metadata_tag(mesh,'StreetSceneryDeliverySHA256',deliverysha);assert EAL.save_loaded_asset(mesh);assets[q['id']]=mesh;rows.append(dict(id=q['id'],asset=mesh.get_path_name(),lod_triangles=counts,lod_screen_sizes=list(api.get_lod_screen_sizes(mesh)),source_sha256=q['lods'][0]['sha256']))
  write(OUT/'native-import-progress.json',dict(completed=len(rows),assets=rows));unreal.log('STREET IMPORT '+q['id'])
 inventory={q['id']:q for q in read(BASE/'scene-integration-v1/inventory.json')['rows']}
 for q in delivery['facility_sources']:
  mesh=importer.import_mesh(q['path'],'/Game/UniversityCity/StreetScenery/SeparatedFacilities',q['label']);canonical(mesh);compounds.append(dict(label=q['label'],asset=mesh.get_path_name(),source_sha256=q['sha256']))
 # Build mipmaps for generated non-power-of-two PNGs without changing source art.
 for path in EAL.list_assets('/Game/UniversityCity/StreetScenery/Assets',recursive=True):
  if any('/'+aid+'/' in path for aid in ['camphor','cherry']):
   tex=EAL.load_asset(path)
   if isinstance(tex,unreal.Texture2D):
    tex.set_editor_property('power_of_two_mode',unreal.TexturePowerOfTwoSetting.STRETCH_TO_POWER_OF_TWO);tex.set_editor_property('max_texture_size',2048);tex.set_editor_property('do_scale_mips_for_alpha_coverage',True);tex.set_editor_property('alpha_coverage_thresholds',unreal.Vector4(0,0,0,.33));assert EAL.save_loaded_asset(tex)
 for aid in ['camphor','cherry']:apply_to_mesh(assets[aid],foliage_material(aid))
 assert unreal.EditorLevelLibrary.load_level('/Game/UniversityCity/Maps/UniversityCity');unreal.AutomationLibrary.finish_loading_before_screenshot()
except Exception:write(OUT/'native-install-error.json',dict(error=traceback.format_exc()));raise
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
def install():
 actors={a.get_actor_label():a for a in sub.get_all_level_actors()};bp=EAL.load_asset('/Game/UniversityCity/StreetScenery/BP_StreetCluster');assert bp;placed=[]
 for group in delivery['groups']:
  label=group['label'];aid=group['asset'];x,y,z=group['origin_m'];actor=actors.get(label)
  if actor:assert 'StreetScenery-v1' in [str(t) for t in actor.tags],('Refuse to change an unrelated actor',label)
  else:actor=sub.spawn_actor_from_class(bp.generated_class(),unreal.Vector(y*100,x*100,z*100));assert actor;actor.set_actor_label(label)
  actor.tags=[unreal.Name('StreetScenery-v1'),unreal.Name('Asset-'+aid),unreal.Name('Delivery-'+deliverysha)];actor.set_folder_path('UniversityCity/StreetScenery/'+aid);comp=actor.get_component_by_class(unreal.HierarchicalInstancedStaticMeshComponent);assert comp
  if actor.root_component:actor.root_component.set_mobility(unreal.ComponentMobility.STATIC)
  comp.set_mobility(unreal.ComponentMobility.STATIC);comp.set_static_mesh(assets[aid])
  materials=[s.get_editor_property('material_interface') for s in assets[aid].get_editor_property('static_materials')] if aid in ('camphor','cherry') else importer.ensure_mesh_materials(assets[aid])
  for i,material in enumerate(materials):comp.set_material(i,material)
  # Runtime setters keep this live Blueprint component. PostEditChange through
  # set_editor_property can reconstruct its owner and invalidate the old handle.
  comp.clear_instances();comp.set_collision_profile_name(group['collision']);comp.set_visibility(group['visible'],True);comp.set_hidden_in_game(not group['visible']);comp.set_cull_distances(group['cull_start_m']*100,group['cull_end_m']*100);comp.set_cast_shadow(group['visible'] and aid not in ('parking-mark','drain','tree-grate'))
  transforms=[]
  for q in group['instances']:
   px,py,pz=q['position_m'];transforms.append(unreal.Transform(location=unreal.Vector((py-y)*100,(px-x)*100,(pz-z)*100),rotation=unreal.Rotator(pitch=0,yaw=90-q['yaw_deg'],roll=0),scale=unreal.Vector(*q['scale'])))
  comp.add_instances(transforms,False,False,False);assert comp.get_instance_count()==len(transforms);placed.append(dict(label=label,asset=assets[aid].get_path_name(),instances=len(transforms)))
 for actor in sub.get_all_level_actors():
  if 'StreetScenery-v1' in [str(t) for t in actor.tags] and actor.get_actor_label() not in {q['label'] for q in delivery['groups']}:assert sub.destroy_actor(actor)
 for q in delivery['legacy_replacements']:
  actor=actors[q['label']];comp=actor.static_mesh_component;assert EAL.get_metadata_tag(comp.static_mesh,'StreetCitySourceSHA256')==q['source_sha256'];comp.set_collision_profile_name('NoCollision');comp.set_visibility(False,True);comp.set_hidden_in_game(True);actor.set_actor_hidden_in_game(True)
 for q in compounds:
  actor=actors[q['label']];mesh=EAL.load_asset(q['asset']);actor.static_mesh_component.set_static_mesh(mesh)
  for i,material in enumerate(importer.ensure_mesh_materials(mesh)):actor.static_mesh_component.set_material(i,material)
 configure_daylight(sub)
 assert unreal.EditorLevelLibrary.save_current_level();mapsha=sha(ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap')
 write(OUT/'native-install.json',dict(status='saved_requires_independent_audit',delivery_sha256=deliverysha,map_sha256=mapsha,assets=rows,compound_models=compounds,groups=placed,legacy_replacements=delivery['legacy_replacements'],installer_sha256=sha(__file__)))
 write(OUT/'activation.json',dict(status='installed_pending_independent_audit',delivery_sha256=deliverysha,legacy_replacements=delivery['legacy_replacements'],facility_sources=delivery['facility_sources'],map_sha256=mapsha))
def tick(dt):
 global frames
 frames+=1
 if frames<60:return
 unreal.unregister_slate_post_tick_callback(handle)
 try:install()
 except Exception:write(OUT/'native-install-error.json',dict(error=traceback.format_exc()));unreal.log_error(traceback.format_exc())
 finally:unreal.SystemLibrary.quit_editor()
handle=unreal.register_slate_post_tick_callback(tick)
