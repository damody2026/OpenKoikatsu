"""Portable KHR_materials_variants; preserve every geometry/animation byte.

Uses the accepted source model unchanged on disk and produces a separate GLB.
Only text-bearing base-color slots vary; PBR, UV, geometry and units are shared.
"""
import copy,concurrent.futures,hashlib,json,re,struct
from pathlib import Path
from street_city_localization import ROOT,OUT,LOCALES,path_for,sha,catalogue

def read(path):
 data=Path(path).read_bytes();n,typ=struct.unpack_from('<II',data,12);assert typ==0x4e4f534a
 doc=json.loads(data[20:20+n]);binary=data[28+n:];return doc,binary

def write(path,doc,binary):
 while len(binary)%4:binary+=b'\0'
 doc['buffers'][0]['byteLength']=len(binary);raw=json.dumps(doc,ensure_ascii=False,separators=(',',':')).encode();raw+=b' '*((-len(raw))%4)
 Path(path).parent.mkdir(parents=True,exist_ok=True);Path(path).write_bytes(struct.pack('<III',0x46546c67,2,28+len(raw)+len(binary))+struct.pack('<II',len(raw),0x4e4f534a)+raw+struct.pack('<II',len(binary),0x004e4942)+binary)

def geometry_digest(doc,binary):
 h=hashlib.sha256();h.update(json.dumps([{k:v for k,v in mesh.items() if k!='primitives'} for mesh in doc['meshes']],sort_keys=True).encode())
 for mesh in doc['meshes']:
  for prim in mesh['primitives']:
   for acc in [*prim['attributes'].values(),prim.get('indices')]:
    if acc is None:continue
    accessor=doc['accessors'][acc];view=doc['bufferViews'][accessor['bufferView']];start=view.get('byteOffset',0);h.update(binary[start:start+view['byteLength']])
 h.update(json.dumps(doc.get('nodes'),sort_keys=True).encode());h.update(json.dumps(doc.get('animations'),sort_keys=True).encode());return h.hexdigest()

def semantic(bid,image,mat):
 key=mat.get('extras',{}).get('city_text_id')
 if key:return key
 if image in ('sign-2k','signboard-2k'):return 'building.'+bid
 if image=='menu-board-2k':return 'shared.tea.menu'
 if 'Room sign ' in mat.get('name',''):return 'room.'+mat['name'].split('Room sign ',1)[1]
 if re.fullmatch(r'M1-\d{4}',image):return 'tenant.'+image
 if image=='mallbrand-2k':return 'shared.mall.brand'
 if image=='campaign-2k':return 'shared.mall.campaign'
 if image=='feature-basecolor' and re.fullmatch(r'S\d+',bid):return 'wall.'+bid
 return None

def text_free_wall(bid):
 import numpy as np
 from PIL import Image
 from street_city_grok_brand_materials import OUT as GROK,periodic
 from street_city_shop_wall_styles import profile
 p=profile(bid);w,h=p['feature_period_m'];n=2048
 with Image.open(GROK/'assets'/bid/'material/concept.jpg') as im:raw=np.asarray(im.convert('RGB').resize((1024,1024)),dtype=float)
 tile=periodic(raw).astype('uint8');u=((np.arange(n)/n*w/1.5)%1*1023).astype(int);v=(((1-np.arange(n)/n)*h/1.5)%1*1023).astype(int)
 target=OUT/'text-free-walls'/(bid+'.png');target.parent.mkdir(parents=True,exist_ok=True);Image.fromarray(tile[v[:,None],u[None,:]]).save(target);return target

def localized_wall(bid):
 from PIL import Image
 from street_city_shop_wall_styles import profile
 from street_city_grok_brand_materials import load_logo
 p=profile(bid);w,h=p['feature_period_m'];n=2048
 placement_path=ROOT/'output/street-social-city-production/shop-brand-grok-v2/placements'/(bid+'.json')
 placement=json.loads(placement_path.read_text('utf8'));assert placement['status']=='passed_source_geometry'
 y0,z0,y1,z1=placement['logo_bounds_yz_m'];width=round((y1-y0)/w*n);height=round((z1-z0)/h*n)
 x=round((y0+w/2)/w*n);y=round((1-(z1-placement['floor_z_m'])/h)*n)
 assert x>=0 and y>=0 and x+width<=n+1 and y+height<=n+1,(bid,'Logo outside UV canvas')
 base=text_free_wall(bid);variants={}
 for locale in LOCALES:
  with Image.open(base) as im:canvas=im.convert('RGB')
  logo=load_logo(bid,locale).resize((width,height),Image.Resampling.LANCZOS);canvas.paste(logo,(x,y),logo)
  target=OUT/'walls'/locale/(bid+'.png');target.parent.mkdir(parents=True,exist_ok=True);canvas.save(target)
  variants[locale]=dict(path=str(target),sha256=sha(target),size=[n,n],integrated_lettering=True,brand_artwork_sha256=sha(OUT/'brands'/locale/(bid+'.png')),placement_sha256=sha(placement_path))
 return dict(id='wall.'+bid,building=bid,kind='wall',variants=variants)

