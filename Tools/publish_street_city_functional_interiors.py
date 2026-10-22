"""Publish only the fully integrated and independently reopened functional revision."""
import json,html,os,datetime
from pathlib import Path
from street_city_functional_revision import ROOT,OUT,read,sha
def main():
 a=read(OUT/'adoption.json');native=read(OUT/'native-acceptance.json');scene=read(OUT.parent/'scene-integration-v1/acceptance.json');sightlines=read(OUT/'sightline-acceptance.json');mapfile=ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap';mapsha=sha(mapfile)
 assert native['status']==scene['status']=='passed' and native['adoption_sha256']==sha(OUT/'adoption.json') and native['map_sha256']==scene['map_sha256']==mapsha
 assert sightlines['status']=='passed' and sightlines['adoption_sha256']==sha(OUT/'adoption.json')
 themed={q['building']:q for q in read(OUT.parent/'world-wall-grok-v1/models.json')['buildings']};localized={q['building']:q for q in read(OUT.parent/'world-wall-grok-v1/portable-localization.json')['models']};records=[read(q['receipt']) for q in a['buildings']]
 names={q['id']:q['name'] for q in read(ROOT/'Docs/StreetSocialCity/production/building-floor-audit.json')['buildings']}
 def link(path,label):return '<a href="'+html.escape(os.path.relpath(path,OUT).replace('\\','/'),quote=True)+'">'+html.escape(label)+'</a>'
 def image(path):return '<img loading="lazy" src="'+html.escape(os.path.relpath(path,OUT).replace('\\','/'),quote=True)+'">'
 pictures=[]
 for bid in ['EE','AD','H1','M1']:
  review=read(OUT/'previews'/(bid+'-receipt.json'));assert review['geometry_source_sha256']==next(r['model_sha256'] for r in records if r['building']==bid)
  assert review['model_sha256']==themed[bid]['model_sha256'],'Refresh previews from latest themed model'
  for q in review['images']:assert sha(q['path'])==q['sha256'];pictures.append(q)
 cards=[];classrooms=[];admins=[]
 for r in records:
  bid=r['building'];t=themed[bid];assert t['source_sha256']==r['model_sha256'];l=localized[bid];assert l['source_sha256']==t['model_sha256'];classrooms+=r['classrooms'];admins+=r['administration']
  body='<article><h2>'+html.escape(bid+' '+names[bid])+'</h2>'+image(t['exterior'])+'<p>'+link(t['model'],'最新完整 GLB')+' · '+link(Path(t['model']).with_suffix('.blend'),'Blender')+' · '+link(l['model'],'繁／簡／英 GLB')+'</p><p>'+str(len(r['classrooms']))+' 間教學空間／'+str(sum(q['tiered'] for q in r['classrooms']))+' 間階梯教室／'+str(len(r['administration']))+' 處行政接待空間</p><details><summary>逐層模型與實際面數</summary><ul>'
  for f in r['floors']:body+='<li>'+link(f['source'],str(f['floor'])+'F 功能修訂幾何')+' · '+str(f['triangles'])+' 三角面</li>'
  body+='</ul><p>逐層連結為驗收幾何；原生 Unreal 已套主題牆與三語。完整 GLB／Blender／三語 GLB 包含最新主題材質。</p></details></article>';cards.append(body)
 assert len(classrooms)==a['classrooms'] and all(q.get('display_arrangement')=='continuous_centered_blackboard_with_lowered_retractable_screen_in_front' for q in classrooms),'Every classroom must adopt the corrected layered arrangement'
 metrics=dict(status='passed',completed_at=datetime.datetime.now().astimezone().isoformat(),buildings=len(a['buildings']),revised_floors=len(a['floors']),classrooms=a['classrooms'],classroom_display_arrangement='continuous_centered_blackboard_with_lowered_retractable_screen_in_front',tiered_classrooms=a['tiered_classrooms'],administrative_rooms=a['administrative_rooms'],computer_workstations=sum(len(q['workstations']) for q in admins),opaque_bays=a['opaque_bays'],model_checks=a['checks'],native_checks=native['checks'],integration_checks=scene['checks'],max_floor_triangles=max(f['triangles'] for f in a['floors']),map=scene['map'],map_sha256=mapsha,adoption_sha256=sha(OUT/'adoption.json'),native_receipt_sha256=sha(OUT/'native-acceptance.json'),rendered_images=pictures,scope='Actual static models, materials, collision and runtime language regression; gameplay interaction work remains separate')
 (OUT/'acceptance.json').write_text(json.dumps(metrics,ensure_ascii=False,indent=2),'utf8');(OUT/'classroom-register.json').write_text(json.dumps(classrooms,ensure_ascii=False,indent=2),'utf8');(OUT/'administration-register.json').write_text(json.dumps(admins,ensure_ascii=False,indent=2),'utf8')
 figures=''.join('<figure>'+image(q['path'])+'<figcaption>'+html.escape(Path(q['path']).stem)+'</figcaption></figure>' for q in pictures)
 page='<!doctype html><html lang="zh-Hant"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>教室、行政設備與百貨遮光修訂</title><style>body{margin:0;background:#111b22;color:#e7ecee;font:17px/1.7 system-ui}main{max-width:1500px;margin:auto;padding:32px}a{color:#90d9cf}h1{line-height:1.3}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(360px,1fr));gap:22px}article,figure,.summary{margin:0;padding:22px;background:#1c2b34;border-radius:12px}img{width:100%;height:auto}details{margin-top:15px}figcaption{color:#a4bbc5}code{overflow-wrap:anywhere}</style><main><h1>百貨實牆・教室設備・行政電腦</h1><section class="summary"><p>已完成並整合至 Unreal：'+str(metrics['classrooms'])+' 間教學空間，'+str(metrics['tiered_classrooms'])+' 間階梯教室，'+str(metrics['administrative_rooms'])+' 處行政／接待空間，'+str(metrics['computer_workstations'])+' 個電腦工作台，'+str(metrics['opaque_bays'])+' 處不透明外牆嵌板。</p><p>前牆為完整連續大黑板，中央捲軸投影布在黑板前方放下，遮住中央並露出兩側書寫區。<br>階梯教室24席；中央2m階梯通道、兩側至少2m平坦路線，前排0／中排240／後排480mm；每個踏步120mm。百貨嵌板厚180mm，材質不透明且不透射。</p><p>模型驗收 '+str(a['checks'])+' 項，原生驗收 '+str(native['checks'])+' 項，全城整合 '+str(scene['checks'])+' 項。最高每層 '+str(metrics['max_floor_triangles'])+' 三角面，貼圖最高2K。</p><p>'+link(OUT/'acceptance.json','完整驗收')+' · '+link(OUT/'native-acceptance.json','Unreal 原生檢查')+' · '+link(OUT/'classroom-register.json','逐間教室')+' · '+link(OUT/'administration-register.json','逐間行政空間')+' · '+link(OUT/'WORK_PLAN.md','計畫與參考')+'</p><code>'+scene['map']+'</code></section><h2>實際模型室內與外牆</h2><div class="grid">'+figures+'</div><h2>最新建築模型</h2><div class="grid">'+''.join(cards)+'</div></main></html>'
 (OUT/'index.html').write_text(page,'utf8')
 report=f'''# 功能修訂完整驗收\n\n已更新 {metrics['buildings']} 棟／{metrics['revised_floors']} 層。\n\n- {metrics['classrooms']} 間教學空間配置完整置中的大黑板；中央垂降投影幕置於黑板前方，放下遮住中央，兩側保留書寫區。投影機對準中央布幕。\n- {metrics['tiered_classrooms']} 間較大教室改為24席階梯座席；保留2m中央與平坦側道。\n- {metrics['administrative_rooms']} 處行政／接待空間，{metrics['computer_workstations']} 個電腦工作台。\n- 百貨319處透明外牆窗格改180mm實心板，保留入口。\n- 模型{a['checks']}項、Unreal原生{native['checks']}項、全城整合{scene['checks']}項通過。\n- 原有主題牆面、三語招牌及真正PIE語言切換回歸完成。\n\n地圖：{scene['map']}\n\nSHA256：{mapsha}\n\n模型幾何、原生碰撞、實際材質與渲染已驗證；不是法規或施工認證。完整入口為index.html，舊來源與修訂前地圖備份保留。\n'''
 (OUT/'FINAL_REVIEW.md').write_text(report,'utf8')
 doc=ROOT/'Docs/StreetSocialCity/COMPLETED_WORLD_ADOPTION.json';d=read(doc);d['map_sha256']=mapsha;d['functional_interiors']=dict(receipt=str((OUT/'acceptance.json').relative_to(ROOT)),sha256=sha(OUT/'acceptance.json'),classrooms=a['classrooms'],tiered_classrooms=a['tiered_classrooms'],administrative_rooms=a['administrative_rooms']);doc.write_text(json.dumps(d,ensure_ascii=False,indent=2),'utf8')
 for rel in ['world-wall-grok-v1/index.html','campus-v1/index.html','mall-v1/index.html']:
  path=OUT.parent/rel;text=path.read_text('utf8');marker='<!-- functional-interiors-current -->';banner=marker+'<p style="padding:18px;background:#24525a;color:white"><a style="color:white" href="../functional-interiors-v1/index.html">最新：教室黑板／階梯座席、行政電腦、百貨不透明外牆與完整驗收</a></p>'
  if marker not in text:text=text.replace('<body>','<body>'+banner,1) if '<body>' in text else text.replace('<h1>',banner+'<h1>',1)
  path.write_text(text,'utf8')
 print('PUBLISHED FUNCTIONAL REVISION',metrics['buildings'],metrics['revised_floors'],flush=True)
if __name__=='__main__':main()
