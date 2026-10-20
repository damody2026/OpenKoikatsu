"""Publish any reviewed building profile only after geometry and identity gates."""
import json,hashlib,argparse
from pathlib import Path
from street_city_building_context import context,ROOT
p=argparse.ArgumentParser();p.add_argument('--building',default='S01');a=p.parse_args();C=context(a.building);BID=C['id'];OUT=C['out'];BASE=OUT.parent
def read(path):return json.loads(path.read_text('utf-8'))
report=read(OUT/'acceptance.json');assembly=read(OUT/'assembly-report.json')
assert Path(report['source']).resolve()==(OUT/f'{BID}-complete-2k.glb').resolve(), 'Wrong building publication target'
assert assembly.get('building',BID)==BID,'Wrong building identity'
assert report['status']=='passed' and hashlib.sha256(Path(report['source']).read_bytes()).hexdigest()==report['sha256']
catalog=[]
for aid,s in assembly['sources'].items():
 assert hashlib.sha256(Path(s['path']).read_bytes()).hexdigest()==s['sha256'],aid
 r=read(Path(s['remesh_report']));assert r['status']=='passed',aid
 source=Path(r['source']);source=source if source.is_absolute() else ROOT/source
 assert hashlib.sha256(source.read_bytes()).hexdigest()==r['source_sha256'],aid
 if str(OUT) in s['path']:
  r.update(adopted=True,adoption_scope=BID+' exported full building accepted');Path(s['remesh_report']).write_text(json.dumps(r,ensure_ascii=False,indent=2),'utf-8')
 catalog.append(dict(id=aid,triangles_per_asset=r['triangles'],triangle_cap=r['triangle_cap'],instances=sum(i['asset']==aid for i in assembly['instances']),source=s,reused=str(OUT) not in s['path']))
assembly.update(status='passed_exported_full_building_geometry',approved=True,acceptance=str(OUT/'acceptance.json'));(OUT/'assembly-report.json').write_text(json.dumps(assembly,ensure_ascii=False,indent=2),'utf-8')
(BASE/'component-catalog.json').write_text(json.dumps(dict(building=BID,assets=catalog),ensure_ascii=False,indent=2),'utf-8')
names=['咖啡書屋','共讀空間','店主住宅'] if BID=='S01' else ['點餐製飲、取餐與等候','備料、冷藏、乾貨與包裝','員工休息、午睡、茶水及淋浴']
title='咖啡書屋' if BID=='S01' else '沐茶手搖飲店'
style='body{max-width:1250px;margin:30px auto;padding:20px;font:17px/1.7 system-ui;background:#f4f1eb;color:#27352d}a{color:#176453}img{width:100%;height:auto;border-radius:10px}td,th{padding:12px;border-bottom:1px solid #ddd;text-align:left}table{width:100%;background:white}.status{background:#dcece1;padding:20px;border-radius:10px}.note{background:#fff0cc;padding:18px}'
rows=''.join(f'<tr><td>F{int(no):02d}</td><td>{names[int(no)-1]}</td><td>{count:,} / 100,000</td><td><a href="{BID}-F{int(no):02d}-complete-2k.glb" download>樓層GLB</a></td></tr>' for no,count in report['floor_triangles'].items())
page=f'''<!doctype html><html lang="zh-Hant"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>{BID} {title}</title><style>{style}</style><h1>{BID}｜三層{title}</h1><p class="status">{C['width']}×{C['depth']}m，完成面Z=0／3.8／7.6m，屋頂11m；整棟{report['triangles']:,}三角面，每層低於十萬。{report['checks']:,}項實際匯出檢查通過。</p><p><a href="{BID}-complete-2k.blend" download>完整Blender</a> · <a href="{BID}-complete-2k.glb" download>完整GLB含電梯動畫</a> · <a href="../../../../model3d.md">model3d.md</a> · <a href="acceptance.json">逐項驗收</a> · <a href="assembly-report.json">來源與擺放</a> · <a href="../component-catalog.json">元件及複用紀錄</a></p><img src="{BID}-exterior.png" alt="完整建築"><table><tr><th>樓層</th><th>內容</th><th>實測三角面</th><th>靜態模型</th></tr>{rows}</table><p>共用樓板僅一份；真實樓梯連接一至三樓，頂層不生成撞向屋頂的多餘梯段。樓梯孔護欄、正門2.4m、一般門1.2m、走廊至少2m、貼圖最大2K。</p>'''
if BID=='S02':
 finishes=read(OUT/'material-report.json')
 for name,sha in finishes['files'].items():assert hashlib.sha256((OUT/'materials'/name).read_bytes()).hexdigest()==sha,'Material identity '+name
 page+='''<p>一樓有收銀、取餐、封口、調茶、糖漿定量、製冰、雙槽洗滌、獨立洗手及等候座位。二樓為原料冷藏、乾貨架、備製水槽與包裝桌；三樓有獨立午睡室、衣櫃、起居、書桌及茶水區。各層有衛浴與照明。水槽以獨立底板、側板製作真正凹槽，沒有實心檯面封住盆口。</p><p>配置參考<a href="https://youfondit.com/shop/custom-beverage-bar-counter/">永發飲料吧檯規畫</a>；封口機尺度參考<a href="https://zh-tw.phoenixes.com.tw/pdf/507130/PH-98S-Information%20TW-2023.pdf">PH-98S原廠資料</a>。設備為遊戲用板件簡化造型，並非原廠產品精確複製。複用S01已驗收曲面資產，沒有新增Tripo生成或扣點。</p><h2>材質與實際尺度</h2><p>地板及踏階：60×60公分霧面磁磚、3mm填縫。衛浴核心牆：30×60公分壁磚、2mm填縫。其餘牆面：暖白可清潔塗料。全部使用原創程序式2K底色、粗糙度與法線貼圖，公尺UV連續鋪設，並檢查GLB重新匯入結果；未宣稱實測產品掃描或止滑認證。</p><p>尺寸與選材參考<a href="https://www.champion.com.tw/product/1483">台灣冠軍磁磚60×60cm產品</a>，並非複製該產品花色。<a href="material-report.json">材質規格及貼圖來源雜湊</a></p>'''
