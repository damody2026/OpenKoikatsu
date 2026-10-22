"""Finish one accepted revision through current-source native and portable deliveries."""
import os,sys,subprocess,json
from concurrent.futures import ThreadPoolExecutor
from street_city_functional_revision import ROOT,OUT,read
from run_street_city_shop_street import run
from finish_street_city_scene_integration import native
os.environ['STREET_CITY_BLENDER_THREADS']='5'
def python(script):subprocess.run([sys.executable,str(ROOT/'Tools'/script)],cwd=ROOT,check=True)
def walls():
 run('Tools/build_street_city_world_wall_models.py',OUT/'wall-models')
 python('localize_street_city_world_wall_models.py');python('audit_street_city_world_wall_models.py')
 run('Tools/render_street_city_world_wall_closeups.py',OUT/'wall-closeups')
def main():
 assert read(OUT/'adoption.json')['status']=='passed'
 python('audit_street_city_lecture_sightlines.py')
 if '--after-sync' in sys.argv:
  from street_city_functional_revision import sha
  base=OUT.parent;inv=read(base/'scene-integration-v1/native-inventory.json');wall=read(base/'world-wall-grok-v1/native-acceptance.json');gpu=read(base/'world-wall-grok-v1/native-render.json');mapsha=sha(ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap')
  assert inv['status']==wall['status']=='passed' and inv['map_sha256']==wall['map_sha256']==mapsha
  assert inv['inventory_sha256']==sha(base/'scene-integration-v1/inventory.json')
  assert gpu['status']=='passed_real_rhi_captures' and gpu.get('process_returncode')==0 and gpu['source_install_sha256']==sha(base/'world-wall-grok-v1/native-install.json')
  python('run_street_city_runtime_audit.py');python('verify_street_city_scene_integration.py')
 else:
  # Native mutation and offline model construction have disjoint outputs.
  with ThreadPoolExecutor(max_workers=2) as pool:
   tasks=[pool.submit(python,'run_street_city_scene_integration.py'),pool.submit(walls)]
   for task in tasks:task.result()
 native('audit_street_city_remaining_native.py','remaining-collision',OUT.parent/'world-completion-v1/native-collision-acceptance.json')
 native('audit_street_city_functional_unreal.py','functional-interiors',OUT/'native-acceptance.json')
 python('finalize_street_city_world_walls.py')
 with ThreadPoolExecutor(max_workers=2) as pool:
  futures=[pool.submit(run,'Tools/render_street_city_functional_interiors.py',OUT/'previews'/bid,'--building',bid) for bid in ['EE','AD','H1','M1']]
  for f in futures:f.result()
 python('publish_street_city_functional_interiors.py')
 print('FUNCTIONAL REVISION FULLY VERIFIED',flush=True)
if __name__=='__main__':main()
