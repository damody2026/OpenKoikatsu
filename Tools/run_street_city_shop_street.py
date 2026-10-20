"""Resumable production: distinct programme, identical geometry reused only by contract hash."""
import argparse,json,hashlib,subprocess,sys,html,re,os
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
from street_city_building_context import context,ROOT
from street_city_shop_street import inventory,publish,OUT as REVIEW
from street_city_retail_profiles import placements,COMMON
from street_city_remesh_gate import verify as verify_remesh
from street_city_programme_gate import verify_current
from street_city_atomic import write_text as atomic_write
BLENDER='C:/Program Files/Blender Foundation/Blender 5.2/blender.exe'
def run(script,out,*args):
 status=out/(Path(script).stem+'-status.json');out.mkdir(parents=True,exist_ok=True)
 with status.with_suffix('.log').open('w',encoding='utf-8') as log:
  threads=['--threads',str(int(os.environ['STREET_CITY_BLENDER_THREADS']))] if os.environ.get('STREET_CITY_BLENDER_THREADS') else []
  p=subprocess.run([BLENDER,'--background','--factory-startup',*threads,'--python',str(ROOT/'Tools/run_street_city_blender_task.py'),'--','--script',str(ROOT/script),'--status-file',str(status),'--',*map(str,args)],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
 result=json.loads(status.read_text('utf-8')) if status.exists() else {}
 if p.returncode or result.get('status')!='succeeded':raise RuntimeError(str(status))
def prepare_wall_style(bid):
 from street_city_shop_wall_styles import OUT,generate,profile_hash,sha
 path=OUT/'textures'/bid/'generation.json';want=dict(generator_sha256=sha(ROOT/'Tools/street_city_shop_wall_styles.py'),profile_sha256=profile_hash(bid))
 if not path.exists() or json.loads(path.read_text('utf8'))!=want:generate(bid)

def signature(b,f):
 fields=['z_m','height_m','cells','private_rooms','doors','exterior_openings','stair_geometry']
 data=dict(structure_revision=2,footprint=b['footprint_m'],floor={k:f[k] for k in fields})
 def geometry_only(x):
  if isinstance(x,dict):return {k:geometry_only(v) for k,v in x.items() if k not in ('name','motion_mesh_verified','hinge_convention')}
  if isinstance(x,list):return [geometry_only(v) for v in x]
  if isinstance(x,float):return round(x,6)
  return x
 text=json.dumps(geometry_only(data),sort_keys=True,ensure_ascii=False).replace(b['building'],'BUILDING')
 return hashlib.sha256(text.encode()).hexdigest()
def sign(C):
 from street_city_localization import path_for
 import shutil
 # Shared brand artwork, localized names; never bake an untranslated name.
 source=path_for('building.'+C['id'],'zh-Hant');assert source.exists(),source
 shutil.copy2(source,C['out']/'sign-2k.png')
def profile_hash(C):
 from street_city_shop_wall_styles import profile_hash as wall_hash
 value=[signature(C['building'],f) for f in C['building']['small_shop_design']['floors']]+[placements(C),wall_hash(C['id'])]
 return hashlib.sha256(json.dumps(value,sort_keys=True,ensure_ascii=False).encode()).hexdigest()

def already_accepted(C,large=False):
 try:
  out=C['out'];bid=C['id'];a=json.loads((out/'acceptance.json').read_text('utf-8'));r=json.loads((out/'assembly-report.json').read_text('utf-8'))
  if a['status']!='passed' or not r.get('approved') or hashlib.sha256((out/f'{bid}-complete-2k.glb').read_bytes()).hexdigest()!=a['sha256']:return False
  from street_city_shop_wall_styles import verify_metadata
  verify_metadata(bid,r)
  if large:
   from street_city_large_shop_contract import contract_hash
   if r.get('contract_sha256')!=contract_hash(C):return False
  else:
   verify_current(C,r)
   if r.get('programme_sha256')!=profile_hash(C):return False
  for s in r['sources'].values():verify_remesh(Path(s['remesh_report']).parent,s['sha256'])
  return True
 except (OSError,ValueError,KeyError,AssertionError):return False

def process_large(bid,prepare_only=False,assemble_only=False):
 from street_city_large_shop_contract import context as large_context
 C=large_context(bid);out=C['out'];out.mkdir(parents=True,exist_ok=True)
 if not prepare_only and already_accepted(C,large=True):return 'passed'
 if not assemble_only:
  for f in C['building']['floors']:
   no=f['floor'];folder=out/f'structure/F{no:02d}';run('Tools/build_street_city_large_shop_structure.py',folder,'--building',bid,'--floor',no)
   target=folder/('remesh-planar' if bid=='S19' else 'remesh');extra=['--dissolve-planes'] if bid=='S19' else []
   run('Tools/remesh_street_city_asset.py',folder,'--source',folder/'source.glb','--output',target,'--triangle-cap',20000,'--box-panels',*extra);verify_remesh(target)
 if prepare_only:return 'structure_prepared'
 prepare_wall_style(bid)
 run('Tools/build_street_city_large_shop_assets.py',COMMON)
 for q in json.loads((COMMON/'large-shop-board-sources.json').read_text('utf-8')):
  target=COMMON/q['id']/('remesh-planar' if bid=='S19' else 'remesh')
  try:verify_remesh(target)
  except (OSError,ValueError,KeyError,AssertionError):
   extra=['--dissolve-planes'] if bid=='S19' else [];run('Tools/remesh_street_city_asset.py',target.parent,'--source',q['source'],'--output',target,'--triangle-cap',q['triangle_cap'],'--box-panels',*extra);verify_remesh(target)
 missing=[]
 for aid in ['toilet-lod4500','sink-lod2500']:
  try:verify_remesh(COMMON/aid/'remesh')
  except (OSError,ValueError,KeyError,AssertionError):missing.append(aid)
 if missing:subprocess.run([sys.executable,str(ROOT/'Tools/prepare_street_city_retail_tripo.py'),'--ids',*missing],check=True,cwd=ROOT)
 if bid=='S19':
  for aid,source in [('window',ROOT/'output/street-social-city-production/S01-modular-components-v1/local-components/window/source.glb'),('retail-rack',COMMON/'retail-rack/source.glb')]:
   target=COMMON/aid/'remesh-planar'
   try:verify_remesh(target)
   except (OSError,ValueError,KeyError,AssertionError):run('Tools/remesh_street_city_asset.py',target.parent,'--source',source,'--output',target,'--triangle-cap',1000,'--box-panels','--dissolve-planes');verify_remesh(target)
 run('Tools/assemble_street_city_large_shop.py',out,'--building',bid);run('Tools/accept_street_city_large_shop.py',out,'--building',bid)
 subprocess.run([sys.executable,str(ROOT/'Tools/publish_street_city_large_shop.py'),bid],check=True,cwd=ROOT)
 return 'passed'
def equivalent(C):
 digest=profile_hash(C)
 for b in inventory():
  bid=b['building']
  if bid in ('S01','S02',C['id']) or not b.get('small_shop_design'):continue
  other=context(bid)
  f=other['out']/'acceptance.json';rpath=other['out']/'assembly-report.json'
  if not f.exists() or not rpath.exists():continue
  result=json.loads(f.read_text('utf-8'));r=json.loads(rpath.read_text('utf-8'))
  if result['status']!='passed' or not r.get('approved') or profile_hash(other)!=digest:continue
  if not already_accepted(other):continue
  expected=placements(other)[0];actual={q['name']:q for q in r['instances']}
  mismatch=False
  for q in expected:
   item=actual.get(f'F{q["floor"]:02d}-{q["name"]}')
   if not item or item['asset']!=q['asset'] or abs(item['yaw_rad']-__import__('math').radians(q['yaw']))>1e-6 or any(abs(a-b)>1e-6 for a,b in zip(item['position_m'],q['pos'])) or any(abs(a-b)>1e-6 for a,b in zip(item['scale'],q['scale'])):mismatch=True;break
  if mismatch:continue
  glb=other['out']/f'{bid}-complete-2k.glb'
  if hashlib.sha256(glb.read_bytes()).hexdigest()!=result['sha256']:continue
  if any(hashlib.sha256(Path(q['path']).read_bytes()).hexdigest()!=q['sha256'] for q in r['sources'].values()):continue
  try:
   for q in r['sources'].values():verify_remesh(Path(q['remesh_report']).parent,q['sha256'])
  except (AssertionError,OSError,KeyError,ValueError):continue
  return bid
def publish_building(C):
 out=C['out'];bid=C['id'];a=json.loads((out/'acceptance.json').read_text('utf-8'));r=json.loads((out/'assembly-report.json').read_text('utf-8'));model=out/f'{bid}-complete-2k.glb'
 assert a['status']=='passed' and a['sha256']==hashlib.sha256(model.read_bytes()).hexdigest()
 assert r['building']==bid
 verify_current(C,r)
 for aid,s in r['sources'].items():assert hashlib.sha256(Path(s['path']).read_bytes()).hexdigest()==s['sha256']
 for s in r['sources'].values():verify_remesh(Path(s['remesh_report']).parent,s['sha256'])
 for aid,s in r['sources'].items():
  receipts=Path(s['remesh_report']).parent/'adoptions';receipts.mkdir(exist_ok=True)
  (receipts/f'{bid}.json').write_text(json.dumps(dict(building=bid,asset=aid,remeshed_sha256=s['sha256'],accepted_building_sha256=a['sha256'],acceptance=str(out/'acceptance.json'),status='adopted_in_accepted_building'),ensure_ascii=False,indent=2),'utf-8')
 finish=json.loads((out/'material-report.json').read_text('utf-8'))
 for name,sha in finish['files'].items():assert hashlib.sha256((Path(finish['texture_directory'])/name).read_bytes()).hexdigest()==sha
 name=C['building']['small_shop_design']['name'];rows,equipment=placements(C)
 atomic_write(out/'programme.json',json.dumps(dict(building=bid,equipment=equipment,placements=rows),ensure_ascii=False,indent=2))
 page=f'<!doctype html><html lang="zh-Hant"><meta charset="utf-8"><title>{bid} {html.escape(name)}</title><style>body{{font:17px/1.7 system-ui;max-width:1100px;margin:30px auto;background:#f4f1eb}}img{{width:100%}}td{{padding:10px;border-bottom:1px solid #ccc}}</style><h1>{bid} {html.escape(name)}</h1><p>{C["width"]}×{C["depth"]}m；三層；每層低於10萬三角面。{a["checks"]}項實際GLB驗收通過。</p><p><a href="{bid}-complete-2k.blend">Blender</a> · <a href="{bid}-complete-2k.glb">GLB含電梯動畫</a> · <a href="acceptance.json">驗收</a> · <a href="assembly-report.json">來源／remesh</a> · <a href="programme.json">用途與設備配置</a> · <a href="material-report.json">2K公尺PBR</a> · <a href="../../shop-street-v1/index.html">整條商店街進度</a></p><img src="{bid}-exterior.png"><table>'
 for f in C['building']['small_shop_design']['floors']:
  no=f['floor'];page+=f'<tr><td>F{no:02d}，Z={f["z_m"]}m</td><td>{html.escape(next(c["name"] for c in f["cells"] if c["kind"]=="activity"))}</td><td>{a["floor_triangles"][str(no)]:,}三角面</td><td><a href="{bid}-F{no:02d}-complete-2k.glb">單層GLB</a></td></tr>'
 page+='</table><p>2m走廊、1.2m一般門、2.4m主入口；各層衛浴、單一電梯車廂、上下銜接樓梯。貼圖為原創程序材質或Tripo材質，元件來源詳列。未接遊戲導航／碰撞／控制器，未宣稱衛浴為封閉實體。</p>'
 for no in [1,2,3]:page+=f'<h2>F{no:02d}</h2><img src="{bid}-F{no:02d}-cutaway.png">'
 page+='<h2>研究來源</h2><p>'+html.escape(C['building']['small_shop_design'].get('source_url',''))+'</p></html>'
 atomic_write(out/'index.html',page);r.update(approved=True,status='passed_exported_full_building_geometry',programme_sha256=profile_hash(C));atomic_write(out/'assembly-report.json',json.dumps(r,ensure_ascii=False,indent=2))
 from publish_street_city_shop_wall_styles import decorate_review
 decorate_review(bid,out)
def main():
 p=argparse.ArgumentParser();p.add_argument('--ids',nargs='*');p.add_argument('--prepare-only',action='store_true');p.add_argument('--assemble-only',action='store_true');p.add_argument('--ledger-name');a=p.parse_args()
 if a.ledger_name:assert re.fullmatch(r'[a-z0-9-]{1,64}',a.ledger_name),'Use a simple ledger name'
 result_file=REVIEW/(a.ledger_name+'.json' if a.ledger_name else 'prepare-results.json' if a.prepare_only else 'batch-results.json')
 REVIEW.mkdir(exist_ok=True);cachefile=REVIEW/'structure-cache.json';cache=json.loads(cachefile.read_text('utf-8')) if cachefile.exists() else {}
 # Migration of previously accepted south-facing geometry to stable numeric keys.
 for previous in inventory():
  if not previous.get('small_shop_design'):continue
  P=context(previous['building']);report=P['out']/'assembly-report.json';accept=P['out']/'acceptance.json'
  if P['world_yaw_deg'] or not report.exists() or not accept.exists():continue
  r=json.loads(report.read_text('utf-8'));acc=json.loads(accept.read_text('utf-8'))
  if acc['status']!='passed' or not r.get('approved'):continue
  for f in P['building']['small_shop_design']['floors']:
   asset=r['sources'][f'structure-F{f["floor"]:02d}'];path=Path(asset['path'])
   source_meta=Path(json.loads((path.parent/'remesh-report.json').read_text('utf-8'))['source']).parent/'source.json'
   if not source_meta.exists() or json.loads(source_meta.read_text('utf-8')).get('structure_revision')!=2:continue
   if hashlib.sha256(path.read_bytes()).hexdigest()==asset['sha256']:cache[signature(P['building'],f)]=str(path.parent)
 results=[]
 for b in inventory():
  bid=b['building']
  if bid in ('S01','S02') or (a.ids and bid not in a.ids):continue
  try:
   if bid in ('S18','S19'):
    results.append(dict(id=bid,status=process_large(bid,a.prepare_only,a.assemble_only)));atomic_write(result_file,json.dumps(results,ensure_ascii=False,indent=2));publish();print(json.dumps(results[-1],ensure_ascii=False),flush=True);continue
   C=context(bid);b=C['building'];out=C['out'];out.mkdir(parents=True,exist_ok=True);(out/'furniture').mkdir(exist_ok=True)
   if not a.prepare_only and already_accepted(C):
    publish_building(C);results.append(dict(id=bid,status='passed'));atomic_write(result_file,json.dumps(results,ensure_ascii=False,indent=2));publish();print(json.dumps(results[-1],ensure_ascii=False),flush=True);continue
   (out/'furniture/sources.json').write_text('[]','utf-8');index={}
   for f in ([] if a.assemble_only else b['small_shop_design']['floors']):
    no=f['floor'];sig=signature(b,f);folder=out/f'structure/F{no:02d}'
    if sig in cache:
     report=Path(cache[sig])/'remesh-report.json';r=json.loads(report.read_text('utf-8'));source=Path(r['source']);source=source if source.is_absolute() else ROOT/source
     assert r['status']=='passed' and hashlib.sha256(source.read_bytes()).hexdigest()==r['source_sha256']
     index[f'structure-F{no:02d}']=cache[sig]
    else:
     run('Tools/build_street_city_modular_floor_structure.py',folder,'--building',bid,'--floor',no,'--building-mode')
     run('Tools/remesh_street_city_asset.py',folder,'--source',folder/'source.glb','--output',folder/'remesh','--triangle-cap',10000,'--box-panels')
     r=json.loads((folder/'remesh/remesh-report.json').read_text('utf-8'));assert r['status']=='passed'
     index[f'structure-F{no:02d}']=str((folder/'remesh').resolve());cache[sig]=index[f'structure-F{no:02d}'];cachefile.write_text(json.dumps(cache,indent=2),'utf-8')
   if not a.assemble_only:(out/'structure-library.json').write_text(json.dumps(index,indent=2),'utf-8')
   else:assert (out/'structure-library.json').exists(),'Structure preparation incomplete'
   sign(C)
   if not a.prepare_only:
    prepare_wall_style(bid)
    placements(C) # Reject unknown equipment before exporting anything.
    previous=equivalent(C)
    if previous:run('Tools/clone_street_city_retail_building.py',out,previous,bid)
    else:run('Tools/assemble_street_city_building.py',out,'--building',bid,'--quick-preview')
    run('Tools/accept_street_city_building.py',out,'--building',bid)
    publish_building(C)
   results.append(dict(id=bid,status='structure_prepared' if a.prepare_only else 'passed'))
  except Exception as e:
   results.append(dict(id=bid,status='failed',reason=str(e)))
   with (ROOT/'Docs/StreetSocialCity/ERRORS_AND_PREVENTION.md').open('a',encoding='utf-8') as log:log.write(f'\n- 商店街批次 {bid}：{e}。保留候選與日誌，未通過不可宣稱完成。\n')
  atomic_write(result_file,json.dumps(results,ensure_ascii=False,indent=2));publish();print(json.dumps(results[-1],ensure_ascii=False),flush=True)
if __name__=='__main__':main()
