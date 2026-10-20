"""Regenerate vector shop drawings from interpreted Grok images + metre geometry.

Raster proportions are deliberately not traced: the image supplies materials,
canopy/sign motifs and furnishing finish; the validated model supplies lengths.
"""
import hashlib, html, json, re
from pathlib import Path
import xml.etree.ElementTree as ET
from street_city_io import write_json, write_text
from street_city_small_shop import draw, assert_building
from street_city_store_symbols import symbol
from street_city_doors import door_svg
from label_street_city_floor_metrics import floor_metrics_svg

ROOT=Path(__file__).resolve().parents[1];PROD=ROOT/'Docs/StreetSocialCity/production'
CACHE=ROOT/'output/street-social-city-production/store-concepts'
READS=ROOT/'output/street-social-city-production/image-to-svg';DEST=PROD/'store-planning-svg'
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def txt(x,y,value,size=14):return f'<text x="{x}" y="{y}" font-size="{size}" fill="#203c45">{html.escape(str(value))}</text>'
def rect(x,y,w,h,fill,extra=''):return f'<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="{fill}" {extra}/>'

def provenance(text,record,decision,receipt):
 metadata=dict(id=record['id'],image_sha256=receipt['image_sha256'],image_source_design_sha256=record.get('image_origin',{}).get('design_sha256',record['design_sha256']),design_sha256=record['design_sha256'],interpretation_sha256=receipt['interpretation_sha256'],dimensions_source='assembly-manifest.json',image_informs=['material_palette','facade_trim','sign_motif','canopy_motif','furnishing_finish','review_and_correct_fixture_distribution'],raster_geometry_traced=False,metre_geometry_approved_separately=True,actual_tripo_model=False)
 return text.replace('</svg>','<metadata id="grok-image-to-vector">'+html.escape(json.dumps(metadata,ensure_ascii=False))+'</metadata></svg>')

def facade(b,style):
 w,d=b['footprint_m'];s=min(38,800/w);h=b['height_m'];base=90+h*s
 p=style['palette'];front='north' if b.get('front_orientation')=='north' else 'south'
 body=txt(35,32,'依 Grok 圖稿重畫的公尺立面／材質與招牌細節',22)
 body+=txt(35,58,'開口位置、樓層與層高依同一建物資料；弧形僅作框內裝飾。',13)
 body+=rect(50,90,w*s,h*s,p['wall'],'stroke="'+p['frame']+'" stroke-width="2" data-vector-facade="true"')
 for f in b['floors']:
  z=f['z_m'];fh=f['height_m'];yy=base-(z+fh)*s
  body+=rect(50,yy,w*s,.12*s,p['frame'])
  body+=txt(65+w*s,yy+22,f'{f["floor"]}F / 層高 {fh:g}m',13)
  for i,o in enumerate(f['small_shop_design'].get('exterior_openings',[])):
   if o['side']!=front:continue
   x,_,oz=o['local_position_m'];ww=o['width_m'];hh=o['height_m'];xx=50+(x+w/2-ww/2)*s;oy=base-(z+oz+hh)*s
   body+=rect(xx,oy,ww*s,hh*s,'#c6e4ea',f'stroke="{p["frame"]}" stroke-width="2" data-facade-opening="{f["floor"]}-{i}" data-width-m="{ww}" data-height-m="{hh}"')
   body+=f'<line x1="{xx+ww*s/2}" y1="{oy}" x2="{xx+ww*s/2}" y2="{oy+hh*s}" stroke="{p["frame"]}" stroke-width="2"/>'
   if oz==0:body+=txt(xx,oy+hh*s+18,'正門 2.4m',11)
   elif style['glazing']=='arched':
    body+=f'<path d="M{xx+3},{oy+min(hh*s/2,ww*s/2)} Q{xx+ww*s/2},{oy+3} {xx+ww*s-3},{oy+min(hh*s/2,ww*s/2)}" fill="none" stroke="{p["wood"]}" stroke-width="3" data-image-motif="arched-trim-within-existing-opening"/>'
  if f['floor']==1:
   sy=base-3.05*s;body+=rect(50+.3*s,sy,(w-.6)*s,.45*s,p['accent'],'data-image-motif="fascia"')
   body+=txt(50+.5*s,sy+.32*s,b['small_shop_design']['name'],max(12,min(20,s*.4)))
   if style['sign_shape']=='round':body+=f'<circle cx="{50+(w-.9)*s}" cy="{sy+.22*s}" r="{.32*s}" fill="{p["wood"]}" stroke="{p["frame"]}" data-image-motif="round-sign"/>'
   elif style['sign_shape']=='blade':body+=rect(50+(w-.75)*s,sy,.45*s,.9*s,p['wood'],'data-image-motif="blade-sign"')
   if style['canopy'] in ('flat','striped'):
    body+=rect(50+.2*s,base-2.8*s,(w-.4)*s,.15*s,p['wood'],'data-image-motif="canopy-front-profile"')
    if style['canopy']=='striped':
     for k in range(int((w-.4)/.4)):body+=rect(50+(.2+k*.4)*s,base-2.8*s,.2*s,.15*s,p['accent'])
   elif style['canopy']=='recessed':body+=rect(50+.2*s,base-2.7*s,(w-.4)*s,.08*s,p['frame'],'data-image-motif="recessed-entry-frame"')
 body+=txt(50,base+40,f'面寬 {w:g}m × 進深 {d:g}m；總高 {h:g}m；正門朝'+('北' if front=='north' else '南'),15)
 notes=style['observed_features']+['採用：'+q for q in style['adopt']]+['校正：'+q for q in style['reject']]
 y=base+75
 for line in notes:
  # Wrap by characters so long Chinese observations do not run out of the SVG.
  for start in range(0,len(line),48):body+=txt(50,y,line[start:start+48],13);y+=22
 return body,y+35

