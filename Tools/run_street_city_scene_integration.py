"""Single repeatable entry point: exact-source sync, runtime localization and final acceptance."""
import subprocess,sys,json
from street_city_scene_inventory import ROOT,OUT,BASE,read,sha,build
from finish_street_city_scene_integration import native,main as finish

def main():
 build()
 plugin=ROOT/'StreetSocialCity/Plugins/CityLocalization'
 current=False
 try:
  r=read(OUT/'plugin-build.json');current=r['status']=='passed' and all(sha(ROOT/p)==s for p,s in r['source_hashes'].items()) and all(sha(ROOT/p)==s for p,s in r['binaries'].items())
 except (OSError,ValueError,KeyError):pass
 if not current:subprocess.run([sys.executable,str(ROOT/'Tools/build_street_city_language_plugin.py')],cwd=ROOT,check=True)
 # A baseline may legitimately report updates. The fresh receipt is mandatory.
 baseline=native('audit_street_city_scene_unreal.py','scene-inventory',OUT/'native-inventory.json',expected=None)
 if baseline['issues']:
  native('prepare_street_city_scene_updates_unreal.py','scene-prepare',OUT/'prepared-updates.json')
  finish()
 else:
  (OUT/'sync.json').write_text(json.dumps(dict(status='passed',changed=[],inventory_sha256=sha(OUT/'inventory.json'),map_sha256=baseline['map_sha256'],reason='All current sources already bound; no map mutation required.'),indent=2),'utf8')
  walls=BASE/'world-wall-grok-v1';wall_rebound=False
  if (walls/'activation.json').exists() and read(walls/'native-install.json')['source_inventory_sha256']!=sha(OUT/'inventory.json'):
   # A separately adopted scenery/LOD layer can change inventory metadata while
   # preserving every floor source. Rebind and certify the current inventory.
   native('install_street_city_world_walls_unreal.py','world-walls',walls/'native-install.json');native('audit_street_city_scene_unreal.py','scene-inventory',OUT/'native-inventory.json');wall_rebound=True
  native('audit_street_city_campus_signs_unreal.py','campus-signs',BASE/'localization-v1/campus-signs/acceptance.json')
  walls=BASE/'world-wall-grok-v1'
  if (walls/'activation.json').exists():native('audit_street_city_world_walls_unreal.py','walls-audit',walls/'native-acceptance.json')
  if wall_rebound:subprocess.run([sys.executable,str(ROOT/'Tools/run_street_city_world_wall_native_render.py')],cwd=ROOT,check=True)
 subprocess.run([sys.executable,str(ROOT/'Tools/run_street_city_runtime_audit.py')],cwd=ROOT,check=True)
 from verify_street_city_scene_integration import main as verify
 verify()
if __name__=='__main__':main()
