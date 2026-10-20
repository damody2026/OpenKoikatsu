"""Store-specific retail fixtures and references, shared by SVG, GLB and Grok.

Case photographs establish typology, not unmarked metre measurements. The
dimensions below are this game's design dimensions. An equipment envelope is
not a licence to deliver an opaque cube: shape requirements travel with it.
"""
from collections import Counter
from street_city_site import EXTRA_SHOPS
from research_street_city_stores import CASES

FAMILY={f'S{i:02d}':f'S{i:02d}' for i in range(1,21)}
FAMILY.update({f'S{i:02d}':base for i,(_,base) in enumerate(EXTRA_SHOPS,21)})

# key: name, envelope (metres), visible construction required in final asset.
EQUIPMENT={
 'perfume':('香水試香桌',[2.2,.8,1.05],'thin top, open metal legs, separate bottles and tester strips'),
 'makeup':('彩妝試妝鏡台',[1.8,1.6,1.65],'illuminated mirror, stool, brushes and sample drawers'),
 'skincare':('保養品試用洗手台',[2.4,.8,1.1],'bottles, basin depression, curved tap and towels'),
 'jewelry':('珠寶玻璃展示櫃',[1.8,.7,1.1],'transparent glazing, slim frame and individual velvet jewelry stands'),
 'watch':('手錶展示與試戴台',[1.8,1.2,1.1],'glazed inset, watch cushions, customer stool'),
 'gift':('精品禮品展示台',[1.6,.8,1.1],'open shelves, ceramics and paper gift packages'),
 'garment':('服裝吊桿架',[1.8,.65,1.75],'thin tubular rail, individual hanging garments, open bottom'),
 'fashion_table':('服裝摺衣展示桌',[1.8,.9,.8],'table legs, folded fabric stacks'),
 'mannequin':('服飾人台與底座',[.65,.65,1.85],'human torso and fabric silhouette, not a rectangular pillar'),
 'fabric':('訂製布料與量身桌',[2.4,1.8,1.1],'fabric books, measuring tape, table and two chairs'),
 'shoe':('鞋履展示層架',[1.8,.45,1.8],'thin shelf brackets, individual left and right shoe silhouettes'),
 'shoe_seat':('試鞋座椅與鏡面',[1.6,1.4,1.0],'upholstered bench, low tilted mirror, shoehorn'),
 'bag':('手袋開放陳列架',[1.6,.5,1.8],'open shelves and separate curved bag bodies with handles'),
 'luggage':('旅行箱展示組',[1.8,.85,1.1],'two rounded suitcase bodies, wheels and telescoping handles'),
 'leather':('皮革手作工作桌',[2.4,1.8,1.1],'wood table, stools, leather sheets and hand tools'),
 'eyewear':('眼鏡試戴鏡台',[1.8,.8,1.65],'individual eyeglass frames, mirror, open-leg counter'),
 'sofa_display':('沙發情境展示組',[3.2,3.2,1.0],'upholstered curved sofa, coffee table and rug'),
 'lamp':('燈飾試點展示組',[2,1,2.3],'thin lamp stems, shade domes and separate bulbs'),
 'bed_display':('寢具試躺床',[2.2,2.4,1.2],'mattress layers, pillows, draped textile and headboard'),
 'kitchen_demo':('廚具情境示範檯',[3.6,2.4,2.2],'recessed sink and cooktop, cabinet joints, extractor'),
 'homeware':('生活器皿開放展架',[1.8,.6,1.8],'thin shelves, bowls, plates and household objects'),
 'plant':('植栽分層展示台',[1.6,.8,1.6],'pots of different diameters, leaves, stepped open stand'),
 'book':('書籍開放書架',[1.8,.4,1.8],'thin shelves, separately modelled book spines, open front'),
 'book_table':('選書低展示桌',[1.8,.9,.8],'thin tabletop, legs and stacked books'),
 'stationery':('文具試寫展台',[1.8,1.0,1.0],'pen trays, paper, drawers and sample notebooks'),
 'art':('美術紙材與畫具架',[1.8,.6,1.8],'flat paper drawers, rolled papers, brushes and paint tubes'),
 'phone':('手機試用長桌',[2.4,1,1.0],'thin open-leg table, individual phones and cable sockets'),
 'computer_demo':('電腦試用桌',[2.4,1.4,1.3],'laptop screens, keyboard, demo monitor and open legs'),
 'camera_case':('相機玻璃展櫃',[1.8,.7,1.1],'glazing, round lenses and camera bodies'),
 'audio':('音響試聽組',[3.2,3.2,1.8],'two separate speakers with circular drivers, amplifier and listening seat'),
 'appliance':('家電操作展示檯',[2.4,1,1.3],'small appliances, handles, controls and power points'),
 'console_demo':('遊戲主機試玩組',[2.4,2.2,1.6],'screen, separate console, controllers, two seats'),
 'repair_elec':('電器維修工作站',[2.4,1.8,1.5],'task bench, stool, tools, anti-static mat and parts drawers'),
 'model':('模型透明展示櫃',[1.8,.6,1.8],'transparent glazing and individually posed model figures'),
 'brick':('積木體驗桌',[2,1.8,.8],'low child-height table, stools, colourful individual bricks'),
 'puzzle':('拼圖體驗桌',[2,1.8,.9],'table, two chairs and laid-out puzzle pieces'),
 'boardgame':('桌遊四人桌椅組',[2.4,2.4,1.0],'four separate chairs, table legs, cards, game pieces'),
 'claw':('透明夾物機',[1.0,1.0,2.1],'transparent upper chamber, suspended claw, prizes and controls'),
 'craft':('親子手作桌椅組',[2.4,2.4,1.0],'rounded tabletop, four stools, tools and material trays'),
 'dining_store':('餐飲四人桌椅組',[2.4,2.4,1.05],'table legs, four chairs and table settings'),
 'bar_seat':('吧台雙座組',[1.6,1.4,1.2],'narrow top and two separate stools'),
 'coffee_bar':('咖啡製作吧台',[2.4,.9,1.5],'espresso group heads, grinder, sink, cupboards and open customer front'),
 'tea_bar':('封口調飲工作線',[3.6,1.2,1.8],'sealer, tea urns, shaker cups, sink and under-counter fridge'),
 'bakery_case':('麵包斜面展示架',[1.8,.65,1.4],'sloped open trays, tongs and separately shaped bread'),
 'cake_case':('甜點冷藏玻璃櫃',[1.8,.8,1.4],'transparent glazing, rounded cakes and refrigeration plinth'),
 'fruit_bin':('鮮果傾斜分格台',[1.8,.8,1.1],'tilted divided timber trays with individual rounded fruit'),
 'cold_wall':('冷藏玻璃門櫃',[1.8,.8,2.1],'transparent hinged doors, shelves, bottles and handles'),
 'pet_rack':('寵物用品開放貨架',[1.8,.6,1.8],'food bags, bowls, toys, leashes and open shelves'),
 'flower_bucket':('花桶階梯展示組',[1.8,.9,1.6],'cylindrical water buckets, stems and irregular flowers'),
 'flower_bench':('花藝清洗包裝檯',[2.4,1.0,1.1],'recessed sink, tap, paper rolls and cut stems'),
 'salon_station':('剪髮椅與鏡台',[1.8,2.2,1.9],'hydraulic chair, round mirror, counter and tool trolley'),
 'wash_chair':('洗髮躺椅與盆',[.9,2.0,1.1],'reclined upholstered seat, curved wash basin and tap'),
 'record_bin':('唱片翻閱櫃',[1.5,.7,1.0],'open divided bin, individually angled record sleeves'),
 'guitar':('吉他展示支架組',[1.8,.6,1.8],'three guitar bodies, necks and open stands'),
 'listen':('唱片雙座試聽檯',[1.8,1.8,1.3],'turntable disc and tonearm, headphones, two stools'),
 'bike_display':('單車展示架',[.8,2.0,1.4],'two spoked wheels, open triangular frame, handlebars and saddle'),
 'bike_service':('單車維修架與工具檯',[2.4,2.4,2.0],'clamp repair stand, raised bicycle, pegboard and workbench'),
 'drug_shelf':('藥妝開放貨架',[1.8,.6,1.6],'adjustable thin shelves, bottles and sealed packages'),
 'washer_store':('商用前開洗衣機',[1,1.2,1.5],'circular transparent door, drum recess, controls and detergent inlet'),
 'stack_dryer':('雙層商用烘衣機',[1,1.2,2.3],'two circular doors, separate drums, controls and rear exhaust sockets'),
 'fold_table':('摺衣工作桌',[1.8,.9,.9],'thin top, open legs, laundry baskets and folded textiles'),
 'grocery_shelf':('便利店食品日用品開放架',[1.8,.6,1.6],'thin shelves, snack bags, instant noodle cups and daily goods; not pharmacy bottles'),
 'stock_shelf':('理貨與補貨儲架',[1.8,.6,1.8],'open metal storage frame, labelled shipping cartons and reusable crates'),
 'packing_table':('商品分裝包裝工作桌',[1.8,.9,.9],'open-leg worktable, wrapping paper, parcel tray and small packing tools'),
 'bakery_workbench':('揉麵整形工作檯',[2.4,1.8,1.1],'stainless worktable, dough, rolling pin, flour bin and baking trays'),
 'bakery_oven':('層爐烤箱與排煙罩',[1.8,1.2,2.0],'separate deck doors, handles, dark viewing windows, top extractor and temperature controls'),
 'food_wash':('餐館備料洗滌檯',[2.4,1.0,1.1],'recessed sinks, curved taps, separated food preparation and dirty utensil trays; no flowers'),
 'reading_table':('四人閱讀桌椅組',[2.4,2.4,1.05],'four chairs, open-leg reading table, books, notebooks and reading lamps; no dining settings'),
 'broadcast_seating':('競賽轉播觀賞座席',[3.2,3.2,1.8],'wall screen, low media stand and separate lounge seats; no game controllers'),
 'lottery_desk':('選號刮票雙座桌',[1.8,1.8,1.0],'two chairs, writing surface, pens and ticket trays'),
}

