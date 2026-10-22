"""Single semantic catalogue and deterministic three-language text textures.

Non-text surfaces are never translated. The same semantic ID and pixel canvas
are retained across languages; unknown translations fail instead of falling back.
"""
import ctypes,hashlib,json,re
from functools import lru_cache
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'output/street-social-city-production/localization-v1'
LOCALES=('zh-Hant','zh-Hans','en')
FONT='C:/Windows/Fonts/msjh.ttc'

def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def simplified(text):
 fn=ctypes.windll.kernel32.LCMapStringEx
 fn.argtypes=[ctypes.c_wchar_p,ctypes.c_uint32,ctypes.c_wchar_p,ctypes.c_int,ctypes.c_wchar_p,ctypes.c_int,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_longlong]
 fn.restype=ctypes.c_int
 buf=ctypes.create_unicode_buffer(len(text)*3+8)
 assert fn('zh-CN',0x02000000,text,len(text),buf,len(buf),None,None,0)>0
 return buf.value

def inventory():
 from street_city_building_context import load_manifest
 from street_city_campus_contract import IDS,context
 from street_city_mall import contract
 m=load_manifest();rows=[];assets={a['id']:a for a in m['assets']}
 def add(key,bid,text,kind,source=None,size=(2048,512)):
  rows.append(dict(id=key,building=bid,kind=kind,text=text,source=str(source) if source else None,size=list(size)))
 for b in m['layouts']:
  if b.get('kind')!='building':continue
  bid=b['building'];name=b.get('small_shop_design',{}).get('name')
  if not name:
   names={'S18':'日式民宿','S19':'溫泉會館','M1':'大學城百貨'}
   name=names.get(bid) or assets.get(bid+'-shell',{}).get('name','').split('空房外殼')[0]
   if not name:
    plan=json.loads((ROOT/'Docs/StreetSocialCity/production/effective-city-plan.json').read_text('utf8'))
    # The authoritative plan uses building names rather than geometry labels.
    found=next((v for v in plan.get('buildings',[]) if v.get('id',v.get('building'))==bid),{})
    name=found.get('name')
   assert name,(bid,'Missing building display name')
  add('building.'+bid,bid,name,'building',ROOT/f'output/street-social-city-production/{bid}-modular-components-v1/assembly-building/sign-2k.png',(2048,420))
 for bid in IDS:
  for room in context(bid)['rooms'].values():
   rid=room['room_id'];add('room.'+rid,bid,room['display_name'],'room',ROOT/f'output/street-social-city-production/campus-v1/materials/{bid}/{rid}.png')
 from street_city_remaining_contract import IDS as REMAINING_IDS,context as remaining_context
 for bid in REMAINING_IDS:
  for room in remaining_context(bid)['rooms'].values():
   rid=room['room_id'];add('room.'+rid,bid,room['display_name'],'room',ROOT/f'output/street-social-city-production/world-completion-v1/materials/{bid}/{rid}.png')
 for room in contract()['rooms']:
  add('tenant.'+room['id'],'M1',room['name'],'tenant',ROOT/f'output/street-social-city-production/mall-v1/signs/{room["id"]}.png')
 for key,text in {'mall.brand':'大學城百貨','mall.campaign':'城市生活','tea.menu':'紅茶\n綠茶\n鮮奶茶\n選擇糖度與冰量\n點餐 → 調飲 → 封口 → 取餐','tea.brand':'沐茶手搖','wc':'廁所','accessible_wc':'無障礙廁所','stairs':'樓梯','lift':'電梯','exit':'出口','entrance':'入口','reception':'服務台','floor':'樓層','changing':'更衣室','shower':'淋浴間','male':'男','female':'女','staff':'員工專用','no_entry':'請勿進入','emergency':'緊急出口'}.items():
  size=(2048,1024) if key=='mall.brand' else (1024,2048) if key=='mall.campaign' else (2048,640) if key.startswith('tea') else (2048,512)
  add('shared.'+key,'M1' if key.startswith('mall') else 'S02' if key.startswith('tea') else '*',text,'graphic',size=size)
 assert len({r['id'] for r in rows})==len(rows)
 return rows

