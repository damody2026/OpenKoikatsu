"""Independently inspect generated pixels, glTF variants and unchanged geometry."""
import concurrent.futures,ctypes,hashlib,json,re
from pathlib import Path
from PIL import Image
from street_city_localization import ROOT,OUT,LOCALES,catalogue,sha
from build_street_city_language_models import read,geometry_digest

def glyphs(chars):
 g=ctypes.windll.gdi32;g.CreateCompatibleDC.restype=ctypes.c_void_p;dc=g.CreateCompatibleDC(None)
 g.CreateFontW.restype=ctypes.c_void_p;g.CreateFontW.argtypes=[ctypes.c_int]*13+[ctypes.c_wchar_p]
 font=g.CreateFontW(-24,0,0,0,400,0,0,0,1,0,0,0,0,'Microsoft JhengHei');g.SelectObject.argtypes=[ctypes.c_void_p,ctypes.c_void_p];old=g.SelectObject(dc,font)
 values=(ctypes.c_uint16*len(chars))();g.GetGlyphIndicesW.argtypes=[ctypes.c_void_p,ctypes.c_wchar_p,ctypes.c_int,ctypes.POINTER(ctypes.c_uint16),ctypes.c_uint32];n=g.GetGlyphIndicesW(dc,chars,len(chars),values,1);assert n!=0xffffffff
 missing=[c for c,v in zip(chars,values) if v==0xffff];g.SelectObject(dc,old);g.DeleteObject.argtypes=[ctypes.c_void_p];g.DeleteObject(font);g.DeleteDC.argtypes=[ctypes.c_void_p];g.DeleteDC(dc);return missing

def pixels(row):
 sizes=[]
 for locale in LOCALES:
  v=row['variants'][locale];path=Path(v['path']);assert sha(path)==v['sha256'],str(path)
  with Image.open(path) as im:
   im.load();assert max(im.size)<=2048 and min(im.size)>0;assert list(im.size)==v['size'];sizes.append(im.size)
 assert len(set(sizes))==1,row['id'];return len(LOCALES)

def model(row):
 source=Path(row['source']);path=Path(row['model']);assert sha(source)==row['source_sha256'] and sha(path)==row['model_sha256'];original,oldbin=read(source);doc,binary=read(path)
 assert geometry_digest(original,oldbin)==geometry_digest(doc,binary)==row['geometry_sha256']
 assert [v['name'] for v in doc['extensions']['KHR_materials_variants']['variants']]==list(LOCALES)
 count=0
 for binding in row['bindings']:
  index=binding['material_index'];referenced=[p for m in doc['meshes'] for p in m['primitives'] if p.get('material')==index];assert referenced,(row['building'],binding['semantic_id'])
  for prim in referenced:
   mappings=prim['extensions']['KHR_materials_variants']['mappings'];assert sorted(i for v in mappings for i in v['variants'])==[0,1,2]
   for n,locale in enumerate(LOCALES):
    mat=doc['materials'][next(v['material'] for v in mappings if n in v['variants'])];texture=mat['pbrMetallicRoughness']['baseColorTexture']['index'];image=doc['images'][doc['textures'][texture]['source']];view=doc['bufferViews'][image['bufferView']];start=view.get('byteOffset',0);image_sha=hashlib.sha256(binary[start:start+view['byteLength']]).hexdigest();assert image_sha==sha(binding['variants'][locale]),(row['building'],binding['semantic_id'],locale)
    # Every other PBR property must be byte-for-byte semantically unchanged.
    a=json.loads(json.dumps(original['materials'][index]));b=json.loads(json.dumps(mat))
    for value in (a,b):
     value.pop('name',None);value.pop('extras',None);value['pbrMetallicRoughness']['baseColorTexture']['index']=0
    assert a==b,(row['building'],binding['semantic_id'],'PBR connection drift');count+=1
 return dict(building=row['building'],localized_slots=len(row['bindings']),variant_checks=count,geometry_and_animation_preserved=True)

def main():
 c=catalogue();entries=c['entries'];assert len(entries)==901
 assert len([e for e in entries.values() if e['kind']=='building'])==122
 for key,e in entries.items():
  assert set(e['translations'])==set(LOCALES) and all(t.strip() for t in e['translations'].values()),key
  assert not re.search('[\u3400-\u9fff]',e['translations']['en']),key
 chars=''.join(sorted(set(''.join(t for e in entries.values() for t in e['translations'].values()))-{'\n','\r'}));missing=glyphs(chars);assert not missing,('Font has missing glyphs',missing)
 assets=json.loads((OUT/'assets.json').read_text('utf8'));assert assets['catalogue_sha256']==sha(OUT/'catalogue.json')
 assert len(assets['brands'])==96 and len(assets['walls'])==96
 for r in assets['brands']+assets['walls']+[r for r in assets['rows'] if r['kind']=='building' and re.fullmatch(r'S\d+',r['building'])]:
  assert all(v.get('integrated_lettering') for v in r['variants'].values()),r['id']
  receipt=json.loads((OUT/'integrated-logos/jobs'/r['building']/'variants.json').read_text('utf8'));assert receipt['status']=='passed_visual_review'
 with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:images=sum(pool.map(pixels,assets['rows']+assets['brands']+assets['walls']))
 models=json.loads((OUT/'models.json').read_text('utf8'))['buildings'];assert len(models)==114
 with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:rows=list(pool.map(model,models))
 receipt=dict(status='passed_textures_and_portable_models',locales=list(LOCALES),catalogue_entries=len(entries),planned_buildings=122,existing_3d_buildings=114,language_images=images,unique_font_glyphs=len(chars),missing_glyphs=[],portable_models=rows,native_acceptance='separate reopen audit required',catalogue_sha256=sha(OUT/'catalogue.json'),assets_sha256=sha(OUT/'assets.json'),models_sha256=sha(OUT/'models.json'))
 (OUT/'asset-acceptance.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2),'utf8');print(receipt['status'],images,'images',len(rows),'models',flush=True)
if __name__=='__main__':main()