# Source URLs were searched/opened before being assigned. Keep typology and
# dimensional evidence distinct; descriptions are our adaptation, not quotes.
SPECIAL_SOURCES={
 'beauty':('https://unmedesign.co/projects/sh-island/','森芯島百貨櫃位設計','少量商品、試用與開放中島'),
 'perfume':('https://www.weiwustudio.com/副本-台北-whisky-retail','台中 NoseWay 大遠百','香水陳列、柔光、開放展示中島'),
 'jewelry':('https://www.rhema-designers.com.tw/portfolio-item/jewelry-store/','珠寶店設計','玻璃展示、座席與洽談'),
 'tailor':('https://nobility.com.tw/about/','名士舘營業配置','接待、布料配件、打版、試穿與縫製分區'),
 'shoe':('https://www.cheng-qi.com/c008.html','阿瘦皮鞋設計者案例','鞋架、試鞋與座席、鞋楦造型陳列'),
 'luggage':('https://travelgomall.com/store-locations/','TravelGO 門市與維修','實際推拉試用、展示與維修收件'),
 'leather':('https://www.otm-leather.com/taipei-leather-class','東千里革製間','皮件展示、手作及教學共用空間'),
 'eyewear':('https://www.joes.com.tw/project/hb25ab-20220907/','UP眼鏡設計','商品展示、接待、洽談、驗光分區'),
 'furniture':('https://www.joysofa.com.tw/exhibition.html','喜悅沙發展示中心','沙發試坐、床墊試躺與材質樣本'),
 'kitchen':('https://www.deryi.com.tw/zh-tw/About/Store','德宜廚具展示中心','廚具設備與智慧情境操作'),
 'stationery':('https://nong-mu.com/ck3.html','SKB 老筆店設計','文具、試寫、歷史展示及文化活動'),
 'book':('https://underliight.com/2026/06/14/探索打鐵街的獨特閱讀空間-書的本事-licorice-book-service/','書的本事設計者燈光案例','實體書店商品展示與場域照明'),
 'audio':('https://om-audio.com.tw/','展樂音響設計中心','情境試聽、家庭劇院、耳機及音響展示'),
 'appliance':('https://ondesignlabltd.com/commercial/lalaport/index.html','恆隆行南港 LaLaport','顧客靠近實際操作，展演工作面配置'),
 'model':('https://www.ideals.tw/album_info?id=211','Dr. Hammer 設計者案例','商場戰棋模型展示、交流與品牌店面'),
 'toy':('https://www.wjtoy.com.tw/zh/stores','玩的夥直營門市','模型、景品、收藏玩具展示與體驗'),
 'brick':('https://www.attimolego.com/','ATTIMO 場館介紹','積木、拼豆、手作與親子互動'),
 'claw':('https://www.dreammall.com.tw/brand/p/ZKwjf55LgoOwRTn5','夢時代選物遊樂店介紹','透明夾物設備、兌換與親子活動'),
 'restaurant':('https://www.rezo.com.tw/projects_detail/110','TOK 盡興食光設計','百貨開放餐飲平面、多種座位與包廂'),
 'drug':('https://www.watsons.com.tw/store/wtctw_469_zh_TW','屈臣氏西門官方門市實景照片','藥妝開放陳列、試用、櫃台及網購取貨；不混同藥局調劑'),
 'record':('https://www.cna.com.tw/culture/article/20180112w006','先行一車現地採訪','黑膠翻閱、唱盤、座席交流；現地採訪非施工圖'),
 'phone':('https://www.apple.com/tw/retail/taipei101/','Apple 台北101官方門市','產品實際體驗、一對一服務與支援，配件另區'),
 'computer':('https://www.asus.com/tw/event/Asus_Store/about.aspx','ASUS 官方體驗店','展示、體驗、銷售、教學聚會；筆電螢幕與電競分區'),
 'lamp':('https://shop.lightplus.tw/pages/about','LIGHT PLUS 忠孝展示門市','不同燈具實品與照明情境展示'),
 'art':('https://www.styleart.com.tw/pages/about-us','時代中西畫材官方門市','多樣畫材分類、顧客材料使用諮詢與試用'),
}

