"""Exercise the compiled runtime API in the real city PIE world, not just editor setters."""
import unreal,json,time,traceback,sys,os
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_scene_inventory import ROOT,OUT,sha
OUT.mkdir(exist_ok=True)
SCRIPT=Path(__file__).resolve()
state=dict(status='running',phase='starting',started=time.monotonic());handle=None;restart_handle=None
checks=[]
EAL=unreal.EditorAssetLibrary
COLLECTION=unreal.load_asset('/Game/UniversityCity/Localization/MPC_CityLanguage')
original_culture=unreal.InternationalizationLibrary.get_current_culture()

def snapshot(world):
 result={}
 for a in unreal.GameplayStatics.get_all_actors_of_class(world,unreal.StaticMeshActor):
  comp=a.static_mesh_component
  if not comp.static_mesh:continue
  loc=a.get_actor_location();rot=a.get_actor_rotation();scale=a.get_actor_scale3d()
  result[a.get_path_name()]=dict(transform=[loc.x,loc.y,loc.z,rot.pitch,rot.yaw,rot.roll,scale.x,scale.y,scale.z],mesh=comp.static_mesh.get_path_name(),collision=str(comp.get_collision_profile_name()),materials=[comp.get_material(i).get_path_name() if comp.get_material(i) else None for i in range(comp.get_num_materials())])
 return result
def check(ok,label):
 checks.append(dict(passed=bool(ok),name=label));assert ok,label

def finish(error=None):
 global handle,restart_handle
 if handle:unreal.unregister_slate_post_tick_callback(handle);handle=None
 if restart_handle:unreal.unregister_slate_post_tick_callback(restart_handle);restart_handle=None
 state['phase']='done'
 try:unreal.EditorLevelLibrary.editor_end_play()
 except Exception:pass
 unreal.InternationalizationLibrary.set_current_culture(original_culture,False)
 report=dict(status='failed' if error else 'passed',checks=checks,error=error,source_sha256=sha(SCRIPT),plugin_sources={str(p.relative_to(ROOT)):sha(p) for p in (ROOT/'StreetSocialCity/Plugins/CityLocalization/Source').rglob('*') if p.is_file()},runtime_world=state.get('world'),map='/Game/UniversityCity/Maps/UniversityCity',no_geometry_or_material_rebinding=True)
 (OUT/'runtime-acceptance.json').write_text(json.dumps(report,indent=2),'utf8')
 unreal.SystemLibrary.quit_editor()

def tick(delta):
 try:
  if time.monotonic()-state['started']>180:raise RuntimeError('PIE runtime audit timed out')
  world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
  if not world:return
  if state['phase']=='starting':
   state['phase']='testing';state['world']=world.get_path_name()
   api=unreal.CityLanguageLibrary;collection=COLLECTION
   gi=unreal.GameplayStatics.get_game_instance(world)
   check(gi is not None,'real game instance in PIE')
   before=snapshot(world);inventory=json.loads((OUT/'inventory.json').read_text('utf8'));check(len(before)>=len(inventory['rows']),'all adopted static city models are present in PIE')
   for i,code in enumerate(['zh-Hant','zh-Hans','en']):
    check(api.set_city_language_by_code(world,code,False),'runtime code API '+code)
    check(api.get_city_language_code(api.get_city_language(world))==code,'reported language '+code)
    actual_index=unreal.MaterialLibrary.get_scalar_parameter_value(world,collection,'LanguageIndex')
    check(actual_index==i,'actual PIE MPC '+code+' got '+str(actual_index)+' world '+world.get_path_name()+' collection '+collection.get_path_name())
    check(unreal.InternationalizationLibrary.get_current_culture()==code,'actual Unreal culture '+code)
    after=snapshot(world)
    if after!=before:(OUT/'runtime-actor-differences.json').write_text(json.dumps([dict(actor=k,before=before.get(k),after=after.get(k)) for k in sorted(set(before)|set(after)) if before.get(k)!=after.get(k)],indent=2),'utf8')
    check(after==before,'no actor/mesh/transform/material rebinding '+code)
   check(not api.set_city_language_by_code(world,'unsupported-language',False),'unsupported code rejected')
   check(unreal.MaterialLibrary.get_scalar_parameter_value(world,collection,'LanguageIndex')==2,'invalid code leaves current language unchanged')
   # The runner uses an isolated UserDir; only its test preference file is written.
   check(api.set_city_language(world,unreal.CityLanguage.TRADITIONAL_CHINESE,False),'enum API')
   # Exercise the same engine console command that a game/settings script can call.
   unreal.SystemLibrary.execute_console_command(world,'City.Language zh-Hans')
   check(api.get_city_language_code(api.get_city_language(world))=='zh-Hans','console API updates authoritative state')
   check(unreal.MaterialLibrary.get_scalar_parameter_value(world,collection,'LanguageIndex')==1,'console API changes actual MPC')
   check(api.set_city_language_by_code(world,'en',False),'nonpersistent switch before level travel')
   state['phase']='travel';unreal.GameplayStatics.open_level(world,'/Engine/Maps/Entry');return
  if state['phase']=='travel':
   if not world.get_name().endswith('Entry'):return
   check(unreal.CityLanguageLibrary.get_city_language_code(unreal.CityLanguageLibrary.get_city_language(world))=='en','game instance retains unsaved language across real level travel')
   collection=COLLECTION
   check(unreal.MaterialLibrary.get_scalar_parameter_value(world,collection,'LanguageIndex')==2,'new level auto-applies retained MPC')
   state['phase']='restart';unreal.EditorLevelLibrary.editor_end_play();state['restart_at']=time.monotonic();return
  if state['phase']=='restart':return
  if state['phase']=='reopened':
   check(unreal.CityLanguageLibrary.get_city_language_code(unreal.CityLanguageLibrary.get_city_language(world))=='zh-Hans','saved preference restored in fresh PIE instance')
   collection=COLLECTION
   check(unreal.MaterialLibrary.get_scalar_parameter_value(world,collection,'LanguageIndex')==1,'new world automatically applies saved MPC')
   unreal.CityLanguageLibrary.set_city_language_by_code(world,'zh-Hant',False)
   finish()
 except Exception:finish(traceback.format_exc())

def restart_tick(delta):
 if state['phase']=='restart' and time.monotonic()-state['restart_at']>2:
  state['phase']='reopened';unreal.EditorLevelLibrary.editor_play_simulate()

try:
 unreal.EditorPythonScripting.set_keep_python_script_alive(True)
 check(hasattr(unreal,'CityLanguageLibrary'),'compiled runtime plugin loaded')
 check(isinstance(COLLECTION,unreal.MaterialParameterCollection),'real collection loaded before PIE')
 check(unreal.EditorLevelLibrary.load_level('/Game/UniversityCity/Maps/UniversityCity'),'saved city map loaded')
 state['started']=time.monotonic()
 unreal.EditorLevelLibrary.editor_play_simulate()
 handle=unreal.register_slate_post_tick_callback(tick)
 restart_handle=unreal.register_slate_post_tick_callback(restart_tick)
except Exception:finish(traceback.format_exc())
