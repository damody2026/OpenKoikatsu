"""Prepare accepted 12-store asset batches without editing the shared map."""
import json,time,subprocess
from pathlib import Path
from generate_street_city_grok_brands import OUT,ROOT

done=set()
while len(done)<8:
 path=OUT/'model-progress.json'
 try:r=json.loads(path.read_text('utf8'));ready={b['id'] for b in r['buildings'] if b['status']=='passed'}
 except (OSError,ValueError):time.sleep(5);continue
 for group in range(8):
  first=group*12+1;last=first+11
  if group in done or not {f'S{n:02d}' for n in range(first,last+1)}<=ready:continue
  print('PREPARE ACCEPTED ASSETS',first,last,flush=True)
  subprocess.run([str(ROOT/'Tools/lua/lua.exe'),str(ROOT/'Tools/run_street_city_unreal.lua'),'--stage','PrepareShops','--shop-batch-start',str(first),'--shop-batch-end',str(last)],cwd=ROOT,check=True)
  done.add(group);(OUT/'native-prewarm.json').write_text(json.dumps(dict(status='passed_asset_preparation_only' if len(done)==8 else 'in_progress',completed_batches=sorted(done)),indent=2),'utf8')
 time.sleep(5)
print('ALL ASSET BATCHES PREPARED; WORLD BINDING STILL REQUIRES FINAL GATE',flush=True)