CATEGORY_KEYS={
 'perfume':['perfume','skincare','gift'], 'makeup':['makeup','skincare','perfume'],
 'jewelry':['jewelry','watch','gift'], 'watch':['watch','jewelry'],
 'gift':['gift','homeware'], 'fashion':['garment','fashion_table','mannequin'],
 'tailor':['garment','fabric','mannequin'], 'shoe':['shoe','shoe_seat'],
 'bag':['bag','luggage'], 'luggage':['luggage','bag'], 'leather':['leather','bag'],
 'eyewear':['eyewear','gift'], 'furniture':['sofa_display','homeware'],
 'lamp':['lamp','homeware'], 'bed':['bed_display','homeware'],
 'kitchen':['kitchen_demo','homeware'], 'home':['homeware','gift'],
 'plant':['plant','flower_bucket'], 'book':['book','book_table'],
 'stationery':['stationery','book'], 'art':['art','stationery'],
 'phone':['phone','appliance'], 'computer':['computer_demo','phone'],
 'camera':['camera_case','phone'], 'audio':['audio','listen'],
 'appliance':['appliance','kitchen_demo'], 'console':['console_demo','model'],
 'repair':['repair_elec','appliance'], 'model':['model','boardgame'],
 'toy':['model','puzzle'], 'brick':['brick','model'], 'puzzle':['puzzle','book'],
 'card':['boardgame','model'], 'game':['console_demo','boardgame'],
 'claw':['claw'], 'craft':['craft','art'], 'food':['dining_store','bar_seat'],
 'coffee':['dining_store','coffee_bar','book'], 'tea':['tea_bar','bar_seat'],
 'bakery':['bakery_case','cake_case','dining_store'], 'fruit':['fruit_bin','cold_wall'],
 'convenience':['grocery_shelf','cold_wall','appliance'], 'pet':['pet_rack','gift'],
 'flower':['flower_bucket','flower_bench','plant'], 'salon':['salon_station','wash_chair'],
 'music':['guitar','record_bin','listen'], 'record':['record_bin','listen'],
 'bike':['bike_display','bike_service'], 'drug':['drug_shelf','makeup'],
 'laundry':['washer_store','stack_dryer','fold_table'], 'lottery':['lottery_desk'],
}

