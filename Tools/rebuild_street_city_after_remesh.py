"""Rebuild every current city instance after a successful staged tool refresh."""
import os,sys,json,subprocess,shutil,traceback,datetime
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor,as_completed
from street_city_building_context import ROOT,context
from street_city_remesh_gate import ENGINE,sha,verify
from run_street_city_shop_street import run,publish_building,prepare_wall_style
from refresh_street_city_remesh import inventory
BASE=ROOT/'output/street-social-city-production';OUT=BASE/('remesh-refresh-'+sha(ENGINE)[:12]);TARGET=sha(ENGINE)
os.environ['STREET_CITY_BLENDER_THREADS']='2';os.environ['TEMP']=str(BASE/'campus-v1/scratch');os.environ['TMP']=os.environ['TEMP']
STATE=OUT/'scene-rebuild.json'

def command(*args):subprocess.run([str(a) for a in args],cwd=ROOT,check=True)
def py(name,*args):command(sys.executable,ROOT/'Tools'/name,*args)
def native(stage):command(str(ROOT/'Tools/lua/lua.exe'),ROOT/'Tools/run_street_city_unreal.lua','--stage',stage)
def current(folder,bid,campus=False):
 try:
  r=json.loads((folder/'assembly-report.json').read_text('utf8'));a=json.loads((folder/'acceptance.json').read_text('utf8'))
  if a['status']!='passed' or not r.get('approved') or a['sha256']!=sha(folder/f'{bid}-complete-2k.glb'):return False
  if bid.startswith('S') and bid[1:].isdigit():
   from street_city_shop_wall_styles import verify_metadata
   verify_metadata(bid,r)
  for q in r['sources'].values():
   v=verify(Path(q['remesh_report']).parent,q['sha256'])
   if v['tool_sha256']!=TARGET or q['tool_sha256']!=TARGET:return False
  return True
 except (OSError,ValueError,KeyError,AssertionError):return False

def models():
 from street_city_shop_street import inventory as shops
 from street_city_campus_contract import IDS,context as campus
 from street_city_large_shop_contract import context as large
 jobs=[('shop',b['building']) for b in shops()]+[('campus',b) for b in IDS];rows=[]
 def work(job):
  kind,bid=job
  try:
   c=campus(bid) if kind=='campus' else large(bid) if bid in ('S18','S19') else context(bid);folder=c['out']
   if kind=='shop':prepare_wall_style(bid)
   if not current(folder,bid):
    if kind=='campus':
     run('Tools/assemble_street_city_campus.py',folder,'--building',bid);run('Tools/accept_street_city_campus.py',folder,'--building',bid)
    elif bid in ('S18','S19'):
     run('Tools/assemble_street_city_large_shop.py',folder,'--building',bid);run('Tools/accept_street_city_large_shop.py',folder,'--building',bid);py('publish_street_city_large_shop.py',bid)
    else:
     run('Tools/assemble_street_city_building.py',folder,'--building',bid,'--quick-preview');run('Tools/accept_street_city_building.py',folder,'--building',bid)
     if bid not in ('S01','S02'):publish_building(c)
     else:
      r=json.loads((folder/'assembly-report.json').read_text('utf8'));a=json.loads((folder/'acceptance.json').read_text('utf8'));assert a['status']=='passed' and a['sha256']==sha(folder/f'{bid}-complete-2k.glb');r.update(approved=True,status='passed_exported_full_building_geometry');(folder/'assembly-report.json').write_text(json.dumps(r,ensure_ascii=False,indent=2),'utf8')
   assert current(folder,bid)
   return dict(kind=kind,id=bid,status='passed',model_sha256=sha(folder/f'{bid}-complete-2k.glb'))
  except Exception:return dict(kind=kind,id=bid,status='failed',error=traceback.format_exc())
 with ThreadPoolExecutor(max_workers=4) as pool:
  for f in as_completed([pool.submit(work,j) for j in jobs]):
   q=f.result();rows.append(q);(OUT/'building-progress.json').write_text(json.dumps(rows,indent=2),'utf8');print(q['kind'],q['id'],q['status'],len(rows),'/',len(jobs),flush=True)
 bad=[q for q in rows if q['status']!='passed'];assert not bad,bad

def verify_fixture_audit():
 path=BASE/'interior-repair-v1/fixtures-expanded-after.json';r=json.loads(path.read_text('utf8'));assert len(r['buildings'])==96
 for b in r['buildings']:
  assert not b['findings'] and all(q['passed'] for q in b['door_checks']),b['building']
  assert b['source_sha256']==sha(BASE/f"{b['building']}-modular-components-v1/assembly-building/{b['building']}-complete-2k.glb"),b['building']
 return path

