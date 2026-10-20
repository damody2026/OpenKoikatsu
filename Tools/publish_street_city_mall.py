"""Floor-by-floor metric SVG and browsable model delivery."""
import json,html,math
from street_city_mall import OUT,ROOT,sha
def publish():
 G=json.loads((OUT/'geometry.json').read_text('utf8'));C=G['contract'];cards=[]
 for f in C['floors']:
  n=f['floor'];z=f['z_m'];draw=['<rect x="-75" y="-50" width="150" height="100" fill="#eee8da"/>']
  for s in [q for q in C['rooms'] if q['floor']==n]:
   a,b,c,d=s['bounds'];front=b+1.2 if (b+d)>0 else d-1.2;draw.append(f'<rect x="{a}" y="{-d}" width="{c-a}" height="{d-b}" fill="#decfa7" stroke="#9b8e71" stroke-width=".08"/><text x="{(a+c)/2}" y="{-front}" text-anchor="middle" font-size="1.1">{html.escape(s["id"]+" "+s["name"])}</text>')
  if n>1:draw.append('<rect x="-12" y="-12" width="24" height="24" fill="#9dc9d3"/><text y="0" text-anchor="middle" font-size="1.3">中庭挑空</text>')
  for q in G['parts']:
   if q['floor']!=n or q['kind'] not in ('wall','stair_tread','stair_landing','escalator_tread','escalator_landing','column','lift_barrier','furniture','wc_partition','wc_door','lift_car','void_guard','handrail','entrance_ramp','boutique_wall','boutique_display','boutique_detail','stock_fixture','merchandise_member'):continue
   if q['kind'].startswith('boutique_') and q['pos'][2]-z>2.5:continue
   x,y,_=q['pos'];w,d,h=q['size'];yaw=q.get('yaw',0);color='#384b52' if q['kind']=='wall' else '#989f9a';draw.append(f'<rect x="{x-w/2}" y="{-y-d/2}" width="{w}" height="{d}" fill="{color}" transform="rotate({-yaw},{x},{-y})"/>')
  for fixture in G['curves']:
   if fixture['floor']==n and fixture['asset'].startswith('product-'):
    x,y,_=fixture['pos'];w,d=fixture['size_m'][:2];draw.append(f'<g transform="translate({x},{-y}) rotate({-fixture["yaw"]})"><title>{html.escape(fixture["tenant_id"]+" "+fixture["product_id"])}</title><rect x="{-w/2}" y="{-d/2}" width="{w}" height="{d}" fill="#ac6845"/></g>');continue
   if fixture['floor']!=n or fixture['asset'] not in ('toilet','basin','urinal'):continue
   x,y,_=fixture['pos'];w,d={'toilet':(.4,.72),'basin':(.6,.5),'urinal':(.42,.35)}[fixture['asset']];draw.append(f'<g transform="translate({x},{-y}) rotate({-fixture["yaw"]})"><rect x="{-w/2}" y="{-d/2}" width="{w}" height="{d}" rx=".08" fill="#edf6f4" stroke="#517983" stroke-width=".05"/><ellipse rx="{w*.3}" ry="{d*.3}" fill="#bdd5d8"/></g>')
  for q in G['doors']:
   if q['floor']!=n:continue
   x,y,_=q['position_m'];w=q['width_m'];yaw=q['yaw_deg'];side=-1 if q['id'].startswith('entry-') else 1
   if q.get('kind')=='lift':draw.append(f'<text x="{x}" y="{-y-1}" text-anchor="middle" font-size=".8">{q["shaft"]}電梯</text>');continue
   # Plan symbols follow the same inward/outward swing as the actual model.
   if w>1.5:
    leaf=w/2;paths=''.join(f'<path d="M{sign*w/2},0 v{-side*leaf} M0,0 A{leaf},{leaf} 0 0 {int((side==1)==(sign<0))} {sign*w/2},{-side*leaf}" fill="none" stroke="#9a5d38" stroke-width=".12"/>' for sign in [-1,1]);draw.append(f'<g transform="translate({x},{-y}) rotate({-yaw})">{paths}</g>')
   else:draw.append(f'<g transform="translate({x},{-y}) rotate({-yaw})"><path d="M{-w/2},0 L{-w/2},{-side*w} M{w/2},0 A{w},{w} 0 0 {1 if side==1 else 0} {-w/2},{-side*w}" fill="none" stroke="#9a5d38" stroke-width=".12"/></g>')
  for q in G['parts']:
   if q['floor']!=n or q['kind']!='wc_door':continue
   x,y,_=q['pos'];w,d,h=q['size'];radius=max(w,d)
   if w<d:
    hy=y-radius/2;path=f'M{x},{-hy} v{-radius} M{x+radius},{-hy} A{radius},{radius} 0 0 0 {x},{-hy-radius}'
   else:
    hx=x+radius/2;path=f'M{hx},{-y} h{-radius} M{hx},{-y-radius} A{radius},{radius} 0 0 0 {hx-radius},{-y}'
   draw.append(f'<path d="{path}" stroke="#9a5d38" fill="none" stroke-width=".08"/>')
  for e in [q for q in C['escalators'] if q['lower_floor']==n]:
   direction=1 if e['direction']=='up' else -1;dx=e['slope_sign']*direction;yy=e['y'];draw.append(f'<path d="M{-dx*2},{-yy} h{dx*4} l{-dx*.7},-.35 m{dx*.7},.35 l{-dx*.7},.35" stroke="#e27b28" stroke-width=".2" fill="none"/><text x="0" y="{-yy-.2}" text-anchor="middle" font-size=".6">{e["bank"]} {e["direction"]}</text>')
  draw.append(f'<text x="-75" y="-56" font-size="2.2">M1 F{n:02d} {f["theme"]} · 外框150×100m · 層高4.2m</text><text x="-75" y="55" font-size="1.4">外牆0.2m／樓板0.3m；一般門1.2m、主門2.4m；通道≥2m</text>')
  for i,length in enumerate([1,5,10]):
   x=-75+i*20;draw.append(f'<path d="M{x},60 h{length} m{-length},-1 v2 m{length},-2 v2" stroke="#263d43" stroke-width=".16"/><text x="{x}" y="64" font-size="1.4">{length}m</text>')
  (OUT/f'M1-F{n:02d}.svg').write_text('<svg xmlns="http://www.w3.org/2000/svg" viewBox="-82 -62 164 130"><style>text{font-family:Microsoft JhengHei,sans-serif;fill:#253f46}</style>'+''.join(draw)+'</svg>','utf8')
  (ROOT/f'Docs/StreetSocialCity/production/floor-layouts/M1-F{n:02d}.svg').write_text((OUT/f'M1-F{n:02d}.svg').read_text('utf8'),'utf8')
  shops=[q for q in C['rooms'] if q['floor']==n]
  cards.append(f'<article><h2>{n}F {f["theme"]}</h2><p>'+ '、'.join(html.escape(q['name']) for q in shops)+f'</p><p><a href="M1-F{n:02d}.svg">尺寸平面與比例尺</a> · <a href="M1-F{n:02d}-complete-2k.glb">整層 GLB</a></p><img src="F{n:02d}-preview.png"></article>')
 page='<!doctype html><meta charset="utf-8"><title>M1 百貨公司</title><style>body{max-width:1400px;margin:30px auto;background:#18232b;color:#eef3ee;font:18px Microsoft JhengHei}a{color:#8fdcca}article{padding:25px;margin:24px 0;background:#273741}img{width:100%;max-height:900px;object-fit:contain}</style><h1>M1 百貨公司 · 150×100m · 10層80櫃店</h1><p>開放賣場、中庭、四角樓梯、四區各三部客梯（共12部）、兩部貨梯、四組交錯手扶梯（每組上下一對）、男女／無障礙廁所、親子哺乳室。石材基座、陶板上層、古銅金屬分格與玻璃立面；公共柱面採洞石與深色踢腳，衛浴使用瓷磚。台灣百貨用途與材質組成參考；尺寸為遊戲改編。手扶梯可沿靜態階梯步行，電梯、購物及餐飲控制器尚未接入。</p><p><a href="M1-complete-2k.blend">整棟 Blender</a> · <a href="M1-complete-2k.glb">整棟 GLB</a> · <a href="programme.json">完整規格與80櫃店互動</a> · <a href="RESEARCH.md">參考依據</a> · <a href="acceptance.json">幾何驗收</a> · <a href="unreal-acceptance.json">Unreal驗收</a> · <a href="delivery-acceptance.json">最終版本驗收</a></p><img src="building-preview.png">'+''.join(cards)
 if (OUT/'building-before-walls.png').exists():page=page.replace('<img src="building-preview.png">','<img src="building-preview.png"><details><summary>查看改版前立面（舊版渲染視角）</summary><img src="building-before-walls.png"></details>',1)
 (OUT/'index.html').write_text(page,'utf8')
 detail='<article><h2>人眼高度細節</h2>'+''.join(f'<p>{label}</p><img src="{name}">' for name,label in [('entrance-closeup-preview.png','石材入口門廊、玻璃分格與金屬立面細節'),('retail-closeup-preview.png','美妝櫃台、店招與材質'),('dining-closeup-preview.png','餐飲桌椅與廚房分隔'),('escalator-closeup-preview.png','中央手扶梯、平台與中庭')])+'</article>'
 (OUT/'index.html').write_text(page.replace('<article>',detail+'<article>',1),'utf8')
 text=(OUT/'index.html').read_text('utf8').replace('<a href="RESEARCH.md">','<a href="geometry.json">逐元件尺寸與座標</a> · <a href="assembly-report.json">組裝與remesh來源</a> · <a href="../../../model3d.md">完整製作方法</a> · <a href="RESEARCH.md">')
 (OUT/'index.html').write_text(text,'utf8')
 import xml.etree.ElementTree as ET
 whole=ET.Element('svg',xmlns='http://www.w3.org/2000/svg',viewBox='-82 -62 164 1300',width='1640',height='13000')
 for n in range(1,11):
  item=ET.fromstring((OUT/f'M1-F{n:02d}.svg').read_text('utf8'));item.set('x','-82');item.set('y',str(-62+(n-1)*130));item.set('width','164');item.set('height','130');whole.append(item)
 ET.ElementTree(whole).write(ROOT/'Docs/StreetSocialCity/production/floor-layouts/M1.svg',encoding='utf-8',xml_declaration=True)
 (OUT/'RESEARCH.md').write_text('# M1百貨公司參考與改編\n\n'+'\n'.join(f'- [{s["title"]}]({s["url"]})：{s["use"]}' for s in C['sources'])+'\n\n保留既有150×100m地塊、10層及世界座標(655,460,12)m；每層4.2m、8櫃店，非原百貨實測複刻。公共賣場不製作店門。每層兩組公廁、無障礙隔間、親子哺乳及備品管理；客貨梯分設，後側作補貨與管理空間。兩側入口、南北主入口連通環路；以四角樓梯及中央手扶梯銜接，不切場景。樓板孔、樓梯淨高、門洞及走廊均驗收實際模型。\n\n簡單板件沿用Rust remesh後的頂點，曲面衛浴及人台複用已remesh的Tripo模型；不重新生成同一家具。地板牆壁採公尺UV及2K PBR貼圖。靜態組裝不等於已完成營運、門梯控制器或消防認證。\n','utf8')
 text=(OUT/'RESEARCH.md').read_text('utf8').replace('世界座標(655,460,12)m','地形基準(655,460,12)m；建物完成面座標(655,460,12.06)m').replace('兩側入口、南北主入口連通環路','南／東／西入口連通環路，東南側設收貨門；北側因實測陡升地形取消一樓入口，各入口以≤5%坡道接地')
 text+='\n\n## 垂直動線改版\n\n四個分散的客梯廳位於西北、西南、東北、東南，每廳3部，共12部；每部井道3.2×5m、車廂2.4×3.4m，門口面向側邊公共走道。另保留北側2部貨梯。中庭24×24m，四組上下成對的交錯手扶梯，每相鄰樓層共8部、全棟72部；層高4.2m、30度、水平行程7.275m、24階、每階17.5cm、淨寬1.2m。各對固定相反實體斜率，行進方向逐層交替，到站同一端轉換相鄰梯道即可續行。中庭兩端另留2.4m轉乘走道；平台、護欄、樓板孔洞及逐層實體淨高均驗收。此為遊戲場景配置，電梯與梯級馬達控制仍待串接。\n'
 text+='\n\n## 立面與牆面改版\n\n參考寶麗廣塲官方建築介紹與環境欣賞，取石材、大片玻璃及分格層次的組成邏輯，非實測複刻。低樓層為暖色石材、上層為淺色陶板；側面深色石材搭配金屬肋；玻璃有實體金屬分格；南、東、西入口增加兩層高石材門廊、金屬框玻璃門、實體百貨招牌與門楣燈；無窗立面設跨層展示看板，不遮蔽原玻璃窗。所有構件限制於原150×100m外框，入口與收貨門保持淨空。公共柱採洞石、深色基座及金屬柱頭，公廁／廚房另用瓷磚，不再被通用白漆覆蓋。貼圖為原創程序式2K PBR，非掃描石材。\n'
 (OUT/'RESEARCH.md').write_text(text,'utf8')
 publish_boutique_link()

def publish_boutique_link():
 path=OUT/'index.html'
 if not path.exists():return
 text=path.read_text('utf8')
 if 'id="premium-boutiques"' not in text:
  text=text.replace('<h1>','<p id="premium-boutiques"><a href="boutiques-v1/index.html">80 個專櫃：精品化牆面、裝潢與展示櫃 · 實際模型逐櫃檢視</a></p><h1>',1);path.write_text(text,'utf8')
 if 'id="stocked-merchandise"' not in text:
  text=text.replace('<h1>','<p id="stocked-merchandise"><a href="merchandise-v1/stocked-stores.html">80 個專櫃：真實商品陳列、逐櫃近景及商品模型庫</a></p><h1>',1);path.write_text(text,'utf8')

if __name__=='__main__':publish();publish_boutique_link()
