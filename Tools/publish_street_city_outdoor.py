"""Publish outdoor metric drawings and actual model review, persistently linked."""
import json,math,html
import re,hashlib
from pathlib import Path
from street_city_outdoor_contract import OUT,R,S
def publish():
 a=json.loads((OUT/'acceptance.json').read_text('utf8'));assert a['status']=='passed'
 cards=[]
 for q in sorted(a['assets'],key=lambda q:{'campus-track':0,'campus-track-access':1}.get(q['id'],2)):
  fid=q['id'];w,d=q['size'];draw=[f'<rect x="{-w/2}" y="{-d/2}" width="{w}" height="{d}" fill="#c6d6ae"/>']
  if fid=='connection':
   ticks=''.join(f'<path d="M 0,{6+i*3} h {n}" stroke="#203c35" stroke-width=".15"/><text x="{n+1}" y="{7+i*3}" font-size="1.5">{n}m</text>' for i,n in enumerate([1,5,10]))
   (OUT/fid/'plan.svg').write_text('<svg xmlns="http://www.w3.org/2000/svg" viewBox="-5 -12 185 32"><style>text{font-family:Microsoft JhengHei,sans-serif;fill:#203c35}</style><rect x="0" y="-2" width="175" height="4" fill="#c9bea7"/><path d="M 15,-2 v 4" stroke="#405e53"/><text x="0" y="-7" font-size="2">橋頭平段15m＋1:20步道160m · 總長175m × 寬4m</text><text x="0" y="-4" font-size="1.5">高程20m</text><text x="157" y="-4" font-size="1.5">高程12m</text>'+ticks+'</svg>','utf8')
   cards.append('<article><h2>橋頭連接步道與支柱</h2><p>總長175m、寬4m；平段15m、1:20下降段160m。支柱依下方地形落地。</p><a href="connection/plan.svg">公尺平面／高程／比例尺</a> · <a href="connection/adopted.glb">GLB</a></article>')
   continue
  if fid in ['stadium','campus-track']:
   radius=q.get('curb_radius',R);straight=q.get('straight',S)
   if q.get('long_axis')=='X':draw.append('<g transform="rotate(90)">')
   for lane in reversed(range(9)):
    rr=radius+lane*1.22;draw.append(f'<rect x="{-rr}" y="{-straight/2-rr}" width="{2*rr}" height="{straight+2*rr}" rx="{rr}" fill="'+('#bf7158' if lane%2 else '#a95139')+'" stroke="#fff" stroke-width=".05"/>')
   draw.append(f'<rect x="{-radius}" y="{-straight/2-radius}" width="{2*radius}" height="{straight+2*radius}" rx="{radius}" fill="#9bbd7f"/>')
   if q.get('long_axis')=='X':draw.append('</g>')
  for f in sorted(q['features'],key=lambda f:0 if f['kind']=='recreation_lawn' else 1):
   kind=f['kind'];x,y,z=f['center'];size=f.get('safety_m',f.get('size',[0,0,0]));fw,fd=size[:2]
   if kind in ['site_floor','extension_terrain','track']:continue
   color={'tennis':'#317a7c','volleyball':'#d6b776','path':'#dcd5c4','grade_link':'#dcd5c4','track_crossing':'#dcd5c4','recreation_lawn':'#9fc483','pergola':'#b19b75','football_pitch':'#52954c'}.get(kind,'#aa9071')
   draw.append(f'<rect x="{x-fw/2}" y="{-y-fd/2}" width="{fw}" height="{fd}" fill="{color}" stroke="#425348" stroke-width=".08"/>')
   if kind in ['tennis','volleyball']:
    pw,pd=f['playing_m'];draw.append(f'<rect x="{x-pw/2}" y="{-y-pd/2}" width="{pw}" height="{pd}" fill="none" stroke="#f4f7eb" stroke-width=".15"/><path d="M {x-pw/2},{-y} h {pw}" stroke="#26392e" stroke-width=".15"/>')
   if kind=='football_pitch':draw.append(f'<rect x="-45" y="-30" width="90" height="60" fill="none" stroke="#fff" stroke-width=".1"/><path d="M0,-30 v60" stroke="#fff" stroke-width=".1"/><circle cx="0" cy="0" r="9.15" fill="none" stroke="#fff" stroke-width=".1"/>')
   if kind in ['tennis','volleyball','pergola','stage','recreation_lawn','football_pitch']:
    name={'tennis':'網球 10.97×23.77m','volleyball':'排球 9×18m','pergola':'遮蔭棚','stage':'社團舞台','recreation_lawn':'飛盤／野餐／社團草坪','football_pitch':'足球場 90×60m'}[kind]
    draw.append(f'<text x="{x}" y="{-y}" text-anchor="middle" font-size="{1.35 if fid=="stadium" else .8}">{name}</text>')
  draw.append(f'<text x="{-w/2}" y="{-d/2-7}" font-size="3">{html.escape(q["name"])} · {w:g}×{d:g}m</text>')
  for i,length in enumerate([1,5,10]):
   yy=d/2+5+i*3;draw.append(f'<path d="M {-w/2},{yy} h {length}" stroke="#233a35" stroke-width=".2"/><text x="{-w/2+length+1}" y="{yy+1}" font-size="2">{length}m</text>')
  path=OUT/fid/'plan.svg';path.write_text(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{-w/2-5} {-d/2-13} {w+10} {d+34}"><style>text{{font-family:Microsoft JhengHei,sans-serif;fill:#17392f}}</style>'+''.join(draw)+'</svg>','utf8')
  if fid=='annex-ground':continue
  cards.append(f'<article><h2>{html.escape(q["name"])}</h2><p>{w:g}×{d:g}m · {q["actual_triangles"]:,} 三角面 · 已採用 Rust remesh 板件／柱件；曲線另經 remesh</p><img src="{fid}/preview.png"><p><a href="{fid}/plan.svg">公尺平面圖／1、5、10m比例尺</a> · <a href="{fid}/adopted.glb">GLB</a></p></article>')
 interactions={'stadium':['800m慢跑計圈／8道競速','網球練習及雙打','排球練習及隊伍競賽','草坪飛盤、野餐、社團遊戲','看台觀賽及棚下休息'],'reading-park':['閱讀','桌遊／棋類','小組討論','野餐及棚下休息'],'active-park':['舞蹈與社團表演','階梯觀眾席','引體向上及伸展','休息交流'],'connection':['步行及輪椅通行路徑'],'annex-ground':['場地基底'],'campus-links':['校舍與公園步行連接']}
 interactions.update({'campus-track':['400m慢跑／計圈／體育課','足球練習與校內比賽','看台觀賽及校園集會'],'campus-track-access':['校內主步道與操場連續步行通行']})
 (OUT/'interactions.json').write_text(json.dumps(dict(status='planned_gameplay_not_implemented',facilities=interactions),ensure_ascii=False,indent=2),'utf8')
 page='<!doctype html><meta charset="utf-8"><title>校園戶外運動與學生公園</title><style>body{max-width:1400px;margin:30px auto;background:#18232b;color:#eaf1ed;font:18px Microsoft JhengHei}a{color:#91e4cf}article{background:#273742;padding:24px;margin:24px 0}img{width:100%;max-height:850px;object-fit:contain}</style><h1>校園戶外運動與學生公園</h1><p>800m指第一道測量線的一圈長度；8道×1.22m，彎道內緣半徑55m，直道 '+f'{S:.3f}'+'m。這是遊戲擴充規格，不是台科大實測或官方競賽800m跑道。</p><p>4面網球、4面排球、68×105m活動草坪、閱讀公園及社團活動公園。河東擴充由既有橋梁接入，160m長步道以1:20下降8m。模型為靜態環境；球類規則、活動和互動控制另需遊戲實作。</p><p><a href="../index.html">完整校園</a> · <a href="RESEARCH.md">官方參考與尺寸依據</a> · <a href="acceptance.json">實際 remesh 幾何驗收</a> · <a href="unreal-acceptance.json">Unreal 戶外碰撞驗收</a> · <a href="programme.json">座標與完整規格</a> · <a href="interactions.json">逐設施互動規畫</a></p>'+''.join(cards)
 page=page.replace('<h1>','<p>校內操場位於運動館南側：400m一圈、8道、90×60m足球場，北側步道連到校園主路；河東800m園區保留。</p><h1>',1)
 if (OUT/'tripo-pending.json').exists():page=page.replace('<h1>','<p>公園目前以棚架遮蔭；Tripo樹木任務已送出，但Chrome斷線後尚未下載／驗收，未列入採用模型。<a href="tripo-pending.json">任務狀態</a></p><h1>',1)
 (OUT/'index.html').write_text(page,'utf8')
 # Preserve the original city planning artwork and publish a separate metric
 # overlay for the actual new campus sites, without rescaling old buildings.
 base=OUT.parents[3]/'Docs/StreetSocialCity/production/world-image-plan.svg'
 if base.exists():
  svg=base.read_text('utf8');svg=svg.replace('width="2400"','width="2960"',1).replace('viewBox="0 0 2400 2700"','viewBox="0 0 2960 2700"',1)
  overlay=[]
  for q in sorted(a['assets'],key=lambda q:0 if q['id']=='annex-ground' else 1):
   plan=OUT/q['id']/'plan.svg'
   if not plan.exists():continue
   inner=re.sub(r'^<svg[^>]*>|</svg>$','',plan.read_text('utf8'));x,y,_=q['origin'];overlay.append(f'<g transform="translate({100+2*x},{215+2*(1100-y)}) scale(2)">{inner}</g>')
  meta=json.dumps(dict(world_size_m=[1380,1100],svg_units_per_m=2,map_origin_svg=[100,215],base_reference_sha256=hashlib.sha256(base.read_bytes()).hexdigest(),scope='Original city planning reference plus current accepted campus outdoor overlay'))
  svg=re.sub(r'<metadata id="world-metre-contract">.*?</metadata>','<metadata id="world-metre-contract">'+html.escape(meta)+'</metadata>',svg,flags=re.S)
  svg=re.sub(r'<desc>.*?</desc>','<desc>原城市規畫底圖與目前校園戶外擴充；世界包圍範圍1380×1100m。河東800m操場、四面網球、四面排球、學生公園與連橋步道依實際模型座標疊加。</desc>',svg,count=1,flags=re.S)
  svg=svg.replace('</svg>','<metadata id="campus-outdoor-world-contract">'+html.escape(meta)+'</metadata>'+''.join(overlay)+'</svg>')
  (OUT/'world-plan.svg').write_text(svg,'utf8');page=page.replace('<h1>','<p><a href="world-plan.svg">校園戶外延伸總圖 SVG</a></p><h1>',1);(OUT/'index.html').write_text(page,'utf8')
 campus=OUT.parent/'index.html';text=campus.read_text('utf8');marker='<p id="outdoor-campus"><a href="outdoor/index.html">校內400m操場／足球場、河東800m園區、4面網球、4面排球與學生公園（模型／平面／驗收）</a></p>'
 if 'id="outdoor-campus"' not in text:text=text.replace('<h1>',marker+'<h1>',1)
 else:text=re.sub(r'<p id="outdoor-campus">.*?</p>',marker,text,flags=re.S)
 campus.write_text(text,'utf8')
if __name__=='__main__':publish()
