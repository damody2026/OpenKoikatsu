"""Render only current accepted campus models, as production finishes."""
import os,json,time,hashlib,argparse
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
from street_city_campus_contract import IDS,OUT,context,digest
from run_street_city_shop_street import run
p=argparse.ArgumentParser();p.add_argument('--views-only',action='store_true');a=p.parse_args();os.environ['STREET_CITY_BLENDER_THREADS']='6';scratch=OUT/'scratch';scratch.mkdir(exist_ok=True);os.environ['TEMP']=str(scratch.resolve());os.environ['TMP']=str(scratch.resolve());pending={};finished=[]
def ready(bid):
 try:
  c=context(bid);r=json.loads((c['out']/'assembly-report.json').read_text('utf8'));a=json.loads((c['out']/'acceptance.json').read_text('utf8'))
  return a['status']=='passed' and r['contract_sha256']==digest(c) and all(hashlib.sha256(Path(p).read_bytes()).hexdigest()==s for p,s in r['recipe_sources'].items()) and hashlib.sha256((c['out']/f'{bid}-complete-2k.glb').read_bytes()).hexdigest()==a['sha256']
 except (OSError,ValueError,KeyError):return False
with ThreadPoolExecutor(max_workers=2) as pool:
 while len(finished)<17:
  for bid in IDS:
   if bid not in pending and bid not in finished and ready(bid):
    folder=context(bid)['out'];receipt=folder/'render-receipt.json';reuse=False
    if receipt.exists():
     r=json.loads(receipt.read_text('utf8'));acc=json.loads((folder/'acceptance.json').read_text('utf8'));renderer=Path(__file__).resolve().parent/'render_street_city_campus.py';reuse=r['source_sha256']==acc['sha256'] and r['renderer_sha256']==hashlib.sha256(renderer.read_bytes()).hexdigest() and all(hashlib.sha256((folder/f).read_bytes()).hexdigest()==s for f,s in r['images'].items())
    if reuse:finished.append(bid)
    else:pending[bid]=pool.submit(run,'Tools/render_street_city_campus.py',folder,'--building',bid)
  for bid,future in list(pending.items()):
   if future.done():future.result();finished.append(bid);del pending[bid];print(bid,'review rendered',flush=True)
  (OUT/'review-progress.json').write_text(json.dumps(dict(status='passed' if len(finished)==17 else 'in_progress',rendered=finished),indent=2),'utf8')
  if len(finished)<17:time.sleep(10)
if not a.views_only:
 if (OUT/'outdoor/acceptance.json').exists():run('Tools/assemble_street_city_outdoor.py',OUT/'outdoor')
 else:run('Tools/render_street_city_campus.py',OUT)
 run('Tools/accept_street_city_campus_world.py',OUT)
 accepted=json.loads((OUT/'world-acceptance.json').read_text('utf8'));receipt=dict(renderer_sha256=hashlib.sha256((Path(__file__).parent/'render_street_city_campus.py').read_bytes()).hexdigest(),files=accepted['files'],image_sha256=hashlib.sha256((OUT/'campus-overview.png').read_bytes()).hexdigest())
 from street_city_atomic import write_text
 write_text(OUT/'world-render-receipt.json',json.dumps(receipt,indent=2))
from publish_street_city_campus import publish
publish();print('All 17 building views verified' if a.views_only else 'All 17 buildings reviewed and complete campus accepted',flush=True)
if (OUT/'outdoor/acceptance.json').exists():
 from publish_street_city_outdoor import publish as publish_outdoor
 publish_outdoor()
