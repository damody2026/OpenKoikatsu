"""Taiwanese attached-shop typologies, not scaled-down copies of big halls.

Metres are game design allocations. Published case areas describe the shop's
customer/work area, not our extra lift, stairs, hall, WC and upper residence.
"""
import copy,json
from collections import Counter
from street_city_store_program import FAMILY,category,reference,EQUIPMENT,CATEGORY_KEYS

SIZES={'coffee':[12,18],'tea':[10,16],'bakery':[10,18],'food':[14,24],
 'fruit':[10,18],'convenience':[12,20],'book':[12,18],'pet':[10,18],
 'flower':[12,18],'fashion':[10,18],'salon':[12,20],'music':[12,20],
 'record':[10,18],'game':[14,22],'card':[14,22],'camera':[12,20],
 'bike':[14,22],'drug':[12,20],'laundry':[12,18],'lottery':[10,16]}
SECOND={
 'coffee':('開放閱讀與共讀座席',True,['book','dining_store']),
 'tea':('茶料備製與包裝工作區',False,['tea_bar','cold_wall']),
 'bakery':('烘焙製作與冷卻包裝工坊',False,['bakery_workbench','bakery_oven','cold_wall','bakery_case']),
 'food':('備料、乾貨與員工用餐',False,['kitchen_demo','cold_wall','dining_store']),
 'fruit':('果品分裝、冷藏與配送整理',False,['fruit_bin','cold_wall','packing_table']),
 'convenience':('物流理貨與員工休息',False,['stock_shelf','cold_wall','packing_table']),
 'book':('開放選書與閱讀區',True,['book','book_table','reading_table']),
 'pet':('商品補貨與包裝整理',False,['pet_rack','fold_table']),
 'flower':('開放花藝工作坊',True,['flower_bench','flower_bucket','craft']),
 'fashion':('開放搭配展示與服裝整理',True,['garment','fashion_table','mannequin']),
 'salon':('開放美髮造型與洗髮區',True,['salon_station','wash_chair']),
 'music':('隔音樂器試奏與練習工作室',False,['guitar','listen','record_bin']),
 'record':('隔音試聽與唱片整理工作室',False,['listen','record_bin']),
 'game':('開放主機與桌遊活動區',True,['console_demo','boardgame']),
 'card':('開放桌遊與卡牌活動區',True,['boardgame','model']),
 'camera':('攝影器材整理與沖印工作室',False,['camera_case','repair_elec']),
 'bike':('單車維修與零件工作室',False,['bike_service','bike_display']),
 'drug':('藥妝補貨與員工工作區',False,['drug_shelf','packing_table']),
 'laundry':('衣物摺疊修補與設備維護',False,['fold_table','repair_elec']),
 'lottery':('競賽轉播與員工休息',False,['lottery_desk','broadcast_seating'])}
LIMITS={'coffee':10,'tea':3,'bakery':7,'food':10,'fruit':7,'convenience':10,
 'book':10,'pet':8,'flower':8,'fashion':10,'salon':7,'music':8,'record':8,
 'game':7,'card':7,'camera':7,'bike':6,'drug':10,'laundry':10,'lottery':5}

def profiles():
 result={}
 for bid in FAMILY:
  if bid in ('S01','S02','S03','S18','S19'):continue
  cat=category(bid,bid+'-0101');ref=reference(bid,bid+'-0101')
  result[bid]=dict(size=SIZES[cat][:],name=bid,colour=[.35,.42,.37],source=ref['url'],
   uses=['開放營業與顧客服務',SECOND[cat][0],'店主住宅與私人睡眠'],category=cat,open_floors=[True,SECOND[cat][1],False])
 return result

def overlap(a,b,gap=0):
 return min(a[2],b[2])-max(a[0],b[0])>-gap+1e-7 and min(a[3],b[3])-max(a[1],b[1])>-gap+1e-7

def pack(f,keys,capacity,checkout=False):
 """Reserve actual routes/room walls first; place recognisable sized fixtures.

No fixture is shrunk to fit. Rows retain 2m access between furniture envelopes.
Front cross circulation serves the 2.4m entrance and all fixture bays.
"""
 a,b,c,d=f['cells'][0]['bounds_m'];routes=[f['internal_route_bounds_m']]
 if f.get('internal_cross_route_bounds_m'):routes.append(f['internal_cross_route_bounds_m'])
 routes+=f.get('additional_routes',[])
 obstacles=routes+[[v['bounds_m'][0]-.2,v['bounds_m'][1]-.2,v['bounds_m'][2]+.2,v['bounds_m'][3]+.2] for v in f['private_rooms']]
 items=[]
 def put(key,rect,dim):
  items.append(dict(name=EQUIPMENT[key][0] if key in EQUIPMENT else '結帳服務檯',bounds_m=rect,height_m=dim[2],equipment_key=key,interactions=['使用','選購','整理'] if key!='checkout' else ['結帳','詢問','兼職服務']))
 if checkout:
  dim=[2.4,.8,1.1];rect=[max(c-2.7,f['internal_route_bounds_m'][2]+.2),b+2.2,c-.3,b+3]
  if rect[2]-rect[0]<dim[0]-1e-6:raise ValueError('Checkout cannot fit full size')
  put('checkout',rect,dim)
 queues={}
 for key in keys:
  sw,sd,sh=EQUIPMENT[key][1];candidates=[]
  # Left and right sides of the continuous entrance-to-core spine.
  for left,right in ((a+.3,f['internal_route_bounds_m'][0]-.2),(f['internal_route_bounds_m'][2]+.2,c-.3)):
   x=left+sw/2
   while x+sw/2<=right+1e-6:
    y=b+2.2+sd/2
    while y+sd/2<=d-.3+1e-6:
     rect=[x-sw/2,y-sd/2,x+sw/2,y+sd/2]
     if not any(overlap(rect,r,.05) for r in obstacles):candidates.append(rect)
     y+=sd+2
    x+=sw+2
  queues[key]=candidates
 counts=Counter()
 # Equipment with a unique work function has one station; wet/production
 # equipment is not repeated to consume every spare square metre.
 maximum={'coffee_bar':1,'tea_bar':1,'flower_bench':1,'bike_service':1,'repair_elec':1,'kitchen_demo':1,'listen':1,'cold_wall':2,'wash_chair':2}
 while len(items)<capacity:
  changed=False
  for key in keys:
   if counts[key]>=maximum.get(key,6):continue
   while queues[key]:
    rect=queues[key].pop(0)
    if any(overlap(rect,v['bounds_m'],.15) for v in items):continue
    put(key,rect,EQUIPMENT[key][1]);counts[key]+=1;changed=True;break
   if len(items)>=capacity:break
  if not changed:break
 if not counts:raise ValueError('No usable shop-specific fixtures fit')
 f['equipment']=items
 return counts

