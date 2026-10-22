"""Bind prepared current models without disturbing unchanged buildings or material overrides."""
import unreal,json,sys,traceback
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_scene_inventory import ROOT,OUT,read,sha
from street_city_unreal_import import ensure_mesh_materials
try:
 inventory=read(OUT/'inventory.json');prepared=read(OUT/'prepared-updates.json');assert prepared['status']=='passed' and prepared['inventory_sha256']==sha(OUT/'inventory.json')
 approved={q['id']:q for q in prepared['rows']}
 assert unreal.EditorLevelLibrary.load_level(inventory['map'])
 sub=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);actors={a.get_actor_label():a for a in sub.get_all_level_actors()};changed=[]
 for q in inventory['rows']:
  if q['id'] not in approved:continue
  p=approved[q['id']];mesh=unreal.EditorAssetLibrary.load_asset(p['asset']);assert unreal.EditorAssetLibrary.get_metadata_tag(mesh,'StreetCitySourceSHA256')==q['source_sha256']
  actor=actors.get(q['id'])
  if actor is None:
   actor=sub.spawn_actor_from_object(mesh,unreal.Vector(*q['position_cm']),unreal.Rotator(pitch=0,yaw=q['yaw_deg'],roll=0));actor.set_actor_label(q['id'])
  comp=actor.static_mesh_component;comp.set_static_mesh(mesh);comp.set_editor_property('override_materials',ensure_mesh_materials(mesh));comp.set_collision_profile_name(q['collision'])
  actor.set_actor_location_and_rotation(unreal.Vector(*q['position_cm']),unreal.Rotator(pitch=0,yaw=q['yaw_deg'],roll=0),False,True);actor.set_actor_scale3d(unreal.Vector(1,1,1));changed.append(q['id'])
 assert unreal.EditorLevelLibrary.save_current_level()
 (OUT/'sync.json').write_text(json.dumps(dict(status='passed',changed=changed,inventory_sha256=sha(OUT/'inventory.json'),map_sha256=sha(ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap')),indent=2),'utf8')
except Exception:
 (OUT/'sync-error.txt').write_text(traceback.format_exc(),'utf8');raise
finally:unreal.SystemLibrary.quit_editor()
