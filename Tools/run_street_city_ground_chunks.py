"""Run Rust for every geometry version before terrain placement or Unreal import."""
import json,sys,argparse
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from run_street_city_shop_street import run,REVIEW
from street_city_remesh_gate import verify,sha
args=argparse.ArgumentParser();args.add_argument('--smoke',action='store_true');a=args.parse_args();OUT=REVIEW.parent/'world-ground-v1';rows=json.loads((OUT/('chunk-sources-smoke.json' if a.smoke else 'chunk-sources.json')).read_text('utf-8'))['chunks'];results=[];report=OUT/('chunk-remesh-smoke.json' if a.smoke else 'chunk-remesh.json')
for row in rows:
 folder=Path(row['source']).parent
 for layer,source,cap in [('ground',row['source'],100000),('water',row.get('water_source'),50000)]:
  if not source:continue
  output=folder/('remesh' if layer=='ground' else 'water-remesh')
  try:receipt=verify(output)
  except (AssertionError,OSError,ValueError,KeyError):
   run('Tools/remesh_street_city_asset.py',output,'--source',source,'--output',output,'--triangle-cap',cap,'--recover-source-topology','--pin-source-extrema','x','y')
   receipt=verify(output)
  results.append(dict(chunk=row['id'],layer=layer,**receipt));print(row['id'],layer,'remesh passed',flush=True)
 report.write_text(json.dumps(dict(status='remeshing',assets=results),indent=2),'utf-8')
assert len(rows)==(1 if a.smoke else 16)
report.write_text(json.dumps(dict(status='passed_smoke_only' if a.smoke else 'passed',assets=results),indent=2),'utf-8')
