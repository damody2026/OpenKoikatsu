"""Mandatory Rust remesh of derived retail display LODs."""
import json,os
from street_city_mall_merchandise import OUT
from street_city_remesh_gate import verify,sha
from run_street_city_shop_street import run
os.environ['STREET_CITY_BLENDER_THREADS']='4'
run('Tools/build_street_city_product_lods.py',OUT)
data=json.loads((OUT/'lods/sources.json').read_text('utf8'));results={}
for aid,row in data['products'].items():
 folder=OUT/'lods'/aid;valid=False
 try:r=verify(folder/'remesh');valid=r['source_sha256']==row['normalized_source_sha256']
 except (OSError,KeyError,ValueError,AssertionError):pass
 if not valid:
  args=['--source',folder/'normalized-source.glb','--output',folder/'remesh','--triangle-cap',row['triangle_cap'],'--quad-mode','--project-source','--dense-pass',3,'--threads',4]
  run('Tools/remesh_street_city_asset.py',folder,*args)
  report=json.loads((folder/'remesh/remesh-report.json').read_text('utf8'))
  if report['status']!='passed':run('Tools/remesh_street_city_asset.py',folder,*args,'--recover-source-topology')
  r=verify(folder/'remesh')
 results[aid]=dict(**row,**r);print('LOD remesh',aid,'passed',flush=True)
 (OUT/'lods/acceptance.json').write_text(json.dumps(dict(status='passed' if len(results)==len(data['products']) else 'in_progress',products=results),indent=2),'utf8')
