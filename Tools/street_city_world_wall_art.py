"""Original Grok wall art catalogue; no lettering and no changes to geometry."""
import json,hashlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'output/street-social-city-production/world-wall-grok-v1'
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
CITY={
 'H1':('Luxury Taiwanese hot-spring resort, champagne gold, mineral jade and warm ivory',[
  'honed warm cream travertine large panels, fine warm bronze linear inlays, quiet sophisticated mineral strata',
  'jade green and deep petrol mineral-stone mosaic with delicate pale gold original flowing water motifs, refined five-star spa luxury',
  'warm champagne silk-textured wallcovering with sparse large original muted celadon ginkgo leaves, premium calm guest-room material',
  'small glossy jade and pale aqua porcelain mosaic tiles with restrained cream grout, washable luxury spa wall']),
 'R1':('Young professionals rental apartments, terracotta, denim blue and pale oak',[
  'muted terracotta fired-brick rainscreen with selected deep denim blue glazed brick accents, fine flush pale mortar',
  'ochre and burnt orange mineral plaster with large friendly original geometric clay and denim-blue inlaid shapes, apartment shared lounge',
  'sage and sand woven-linen wallcovering with small quiet original asymmetrical geometric pattern, residential and cozy',
  'small dusty blue and cream rectangular glazed ceramic tiles, handmade color variation, clean washable residential wall']),
 'R2':('Family river-view apartments, sea glass green, dusty coral and sandy cream',[
  'pale sand exposed-aggregate precast cladding with narrow sea-glass green ceramic tile horizontal courses, Taiwanese family housing',
  'muted coral and sea-glass green large rounded original abstract botanical terrazzo inlaid pattern in creamy plaster, friendly residential lobby',
  'warm muted mint mineral limewash with delicate original cream leaf line ornament, calm family living space',
  'small pearl-white ceramic tiles with soft seafoam and pastel coral tile accents, clean family bathroom wall']),
 'O01':('Refined business headquarters, charcoal blue, copper and smoked stone',[
  'deep blue-gray honed granite facade panels with fine copper-colored recessed joints, elegant high-end office architecture',
  'navy blue stone with very thin original restrained copper Art Deco geometric inlay, executive business lobby',
  'smoky petrol-blue wool acoustic fabric panels with fine vertical texture and restrained graphite seams, premium meeting room',
  'charcoal slate and off-white rectangular porcelain tiles with very fine copper accent inlay, upscale office washroom']),
 'O02':('Youth startup incubator, cobalt, saffron and warm birch, energetic yet professional',[
  'warm cream fiber-cement rainscreen panels with sparse cobalt blue and saffron ceramic linear accents, contemporary startup building',
  'large bold original cobalt and saffron asymmetric geometric shapes on muted warm birch-colored plaster, startup collaboration space',
  'muted turquoise textured acoustic felt with original small saffron and cream modular geometric inlays, productive coworking space',
  'matte aqua and warm ivory square washable ceramic tiles with sparse saffron accents, cheerful professional washroom']),
 'O03':('Riverside enterprise offices, oxidized teal, walnut and pale stone',[
  'weather-resistant oxidized teal-green architectural metal panels with subtle natural patina, warm pale limestone insert bands',
  'deep walnut-colored finely ribbed wood veneer with elegant original sea-green ceramic geometric inserts, upscale enterprise reception',
  'pale muted teal mineral plaster with fine textured grasscloth insets and thin walnut vertical bands, calm business interior',
  'teal and pale gray elongated porcelain tiles with subtle stone grain, clean premium business washroom']),
 'CV':('Civic arts and community cultural center, expressive cobalt, warm clay and verdigris',[
  'handmade warm terracotta architectural tiles with sparse cobalt and pale turquoise glazed inserts, contemporary Taiwanese civic arts facade',
  'original large-scale abstract ceramic mosaic of flowing cobalt, coral, golden ochre and cream shapes, sophisticated public cultural art wall',
  'soft warm coral textured mineral plaster with original muted indigo paper-cut botanical geometry, community reading lounge',
  'clean cream and cobalt ceramic square tiles with small original cheerful geometric accent tiles, public culture-center washroom']),
 'TR':('University-town transport interchange, clean durable indigo, safety amber and ivory',[
  'durable deep indigo glazed ceramic architectural wall panels with sparse muted amber and ivory horizontal accent bands',
  'large original abstract travel-route geometry with indigo, muted amber and ivory terrazzo inlaid shapes, transport-station public wall, no map labels',
  'quiet pale blue-gray acoustic perforated mineral panels with fine regular holes and restrained indigo modular seams, station waiting area',
  'durable off-white and deep indigo small porcelain wall tiles with restrained amber accent course, station public washroom'])}
