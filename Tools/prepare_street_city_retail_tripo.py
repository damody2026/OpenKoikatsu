"""Consume downloaded files only; never regenerate a missing browser event."""
import argparse,json,hashlib
from pathlib import Path
from run_street_city_shop_street import run,ROOT
from street_city_retail_profiles import COMMON
p=argparse.ArgumentParser();p.add_argument('--ids',nargs='+',required=True);a=p.parse_args()
catalog=json.loads((COMMON/'components.json').read_text('utf-8'))
for aid in a.ids:
 entry=next(q for q in catalog['components'] if q['id']==aid);source=Path('C:/Users/Damody/Downloads')/f'street-{aid}-multiview-source.glb'
 if aid=='croissant':source=source.with_name('street-croissant-multiview-2500-source.glb')
 aliases={'toilet-lod':'S01-toilet-multiview-15000-source.glb','sink-lod':'S01-sink-multiview-10000-source.glb','bicycle-lod':'street-bicycle-multiview-source.glb'}
 if aid in aliases:source=source.with_name(aliases[aid])
 if aid.startswith('toilet-lod'):source=source.with_name(aliases['toilet-lod'])
 if aid.startswith('sink-lod'):source=source.with_name(aliases['sink-lod'])
 assert source.exists() and source.stat().st_size>100 and source.read_bytes()[:4]==b'glTF',source
 out=COMMON/aid
 run('Tools/prepare_street_city_generated_component.py',out,'--id',aid,'--source',source,'--catalog',COMMON/'components.json','--output',out,'--lod-triangles',entry['triangle_cap'],'--yaw-deg',entry.get('yaw_deg',0))
 run('Tools/remesh_street_city_asset.py',out,'--source',out/'normalized-source.glb','--output',out/'remesh','--triangle-cap',entry['triangle_cap'],'--quad-mode','--project-source','--dense-pass',3)
 r=json.loads((out/'remesh/remesh-report.json').read_text('utf-8'))
 if r['status']!='passed':
  # Explicit documented topology reconstruction; raw external output retained.
  run('Tools/remesh_street_city_asset.py',out,'--source',out/'normalized-source.glb','--output',out/'remesh','--triangle-cap',entry['triangle_cap'],'--quad-mode','--project-source','--dense-pass',3,'--recover-source-topology')
  r=json.loads((out/'remesh/remesh-report.json').read_text('utf-8'))
 assert r['status']=='passed',(aid,'remesh rejected')
 (out/'generation.json').write_text(json.dumps(dict(asset=aid,provider='Tripo Model',reference_provider='GPT imagegen',views=['front','left'],source=str(source),sha256=hashlib.sha256(source.read_bytes()).hexdigest(),remesh_triangles=r['triangles'],job=entry.get('job'),texture='2K PBR'),indent=2),'utf-8')
 print(aid,r['triangles'],flush=True)
