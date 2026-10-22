import subprocess,os,time,json,re,argparse
from pathlib import Path
from street_city_world_wall_art import ROOT,OUT
def main():
 p=argparse.ArgumentParser();p.add_argument('--rhi',choices=['d3d11','d3d12'],default='d3d12');a=p.parse_args()
 engine=json.loads((ROOT/'Tools/engine.local.json').read_text('utf8'))['engine'];start=time.time();log=OUT/'native-render-editor.log'
 cmd=[str(Path(engine)/'Engine/Binaries/Win64/UnrealEditor.exe'),str(ROOT/'StreetSocialCity/StreetSocialCity.uproject'),'/Engine/Maps/Entry','-ExecutePythonScript='+str(ROOT/'Tools/run_street_city_unreal_deferred.py'),'-UserDir=E:/StreetSocialCityProduction/wall-render-user','-unattended','-RenderOffscreen','-NoAsyncLoadingThread','-NoAsyncPostLoad','-nosplash','-nosound','-nop4','-abslog='+str(log)]
 cmd.append('-'+a.rhi)
 env=dict(os.environ,TEMP='E:/StreetSocialCityProduction/unreal-scratch',TMP='E:/StreetSocialCityProduction/unreal-scratch',STREET_CITY_CAPTURE_RHI=a.rhi,STREET_CITY_DEFERRED_SCRIPT=str(ROOT/'Tools/render_street_city_world_walls_unreal.py'));env['UE-LocalDataCachePath']='E:/StreetSocialCityProduction/unreal-ddc'
 with (OUT/'native-render-process.log').open('w',encoding='utf8') as output:process=subprocess.run(cmd,cwd=ROOT,env=env,stdout=output,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
 (OUT/'native-render-process-result.json').write_text(json.dumps(dict(returncode=process.returncode,started_at=start,command=cmd),indent=2),'utf8')
 receipt=OUT/'native-render.json'
 if process.returncode!=0:
  if receipt.exists() and receipt.stat().st_mtime>=start:
   r=json.loads(receipt.read_text('utf8'));r.update(status='failed_process_exit',process_returncode=process.returncode);receipt.write_text(json.dumps(r,indent=2),'utf8')
  raise RuntimeError(('Real RHI process failed',process.returncode,str(log)))
 assert receipt.exists() and receipt.stat().st_mtime>=start,'No new real-RHI image receipt';r=json.loads(receipt.read_text('utf8'));assert r['status']=='captured_real_rhi_images'
 errors=[line for line in log.read_text('utf8',errors='replace').splitlines() if re.search(r'Failed to compile Material|LogShaderCompilers: Error|\[SM[56]\].*error',line,re.I)];assert not errors,errors[:8]
 from street_city_world_wall_art import sha
 r.update(status='passed_real_rhi_captures',process_returncode=0,rhi=a.rhi,renderer_sha256=sha(ROOT/'Tools/render_street_city_world_walls_unreal.py'));receipt.write_text(json.dumps(r,indent=2),'utf8')
 print('Real RHI wall rendering passed',flush=True)
if __name__=='__main__':main()
