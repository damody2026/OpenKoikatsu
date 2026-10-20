"""Final current-plan/source audit; refresh metadata only for accepted exports."""
import json,sys,argparse
from pathlib import Path
from street_city_shop_street import inventory,publish,ROOT,OUT
from street_city_building_context import context
from street_city_remesh_gate import verify,sha
from run_street_city_shop_street import publish_building
p=argparse.ArgumentParser();p.add_argument('--allow-pending',action='store_true');args=p.parse_args();records=[];errors=[]
for b in inventory():
 bid=b['building'];folder=ROOT/f'output/street-social-city-production/{bid}-modular-components-v1/assembly-building'
 try:
  a=json.loads((folder/'acceptance.json').read_text('utf-8'));r=json.loads((folder/'assembly-report.json').read_text('utf-8'));model=folder/f'{bid}-complete-2k.glb'
  assert a['status']=='passed' and r.get('approved') and a['sha256']==sha(model),'full export is not currently approved'
  for asset,s in r['sources'].items():
   verify(Path(s['remesh_report']).parent,s['sha256'])
   if not args.allow_pending:
    receipts=Path(s['remesh_report']).parent/'adoptions';receipts.mkdir(exist_ok=True);(receipts/f'{bid}.json').write_text(json.dumps(dict(building=bid,asset=asset,remeshed_sha256=s['sha256'],accepted_building_sha256=a['sha256'],acceptance=str(folder/'acceptance.json'),status='adopted_in_accepted_building'),indent=2),'utf-8')
  if not args.allow_pending and bid not in ('S01','S02','S18','S19'):publish_building(context(bid))
  for key,total in a['floor_triangles'].items():assert total<=100000,(bid,key,total)
  files={f.name:sha(f) for f in [model,*[folder/f'{bid}-F{n:02d}-complete-2k.glb' for n in (1,2,3)]]}
  records.append(dict(id=bid,status='passed',checks=a['checks'],triangles=a['triangles'],floor_triangles=a['floor_triangles'],sources=len(r['sources']),files=files))
 except Exception as error:
  if args.allow_pending:records.append(dict(id=bid,status='pending',reason=str(error)))
  else:errors.append(dict(id=bid,reason=str(error)));records.append(dict(id=bid,status='failed',reason=str(error)))
 print(bid,records[-1]['status'],flush=True)
result=dict(status='partial_audit' if args.allow_pending else 'passed' if len(records)==96 and not errors else 'failed',building_count=len(records),passed=sum(q['status']=='passed' for q in records),checks=sum(q.get('checks',0) for q in records),instanced_triangles=sum(q.get('triangles',0) for q in records),errors=errors,buildings=records)
(OUT/('progress-audit.json' if args.allow_pending else 'final-audit.json')).write_text(json.dumps(result,ensure_ascii=False,indent=2),'utf-8');publish()
print(result['status'],result['passed'],result['checks'],flush=True)
if errors:raise RuntimeError(f'{len(errors)} buildings require repair')
