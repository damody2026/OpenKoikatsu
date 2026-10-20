"""Forced, resumable current-tool refresh of every adopted city component.

Stage candidates separately. Never make a failed or mixed-tool batch authoritative.
Original geometry and previous adopted candidates/receipts are retained.
"""
import argparse,json,os,sys,hashlib,shutil,datetime,math,threading
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor,as_completed
from street_city_building_context import ROOT
from street_city_remesh_gate import ENGINE,sha,verify
from run_street_city_shop_street import run
BASE=ROOT/'output/street-social-city-production'

def inventory():
 from street_city_shop_street import inventory as shops
 from street_city_building_context import context
 from street_city_large_shop_contract import context as large
 from street_city_campus_contract import IDS,context as campus
 reports=set();assemblies=[]
 def walk(x):
  if isinstance(x,dict):
   for k,v in x.items():
    if k=='remesh_report' and isinstance(v,str):reports.add(str(Path(v).resolve()))
    walk(v)
  elif isinstance(x,list):
   for v in x:walk(v)
 paths=[(large(b['building']) if b['building'] in ('S18','S19') else context(b['building']))['out']/'assembly-report.json' for b in shops()]
 paths += [campus(b)['out']/'assembly-report.json' for b in IDS]
 paths += [BASE/'mall-v1/assembly-report.json',BASE/'campus-v1/outdoor/acceptance.json',BASE/'world-ground-v1/ground-acceptance.json']
 for path in (BASE/'world-ground-v1/assets').glob('*/remesh/remesh-report.json'):reports.add(str(path.resolve()))
 for p in paths:
  assert p.is_file(),p;assemblies.append(str(p.resolve()));walk(json.loads(p.read_text('utf8')))
 for name in ['campus-v1/delivery-acceptance.json','mall-v1/delivery-acceptance.json']:
  for p in json.loads((BASE/name).read_text('utf8')).get('files',{}):
   if p.endswith('remesh-report.json'):reports.add(str(Path(p).resolve()))
 return sorted(reports),assemblies

def options(r):
 a=r.get('arguments')
 if a:
  result=[]
  for key in ['box_panels','dissolve_planes','project_source','quad_mode','recover_source_topology']:
   if a.get(key):result.append('--'+key.replace('_','-'))
  for key in ['dense_pass','voxel_input','repair_solid','smooth']:
   if key in a:result += ['--'+key.replace('_','-'),str(a[key])]
  if a.get('pin_source_extrema'):result+=['--pin-source-extrema',*a['pin_source_extrema']]
  return result
 records=r['records'];repairs=[x for q in records for x in q.get('repairs',[])];cmd=records[0]['command'];result=[]
 box=all(q.get('conditioning','').startswith('isotropic box') for q in records)
 if box:result.append('--box-panels')
 if any(q.get('exact_coplanar_cleanup') for q in repairs):result.append('--dissolve-planes')
 if any('source projection' in q.get('postprocess','') for q in records):result.append('--project-source')
 quad=cmd[cmd.index('-p')+1]=='4'
 if quad and not box:result.append('--quad-mode')
 if any(q.get('reference_topology_recovery') or q.get('distance_validation') for q in repairs):result.append('--recover-source-topology')
 pins=sorted({a for q in repairs for a in q.get('exact_source_extrema_axes',[])})
 if pins:result+=['--pin-source-extrema',*pins]
 if not box:
  target=int(cmd[cmd.index('-f')+1]);base=r['triangle_cap']/len(records)*(.35 if quad else .75)
  dense=target/base
  if target>16 and abs(dense-1)>.02:result+=['--dense-pass',str(round(dense,6))]
 if '-S' in cmd:result+=['--smooth',cmd[cmd.index('-S')+1]]
 return result

