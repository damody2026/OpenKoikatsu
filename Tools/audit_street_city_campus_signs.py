"""Check every entrance, locale image, GLB variant and remesh provenance."""
import json,struct
from pathlib import Path
from PIL import Image
from street_city_localization import LOCALES,sha,text
from build_street_city_campus_signs import DEST
from build_street_city_language_models import read,geometry_digest
from street_city_remesh_gate import verify

def main():
 m=json.loads((DEST/'manifest.json').read_text('utf8'));assert len(m['buildings'])==17
 checked=[];names=set()
 for b in m['buildings']:
  verify(Path(b['remesh']['remesh_report']).parent,b['remesh']['sha256'])
  assert set(b['variants'])==set(LOCALES)
  for l,v in b['variants'].items():
   assert v['text']==text(b['key'],l) and sha(v['path'])==v['sha256']
   with Image.open(v['path']) as im:assert im.size==(2048,420);im.verify()
  doc,raw=read(b['portable']['model']);src,sraw=read(b['portable']['source'])
  assert geometry_digest(doc,raw)==geometry_digest(src,sraw)==b['portable']['geometry_sha256']
  assert sha(b['portable']['model'])==b['portable']['model_sha256']
  assert [v['name'] for v in doc['extensions']['KHR_materials_variants']['variants']]==list(LOCALES)
  prim=doc['meshes'][0]['primitives'][0];assert doc['accessors'][prim['indices']]['count']==36
  pa=doc['accessors'][prim['attributes']['POSITION']];pv=doc['bufferViews'][pa['bufferView']]
  ua=doc['accessors'][prim['attributes']['TEXCOORD_0']];uv=doc['bufferViews'][ua['bufferView']]
  for i in range(pa['count']):
   x,y,z=struct.unpack_from('<fff',raw,pv.get('byteOffset',0)+pa.get('byteOffset',0)+i*pv.get('byteStride',12))
   u,v=struct.unpack_from('<ff',raw,uv.get('byteOffset',0)+ua.get('byteOffset',0)+i*uv.get('byteStride',8))
   assert abs(u-x-.5)<1e-6 and abs(v+y-.5)<1e-6
  assert len(prim['extensions']['KHR_materials_variants']['mappings'])==3
  assert abs(b['size_m'][0]/b['size_m'][2]-2048/420)<1e-6
  for p in b['placements']:
   assert p['id'] not in names;names.add(p['id']);assert p['position_m'][2]>=2.75
  checked.append(dict(building=b['building'],placements=len(b['placements']),locales=list(b['variants'])))
 assert len(names)==34
 report=dict(status='passed',buildings=17,placements=34,textures=51,portable_models=17,checks=checked,scope='asset identity, remesh provenance, upright UV convention, metric proportions, entrance head clearance; native scene separately audited')
 (DEST/'asset-acceptance.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),'utf8');print('CAMPUS SIGNS ASSET AUDIT PASSED')

if __name__=='__main__':main()
