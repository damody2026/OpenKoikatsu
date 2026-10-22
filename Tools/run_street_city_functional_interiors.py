"""Bounded resumable building revision jobs; native adoption is a separate verified stage."""
import os,json,sys,traceback
from concurrent.futures import ThreadPoolExecutor,as_completed
from street_city_functional_revision import OUT,read
from run_street_city_shop_street import run
os.environ['STREET_CITY_BLENDER_THREADS']='5'
ids=[r['building'] for r in read(OUT/'baseline.json')['buildings']]
if len(sys.argv)>1:ids=sys.argv[1:]
def process(bid):
 try:
  latest=OUT/'buildings'/bid/'latest.json'
  if latest.exists():
   from street_city_functional_revision import sha,ROOT
   import hashlib
   r=read(read(latest)['receipt']);baseline=next(x for x in read(OUT/'baseline.json')['buildings'] if x['building']==bid)
   signature=hashlib.sha256(json.dumps([sha(ROOT/'Tools/build_street_city_functional_interiors.py'),sha(ROOT/'Tools/street_city_functional_revision.py'),baseline['source_sha256']],sort_keys=True).encode()).hexdigest()
   if r['signature']==signature and sha(r['model'])==r['model_sha256']:return dict(building=bid,status='built')
  run('Tools/build_street_city_functional_interiors.py',OUT/'buildings'/bid,'--building',bid);return dict(building=bid,status='built')
 except Exception:return dict(building=bid,status='failed',error=traceback.format_exc())
rows=[]
with ThreadPoolExecutor(max_workers=3) as pool:
 for future in as_completed([pool.submit(process,b) for b in ids]):
  rows.append(future.result());print(rows[-1]['building'],rows[-1]['status'],flush=True);(OUT/'build-progress.json').write_text(json.dumps(rows,indent=2),'utf8')
assert all(r['status']=='built' for r in rows),[r for r in rows if r['status']!='built']
