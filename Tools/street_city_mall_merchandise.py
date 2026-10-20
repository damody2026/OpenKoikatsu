"""Bounded, semantic merchandise catalogue shared by generation and assembly.

The limit counts distinct product models, not instances on shelves. Missing
products are Tripo Model jobs; colour changes never masquerade as new models.
"""
import json, hashlib
from pathlib import Path
from street_city_building_context import ROOT

OUT=ROOT/'output/street-social-city-production/mall-v1/merchandise-v1'
LIMIT=100
# id, physical XYZ metres, runtime triangle ceiling, recognizable description.
DEFINITIONS=[
 ('perfume',[.075,.045,.13],500,'An elegant amber glass perfume bottle with a faceted clear glass stopper and a small gold atomizer collar'),
 ('lipstick',[.025,.025,.085],400,'An uncapped luxury lipstick with visible rose-red angled lipstick bullet above its black and gold cylindrical case'),
 ('skincare',[.065,.065,.11],400,'A pearl white cosmetic cream jar with a separate-looking polished gold lid, realistic glass and cream materials'),
 ('brushes',[.14,.09,.22],600,'A small ceramic holder containing five realistic makeup brushes with distinct soft brown bristle heads and gold ferrules'),
 ('necklace',[.16,.12,.24],1500,'A gold necklace with a blue teardrop gemstone draped on a charcoal velvet jewellery display bust'),
 ('watch',[.09,.095,.13],650,'A realistic silver wristwatch with visible white dial, hour hands and dark leather strap wrapped around a charcoal jewellery cushion'),
 ('gift',[.24,.2,.16],400,'A premium ivory gift box with a sculpted burgundy satin ribbon and clearly looped bow'),
 ('shirt',[.60,.18,.72],800,'A single short sleeved ivory cotton shirt on a wooden clothes hanger, empty inside, clearly open neck and sleeve cuffs, natural fabric folds'),
 ('dress',[.48,.19,1.25],1000,'A single elegant burgundy sleeveless evening dress on a wooden clothes hanger, fitted waist, draped flared skirt, natural fabric folds, no person'),
 ('blazer',[.55,.22,.78],1000,'A single tailored navy suit jacket on a wooden clothes hanger, lapels, front buttons, pockets, natural woven fabric folds, no person'),
 ('sportswear',[.58,.18,.72],800,'A single charcoal technical sports zip-up jacket on a wooden clothes hanger, contrasting teal panels and natural fabric folds, no person'),
 ('lingerie',[.5,.2,1.05],1000,'A single premium unisex ivory cotton bathrobe on a wooden hanger, full long sleeves, thick shawl collar, two pockets and tied waist belt'),
 ('jeans',[.38,.16,.95],800,'A single pair of blue denim jeans suspended from a wooden clip hanger, two distinct trouser legs, seam stitching, belt loops and pockets, no person'),
 ('accessories',[.26,.2,.18],650,'A premium camel felt fedora hat with a sculpted brim, indented crown and burgundy hat band'),
 ('sneaker',[.12,.3,.13],850,'A single premium white and navy running sneaker with rubber sole tread, visible laces, breathable fabric and leather panels, no foot'),
 ('loafer',[.105,.29,.105],700,'A single polished dark brown leather penny loafer shoe with stitched upper, realistic heel and leather outsole, no foot'),
 ('heel',[.20,.25,.18],750,'A single elegant black leather high heel pump with pointed toe, visible empty shoe opening and slender heel, no foot'),
 ('handbag',[.32,.13,.34],850,'A structured burgundy luxury leather handbag with two curved handles, gold clasp, gussets and visible leather stitching'),
 ('luggage',[.4,.25,.62],850,'A premium graphite ribbed hard shell cabin suitcase with four small spinner wheels, top carry handle and extended telescopic handle'),
 ('glasses',[.145,.145,.045],600,'A pair of realistic tortoiseshell eyeglasses unfolded for display, two transparent lenses, sculpted bridge and both curved temples'),
 ('cookware',[.3,.23,.17],650,'A polished stainless steel cooking pot with a fitted lid, central lid knob and two loop side handles'),
 ('vase',[.15,.15,.28],500,'A sculptural ivory ceramic vase with a narrow visibly open neck, flared lip and round asymmetric belly'),
 ('diffuser',[.095,.095,.28],450,'An amber glass reed diffuser bottle with six slender dark reeds visibly protruding above its open neck'),
 ('books',[.32,.24,.31],1500,'A retail group of five upright hardcover books with distinct cloth covers, curved spines, white page edges and differing heights, no lettering'),
 ('stationery',[.28,.2,.08],600,'A compact premium stationery display set containing a closed stitched notebook, two metal fountain pens and a small eraser, arranged flat'),
 ('art-kit',[.24,.17,.12],650,'An open wooden artist paint box with visibly different paint colour pans, palette and two brushes resting on its edge'),
 ('boardgame',[.34,.27,.09],600,'An open premium board game box with a visible checkered folding board, several coloured pawns and dice, no lettering'),
 ('phone',[.075,.012,.155],450,'A modern smartphone standing vertically on its bottom edge, dark blue rounded metal body, glass screen with colourful non-text app icons and triple rear camera lenses'),
 ('laptop',[.33,.24,.23],700,'A realistic open silver laptop with visible dark keyboard, thin hinged display at a 110 degree opening angle and non-text abstract desktop wallpaper'),
 ('headphones',[.19,.09,.22],750,'A pair of premium over ear headphones with curved padded headband, two soft oval earcups and polished dark metal hinges'),
 ('console',[.26,.21,.1],700,'A compact modern white video game console beside a single dark game controller with two joysticks and coloured buttons, no text'),
 ('smart-speaker',[.12,.12,.2],500,'A cylindrical smart home speaker with woven grey acoustic fabric, rounded top and four small illuminated circular control symbols, no text'),
 ('repair-kit',[.3,.2,.05],650,'An open compact electronics repair toolkit with precision screwdriver, tweezers, multicolour small driver bits and magnifying lens neatly fitted in a black tray'),
 ('toy-car',[.22,.105,.08],700,'A realistic red die cast miniature sports car with four black tyres, glass windows, headlights and detailed body panels'),
 ('plush',[.22,.16,.3],900,'A premium soft beige teddy bear toy sitting upright, two rounded ears, stitched dark eyes and nose, fabric texture and a small burgundy neck bow'),
 ('figurine',[.15,.13,.28],900,'An original collectible small friendly teal astronaut figurine standing on its own two boots, round helmet visor and detailed orange backpack, no existing franchise'),
 ('blocks',[.26,.18,.2],600,'An assembled colourful educational building block toy house with studded plastic bricks, a red pitched roof, doorway and two windows, no lettering'),
 ('ramen',[.26,.26,.12],900,'A realistic ceramic bowl of Japanese ramen containing visible yellow noodles, two pork slices, half a boiled egg, green scallions and dark seaweed sheets'),
 ('curry',[.29,.24,.06],850,'A realistic white oval plate of Japanese curry rice with distinct mound of white rice, brown curry sauce, carrot chunks and golden cutlet slices'),
 ('meal',[.32,.25,.065],850,'A realistic Taiwanese ceramic meal plate with rice, glazed sliced chicken, green vegetables and a halved braised egg in clearly separate portions'),
 ('pasta',[.28,.28,.07],850,'A realistic white ceramic plate of spaghetti with red tomato sauce, several visible meatballs, grated cheese and green basil leaves'),
 ('salad',[.26,.26,.09],850,'A realistic shallow ceramic bowl of fresh salad with recognizable green lettuce, red cherry tomato halves, sliced cucumber and yellow sweetcorn'),
 ('cake',[.16,.13,.12],750,'A realistic single strawberry layer cake slice on a small white plate, distinct sponge and cream layers with two glossy red strawberry halves on top'),
 ('tea',[.09,.09,.19],650,'A realistic clear plastic cup of Taiwanese bubble milk tea with visible dark tapioca pearls, tan milk tea, flat sealed lid and one thick burgundy straw'),
 ('sushi',[.3,.2,.065],850,'A premium dark rectangular ceramic platter holding four clearly separated realistic nigiri sushi pieces with salmon, tuna and egg toppings on white rice'),
 ('steak',[.3,.26,.065],850,'A realistic premium white ceramic dinner plate with grilled steak showing grill marks, three asparagus spears and small roast potatoes'),
 ('coffee',[.15,.12,.09],650,'A realistic ivory porcelain cappuccino cup on a matching saucer, visible loop handle and brown coffee with white heart latte art'),
 ('cocktail',[.09,.09,.19],650,'A realistic elegant stemmed cocktail glass with amber drink, a sculpted orange peel garnish and slender stem above a circular foot'),
 ('sofa',[2.1,.9,.9],1500,'A single premium ivory boucle upholstered three-seat sofa with separate soft cushions, rounded armrests and four short dark wood legs'),
 ('floor-lamp',[.55,.55,1.7],1500,'An elegant brass floor lamp with a circular heavy base, slender pole and an ivory fabric tapered round lampshade'),
 ('bed',[2.1,2.3,1.1],1500,'A premium king-size bed with padded ivory headboard, dark wood frame, cream duvet, four arranged pillows and burgundy throw'),
 ('folded-shirt',[.35,.25,.05],500,'A neatly folded navy cotton polo shirt with a curved collar, white button placket, tucked sleeves, layered edges and soft fabric wrinkles'),
]
REUSE=[
 ('flowers','shop-street-components-v1/flowers',[.35,.35,.65]),
 ('camera','shop-street-components-v1/camera',[.14,.11,.095]),
 ('croissant','shop-street-components-v1/croissant',[.18,.11,.065]),
 ('apple','shop-street-components-v1/apple',[.085,.085,.09]),
 ('laundry','campus-v1/components/tripo-laundry-appliance',[.7,.75,1]),
]
# One list per tenant, in the same order as the building's explicit programme.
ASSIGNMENTS=[
 ['perfume','lipstick brushes','skincare perfume','brushes','necklace','watch','flowers gift','gift vase'],
 ['shirt jeans','blazer shirt','dress blazer','dress','sportswear','lingerie','blazer dress','accessories handbag'],
 ['shirt jeans','blazer shirt','shirt accessories','sportswear','blazer','watch accessories','jeans','sportswear'],
 ['sneaker','loafer','heel','handbag','luggage','handbag gift','glasses','necklace watch'],
 ['sofa vase','floor-lamp','bed','cookware','vase cookware','diffuser','flowers vase','vase gift'],
 ['books','books figurine','books','stationery','stationery books','art-kit','coffee croissant','boardgame'],
 ['phone','laptop','camera','headphones','laundry cookware','console','smart-speaker phone','repair-kit'],
 ['toy-car','blocks','figurine','plush blocks','boardgame','console','plush','art-kit blocks'],
 ['ramen tea','curry tea','meal tea','meal salad','pasta salad','salad apple','cake coffee','tea cake'],
 ['steak salad','sushi tea','meal tea','steak','pasta salad','coffee cake','cocktail','meal sushi'],
]

