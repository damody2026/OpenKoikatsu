"""Current adopted scene sources; prototypes and isolated duplicate components are excluded."""
import hashlib,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'output/street-social-city-production/scene-integration-v1'
BASE=ROOT/'output/street-social-city-production'
def sha(path):
 h=hashlib.sha256()
 with Path(path).open('rb') as f:
  for chunk in iter(lambda:f.read(8*1024*1024),b''):h.update(chunk)
 return h.hexdigest()
def read(path):return json.loads(Path(path).read_text('utf8'))
def build():
 OUT.mkdir(exist_ok=True)
 rows=[]
 for rel,key,kind,prefix in [('world-ground-v1/unreal-shops-import.json','floors','shop',''),('world-ground-v1/unreal-campus-import.json','floors','campus',''),('mall-v1/unreal-acceptance.json','imports','mall',''),('campus-v1/outdoor/unreal-acceptance.json','imports','outdoor','Outdoor_')]:
  receipt=read(BASE/rel);assert receipt['status']=='passed'
  for item in receipt[key]:
   source=Path(item['source']);label=item.get('id',item.get('label'))
   if kind=='mall':label='Mall_M1_F'+str(item.get('floor',len([r for r in rows if r['kind']=='mall'])+1)).zfill(2)
   elif kind=='outdoor':label=prefix+label.replace('-','_')
   position=item.get('position_cm')
   if kind=='mall':
    x,y,z=read(BASE/'mall-v1/geometry.json')['contract']['world_position_m'];position=[y*100,x*100,z*100]
   if position is None:
    position=[item['position_m'][1]*100,item['position_m'][0]*100,item['position_m'][2]*100]
   asset=item['asset'];folder=asset.split('/Source_')[0] if '/Source_' in asset else asset.rsplit('/',1)[0]
   rows.append(dict(id=label,kind=kind,source=str(source),source_sha256=sha(source),position_cm=position,yaw_deg=item.get('yaw_deg',90),folder=folder,collision='BlockAll'))
 ground=read(BASE/'world-ground-v1/ground-acceptance.json');assert ground['status']=='passed'
 for chunk in ground['chunks']:
  x,y,_=chunk['world_origin_m']
  for key,water in [('ground',False),('water',True)]:
   if not chunk.get(key):continue
   item=chunk[key];assert sha(item['path'])==item['sha256']
   rows.append(dict(id=chunk['id']+('_Water' if water else ''),kind='water' if water else 'ground',source=item['path'],source_sha256=item['sha256'],position_cm=[y*100,x*100,0],yaw_deg=90,folder='/Game/UniversityCity/'+('Water' if water else 'Ground'),collision='NoCollision' if water else 'BlockAll'))
 extra=BASE/'world-completion-v1/adopted-sources.json'
 if extra.exists():
  adopted=read(extra);assert adopted['status']=='passed'
  overrides={q['id']:q for q in adopted.get('overrides',[])}
  assert set(overrides)<={q['id'] for q in rows},'Overrides must match existing actor identities'
  rows=[dict(overrides[q['id']]) if q['id'] in overrides else q for q in rows]
  rows.extend(dict(q) for q in adopted['rows'])
  for q in [*adopted['rows'],*overrides.values()]:assert sha(q['source'])==q['source_sha256'],q['id']+' changed after adoption'
 from street_city_functional_revision import apply_sources
 rows=apply_sources(rows)
 from street_city_scenery_adoption import apply as apply_scenery
 rows=apply_scenery(rows)
 assert len({r['id'] for r in rows})==len(rows)
 result=dict(status='current_sources',map='/Game/UniversityCity/Maps/UniversityCity',rows=rows,counts={k:sum(r['kind']==k for r in rows) for k in sorted({r['kind'] for r in rows})})
 (OUT/'inventory.json').write_text(json.dumps(result,indent=2),'utf8');print(result['counts'],flush=True)
 return result
if __name__=='__main__':build()
