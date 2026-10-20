"""Publish exact metric plans and accepted campus evidence, never proxy images."""
import json,html,hashlib,math
from pathlib import Path
from street_city_campus_contract import IDS,OUT,context,digest
def esc(s):return html.escape(str(s))
def publish(require_all=True):
 rows=[];errors=[];offices=[];programme_index=[]
 for bid in IDS:
  c=context(bid);folder=c['out'];accept=folder/'acceptance.json'
  if not accept.exists():errors.append(bid+' missing acceptance');continue
  a=json.loads(accept.read_text('utf8'));r=json.loads((folder/'assembly-report.json').read_text('utf8'));g=json.loads((folder/'structure-plan.json').read_text('utf8'))
  model=folder/f'{bid}-complete-2k.glb'
  if a['status']!='passed' or r['contract_sha256']!=digest(c) or hashlib.sha256(model.read_bytes()).hexdigest()!=a['sha256']:errors.append(bid+' rejected/stale model');continue
  W,D=c['building']['footprint_m'];floorrows=[]
  programme=[dict(id=q['room_id'],floor=q['floor'],name=q['display_name'],clear_size_m=q['clear_size_m'],position_m=q['position_m'],interactions=q['interactions'],units=q['campus_units']) for q in c['rooms'].values()]
  for q in programme:
   programme_index.append(dict(q,building=bid))
   if c['rooms'][q['id']].get('functional_programme',{}).get('kind',c['rooms'][q['id']].get('room_type')) in ('admin','administration'):offices.append(dict(q,building=bid,world_position_m=[q['position_m'][k]+c['building']['world_position_m'][k] for k in range(3)],scope='cross-building support' if '跨館' in q['name'] or '聯合服務' in q['name'] else 'game campus office/service space'))
  auxiliary=[dict(id=i['asset_id'],floor=i['floor'],name=c['assets'][i['asset_id']]['name'],role=c['assets'][i['asset_id']]['role'],size_m=c['assets'][i['asset_id']]['size_m'],position_m=i['position_m'],yaw_deg=i['rotation_z_deg']) for i in c['instances'] if c['assets'][i['asset_id']]['role'] in ('annex_room','support_room','public_restroom','circulation')]
  (folder/'room-programme.json').write_text(json.dumps(dict(building=bid,rooms=programme,auxiliary_spaces=auxiliary,references=c['references'],dimension_basis=c['dimension_basis']),ensure_ascii=False,indent=2),'utf8')
  for f in c['building']['floors']:
   no=f['floor'];z=f['z_m'];draw=[]
   def rect(x,y,w,d,fill,stroke='none',sw=.03):return f'<rect x="{x-w/2}" y="{-y-d/2}" width="{w}" height="{d}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}"/>'
   draw.append(rect(0,0,W,D,'#dce5e2','#4b5357',.1))
   for q in g['parts']:
    if q['kind']=='floor' and abs(q['pos'][2]+q['size'][2]-z)<.001:
     x,y,_=q['pos'];w,d,_=q['size'];draw.append(rect(x,y,w,d,'#f6f1e7'))
   for q in g['parts']:
    x,y,zz=q['pos'];w,d,h=q['size']
    if q['kind']=='wall' and zz<=z+1<zz+h:draw.append(rect(x,y,w,d,'#47535b'))
    elif q['floor']==no and q['kind'] in ('stair_tread','stair_landing'):draw.append(rect(x,y,w,d,'#ccd5df','#7c8b99',.025))
   for i in r['instances']:
    if i['floor']!=no:continue
    x,y,_=i['position_m'];asset=c['assets'][i['asset']];w,d=asset['size_m'][:2]
    draw.append(f'<g transform="translate({x},{-y}) rotate({-i["yaw_deg"]})">'+rect(0,0,w,d,'#d7c5a8','#a39179',.025)+'</g>')
   for room in c['rooms'].values():
    if room['floor']!=no:continue
    x,y,_=room['position_m'];draw.append(f'<text x="{x}" y="{-y}" text-anchor="middle" font-size=".65" paint-order="stroke" stroke="#fff" stroke-width=".16">{esc(room["room_id"])} {esc(room["display_name"])}</text>')
   for core in f['vertical_circulation']:
    x,y,_=core['position_m'];label={'stairs':'樓梯','lift':'電梯','restroom':'公共廁所','lobby':'梯廳'}.get(core['kind'],core['kind'])+str(core.get('core',''));cw,cd=core['size_m'][:2];draw.append(rect(x,y,2.8,1.2,'#ffffffcc'));draw.append(f'<text x="{x}" y="{-y-.1}" text-anchor="middle" font-size=".5">{esc(label)}</text><text x="{x}" y="{-y+.45}" text-anchor="middle" font-size=".36">{cw:g} × {cd:g} m</text>')
   for door in r['doors']:
    if door['floor']!=no:continue
    x,y,_=door['position_m'];w=door['width_m'];yaw=door['yaw_deg']
    # Architectural swing; large entrances use two 1.2m leaves.
    if door.get('kind')=='lift':shape=rect(0,0,w,.08,'#3486b4')
    else:
     leaves=[(-w/2,w)] if w<=1.5 else [(-w/2,w/2),(w/2,-w/2)]
     shape=''.join(f'<path d="M {hinge},0 l 0,{-abs(leaf)} M {hinge+leaf},0 A {abs(leaf)},{abs(leaf)} 0 0,{1 if leaf>0 else 0} {hinge},{-abs(leaf)}" fill="none" stroke="#a85d32" stroke-width=".045"/>' for hinge,leaf in leaves)
    draw.append(f'<g transform="translate({x},{-y}) rotate({-yaw})">{shape}</g>')
   top=-D/2-4;bottom=D/2+3
   draw.append(f'<text x="{-W/2}" y="{top}" font-size="1.2">{esc(bid)} F{no:02d}　外牆 {W:g} × {D:g} m　層高 {f["height_m"]:g} m</text>')
   for length,dy in [(1,0),(5,1.4),(10,2.8)]:
    xx=-W/2;yy=bottom+dy;draw.append(f'<path d="M {xx},{yy} h {length} m {-length},-.2 v .4 m {length},0 v -.4" fill="none" stroke="#1c313b" stroke-width=".06"/><text x="{xx+length+.5}" y="{yy+.25}" font-size=".7">{length} m</text>')
   path=folder/f'{bid}-F{no:02d}-plan.svg';path.write_text(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{-W/2-2} {-D/2-7} {W+4} {D+16}" width="1600" height="1200"><style>text{{font-family:Microsoft JhengHei,sans-serif;fill:#263f4c}}</style>'+''.join(draw)+'</svg>','utf8')
   floorrows.append(dict(floor=no,triangles=a['floor_triangles'][str(no)],plan=path.name,model=f'{bid}-F{no:02d}-complete-2k.glb'))
  rows.append(dict(id=bid,units=c['units'],footprint_m=[W,D],floors=floorrows,checks=a['checks'],model=model.name,sha256=a['sha256'],world_position_m=c['building']['world_position_m']))
 status='passed' if len(rows)==17 and not errors else 'in_progress';data=dict(status=status,buildings=rows,building_count=len(rows),room_count=len(programme_index),floor_count=sum(len(r['floors']) for r in rows),errors=errors,scope='Static modular environment; gameplay door/lift controls require integration')
 (OUT/'campus-acceptance.json').write_text(json.dumps(data,ensure_ascii=False,indent=2),'utf8')
 (OUT/'department-offices.json').write_text(json.dumps(dict(scope='Actual room IDs in the game campus; adapted placements are not real-world navigation addresses',offices=offices),ensure_ascii=False,indent=2),'utf8')
 (OUT/'OFFICES.md').write_text('# 遊戲校園辦公室與行政服務位置\n\n學生中心二樓SC-0201為學務處學生服務中心；AD-0201為聯合服務窗口。資訊工程主系辦規畫在T4-0506，EE-0306為跨館專題支援。保留既有遊戲地塊而合併的系所，以及樓層門牌，皆屬遊戲改編；參考RB-405的設計系辦在本案使用RB-0406門牌。\n\n| 建物 | 樓層 | 房間 | 用途 |\n|---|---|---|---|\n'+''.join(f'| {q["building"]} | {q["floor"]}F | [{q["id"]}](buildings/{q["building"]}/{q["building"]}-F{q["floor"]:02d}-plan.svg) | {q["name"]} |\n' for q in offices),'utf8')
 canonical=Path('C:/portable/OpenKoikatsu/Docs/StreetSocialCity/production/campus-programme.json');canonical.write_text(json.dumps(dict(status=status,source='current accepted campus adapter; supplements the original city planning manifest',buildings=rows,rooms=programme_index,offices=offices),ensure_ascii=False,indent=2),'utf8')
 cards=[]
 for r in rows:
  bid=r['id'];prefix='buildings/'+bid+'/'
  cards.append(f'<article><h2>{bid} · {esc("／".join(r["units"]))}</h2><p>{r["footprint_m"][0]:g} × {r["footprint_m"][1]:g} m · {len(r["floors"])} 層 · {r["checks"]:,} 項檢查</p><img src="{prefix}building-preview.png" alt="{bid} 實際模型外觀"><details><summary>首層剖視</summary><img src="{prefix}interior-preview.png"></details><p><a href="{prefix}room-programme.json">逐室用途／互動規畫</a> · <a href="{prefix}{r["model"]}">完整 GLB</a> · <a href="{prefix}{bid}-complete-2k.blend">Blender</a></p><table><tr><th>樓層</th><th>平面圖</th><th>模型</th><th>三角面</th></tr>'+''.join(f'<tr><td>F{f["floor"]:02d}</td><td><a href="{prefix}{f["plan"]}">尺寸／房間／門／比例尺</a></td><td><a href="{prefix}{f["model"]}">GLB</a></td><td>{f["triangles"]:,}</td></tr>' for f in r['floors'])+'</table></article>')
 (OUT/'index.html').write_text('<!doctype html><meta charset="utf-8"><title>大學城校園模型驗收</title><style>body{margin:30px auto;max-width:1450px;background:#18232b;color:#edf2ed;font:17px Microsoft JhengHei,sans-serif}a{color:#97dfc9}article{background:#273742;padding:24px;margin:20px 0;border-radius:14px}img{width:100%;max-height:850px;object-fit:contain;background:#bcc8cd}table{width:100%;border-collapse:collapse}td,th{padding:10px;border-bottom:1px solid #536572;text-align:left}</style><h1>台科大優先參考 · 大學城校園</h1><p>'+str(len(rows))+' 棟／'+str(data['floor_count'])+' 層／'+str(len(programme_index))+' 個主要空間。模型以既有遊戲地塊的公尺尺寸製作，院系與設施參考官方資料，並非台科大實測復刻。每層不超過十萬三角面；元件皆先經 Rust remesh；貼圖最高 2K。</p><p><a href="OFFICES.md">系辦與行政服務位置</a> · <a href="RESEARCH.md">台科大研究依據</a> · <a href="RESEARCH-SUPPLEMENT.md">臺大／清大交叉參考</a> · <a href="delivery-acceptance.json">最新完整交付驗收</a> · <a href="../../../model3d.md">製作方法</a> · <a href="campus-acceptance.json">建物驗收</a> · <a href="world-acceptance.json">完整場景驗收</a> · <a href="unreal-acceptance.json">Unreal 碰撞驗收</a> · <a href="campus-complete-2k.blend">完整校園 Blender</a> · <a href="campus-complete-2k.glb">完整校園 GLB</a></p><p>靜態環境已建置；門與電梯有原始動作資料，遊戲互動控制不列為已完成。</p><img src="campus-overview.png" alt="實際校園模型全景">'+''.join(cards),'utf8')
 from publish_street_city_campus_sign_link import publish as publish_sign_link
 publish_sign_link()
 if require_all:assert status=='passed',errors
 return data
if __name__=='__main__':print(publish()['status'])
