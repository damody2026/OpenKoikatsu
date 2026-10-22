import unreal,json,sys,traceback
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'output/street-social-city-production/streetscape-v1';r=[]
try:
 q=json.loads((OUT/'native-import-progress.json').read_text('utf8'))['assets'][0];mesh=unreal.load_asset(q['asset']);bp=unreal.load_asset('/Game/UniversityCity/StreetScenery/BP_StreetCluster');actor=unreal.EditorLevelLibrary.spawn_actor_from_class(bp.generated_class(),unreal.Vector());comp=actor.get_component_by_class(unreal.HierarchicalInstancedStaticMeshComponent);comp.set_static_mesh(mesh)
 ts=[unreal.Transform(location=unreal.Vector(i*300,0,0),rotation=unreal.Rotator(0,0,0),scale=unreal.Vector(1,1,1)) for i in range(3)]
 r.append(dict(stage='initial',path=comp.get_path_name(),count=comp.get_instance_count(),transforms=len(ts)))
 indices=comp.add_instances(ts,True,False,False);r.append(dict(stage='batch',indices=list(indices),count=comp.get_instance_count()))
 comp.clear_instances();idx=comp.add_instance(ts[0],False);r.append(dict(stage='single',index=idx,count=comp.get_instance_count()))
 comp.clear_instances();comp.set_mobility(unreal.ComponentMobility.STATIC);comp.set_editor_property('instancing_random_seed',20261020);indices=comp.add_instances(ts,True,False,False);r.append(dict(stage='configured',indices=list(indices),count=comp.get_instance_count(),actual_path=actor.get_component_by_class(unreal.HierarchicalInstancedStaticMeshComponent).get_path_name()))
 comp=actor.get_component_by_class(unreal.HierarchicalInstancedStaticMeshComponent);comp.set_static_mesh(mesh);comp.set_cast_shadow(True);comp.set_cull_distances(10000,18000);comp.clear_instances();indices=comp.add_instances(ts,True,False,False);r.append(dict(stage='runtime_setters_and_fresh_handle',indices=list(indices),count=comp.get_instance_count()));assert comp.get_instance_count()==3
except Exception:r.append(dict(error=traceback.format_exc()))
finally:(OUT/'hism-probe.json').write_text(json.dumps(r,indent=2),'utf8');unreal.SystemLibrary.quit_editor()
