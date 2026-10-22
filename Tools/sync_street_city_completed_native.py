"""Deferred, checkpointed native adoption; never spawn while map initialization is pending."""
import unreal,json,sys,time,traceback
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_scene_inventory import ROOT,OUT,read,sha
from street_city_unreal_import import ensure_mesh_materials
inventory=read(OUT/'inventory.json');prepared=read(OUT/'prepared-updates.json');assert prepared['status']=='passed' and prepared['inventory_sha256']==sha(OUT/'inventory.json')
approved={q['id']:q for q in prepared['rows']};todo=[q for q in inventory['rows'] if q['id'] in approved];changed=[];task=OUT/'completion-sync-task.json';frames=0;index=0;busy=False
task.write_text(json.dumps(dict(status='loading_map',total=len(todo))), 'utf8');unreal.log('CITY COMPLETION SYNC loading map')
import faulthandler
tracefile=(OUT/'completion-load-trace.txt').open('w');faulthandler.dump_traceback_later(45,file=tracefile)
assert unreal.EditorLevelLibrary.load_level(inventory['map']);faulthandler.cancel_dump_traceback_later();unreal.log('CITY COMPLETION SYNC map load returned')
sub=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);all_actors=sub.get_all_level_actors();owned_by_source={q['source_sha256']:q for q in todo if q['kind'] in ('city_building','city_water','world_facility')};kept={};removed=[]
for actor in all_actors:
 if not isinstance(actor,unreal.StaticMeshActor) or not actor.static_mesh_component.static_mesh:continue
 source=unreal.EditorAssetLibrary.get_metadata_tag(actor.static_mesh_component.static_mesh,'StreetCitySourceSHA256');q=owned_by_source.get(source)
 if not q or not actor.get_actor_label().startswith(('City_','World_')):continue
 if q['id'] in kept:
  removed.append(dict(actor=actor.get_path_name(),source_sha256=source,expected=q['id']));assert sub.destroy_actor(actor)
 else:actor.set_actor_label(q['id']);kept[q['id']]=actor
(OUT/'completion-duplicate-repair.json').write_text(json.dumps(dict(removed=removed,scope='only actors proven by this delivery source hashes; no prior source actors removed'),indent=2),'utf8')
actors={a.get_actor_label():a for a in sub.get_all_level_actors()};unreal.EditorPythonScripting.set_keep_python_script_alive(True)

def tick(delta):
 global frames,index,busy
 if busy:return
 frames+=1
 if frames<60:return
 busy=True
 try:
  q=todo[index];unreal.log('CITY COMPLETION SYNC begin '+str(index)+' '+q['id']);p=approved[q['id']];unreal.log('CITY COMPLETION SYNC load asset '+p['asset']);mesh=unreal.EditorAssetLibrary.load_asset(p['asset']);assert mesh and unreal.EditorAssetLibrary.get_metadata_tag(mesh,'StreetCitySourceSHA256')==q['source_sha256'];unreal.log('CITY COMPLETION SYNC asset loaded '+q['id'])
  actor=actors.get(q['id'])
  if actor is None:
   unreal.log('CITY COMPLETION SYNC spawn '+q['id']);actor=sub.spawn_actor_from_object(mesh,unreal.Vector(*q['position_cm']),unreal.Rotator(pitch=0,yaw=q['yaw_deg'],roll=0));assert actor;actor.set_actor_label(q['id']);actors[q['id']]=actor
  unreal.log('CITY COMPLETION SYNC bind '+q['id']);comp=actor.static_mesh_component
  # Preserve already-localized overrides when the exact source mesh is unchanged.
  if comp.static_mesh!=mesh or any(comp.get_material(i) is None for i in range(comp.get_num_materials())):
   comp.set_static_mesh(mesh);comp.set_editor_property('override_materials',ensure_mesh_materials(mesh))
  comp.set_collision_profile_name(q['collision']);actor.set_actor_location_and_rotation(unreal.Vector(*q['position_cm']),unreal.Rotator(pitch=0,yaw=q['yaw_deg'],roll=0),False,True);actor.set_actor_scale3d(unreal.Vector(1,1,1));actor.set_folder_path('CityCompletion/'+q.get('building',q['kind']));changed.append(q['id']);index+=1
  if 'visible' in q:comp.set_visibility(q['visible'],True)
  if 'hidden_in_game' in q:comp.set_hidden_in_game(q['hidden_in_game']);actor.set_actor_hidden_in_game(q['hidden_in_game'])
  task.write_text(json.dumps(dict(status='placing',completed=index,total=len(todo),last=q['id']),indent=2),'utf8')
  if index%10==0 or index==len(todo):assert unreal.EditorLevelLibrary.save_current_level();unreal.log('CITY COMPLETION SYNC saved '+str(index))
  if index==len(todo):
   (OUT/'sync.json').write_text(json.dumps(dict(status='passed',changed=changed,inventory_sha256=sha(OUT/'inventory.json'),map_sha256=sha(ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap')),indent=2),'utf8');task.write_text(json.dumps(dict(status='passed',completed=index,total=len(todo)),indent=2),'utf8');unreal.unregister_slate_post_tick_callback(handle);unreal.SystemLibrary.quit_editor()
 except Exception:
  error=traceback.format_exc();task.write_text(json.dumps(dict(status='failed',completed=index,total=len(todo),error=error),indent=2),'utf8');(OUT/'completion-sync-error.txt').write_text(error,'utf8');unreal.log_error(error);unreal.unregister_slate_post_tick_callback(handle);unreal.SystemLibrary.quit_editor()
 finally:busy=False
handle=unreal.register_slate_post_tick_callback(tick)