def street(record,b,style,receipt):
 if b.get('small_shop_design'):
  assert_building(b);draw(b,DEST,image_style=style)
 else:
  # Inns/spas use the large-building solver, not the attached-shop core.
  # Re-render its existing native geometry with the interpreted finish palette.
  for suffix in ['']+[f'-F{f["floor"]:02d}' for f in b['floors']]:
   source=PROD/f'floor-layouts/{record["id"]}{suffix}.svg';raw=source.read_text('utf8')
   for old,new in (('#d8c6a6',style['palette']['wood']),('#9ab5a6',style['palette']['accent']),('#375b59',style['palette']['frame'])):raw=raw.replace(old,new)
   write_text(DEST/source.name,raw)
 key=record['id'];target=DEST/(key+'.svg');raw=target.read_text('utf8');root=ET.fromstring(raw);height=float(root.get('height'))
 if b.get('small_shop_design'):body,extra=facade(b,style)
 else:
  body=txt(35,35,'依 Grok 圖稿重畫：公共接待與材質細節',22);y=70
  for j,(name,col) in enumerate(style['palette'].items()):
   body+=rect(35+j*170,y,140,70,col,'stroke="#203c45"');body+=txt(35+j*170,y+90,name+' '+col,12)
  y+=130
  for line in style['observed_features']+['採用：'+q for q in style['adopt']]+['校正：'+q for q in style['reject']]:
   for start in range(0,len(line),60):body+=txt(35,y,line[start:start+60],13);y+=22
  body+=txt(35,y+20,'完整樓層、接待開放區、樓梯、電梯與衛浴位置均見上方公尺平面。',14);extra=y+65
 raw=raw.replace('height="'+root.get('height')+'"',f'height="{height+extra}"',1).replace(root.get('viewBox'),f'0 0 {root.get("width")} {height+extra}',1)
 raw=raw.replace('</svg>',f'<g transform="translate(0,{height})">{body}</g></svg>')
 write_text(target,provenance(raw,record,style,receipt))
 for f in b['floors']:
  p=DEST/f'{key}-F{f["floor"]:02d}.svg';write_text(p,provenance(p.read_text('utf8'),record,style,receipt))
 return target