CAMPUS={
 'E1':('Mechanical and materials engineering','rust red, steel blue and warm gray','original precise large mechanical linkage-inspired geometric inlaid motif'),
 'EE':('Electrical electronics and photonics','deep cobalt, turquoise and ivory','original restrained circuit-network-inspired geometric inlay, no symbols or digits'),
 'MA':('Management and business studies','burgundy, warm tan and muted gold','original refined interlocking rectangular grid ornament'),
 'T4':('Humanities foreign languages and computing','saffron, deep indigo and parchment','original abstract interwoven ribbon and paper-cut forms'),
 'T2':('Technology teaching and creative design','teal, burnt orange and warm white','original asymmetrical Bauhaus geometric mural shapes'),
 'IB':('International academic services','muted ultramarine, jade and sandy cream','original abstract interlocking wave mosaic, international and welcoming'),
 'T1':('Engineering and automation teaching','charcoal blue, terracotta and cream','original fine modular engineering-grid inlays'),
 'RB':('Architecture design and research','forest green, ochre and dusty coral','original architectural abstract layered geometric composition'),
 'AD':('University central administration','warm gray limestone, deep green and muted bronze','original discreet thin bronze geometric inlay, dignified and welcoming'),
 'LI':('University library and reading rooms','deep forest green, walnut and parchment','original delicate quiet botanical line motifs with substantial calm negative space'),
 'SC':('Student center clubs and food services','teal, paprika orange and mustard','original playful large social interlocking rounded abstract shapes'),
 'IN':('Interdisciplinary advanced research','muted electric blue, silver gray and warm white','original precise cellular geometric ornament, no text'),
 'SP':('Indoor swimming and multisport health center','cobalt blue, aqua and warm terracotta','original dynamic abstract broad wave pattern, clean washable athletic environment'),
 'D1':('Residential student dormitory one','muted lavender, sage and cream','original subtle gingko and flowing leaf pattern, calm homelike space'),
 'D2':('Residential student dormitory two','denim blue, ochre and sand','original subtle offset rectangular woven pattern, calm homelike space'),
 'D3':('Graduate student residence','forest green, dark teal and parchment','original quiet botanical geometric pattern, restful scholarly space'),
 'D4':('International student residence','dusty coral, turquoise and cream','original warm interlocking woven pattern, restful welcoming home')}
SHOP={
 'coffee':'warm amber glazed tiny tiles and walnut-toned geometric inlay, independent coffee and book shop',
 'tea':'deep tea-green handmade ceramic tiles, sparse cream botanical-shaped mosaic inserts',
 'bakery':'warm butter-yellow limewash and muted orange terracotta herringbone inlays, artisan bakery',
 'diner':'mid-century Taiwanese jade and ivory ceramic tile checker bands with muted brick-red accent tiles',
 'produce':'olive green and muted citrus-colored mosaic in creamy exposed-aggregate render, fresh fruit shop',
 'convenience':'muted turquoise and cream washable mineral panels with restrained terracotta neighborhood geometric accents',
 'book':'burnished burgundy limewash with warm parchment-colored hand-made small geometric inlays, literary bookshop',
 'pet':'warm sand and soft terracotta washable ceramic wall with original small abstract paw-like rounded geometric pattern',
 'floral':'forest-green mineral plaster and original delicate cream botanical line motifs, independent flower shop',
 'fashion':'muted wine-red and pink hand-painted abstract linen wallcovering, original artful independent fashion store',
 'salon':'dusty rose plaster and deep teal terrazzo inlaid broad curves, fashionable neighborhood hair salon',
 'music':'exposed warm red brick with original indigo and ochre abstract rhythmic geometric inlays, independent music store',
 'games':'matte midnight violet mineral wall panels with original muted cyan and orange pixel-like geometric details, no glow',
 'photo':'warm charcoal and gray exposed-aggregate wall with precise copper and cream geometric inlays, film photography shop',
 'bike':'muted navy corrugated weather-resistant architectural metal with sparse rust orange geometric accent bands',
 'pharmacy':'sage green and warm ivory ceramic wall tiles with small clean muted coral geometric accent inserts',
 'laundry':'powder-blue washable ceramic tiles with small cream and muted yellow alternating vertical bands',
 'inn':'warm earthen ochre plaster with original subtle indigo traditional woven geometric inlays, quiet Japanese guesthouse',
 'spa':'jade mineral-stone mosaic and original thin warm cream abstract flowing water bands, quiet onsen club',
 'lottery':'warm cream and faded cinnabar red glazed tile mosaic with subtle original muted gold geometric accents, local neighborhood shop'}

