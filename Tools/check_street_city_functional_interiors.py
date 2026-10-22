"""Run all independent Blender checks, then publish a guarded source adoption."""
import os,json,traceback
from concurrent.futures import ThreadPoolExecutor,as_completed
from street_city_functional_revision import ROOT,OUT,read,sha
from run_street_city_shop_street import run
os.environ['STREET_CITY_BLENDER_THREADS']='4'
ids=[r['building'] for r in read(OUT/'baseline.json')['buildings']]
def process(bid):
 try:
  receipt=read(OUT/'buildings'/bid/'latest.json')['receipt'];built=read(receipt)
  if not built['additions'] and not built['moved_seats'] and not built['removed_seats']:return dict(building=bid,status='unchanged',reason='No requested functional changes; retain original accepted GLB without reserialization')
  run('Tools/accept_street_city_functional_interiors.py',OUT/'buildings'/bid,'--building',bid)
  r=read(read(OUT/'buildings'/bid/'latest.json')['receipt']);a=read(__import__('pathlib').Path(read(OUT/'buildings'/bid/'latest.json')['receipt']).parent/'acceptance.json');assert a['status']=='passed';return dict(building=bid,status='passed',receipt=read(OUT/'buildings'/bid/'latest.json')['receipt'],checks=a['checks'])
 except Exception:return dict(building=bid,status='failed',error=traceback.format_exc())
rows=[]
with ThreadPoolExecutor(max_workers=3) as pool:
 for future in as_completed([pool.submit(process,bid) for bid in ids]):
  rows.append(future.result());print(rows[-1]['building'],rows[-1]['status'],flush=True);(OUT/'acceptance-progress.json').write_text(json.dumps(rows,indent=2),'utf8')
assert all(r['status'] in ('passed','unchanged') for r in rows),[r for r in rows if r['status'] not in ('passed','unchanged')]
accepted=sorted([r for r in rows if r['status']=='passed'],key=lambda r:r['building']);models=[read(r['receipt']) for r in accepted];floors=[f for r in models for f in r['floors']]
for r in models:
 for f in r['floors']:
  baseline=__import__('pathlib').Path(r['source']).with_name(f"{r['building']}-F{f['floor']:02d}-complete-2k.glb");f['baseline_source']=str(baseline);f['baseline_source_sha256']=sha(baseline)
adoption=dict(status='passed',buildings=[dict(building=r['building'],model=r['model'],model_sha256=r['model_sha256'],receipt=q['receipt'],receipt_sha256=sha(q['receipt'])) for q,r in zip(accepted,models)],unchanged=[r for r in rows if r['status']=='unchanged'],floors=floors,checks=sum(r.get('checks',0) for r in rows),classrooms=sum(len(r['classrooms']) for r in models),tiered_classrooms=sum(q['tiered'] for r in models for q in r['classrooms']),administrative_rooms=sum(len(r['administration']) for r in models),opaque_bays=sum(len(r['opaque_bays']) for r in models),scope='Certified revision sources only; Unreal native validation is a separate required stage')
(OUT/'adoption.json').write_text(json.dumps(adoption,ensure_ascii=False,indent=2),'utf8');print('ADOPTED',len(floors),'floors',adoption['checks'],'checks',flush=True)