def catalogue():
 rows=[dict(id=q[0],size=q[1],triangle_cap=q[2],generation_faces=1500,
  provider='Tripo Model',prompt=q[3]+'. Isolated complete realistic PBR retail product. No people, no surrounding furniture, no room, no plinth, no ground plane, no logos, no written letters. Preserve all distinct functional shapes.',
  source_status='pending_generation') for q in DEFINITIONS]
 rows += [dict(id=aid,size=size,provider='reuse',folder=str(ROOT/'output/street-social-city-production'/folder),source_status='existing_remesh_requires_verification') for aid,folder,size in REUSE]
 ids={q['id'] for q in rows};assert len(ids)==len(rows)<=LIMIT
 tenants={f'M1-{f+1:02d}{i+1:02d}':names.split() for f,row in enumerate(ASSIGNMENTS) for i,names in enumerate(row)}
 folded_stores={'M1-0201','M1-0202','M1-0203','M1-0205','M1-0301','M1-0302','M1-0303','M1-0304','M1-0307'}
 for rid,required in tenants.items():
  if rid in folded_stores:required.append('folded-shirt')
 assert len(tenants)==80 and all(set(v)<=ids for v in tenants.values())
 return dict(schema=1,unique_product_model_limit=LIMIT,unique_product_models=len(rows),new_tripo_models=len(DEFINITIONS),reused_models=len(REUSE),limit_scope='Distinct product models, not repeated display instances. Variants reuse the same source mesh.',texture_max_px=2048,floor_triangle_limit=100000,components=rows,tenants=tenants)