def files():
 from street_city_campus_contract import IDS,context
 base=ROOT/'output/street-social-city-production';rows=[]
 for n in range(1,97):
  bid=f'S{n:02d}';folder=base/f'{bid}-modular-components-v1/assembly-building'
  rows.append((bid,folder/f'{bid}-complete-2k.glb'))
 for bid in IDS:rows.append((bid,context(bid)['out']/f'{bid}-complete-2k.glb'))
 rows.append(('M1',base/'mall-v1/M1-complete-2k.glb'))
 return rows

def convert(row,output_dir=None):
 bid,source=row;doc,binary=read(source);before=geometry_digest(doc,binary);bindings=[];original_count=len(doc['materials']);image_cache={}
 def texture(path):
  nonlocal binary
  if str(path) in image_cache:return image_cache[str(path)]
  raw=Path(path).read_bytes();binary+=b'\0'*((-len(binary))%4);view=len(doc['bufferViews']);doc['bufferViews'].append(dict(buffer=0,byteOffset=len(binary),byteLength=len(raw)));binary+=raw
  image=len(doc.setdefault('images',[]));doc['images'].append(dict(name=Path(path).stem,bufferView=view,mimeType='image/png'));index=len(doc.setdefault('textures',[]));doc['textures'].append(dict(source=image));image_cache[str(path)]=index;return index
 for index,m in enumerate(list(doc['materials'])):
  t=m.get('pbrMetallicRoughness',{}).get('baseColorTexture')
  if not t:continue
  image=doc['images'][doc['textures'][t['index']]['source']].get('name','');key=semantic(bid,image,m)
  if not key:continue
  variants={l:OUT/'walls'/l/(bid+'.png') for l in LOCALES} if key.startswith('wall.') else {l:path_for(key,l) for l in LOCALES}
  assert all(p.exists() for p in variants.values()),(bid,key)
  choices=[]
  for locale in LOCALES:
   new=copy.deepcopy(m);new['name']=m['name']+'__'+locale.replace('-','_');new['pbrMetallicRoughness']['baseColorTexture']['index']=texture(variants[locale]);new.setdefault('extras',{}).update(city_text_id=key,city_text_locale=locale)
   if locale=='zh-Hant':doc['materials'][index]=new;choices.append(index)
   else:choices.append(len(doc['materials']));doc['materials'].append(new)
  for mesh in doc['meshes']:
   for prim in mesh['primitives']:
    if prim.get('material')==index:prim.setdefault('extensions',{})['KHR_materials_variants']={'mappings':[dict(material=v,variants=[i]) for i,v in enumerate(choices)]}
  bindings.append(dict(material=m['name'],material_index=index,semantic_id=key,variants={l:str(p) for l,p in variants.items()},source_image=image))
 doc.setdefault('extensions',{})['KHR_materials_variants']={'variants':[dict(name=l) for l in LOCALES]};doc.setdefault('extensionsUsed',[])
 if 'KHR_materials_variants' not in doc['extensionsUsed']:doc['extensionsUsed'].append('KHR_materials_variants')
 directory=Path(output_dir) if output_dir else OUT/'models'
 target=directory/(bid+'-three-languages.glb');write(target,doc,binary);again,raw=read(target);assert geometry_digest(again,raw)==before
 result=dict(building=bid,source=str(source),source_sha256=sha(source),model=str(target),model_sha256=sha(target),geometry_sha256=before,geometry_and_animation_unchanged=True,bindings=bindings,original_materials=original_count)
 (directory/(bid+'.json')).write_text(json.dumps(result,ensure_ascii=False,indent=2),'utf8');print(bid,len(bindings),'localized slots',flush=True);return result

def main():
 with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:walls=list(pool.map(lambda n:localized_wall(f'S{n:02d}'),range(1,97)))
 assets=json.loads((OUT/'assets.json').read_text('utf8'));assets['walls']=walls;(OUT/'assets.json').write_text(json.dumps(assets,ensure_ascii=False,indent=2),'utf8')
 with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:rows=list(pool.map(convert,files()))
 (OUT/'models.json').write_text(json.dumps(dict(status='passed_portable_geometry_preservation',locales=list(LOCALES),buildings=rows),ensure_ascii=False,indent=2),'utf8')
if __name__=='__main__':main()
