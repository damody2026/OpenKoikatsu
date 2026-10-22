"""Publish current, evidence-backed static world delivery and actual model previews."""
import json,html,os,sys
from pathlib import Path
from PIL import Image,ImageOps,ImageDraw
from street_city_remaining_contract import ROOT,OUT,IDS,context,digest
from street_city_remesh_gate import sha

def read(p):return json.loads(p.read_text('utf8'))
def link(p,label):
 return '<a href="'+html.escape(os.path.relpath(p,OUT).replace('\\','/'),quote=True)+'">'+html.escape(label)+'</a>'
def image(p):return '<img loading="lazy" src="'+os.path.relpath(p,OUT).replace('\\','/')+'">'
def main():
 native=read(OUT/'native-collision-acceptance.json');scene=read(OUT.parent/'scene-integration-v1/acceptance.json');outdoors=read(OUT/'outdoor/assembly-report.json');ground=read(OUT/'ground/acceptance.json')
 assert native['status']==scene['status']==ground['status']=='passed'
 assert native['map_sha256']==scene['map_sha256']==sha(ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap')
 renders=read(OUT/'floor-render-acceptance.json');assert renders['status']=='rendered_actual_models' and renders['floors']==40
 for q in renders['images']:assert q['source_sha256']==sha(OUT/'buildings'/q['building']/(q['building']+'-complete-2k.glb')) and q['image_sha256']==sha(q['image'])
 ore=read(OUT/'outdoor/render-receipt.json');models={q['id']:q for q in outdoors['models']}
 for q in ore['images']:assert q['source_sha256']==models[q['id']]['sha256'] and q['image_sha256']==sha(q['image'])
 for group,paths in [('floors',sorted((OUT/'buildings').glob('*/F*-review.png'))),('outdoor',sorted((OUT/'outdoor').glob('PK-*-preview.png'))),('buildings',sorted((OUT/'buildings').glob('*/building-preview.png')))]:
  w,h=500,375;cols=4;sheet=Image.new('RGB',(w*cols,(len(paths)+cols-1)//cols*h),'#ddd');draw=ImageDraw.Draw(sheet)
  for i,p in enumerate(paths):
   img=Image.open(p).convert('RGB');thumb=ImageOps.contain(img,(w,h-25));x=(i%cols)*w;y=(i//cols)*h;sheet.paste(thumb,(x+(w-thumb.width)//2,y));draw.text((x+8,y+h-23),p.parent.name+'/'+p.stem,fill='black')
  sheet.save(OUT/(group+'-contact-sheet.jpg'),quality=92)
 cards=[];checks=0;maximum=0
 names={b['id']:b['name'] for b in read(ROOT/'Docs/StreetSocialCity/production/building-floor-audit.json')['buildings']}
 units={'H1':40,'R1':40,'R2':40,'O01':24,'O02':24,'O03':24,'CV':30,'TR':10}
 for bid in IDS:
  c=context(bid);b=c['building'];folder=c['out'];r=read(folder/'assembly-report.json');a=read(folder/'acceptance.json');assert a['status']=='passed' and r['contract_sha256']==digest(c);checks+=a['checks'];maximum=max(maximum,max(r['floor_triangles'].values()))
  size=r['footprint_m'];title=f"{bid} · {names[bid]} · {size[0]}×{size[1]} m · {len(b['floors'])} 層 · {units[bid]} 個主要空間"
  body='<article><h2>'+html.escape(title)+'</h2>'+image(folder/'building-preview.png')+'<p>'+link(folder/(bid+'-complete-2k.blend'),'Blender 完整建築')+' · '+link(folder/(bid+'-complete-2k.glb'),'GLB 完整建築')+' · '+link(folder/'acceptance.json','尺寸／門洞／動線驗收')+'</p><div class="floors">'
  body+= '<p>'+link(OUT.parent/'localization-v1/models'/(bid+'-three-languages.glb'),'三語材質變體 GLB')+'</p>'
  if (folder/(bid+'-water-2k.glb')).exists():body+='<p>'+link(folder/(bid+'-water-2k.glb'),'獨立水面模型（NoCollision）')+'</p>'
  for f in b['floors']:
   no=f['floor'];png=folder/f'F{no:02d}-review.png';body+='<figure>'+link(png,f'{no}F 放大')+image(png)+'<figcaption>'+f"樓高 {f['height_m']}m · {r['floor_triangles'][str(no)]:,} triangles · "+link(folder/f'{bid}-F{no:02d}-complete-2k.glb','本層模型')+'</figcaption></figure>'
  cards.append(body+'</div></article>')
 outdoorcards=[]
 for q in outdoors['nodes']:
  fid=q['id'];o=q['origin'];body='<article><h3>'+html.escape(fid+' '+q['name'])+'</h3><p>位置 '+', '.join(f'{v:.2f}' for v in o)+' m</p>'
  if q.get('existing_asset'):body+='<p>複用已完成設施：'+html.escape(q['existing_feature'])+'</p>'
  else:
   m=models[fid];body+=image(OUT/'outdoor'/(fid+'-preview.png'))+'<p>'+link(Path(m['source']),'GLB')+' · '+link(Path(m['source']).with_suffix('.blend'),'Blender')+f" · {m['triangles']:,} triangles</p>"
  outdoorcards.append(body+'</article>')
 stats=dict(status='passed',scope='static models only; gameplay intentionally deferred',buildings=8,floors=40,main_indoor_units=232,outdoor_nodes=20,new_outdoor_models=34,regional_tree_instances=outdoors['vegetation_instances'],building_geometry_checks=checks,native_collision_checks=native['checks'],scene_checks=scene['checks'],total_world_buildings=122,total_world_floors=scene['floor_actors'],total_model_actors=scene['model_actors'],max_triangles_per_floor=maximum,map_sha256=scene['map_sha256'],receipts={str(p.relative_to(ROOT)):sha(p) for p in [OUT/'native-collision-acceptance.json',OUT/'ground/acceptance.json',OUT/'outdoor/acceptance.json',OUT.parent/'scene-integration-v1/acceptance.json']})
 (OUT/'acceptance.json').write_text(json.dumps(stats,ensure_ascii=False,indent=2),'utf8')
 top='<h1>大學城・建築與戶外模型補齊</h1><p>8 棟 / 40 層 / 232 個主要室內空間；20 個戶外節點。以下全部為實際 Blender 模型渲染。新增場景已整合 Unreal；本次只製作靜態模型。</p><p>完整世界：122 棟、427 層、495 個模型 Actor。新樓層最高 '+f'{maximum:,}'+' triangles；材質最高 2K。沿用實際 Rust remesh 元件，再以公尺組裝。</p><p>'+link(OUT/'acceptance.json','完整驗收')+' · '+link(OUT/'native-collision-acceptance.json','Unreal 實際碰撞')+' · '+link(OUT.parent/'scene-integration-v1/acceptance.json','場景與三語切換')+' · '+link(OUT/'RESEARCH.md','臺灣場地參考')+' · '+link(ROOT/'Docs/StreetSocialCity/ERRORS_AND_PREVENTION.md','錯誤與預防紀錄')+' · '+link(ROOT/'model3d.md','建模方法')+'</p><p>三語採用共用幾何與語言材質切換：繁體、簡體、英文。遊戲互動、電梯控制與購物等依指示留待後續。</p>'
 page='<!doctype html><html lang="zh-Hant"><meta charset="utf-8"><title>大學城模型補齊驗收</title><style>body{margin:30px auto;padding:0 22px;max-width:1600px;font:17px/1.65 system-ui;background:#f4f2eb;color:#252b29}a{color:#236658}article{background:white;padding:22px;margin:24px 0;border-radius:16px}img{width:100%;height:auto;border-radius:8px}h1,h2,h3{line-height:1.3}.floors{display:grid;grid-template-columns:repeat(auto-fit,minmax(330px,1fr));gap:16px}figure{margin:0}figcaption{font-size:14px}.outdoors{display:grid;grid-template-columns:repeat(auto-fit,minmax(370px,1fr));gap:18px}.outdoors article{margin:0}</style>'+top+''.join(cards)+'<h2>20 個戶外節點</h2><div class="outdoors">'+''.join(outdoorcards)+'</div><h2>森林・河岸・海灣</h2><p>16 組區域植栽模型，448 株實體樹；節點內另有櫻花樹與樟樹。釣魚平台、神社、露營衛浴、舞台、廣場、自行車設施皆有模型。</p><ul>'+''.join('<li>'+link(Path(m['source']),m['id'])+'</li>' for m in outdoors['models'] if not m['id'].startswith('PK-'))+'</ul></html>'
 (OUT/'index.html').write_text(page,'utf8');print(json.dumps(stats,ensure_ascii=False,indent=2))
if __name__=='__main__':main()

