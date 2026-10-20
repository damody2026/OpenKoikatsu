"""Import the accepted, previously remeshed 288 shop floors into Unreal assets."""
import sys,json,traceback
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_unreal_import import *
smoke='-CitySmoke' in unreal.SystemLibrary.get_command_line();rows=[];report=OUT/('unreal-shops-smoke.json' if smoke else 'unreal-shops-import.json');SOURCE_SHA=sha(Path(__file__))
MAP='/Game/UniversityCity/Maps/UniversityCity';scene_bindings=[];actors={};preserved={}
def nonshop_snapshot(objects):
 data={}
 for actor in objects:
  label=actor.get_actor_label()
  if re.fullmatch(r'S\d+_F\d+',label):continue
  rotation=actor.get_actor_rotation();value=dict(position=xyz(actor.get_actor_location()),rotation=[float(rotation.pitch),float(rotation.yaw),float(rotation.roll)],scale=xyz(actor.get_actor_scale3d()))
  if isinstance(actor,unreal.StaticMeshActor):
   comp=actor.static_mesh_component;mesh=comp.static_mesh;value.update(mesh=mesh.get_path_name() if mesh else None,materials=[comp.get_material(i).get_path_name() if comp.get_material(i) else None for i in range(comp.get_num_materials())],collision=str(comp.get_collision_profile_name()))
  data[label]=value
 return data
try:
 if unreal.EditorAssetLibrary.does_asset_exist(MAP):
  assert unreal.EditorLevelLibrary.load_level(MAP);sub=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);objects=sub.get_all_level_actors();actors={a.get_actor_label():a for a in objects if re.fullmatch(r'S\d+_F\d+',a.get_actor_label())};assert len(actors) in (0,288);preserved=nonshop_snapshot(objects)
 for item in floor_inputs():
  if smoke and item['building']!='S01':continue
  mesh=import_mesh(item['source'],'/Game/UniversityCity/Shops/'+item['building'],item['id']);box=mesh.get_bounding_box();size=box.max-box.min;assert abs(size.x-item['footprint_m'][0]*100)<.5 and abs(size.y-item['footprint_m'][1]*100)<.5,(item['id'],'unit or axis mismatch',xyz(size));assert mesh.get_editor_property('body_setup').get_editor_property('collision_trace_flag')==unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE
  if actors:
   actor=actors[item['id']];comp=actor.static_mesh_component;bound=ensure_mesh_materials(mesh);comp.set_static_mesh(mesh);comp.set_editor_property('override_materials',bound)
   assert comp.get_num_materials()==len(bound) and all(comp.get_material(i)==mat for i,mat in enumerate(bound)),item['id']+' stale scene material override'
   scene_bindings.append(dict(actor=item['id'],source_sha256=sha(item['source']),asset=mesh.get_path_name(),materials=[m.get_path_name() for m in bound]))
  row={k:str(v) if isinstance(v,Path) else v for k,v in item.items()};row.update(asset=mesh.get_path_name(),source_sha256=sha(item['source']),local_bounds_cm=[xyz(box.min),xyz(box.max)],collision='complex_as_simple',pose='static accepted floor export; animation source remains in original building GLB');rows.append(row);report.write_text(json.dumps(dict(status='importing',floors=rows),indent=2),'utf-8');unreal.log(item['id']+' imported and metric bounds verified')
  if item['floor']==3:
   if actors and len(rows)%36==0:
    assert unreal.EditorLevelLibrary.save_current_level()
    (OUT/'unreal-shop-style-checkpoint.json').write_text(json.dumps(dict(status='in_progress',importer_sha256=SOURCE_SHA,actors=scene_bindings),indent=2),'utf8')
   mesh=None;unreal.SystemLibrary.collect_garbage()
 assert len(rows)==(3 if smoke else 288)
 if actors:
  assert len(scene_bindings)==len(rows);assert nonshop_snapshot(sub.get_all_level_actors())==preserved,'unrelated campus/mall/outdoor actor changed';assert unreal.EditorLevelLibrary.save_current_level()
 assert sha(Path(__file__))==SOURCE_SHA
 report.write_text(json.dumps(dict(status='passed_smoke_only' if smoke else 'passed',importer_sha256=SOURCE_SHA,floors=rows,scene_material_bindings=scene_bindings,preserved_nonshop_actors=len(preserved)),indent=2),'utf-8')
 (OUT/'unreal-shop-style-bindings.json').write_text(json.dumps(dict(status='passed',importer_sha256=SOURCE_SHA,actors=scene_bindings,bindings=MATERIAL_BINDINGS),indent=2),'utf8')
except Exception:
 report.write_text(json.dumps(dict(status='failed',floors=rows,error=traceback.format_exc()),indent=2),'utf-8');raise