# A real programme sets the count. Secondary work/wet fixtures must not be
# multiplied merely because a large floor has spare space.
COUNTS={
 'perfume':{'perfume':10,'skincare':1,'gift':4},
 'makeup':{'makeup':6,'skincare':1,'perfume':2},
 'jewelry':{'jewelry':10,'watch':2,'gift':2},'watch':{'watch':8,'jewelry':2},
 'gift':{'gift':8,'homeware':4},'fashion':{'garment':12,'fashion_table':4,'mannequin':4},
 'tailor':{'garment':6,'fabric':2,'mannequin':2},'shoe':{'shoe':10,'shoe_seat':4},
 'bag':{'bag':10,'luggage':2},'luggage':{'luggage':10,'bag':4},
 'leather':{'leather':4,'bag':4},'eyewear':{'eyewear':6,'gift':2},
 'furniture':{'sofa_display':6,'homeware':4},'lamp':{'lamp':12,'homeware':2},
 'bed':{'bed_display':8,'homeware':4},'kitchen':{'kitchen_demo':4,'homeware':4},
 'home':{'homeware':12,'gift':4},'plant':{'plant':10,'flower_bucket':2},
 'book':{'book':24,'book_table':6},'stationery':{'stationery':10,'book':4},
 'art':{'art':10,'stationery':3},'phone':{'phone':8,'appliance':2},
 'computer':{'computer_demo':8,'phone':2},'camera':{'camera_case':8,'phone':2},
 'audio':{'audio':3,'listen':2},'appliance':{'appliance':8,'kitchen_demo':2},
 'console':{'console_demo':6,'model':2},'repair':{'repair_elec':4,'appliance':2},
 'model':{'model':12,'boardgame':2},'toy':{'model':10,'puzzle':3},
 'brick':{'brick':8,'model':4},'puzzle':{'puzzle':6,'book':4},
 'card':{'boardgame':10,'model':3},'game':{'console_demo':6,'boardgame':4},
 'claw':{'claw':12},'craft':{'craft':8,'art':3},
 'food':{'dining_store':24,'bar_seat':4},'coffee':{'dining_store':12,'coffee_bar':1,'book':3},
 'tea':{'tea_bar':1,'bar_seat':4},'bakery':{'bakery_case':6,'cake_case':2,'dining_store':4},
 'fruit':{'fruit_bin':8,'cold_wall':2},'convenience':{'grocery_shelf':12,'cold_wall':3,'appliance':1},
 'pet':{'pet_rack':12,'gift':2},'flower':{'flower_bucket':8,'flower_bench':2,'plant':6},
 'salon':{'salon_station':6,'wash_chair':3},'music':{'guitar':4,'record_bin':6,'listen':2},
 'record':{'record_bin':10,'listen':2},'bike':{'bike_display':8,'bike_service':2},
 'drug':{'drug_shelf':12,'makeup':2},'laundry':{'washer_store':4,'stack_dryer':5,'fold_table':3},
 'lottery':{'lottery_desk':6},
}
STREET_CATEGORY=dict(zip([f'S{i:02d}' for i in range(1,21)],
 ['coffee','tea','bakery','food','fruit','convenience','book','pet','flower','fashion','salon','music','game','camera','bike','drug','laundry','inn','spa','lottery']))
