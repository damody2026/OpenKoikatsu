"""Start editor automation only after startup map/asset registry tick work is settled."""
import unreal,os,runpy,traceback,json
from pathlib import Path
SCRIPT=Path(os.environ['STREET_CITY_DEFERRED_SCRIPT']).resolve();assert SCRIPT.is_file()
unreal.EditorPythonScripting.set_keep_python_script_alive(True);ticks=0
def tick(dt):
 global ticks
 ticks+=1
 if ticks<90:return
 unreal.unregister_slate_post_tick_callback(handle)
 try:
  if os.environ.get('STREET_CITY_DEFERRED_JSON'):
   payload=json.loads(Path(os.environ['STREET_CITY_DEFERRED_JSON']).read_text('utf-8'))
   exec(compile(payload['code'],payload['source'],'exec'),{'__name__':'__main__','__file__':payload['source'],'PAYLOAD':payload})
  else:runpy.run_path(str(SCRIPT),run_name='__main__')
 except BaseException:
  Path(str(SCRIPT)+'.deferred-error.txt').write_text(traceback.format_exc(),'utf8');unreal.log_error(traceback.format_exc());unreal.SystemLibrary.quit_editor()
handle=unreal.register_slate_post_tick_callback(tick)
