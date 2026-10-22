import unreal,sys,traceback
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_streetscape import ROOT,OUT,read,write,sha
from street_city_native_foliage import foliage_material,apply_to_mesh
installed=read(OUT/'native-install.json');mats={aid:foliage_material(aid) for aid in ['camphor','cherry']};meshes={row['id']:unreal.load_asset(row['asset']) for row in installed['assets'] if row['id'] in mats}
for aid,mesh in meshes.items():apply_to_mesh(mesh,mats[aid])
assert unreal.EditorLevelLibrary.load_level('/Game/UniversityCity/Maps/UniversityCity');unreal.EditorPythonScripting.set_keep_python_script_alive(True);frames=0
def tick(dt):
 global frames
 frames+=1
 if frames<60:return
 unreal.unregister_slate_post_tick_callback(handle)
 try:
  subsystem=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);actors=list(subsystem.get_all_level_actors())
  for actor in actors:
   comp=actor.get_component_by_class(unreal.HierarchicalInstancedStaticMeshComponent)
   if comp:
    for aid,mesh in meshes.items():
     if comp.static_mesh==mesh:
      for i,slot in enumerate(mesh.get_editor_property('static_materials')):comp.set_material(i,slot.get_editor_property('material_interface'))
   if actor.get_actor_label()=='CitySkyLight':
    actor.light_component.set_editor_property('source_type',unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP);cube=unreal.EditorAssetLibrary.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap');assert cube;actor.light_component.set_editor_property('cubemap',cube);actor.light_component.set_intensity(2.5);actor.light_component.recapture_sky()
  fill=next((a for a in actors if a.get_actor_label()=='CityStreetFill'),None) or subsystem.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(55000,55000,50000),unreal.Rotator(pitch=-65,yaw=145,roll=0));fill.set_actor_label('CityStreetFill');fill.light_component.set_mobility(unreal.ComponentMobility.MOVABLE);fill.light_component.set_intensity(1.5);fill.light_component.set_cast_shadows(False)
  assert unreal.EditorLevelLibrary.save_current_level();write(OUT/'native-foliage-repair.json',dict(status='passed',map_sha256=sha(ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap'),materials={aid:mat.get_path_name() for aid,mat in mats.items()},source_sha256=sha(__file__)))
 except Exception:write(OUT/'native-foliage-repair.json',dict(status='failed',error=traceback.format_exc()));unreal.log_error(traceback.format_exc())
 finally:unreal.SystemLibrary.quit_editor()
handle=unreal.register_slate_post_tick_callback(tick)
