"""Idempotent build/remesh/audit/scene integration for outdoor campus facilities."""
import sys,json,os,hashlib,subprocess
from pathlib import Path
from street_city_outdoor_contract import ROOT,OUT
from street_city_remesh_gate import verify,sha
from run_street_city_shop_street import run
os.environ['STREET_CITY_BLENDER_THREADS']='4'
os.environ['TEMP']=str(OUT.parent/'scratch');os.environ['TMP']=os.environ['TEMP']
run('Tools/build_street_city_outdoor.py',OUT)
sources=json.loads((OUT/'sources.json').read_text('utf8'))
for row in sources['facilities']:
 if not row.get('track_source'):continue
 folder=OUT/row['id']/'track-remesh'
 try:r=verify(folder);assert json.loads((folder/'remesh-report.json').read_text('utf8')) # gate also validates source hash
 except (OSError,AssertionError,KeyError,ValueError):
  run('Tools/remesh_street_city_asset.py',folder,'--source',row['track_source'],'--output',folder,'--triangle-cap',40000,'--quad-mode','--dense-pass',.25,'--recover-source-topology','--pin-source-extrema','x','y','z')
  r=verify(folder)
 print(row['id'],'Rust accepted',flush=True)
run('Tools/seal_street_city_outdoor.py',OUT)
run('Tools/accept_street_city_outdoor.py',OUT)
run('Tools/assemble_street_city_outdoor.py',OUT)
subprocess.run([sys.executable,str(ROOT/'Tools/publish_street_city_outdoor.py')],check=True)