def design_store(bid,profile,base):
 cat=profile['category'];q=base('S01',profile)
 # Rename identities once before any geometry enters the common compiler.
 q=json.loads(json.dumps(q,ensure_ascii=False).replace('S01',bid));w,d=q['footprint_m']
 for f in q['floors'][:2]:
  no=f['floor'];activity=f['cells'][0];a,b,c,e=activity['bounds_m']
  f['private_rooms']=[];f['additional_routes']=[[a,b,c,b+2]]
  if no==2:
   activity['open']=SECOND[cat][1]
   if not activity['open']:
    f['internal_route_bounds_m'][3]=e
    f['internal_cross_route_bounds_m']=[a,e-2,f['internal_route_bounds_m'][2],e]
    f['doors'].append(dict(id=f'{bid}-0201-door',center_m=[-w/2+1.4,e],outward_side='north',width_m=1.2,height_m=2.4,source='room',owner=bid+'-0201',kind='hinged',opens='inward',hinge_convention='left_when_viewed_from_outside',motion_mesh_verified=False))
  if no==1 and cat=='food':
   rect=[-w/2+4.6,e-6.6,c,e];door=dict(center_m=[rect[0],rect[1]+1.3],side='west',width_m=1.2)
   f['private_rooms'].append(dict(id=bid+'-F01-kitchen',name='隔牆廚房／洗滌備料',bounds_m=rect,door=door))
   f['doors'].append(dict(id=bid+'-F01-kitchen-door',center_m=door['center_m'],outward_side='west',width_m=1.2,height_m=2.4,source='internal_partition',owner=bid+'-F01-kitchen',kind='hinged',opens='inward',hinge_convention='left_when_viewed_from_outside',motion_mesh_verified=False))
  if no==1 and cat=='fashion':
   for j in range(2):
    rect=[c-(j+1)*2,e-2.2,c-j*2-.2,e];door=dict(center_m=[(rect[0]+rect[2])/2,rect[1]],side='south',width_m=1.2)
    f['private_rooms'].append(dict(id=bid+f'-F01-fitting-{j}',name='獨立試衣間',bounds_m=rect,door=door))
    f['doors'].append(dict(id=bid+f'-F01-fitting-{j}-door',center_m=door['center_m'],outward_side='south',width_m=1.2,height_m=2.4,source='internal_partition',owner=bid+f'-F01-fitting-{j}',kind='hinged',opens='inward',hinge_convention='left_when_viewed_from_outside',motion_mesh_verified=False))
   f['additional_routes'].append([f['internal_route_bounds_m'][2],e-4.4,c,e-2.4])
  counts=pack(f,CATEGORY_KEYS[cat] if no==1 else SECOND[cat][2],LIMITS[cat],checkout=no==1)
  if no==1 and cat=='food':
   r=f['private_rooms'][0]['bounds_m'];x,y,xx,yy=r
   f['equipment'] += [dict(name='熱廚爐具與排煙',bounds_m=[xx-3.8,yy-2.6,xx-.2,yy-.2],height_m=2.2,equipment_key='kitchen_demo',interactions=['烹調','備餐']),dict(name='生熟分流備料洗滌檯',bounds_m=[xx-2.6,y+.3,xx-.2,y+1.3],height_m=1.1,equipment_key='food_wash',interactions=['清洗器具','備料'])]
   f['additional_routes'].append([x,y+1.4,xx,y+3.4])
  f['programme_category']=cat;f['store_specific_design']=dict(category=cat,reference=reference(bid,bid+'-0101'),shape_requirements={k:EQUIPMENT[k][2] for k in counts},fixture_counts=dict(counts),review_status='dimensioned_typology_pending_art_review')
 q['typology_review']=dict(status='dimensioned_compact_store',reference=reference(bid,bid+'-0101'),measured_from_photo=False,whole_plate_area_m2=w*d,customer_area_m2=(q['floors'][0]['cells'][0]['bounds_m'][2]-q['floors'][0]['cells'][0]['bounds_m'][0])*(q['floors'][0]['cells'][0]['bounds_m'][3]-q['floors'][0]['cells'][0]['bounds_m'][1]))
 return q
