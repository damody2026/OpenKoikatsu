"""Bounded asset-only imports; no world editing and no claim of complete delivery."""
import sys,json,re
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_unreal_import import *
from street_city_shop_wall_styles import verify_metadata
command=unreal.SystemLibrary.get_command_line()
start=int(re.search(r'-CityShopStart=(\d+)',command)[1]);end=int(re.search(r'-CityShopEnd=(\d+)',command)[1])
assert 1<=start<=end<=96 and end-start<12
source_sha=sha(Path(__file__));rows=[]
def local_inputs():
 # Asset preparation can overlap unrelated buildings. Actual world placement
 # still requires all 288 sources to pass the complete city audit afterwards.
 for n in range(start,end+1):
  bid=f'S{n:02d}';folder=ROOT/f'output/street-social-city-production/{bid}-modular-components-v1/assembly-building'
  report=json.loads((folder/'assembly-report.json').read_text('utf8'));accepted=json.loads((folder/'acceptance.json').read_text('utf8'))
  assert accepted['status']=='passed' and report['approved'] and sha(folder/f'{bid}-complete-2k.glb')==accepted['sha256'];verify_metadata(bid,report)
  for floor in (1,2,3):yield dict(id=f'{bid}_F{floor:02d}',building=bid,floor=floor,source=folder/f'{bid}-F{floor:02d}-complete-2k.glb')
for item in local_inputs():
 mesh=import_mesh(item['source'],'/Game/UniversityCity/Shops/'+item['building'],item['id'])
 assert unreal.EditorAssetLibrary.get_metadata_tag(mesh,'StreetCitySourceSHA256')==sha(item['source'])
 ensure_mesh_materials(mesh)
 rows.append(dict(id=item['id'],source_sha256=sha(item['source']),asset=mesh.get_path_name()))
 unreal.log('Prepared asset '+item['id'])
 if item['floor']==3:mesh=None;unreal.SystemLibrary.collect_garbage()
assert len(rows)==(end-start+1)*3 and source_sha==sha(Path(__file__))
result=dict(status='passed',scope='asset-only batch; world material binding and collision not yet verified',source_sha256=source_sha,start=start,end=end,floors=rows)
for name in ['unreal-shops-prepare.json',f'unreal-shops-prepare-{start:02d}-{end:02d}.json']:(OUT/name).write_text(json.dumps(result,indent=2),'utf8')
