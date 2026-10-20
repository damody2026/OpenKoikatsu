"""Per-store evidence, furnishing and visual review tickets, never mesh approval."""
import hashlib,json,re,html
from pathlib import Path
from collections import Counter
from street_city_io import write_json,write_text
from street_city_store_program import category,reference,EQUIPMENT,FAMILY
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'Docs/StreetSocialCity/production'
CACHE=ROOT/'output/street-social-city-production/store-concepts'
def sha(q):return hashlib.sha256(json.dumps(q,ensure_ascii=False,sort_keys=True).encode()).hexdigest()

def image_contract(entry):
 """Image is input to the corrected SVG; freeze its structural/type identity.

 Fixture coordinates may be corrected AFTER viewing the generated image.
 Do not relabel that older image as generated from the corrected output plan.
 """
 unit=entry['tenant_unit']
 return {k:entry[k] for k in ('id','name','category','building_footprint_m','tenant_clear_size_m','wall_m','corridor_clear_m','regular_door_clear_m','building_entrance_clear_m','floor_programmes')}|dict(tenant_rotation=unit['rotation_z_deg'],tenant_door=unit['door'],tenant_enclosure=unit.get('enclosure_policy'),reference=entry['reference'])
def build():
 m=json.loads((OUT/'assembly-manifest.json').read_text('utf8'));plan=json.loads((OUT/'effective-city-plan.json').read_text('utf8'))
 names={b['id']:b['name'] for b in plan['buildings']};room_names={r['id']:r['name'] for r in plan['rooms']}
 research=json.loads((OUT/'store-research-register.json').read_text('utf8'));sources={q['url']:q for q in research['cases']}
 buildings={q['building']:q for q in m['layouts'] if q.get('kind')=='building'}
 units={q['room_id']:q for q in m['layouts'] if q.get('room_id')};records=[]
 for key in sorted([b for b in buildings if re.fullmatch(r'S\d{2}',b)])+sorted(k for k in units if k.startswith('M1-')):
  bid='M1' if key.startswith('M1-') else key;b=buildings[bid];rid=key if bid=='M1' else key+'-0101';unit=units[rid];q=unit['dimensioned_programme']
  name=room_names[rid] if bid=='M1' else names[bid];cat=category(bid,rid,name);ref=reference(bid,rid,name);evidence=sources.get(ref['url'],{})
  photos=[p for p in evidence.get('photos',[]) if p.get('review',{}).get('usable_for_spatial_design')]
  annex=[dict(floor=f['floor'],**a) for f in b['floors'] for a in f.get('annex_rooms',[]) if a.get('owner_unit')==rid]
  equipment=Counter(v['equipment'] for v in q.get('equipment_layout',[]))
  for a in annex:equipment.update(v['equipment'] for v in a.get('equipment_layout',[]))
  entry=dict(id=key,building=bid,name=name,category=cat,reference=ref,source_page_status=evidence.get('http_status','not_downloaded'),verified_photo_files=photos,
   dimensions_are_game_design_not_photo_measurements=True,building_footprint_m=b['footprint_m'],tenant_clear_size_m=unit['clear_size_m'],
   corridor_clear_m=2,regular_door_clear_m=1.2,building_entrance_clear_m=2.4,wall_m=.2,
   tenant_unit=unit,associated_annex_spaces=annex,fixture_counts=dict(equipment),
   visible_shapes={k:EQUIPMENT[k][2] for k in equipment if k in EQUIPMENT},
   floor_programmes=[dict(floor=f['floor'],z_m=f['z_m'],height_m=f['height_m'],vertical_circulation=f['vertical_circulation'],door_schedule=f.get('door_schedule',[]),exterior_openings=f.get('small_shop_design',{}).get('exterior_openings',[])) for f in b['floors']] if bid!='M1' else [],
   source_limitations='來源未提供本案完整公尺施工圖；照片只辨識用途、材質、商品與店型，不反推未標示尺寸。',
   geometry_review_status='dimensioned_compact_store_verified' if bid.startswith('S') else 'department_store_tenant_dimensioned',
   actual_tripo_assets_approved=False,runtime_walkthrough_verified=False)
  folder=CACHE/key;folder.mkdir(parents=True,exist_ok=True)
  origin_file=folder/'image-origin-design.json';origin_prompt=folder/'image-origin-prompt.txt'
  g=json.loads((folder/'generation.json').read_text('utf8')) if (folder/'generation.json').is_file() else {}
  previous=json.loads((folder/'design-input.json').read_text('utf8')) if (folder/'design-input.json').is_file() else None
  current_prompt=folder/'grok-prompt.txt';image=folder/'concept.jpg'
  completed=g.get('status')=='generated_review_pending' and image.is_file() and hashlib.sha256(image.read_bytes()).hexdigest()==g.get('image_sha256')
  existing_origin=json.loads(origin_file.read_text('utf8')) if origin_file.is_file() else None
  if (not existing_origin or existing_origin.get('design_sha256')!=g.get('design_sha256')) and completed and previous and previous.get('design_sha256')==g.get('design_sha256') and current_prompt.is_file() and hashlib.sha256(current_prompt.read_bytes()).hexdigest()==g.get('prompt_sha256'):
   if existing_origin:
    archive=folder/'origin-history';archive.mkdir(exist_ok=True);write_json(archive/(existing_origin['design_sha256']+'.json'),existing_origin)
    if origin_prompt.is_file():write_text(archive/(existing_origin['design_sha256']+'-prompt.txt'),origin_prompt.read_text('utf8'))
   write_json(origin_file,previous);write_text(origin_prompt,current_prompt.read_text('utf8'))
  origin=json.loads(origin_file.read_text('utf8')) if origin_file.is_file() else None
  reuse_image=bool(completed and origin and origin_prompt.is_file() and origin['design_sha256']==g.get('design_sha256') and image_contract(origin)==image_contract(entry) and hashlib.sha256(origin_prompt.read_bytes()).hexdigest()==g.get('prompt_sha256'))
  if reuse_image:
   entry['image_origin']=dict(design_sha256=origin['design_sha256'],prompt_sha256=g['prompt_sha256'],image_sha256=g['image_sha256'],original_design_file=origin_file.relative_to(ROOT).as_posix(),original_design_file_sha256=hashlib.sha256(origin_file.read_bytes()).hexdigest(),original_prompt_file=origin_prompt.relative_to(ROOT).as_posix(),usage='Generated image is the input to SVG correction; fixture positions can change in the output plan, structural/type identity stays equal')
  entry['design_sha256']=sha(entry)
  (folder/'design-input.json').write_text(json.dumps(entry,ensure_ascii=False,indent=2),'utf8')
  # The job is an inspectable concept, not a declaration that legacy areas fit a
  # real Taiwanese shop. Dimensions and pending review are visible in the portal.
  if bid=='M1':
   scene=f"A single department-store tenant on floor {unit['floor']}, clear tenant rectangle {unit['clear_size_m']} metres, plus its listed associated open activity/support zones. This is not a separate freestanding building. Three coordinated perspective panels: open shopfront from public concourse, customer eye-level interior, high oblique full-tenant cutaway. Existing mall lifts, stairs and public toilets remain in the shared building core, not duplicated in the tenant."
  else:
   scene=f"A Taiwanese university-town attached street building, outer rectangle {b['footprint_m']} metres, {len(b['floors'])} floors. Three coordinated perspective panels: front-left full exterior, customer eye-level first-floor interior, high oblique first-floor cutaway showing the complete floor and existing core. Store name {name}."
  programme=dict(room_position_m=unit['position_m'],room_size_m=unit['clear_size_m'],equipment=q.get('equipment_layout',[]),rooms=q.get('internal_rooms',[]),routes=q.get('internal_corridors',[]),annex_spaces=annex)
  if bid!='M1':
   street_entry=next(e for e in b['entrances'] if e.get('purpose')=='street_main')
   front=b.get('front_orientation','north' if street_entry['facade_side']=='N' else 'south')
   programme.update(room_rotation_z_deg=unit['rotation_z_deg'],complete_floor_core=entry['floor_programmes'][0],street_entrances=b['entrances'],front_orientation=front,exterior_schedule=[dict(floor=f['floor'],z_m=f['z_m'],height_m=f['height_m'],openings=f['exterior_openings']) for f in entry['floor_programmes']])
   scene+=f" The public shopfront faces {front.upper()} ({'+Y' if front=='north' else '-Y'}); do not relocate it to the rear."
  # Keep the visual agent focused on the actual store rather than 20 MB of city.
  prompt=("Use image_gen exactly ONCE to make ONE wide 3-panel architectural concept sheet. Use no shell, no subagents, no other tools. Return only the absolute generated image path.\n"
   +scene+"\nReference already researched by the host: "+json.dumps(ref,ensure_ascii=False)+"\n"
   +"Original fictional design; learn the real store typology rather than copying a logo or photograph. Avoid featureless sealed cubes: show thin open furniture frames, separate recognisable merchandise, glass transparency, real chair legs, visible handles, lighting, shelf thickness and functional work equipment. Shopfront needs a coherent frame, display glazing, a sheltered or recessed entrance, a readable material rhythm, a modest individual signboard and realistic upper-floor windows where applicable. Use no branded logos, no giant text, no people, no cars. Keep neighbouring party walls windowless. Details may be curved, cylindrical, sloped, upholstered and open-framed even though the floor plate is rectangular.\n"
   +"Shop-specific objects: "+json.dumps(entry['visible_shapes'],ensure_ascii=False)+"\n"
   +"Preserve a continuous 2m clear customer route, 1.2m regular doors and 2.4m street main entrance. A door may not be sealed by a display. Toilets are private and accessible from the hall. Lifts are hollow walk-in cabins, not wash basins. Stairs have supported flights, landings and guards. Wet production, food preparation, washing and storage are not guest seating. Show the same layout and objects across the views. Geometry is a review draft; do not invent a claim of construction accuracy.\n"
   +("Current layout input (metres, building local +Y north, front -Y): " if bid=='M1' else "Current layout input (metres, building local +Y north; street front stated above): ")+json.dumps(programme,ensure_ascii=False,separators=(',',':'))+"\n"
   +"Restrained contemporary Taiwanese game-environment art, convincing materials and human-scale details. Warm neutral light, high clarity. All panels must depict the same store. Do not draw orthographic elevations or an exterior cube in all three panels.")
  if reuse_image:prompt=origin_prompt.read_text('utf8')
  prompt_file=folder/'grok-prompt.txt';prompt_file.write_text(prompt,'utf8');entry['prompt_sha256']=hashlib.sha256(prompt_file.read_bytes()).hexdigest();records.append(entry)
 assert records and len({r['id'] for r in records})==len(records)
 report=dict(schema=1,stores=records,store_count=len(records),source_manifest_sha256=hashlib.sha256((OUT/'assembly-manifest.json').read_bytes()).hexdigest(),all_geometry_approved=False)
 (OUT/'store-design-register.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),'utf8')
 cards=[]
 for r in records:
  key=r['id'];folder=CACHE/key;generation={}
  if (folder/'generation.json').is_file():generation=json.loads((folder/'generation.json').read_text('utf8'))
  img='';image_file=folder/'concept.jpg'
  if image_file.is_file():img=f'<a href="../../../{image_file.relative_to(ROOT).as_posix()}"><img loading="lazy" src="../../../{image_file.relative_to(ROOT).as_posix()}" alt="{key} Grok review draft"></a>'
  rawstatus=generation.get('review',{}).get('status',generation.get('status','未生成'))
  current=generation.get('design_sha256')==r.get('image_origin',{}).get('design_sha256',r['design_sha256'])
  visual={};review_file=folder/'visual-review.json'
  if review_file.is_file():visual=json.loads(review_file.read_text('utf8'))
  reviewed=bool(visual) and visual.get('image_sha256')==generation.get('image_sha256')
  if reviewed:rawstatus='已目視審查：造型參考，原始幾何退件'
  findings='<ul>'+''.join('<li>'+html.escape(v)+'</li>' for v in visual.get('observations',[]))+'</ul>' if reviewed else '<p>此版本圖稿尚未目視審查。</p>'
  exact=f'<p><a href="shop-references/{key}-F01-locked.jpg">目前公尺幾何的1F透視圖</a></p>' if key in ('S01','S02','S03') else ''
  photos=''.join(f'<a href="../../../{p["file"]}">已核讀照片</a> ' for p in r['verified_photo_files'])
  target=f'floor-layouts/M1-F{units[key]["floor"]:02d}.svg' if key.startswith('M1-') else f'floor-layouts/{key}.svg'
  cards.append(f'<article data-search="{html.escape(key+" "+r["name"]+" "+r["category"])}"><h2>{key} {html.escape(r["name"])}</h2><p>建物 {r["building_footprint_m"][0]}×{r["building_footprint_m"][1]}m；主要客區 {r["tenant_clear_size_m"][0]:.2f}×{r["tenant_clear_size_m"][1]:.2f}m</p><p><a href="{target}">完整樓層 SVG</a> · <a href="{html.escape(r["reference"]["url"])}">實例來源</a> · <a href="../../../{(folder/"design-input.json").relative_to(ROOT).as_posix()}">設備／動線尺寸</a></p><p>{photos}</p>{img}<p>Grok：{html.escape(rawstatus)}；用途／外框對應現行設計：{"是" if current else "否／未生成"}；輸出SVG另依讀圖結果校正配置。</p>{findings}{exact}<p>尺度：{r["geometry_review_status"]}。圖稿不是 Tripo 成品。</p><details><summary>店型設備與本案推導</summary><p>{html.escape(r["reference"]["design_adaptation"])}</p><pre>{html.escape(json.dumps(r["fixture_counts"],ensure_ascii=False,indent=2))}</pre></details></article>')
 htmltext='<!doctype html><meta charset="utf-8"><title>128 間店面研究與 Grok 審查</title><style>body{background:#f5f1e7;color:#233d41;font:16px Microsoft JhengHei,sans-serif;margin:24px}input{padding:12px;font:inherit;width:360px}main{display:grid;grid-template-columns:repeat(auto-fit,minmax(480px,1fr));gap:20px}article{background:white;padding:20px;border-radius:10px}img{width:100%}a{color:#17697a}h2{font-size:22px}pre{white-space:pre-wrap}</style><h1>128 間店面：研究、配置與圖稿審查</h1><p>48 棟街邊建物＋80 間百貨店。來源參考、設計公尺、Grok 圖稿、實際模型分別記錄；未通過者持續列為待修。</p><p>部分舊店面仍有過大的外框，尺度審查未完成；不以漂亮圖片宣稱已修正。</p><input id="search" placeholder="搜尋 S01、M1-0101 或店型"><main>'+''.join(cards)+'</main><script>document.querySelector("#search").oninput=e=>document.querySelectorAll("article").forEach(a=>a.hidden=!a.dataset.search.toLowerCase().includes(e.target.value.toLowerCase()))</script>'
 htmltext=htmltext.replace('部分舊店面仍有過大的外框，尺度審查未完成；不以漂亮圖片宣稱已修正。','46 棟一般店屋已依店型改為 10–14m 面寬、16–24m 進深；民宿 32×40m、溫泉會館 36×36m。Grok 圖稿另驗門窗、核心與家具，不能用生成成功代替幾何核對。')
 street_count=sum(r['building']!='M1' for r in records);mall_count=len(records)-street_count
 htmltext=htmltext.replace('128 間店面',str(len(records))+' 間店面').replace('48 棟街邊建物＋80 間百貨店',f'{street_count} 棟街邊建物＋{mall_count} 間百貨店').replace('46 棟一般店屋',f'{street_count-2} 棟一般店屋')
 htmltext=htmltext.replace('<h1>','<p><a href="image-to-svg-review.html">逐店 Grok 圖稿轉繪 SVG</a></p><h1>',1) if (OUT/'image-to-svg-review.html').is_file() else htmltext
 (OUT/'store-review.html').write_text(htmltext,'utf8')
 print(json.dumps(dict(stores=len(records),source_downloaded=sum(r['source_page_status']=='downloaded' for r in records),stores_with_reviewed_spatial_photos=sum(bool(r['verified_photo_files']) for r in records)),ensure_ascii=False))
if __name__=='__main__':build()
