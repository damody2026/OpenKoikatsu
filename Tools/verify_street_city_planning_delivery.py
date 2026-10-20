"""Current 2D planning acceptance while the user has explicitly paused modelling."""
import hashlib,json,subprocess,sys,shutil,unittest
from pathlib import Path
from street_city_io import write_json,write_text
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'Docs/StreetSocialCity/production'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text('utf8'))
def main():
 stage=read(OUT/'planning-stage.json');manifest=read(OUT/'assembly-manifest.json');digest=sha(OUT/'assembly-manifest.json')
 assert stage['models_paused'] and stage['source_manifest_sha256']==digest
 assert sha(OUT/'generation-ledger.json')==stage['existing_generation_ledger_sha256'],'Model submission ledger changed'
 subprocess.run([sys.executable,'-m','unittest','discover','-s','Tools/tests','-p','test_street_city*.py'],cwd=ROOT,check=True)
 test_count=unittest.defaultTestLoader.discover(str(ROOT/'Tools/tests'),pattern='test_street_city*.py').countTestCases()
 sources={}
 for name in ('site-acceptance','layout-acceptance','functional-acceptance','scale-audit','modular-acceptance','modular-geometry-acceptance','asset-library-acceptance','floor-metrics-acceptance','drawing-acceptance','space-enclosure-acceptance','store-programme-acceptance','campus-layout-acceptance','image-to-svg-acceptance','scene-svg-acceptance'):
  path=OUT/(name+'.json');r=read(path);assert r['status']=='passed',name
  assert r.get('source_manifest_sha256',r.get('manifest_sha256'))==digest,name+': outdated plan'
  sources[name]=dict(file=path.name,sha256=sha(path),checks=r.get('checks'))
 scene=read(OUT/'scene-illustration-acceptance.json');assert scene['source_manifest_sha256']==digest and scene['scenes']==9 and scene['model_ledger_unchanged']
 site=read(OUT/'site-acceptance.json')
 for name,d in site['geographic_files_sha256'].items():assert sha(OUT.parent/name)==d
 for f in read(OUT/'floor-metrics-acceptance.json')['files']:assert sha(OUT/f['file'])==f['sha256']
 current_svg=read(OUT/'image-to-svg-register.json')
 for r in current_svg['stores']:assert sha(ROOT/r['svg_file'])==r['svg_sha256']
 for r in read(OUT/'image-to-svg-acceptance.json')['files']:assert sha(ROOT/r['file'])==r['sha256']
 render_file=ROOT/'output/street-social-city-production/image-to-svg/vector-previews/render-receipt.json';renders=read(render_file);visual=read(render_file.parent/'visual-review.json')
 assert renders['source_register_sha256']==sha(OUT/'image-to-svg-register.json') and visual['render_receipt_sha256']==sha(render_file) and visual['source_manifest_sha256']==digest
 assert renders['stores']==visual['stores']==current_svg['store_count'] and visual['reviewed_by']=='Codex'
 for q in renders['records']:assert sha(ROOT/q['svg_file'])==q['svg_sha256'] and sha(ROOT/q['preview_file'])==q['preview_sha256']
 for q in visual['pages']:assert sha(ROOT/q['file'])==q['sha256']
 b=[x for x in manifest['layouts'] if x.get('kind')=='building'];units=[x for x in manifest['layouts'] if x.get('room_id')];vectors=read(OUT/'image-to-svg-acceptance.json')
 # Preserve the old, independently verified control receipt as historical proof.
 # No GLB or BLEND is exported while modelling is paused.
 history=OUT/'acceptance-history';history.mkdir(exist_ok=True)
 for name in ('current-acceptance.json','current-acceptance.md'):
  p=OUT/name
  if p.is_file():shutil.copy2(p,history/(sha(p)+p.suffix))
 prior_controls=read(ROOT/'output/street-social-city-production/control-assemblies-acceptance.json')
 result=dict(status='current_planning_features_passed',scope='2D world, complete floor geometry, Grok illustrations and regenerated native SVG; modelling explicitly paused',source_manifest_sha256=digest,buildings=len(b),floors=sum(len(x['floors']) for x in b),indoor_primary_units=len(units),outdoor_primary_units=20,primary_units=len(units)+20,street_shops=site['shops'],mall_tenants=vectors['store_count']-site['shops'],grok_store_images=vectors['grok_images'],regenerated_svg_files=vectors['native_svg_files'],scene_illustrations=9,models_paused=True,model_ledger_unchanged=True,actual_tripo_models_created_this_task=0,prior_control_buildings=len(prior_controls['buildings']),prior_control_source_manifest_sha256=prior_controls['source_manifest_sha256'],current_model_geometry_approval=False,evidence=sources)
 result['unit_tests']=test_count
 result['rendered_and_visually_reviewed_stores']=renders['stores']
 write_json(OUT/'current-planning-acceptance.json',result);write_json(OUT/'current-acceptance.json',result)
 lines=['# 本輪完整驗收：場景規畫、Grok 圖稿與 SVG','',f'結果：**通過目前規畫功能**。來源 `{digest}`。','',f'- {len(b)} 棟、{result["floors"]} 層、{result["primary_units"]} 個主要互動單元。',f'- 學生商街 {site["shops"]} 棟，四排兩兩相向；南側新增48棟，逐層尺寸、門位與通路已驗。',f'- {vectors["store_count"]} 間店均有實際 Grok 圖稿、逐圖解讀與原生 SVG；共 {vectors["native_svg_files"]} 份轉繪向量檔。','- 一般門1.2m、正門2.4m、室內走廊淨寬2m、牆厚0.2m；每層包含真實尺寸的樓梯、電梯、廁所及1／5／10m比例尺。','- 全世界與商街轉繪保留獨立建物輪廓；不把Grok合併的長屋頂或錯誤院落描成尺寸圖。','- 模型製作保持暫停，製作紀錄SHA未變。本輪沒有生成或匯出模型。先前74棟控制模型的驗收存為歷史證據，不能宣稱新增48棟已有模型。','', '[逐店圖稿→SVG](image-to-svg-review.html) · [世界與分區](scene-review.html) · [完整尺寸地形圖](../world-terrain.svg) · [驗收資料](current-planning-acceptance.json) · [錯誤記錄](../ERRORS_AND_PREVENTION.md)','']
 write_text(OUT/'current-acceptance.md','\n'.join(lines));print(json.dumps({k:v for k,v in result.items() if k!='evidence'},ensure_ascii=False))
if __name__=='__main__':main()
