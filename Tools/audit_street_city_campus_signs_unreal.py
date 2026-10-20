"""Independent reopen: facade signs plus all prior floor language bindings."""
import sys,os,traceback,json
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
import unreal
from street_city_localization import OUT,sha

if __name__=='__main__':
 try:
  os.environ['STREET_CITY_LANGUAGE_GROUP']='signs-audit'
  from install_street_city_campus_signs_unreal import main as audit_signs
  from audit_street_city_localization_unreal import main as audit_existing
  audit_signs();audit_existing()
  sign=json.loads((OUT/'campus-signs/unreal-acceptance.json').read_text('utf8'))
  floors=json.loads((OUT/'native-bindings.json').read_text('utf8'))
  assert sign['status']=='passed_reopened' and floors['actor_count']==387
  assert sign['map_sha256']==floors['native_map_sha256']
  (OUT/'campus-signs/acceptance.json').write_text(json.dumps(dict(status='passed',buildings=17,signs=34,existing_floor_actors_verified=387,locales=['zh-Hant','zh-Hans','en'],runtime_values_tested=[0,1,2],map_sha256=sign['map_sha256'],manifest_sha256=sha(OUT/'campus-signs/manifest.json'),scope='native material/texture identities, saved placement, parameter switching; no rendered runtime screenshot'),indent=2),'utf8')
 except Exception:
  (OUT/'campus-signs/reopen-error.txt').write_text(traceback.format_exc(),'utf8');raise
 finally:unreal.SystemLibrary.quit_editor()
