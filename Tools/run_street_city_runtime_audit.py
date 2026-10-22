"""Launch the real editor/PIE audit with isolated settings and a fresh receipt gate."""
import json,subprocess,os,time,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'output/street-social-city-production/scene-integration-v1'
def main():
 config=ROOT/'Tools/engine.local.json';engine=os.environ.get('OK_ENGINE_ROOT') or (json.loads(config.read_text('utf8'))['engine'] if config.exists() else 'E:/PhysX/UE5.8')
 smoke='--smoke' in sys.argv
 script='smoke_street_city_language_runtime.py' if smoke else 'audit_street_city_language_runtime.py'
 OUT.mkdir(exist_ok=True);started=time.time();user=Path('E:/StreetSocialCityProduction/runtime-language-test')
 user.mkdir(exist_ok=True)
 cmd=[str(Path(engine)/'Engine/Binaries/Win64/UnrealEditor.exe'),str(ROOT/'StreetSocialCity/StreetSocialCity.uproject'),'/Engine/Maps/Entry','-ExecutePythonScript='+str(ROOT/'Tools/run_street_city_unreal_deferred.py'),'-UserDir='+str(user),'-unattended','-nosplash','-nosound','-nop4','-NullRHI','-NoAsyncLoadingThread','-NoAsyncPostLoad','-abslog='+str(OUT/('runtime-smoke-editor.log' if smoke else 'runtime-editor.log'))]
 env=dict(os.environ,TEMP='E:/StreetSocialCityProduction/unreal-scratch',TMP='E:/StreetSocialCityProduction/unreal-scratch',STREET_CITY_DEFERRED_SCRIPT=str(ROOT/'Tools'/script));env['UE-LocalDataCachePath']='E:/StreetSocialCityProduction/unreal-ddc'
 with (OUT/'runtime-process.log').open('w',encoding='utf8') as log:
  process=subprocess.run(cmd,cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
 (OUT/'runtime-process-result.json').write_text(json.dumps(dict(returncode=process.returncode,started_at=started,command=cmd),indent=2),'utf8');assert process.returncode==0,('Runtime process failed',process.returncode)
 p=OUT/('runtime-smoke.json' if smoke else 'runtime-acceptance.json');assert p.exists() and p.stat().st_mtime>=started,'No fresh runtime receipt'
 data=json.loads(p.read_text('utf8'));assert data['status']=='passed',data.get('error');data['process_returncode']=0;p.write_text(json.dumps(data,indent=2),'utf8');print('PASSED real PIE runtime API',len(data.get('checks',data.get('rows',[]))),'checks',flush=True)
if __name__=='__main__':main()