MALL_CATEGORIES=[
 ['perfume','makeup','makeup','makeup','jewelry','watch','flower','gift'],
 ['fashion','fashion','fashion','fashion','fashion','fashion','tailor','bag'],
 ['fashion','fashion','fashion','fashion','tailor','bag','fashion','fashion'],
 ['shoe','shoe','shoe','bag','luggage','leather','eyewear','jewelry'],
 ['furniture','lamp','bed','kitchen','home','perfume','plant','home'],
 ['book','book','book','stationery','stationery','art','coffee','card'],
 ['phone','computer','camera','audio','appliance','console','appliance','repair'],
 ['model','brick','toy','puzzle','card','game','claw','craft'],
 ['food','food','food','food','food','food','bakery','tea'],
 ['food','food','food','food','food','coffee','food','food'],
]

def category(bid,rid,name=''):
 if bid=='M1':return MALL_CATEGORIES[int(rid.split('-')[1][:2])-1][int(rid[-2:])-1]
 family=FAMILY.get(bid)
 if not family:return None
 if bid=='S48':return 'record'
 if bid=='S33':return 'card'
 return STREET_CATEGORY[family]

def reference(bid,rid,name=''):
 cat=category(bid,rid,name)
 if bid!='M1':
  key=FAMILY[bid];url,fact,adapt=CASES[key]
  if cat in ('drug','record','book'):
   url,title,adapt=SPECIAL_SOURCES[cat];fact=title+'；未提供本案公尺尺寸'
  return dict(url=url,published_fact=fact,design_adaptation=adapt,dimensions_measured_from_photo=False)
 key={'makeup':'beauty','watch':'jewelry','gift':'beauty','fashion':'S10','bag':'luggage','bed':'furniture','home':'stationery','plant':'S09','camera':'S14','console':'computer','repair':'appliance','puzzle':'brick','card':'S13','game':'S13','craft':'brick','food':'restaurant','coffee':'S01','tea':'S02','bakery':'S03','flower':'S09'}.get(cat,cat)
 if key in CASES:
  url,title,adapt=CASES[key]
 else:url,title,adapt=SPECIAL_SOURCES[key]
 return dict(url=url,published_fact=title,design_adaptation=adapt,dimensions_measured_from_photo=False)

