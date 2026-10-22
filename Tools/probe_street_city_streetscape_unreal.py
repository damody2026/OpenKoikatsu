import unreal,json,sys,traceback
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'output/street-social-city-production/streetscape-v1'
try:
 path='/Game/UniversityCity/StreetScenery/BP_StreetCluster'
 if unreal.EditorAssetLibrary.does_asset_exist(path):bp=unreal.load_asset(path)
 else:
  factory=unreal.BlueprintFactory();factory.set_editor_property('parent_class',unreal.Actor)
  bp=unreal.AssetToolsHelpers.get_asset_tools().create_asset('BP_StreetCluster',path.rsplit('/',1)[0],unreal.Blueprint,factory)
  subsystem=unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem);handles=subsystem.k2_gather_subobject_data_for_blueprint(bp)
  handle,reason=subsystem.add_new_subobject(unreal.AddNewSubobjectParams(parent_handle=handles[0],new_class=unreal.HierarchicalInstancedStaticMeshComponent,blueprint_context=bp))
  assert not str(reason),str(reason)
  unreal.BlueprintEditorLibrary.compile_blueprint(bp);assert unreal.EditorAssetLibrary.save_loaded_asset(bp)
 actor=unreal.EditorLevelLibrary.spawn_actor_from_class(bp.generated_class(),unreal.Vector())
 comp=actor.get_component_by_class(unreal.HierarchicalInstancedStaticMeshComponent);assert comp
 api=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
 result=dict(status='passed',blueprint=path,component=str(comp),lod_api={n:getattr(api,n).__doc__ for n in ['set_lod_from_static_mesh','get_lod_count','get_number_verts','get_lod_screen_sizes','set_lod_screen_sizes'] if hasattr(api,n)},component_api={n:getattr(comp,n).__doc__ for n in ['add_instances','set_cull_distances','get_instance_transform']},mesh_api=[n for n in dir(unreal.StaticMesh) if 'lod' in n.lower() or 'triangle' in n.lower()])
 (OUT/'unreal-capabilities.json').write_text(json.dumps(result,indent=2),'utf8')
 unreal.get_editor_subsystem(unreal.EditorActorSubsystem).destroy_actor(actor)
except Exception:(OUT/'unreal-probe-error.txt').write_text(traceback.format_exc(),'utf8');raise
finally:unreal.SystemLibrary.quit_editor()