@lru_cache(maxsize=1)
def catalogue():
 return json.loads((OUT/'catalogue.json').read_text('utf8'))

def text(key,locale='zh-Hant'):
 assert locale in LOCALES
 return catalogue()['entries'][key]['translations'][locale]

def fit_lines(value,size,initial=160,margin=60):
 from PIL import ImageFont,ImageDraw,Image
 d=ImageDraw.Draw(Image.new('RGB',size));words=value.split('\n')
 for fs in range(initial,11,-2):
  font=ImageFont.truetype(FONT,fs);lines=[]
  for line in words:
   if d.textlength(line,font=font)<=size[0]-2*margin:lines.append(line);continue
   tokens=line.split(' ') if ' ' in line else list(line);sep=' ' if ' ' in line else '';cur=''
   for token in tokens:
    nxt=cur+sep+token if cur else token
    if d.textlength(nxt,font=font)>size[0]-2*margin and cur:lines.append(cur);cur=token
    else:cur=nxt
   lines.append(cur)
  step=round(fs*1.3)
  if step*len(lines)<=size[1]-2*margin and all(d.textlength(t,font=font)<=size[0]-2*margin for t in lines):return font,lines,step
 raise ValueError(('Text cannot fit',value,size))

def render(key,locale,path,size=None,background=(30,67,60),foreground=(239,232,211),prefix=None):
 from PIL import Image,ImageDraw
 entry=catalogue()['entries'][key];size=tuple(size or entry['size']);value=entry['translations'][locale]
 if prefix:value=prefix+'\n'+value
 font,lines,step=fit_lines(value,size,margin=35 if size[1]<650 else 70)
 image=Image.new('RGB',size,background);d=ImageDraw.Draw(image);y=(size[1]-step*len(lines))/2
 for line in lines:
  box=d.textbbox((0,0),line,font=font);d.text(((size[0]-d.textlength(line,font=font))/2,y-box[1]),line,font=font,fill=foreground);y+=step
 path=Path(path);path.parent.mkdir(parents=True,exist_ok=True);image.save(path)
 return dict(path=str(path),sha256=sha(path),size=list(size),font_px=font.size,lines=lines)

def path_for(key,locale):return OUT/'textures'/locale/(key+'.png')

def register_material(mat,key):
 """Blender material metadata survives GLB extras and identifies its text slot."""
 mat['city_text_id']=key;mat['city_text_locale']='zh-Hant';mat['city_text_locales']=list(LOCALES)

def language_group_buildings(group):
 if group=='smoke':return ['S01']
 if group=='shops-all':return [f'S{i:02d}' for i in range(1,97)]
 if group=='campus-all':
  from street_city_campus_contract import IDS
  return list(IDS)
 if group.startswith('shops-'):
  n=int(group.split('-')[1]);return [f'S{i:02d}' for i in range(n,n+12)]
 if group.startswith('campus-'):
  from street_city_campus_contract import IDS
  n=int(group.split('-')[1]);return list(IDS[n:n+4])
 if group=='remaining-all':
  from street_city_remaining_contract import IDS
  return list(IDS)
 assert group=='mall';return ['M1']

def language_group_signature(group):
 models=json.loads((OUT/'models.json').read_text('utf8'));ids=language_group_buildings(group)
 rows=[(b['building'],v['material'],[(l,sha(v['variants'][l])) for l in LOCALES]) for b in models['buildings'] if b['building'] in ids for v in b['bindings']]
 assert {r[0] for r in rows}==set(ids)
 return hashlib.sha256(json.dumps(rows,sort_keys=True).encode()).hexdigest()

if __name__=='__main__':
 OUT.mkdir(parents=True,exist_ok=True);rows=inventory();(OUT/'inventory.json').write_text(json.dumps(dict(buildings=len({r['building'] for r in rows if r['kind']=='building'}),entries=rows),ensure_ascii=False,indent=2),'utf8');print(len(rows))
