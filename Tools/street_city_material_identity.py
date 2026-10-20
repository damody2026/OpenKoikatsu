"""Content identity for glTF PBR materials, including embedded image bytes."""
import json,struct,hashlib,re
from pathlib import Path

def material_identities(path):
 data=Path(path).read_bytes();length=struct.unpack_from('<I',data,12)[0];doc=json.loads(data[20:20+length]);binary=data[28+length:]
 def texture_identity(index):
  texture=doc['textures'][index];image=doc['images'][texture['source']];assert 'bufferView' in image,'Embedded images required'
  view=doc['bufferViews'][image['bufferView']];start=view.get('byteOffset',0);raw=binary[start:start+view['byteLength']]
  return dict(image_sha256=hashlib.sha256(raw).hexdigest(),sampler=doc.get('samplers',[])[texture['sampler']] if 'sampler' in texture else {})
 def canonical(value,key=''):
  if isinstance(value,dict):
   result={k:canonical(v,k) for k,v in value.items() if k not in ['name','extras']}
   if key.endswith('Texture') and 'index' in result:result['index']=texture_identity(result['index'])
   return result
  if isinstance(value,list):return [canonical(v) for v in value]
  return value
 result={}
 for index,material in enumerate(doc.get('materials',[])):
  name=re.sub(r'[^A-Za-z0-9_]', '_',material.get('name',f'Material_{index}'));identity=hashlib.sha256(json.dumps(canonical(material),sort_keys=True,separators=(',',':')).encode()).hexdigest()
  assert name not in result or result[name]==identity,('Ambiguous glTF material name',path,name)
  result[name]=identity
 return result
