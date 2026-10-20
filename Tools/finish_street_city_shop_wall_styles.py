"""Resume final delivery from already accepted owner finishes and street geometry."""
import datetime,json,subprocess,sys,time
from pathlib import Path
from run_street_city_shop_wall_styles import ROOT,OUT,delivery,version,sha

BASE=ROOT/'output/street-social-city-production'
STATUS=OUT/'completion.json'

def stage(name,command):
 STATUS.write_text(json.dumps(dict(status='running',phase=name),indent=2),'utf8')
 print('START',name,flush=True)
 subprocess.run(command,cwd=ROOT,check=True)

def main():
 native=BASE/'world-ground-v1'
 while True:
  task=json.loads((native/'unreal-importshops-task.json').read_text('utf-8-sig'))
  if task['status']=='failed':raise RuntimeError(task)
  if task['status']=='succeeded':
   p=native/'unreal-importshops-process.json'
   proc=json.loads(p.read_text('utf-8-sig')) if p.exists() else {}
   if p.exists() and p.stat().st_mtime_ns>=(native/'unreal-importshops-task.json').stat().st_mtime_ns:
    assert proc.get('exitCode')==0,proc
    break
  time.sleep(10)
 bindings=json.loads((native/'unreal-shop-style-bindings.json').read_text('utf8'))
 assert bindings['status']=='passed' and len(bindings['actors'])==288
 assert bindings['importer_sha256']==sha(ROOT/'Tools/import_street_city_unreal_shops.py')
 stage('whole-shared-city-regression',[sys.executable,'Tools/finish_street_city_outdoor.py'])
 stage('mall-current-map-audit',[str(ROOT/'Tools/lua/lua.exe'),'Tools/run_street_city_mall_unreal.lua','--audit-only'])
 stage('mall-delivery',[sys.executable,'Tools/verify_street_city_mall_delivery.py'])
 delivery()
 from rebuild_street_city_after_remesh import delivery as remesh_delivery,OUT as REMESH
 remesh_delivery()
 for p in [OUT/'acceptance.json',BASE/'mall-v1/delivery-acceptance.json',REMESH/'acceptance.json']:
  r=json.loads(p.read_text('utf8'));assert r['status']=='passed' and not r['issues']
  for path,digest in r['files'].items():assert sha(Path(path))==digest,('Stale delivery',str(p),path)
 names=['96-styled-models','original-review-pages','gallery','shop-merge-and-native-import','whole-shared-city-regression','mall-current-map-audit','mall-delivery','wall-style-delivery','current-remesh-delivery']
 final=dict(status='passed',completed=names,phase=names[-1],version=version(),completed_at=datetime.datetime.now().astimezone().isoformat())
 (OUT/'stages.json').write_text(json.dumps(final,indent=2),'utf8')
 STATUS.write_text(json.dumps(dict(status='passed',phase='all-current-file-hashes-verified'),indent=2),'utf8')
 print('ALL WALL FINISHES AND CURRENT SHARED CITY VERIFIED',flush=True)

if __name__=='__main__':
 try:main()
 except Exception:
  import traceback
  STATUS.write_text(json.dumps(dict(status='failed',error=traceback.format_exc()),indent=2),'utf8')
  raise
