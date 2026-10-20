"""Render all language variants and an offline, genuinely switchable review."""
import concurrent.futures,html,json,re
from pathlib import Path
from PIL import Image,ImageDraw
from street_city_localization import OUT,ROOT,LOCALES,catalogue,render,path_for,sha,fit_lines

def brand(bid,locale,size=(2048,1024)):
 from street_city_integrated_logo_assets import artwork
 image,evidence=artwork(bid,locale,size)
 target=OUT/'brands'/locale/(bid+'.png');target.parent.mkdir(parents=True,exist_ok=True);image.save(target)
 return dict(path=str(target),sha256=sha(target),size=list(size),**evidence)


def one(entry):
 key=entry['id'];variants={}
 for locale in LOCALES:
  prefix=key.split('.',1)[1] if entry['kind'] in ('room','tenant') else None
  extra={}
  if entry['kind']=='tenant' and key.startswith('tenant.M1-'):
   from street_city_mall_boutiques import profiles,PALETTES
   p=profiles()[key.split('.',1)[1]];bg=PALETTES[p['palette_index']][1];extra=dict(background=tuple(bg),foreground=(238,232,216) if sum(bg)<480 else (40,43,41));prefix=None
  variants[locale]=render(key,locale,path_for(key,locale),prefix=prefix,**extra)
 return dict(id=key,building=entry['building'],kind=entry['kind'],variants=variants)

def publish(rows):
 c=catalogue();values=[]
 for r in rows:
  e=c['entries'][r['id']];r={**r,'translations':e['translations']}
  for v in r['variants'].values():v['url']=Path(v['path']).relative_to(OUT).as_posix()
  values.append(r)
 data=json.dumps(values,ensure_ascii=False).replace('</',r'<\/');page='''<!doctype html><html lang="zh-Hant"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>建築文字三語切換</title><style>body{background:#172124;color:#eee;font:16px/1.6 system-ui;margin:0}header{position:sticky;top:0;background:#172124;padding:20px;z-index:2}main{display:grid;grid-template-columns:repeat(auto-fit,minmax(330px,1fr));gap:16px;padding:20px}article{background:#293638;padding:14px}img{width:100%;object-fit:contain}select,input{padding:10px;font:inherit}a{color:#aee0c4}</style><header><h1>所有建築｜繁體・简体・English</h1><p>122棟建物，店名、校園房名、百貨櫃位與公共指示。下方切換的是實際PNG貼圖。<span id="count"></span></p><select id="locale"><option value="zh-Hant">繁體中文</option><option value="zh-Hans">简体中文</option><option value="en">English</option></select> <input id="search" placeholder="搜尋 ID／建築／名稱"><a href="catalogue.json">集中語系表</a> · <a href="assets.json">貼圖與SHA</a></header><main id="cards"></main><script>const rows=__DATA__;let locale=localStorage.getItem('StreetCityLocale')||'zh-Hant';if(!['zh-Hant','zh-Hans','en'].includes(locale))locale='zh-Hant';const select=document.getElementById('locale');select.value=locale;function show(){document.documentElement.lang=locale;let q=document.getElementById('search').value.toLowerCase();let filtered=rows.filter(r=>JSON.stringify([r.id,r.building,r.translations]).toLowerCase().includes(q));document.getElementById('count').textContent=filtered.length+' 項';document.getElementById('cards').replaceChildren(...filtered.map(r=>{let a=document.createElement('article'),h=document.createElement('h2'),i=document.createElement('img'),p=document.createElement('p');h.textContent=r.id+' '+r.translations[locale];i.loading='lazy';i.src=r.variants[locale].url;i.alt=r.translations[locale];p.textContent=r.variants[locale].size.join(' × ')+' px';a.append(h,i,p);return a}));}select.onchange=()=>{locale=select.value;localStorage.setItem('StreetCityLocale',locale);show()};document.getElementById('search').oninput=show;show();</script></html>'''.replace('__DATA__',data)
 (OUT/'index.html').write_text(page,'utf8')

def main():
 entries=list(catalogue()['entries'].values())
 with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:rows=list(pool.map(one,entries))
 brands=[]
 for n in range(1,97):
  bid=f'S{n:02d}';brands.append(dict(id='brand.'+bid,building=bid,kind='brand',variants={l:brand(bid,l) for l in LOCALES}))
 # Preserve the full approved integrated logo on each real fascia canvas.
 from street_city_integrated_logo_assets import artwork
 for row in rows:
  if row['kind']!='building' or not re.fullmatch(r'S\d+',row['building']):continue
  for locale in LOCALES:
   target=path_for(row['id'],locale);image,evidence=artwork(row['building'],locale,(2048,420));image.save(target)
   row['variants'][locale].update(path=str(target),sha256=sha(target),size=[2048,420],**evidence)
 (OUT/'assets.json').write_text(json.dumps(dict(status='generated_requires_native_binding',catalogue_sha256=sha(OUT/'catalogue.json'),rows=rows,brands=brands),ensure_ascii=False,indent=2),'utf8');publish(rows)
 print('LANGUAGE TEXTURES',len(rows)*3,'BRANDS',len(brands)*3,flush=True)
if __name__=='__main__':main()
