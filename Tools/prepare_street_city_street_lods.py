import os,sys,json,traceback
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor,as_completed
from street_city_streetscape import ROOT,BASE,OUT,read,write,sha
from street_city_remesh_gate import verify
from run_street_city_shop_street import run
os.environ['STREET_CITY_BLENDER_THREADS']='3'
specs=[('camphor',BASE/'world-completion-v1/components/camphor/remesh',[(1,1000,.25),(2,340,.65)]),('cherry',BASE/'world-completion-v1/components/cherry/remesh',[(1,1000,.2),(2,340,.75)]),('bicycle',BASE/'shop-street-components-v1/bicycle-lod/remesh',[(1,3000,.14),(2,1000,.25)]),('flowers',BASE/'shop-street-components-v1/flowers/remesh',[(0,800,.06)])]
specs.append(('bed-flowers',OUT/'assets/bed-flowers/base-remesh',[(0,800,.04)]))
def job(spec):
 aid,source,levels=spec;original=verify(source);results=[]
 if aid in ('camphor','cherry','bicycle'):results.append(dict(level=0,**original,triangles=read(source/'remesh-report.json')['triangles']))
 for level,cap,error in levels:
  folder=OUT/'assets'/aid/f'lod{level}';target=folder/'remesh'
  valid=False
  try:
   r=verify(target);s=read(folder/'lod-source.json');valid=s['source_sha256']==original['sha256'] and s['generator_sha256']==sha(ROOT/'Tools/derive_street_city_street_lod.py')
  except (AssertionError,OSError,ValueError,KeyError):pass
  if not valid:
   run('Tools/derive_street_city_street_lod.py',folder,'--asset',aid,'--source',original['path'],'--level',level,'--cap',cap,'--error',error)
   run('Tools/remesh_street_city_asset.py',folder,'--source',folder/'normalized-source.glb','--output',target,'--triangle-cap',cap,'--quad-mode','--project-source','--dense-pass',2,'--threads',3)
   if read(target/'remesh-report.json')['status']!='passed':
    failed=read(target/'remesh-report.json');write(folder/'rejected-first-remesh.json',failed)
    # Coarse mixed quads can fail extraction inside Rust. A real triangular
    # solve is an alternate supported mode, never a fabricated successful run.
    mode=[] if any(q['status']=='tool_failed' for q in failed['records']) else ['--quad-mode']
    run('Tools/remesh_street_city_asset.py',folder,'--source',folder/'normalized-source.glb','--output',target,'--triangle-cap',cap,*mode,'--project-source','--dense-pass',2,'--threads',3,'--recover-source-topology')
   r=verify(target)
  results.append(dict(level=level,**r,triangles=read(target/'remesh-report.json')['triangles'],lod_source=str(folder/'lod-source.json')))
 return dict(id=aid,lods=results)
rows=[]
with ThreadPoolExecutor(max_workers=2) as pool:
 for f in as_completed([pool.submit(job,s) for s in specs]):
  rows.append(f.result());write(OUT/'lod-assets-progress.json',rows);print('STREET LOD',rows[-1]['id'],[r['triangles'] for r in rows[-1]['lods']],flush=True)
write(OUT/'lod-assets.json',dict(status='passed',assets=rows))