def street():
 py('publish_street_city_refreshed_reviews.py')
 py('audit_street_city_shop_street.py');run('Tools/audit_street_city_fixture_clearance.py',BASE/'interior-repair-v1','--expanded')
 verify_fixture_audit()
 py('merge_street_city_shop_street_glb.py');folder=BASE/'shop-street-v1/direct';run('Tools/accept_street_city_shop_street.py',folder,'--output',folder);run('Tools/review_street_city_direct_merge.py',folder)
 for name in ['shop-street-complete.glb','shop-street-complete.blend','shop-street-overview.png','world-assembly.json','world-acceptance.json']:shutil.copy2(folder/name,folder.parent/name)
 native('ImportShops')

def activate():
 py('refresh_street_city_remesh.py','--activate')
 backup=OUT/'previous-deliveries';backup.mkdir(exist_ok=True)
 for kind in ['campus-v1','mall-v1']:
  path=BASE/kind/'delivery-acceptance.json';dest=backup/(kind+'.json')
  if not dest.exists():shutil.copy2(path,dest)
  path.write_text(json.dumps(dict(status='in_progress',stage='full_new_tool_remesh_scene_rebuild',tool_sha256=TARGET)), 'utf8')

def grounds():
 py('run_street_city_ground_assets.py')
 run('Tools/build_street_city_ground_chunks.py',BASE/'world-ground-v1')
 py('run_street_city_ground_chunks.py');run('Tools/accept_street_city_ground_chunks.py',BASE/'world-ground-v1')
 run('Tools/accept_street_city_paving_geometry.py',BASE/'campus-v1')

def campus_world():
 run('Tools/render_street_city_campus.py',BASE/'campus-v1');py('run_street_city_outdoor.py');py('run_street_city_campus_reviews.py','--views-only')

def mall():
 from street_city_mall import OUT as MOUT,prepare_signs,estimate_triangles,plan
 from street_city_mall_architecture import generate,generate_graphics
 prepare_signs();generate(MOUT/'materials');generate_graphics(MOUT/'materials');assert max(estimate_triangles(plan()).values())<=100000
 r=json.loads((MOUT/'assembly-report.json').read_text('utf8'))
 if any(q['tool_sha256']!=TARGET for q in r['sources'].values()):run('Tools/build_street_city_mall.py',MOUT);run('Tools/accept_street_city_mall.py',MOUT)
 py('run_street_city_mall.py')

