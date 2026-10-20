"""Bind native interior audit evidence to the exact saved map and mesh assets."""
import hashlib,json
from pathlib import Path

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def capture(root):
 root=Path(root);out=root/'output/street-social-city-production';project=root/'StreetSocialCity'
 imports=out/'world-ground-v1/unreal-shops-import.json'
 data=json.loads(imports.read_text('utf-8-sig'))
 assert data['status']=='passed' and len(data['floors'])==288
 paths=[project/'Content/UniversityCity/Maps/UniversityCity.umap',imports,out/'shop-street-v1/world-assembly.json']
 for floor in data['floors']:
  asset=floor['asset'].split('.')[0]
  assert asset.startswith('/Game/UniversityCity/Shops/'),asset
  paths.append(project/'Content'/Path(asset[len('/Game/'):]+'.uasset'))
 return [dict(path=str(p.resolve()),sha256=sha(p)) for p in dict.fromkeys(paths)]

def verify(root,snapshot):
 assert snapshot,'Native audit has no saved-asset snapshot; rerun InteriorAudit'
 current=capture(root)
 assert current==snapshot,'Saved Unreal map, mesh assets or import records changed after interior audit'
 return len(current)
