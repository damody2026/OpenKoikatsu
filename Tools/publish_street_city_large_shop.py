"""Publish only the exact large-building export that independently passed QA."""
import sys,json,html
from pathlib import Path
from street_city_large_shop_contract import context,contract_hash
from street_city_remesh_gate import verify,sha
from street_city_shop_street import publish
C=context(sys.argv[1]);OUT=C['out'];bid=C['id'];r=json.loads((OUT/'assembly-report.json').read_text('utf-8'));a=json.loads((OUT/'acceptance.json').read_text('utf-8'));assert a['status']=='passed';assert sha(OUT/f'{bid}-complete-2k.glb')==a['sha256']
if bid=='S19':assert 'large-spa-basin-v2' in r['sources'],'Private pools require physical entry/exit steps before publication'
assert r.get('contract_sha256')==contract_hash(C),'Large-building geometry contract changed; rebuild before publishing'
for aid,s in r['sources'].items():
 verify(Path(s['remesh_report']).parent,s['sha256']);folder=Path(s['remesh_report']).parent/'adoptions';folder.mkdir(exist_ok=True);(folder/f'{bid}.json').write_text(json.dumps(dict(building=bid,asset=aid,remeshed_sha256=s['sha256'],accepted_building_sha256=a['sha256'],acceptance=str(OUT/'acceptance.json'),status='adopted_in_accepted_building'),indent=2),'utf-8')
r.update(approved=True,status='passed_exported_large_building_geometry');(OUT/'assembly-report.json').write_text(json.dumps(r,ensure_ascii=False,indent=2),'utf-8')
name={'S18':'日式民宿','S19':'溫泉會館'}[bid];W,D=C['building']['footprint_m'];page=f'<!doctype html><html lang="zh-Hant"><meta charset="utf-8"><title>{bid} {name}</title><style>body{{font:17px/1.7 system-ui;max-width:1100px;margin:30px auto;background:#f4f1eb}}img{{width:100%}}td{{padding:12px}}</style><h1>{bid} {name}</h1><p>外框{W}×{D}m。{a["checks"]}項實際完整GLB驗收通過，所有元件先經指定Rust remesh。</p><p><a href="{bid}-complete-2k.blend">Blender</a> · <a href="{bid}-complete-2k.glb">完整GLB</a> · <a href="acceptance.json">實際驗收</a> · <a href="assembly-report.json">元件來源與尺寸</a> · <a href="routes.json">通道</a> · <a href="../../shop-street-v1/index.html">商店街總覽</a></p><img src="{bid}-exterior.png"><table>'
for f in C['building']['floors']:
 no=f['floor'];page+=f'<tr><td>F{no:02d}</td><td>Z={f["z_m"]}m，樓高{f["height_m"]}m</td><td>{a["floor_triangles"][str(no)]:,}三角面／100,000上限</td><td><a href="{bid}-F{no:02d}-complete-2k.glb">樓層GLB</a></td></tr>'
page+='</table><p>公共通道2m、一般門1.2m、主入口2.4m；各層衛浴與上下連續的樓梯，單一三站電梯。室內家具依各自用途配置，木板可組裝元件本機建構，曲面衛浴複用既有Tripo來源。未接入遊戲控制器、碰撞與導航。</p>'
for f in C['building']['floors']:no=f['floor'];page+=f'<h2>F{no:02d}</h2><img src="{bid}-F{no:02d}-cutaway.png">'
(OUT/'index.html').write_text(page+'</html>','utf-8');publish()

from publish_street_city_shop_wall_styles import decorate_review
decorate_review(bid,OUT)
