import subprocess,os,time,re
import sys
from PIL import Image,ImageStat
from pathlib import Path
from street_city_streetscape import ROOT,OUT,read,write,sha
subprocess.run([sys.executable,str(ROOT/'Tools/prepare_street_city_street_views.py')],cwd=ROOT,check=True)
engine=read(ROOT/'Tools/engine.local.json')['engine'];started=time.time();log=OUT/'native-render-editor.log';cmd=[str(Path(engine)/'Engine/Binaries/Win64/UnrealEditor.exe'),str(ROOT/'StreetSocialCity/StreetSocialCity.uproject'),'/Engine/Maps/Entry','-ExecutePythonScript='+str(ROOT/'Tools/run_street_city_unreal_deferred.py'),'-UserDir=E:/StreetSocialCityProduction/street-render-user','-unattended','-RenderOffscreen','-NoAsyncLoadingThread','-NoAsyncPostLoad','-nosplash','-nosound','-nop4','-d3d12','-abslog='+str(log)];env=dict(os.environ,STREET_CITY_DEFERRED_SCRIPT=str(ROOT/'Tools/render_street_city_streetscape_unreal.py'),TEMP='E:/StreetSocialCityProduction/unreal-scratch',TMP='E:/StreetSocialCityProduction/unreal-scratch');env['UE-LocalDataCachePath']='E:/StreetSocialCityProduction/unreal-ddc'
with (OUT/'native-render-process.log').open('w',encoding='utf8') as output:r=subprocess.run(cmd,cwd=ROOT,env=env,stdout=output,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
write(OUT/'native-render-process.json',dict(returncode=r.returncode,started_at=started,command=cmd));assert r.returncode==0,'Actual renderer did not exit cleanly'
path=OUT/'native-render.json';assert path.exists() and path.stat().st_mtime>=started;receipt=read(path);assert receipt['status']=='captured_requires_clean_exit_and_visual_review' and len(receipt['images'])==4
errors=[line for line in log.read_text('utf8',errors='replace').splitlines() if re.search(r'Failed to compile Material|LogShaderCompilers: Error|\[SM[56]\].*error',line,re.I)];assert not errors,errors[:10]
for row in receipt['images']:
 stats=ImageStat.Stat(Image.open(row['image']).convert('RGB'));row['mean_rgb']=stats.mean;row['stddev_rgb']=stats.stddev
 assert sum(stats.mean)/3>12 and max(stats.stddev)>12,('Black or featureless native capture',row)
receipt.update(status='rendered_requires_visual_review',process_returncode=0,camera_plan_sha256=sha(OUT/'camera-plan.json'));write(path,receipt);print('REAL STREET RHI RENDER COMPLETE',flush=True)