def catalogue():
 rows=[]
 def add(key,building,role,brief,area=2,rough=.6):
  rows.append(dict(id=key,building=building,role=role,brief=brief,period_m=[area,area],roughness=rough))
 for bid,(theme,descriptions) in CITY.items():
  for role,desc in zip(['facade','lobby','quiet','wet'],descriptions):add('city/'+bid+'/'+role,bid,role,theme+'. '+desc,2,.3 if role=='wet' else .62)
 for bid,(theme,colors,pattern) in CAMPUS.items():
  add('campus/'+bid+'/facade',bid,'facade',theme+' Taiwanese university facade: weather-resistant architectural ceramic brick and precast cladding, '+colors+', refined durable color distribution, no artwork applied to windows',2,.6)
  finish='washable porcelain mosaic tiles' if bid=='SP' else 'textured mineral plaster and glazed ceramic inlaid artwork'
  add('campus/'+bid+'/interior',bid,'interior',theme+' interior wall: '+finish+', '+colors+', '+pattern,2,.4 if bid=='SP' else .72)
 for family,desc in SHOP.items():add('shops/'+family+'/facade',family,'facade',desc+'. Weather-resistant street-shop upper-story wall material, distinctive but restrained enough to accompany an existing original shop logo',2,.58)
 for role,desc in [
  ('public','honed warm champagne travertine with original delicate muted bronze and jade stone geometric inlay, quiet five-star department-store public circulation'),
  ('gallery','warm ivory silk-textured mineral plaster with original sparse champagne-gold fan-shaped art-deco ornament, very expensive luxury department-store gallery wall'),
  ('restaurant','deep warm burgundy and cream handmade glazed ceramic with original fine bronze geometric accent lines, luxury restaurant floor public wall'),
  ('wet','pearl-white and deep jade marble-look porcelain tile mosaic with narrow warm bronze accent joints, luxury department-store washroom wall')]:add('mall/M1/'+role,'M1',role,desc,2,.4)
 assert len(rows)==90,len(rows)
 return rows

def prepare():
 OUT.mkdir(parents=True,exist_ok=True);rows=catalogue();jobs=[]
 for r in rows:
  folder=OUT/'assets'/r['id'];folder.mkdir(parents=True,exist_ok=True)
  prompt='Use image_gen exactly ONCE. No shell, no subagents, no other tools. Return only the absolute generated image path.\nCreate ONE original production-ready seamless square wall ALBEDO texture. Design: '+r['brief']+'. The entire photographed surface is exactly 2 metres square. Rich believable material and pigment variation, sophisticated intentional colors, fine tactile detail. Flat straight-on orthographic view, perfectly planar, full bleed material to all four edges. Neutral uniform diffuse illumination. No baked shadows or highlights, no reflections of rooms, no corners, no perspective, no furniture, no doors or windows, no plant objects, no people, no frames. ABSOLUTELY NO writing, no letters, no numbers, no logo, no watermark. No text even as decoration. Do not generate a room or a multi-map grid. Seamlessly repeat on both axes. Only the base-color image, no normal-map look. Use realistic material colors without oversaturation.\n'
  (folder/'grok-prompt.txt').write_text(prompt,'utf8');(folder/'brief.json').write_text(json.dumps(r,ensure_ascii=False,indent=2),'utf8')
  jobs.append(dict(id=r['id'],tools='image_gen',design_sha256=sha(folder/'brief.json'),prompt_sha256=sha(folder/'grok-prompt.txt')))
 (OUT/'catalogue.json').write_text(json.dumps(dict(status='planned',images=rows,jobs=jobs,scope='90 new original no-text Grok wall textures, all 122 buildings; retain existing integrated localized shop logos and luxury tenant artwork'),ensure_ascii=False,indent=2),'utf8')
 return jobs
if __name__=='__main__':prepare()
