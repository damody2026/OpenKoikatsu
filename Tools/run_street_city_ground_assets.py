"""Resume verified ground prototypes, mandatory Rust remesh and material bakes."""
import sys,json
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from run_street_city_shop_street import run,REVIEW
from street_city_remesh_gate import verify,sha
OUT=REVIEW.parent/'world-ground-v1';ids=['urban','grass','campus','forest','riverbank','sand']
for kind in ids:
 folder=OUT/'assets'/kind
 try:receipt=verify(folder/'remesh')
 except (AssertionError,OSError,ValueError,KeyError):
  run('Tools/remesh_street_city_asset.py',folder/'remesh','--source',folder/'normalized-source.glb','--output',folder/'remesh','--triangle-cap',50000,'--recover-source-topology','--pin-source-extrema','x','y')
  receipt=verify(folder/'remesh')
 print(kind,'remesh passed',flush=True)
for kind in ids:
 folder=OUT/'assets'/kind;surface=folder/'surface.json'
 if surface.exists():
  s=json.loads(surface.read_text('utf-8'))
  if s['source']['sha256']==verify(folder/'remesh')['sha256'] and all(Path(m['path']).exists() and sha(m['path'])==m['sha256'] for m in s['maps'].values()):continue
 run('Tools/prepare_street_city_ground_assets.py',folder/'bake','--stage','bake','--ids',kind)
print('All six ground prototypes remeshed and baked',flush=True)