for floor in [1,2,3]:page+=f'<h2>F{floor:02d} 剖視與俯視</h2><img src="{BID}-F{floor:02d}-cutaway.png" alt="室內剖視"><img src="{BID}-F{floor:02d}-plan.png" alt="實際模型俯視"><p>預覽隱藏其他樓層、部分外牆及燈具；下載模型保留完整幾何。</p>'
page+='<h2>動畫與限制</h2><p>只有一部車廂，三層樓層門及車廂門共用同步動畫clip。1、80、160影格停靠，40、120為移動中關門檢查姿態；單層GLB為靜態模型。</p><p class="note">遊戲控制器、角色導航與碰撞系統尚未接入。複用衛浴含已記錄的來源拓樸修復；椅子和馬桶保留來源開口接縫，未宣稱封閉實體。幾何驗收不等同角色實際遊玩測試。</p></html>'
page=page.replace('</html>','<p><a href="measured-instances.json">每件家具與設備實測長寬高、位置及互動規畫</a></p></html>')
(OUT/'index.html').write_text(page,'utf-8')
if BID!='S01':(BASE/'index.html').write_text(f'<!doctype html><meta charset="utf-8"><h1>{BID} {title}</h1><a href="assembly-building/index.html">完整三層建築與驗收</a>','utf-8')
md=ROOT/'model3d.md';text=md.read_text('utf-8');heading=f'\n\n## {BID} 整棟本次實測';start=text.find(heading)
if start!=-1:
 end=text.find('\n\n## ',start+len(heading));text=text[:start]+(text[end:] if end!=-1 else '')
text+=heading+f"\n\n整棟{report['triangles']:,}三角面；"+'、'.join(f"F{int(k):02d}={v:,}" for k,v in report['floor_triangles'].items())+f"。{report['checks']:,}項實際匯出檢查通過。資料位於 `output/street-social-city-production/{BID}-modular-components-v1/assembly-building/`。動畫已保留；遊戲控制器尚未接入。\n"
md.write_text(text,'utf-8');print(json.dumps(dict(building=BID,status='published',triangles=report['triangles'],checks=report['checks'])))
