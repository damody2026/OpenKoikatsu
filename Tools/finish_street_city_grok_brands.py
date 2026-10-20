"""Complete shared-city delivery only after all genuine Grok brands are adopted."""
import json,subprocess,sys,time,datetime,os
from pathlib import Path
from generate_street_city_grok_brands import OUT,ROOT,publish
from activate_street_city_grok_brands import verify
from street_city_shop_wall_styles import sha

def finish():
 path=OUT/'model-progress.json'
 while True:
  if path.exists():
   state=json.loads(path.read_text('utf8'))
   if state.get('status')=='failed':raise RuntimeError(state)
   if state.get('status')=='passed' and state['completed']==96:break
  time.sleep(10)
 assert publish()==96
 # World GLB verification may overlap asset-only preparation. The native map
 # binding stage waits explicitly, preventing two writers on the same assets.
 os.environ['STREET_CITY_GROK_PREWARM']='1'
 subprocess.run([sys.executable,str(ROOT/'Tools/run_street_city_shop_wall_styles.py')],cwd=ROOT,check=True)
 assert publish()==96
 files={};rows=[]
 def pin(path):path=Path(path).resolve();files[str(path)]=sha(path)
 for n in range(1,97):
  bid=f'S{n:02d}';brand=verify(bid);folder=ROOT/f'output/street-social-city-production/{bid}-modular-components-v1/assembly-building'
  model=json.loads((folder/'assembly-report.json').read_text('utf8'));adopted=model['wall_style']['profile']['grok_branding'];assert adopted['assets']==brand['assets'] and model['approved']
  rows.append(dict(building=bid,direction=brand['direction'],logo_sha256=brand['assets']['logo']['image_sha256'],material_sha256=brand['assets']['material']['image_sha256'],model_sha256=sha(folder/f'{bid}-complete-2k.glb')))
  for kind in ('logo','material'):
   for name in ['concept.jpg','generation.json','grok-prompt.txt','brief.json']:pin(OUT/'assets'/bid/kind/name)
   pin(brand['assets'][kind]['grok_original_image_file']);pin(brand['assets'][kind]['tool_event_log'])
 assert len({r['logo_sha256'] for r in rows})==len({r['material_sha256'] for r in rows})==96
 subprocess.run([sys.executable,str(ROOT/'Tools/review_street_city_grok_brands.py')],cwd=ROOT,check=True)
 for name in ['index.html','inventory.json','activation.json','model-progress.json',*[f'contact-{i:02d}.jpg' for i in range(1,9)]]:pin(OUT/name)
 base=ROOT/'output/street-social-city-production'
 for path in [base/'shop-wall-styles-v1/acceptance.json',base/'shop-wall-styles-v1/stages.json',base/'world-ground-v1/unreal-shop-style-bindings.json',ROOT/'model3d.md',ROOT/'Docs/StreetSocialCity/ERRORS_AND_PREVENTION.md']:pin(path)
 for name in ['generate_street_city_grok_brands.py','street_city_grok_brand_materials.py','activate_street_city_grok_brands.py','run_street_city_grok_brand_models.py','finish_street_city_grok_brands.py','review_street_city_grok_brands.py','prewarm_street_city_grok_native.py']:pin(ROOT/'Tools'/name)
 receipt=dict(status='passed',completed_at=datetime.datetime.now().astimezone().isoformat(),buildings=96,floors=288,genuine_grok_images=192,unique_logos=96,unique_materials=96,design_directions=len({r['direction'] for r in rows}),geometry_changed=False,issues=[],buildings_detail=rows,files=files)
 (OUT/'acceptance.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2),'utf8')
 print('ALL 192 GROK IMAGES AND 96 ADOPTED BUILDINGS VERIFIED',flush=True)

if __name__=='__main__':
 try:finish()
 except Exception:
  import traceback
  (OUT/'delivery-failure.txt').write_text(traceback.format_exc(),'utf8');raise
