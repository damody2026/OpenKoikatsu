"""Full static outdoor delivery gate, preserving campus/shop regression checks."""
import json,sys,os,subprocess,shutil,argparse
from concurrent.futures import ThreadPoolExecutor
from street_city_outdoor_contract import OUT,ROOT
from street_city_remesh_gate import sha
from run_street_city_shop_street import run
parser=argparse.ArgumentParser();parser.add_argument('--reuse-native',action='store_true',help='Only reuse exact current saved native versions, checked again by the complete delivery gate');args=parser.parse_args()
os.environ['STREET_CITY_BLENDER_THREADS']='4';os.environ['TEMP']=str(OUT.parent/'scratch');os.environ['TMP']=os.environ['TEMP']
run('Tools/accept_street_city_outdoor.py',OUT)
assembly=json.loads((OUT/'assembly.json').read_text('utf8'));assert assembly['model_sha256']==sha(OUT.parent/'campus-complete-2k.glb') and assembly['assembler_sha256']==sha(ROOT/'Tools/assemble_street_city_outdoor.py')
lua=str(ROOT/'Tools/lua/lua.exe')
def native():
 if args.reuse_native:
  campus=json.loads((OUT.parent/'unreal-acceptance.json').read_text('utf8'));outdoor=json.loads((OUT/'unreal-acceptance.json').read_text('utf8'));assert campus['status']==outdoor['status']=='passed'
  assert campus['source_sha256']==sha(ROOT/'Tools/assemble_street_city_unreal_campus.py') and outdoor['source_sha256']==sha(ROOT/'Tools/street_city_unreal_outdoor.py') and outdoor['acceptance_sha256']==sha(OUT/'acceptance.json')
  for q in campus['saved_assets']+outdoor['saved_assets']:assert sha(q['path'])==q['sha256']
  for q in outdoor['imports']:assert sha(q['source'])==q['source_sha256']
  print('Reused exact current native geometry, assets and audit versions',flush=True);return
 for stage_args in [('Campus',),('InteriorAuditCollision','--interior-after')]:
  subprocess.run([lua,str(ROOT/'Tools/run_street_city_unreal.lua'),'--stage',*stage_args],cwd=ROOT,check=True)
with ThreadPoolExecutor(max_workers=3) as pool:
 futures=[pool.submit(run,'Tools/accept_street_city_campus_world.py',OUT.parent),pool.submit(run,'Tools/accept_street_city_outdoor_world.py',OUT),pool.submit(run,'Tools/diagnose_street_city_ground_overlap.py',OUT.parent),pool.submit(native)]
 for future in futures:future.result()
w=json.loads((OUT.parent/'world-acceptance.json').read_text('utf8'));assert w['status']=='passed'
(OUT.parent/'world-render-receipt.json').write_text(json.dumps(dict(renderer_sha256=sha(ROOT/'Tools/render_street_city_campus.py'),outdoor_assembler_sha256=assembly['assembler_sha256'],files=w['files'],image_sha256=sha(OUT.parent/'campus-overview.png')),indent=2),'utf8')
subprocess.run([sys.executable,str(ROOT/'Tools/finalize_street_city_campus.py'),'--finish-only'],cwd=ROOT,check=True)
print('Outdoor/campus/preserved shops complete current delivery passed',flush=True)
