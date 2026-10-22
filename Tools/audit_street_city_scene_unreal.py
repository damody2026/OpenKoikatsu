"""Audit the saved native scene against current adopted source files, not old import receipts."""
import unreal,json,sys,traceback
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_scene_inventory import ROOT,OUT,read,sha
try:
 inventory=read(OUT/'inventory.json');assert unreal.EditorLevelLibrary.load_level(inventory['map'])
 all_actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors();actors={a.get_actor_label():a for a in all_actors};label_counts={}
 for a in all_actors:label_counts[a.get_actor_label()]=label_counts.get(a.get_actor_label(),0)+1
 rows=[];issues=[]
 for item in inventory['rows']:
  actor=actors.get(item['id']);faults=[];mesh=None
  if label_counts.get(item['id'],0)!=1:faults.append('expected exactly one adopted actor')
  if not isinstance(actor,unreal.StaticMeshActor):faults.append('missing actor')
  else:
   comp=actor.static_mesh_component;mesh=comp.static_mesh
   if not mesh:faults.append('missing mesh')
   elif unreal.EditorAssetLibrary.get_metadata_tag(mesh,'StreetCitySourceSHA256')!=item['source_sha256']:faults.append('source mismatch')
   pos=actor.get_actor_location();rot=actor.get_actor_rotation();scale=actor.get_actor_scale3d()
   if max(abs(a-b) for a,b in zip([pos.x,pos.y,pos.z],item['position_cm']))>.5:faults.append('position mismatch')
   if abs((rot.yaw-item['yaw_deg']+180)%360-180)>.01 or abs(rot.pitch)>.01 or abs(rot.roll)>.01:faults.append('rotation mismatch')
   if max(abs(v-1) for v in [scale.x,scale.y,scale.z])>.0001:faults.append('scale mismatch')
   if str(comp.get_collision_profile_name())!=item['collision']:faults.append('collision mismatch')
   if 'visible' in item and bool(comp.get_editor_property('visible'))!=item['visible']:faults.append('declared legacy visibility mismatch')
   if 'hidden_in_game' in item and bool(actor.get_editor_property('hidden'))!=item['hidden_in_game']:faults.append('declared legacy retirement mismatch')
   if mesh and item['collision']=='BlockAll' and mesh.get_editor_property('body_setup').get_editor_property('collision_trace_flag')!=unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE:faults.append('triangle collision missing')
   if any(comp.get_material(i) is None for i in range(comp.get_num_materials())):faults.append('unbound material')
  row=dict(id=item['id'],kind=item['kind'],source_sha256=item['source_sha256'],asset=mesh.get_path_name() if mesh else None,issues=faults);rows.append(row)
  if faults:issues.append(row)
 result=dict(status='needs_sync' if issues else 'passed',inventory_sha256=sha(OUT/'inventory.json'),map_sha256=sha(ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap'),counts=inventory['counts'],actors=rows,issues=issues,auditor_sha256=sha(__file__))
 (OUT/'native-inventory.json').write_text(json.dumps(result,indent=2),'utf8');unreal.log('CITY INVENTORY '+result['status']+' '+str(len(issues))+' mismatches')
except Exception:
 (OUT/'native-inventory-error.txt').write_text(traceback.format_exc(),'utf8');raise
finally:unreal.SystemLibrary.quit_editor()
