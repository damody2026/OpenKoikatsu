"""Complete street inventory. A task row is never proof of a finished model."""
import json,re,html,hashlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
from street_city_building_context import load_manifest
from street_city_remesh_gate import verify as verify_remesh
from street_city_building_context import context
from street_city_programme_gate import verify_current
from street_city_atomic import write_text as atomic_write
OUT=ROOT/'output/street-social-city-production/shop-street-v1'
def inventory():
 m=load_manifest()
 return sorted([b for b in m['layouts'] if b.get('kind')=='building' and re.fullmatch(r'S\d+',b.get('building',''))],key=lambda b:int(b['building'][1:]))
def publish():
 OUT.mkdir(exist_ok=True);rows=[]
 for b in inventory():
  bid=b['building'];folder=ROOT/f'output/street-social-city-production/{bid}-modular-components-v1/assembly-building'
  name=b.get('small_shop_design',{}).get('name',{'S18':'日式民宿','S19':'溫泉會館'}.get(bid,bid))
  result=dict(id=bid,name=name,footprint_m=b['footprint_m'],status='pending',floor_count=len(b.get('small_shop_design',{}).get('floors',b.get('floors',[]))))
  path=folder/'acceptance.json'
  if path.exists():
   a=json.loads(path.read_text('utf-8'));glb=folder/f'{bid}-complete-2k.glb'
   try:
    r=json.loads((folder/'assembly-report.json').read_text('utf-8'))
    valid=a['status']=='passed' and r.get('approved') and glb.exists() and a['sha256']==hashlib.sha256(glb.read_bytes()).hexdigest()
    if valid:
     if b.get('small_shop_design'):verify_current(context(bid),r)
     for asset in r['sources'].values():verify_remesh(Path(asset['remesh_report']).parent,asset['sha256'])
     result.update(status='passed',triangles=a['triangles'],checks=a['checks'],review='../'+folder.parent.name+'/assembly-building/index.html')
   except (AssertionError,OSError,KeyError,ValueError) as error:result['pending_reason']=str(error)
  rows.append(result)
 atomic_write(OUT/'inventory.json',json.dumps(dict(count=len(rows),passed=sum(r['status']=='passed' for r in rows),buildings=rows),ensure_ascii=False,indent=2))
 page='<!doctype html><html lang="zh-Hant"><meta charset="utf-8"><title>商店街模型製作</title><style>body{font:17px/1.7 system-ui;max-width:1200px;margin:30px auto;background:#f4f1eb}td,th{padding:9px;border-bottom:1px solid #ccc;text-align:left}.passed{background:#d8eddf}</style><h1>商店街｜96 棟模型製作</h1>'
 page+=f'<p>已通過實際GLB驗收：{sum(r["status"]=="passed" for r in rows)}／{len(rows)}棟。待製作不等同完成；每棟必須有尺寸、每層室內、元件來源、remesh與實際匯出驗收。</p><p>共同標準：走廊2m、一般門1.2m、正門2.4m；每層10萬三角面；貼圖最大2K。遊戲導航、碰撞與控制器尚未接入。</p><table><tr><th>編號／店名</th><th>外框長寬</th><th>樓層</th><th>狀態</th><th>成果</th></tr>'
 try:
  world=json.loads((OUT/'world-assembly.json').read_text('utf-8'));qa=json.loads((OUT/'world-acceptance.json').read_text('utf-8'));model=OUT/'shop-street-complete.glb'
  good=qa['status']=='passed' and qa['building_count']==len(rows)==96 and qa['glb_sha256']==world['glb_sha256']==hashlib.sha256(model.read_bytes()).hexdigest() and all(r['status']=='passed' for r in rows)
  if good and all(hashlib.sha256(Path(b['source']).read_bytes()).hexdigest()==b['sha256'] for b in world['buildings']):
   hero=f'<section><h2>96棟建物合併場景｜驗收通過</h2><p><a href="shop-street-complete.blend">完整Blender場景</a> · <a href="shop-street-complete.glb">完整GLB</a> · <a href="world-acceptance.json">{qa["checks"]:,}項合併驗收</a> · <a href="world-assembly.json">世界座標與來源</a></p><p>本批交付為商店街建物；道路地形與遊戲控制器另行整合。</p><img style="width:100%" src="shop-street-overview.png"></section>'
   page=page.replace('<table><tr>',hero+'<table><tr>',1)
 except (OSError,KeyError,ValueError):pass
 page=page.replace('<table><tr>','<p><a href="../shop-wall-styles-v1/index.html">96間店的牆面風格、實際室內近景與改版前後</a></p><table><tr>',1)
 for r in rows:
  link=f'<a href="{r["review"]}">模型與驗收</a>' if r['status']=='passed' else '—'
  page+=f'<tr class="{r["status"]}"><td>{r["id"]} {html.escape(r["name"])}</td><td>{r["footprint_m"][0]}×{r["footprint_m"][1]}m</td><td>{r["floor_count"]}</td><td>{"驗收通過" if r["status"]=="passed" else "待製作"}</td><td>{link}</td></tr>'
 atomic_write(OUT/'index.html',page+'</table></html>')
if __name__=='__main__':publish()
