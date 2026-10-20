"""Resume all 96 owner finishes, then validate their current shared city."""
import sys,os,json,subprocess,shutil,traceback,datetime,argparse
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor,as_completed
from street_city_building_context import ROOT,context
from street_city_large_shop_contract import context as large_context
from street_city_shop_wall_styles import OUT,generate,profile_hash,verify_metadata,sha
from street_city_shop_street import inventory
from run_street_city_shop_street import run,publish_building
from publish_street_city_shop_wall_styles import publish
os.environ['STREET_CITY_BLENDER_THREADS']='2';os.environ['TEMP']='E:/StreetSocialCityProduction/campus-v1/scratch';os.environ['TMP']=os.environ['TEMP'];os.environ['STREET_CITY_ASSET_PYTHON']=sys.executable

def py(script,*args):subprocess.run([sys.executable,str(ROOT/'Tools'/script),*map(str,args)],cwd=ROOT,check=True)
def native(*args):subprocess.run([str(ROOT/'Tools/lua/lua.exe'),str(ROOT/'Tools'/args[0]),*args[1:]],cwd=ROOT,check=True)

def ready(bid,folder):
 try:
  a=json.loads((folder/'acceptance.json').read_text('utf8'));r=json.loads((folder/'assembly-report.json').read_text('utf8'));v=json.loads((folder/'wall-style-render.json').read_text('utf8'));verify_metadata(bid,r)
  return a['status']=='passed' and a['sha256']==sha(folder/f'{bid}-complete-2k.glb')==v['model_sha256'] and v['renderer_sha256']==sha(ROOT/'Tools/restyle_street_city_shop.py') and all(sha(folder/f)==h for f,h in v['images'].items())
 except (OSError,ValueError,KeyError,AssertionError):return False

def work(bid):
 try:
  C=large_context(bid) if bid in ('S18','S19') else context(bid);folder=C['out']
  if not ready(bid,folder):
   generation=OUT/'textures'/bid/'generation.json';want=dict(generator_sha256=sha(ROOT/'Tools/street_city_shop_wall_styles.py'),profile_sha256=profile_hash(bid))
   if not generation.exists() or json.loads(generation.read_text('utf8'))!=want:generate(bid)
   run('Tools/restyle_street_city_shop.py',folder,'--building',bid)
   run('Tools/accept_street_city_large_shop.py' if bid in ('S18','S19') else 'Tools/accept_street_city_building.py',folder,'--building',bid)
  assert ready(bid,folder)
  if bid in ('S18','S19'):py('publish_street_city_large_shop.py',bid)
  elif bid in ('S01','S02'):
   r=json.loads((folder/'assembly-report.json').read_text('utf8'));r.update(approved=True,status='passed_exported_full_building_geometry');(folder/'assembly-report.json').write_text(json.dumps(r,ensure_ascii=False,indent=2),'utf8')
  else:publish_building(C)
  return dict(id=bid,status='passed',model_sha256=sha(folder/f'{bid}-complete-2k.glb'))
 except Exception:return dict(id=bid,status='failed',error=traceback.format_exc())

def batch(ids):
 rows=[];OUT.mkdir(exist_ok=True)
 with ThreadPoolExecutor(max_workers=6) as pool:
  for future in as_completed([pool.submit(work,b) for b in ids]):
   row=future.result();rows.append(row);(OUT/'progress.json').write_text(json.dumps(dict(status='in_progress',completed=len(rows),total=len(ids),buildings=rows),ensure_ascii=False,indent=2),'utf8');print(row['id'],row['status'],len(rows),'/',len(ids),flush=True)
 bad=[q for q in rows if q['status']!='passed']
 if bad:
  with (ROOT/'Docs/StreetSocialCity/ERRORS_AND_PREVENTION.md').open('a',encoding='utf8') as f:f.write('\n- 店面牆面批次未通過：'+json.dumps(bad,ensure_ascii=False)+'；保留逐店日誌與原版備份，禁止發布完整交付。\n')
  raise RuntimeError(bad)
 (OUT/'progress.json').write_text(json.dumps(dict(status='passed',completed=len(rows),total=len(ids),buildings=rows),ensure_ascii=False,indent=2),'utf8')
 return rows

