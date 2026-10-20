"""Original Grok Build logos and material photographs, with native tool receipts."""
import argparse,concurrent.futures,html,json,shutil,sys
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
from street_city_shop_wall_styles import profile,sha
from street_city_shop_street import inventory
import generate_street_city_store_concepts as engine

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'output/street-social-city-production/shop-brand-grok-v2'
LANGUAGES=[
 ('台灣昭和喫茶','Taiwanese Showa-era kissaten; engraved oval lettering, hand-painted cream enamel, amber, tobacco brown','small irregular deep amber glossy ceramic tiles with thin dark grout'),
 ('白色未來實驗室','white futuristic laboratory; precise custom monogram, acid yellow and electric blue, uncompromising asymmetric geometry','white matte microcement with fine cobalt recycled-glass aggregate speckles'),
 ('南法鄉村','French countryside; lavender botanical watercolor crest, warm linen, sage and chalk lilac','aged ochre limewash plaster with broad naturally uneven trowel marks'),
 ('台式復古辦桌','Taiwanese retro street banquet; vigorous red brush lettering, emerald, celebratory original folk graphics','tiny white and emerald green checkerboard glazed wall tiles with dark weathered joints'),
 ('北歐木工坊','Nordic craft workshop; expressive cut-paper animal or product silhouette, honey birch, pale aqua','vertical blond birch slats of varied narrow widths, recessed charcoal joints'),
 ('黑金精品','black-and-brass boutique; elegant original Art Deco monogram, restrained geometry, black and burnished brass','dark charcoal stone with sparse narrow irregular warm brass inlay lines'),
 ('里斯本彩磚','Portuguese azulejo; original hand-painted blue and terracotta ornamental crest, joyful intricate surface','hand-painted blue and white Portuguese azulejo ceramic tiles with subtle glaze variation'),
 ('街頭滑板塗鴉','youth street skate culture; energetic original sticker-bomb lettering, orange, black, lime, imperfect spray texture','raw gray concrete with irregular faded peach and cobalt original abstract spray-painted patches; no words'),
 ('和風藍染','Japanese indigo craft; original brush emblem and asymmetrical cream washi typography, indigo and ivory','indigo hand-dyed woven fabric wallcovering with subtle sashiko stitched geometric lines'),
 ('熱帶島嶼','tropical island pop; expressive original fruit or leaf mascot, papaya coral, turquoise and sunny yellow','natural woven rattan wallcovering with visible diagonal weave, warm honey tones'),
 ('義式赤陶','Italian artisan atelier; chunky custom serif wordmark, cobalt stamp, warm orange and creamy peach','handmade irregular terracotta rectangular tiles in a herringbone arrangement with pale grout'),
 ('瑞士現代主義','Swiss modernist independent shop; dramatically oversized original letterform and striped red blue graphic mark','warm off-white wall paint with alternating broad red and deep blue diagonal geometric stripes, no text'),
 ('海港鐵工','Taiwan harbor industrial workshop; original utilitarian stencil brand, navy, rust and safety yellow','aged midnight-blue corrugated sheet metal, fine rust only at a few seams, flat front view'),
 ('粉彩孟菲斯','playful Memphis design; original bouncy abstract mascot, mint, lavender and lemon, bold asymmetry','matte blush pink terrazzo wall cladding with large mint, black and cream chips'),
 ('大地侘寂','wabi-sabi earthen atelier; imperfect ink seal and spare organic mark, umber and warm cream','rough earthy taupe rammed-earth wall with subtle horizontal natural sediment layers'),
 ('美式太空年代','American space-age diner; original atomic starburst wordmark, aqua, cherry red and chrome','glossy cream and cherry-red rectangular tiles in alternating vertical paired bands'),
 ('植物銅版畫','Victorian botanical conservatory; detailed original engraved botanical emblem, burgundy, parchment and forest green','aged forest-green botanical patterned wallpaper with delicate original copperplate-style cream leaves'),
 ('數位電玩像素','independent digital arcade; original readable pixel mascot and angular type, hot magenta and near-black','very dark purple wall with sparse regular small cyan and magenta square pixel pattern, no emission glow'),
 ('巴黎黑白印刷','Parisian editorial atelier; bold original high-contrast black ink ligature and expressive typographic mark','ivory painted brick wall with irregular worn black brush bands, no text'),
 ('沙漠西南風','Southwestern desert craft; original ochre sun emblem and thick hand-cut lettering, teal and earthy red','sand-colored stucco with large muted terracotta and teal organic inlaid mosaic shapes'),
 ('銀色科技工業','silver precision workshop; original technical isometric emblem, electric orange, silver and graphite','brushed stainless steel square wall panels with subtle screws and fine flush joints, no reflections of objects'),
 ('古典台灣洗石子','Taiwan mid-century neighborhood heritage; original hand-lettered round seal, celadon, coral and ivory','traditional Taiwanese exposed-aggregate gray-white pebble wall surface with narrow muted celadon mosaic bands'),
 ('夢幻新藝術','Art Nouveau independent boutique; original fluid floral emblem and ornamental custom letterforms, plum and muted gold','muted plum hand-painted wallcovering with fine original flowing gold Art Nouveau botanical ornament'),
 ('戶外山系','outdoor mountain community; original geometric trail badge, burnt orange, moss green and cream','weathered natural cedar wall boards with visible knots, irregular tones and thin recessed joins')]