def intersects(a,b,gap=0):
 return min(a[2],b[2])-max(a[0],b[0])>-gap+1e-7 and min(a[3],b[3])-max(a[1],b[1])>-gap+1e-7

def bounds(f):
 x,y=f['room_local_position_m'][:2];w,d=f['size_m'][:2]
 return [x-w/2,y-d/2,x+w/2,y+d/2]

def specialize(q,bid,name,catalog):
 """Preserve actual support rooms; replace the anonymous retail shelf grid.

2m circulation is reserved before placing any fixture. Additional empty floor
is customer browsing, queuing and event space; fixture count is bounded by the
store programme rather than scaled to every square metre of a large tenant.
"""
 rid=q.get('owner_unit',q['id']);cat=category(bid,rid,name)
 if cat not in CATEGORY_KEYS:return False
 # Upper homes/private lodging are not shops. Staff support rooms retain
 # their equipment and nested support-room function.
 if q.get('accommodation_kind') or q.get('functional_role')=='support':return False
 w,d,h=q['size_m'];rooms=q.get('internal_rooms',[])
 protected=[v['bounds_m'] for v in rooms]
 old=q['equipment_layout'];preserved=[v for v in old if any(bounds(v)[0]>=r[0]-1e-6 and bounds(v)[1]>=r[1]-1e-6 and bounds(v)[2]<=r[2]+1e-6 and bounds(v)[3]<=r[3]+1e-6 for r in protected)]
 # Preserve support corridors and add a cross-connected retail loop.
 original_routes=list(q.get('internal_corridors',[]));routes=list(original_routes)
 north=q.get('entrance_side')=='north'
 front_route=[-w/2,d/2-2,w/2,d/2] if north else [-w/2,-d/2,w/2,-d/2+2]
 for rect in ([-1,-d/2,1,d/2],front_route,[-w/2,-1,w/2,1]):
  if not any(intersects(rect,r) for r in protected):routes.append(dict(bounds_m=rect,clear_width_m=2,purpose='store_customer_route'))
 q['internal_corridors']=routes
 obstacles=protected+[v['bounds_m'] for v in routes]
 items=list(preserved);keys=CATEGORY_KEYS[cat]
 limits={'laundry':12,'lottery':6,'tea':6,'audio':8,'furniture':10,'bed':12,'food':28,'coffee':18,'fashion':24,'book':32}
 targets=COUNTS[cat];capacity=min(limits.get(cat,20),sum(targets.values()))
 # Scan real bays; each is separated by 2m circulation from the next bay.
 slots=[]
 for key in keys:
  dim=catalog[key][1];sw,sd,sh=dim
  if sh>h:continue
  for sign in (-1,1):
   x=sign*(1.2+sw/2)
   while abs(x)+sw/2<=w/2-.25+1e-6:
    y=-d/2+2.3+sd/2
    while y+sd/2<=d/2-.25+1e-6:
     py=-y if north else y
     a=[x-sw/2,py-sd/2,x+sw/2,py+sd/2]
     if not any(intersects(a,b,.15) for b in obstacles):slots.append((key,dim,[x,py,0],a))
     y+=sd+2
    x+=sign*(sw+2)
 # Cycle functions so a large floor cannot degenerate into 95 shelves plus
 # one checkout. Keep lower counts for specialist trial/work stations.
 by={k:[s for s in slots if s[0]==k] for k in keys};counts=Counter()
 while len(items)-len(preserved)<capacity:
  changed=False
  for key in keys:
   if counts[key]>=targets[key]:continue
   # Spread successive fixtures across the actual plate rather than exhausting
   # the left column first. All candidate sizes and 2m separations remain real.
   # Normalised farthest-point placement works for wide and deep tenants alike.
   anchors=[v['room_local_position_m'][:2] for v in items if v not in preserved]
   if anchors:
    by[key].sort(key=lambda v:min(((v[2][0]-a[0])/w)**2+((v[2][1]-a[1])/d)**2 for a in anchors),reverse=True)
   else:
    by[key].sort(key=lambda v:((v[2][0]+w/4)/w)**2+((v[2][1]-(-d/4 if not north else d/4))/d)**2)
   while by[key]:
    k,dim,pos,a=by[key].pop(0)
    if any(intersects(a,bounds(v),2) for v in items):continue
    item=dict(equipment=k,size_m=list(dim),room_local_position_m=pos,rotation_z_deg=180 if north else 0)
    items.append(item);counts[k]+=1;changed=True;break
   if len(items)-len(preserved)>=capacity:break
  if not changed:break
 if not counts:
  q['internal_corridors']=original_routes
  return False
 # One service checkout, close to the front, never inside the 2m route.
 has_checkout=False
 for sign in (1,-1):
  dim=catalog['checkout'][1];x=sign*(w/2-dim[0]/2-.3);y=-d/2+2.3+dim[1]/2
  if north:y=-y
  a=[x-dim[0]/2,y-dim[1]/2,x+dim[0]/2,y+dim[1]/2]
  if not any(intersects(a,r,.1) for r in obstacles) and not any(intersects(a,bounds(v),.2) for v in items):
   items.append(dict(equipment='checkout',size_m=list(dim),room_local_position_m=[x,y,0],rotation_z_deg=180 if north else 0));has_checkout=True;break
 # The main tenant always needs a real service counter. If display packing
 # filled both front corners, remove only redundant public display furniture;
 # a protected kitchen or the sole instance of an activity is never sacrificed.
 if not has_checkout and 'owner_unit' not in q:
  for sign in (1,-1):
   dim=catalog['checkout'][1];x=sign*(w/2-dim[0]/2-.3);y=(-1 if north else 1)*(-d/2+2.3+dim[1]/2)
   a=[x-dim[0]/2,y-dim[1]/2,x+dim[0]/2,y+dim[1]/2]
   if any(intersects(a,r,.1) for r in obstacles):continue
   blockers=[v for v in items if intersects(a,bounds(v),.2)]
   removed=Counter(v['equipment'] for v in blockers)
   if any(v in preserved for v in blockers) or any(counts[k]<=n for k,n in removed.items()):continue
   items=[v for v in items if v not in blockers];counts.subtract(removed)
   items.append(dict(equipment='checkout',size_m=list(dim),room_local_position_m=[x,y,0],rotation_z_deg=180 if north else 0));has_checkout=True;break
 q['equipment_layout']=items;q['equipment_counts']=dict(Counter(v['equipment'] for v in items))
 q['store_specific_design']=dict(category=cat,name=name,reference=reference(bid,rid,name),shape_requirements={k:EQUIPMENT[k][2] for k in counts},fixture_counts=dict(counts),open_customer_space=True,photo_dimensions_are_measurements=False,geometry_status='design_control_pending_actual_Tripo',review_status='requires_visual_review')
 q['operational_requirements']=[reference(bid,rid,name)['design_adaptation'],'顧客通路至少2m；不得把家具使用範圍計入淨寬','展示商品、設備本體、動態門扇分件；禁止以實心盒子代替成品']
 return True

CATALOG_ADDITIONS={k:(v[0],v[1]) for k,v in EQUIPMENT.items()}