def main():
 p=argparse.ArgumentParser();p.add_argument('--activate',action='store_true');p.add_argument('--workers',type=int,default=3);p.add_argument('--limit',type=int);a=p.parse_args()
 target=sha(ENGINE);out=BASE/('remesh-refresh-'+target[:12]);out.mkdir(exist_ok=True);ledger=out/'progress.json'
 os.environ['STREET_CITY_BLENDER_THREADS']='2';os.environ['TEMP']=str(BASE/'campus-v1/scratch');os.environ['TMP']=os.environ['TEMP']
 if ledger.exists():
  state=json.loads(ledger.read_text('utf8'));assert state['tool_sha256']==target
  known={q['report'] for q in state['assets']};paths,assemblies=inventory()
  for file in paths:
   if file in known:continue
   path=Path(file);r=json.loads(path.read_text('utf8'));source=Path(r['source']);source=source if source.is_absolute() else ROOT/source
   assert sha(source)==r['source_sha256'];aid=hashlib.sha256(str(path).lower().encode()).hexdigest()[:16];backup=out/'previous'/aid;backup.mkdir(parents=True,exist_ok=True)
   for name in ['remesh-report.json','remeshed-candidate.glb']:shutil.copy2(path.parent/name,backup/name)
   state['assets'].append(dict(id=aid,report=str(path),source=str(source.resolve()),source_sha256=sha(source),old_output_sha256=sha(path.parent/'remeshed-candidate.glb'),old_triangles=r['triangles'],cap=r['triangle_cap'],tolerance=r['tolerance_m'],options=options(r),candidate=str(path.parent/('refresh-'+target[:12])),status='pending'))
  state['assemblies']=assemblies
 else:
  paths,assemblies=inventory();assets=[]
  for file in paths:
   path=Path(file);r=json.loads(path.read_text('utf8'));source=Path(r['source']);source=source if source.is_absolute() else ROOT/source
   assert source.is_file() and sha(source)==r['source_sha256'],source
   aid=hashlib.sha256(str(path).lower().encode()).hexdigest()[:16];backup=out/'previous'/aid;backup.mkdir(parents=True,exist_ok=True)
   for name in ['remesh-report.json','remeshed-candidate.glb']:
    shutil.copy2(path.parent/name,backup/name)
   assets.append(dict(id=aid,report=str(path),source=str(source.resolve()),source_sha256=sha(source),old_output_sha256=sha(path.parent/'remeshed-candidate.glb'),old_triangles=r['triangles'],cap=r['triangle_cap'],tolerance=r['tolerance_m'],options=options(r),candidate=str(path.parent/('refresh-'+target[:12])),status='pending'))
  state=dict(status='in_progress',started_at=datetime.datetime.now().astimezone().isoformat(),tool=str(ENGINE),tool_sha256=target,adapter_sha256=sha(ROOT/'Tools/remesh_street_city_asset.py'),assemblies=assemblies,assets=assets)
  ledger.write_text(json.dumps(state,indent=2),'utf8')
 assert state['adapter_sha256']==sha(ROOT/'Tools/remesh_street_city_asset.py'),'Adapter changed: explicitly migrate candidate generation, do not silently mix receipts'
 lock=threading.Lock()
 def work(q):
  try:
   assert sha(ENGINE)==target and sha(q['source'])==q['source_sha256']
   folder=Path(q['candidate'])
   try:r=verify(folder);assert r['tool_sha256']==target
   except (OSError,ValueError,KeyError,AssertionError):
    run('Tools/remesh_street_city_asset.py',folder,'--source',q['source'],'--output',folder,'--triangle-cap',q['cap'],'--tolerance',q['tolerance'],'--threads',4,*q['options']);r=verify(folder)
   assert sha(ENGINE)==r['tool_sha256']==target
   q.update(status='passed',new_output_sha256=r['sha256'],new_triangles=json.loads((folder/'remesh-report.json').read_text('utf8'))['triangles']);q.pop('error',None)
  except Exception:
   import traceback;q.update(status='failed',error=traceback.format_exc())
  with lock:
   state['status']='candidates_in_progress';ledger.write_text(json.dumps(state,indent=2),'utf8')
   print(q['status'],sum(v['status']=='passed' for v in state['assets']),'/',len(state['assets']),q['report'],flush=True)
 selected=state['assets'][:a.limit] if a.limit else state['assets']
 with ThreadPoolExecutor(max_workers=a.workers) as pool:
  for f in as_completed([pool.submit(work,q) for q in selected]):f.result()
 if not all(q['status']=='passed' for q in state['assets']):
  state['status']='partial_or_failed';ledger.write_text(json.dumps(state,indent=2),'utf8');raise RuntimeError('Not all candidates passed; original adopted assets remain untouched')
 state['status']='all_candidates_passed';ledger.write_text(json.dumps(state,indent=2),'utf8')
 if a.activate:
  assert sha(ENGINE)==target
  for q in state['assets']:
   folder=Path(q['candidate']);verify(folder,q['new_output_sha256']);dest=Path(q['report']).parent
   original=out/'previous'/q['id']/'source.glb'
   if not original.exists():shutil.copy2(q['source'],original)
   for name in ['remeshed-candidate.glb','remeshed-candidate.blend','remesh-report.json']:
    staged=dest/(name+'.refresh-staged');shutil.copy2(folder/name,staged);os.replace(staged,dest/name)
   verify(dest,q['new_output_sha256'])
  state['status']='activated_requires_full_scene_rebuild';state['activated_at']=datetime.datetime.now().astimezone().isoformat();ledger.write_text(json.dumps(state,indent=2),'utf8')
 print(state['status'],len(state['assets']),target,flush=True)

if __name__=='__main__':main()
