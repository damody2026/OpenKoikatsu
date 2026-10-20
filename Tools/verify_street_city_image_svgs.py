"""Verify image provenance and unchanged metric geometry in regenerated SVGs."""
import hashlib,json
from pathlib import Path
import xml.etree.ElementTree as ET
from html.parser import HTMLParser
from urllib.parse import unquote
from street_city_io import write_json,write_text
from street_city_small_shop import assert_building
from label_street_city_floor_metrics import inspect,NS,outer_rectangles,PREFIX

ROOT=Path(__file__).resolve().parents[1];PROD=ROOT/'Docs/StreetSocialCity/production';CACHE=ROOT/'output/street-social-city-production/store-concepts';DEST=PROD/'store-planning-svg'
def read(p):return json.loads(p.read_text('utf8'))
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def door_signatures(root):
 return {q.get('id'):ET.tostring(q,encoding='unicode') for q in root.iter(NS+'g') if q.get('data-door-kind')}

def main():
 report=read(PROD/'image-to-svg-register.json');register=read(PROD/'store-design-register.json');records={r['id']:r for r in register['stores']};m=read(PROD/'assembly-manifest.json');buildings={b['building']:b for b in m['layouts'] if b.get('kind')=='building'}
 assert report['source_manifest_sha256']==register['source_manifest_sha256']==sha(PROD/'assembly-manifest.json')
 assert {r['id'] for r in report['stores']}==set(records) and report['store_count']==len(records)
 assert len({r['image_sha256'] for r in report['stores']})==len(records),'A single illustration was reused under multiple store IDs'
 stage=read(PROD/'planning-stage.json');assert stage['models_paused'] is True
 assert sha(PROD/'generation-ledger.json')==stage['existing_generation_ledger_sha256'],'Model ledger changed'
 checks=0;floors=0;doors=0;native_files=0;files=[]
 for item in report['stores']:
  key=item['id'];r=records[key];g=read(CACHE/key/'generation.json');assert item['image_sha256']==g['image_sha256']==sha(CACHE/key/'concept.jpg');assert item['design_sha256']==r['design_sha256'];assert g['design_sha256']==r.get('image_origin',{}).get('design_sha256',r['design_sha256']);checks+=4
  if r.get('image_origin'):
   from build_street_city_store_review import image_contract
   origin=r['image_origin'];old_input=read(ROOT/origin['original_design_file'])
   assert sha(ROOT/origin['original_design_file'])==origin['original_design_file_sha256'] and image_contract(old_input)==image_contract(r)
   assert sha(ROOT/origin['original_prompt_file'])==origin['prompt_sha256']==g['prompt_sha256'] and old_input['design_sha256']==g['design_sha256'];checks+=2
  assert g.get('completion_evidence')=='exact_prompt_and_completed_image_tool_receipt'
  event=Path(g['tool_event_log']);assert sha(event)==g['tool_event_log_sha256'];events=[json.loads(line) for line in event.read_text('utf8').splitlines()]
  call=next(c for e in events for c in e.get('tool_calls',[]) if c.get('id')==g['tool_call_id']);assert call['name'] in ('image_gen','image_edit')
  brief=json.loads(call['arguments']).get('prompt','');assert len(brief)>500 and 'placeholder' not in brief.lower()[:150];checks+=4
  path=ROOT/item['svg_file'];assert sha(path)==item['svg_sha256'];root=ET.parse(path).getroot();meta=json.loads(root.find(NS+'metadata').text)
  files.append(dict(file=path.relative_to(ROOT).as_posix(),sha256=sha(path)))
  assert meta['image_sha256']==item['image_sha256'] and meta['interpretation_sha256']==item['interpretation_sha256'] and meta['raster_geometry_traced'] is False
  assert not list(root.iter(NS+'image')),'A raster in an SVG wrapper is not a regenerated vector plan';checks+=3
  interpret=list((ROOT/'output/street-social-city-production/image-to-svg').glob('batch-*/decisions.json'))
  match=[p for p in interpret if sha(p)==item['interpretation_sha256']];assert len(match)==1
  packet=read(match[0]);assert sha(Path(packet['event_log']))==packet['event_log_sha256'];assert sha(match[0].parent/'illustrations.jpg')==packet['contact_sha256'];assert sha(match[0].parent/'prompt.txt')==packet['prompt_sha256'];checks+=4
  decision=next(d for d in packet['stores'] if d['id']==key);assert decision['adopt']==item['adopted'] and decision['reject']==item['corrected'];checks+=2
  if r['building']!='M1':
   b=buildings[key]
   if b.get('small_shop_design'):
    audit=assert_building(b);checks+=audit['checks']
   original=ET.parse(PROD/f'floor-layouts/{key}.svg').getroot()
   assert door_signatures(root)==door_signatures(original),key+': altered doors or swings'
   assert outer_rectangles(root)==outer_rectangles(original),key+': altered complete floor plates'
   floors+=inspect(path.read_text('utf8'),b,b['floors']);doors+=len(door_signatures(root));checks+=2
   if b.get('small_shop_design'):assert len([q for q in root.iter(NS+'rect') if q.get('data-vector-facade')])==1
   for f in b['floors']:
    floor_path=DEST/f'{key}-F{f["floor"]:02d}.svg';floor_root=ET.parse(floor_path).getroot();inspect(floor_path.read_text('utf8'),b,[f]);assert floor_root.find(NS+'metadata') is not None
    floor_source=ET.parse(PROD/f'floor-layouts/{key}-F{f["floor"]:02d}.svg').getroot();assert door_signatures(floor_root)==door_signatures(floor_source) and outer_rectangles(floor_root)==outer_rectangles(floor_source)
    files.append(dict(file=floor_path.relative_to(ROOT).as_posix(),sha256=sha(floor_path)));native_files+=1;checks+=3
  else:
   u=r['tenant_unit'];q=u['dimensioned_programme'];outline=next(x for x in root.iter(NS+'rect') if x.get('data-tenant-envelope'))
   displays=[v for v in q['equipment_layout'] if v['equipment']!='checkout']
   assert {(v['room_local_position_m'][0]>0,v['room_local_position_m'][1]>0) for v in displays}=={(False,False),(False,True),(True,False),(True,True)},key+': displays clustered on one side';checks+=1
   group=next(x for x in root.iter(NS+'g') if x.get('id','')==PREFIX+key+f'-F{u["floor"]:02d}');s=float(group.get('data-svg-units-per-m'))
   for actual,expected in zip((float(outline.get('width'))/s,float(outline.get('height'))/s),u['clear_size_m']):assert abs(actual-expected)<1e-6;checks+=1
   bars={int(x.get('data-scale-metres')):float(x.get('width')) for x in group.iter(NS+'rect') if x.get('data-scale-metres')};assert set(bars)=={1,5,10}
   for length,pixels in bars.items():assert abs(pixels-length*s)<1e-6;checks+=1
   main_routes=[x for x in root.iter(NS+'rect') if x.get('data-route-clear-m')];assert len(main_routes)==len(q.get('internal_corridors',[]))
   for a,b in zip(main_routes,q.get('internal_corridors',[])):
    bound=b['bounds_m'];assert abs(float(a.get('width'))-(bound[2]-bound[0])*s)<1e-6 and abs(float(a.get('height'))-(bound[3]-bound[1])*s)<1e-6;checks+=1
   # The context is native SVG with the exact full floor and every public door.
   context=next(x for x in root if x.tag==NS+'svg');source=ET.parse(PROD/f'floor-layouts/M1-F{u["floor"]:02d}.svg').getroot()
   assert door_signatures(context)==door_signatures(source) and outer_rectangles(context)==outer_rectangles(source);checks+=2;doors+=len(door_signatures(context));floors+=1
   detail=ET.parse(DEST/(key+'-detail.svg')).getroot();assert detail.find(NS+'metadata') is not None and not list(detail.iter(NS+'image'));native_files+=1;checks+=1
   detail_path=DEST/(key+'-detail.svg');files.append(dict(file=detail_path.relative_to(ROOT).as_posix(),sha256=sha(detail_path)))
  native_files+=1
 class Links(HTMLParser):
  def handle_starttag(self,tag,attrs):
   nonlocal checks
   for k,v in attrs:
    if k in ('src','href') and v and not v.startswith(('http:','https:','#','data:')):assert (PROD/unquote(v)).is_file(),v;checks+=1
 Links().feed((PROD/'image-to-svg-review.html').read_text('utf8'))
 result=dict(status='passed',store_count=len(records),grok_images=len(records),interpreted_images=len(records),native_svg_files=native_files,metric_floor_panels=floors,door_and_swing_signatures_checked=doors,checks=checks,source_manifest_sha256=sha(PROD/'assembly-manifest.json'),generation_ledger_unchanged=True,models_paused=True,raster_geometry_traced=False)
 result['files']=files;assert len(files)==native_files
 result['balanced_mall_tenants']=sum(r['building']=='M1' for r in records.values())
 write_json(PROD/'image-to-svg-acceptance.json',result)
 write_text(PROD/'IMAGE_TO_SVG.md','\n'.join(['# 逐店 Grok 示意圖與 SVG','',f'完成 {len(records)} 間：48 棟街邊店屋、80 間百貨店。共 {native_files} 份原生向量 SVG；檢查 {checks:,} 項通過。','', '流程：使用已完成且與現行設計相符的128張 Grok Build 原圖 → Grok Build 實際讀取逐店圖片 → 記錄可採用材料、招牌、遮簷及家具細節與應校正事項 → 依完整公尺資料重畫向量平面與立面。原圖逐張保留，不重複生成相同的已完成工作。','', '圖片不含可靠施工尺寸，因此不直接描摹其中錯誤的樓梯、挑高或房間比例。轉繪保留目前驗證過的完整樓層、門扇弧線、2m淨通路及1／5／10m比例尺。百貨櫃位另附完整同層圖，電梯、樓梯及廁所按同一建築座標連接。','', '街邊店屋輸出：Sxx.svg（各層＋立面）及 Sxx-Fxx.svg。百貨輸出：M1-xxxx.svg（店內細部＋附屬區＋完整同層）及 M1-xxxx-detail.svg（放大店內平面）。這些檔案位於 store-planning-svg。','', '每店登錄圖片、讀圖回執、解讀資料與SVG的SHA。原生SVG中沒有用點陣圖冒充向量。色彩與裝修轉繪不代表正式Tripo模型或遊戲走行完成；模型製作仍暫停。','', '[逐店圖稿與SVG對照](image-to-svg-review.html) · [登錄](image-to-svg-register.json) · [驗收](image-to-svg-acceptance.json) · [錯誤記錄](../ERRORS_AND_PREVENTION.md)','', '重跑：python Tools/refresh_street_city_store_visuals.py。流程會驗證目前資料、復用相符圖稿與解讀、補做缺項，再重新產生 SVG 並驗收。','']))
 doc=(PROD/'IMAGE_TO_SVG.md').read_text('utf8')
 street_count=sum(r['building']!='M1' for r in records.values());mall_count=len(records)-street_count
 doc=doc.replace('48 棟街邊店屋、80 間百貨店',f'{street_count} 棟街邊店屋、{mall_count} 間百貨店').replace('已完成且與現行設計相符的128張 Grok Build 原圖',f'已完成且與現行設計相符的{len(records)}張 Grok Build 原圖（原128張復用，南側新增48張）')
 doc+='\n## 原始圖稿與校正後配置\n\nimage-origin-design.json與image-origin-prompt.txt保留當初生成圖稿的輸入；design-input.json及SVG保存校正後配置。圖片提供用途、材料及裝修參考，校正後的家具分布不冒稱為原始生成輸入。百貨80間店均已檢查陳列分散於前後左右各區，並保留2m通路。\n'
 write_text(PROD/'IMAGE_TO_SVG.md',doc)
 print(json.dumps({k:v for k,v in result.items() if k!='files'},ensure_ascii=False))
if __name__=='__main__':main()
