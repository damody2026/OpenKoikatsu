"""Check that the repair gallery, models and saved UE import refer to one version."""
import hashlib,json
from pathlib import Path
from html.parser import HTMLParser
from street_city_unreal_snapshot import verify as verify_native_snapshot
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'output/street-social-city-production/interior-repair-v1'
WORLD=OUT.parent/'world-ground-v1'
checks=0
def check(value,message):
 global checks
 assert value,message
 checks+=1
def read(path):return json.loads(path.read_text('utf-8-sig'))
check(read(OUT/'acceptance.json')['remaining_findings']==0,'Unresolved findings')
delivery=read(OUT/'delivery.json');check(delivery['status']=='passed','Delivery gate')
for item in delivery['files']:
 p=Path(item['path']);check(p.is_file(),str(p));check(p.stat().st_size==item['bytes'],str(p)+' size')
 check(hashlib.sha256(p.read_bytes()).hexdigest()==item['sha256'],str(p)+' SHA')
class Links(HTMLParser):
 def handle_starttag(self,tag,attrs):
  for key,value in attrs:
   if key not in ('href','src') or not value or ':' in value or value.startswith('#'):continue
   check((OUT/value.split('#')[0]).resolve().is_file(),'Broken gallery link '+value)
Links().feed((OUT/'index.html').read_text('utf-8'))
fixtures=read(OUT/'fixtures-expanded-after.json')['buildings']
check(len(fixtures)==96,'Building count')
for b in fixtures:
 check(not b['findings'],b['building']+' fixtures')
 check(all(d['passed'] for d in b['door_checks']),b['building']+' door support')
 folder=OUT.parent/f'{b["building"]}-modular-components-v1/assembly-building'
 check((folder/f'{b["building"]}-complete-2k.blend').is_file(),b['building']+' Blender link')
 for floor in range(1,4):
  p=OUT/f'floor-plans/{b["building"]}-F{floor:02d}.svg'
  check(b['source_sha256'] in p.read_text('utf-8'),str(p)+' stale plan')
  check((folder/f'{b["building"]}-F{floor:02d}-complete-2k.glb').is_file(),str(p)+' floor model link')
unreal=read(WORLD/'unreal-interioraudit.json')
check(unreal['status']=='passed' and not unreal['issues'],'Unreal interior gate')
check(verify_native_snapshot(ROOT,unreal.get('saved_assets'))==291,'Saved map and all 288 mesh assets match native audit')
check(any(x.get('kind')=='clear_aperture' for x in unreal['results']),'Missing initialized opening checks')
imports=read(WORLD/'unreal-shops-import.json');check(imports['status']=='passed' and len(imports['floors'])==288,'UE import count')
for floor in imports['floors']:
 p=Path(floor['source']);check(hashlib.sha256(p.read_bytes()).hexdigest()==floor['source_sha256'],str(p)+' stale UE import')
street=OUT.parent/'shop-street-v1';merged=read(street/'world-acceptance.json')
check(merged['status']=='passed','Merged model acceptance')
check(hashlib.sha256((street/'shop-street-complete.glb').read_bytes()).hexdigest()==merged['glb_sha256'],'Merged model changed after acceptance')
result=dict(status='passed',checks=checks,buildings=96,floors=288,stats=delivery['stats'])
(OUT/'delivery-verification.json').write_text(json.dumps(result,indent=2),'utf-8')
print(json.dumps(result))
