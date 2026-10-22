import unreal,json,sys,traceback
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_scene_inventory import OUT
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
old=unreal.InternationalizationLibrary.get_current_culture();handle=None
COLLECTION=unreal.load_asset('/Game/UniversityCity/Localization/MPC_CityLanguage');assert COLLECTION is not None

def tick(delta):
 global handle
 world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
 if not world:return
 unreal.unregister_slate_post_tick_callback(handle)
 rows=[];error=None
 try:
  collection=COLLECTION
  for n,code in enumerate(['zh-Hant','zh-Hans','en']):
   ok=unreal.CityLanguageLibrary.set_city_language_by_code(world,code,False)
   value=unreal.MaterialLibrary.get_scalar_parameter_value(world,collection,'LanguageIndex')
   rows.append(dict(code=code,api_ok=ok,index=value,world=world.get_path_name(),collection=collection.get_path_name()))
  unreal.MaterialLibrary.set_scalar_parameter_value(world,collection,'LanguageIndex',1)
  control=unreal.MaterialLibrary.get_scalar_parameter_value(world,collection,'LanguageIndex')
 except Exception:error=traceback.format_exc();control=None
 (OUT/'runtime-smoke.json').write_text(json.dumps(dict(status='passed' if not error and all(q['api_ok'] and q['index']==i for i,q in enumerate(rows)) else 'failed',rows=rows,direct_setter_control=control,error=error),indent=2),'utf8')
 unreal.EditorLevelLibrary.editor_end_play();unreal.InternationalizationLibrary.set_current_culture(old,False);unreal.SystemLibrary.quit_editor()
handle=unreal.register_slate_post_tick_callback(tick)
unreal.EditorLevelLibrary.editor_play_simulate()