def spec(bid):
 p=profile(bid);n=int(bid[1:]);index=8 if bid=='S18' else (n*7+p['owner_variant']*5)%len(LANGUAGES);title,language,material=LANGUAGES[index]
 return dict(building=bid,name=p['name'],family=p['family'],direction=title,language=language,material=material,texture_period_m=1.5,
  logo_layout=['large bold freestanding symbol above compact lettering','wide expressive custom wordmark with tiny secondary emblem','asymmetric square seal with integrated lettering','mascot-dominant emblem with very small lettering'][n%4],
  application=['offset near the entrance at eye height','centered high above the main service counter','large vertical identity at the far end wall','low horizontal identity beside the product display'][n%4])

def prepare(bid,kind):
 p=spec(bid);folder=OUT/'assets'/bid/kind;folder.mkdir(parents=True,exist_ok=True)
 common=f"Shop identity: {p['name']} ({bid}), a unique independent Taiwanese {p['family']} shop. Art direction: {p['language']}. This is a new ORIGINAL brand, no existing brand marks. "
 if kind=='logo':
  content=common+f"Create ONE square finished professional brand logo artwork, {p['logo_layout']}. Include the exact Traditional Chinese shop name {p['name']} if possible; do not include the ID {bid}. Use inventive industry-specific symbolism and original custom typography, avoid generic thin-line icons, avoid a pair of framed posters, avoid copying any real trademark. Flat clean ivory background, crisp high quality graphic, generously inset complete silhouette, no scene, no photographic mockup, no wall, no frame, no gradients of illumination. The whole image is the logo artwork, not a brand presentation sheet."
  if bid=='S25':content+=' IMPORTANT: The name must be exactly 四季鮮果, FOUR Chinese characters written ONCE in ONE horizontal line. Do not duplicate 季, do not split the text into multiple lines, no other wording. Inspect the spelling before finishing.'
  if bid=='S86':content+=' IMPORTANT SPELLING: exactly FOUR characters 半 糖 茶 屋 combined as 半糖茶屋, written ONCE on ONE horizontal line. No extra 才 or any other Chinese character. No standalone extra monogram character. Inspect each character before finishing.'
  if bid in ('S83','S85'):content+=' Contrast correction: use very dark charcoal BLACK, thick highly readable lettering and icon contours with vivid orange secondary details. NO pale silver or gray lettering; this logo will be applied on gray steel, so strong dark contrast is essential.'
  if bid=='S82':content+=' Contrast correction: the exact name 南苑藥妝 must use thick very DARK aubergine lettering, not pale pastel lavender. Retain the playful yellow and mint accent graphics, but the name must remain highly readable on pink terrazzo.'
  if bid=='S93':content+=' Contrast correction: use very DARK forest-green thick lettering for the exact name 雨後花店. No pale tan or pastel lettering. Retain the small warm Nordic floral mark. This logo will be applied on honey-colored wood and requires strong dark contrast.'
 else:
  content=common+f"Create ONE square production-ready seamless tileable PBR ALBEDO photograph of {p['material']}. The photographed area is exactly 1.5 metres square. Orthographic straight-on front view, plane fills image to every edge, entirely flat material sample, very fine realistic tactile surface detail and appropriate restrained wear. Completely diffuse neutral flat lighting, NO shadows, NO highlights, NO perspective, NO corners, NO objects, NO logo, NO lettering, NO watermark, NO poster, NO border, NO wainscot, NO room. Seamlessly repeat left-right and top-bottom. This is a base-color texture ONLY, not a multi-map grid or a decorated room."
 prompt='Use image_gen exactly ONCE to generate the following image. Use no shell, no subagents, no other tools. Return only the absolute generated image path.\n'+content
 (folder/'grok-prompt.txt').write_text(prompt,'utf8');(folder/'brief.json').write_text(json.dumps(p,ensure_ascii=False,indent=2),'utf8')
 return dict(id=f'{bid}/{kind}',tools='image_gen',design_sha256=sha(folder/'brief.json'),prompt_sha256=sha(folder/'grok-prompt.txt'))