def mall(record,b,style,receipt):
 u=record['tenant_unit'];q=u['dimensioned_programme'];w,d,_=u['clear_size_m'];s=min(26,1000/max(w,d));ox=70+w*s/2;oy=160+d*s/2;p=style['palette']
 f=next(x for x in b['floors'] if x['floor']==u['floor'])
 body=txt(35,35,record['id']+' '+record['name']+'／Grok 圖稿轉繪配置',24)
 body+=txt(35,65,f'百貨外框 150×100m；本店淨區 {w:.2f}×{d:.2f}m；{u["floor"]}F；共用核心見下方完整樓層。',14)
 body+=txt(35,92,'陳列分散於各區；米色通路淨寬2m，開放邊界不加玻璃牆。圖片供造型參考，尺寸依公尺配置。',13)
 body+=txt(35,116,f'本圖為櫃位局部座標；整層內旋轉 {u["rotation_z_deg"]}°，中心 ({u["position_m"][0]:.2f},{u["position_m"][1]:.2f})m。',12)
 body+=rect(70,160,w*s,d*s,p['wall'],'stroke="#203c45" '+('stroke-dasharray="5 4" ' if u.get('enclosure_policy',{}).get('mode')=='open' else '')+'data-tenant-envelope="true"')
 def area(r,fill,extra=''):
  a,c,e,g=r;return rect(ox+a*s,oy-g*s,(e-a)*s,(g-c)*s,fill,extra)
 for r in q.get('internal_corridors',[]):body+=area(r['bounds_m'],'#ecddc6','data-route-clear-m="2"')
 for j,e in enumerate(q.get('equipment_layout',[]),1):
  x,y,_=e['room_local_position_m'];ew,ed,_=e['size_m'];glyph=symbol(e['equipment'],ox+x*s,oy-y*s,ew*s,ed*s,rotation=-e.get('rotation_z_deg',0))
  glyph=glyph.replace('#d8c6a6',p['wood']).replace('#375b59',p['frame']).replace('#9ab5a6',p['accent']);body+=glyph
  body+=txt(ox+(x-ew/2)*s,oy-(y+ed/2)*s-2,j,10)
 for r in q.get('internal_rooms',[]):
  if r.get('bounds_m'):body+=area(r['bounds_m'],'#e9e3d5','stroke="#697c79"')
 for e in u.get('open_plan',{}).get('open_edges',[]):
  x,y,_=e['local_position_m'];ww=e['width_m'];vertical=e['side'] in ('east','west')
  x1=ox+x*s-(0 if vertical else ww*s/2);y1=oy-y*s-(ww*s/2 if vertical else 0)
  body+=f'<line x1="{x1}" y1="{y1}" x2="{x1+(0 if vertical else ww*s)}" y2="{y1+(ww*s if vertical else 0)}" stroke="#659d86" stroke-width="3" stroke-dasharray="8 5" data-open-edge="{e["side"]}"/>'
 if u.get('enclosure_policy',{}).get('mode')!='open':
  source_door=next(a for a in f['door_schedule'] if a['id']==u['room_id']+'-door')
  dx,dy=u['door']['local_position_m'][:2]
  side=min({'west':abs(dx+w/2),'east':abs(dx-w/2),'south':abs(dy+d/2),'north':abs(dy-d/2)},key=lambda k:{'west':abs(dx+w/2),'east':abs(dx-w/2),'south':abs(dy+d/2),'north':abs(dy-d/2)}[k])
  door=dict(source_door,id=record['id']+'-entry',center_m=[dx,dy],outward_side=side)
  body+=door_svg(door,ox,oy,s)
 body+=floor_metrics_svg(record['id'],u['floor'],[w,d],s,70,160)
 y=160+d*s+115
 detail=f'<svg xmlns="http://www.w3.org/2000/svg" width="1160" height="{y}" viewBox="0 0 1160 {y}"><style>text{{font-family:Microsoft JhengHei,sans-serif}}</style><rect width="100%" height="100%" fill="#fffdf8"/>{body}</svg>'
 write_text(DEST/(record['id']+'-detail.svg'),provenance(detail,record,style,receipt))
 for line in style['observed_features']+['採用：'+a for a in style['adopt']]+['校正：'+a for a in style['reject']]:
  for start in range(0,len(line),65):body+=txt(35,y,line[start:start+65],13);y+=21
 body+=txt(35,y+15,'商品與設備索引（同一公尺配置）',18);y+=45
 for j,e in enumerate(q.get('equipment_layout',[]),1):
  from street_city_store_program import EQUIPMENT
  name=EQUIPMENT.get(e['equipment'],(e['equipment'],))[0];ew,ed,_=e['size_m'];x,y0,_=e['room_local_position_m']
  body+=txt(35,y,f'{j:02d} {name} {ew:g}×{ed:g}m；店內位置 ({x:.2f},{y0:.2f})',12);y+=19
 y+=20;body+=txt(35,y,'附屬區與同層公共核心位置（建物座標）',18);y+=30
 for a in record['associated_annex_spaces']:
  aw,ad,_=a['size_m'];pos=a['position_m'];body+=txt(35,y,f'{a["id"]} {a["name"]}：淨 {aw:.2f}×{ad:.2f}m，中心 ({pos[0]:.2f},{pos[1]:.2f})，門 {a["door_clear_width_m"]}m',12);y+=20
 for a in f['vertical_circulation']:
  aw,ad,_=a['size_m'];pos=a['position_m'];body+=txt(35,y,f'{a["id"]}：{aw:.2f}×{ad:.2f}m，中心 ({pos[0]:.2f},{pos[1]:.2f})',12);y+=20
 y+=30;body+=txt(35,y,'完整同層空間：本店、公共走道、電梯、樓梯、廁所及附屬區均在同一外框',17);y+=25
 context=ET.fromstring((PROD/f'floor-layouts/M1-F{u["floor"]:02d}.svg').read_text('utf8'));cw=float(context.get('width'));ch=float(context.get('height'));factor=1100/cw
 context.set('x','0');context.set('y',str(y));context.set('width','1100');context.set('height',str(ch*factor));body+=ET.tostring(context,encoding='unicode');y+=ch*factor+30
 raw=f'<svg xmlns="http://www.w3.org/2000/svg" width="1160" height="{y}" viewBox="0 0 1160 {y}"><style>text{{font-family:Microsoft JhengHei,sans-serif}}</style><rect width="100%" height="100%" fill="#fffdf8"/>{body}</svg>'
 target=DEST/(record['id']+'.svg');write_text(target,provenance(raw,record,style,receipt));return target