def delivery():
 from rebuild_street_city_after_remesh import verify_fixture_audit
 files={};checks=[]
 def pin(p):p=Path(p).resolve();files[str(p)]=sha(p)
 for b in inventory():
  bid=b['building'];folder=ROOT/f'output/street-social-city-production/{bid}-modular-components-v1/assembly-building';assert ready(bid,folder),bid;r=json.loads((folder/'assembly-report.json').read_text('utf8'));assert r['approved'];verify_metadata(bid,r)
  old=json.loads((OUT/'before'/bid/'acceptance.json').read_text('utf8'));a=json.loads((folder/'acceptance.json').read_text('utf8'));assert old['floor_triangles']==a['floor_triangles'],bid
  for name in ['assembly-report.json','acceptance.json','wall-style-report.json','wall-style-render.json',f'{bid}-complete-2k.glb',f'{bid}-complete-2k.blend','index.html']:pin(folder/name)
  view=json.loads((folder/'wall-style-render.json').read_text('utf8'))
  for name in view['images']:pin(folder/name)
  for name in r['wall_style']['files']:pin(Path(r['wall_style']['texture_directory'])/name)
  if r['wall_style']['profile'].get('grok_branding'):
   from activate_street_city_grok_brands import verify as verify_grok
   from street_city_grok_brand_materials import OUT as GROK
   expected=verify_grok(bid);expected['baker_sha256']=sha(ROOT/'Tools/street_city_grok_brand_materials.py');expected['placement_sha256']=sha(ROOT/'Tools/street_city_grok_brand_placement.py')
   assert r['wall_style']['profile']['grok_branding']==expected,bid+' stale Grok evidence'
   for kind in ('logo','material'):
    folder_grok=GROK/'assets'/bid/kind
    for name in ['concept.jpg','brief.json','generation.json','grok-prompt.txt']:pin(folder_grok/name)
    pin(expected['assets'][kind]['grok_original_image_file']);pin(expected['assets'][kind]['tool_event_log'])
   for name in ['profile.json','generation.json','grok-provenance.json']:pin(Path(r['wall_style']['texture_directory'])/name)
  checks.append(dict(building=bid,status='passed',family=r['wall_style']['profile']['family'],geometry_unchanged=True,model_sha256=a['sha256'],actual_checks=a['checks']))
 assert len(checks)==96 and publish()==96
 profiles=[json.loads((ROOT/f'output/street-social-city-production/{q["building"]}-modular-components-v1/assembly-building/assembly-report.json').read_text('utf8'))['wall_style'] for q in checks]
 assert len({q['profile_sha256'] for q in profiles})==96
 assert len({q['files']['feature-basecolor.png'] for q in profiles})==96
 direct=ROOT/'output/street-social-city-production/shop-street-v1/direct';view=json.loads((direct/'review-cache.json').read_text('utf8'))
 assert view['version']==dict(model_sha256=sha(direct/'shop-street-complete.glb'),renderer_sha256=sha(ROOT/'Tools/review_street_city_direct_merge.py'),frame_helper_sha256=sha(ROOT/'Tools/street_city_scene_review.py'))
 assert len(view['blend_sources'])==96
 for source in view['blend_sources']:assert sha(source['path'])==source['sha256'];pin(source['path'])
 for name,digest in view['files'].items():assert sha(direct/name)==digest;pin(direct/name)
 for name in ['review-cache.json','shop-street-complete.glb','world-acceptance.json','world-assembly.json']:pin(direct/name)
 for name in ['review_street_city_direct_merge.py','street_city_scene_review.py']:pin(ROOT/'Tools'/name)
 native=ROOT/'output/street-social-city-production/world-ground-v1';bindings=json.loads((native/'unreal-shop-style-bindings.json').read_text('utf8'));assert bindings['status']=='passed' and len(bindings['actors'])==288 and bindings['importer_sha256']==sha(ROOT/'Tools/import_street_city_unreal_shops.py')
 for binding in bindings['actors']:
  bid,number=binding['actor'].split('_F');source=ROOT/f'output/street-social-city-production/{bid}-modular-components-v1/assembly-building/{bid}-F{int(number):02d}-complete-2k.glb';assert sha(source)==binding['source_sha256'];pin(source)
 for name in ['unreal-shop-style-bindings.json','unreal-shops-import.json']:pin(native/name)
 for name in ['import_street_city_unreal_shops.py','street_city_unreal_import.py','audit_street_city_unreal_interiors.py']:pin(ROOT/'Tools'/name)
 fixture=verify_fixture_audit();pin(fixture)
 for name in ['mall-v1/delivery-acceptance.json','campus-v1/delivery-acceptance.json']:
  path=ROOT/'output/street-social-city-production'/name;r=json.loads(path.read_text('utf8'));assert r['status']=='passed';pin(path)
 for name in ['index.html','RESEARCH.md','inventory.json','style-contact-sheet.jpg']:pin(OUT/name)
 for name in ['street_city_shop_wall_styles.py','restyle_street_city_shop.py','run_street_city_shop_wall_styles.py','publish_street_city_shop_wall_styles.py','assemble_street_city_building.py','assemble_street_city_large_shop.py','accept_street_city_building.py','accept_street_city_large_shop.py','street_city_programme_gate.py','run_street_city_shop_street.py']:pin(ROOT/'Tools'/name)
 grok_root=ROOT/'output/street-social-city-production/shop-brand-grok-v2'
 if (grok_root/'activation.json').exists():
  assert len(json.loads((grok_root/'activation.json').read_text('utf8'))['buildings'])==96
  pin(grok_root/'activation.json')
  for name in ['street_city_grok_brand_materials.py','street_city_grok_brand_placement.py','activate_street_city_grok_brands.py','generate_street_city_grok_brands.py','run_street_city_grok_brand_models.py','prepare_street_city_unreal_shop_assets.py','run_street_city_unreal.lua','run_street_city_unreal_task.py']:pin(ROOT/'Tools'/name)
 result=dict(status='passed',completed_at=datetime.datetime.now().astimezone().isoformat(),checks=len(checks),issues=[],building_count=96,floor_count=288,family_count=len({q['family'] for q in checks}),scope='Actual exported and rendered owner finishes, unchanged certified geometry; native source/collision and shared-city deliveries passed',buildings=checks,files=files)
 (OUT/'acceptance.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),'utf8')
 progress=json.loads((OUT/'progress.json').read_text('utf8'));assert progress['completed']==progress['total']==96 and all(q['status']=='passed' for q in progress['buildings']);progress['status']='passed';(OUT/'progress.json').write_text(json.dumps(progress,ensure_ascii=False,indent=2),'utf8')
 print('ALL 96 SHOP WALL STYLES ACCEPTED',flush=True)

def version():
 names=['import_street_city_unreal_shops.py','street_city_unreal_import.py','audit_street_city_unreal_interiors.py','review_street_city_direct_merge.py','street_city_scene_review.py','street_city_shop_wall_styles.py','restyle_street_city_shop.py','accept_street_city_building.py','accept_street_city_large_shop.py','publish_street_city_shop_wall_styles.py','run_street_city_shop_street.py','street_city_programme_gate.py']
 names+=['street_city_grok_brand_materials.py','street_city_grok_brand_placement.py','prepare_street_city_unreal_shop_assets.py','run_street_city_unreal.lua','run_street_city_unreal_task.py']
 return dict(sources={n:sha(ROOT/'Tools'/n) for n in names},profiles={b['building']:profile_hash(b['building']) for b in inventory()})

def main():
 p=argparse.ArgumentParser();p.add_argument('--ids',nargs='*');p.add_argument('--models-only',action='store_true');a=p.parse_args();ids=a.ids or [b['building'] for b in inventory()];statefile=OUT/'stages.json';OUT.mkdir(exist_ok=True)
 if a.ids or a.models_only:batch(ids);return
 state=json.loads(statefile.read_text('utf8')) if statefile.exists() else dict(status='in_progress',completed=[])
 target=version()
 if state.get('version')!=target:state=dict(status='in_progress',completed=[],version=target)

 def stage(name,fn):
  if name in state['completed']:return
  state.update(status='in_progress',phase=name);statefile.write_text(json.dumps(state,indent=2),'utf8');fn();state['completed'].append(name);statefile.write_text(json.dumps(state,indent=2),'utf8')
 stage('96-styled-models',lambda:batch(ids))
 stage('original-review-pages',lambda:py('publish_street_city_refreshed_reviews.py'))
 stage('gallery',publish)
 from rebuild_street_city_after_remesh import street
 stage('shop-merge-and-native-import',street)
 stage('whole-shared-city-regression',lambda:py('finish_street_city_outdoor.py'))
 stage('mall-current-map-audit',lambda:native('run_street_city_mall_unreal.lua','--audit-only'))
 stage('mall-delivery',lambda:py('verify_street_city_mall_delivery.py'))
 stage('wall-style-delivery',delivery)
 from rebuild_street_city_after_remesh import delivery as remesh_delivery
 stage('current-remesh-delivery',remesh_delivery)
 state.update(status='passed');statefile.write_text(json.dumps(state,indent=2),'utf8')

if __name__=='__main__':main()
