"""Resumable campus building production; a failed model is never published."""
import argparse,sys,subprocess,json,os,traceback,hashlib
from concurrent.futures import ThreadPoolExecutor,as_completed
from street_city_campus_contract import IDS,context,OUT,publish_research,digest
from street_city_remesh_gate import verify
from run_street_city_shop_street import ROOT,run
from street_city_atomic import write_text
from street_city_campus_finishes import generate as prepare_finishes
p=argparse.ArgumentParser();p.add_argument('--ids',nargs='+');p.add_argument('--reuse-components',action='store_true');p.add_argument('--force',action='store_true');p.add_argument('--workers',type=int,default=3);a=p.parse_args();os.environ['STREET_CITY_BLENDER_THREADS']='6';publish_research()
prepare_finishes()
scratch=OUT/'scratch';scratch.mkdir(exist_ok=True);os.environ['TEMP']=str(scratch.resolve());os.environ['TMP']=str(scratch.resolve())
if not a.reuse_components:subprocess.run([sys.executable,str(ROOT/'Tools/prepare_street_city_campus_components.py')],cwd=ROOT,check=True)
records=[];selected=list(a.ids or IDS)
for bid in selected:context(bid)
def process(bid):
 try:
  c=context(bid);out=c['out'];reuse=False
  try:
   r=json.loads((out/'assembly-report.json').read_text('utf8'))
   reuse=r['contract_sha256']==digest(c) and all((__import__('pathlib').Path(path).name=='street_city_campus_contract.py' or hashlib.sha256((ROOT/'Tools'/__import__('pathlib').Path(path).name).read_bytes()).hexdigest()==sha) and hashlib.sha256(__import__('pathlib').Path(path).read_bytes()).hexdigest()==sha for path,sha in r['recipe_sources'].items())
   for source in r['sources'].values():verify(__import__('pathlib').Path(source['remesh_report']).parent,source['sha256'])
  except (OSError,ValueError,KeyError,AssertionError):reuse=False
  if not reuse or a.force:run('Tools/assemble_street_city_campus.py',out,'--building',bid)
  run('Tools/accept_street_city_campus.py',out,'--building',bid)
  return dict(id=bid,status='passed')
 except Exception:return dict(id=bid,status='failed',error=traceback.format_exc())
with ThreadPoolExecutor(max_workers=max(1,min(4,a.workers))) as pool:
 for future in as_completed([pool.submit(process,bid) for bid in selected]):
  row=future.result();records.append(row);print(row['id'],row['status'],flush=True)
  write_text(OUT/'progress.json',json.dumps(dict(status='in_progress',buildings=records),indent=2))
failed=any(r['status']!='passed' for r in records)
write_text(OUT/'progress.json',json.dumps(dict(status='failed' if failed else 'passed',buildings=records),indent=2))
if failed:raise RuntimeError('Campus buildings failed; inspect individual assembly/acceptance reports')