def main():
 DEST.mkdir(parents=True,exist_ok=True);register=json.loads((PROD/'store-design-register.json').read_text('utf8'));records=register['stores']
 manifest=json.loads((PROD/'assembly-manifest.json').read_text('utf8'));buildings={b['building']:b for b in manifest['layouts'] if b.get('kind')=='building'}
 decisions={}
 for path in READS.glob('batch-*/decisions.json'):
  doc=json.loads(path.read_text('utf8'));assert digest(Path(doc['event_log']))==doc['event_log_sha256']
  for row in doc['stores']:assert row['id'] not in decisions;decisions[row['id']]=(row,doc,path)
 assert set(decisions)=={r['id'] for r in records},'Not all images interpreted; SVG generation stops before publishing a partial result'
 results=[];cards=[]
 for r in records:
  key=r['id'];style,doc,path=decisions[key];g=json.loads((CACHE/key/'generation.json').read_text('utf8'));im=CACHE/key/'concept.jpg';source=next(x for x in doc['sources'] if x['id']==key)
  assert source['image_sha256']==digest(im)==g['image_sha256'] and source['design_sha256']==r.get('image_origin',{}).get('design_sha256',r['design_sha256'])
  receipt=dict(image_sha256=digest(im),interpretation_sha256=digest(path))
  target=street(r,buildings[r['building']],style,receipt) if r['building']!='M1' else mall(r,buildings['M1'],style,receipt)
  result=dict(id=key,name=r['name'],svg_file=target.relative_to(ROOT).as_posix(),svg_sha256=digest(target),**receipt,design_sha256=r['design_sha256'],image_source_design_sha256=g['design_sha256'],image_geometry_traced=False,adopted=style['adopt'],corrected=style['reject']);results.append(result)
  imagehref='../../../'+im.relative_to(ROOT).as_posix();svghref='store-planning-svg/'+key+'.svg'
  thumb='store-planning-svg/'+key+('-detail.svg' if r['building']=='M1' else '-F01.svg')
  notes=''.join('<li>'+html.escape(s)+'</li>' for s in style['adopt'])
  cards.append(f'<article data-search="{html.escape(key+" "+r["name"])}"><h2>{key} {html.escape(r["name"])}</h2><div class="compare"><a href="{imagehref}"><img loading="lazy" src="{imagehref}" alt="{key} Grok 示意圖"></a><a href="{svghref}"><img loading="lazy" src="{thumb}" alt="{key} 重畫向量配置"></a></div><p><a href="{svghref}">開啟完整 SVG（各層／立面／公共核心）</a> · <a href="{imagehref}">Grok 原圖</a></p><ul>{notes}</ul></article>')
 report=dict(schema=1,status='image_interpreted_vectors_regenerated',store_count=len(results),street_store_count=sum(r['building']!='M1' for r in records),mall_tenant_count=sum(r['building']=='M1' for r in records),source_manifest_sha256=digest(PROD/'assembly-manifest.json'),stores=results,models_paused=True)
 write_json(PROD/'image-to-svg-register.json',report)
 page='<!doctype html><meta charset="utf-8"><title>逐店 Grok 示意圖 → SVG</title><style>body{font:16px Microsoft JhengHei,sans-serif;color:#203c45;background:#f3f0e8;margin:24px}article{background:white;padding:20px;margin:20px 0;border-radius:12px}.compare{display:grid;grid-template-columns:1fr 1fr;gap:20px;align-items:start}img{width:100%;max-height:720px;object-fit:contain;object-position:top}input{font:inherit;padding:12px;width:360px}a{color:#17697a}@media(max-width:800px){.compare{grid-template-columns:1fr}}</style><h1>逐店 Grok 示意圖 → SVG</h1><p>48 棟街邊店屋＋80 間百貨店，共128間。先生成圖稿，再逐圖辨識材料、招牌、遮簷與家具造型，最後重畫向量立面與完整配置。尺寸、層數、核心與通路以同一公尺資料校正。</p><p>圖片中的錯誤比例不會直接描成施工尺寸。各店 SVG 列出採用內容與校正事項；百貨店另包含完整同層公共核心位置。</p><p><a href="scene-review.html">世界場景</a> · <a href="store-review.html">店面研究與原始圖稿</a> · <a href="IMAGE_TO_SVG.md">流程與驗收</a></p><input id="find" placeholder="搜尋 S01、M1-0101 或店名"><main>'+''.join(cards)+'</main><script>document.querySelector("#find").oninput=e=>document.querySelectorAll("article").forEach(a=>a.hidden=!a.dataset.search.toLowerCase().includes(e.target.value.toLowerCase()))</script>'
 page=page.replace('48 棟街邊店屋＋80 間百貨店，共128間',f'{report["street_store_count"]} 棟街邊店屋＋{report["mall_tenant_count"]} 間百貨店，共{len(results)}間')
 write_text(PROD/'image-to-svg-review.html',page)
 if not (PROD/'IMAGE_TO_SVG.md').is_file():
  write_text(PROD/'IMAGE_TO_SVG.md',f'# 逐店 Grok 示意圖與 SVG\n\n已產生{len(results)}間店的圖稿轉繪：{report["street_store_count"]}棟街邊店屋及{report["mall_tenant_count"]}間百貨店。\n\n流程：實際圖稿 → Grok讀圖解讀 → 依公尺資料校正並重畫原生向量。圖片提供材料與裝修細節；完整樓層、門位和2m通路使用尺寸資料。\n\n驗收由 verify_street_city_image_svgs.py 執行，完成後更新本頁與 image-to-svg-acceptance.json。\n\n[開啟對照頁](image-to-svg-review.html)\n')
 print(json.dumps(dict(stores=len(results),svg_files=len(list(DEST.glob('*.svg')))),ensure_ascii=False))
if __name__=='__main__':main()
