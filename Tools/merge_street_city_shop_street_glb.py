"""Lossless glTF table merge: preserve node extras, transforms and animations.

Avoid Blender's repeated whole-scene dependency updates while importing 96 files.
Reject unsupported source schemas rather than silently dropping their features.
"""
import json,struct,hashlib,math,copy
from pathlib import Path
from street_city_shop_street import inventory,ROOT,OUT
from street_city_remesh_gate import verify,sha
OUT=OUT/'direct';OUT.mkdir(exist_ok=True)
g={k:[] for k in ('nodes','meshes','materials','textures','images','samplers','accessors','bufferViews')}
g.update(asset={'version':'2.0','generator':'StreetSocial lossless verified-source merge'},scene=0,scenes=[{'nodes':[]}])
binary=bytearray();pools={k:{} for k in g if isinstance(g[k],list)};channels=[];anims=[];extensions=set();records=[]
def intern(table,value,key=None):
 key=key or json.dumps(value,sort_keys=True,separators=(',',':'))
 if key in pools[table]:return pools[table][key]
 index=len(g[table]);g[table].append(value);pools[table][key]=index;return index
def read(path):
 raw=path.read_bytes();magic,version,total=struct.unpack_from('<III',raw);assert magic==0x46546c67 and version==2 and total==len(raw)
 n,kind=struct.unpack_from('<II',raw,12);assert kind==0x4e4f534a
 doc=json.loads(raw[20:20+n]);length,kind=struct.unpack_from('<II',raw,20+n);assert kind==0x004e4942
 return doc,raw[28+n:28+n+length]
def texture_refs(value,mapping):
 if isinstance(value,dict):
  for key,child in value.items():
   if key.endswith('Texture') and isinstance(child,dict) and 'index' in child:child['index']=mapping[child['index']]
   else:texture_refs(child,mapping)
 elif isinstance(value,list):
  for child in value:texture_refs(child,mapping)
for b in inventory():
 bid=b['building'];folder=ROOT/f'output/street-social-city-production/{bid}-modular-components-v1/assembly-building';path=folder/f'{bid}-complete-2k.glb';a=json.loads((folder/'acceptance.json').read_text('utf-8'));r=json.loads((folder/'assembly-report.json').read_text('utf-8'))
 assert a['status']=='passed' and r.get('approved') and a['sha256']==sha(path)
 for s in r['sources'].values():verify(Path(s['remesh_report']).parent,s['sha256'])
 doc,raw=read(path);assert not(set(doc)-{'asset','extensionsUsed','scene','scenes','nodes','animations','materials','meshes','textures','images','accessors','bufferViews','samplers','buffers'})
 assert len(doc['buffers'])==1 and 'uri' not in doc['buffers'][0]
 extensions.update(doc.get('extensionsUsed',[]));assert extensions<={'KHR_materials_emissive_strength','KHR_materials_transmission'}
 maps={}
 for table in ('bufferViews','accessors','samplers','images','textures','materials','meshes'):
  mapping=[]
  for original in doc.get(table,[]):
   x=copy.deepcopy(original);x.pop('name',None)
   if table=='bufferViews':
    assert not(set(x)-{'buffer','byteOffset','byteLength','target'}) and x['buffer']==0
    data=raw[x.get('byteOffset',0):x.get('byteOffset',0)+x['byteLength']];assert len(data)==x['byteLength'];key=(hashlib.sha256(data).hexdigest(),x.get('target'))
    if key not in pools[table]:
     binary.extend(b'\0'*((-len(binary))%4));x['byteOffset']=len(binary);binary.extend(data);x['buffer']=0
    index=intern(table,x,key)
   else:
    if table=='accessors':
     assert not(set(x)-{'bufferView','componentType','count','max','min','type'});x['bufferView']=maps['bufferViews'][x['bufferView']]
    elif table=='images':
     assert set(x)=={'bufferView','mimeType'};x['bufferView']=maps['bufferViews'][x['bufferView']]
    elif table=='textures':
     assert not(set(x)-{'sampler','source'});x['source']=maps['images'][x['source']]
     if 'sampler' in x:x['sampler']=maps['samplers'][x['sampler']]
    elif table=='materials':texture_refs(x,maps['textures'])
    elif table=='meshes':
     assert set(x)=={'primitives'}
     for p in x['primitives']:
      assert not(set(p)-{'attributes','indices','material','mode'})
      p['attributes']={k:maps['accessors'][v] for k,v in p['attributes'].items()}
      if 'indices' in p:p['indices']=maps['accessors'][p['indices']]
      if 'material' in p:p['material']=maps['materials'][p['material']]
    index=intern(table,x)
   mapping.append(index)
  maps[table]=mapping
 offset=len(g['nodes'])
 for original in doc['nodes']:
  x=copy.deepcopy(original);assert not(set(x)-{'children','extras','mesh','name','rotation','scale','translation'})
  if 'children' in x:x['children']=[offset+i for i in x['children']]
  if 'mesh' in x:x['mesh']=maps['meshes'][x['mesh']]
  g['nodes'].append(x)
 yaw=r.get('world_placement',{}).get('rotation_z_deg',0);angle=math.radians(yaw)/2;px,py,pz=b['world_position_m']
 root=len(g['nodes']);g['nodes'].append(dict(name=bid+'-world',children=[offset+i for i in doc['scenes'][doc.get('scene',0)]['nodes']],translation=[px,pz,-py],rotation=[0,math.sin(angle),0,math.cos(angle)],extras=dict(building=bid,source_sha256=a['sha256'],footprint_m=b['footprint_m'],world_yaw_deg=yaw)));g['scenes'][0]['nodes'].append(root)
 for animation in doc.get('animations',[]):
  ao=len(anims)
  for sampler in animation['samplers']:
   x=copy.deepcopy(sampler);x['input']=maps['accessors'][x['input']];x['output']=maps['accessors'][x['output']];anims.append(x)
  for channel in animation['channels']:
   x=copy.deepcopy(channel);x['sampler']+=ao;x['target']['node']+=offset;channels.append(x)
 records.append(dict(building=bid,source=str(path),sha256=a['sha256'],position_m=b['world_position_m'],rotation_z_deg=yaw,footprint_m=b['footprint_m'],height_m=r['height_m'],floor_triangles=a['floor_triangles']))
 print(bid,'merged',len(g['meshes']),'unique meshes',flush=True)
assert len(records)==96
g['animations']=[dict(name='Scene',channels=channels,samplers=anims)];g['extensionsUsed']=sorted(extensions);g['buffers']=[dict(byteLength=len(binary))]
encoded=json.dumps(g,separators=(',',':')).encode();encoded+=b' '*((-len(encoded))%4);binary.extend(b'\0'*((-len(binary))%4));total=28+len(encoded)+len(binary);path=OUT/'shop-street-complete.glb'
with path.open('wb') as f:f.write(struct.pack('<III',0x46546c67,2,total));f.write(struct.pack('<II',len(encoded),0x4e4f534a));f.write(encoded);f.write(struct.pack('<II',len(binary),0x004e4942));f.write(binary)
report=dict(status='assembled_requires_world_acceptance',buildings=records,building_count=96,unique_meshes=len(g['meshes']),unique_materials=len(g['materials']),unique_images=len(g['images']),glb_sha256=sha(path),scope='Lossless table merge of approved remeshed buildings; roads, terrain and game controllers separate')
(OUT/'world-assembly.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),'utf-8');print('Merged bytes',total)
