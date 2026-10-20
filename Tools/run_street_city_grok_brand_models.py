"""Build activated brands while independent Grok image jobs are still running."""
import json,time,concurrent.futures
from generate_street_city_grok_brands import OUT
from run_street_city_shop_wall_styles import work,ready,ROOT,OUT as WALL

def main():
 rows={};pending={};start=time.monotonic()
 with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
  while len(rows)<96:
   path=OUT/'activation.json';active=json.loads(path.read_text('utf8'))['buildings'] if path.exists() else {}
   for bid in ([] if (OUT/'pause-models').exists() else sorted(active)):
    if bid not in rows and bid not in pending and len(pending)<6:
     pending[bid]=pool.submit(work,bid)
   for bid,future in list(pending.items()):
    if not future.done():continue
    r=future.result();rows[bid]=r;del pending[bid];print(bid,r['status'],len(rows),'/96',flush=True)
    (OUT/'model-progress.json').write_text(json.dumps(dict(status='in_progress',completed=len(rows),total=96,buildings=list(rows.values())),indent=2),'utf8')
    assert r['status']=='passed',r
   assert time.monotonic()-start<14400,'No complete adoption after four hours; preserve all outputs'
   time.sleep(3)
 for bid in rows:
  folder=ROOT/f'output/street-social-city-production/{bid}-modular-components-v1/assembly-building';assert ready(bid,folder),bid
 result=dict(status='passed',completed=96,total=96,buildings=list(rows.values()))
 for path in [OUT/'model-progress.json',WALL/'progress.json']:path.write_text(json.dumps(result,indent=2),'utf8')
 print('ALL 96 GROK BRAND MODELS PASSED',flush=True)

if __name__=='__main__':main()