def publish():
 OUT.mkdir(parents=True,exist_ok=True);data=catalogue()
 path=OUT/'catalogue.json'
 previous=json.loads(path.read_text('utf8')) if path.exists() else {}
 old={q['id']:q for q in previous.get('components',[])}
 jobs=json.loads((OUT/'jobs.json').read_text('utf8')) if (OUT/'jobs.json').exists() else {}
 adjustments=json.loads((OUT/'budget-adjustments.json').read_text('utf8')) if (OUT/'budget-adjustments.json').exists() else {}
 revisions=json.loads((OUT/'revisions.json').read_text('utf8')) if (OUT/'revisions.json').exists() else {}
 for q in data['components']:
  for key in ['job','job_url','download','source_sha256','source_status','visual_review']:
   if key in old.get(q['id'],{}):q[key]=old[q['id']][key]
  if q['id'] in jobs:
   q['job']=jobs[q['id']];q['job_url']='https://studio.tripo3d.ai/workspace/generate/'+q['job']
  if q['id'] in adjustments:q['triangle_cap']=adjustments[q['id']]['cap']
  if q['id'] in revisions:
   rev=revisions[q['id']];q.update(revision=rev['revision'],job=rev['job'],job_url='https://studio.tripo3d.ai/workspace/generate/'+rev['job'],revision_reason=rev['reason'])
 path.write_text(json.dumps(data,ensure_ascii=False,indent=2),'utf8')
 import html
 cards=[]
 for q in data['components']:
  cards.append('<article><h2>'+html.escape(q['id'])+'</h2><p>'+html.escape(q['provider'])+' · '+html.escape(str(q['size']))+' m · '+str(q.get('triangle_cap','existing'))+' triangles</p><p>'+html.escape(q.get('source_status',''))+'</p><p>'+html.escape(q.get('prompt','Existing certified component'))+'</p></article>')
 (OUT/'index.html').write_text('<!doctype html><meta charset="utf-8"><title>M1 商品元件清單</title><style>body{background:#181c23;color:#eee;font:16px system-ui;margin:32px}main{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:16px}article{background:#262d37;border:1px solid #697484;padding:20px;border-radius:12px}p{line-height:1.5}</style><h1>百貨商品：'+str(data['unique_product_models'])+' / 100 種模型</h1><p>52 件缺少商品以 Tripo Model 生成，5 件複用；此頁是生產清單，pending 不代表已完成模型。</p><main>'+''.join(cards)+'</main>','utf8')
 return data

def folder_for(row):
 return Path(row['folder']) if row['provider']=='reuse' else OUT/'components'/(row['id']+('-'+row['revision'] if row.get('revision') else ''))

def download_name(row):
 return 'M1-product-'+row['id']+('-'+row['revision'] if row.get('revision') else '')+'-source.glb'

if __name__=='__main__':publish()