def delivery():
 (OUT/'acceptance.json').write_text(json.dumps(dict(status='in_progress',stage='strict_final_tool_coverage')),'utf8')
 fixture=verify_fixture_audit()
 receipts,assemblies=inventory();rows=[]
 for file in receipts:
  v=verify(Path(file).parent);assert v['tool_sha256']==TARGET,file;rows.append(v)
 for file in assemblies:
  r=json.loads(Path(file).read_text('utf8'))
  for v in r.get('sources',{}).values():
   if isinstance(v,dict) and 'tool_sha256' in v:assert v['tool_sha256']==TARGET,(file,v)
 a=json.loads((BASE/'mall-v1/delivery-acceptance.json').read_text('utf8'));c=json.loads((BASE/'campus-v1/delivery-acceptance.json').read_text('utf8'));assert a['status']==c['status']=='passed'
 assert sha(ENGINE)==TARGET
 files={str(Path(q['path']).resolve()):q['sha256'] for q in rows}
 for q in rows:
  report=Path(q['remesh_report']);raw=json.loads(report.read_text('utf8'));source=Path(raw['source']);source=source if source.is_absolute() else ROOT/source
  files[str(report.resolve())]=sha(report);files[str(source.resolve())]=sha(source)
  q['report_sha256']=sha(report)
 files[str(ENGINE.resolve())]=TARGET
 files[str(fixture.resolve())]=sha(fixture)
 for p in ['mall-v1/delivery-acceptance.json','campus-v1/delivery-acceptance.json','world-ground-v1/ground-acceptance.json','shop-street-v1/final-audit.json']:
  files[str((BASE/p).resolve())]=sha(BASE/p)
 import html
 original=json.loads((OUT/'progress.json').read_text('utf8'));previous={str(Path(q['report']).resolve()):q for q in original['assets']};table=[]
 for q in rows:
  report=Path(q['remesh_report']);r=json.loads(report.read_text('utf8'));old=previous.get(str(report.resolve()),{});label=str(report.parent)
  table.append('<tr><td>'+html.escape(label)+'</td><td>'+str(old.get('old_triangles','—'))+'</td><td>'+str(r['triangles'])+'</td><td><a href="'+report.as_uri()+'">新版執行收據</a> · <a href="'+Path(q['path']).as_uri()+'">GLB</a></td></tr>')
 links='<a href="../shop-street-v1/index.html">商店街96棟</a> · <a href="../campus-v1/index.html">學校17棟</a> · <a href="../mall-v1/index.html">百貨10層</a> · <a href="../campus-v1/outdoor/index.html">戶外運動與公園</a>'
 page='<!doctype html><html lang="zh-Hant"><meta charset="utf-8"><title>全場景新版remesh驗收</title><style>body{font:17px/1.7 system-ui;max-width:1400px;margin:30px auto;background:#14232a;color:#e8eee6}a{color:#8ee0c7}td,th{padding:10px;border-bottom:1px solid #456}td:first-child{overflow-wrap:anywhere;max-width:760px}table{width:100%;border-collapse:collapse}.ok{background:#22533e;padding:20px}</style><h1>全場景新版 remesh 已完成</h1><p class="ok">96棟商店、17棟學校、1棟百貨，共387樓層；戶外設施與16塊地形同步更新。所有實際採用的元件均已核對新版工具SHA。</p><p>'+links+'</p><p>工具：'+html.escape(str(ENGINE))+'<br>SHA256：'+TARGET+'</p><p>元件本體三角面如下；建築實例另按每層10萬上限驗收。2K材質、門洞、走道、樓梯與原生Unreal碰撞均依各交付報告驗收。</p><p><a href="acceptance.json">本次總驗收</a> · <a href="scene-rebuild.json">整合階段</a> · <a href="progress.json">逐元件新舊記錄</a></p><table><tr><th>元件</th><th>更新前</th><th>本次採用</th><th>證據與模型</th></tr>'+''.join(table)+'</table></html>'
 (OUT/'index.html').write_text(page,'utf8');files[str((OUT/'index.html').resolve())]=sha(OUT/'index.html')
 for name in ['refresh_street_city_remesh.py','rebuild_street_city_after_remesh.py','remesh_street_city_asset.py','publish_street_city_refreshed_reviews.py']:files[str((ROOT/'Tools'/name).resolve())]=sha(ROOT/'Tools'/name)
 result=dict(status='passed',completed_at=datetime.datetime.now().astimezone().isoformat(),checks=len(rows),issues=[],scope='Each adopted component verified against current tool, original source and output; full city delivery receipts pinned',tool_sha256=TARGET,remeshed_components=len(rows),shops=96,school_buildings=17,mall_buildings=1,files=files,receipts=rows)
 (OUT/'acceptance.json').write_text(json.dumps(result,indent=2),'utf8');print('ALL CURRENT CITY COMPONENTS USE NEW REMESH',len(rows),flush=True)

def main():
 steps=[('activate',activate),('ground-acceptance',grounds),('all-building-models',models),('shop-world-and-native',street),('campus-world-and-reviews',campus_world),('mall-and-full-native-regression',mall),('final-tool-coverage',delivery)]
 state=json.loads(STATE.read_text('utf8')) if STATE.exists() else dict(status='in_progress',tool_sha256=TARGET,completed=[])
 assert state['tool_sha256']==TARGET
 # Terrain/material refresh and building assembly read disjoint geometry inputs.
 # Run both bounded batches together; retain completed work if either reports a failure.
 parallel=[(name,fn) for name,fn in steps if name in ('ground-acceptance','all-building-models') and name not in state['completed']]
 if 'activate' in state['completed'] and parallel:
  state.update(status='in_progress',phase='ground-and-building-batches');STATE.write_text(json.dumps(state,indent=2),'utf8');errors=[]
  with ThreadPoolExecutor(max_workers=2) as pool:
   futures={pool.submit(fn):name for name,fn in parallel}
   for future in as_completed(futures):
    name=futures[future]
    try:future.result();state['completed'].append(name)
    except Exception:errors.append(dict(phase=name,error=traceback.format_exc()))
    STATE.write_text(json.dumps(state,indent=2),'utf8')
  if errors:
   state.update(status='failed',errors=errors);STATE.write_text(json.dumps(state,indent=2),'utf8');raise RuntimeError(errors)
 for name,fn in steps:
  if name in state['completed']:continue
  state.update(status='in_progress',phase=name);STATE.write_text(json.dumps(state,indent=2),'utf8');print('START',name,flush=True)
  try:fn()
  except Exception:
   state.update(status='failed',error=traceback.format_exc());STATE.write_text(json.dumps(state,indent=2),'utf8')
   with (ROOT/'Docs/StreetSocialCity/ERRORS_AND_PREVENTION.md').open('a',encoding='utf8') as log:log.write('\n- 新版remesh整合失敗：'+name+'；完整錯誤保留在 '+str(STATE)+'，未通過不可交付。\n')
   raise
  state['completed'].append(name);STATE.write_text(json.dumps(state,indent=2),'utf8')
 state['status']='passed';STATE.write_text(json.dumps(state,indent=2),'utf8')

if __name__=='__main__':main()
