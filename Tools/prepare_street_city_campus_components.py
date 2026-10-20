"""Certified analytic primitives and downloaded Tripo Model props, then Rust remesh."""
import os,json,hashlib,shutil
from pathlib import Path
from run_street_city_shop_street import ROOT,run
from street_city_remesh_gate import verify
OUT=ROOT/'output/street-social-city-production/campus-v1/components';OUT.mkdir(parents=True,exist_ok=True)
os.environ['STREET_CITY_BLENDER_THREADS']='4'
for shape,cap in [('panel',60),('cylinder',120),('ring',700)]:
 folder=OUT/('unit-'+shape)
 if not (folder/'remesh/remesh-report.json').exists():
  run('Tools/build_street_city_campus_panel.py',folder,'--shape',shape)
  mode=['--box-panels','--dissolve-planes'] if shape=='panel' else ['--project-source','--recover-source-topology']
  run('Tools/remesh_street_city_asset.py',folder,'--source',folder/'source.glb','--output',folder/'remesh','--triangle-cap',cap,*mode)
 verify(folder/'remesh');print('unit-'+shape,'verified',flush=True)
specs=[('urinal',[.42,.35,.68],5000),('toilet-compact',[.4,.72,.85],1500),('basin-public',[.6,.5,.55],1000),('basin-economy',[.6,.5,.55],500),('microscope',[.25,.35,.45],3000),('exercise-bike',[.7,1.3,1.4],3000),('laundry-appliance',[.7,.75,1],1500)]
catalog=OUT/'tripo-catalog.json';catalog.write_text(json.dumps(dict(components=[dict(id=aid,size=size) for aid,size,cap in specs])),'utf8');records=[]
for aid,size,cap in specs:
 rawname={'toilet-compact':'toilet','basin-public':'basin'}.get(aid,aid);raw=Path('C:/Users/Damody/Downloads')/f'Campus-{rawname}-Tripo-v1.glb';folder=OUT/('tripo-'+aid)
 version=folder/'source-version-v2.json'
 if version.exists():raw=Path(json.loads(version.read_text('utf8'))['raw'])
 cached=OUT/'raw'/raw.name;cached.parent.mkdir(exist_ok=True)
 if raw.is_file() and not cached.exists():shutil.copy2(raw,cached)
 if cached.is_file():raw=cached
 if not raw.is_file():records.append(dict(id=aid,status='pending_download'));continue
 assert raw.read_bytes()[:4]==b'glTF'
 if not (folder/'normalized-source.glb').exists():run('Tools/prepare_street_city_generated_component.py',folder,'--id',aid,'--source',raw,'--catalog',catalog,'--output',folder)
 if not (folder/'remesh/remesh-report.json').exists():run('Tools/remesh_street_city_asset.py',folder,'--source',folder/'normalized-source.glb','--output',folder/'remesh','--triangle-cap',cap,'--project-source','--recover-source-topology')
 receipt=verify(folder/'remesh');records.append(dict(id=aid,status='passed',raw_sha256=hashlib.sha256(raw.read_bytes()).hexdigest(),**receipt));print(aid,'verified',flush=True)
(OUT/'components.json').write_text(json.dumps(dict(status='passed' if all(r['status']=='passed' for r in records) else 'in_progress',components=records),indent=2),'utf8')
