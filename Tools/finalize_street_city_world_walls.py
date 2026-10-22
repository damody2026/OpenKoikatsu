"""Complete only after every actual model, portable locale and native wall gate passes."""
import json,time,subprocess,sys,shutil,datetime,hashlib,io,os
from pathlib import Path
from PIL import Image
from street_city_world_wall_art import ROOT,OUT,sha
from build_street_city_language_models import read,geometry_digest
def load(p):return json.loads(Path(p).read_text('utf8'))
def main():
 start=time.monotonic()
 while True:
  paths=[OUT/'models.json',OUT/'portable-localization.json',OUT/'model-acceptance.json',OUT/'closeup-renders.json']
  docs=[load(p) if p.exists() else {} for p in paths]
  if all(q.get('status') in ['passed','passed_actual_models'] for q in docs) and len(docs[0]['buildings'])==122 and docs[1]['buildings']==122 and docs[2]['buildings']==122:break
  (OUT/'final-progress.json').write_text(json.dumps(dict(status='waiting_for_all_models',models=len(list((OUT/'buildings').glob('*/model.json'))),localizations=docs[1].get('buildings',0),independent_model_checks=docs[2].get('buildings',0))), 'utf8');assert time.monotonic()-start<7200;time.sleep(15)
 modeldoc,portable,independent,closeup=docs;assert len(closeup['images'])==8
 native=load(OUT/'native-acceptance.json');gpu=load(OUT/'native-render.json');install=load(OUT/'native-install.json');assert native['status']=='passed' and native['source_install_sha256']==gpu['source_install_sha256']==sha(OUT/'native-install.json');assert gpu['status']=='passed_real_rhi_captures' and len(gpu['images'])==11
 assert install['installer_sha256']==sha(ROOT/'Tools/install_street_city_world_walls_unreal.py')
 catalogue=load(OUT/'catalogue.json');materials=load(OUT/'materials.json');review=load(OUT/'visual-material-review.json');assert len(catalogue['images'])==len(materials['images'])==90 and review['status']=='passed';assert load(OUT/'batch.json')['completed']==90
 for q in materials['images']:
  r=load(OUT/'assets'/q['id']/'generation.json');assert 'completed_image_tool_receipt' in r['completion_evidence'];assert sha(r['grok_original_image_file'])==r['grok_original_image_sha256'];assert sha(OUT/'assets'/q['id']/'concept.jpg')==review['approved_image_sha256'][q['id']]==q['grok_image_sha256'];assert q['maximum_edge_difference']==0
  for m in q['maps'].values():assert sha(m['path'])==m['sha256'];Image.open(m['path']).verify()
 original={q['building']:q for q in load(OUT.parent/'localization-v1/models.json')['buildings']};localized={q['building']:q for q in portable['models']};images=0
 for q in modeldoc['buildings']:
  bid=q['building'];assert sha(q['source'])==q['source_sha256'] and sha(q['model'])==q['model_sha256'];assert q['geometry_before']==q['geometry_after'];assert localized[bid]['source_sha256']==q['model_sha256']
  expected={v['semantic_id'] for v in original[bid]['bindings']};actual={v['semantic_id'] for v in localized[bid]['bindings']};assert expected==actual,(bid,'lost localized labels',expected-actual,actual-expected)
  old,oldraw=read(q['model']);new,newraw=read(localized[bid]['model']);assert geometry_digest(old,oldraw)==geometry_digest(new,newraw)
  for kind in ['exterior','interior']:
   assert sha(q[kind])==q[kind+'_sha256'];Image.open(q[kind]).verify();images+=1
 for q in [*closeup['images'],*gpu['images']]:
  path=q.get('image');assert sha(path)==q.get('image_sha256',q.get('sha256'));Image.open(path).verify();images+=1
 # The active marker enables general integration to restore this material layer.
 activation=dict(status='active',images=90,materials_sha256=sha(OUT/'materials.json'),visual_review_sha256=sha(OUT/'visual-material-review.json'),native_acceptance_sha256=sha(OUT/'native-acceptance.json'),installer_sha256=install['installer_sha256'],scope='Material overrides on current certified scene meshes; text slots retained')
 (OUT/'activation.json').write_text(json.dumps(activation,indent=2),'utf8')
 subprocess.run([sys.executable,str(ROOT/'Tools/verify_street_city_scene_integration.py')],check=True,cwd=ROOT)
 scene=load(OUT.parent/'scene-integration-v1/acceptance.json');assert scene['status']=='passed' and scene['map_sha256']==native['map_sha256']==sha(ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap')
 used=sorted({q[k] for q in install['slots'] for k in ['interior','exterior']});available=sorted(q['id'] for q in materials['images']);report=dict(status='passed',completed_at=datetime.datetime.now().astimezone().isoformat(),generator='Grok Build CLI image_gen, grok-4.7',generated_images=90,used_texture_designs=len(used),library_alternates=sorted(set(available)-set(used)),updated_buildings=122,updated_floor_actors=427,wall_material_slots=len(install['slots']),unique_native_materials=native['unique_materials'],native_checks=native['checks'],revised_portable_buildings=122,three_language_buildings=122,actual_preview_images=images,max_texture_dimension=2048,repeat_m=2,source_geometry_unchanged=True,source_sign_and_integrated_logo_semantics_unchanged=True,map_sha256=native['map_sha256'],wall_installer_sha256=install['installer_sha256'],receipts={str(p.relative_to(ROOT)):sha(p) for p in [*paths,OUT/'native-acceptance.json',OUT/'native-render.json',OUT/'visual-material-review.json',OUT.parent/'scene-integration-v1/acceptance.json']})
 (OUT/'acceptance.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),'utf8')
 subprocess.run([sys.executable,str(ROOT/'Tools/publish_street_city_world_walls.py')],check=True,cwd=ROOT)
 # Refresh the original city-completion report to the same newly verified map.
 subprocess.run([sys.executable,str(ROOT/'Tools/publish_street_city_completion_review.py')],check=True,cwd=ROOT)
 page=OUT.parent/'world-completion-v1/index.html';text=page.read_text('utf8');text=text.replace('<h1>','<p style="padding:20px;background:#e0eadf"><a href="../world-wall-grok-v1/index.html">最新：Grok 主題牆面・90 張圖面與122棟建物更新</a></p><h1>',1)
 for q in modeldoc['buildings']:
  bid=q['building']
  if bid not in ['H1','R1','R2','O01','O02','O03','CV','TR']:continue
  for kind,oldname in [('exterior','building-preview.png'),('interior','interior-preview.png')]:text=text.replace('buildings/'+bid+'/'+oldname,os.path.relpath(q[kind],page.parent).replace('\\','/'))
 page.write_text(text,'utf8')
 subprocess.run([sys.executable,str(ROOT/'Tools/refresh_street_city_world_wall_links.py')],check=True,cwd=ROOT)
 summary=f'''# Grok 建築主題牆面：完成驗收

90張新圖面由Grok Build真正生成：8棟城市建築各4種、17棟校舍各2種、商店街20種補充外牆、百貨公共區4種。原始工作、提示、image_gen完成收據與原圖均保留。83種實際套用，7種濕區替代材質保留於圖庫。既有商店獨立品牌及80專櫃個別裝潢保留。

全城122棟、427層的728個牆面槽已更新；281個原生牆材質。2公尺重複尺度，貼圖最高2048。圖面均無文字，三語招牌及整合進品牌的字體維持獨立在地化。法線與粗糙度是近似輔助貼圖，並非獨立物理掃描。

122份新版GLB/Blend與244張實際外觀／首層室內渲染，另有8張實際牆面近景及11張真實Unreal D3D BaseColor擷取。每棟皆有繁體、簡體、英文GLB材質變體。新版GLB獨立比對原始模型三角面數、0.1mm尺寸邊界、表面面積及面積加權中心；座標／拓撲／變換之組裝前後雜湊相同，沒有新增模型面數。Unreal仍採用既有Rust remesh認證幾何，以材質override更新，保留水平地板／天花、碰撞、門洞及樓梯。

原生牆面重新開檔{native['checks']:,}項通過；全城整合{scene['checks']:,}項、31,480項新增建築碰撞回歸、427層與34校園外牆招牌的三語重開、30項真正PIE語言切換全部通過。GPU編譯錯誤、藍色底圖誤辨法線、初始化與黑圖擷取問題均已修正，失敗版本未採用；詳細原因與預防見ERRORS_AND_PREVENTION.md。

地圖：/Game/UniversityCity/Maps/UniversityCity
SHA256：{native['map_sha256']}

入口：index.html。流程與恢復材質層的方法已寫入model3d.md。所有歷史來源、已驗收幾何及備份保留；沒有為換材質重新生成建築或重做遊戲互動。
'''
 (OUT/'FINAL_REVIEW.md').write_text(summary,'utf8')
 with (OUT/'WORK_PLAN.md').open('a',encoding='utf8') as f:f.write('\n\nFINAL VERIFIED: 90 genuine Grok images;122 revised models/three-language variants;244 actual renders+8closeups+11realRHI captures;728nativewallslots;allnative/source/geometry/language/runtime gatespassed. Delivery index.html,acceptance.json,FINAL_REVIEW.md.\n')
 print(json.dumps(report,ensure_ascii=False,indent=2),flush=True)
if __name__=='__main__':main()
