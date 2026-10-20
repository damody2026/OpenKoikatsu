"""Bounded batch: keep processing unrelated products if one mesh is rejected."""
import sys,json,subprocess
from street_city_mall_merchandise import OUT,publish,download_name
from pathlib import Path

def main():
 data=publish();results=[]
 for q in data['components']:
  if q['provider']!='reuse' and not (Path('C:/Users/Damody/Downloads')/download_name(q)).exists():continue
  log=OUT/(q['id']+'-prepare.log')
  with log.open('w',encoding='utf8') as f:
   r=subprocess.run([sys.executable,'Tools/prepare_street_city_mall_merchandise.py','--ids',q['id']],stdout=f,stderr=subprocess.STDOUT)
  results.append(dict(id=q['id'],status='passed' if r.returncode==0 else 'failed',log=str(log)))
  (OUT/'batch-remesh.json').write_text(json.dumps(dict(status='failed' if any(x['status']=='failed' for x in results) else 'in_progress',processed=len(results),required=data['unique_product_models'],results=results),indent=2),'utf8')
  print(q['id'],results[-1]['status'],flush=True)
 if len(results)==data['unique_product_models'] and all(x['status']=='passed' for x in results):
  report=json.loads((OUT/'batch-remesh.json').read_text('utf8'));report['status']='passed';(OUT/'batch-remesh.json').write_text(json.dumps(report,indent=2),'utf8')

if __name__=='__main__':main()
