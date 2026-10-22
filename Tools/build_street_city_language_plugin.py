"""Build the runtime plugin for Editor/Development/Shipping and retain exact compilation inputs."""
import datetime,json,os,shutil,subprocess
from pathlib import Path
from street_city_scene_inventory import ROOT,OUT,BASE,sha,read
PLUGIN=ROOT/'StreetSocialCity/Plugins/CityLocalization'
def record(package):
 package=Path(package);sources={}
 for source in (PLUGIN/'Source').rglob('*'):
  if source.is_file():
   digest=sha(source);assert sha(package/source.relative_to(PLUGIN))==digest
   sources[str(source.relative_to(ROOT))]=digest
 log=BASE/'localization-v1/runtime-plugin-build.log';assert 'BUILD SUCCESSFUL' in log.read_text('utf8')
 binaries={str(p.relative_to(ROOT)):sha(p) for p in (PLUGIN/'Binaries').rglob('*') if p.is_file() and p.suffix in ['.dll','.modules']}
 for path,digest in binaries.items():assert sha(package/(ROOT/path).relative_to(PLUGIN))==digest
 result=dict(status='passed',package=str(package),source_hashes=sources,binaries=binaries,build_log_sha256=sha(log),targets=['UnrealEditor Win64 Development','UnrealGame Win64 Development','UnrealGame Win64 Shipping'])
 OUT.mkdir(exist_ok=True);(OUT/'plugin-build.json').write_text(json.dumps(result,indent=2),'utf8');return result

def main():
 engine=os.environ.get('OK_ENGINE_ROOT') or read(ROOT/'Tools/engine.local.json')['engine']
 package=Path('E:/StreetSocialCityProduction/plugin-builds')/('CityLocalization-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
 assert not package.exists();log=BASE/'localization-v1/runtime-plugin-build.log'
 with log.open('w',encoding='utf8') as f:
  subprocess.run([str(Path(engine)/'Engine/Build/BatchFiles/RunUAT.bat'),'BuildPlugin','-Plugin='+str(PLUGIN/'CityLocalization.uplugin'),'-Package='+str(package),'-TargetPlatforms=Win64','-NoP4','-UTF8Output'],cwd=ROOT,stdout=f,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW,check=True)
 for folder in ['Binaries','Intermediate']:shutil.copytree(package/folder,PLUGIN/folder,dirs_exist_ok=True)
 record(package);print('Runtime plugin built and exact-source receipt recorded',flush=True)
if __name__=='__main__':main()