def publish():
 cards=[];rows=[];adopted=0
 for b in inventory():
  bid=b['building'];p=spec(bid);assets={}
  for kind in ('logo','material'):
   folder=OUT/'assets'/bid/kind;f=folder/'generation.json'
   if f.exists():
    r=json.loads(f.read_text('utf8'));image=folder/'concept.jpg'
    if r['status']=='generated_review_pending' and image.exists() and sha(image)==r['image_sha256']:assets[kind]=r
  rows.append(dict(**p,assets=assets,status='generated' if len(assets)==2 else 'pending'))
  pictures=[]
  for kind in assets:
   if kind=='logo':
    from street_city_localization import OUT as LANGUAGE
    paths={};reviews={};receipt=LANGUAGE/'integrated-logos/jobs'/bid/'variants.json'
    if receipt.exists():
     data=json.loads(receipt.read_text('utf8'))
     for locale,variant in data['variants'].items():
      if locale not in ('zh-Hant','zh-Hans','en'):continue
      approved=data.get('status')=='passed_visual_review' or data.get('locale_reviews',{}).get(locale)=='passed_visual_review'
      candidate=Path(variant['path'])
      if approved and candidate.exists() and sha(candidate)==variant['sha256']:
       paths[locale]='../localization-v1/'+candidate.relative_to(LANGUAGE).as_posix()+'?v='+variant['sha256'][:12]
    paths.setdefault('zh-Hant',f'assets/{bid}/logo/concept.jpg')
    rows[-1]['localized_logo_languages']=list(paths)
    encoded=html.escape(json.dumps(paths),quote=True)
    pictures.append(f'<a class="localized-logo" data-logos="{encoded}" href="{paths["zh-Hant"]}"><img loading="lazy" src="{paths["zh-Hant"]}"><span>原 Grok 品牌｜三語整合 Logo</span><small class="locale-status"></small></a>')
   else:pictures.append(f'<a href="assets/{bid}/{kind}/concept.jpg"><img loading="lazy" src="assets/{bid}/{kind}/concept.jpg"><span>Grok 材質／1.5m</span></a>')
  imgs=''.join(pictures)
  actual='';model=ROOT/f'output/street-social-city-production/{bid}-modular-components-v1/assembly-building'
  try:
   report=json.loads((model/'assembly-report.json').read_text('utf8'));view=json.loads((model/'wall-style-render.json').read_text('utf8'));accept=json.loads((model/'acceptance.json').read_text('utf8'));brand=report['wall_style']['profile']['grok_branding']
   from street_city_grok_brand_materials import active
   if brand==active(bid) and report['approved'] and accept['status']=='passed' and accept['sha256']==view['model_sha256'] and all(brand['assets'][k]['image_sha256']==assets[k]['image_sha256'] for k in ('logo','material')):
    adopted+=1;rel='../'+model.parent.name+'/assembly-building/'
    actual=f'<details><summary>Blender 實際套用預覽</summary><img style="width:100%" loading="lazy" src="{rel}{bid}-wall-detail.png"><a href="{rel}index.html">整棟及每層模型</a></details>'
  except (OSError,KeyError,ValueError):pass
  cards.append(f'<article data-family="{p["family"]}"><h2>{bid} {html.escape(p["name"])}</h2><p>{p["direction"]} · {html.escape(p["application"])}</p><div class="pair">{imgs}</div>{actual}<p><a href="assets/{bid}/logo/brief.json">設計資料</a> · <a href="assets/{bid}/logo/generation.json">Logo生成證據</a> · <a href="assets/{bid}/material/generation.json">材質生成證據</a></p></article>')
 complete=sum(r['status']=='generated' for r in rows)
 approved_logos=sum(set(r.get('localized_logo_languages',[]))=={'zh-Hant','zh-Hans','en'} for r in rows)
 page='<!doctype html><html lang="zh-Hant"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>Grok 逐店品牌與材質</title><style>body{margin:0;background:#172124;color:#f1e9df;font:16px/1.6 system-ui}header,main{max-width:1600px;margin:auto;padding:24px}a{color:#afdfd1}main{display:grid;grid-template-columns:repeat(auto-fit,minmax(380px,1fr));gap:20px}article{padding:20px;background:#263437;border-radius:12px}h2{margin:0;font-size:22px}.pair{display:grid;grid-template-columns:1fr 1fr;gap:10px}.pair img{width:100%;aspect-ratio:1;object-fit:contain;background:#eee}.pair span{display:block}input{padding:12px;font:inherit;width:min(90%,500px)}</style><header><h1>Grok Build：逐店 Logo 與牆面材質</h1><p>'+str(complete)+'/96 間原創素材完成；三語整合 Logo '+str(approved_logos)+'/96。原圖及在地化修稿證據均保留；24種設計語言，依各店用途與個性組合。</p><p>本頁保留 Grok 原創品牌及三語修稿；下方展開可看模型實際套用效果。Grok提供底色圖，PBR伴隨圖另依材質製作。</p><input id="q" placeholder="搜尋店名、ID或風格" oninput="document.querySelectorAll(\'article\').forEach(x=>x.hidden=!x.textContent.toLowerCase().includes(this.value.toLowerCase()))"></header><main>'+''.join(cards)+'</main></html>'
 page=page.replace('<input id="q"','<p><label>Logo 內文字：<select id="logo-language"><option value="zh-Hant">繁體中文</option><option value="zh-Hans">简体中文</option><option value="en">English</option></select></label> · <a href="../localization-v1/index.html">全部建築文字貼圖</a></p><p>三語 Logo 沿用各店原始 Grok 圖案、配色與整合字體；Grok image_edit 初稿再以 GPT 修正錯字，保留來源與修稿紀錄。語言選單直接切換圖案內的文字，未驗收版本不會冒充完成品。</p><input id="q"')
 page=page.replace('</html>','''<script>document.getElementById('logo-language').onchange=function(){const locale=this.value;document.documentElement.lang=locale;localStorage.setItem('StreetCityLocale',locale);document.querySelectorAll('.localized-logo').forEach(a=>{const paths=JSON.parse(a.dataset.logos),img=a.querySelector('img'),status=a.querySelector('.locale-status');if(paths[locale]){img.hidden=false;img.src=paths[locale];a.href=paths[locale];status.textContent='已驗收整合字體：'+locale;}else{img.hidden=true;a.removeAttribute('href');status.textContent='此Logo的 '+locale+' 版尚在修稿驗收';}});};const saved=localStorage.getItem('StreetCityLocale');if(['zh-Hant','zh-Hans','en'].includes(saved))document.getElementById('logo-language').value=saved;document.getElementById('logo-language').onchange();</script></html>''')
 (OUT/'index.html').write_text(page,'utf8');(OUT/'inventory.json').write_text(json.dumps(dict(status='generated' if complete==96 else 'in_progress',completed=complete,buildings=rows),ensure_ascii=False,indent=2),'utf8')
 return complete

def main():
 a=argparse.ArgumentParser();a.add_argument('--ids',nargs='*');a.add_argument('--publish-only',action='store_true');args=a.parse_args();OUT.mkdir(exist_ok=True,parents=True)
 if args.publish_only:print(publish());return
 ids=args.ids or [b['building'] for b in inventory()];records=[prepare(bid,kind) for bid in ids for kind in ('logo','material')]
 engine.CACHE=OUT/'assets';exe=shutil.which('grok.exe') or shutil.which('grok');assert exe
 def generate(record):
  result=None
  for attempt in range(3):
   result=engine.one(record,exe)
   if result['status']=='generated_review_pending':return result
  return result
 completed=[]
 with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
  for future in concurrent.futures.as_completed([pool.submit(generate,r) for r in records]):
   completed.append(future.result());publish()
 bad=[r for r in completed if r['status']!='generated_review_pending']
 (OUT/'batch.json').write_text(json.dumps(dict(status='failed' if bad else 'generated_review_pending',images=len(completed),errors=bad),ensure_ascii=False,indent=2),'utf8')
 assert not bad,bad

if __name__=='__main__':main()
