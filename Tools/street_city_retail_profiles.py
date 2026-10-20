"""Explicit semantic equipment assemblies; unknown programmes stop generation."""
from collections import Counter
import json
from pathlib import Path
from street_city_building_context import ROOT
COMMON=ROOT/'output/street-social-city-production/shop-street-components-v1'
MAPPING={
 '甜點冷藏展示櫃':'retail-case','甜點冷藏玻璃櫃':'retail-case','相機玻璃展櫃':'retail-case','模型透明展示櫃':'retail-case',
 '麵包展示架':'bread-rack','麵包斜面展示架':'bread-rack','冷藏玻璃門櫃':'fridge','原料冷藏櫃':'fridge','冷藏發酵櫃':'fridge',
 '結帳包裝檯':'steel-counter','結帳服務檯':'steel-counter','備餐洗滌檯':'double-sink','器具洗滌檯':'double-sink','生熟分流備料洗滌檯':'double-sink','花藝清洗包裝檯':'double-sink',
 '攪拌與整形檯':'steel-table','揉麵整形工作檯':'steel-table','冷卻包裝檯':'steel-table','商品分裝包裝工作桌':'steel-table','摺衣工作桌':'steel-table','服裝摺衣展示桌':'steel-table','手機試用長桌':'steel-table','電器維修工作站':'steel-table',
 '烤箱與排煙設備':'deck-oven','層爐烤箱與排煙罩':'deck-oven','出爐推車':'tray-rack',
 '鮮果傾斜分格台':'fruit-bin','便利店食品日用品開放架':'retail-rack','理貨與補貨儲架':'retail-rack','寵物用品開放貨架':'retail-rack','藥妝開放貨架':'retail-rack',
 '書籍開放書架':'bookshelf','精品禮品展示台':'display-plinth','選書低展示桌':'table','服裝吊桿架':'clothes-rack','唱片翻閱櫃':'record-bin',
 '植栽分層展示台':'retail-rack','花桶階梯展示組':'retail-rack',
 '家電操作展示檯':'steel-table','廚具情境示範檯':'steel-table','彩妝試妝鏡台':'steel-table',
 '雙人床':'bed','衣櫃':'wardrobe','沙發':'sofa',
}
TABLES={'臨窗雙人座':2,'餐飲四人桌椅組':4,'四人閱讀桌椅組':4,'親子手作桌椅組':4,'桌遊四人桌椅組':4,'吧台雙座組':2,'唱片雙座試聽檯':2,'選號刮票雙座桌':2,'書桌椅組':1,'遊戲主機試玩組':2}
COMPLEX={'服飾人台與底座':'display-mannequin','剪髮椅與鏡台':'salon-chair','洗髮躺椅與盆':'shampoo-station','吉他展示支架組':'guitar','單車展示架':'bicycle','單車維修架與工具檯':'bicycle','商用前開洗衣機':'washing-machine','雙層商用烘衣機':'dryer'}
def asset_size(aid):
 if aid=='chair':return [.48,.52,.85]
 s02=ROOT/'output/street-social-city-production/S02-modular-components-v1/assembly-building/furniture/sources.json'
 if s02.exists():
  for q in json.loads(s02.read_text('utf-8')):
   if q['id']==aid:return q['measured_size_m']
 for path in [COMMON/'board-sources.json',COMMON/'components.json']:
  if path.exists():
   data=json.loads(path.read_text('utf-8'));data=data['components'] if isinstance(data,dict) else data
   for q in data:
    if q['id']==aid:return q['size']
 defaults={'bookshelf':[1,.4,2],'table':[1.2,.8,.75],'bed':[2.2,2.1,1.1],'wardrobe':[.7,1.5,2],'sofa':[1.6,.9,1],'fridge':[.7,.77,1.8],'steel-table':[1.3,.8,.9],'steel-counter':[1.3,.8,.9],'double-sink':[1.3,.8,1.16],'pos':[.3,.25,.39],'coffee-machine':[.6,.5,.6],'hob':[.6,.6,.1]}
 return defaults[aid]
def placements(C):
 rows=[];equipment=[]
 def put(f,aid,label,x,y,z,yaw=0,scale=(1,1,1),actions=(),owner=None):
  rows.append(dict(floor=f,asset=aid,name=label,pos=[x,y,z],yaw=yaw,scale=list(scale),actions=actions,programme=owner))
 for floor in C['building']['small_shop_design']['floors']:
  no=floor['floor'];z=floor['z_m']
  for i,q in enumerate(floor['equipment']):
   name=q['name'];a,b,c,d=q['bounds_m'];w=c-a;dep=d-b;x=(a+c)/2;y=(b+d)/2;label=f'equipment-{i:02d}'
   equipment.append(dict(floor=no,index=i,name=name,bounds=q['bounds_m']))
   if name in TABLES:
    put(no,'table',label+'-table',x,y,z,scale=(.75,.875,1),actions=['read','work','eat'],owner=name)
    n=TABLES[name]
    for j,(dx,dy,angle) in enumerate([(0,.72,0),(0,-.72,180),(.78,0,-90),(-.78,0,90)][:n]):put(no,'chair',label+f'-chair-{j}',x+dx,y+dy,z,angle,actions=['sit'],owner=name)
    if name=='遊戲主機試玩組':put(no,'screen-terminal',label+'-console',x,y,z+.75,actions=['play_game'],owner=name)
    continue
   if name=='小廚房與冰箱':
    put(no,'steel-table',label+'-pantry',x-.5,y,z,scale=(1.25,1,1),actions=['prepare_food'],owner=name)
    put(no,'hob',label+'-hob',x-.5,y,z+.9,actions=['cook'],owner=name)
    put(no,'fridge',label+'-fridge',min(c-.45,x+1.4),y,z,180,actions=['store_food'],owner=name)
    continue
   if name in ('咖啡製作吧台','封口調飲工作線','熱廚爐具與排煙'):
    put(no,'steel-table',label+'-counter',x,y,z,scale=(min(w,2.4)/1.3,1,1),actions=['prepare'],owner=name)
    aid={'咖啡製作吧台':'coffee-machine','封口調飲工作線':'cup-sealer','熱廚爐具與排煙':'hob'}[name]
    put(no,aid,label+'-machine',x,y,z+.9,actions=['operate'],owner=name)
    if name=='熱廚爐具與排煙':put(no,'exhaust-hood',label+'-hood',x,y,z+2.1,owner=name)
    continue
   aid=MAPPING.get(name) or COMPLEX.get(name)
   if not aid:raise ValueError('Unsupported semantic equipment: '+name)
   size=asset_size(aid);yaw=0
   # Long shallow racks may rotate but merchandise is never stretched to a reserved working footprint.
   if aid in ('bookshelf','retail-rack','bread-rack','clothes-rack') and dep>w:yaw=90
   sx=min(1,w/(size[1] if yaw else size[0]));sy=min(1,dep/(size[0] if yaw else size[1]));scale=[sy,sx,1] if yaw else [sx,sy,1]
   if aid in ('bicycle','guitar','salon-chair','shampoo-station','display-mannequin','washing-machine','dryer'):
    scale=[1,1,1]
    if size[0]>w and size[1]<=w and size[0]<=dep:yaw=90
   if name=='衣櫃':yaw=0;scale=[1,1,1]
   put(no,aid,label,x,y,z,yaw,scale,actions=q.get('interactions',[]),owner=name)
   if name=='雙層商用烘衣機':put(no,aid,label+'-upper',x,y,z+size[2],yaw,actions=q.get('interactions',[]),owner=name)
   if '結帳' in name:put(no,'pos',label+'-pos',x,y,z+.9,owner=name)
   if aid=='deck-oven':put(no,'exhaust-hood',label+'-hood',x,y,z+2.1,owner=name)
   if aid=='guitar':put(no,'guitar-stand',label+'-stand',x,y,z,owner=name)
   if aid=='bicycle':put(no,'bike-stand',label+'-stand',x,y,z,owner=name)
   if name=='剪髮椅與鏡台':put(no,'wall-mirror',label+'-mirror',x,y+.7,z+.8,owner=name)
   if aid=='fruit-bin':
    for row in range(2):
     for col in range(6):put(no,'apple',label+f'-fruit-{row}-{col}',x-.38+col*.15,y-.13+row*.2,z+.77,actions=['buy_fruit'],owner=name)
   if name in ('花桶階梯展示組','植栽分層展示台'):
    for j in range(3):put(no,'flowers',label+f'-flowers-{j}',x-.38+j*.38,y,z+.915,actions=['choose_flowers'],owner=name)
   if name=='相機玻璃展櫃':
    for j in range(4):put(no,'camera',label+f'-camera-{j}',x-.45+j*.3,y,z+.79,actions=['inspect_camera'],owner=name)
   if name in ('服裝摺衣展示桌','服裝吊桿架'):
    for j in range(2):put(no,'folded-fabric',label+f'-garment-{j}',x-.25+j*.5,y,z+(.9 if aid=='steel-table' else .04),actions=['buy_clothes'],owner=name)
   if aid in ('bread-rack','retail-case') and ('麵包' in name or '甜點' in name):
    for j in range(4):put(no,'croissant',label+f'-pastry-{j}',x-.4+j*.25,y,z+(1.03 if aid=='retail-case' else .915),owner=name)
   if aid in ('retail-rack','bookshelf'):
    for j in range(3):put(no,'product-carton',label+f'-stock-{j}',x+(-.3+j*.3 if not yaw else 0),y+(0 if not yaw else -.3+j*.3),z+.45,owner=name)
  if no==3:
   bath=next(q for q in floor['wc_equipment'] if q['name']=='淋浴間');a,b,c,d=bath['bounds_m'];put(no,'shower','residence-shower',(a+c)/2,(b+d)/2,z,actions=['shower'])
 # A north entrance leaf occupies the originally drawn transverse route.
 # Keep a complete 2m route farther inside; move affected furniture with it.
 for floor in C['building']['small_shop_design']['floors']:
  cross=floor['internal_cross_route_bounds_m']
  if not cross:continue
  a,bb,c,dd=cross;bb-=1.3;dd-=1.3
  groups={}
  for row in [r for r in rows if r['floor']==floor['floor']]:
   if row['asset'] in ('shower','croissant','hob','pos','apple','camera','flowers','folded-fabric'):continue
   size=asset_size(row['asset']);sx,sy,_=row['scale'];dx,dy=(size[1]*sy,size[0]*sx) if row['yaw']%180 else (size[0]*sx,size[1]*sy);x,y,_=row['pos']
   if min(x+dx/2,c)-max(x-dx/2,a)>.001 and min(y+dy/2,dd)-max(y-dy/2,bb)>.001:
    prefix=row['name'].split('-table')[0].split('-counter')[0].split('-chair-')[0]
    groups[prefix]=min(groups.get(prefix,0),bb-.1-(y+dy/2))
  for row in [r for r in rows if r['floor']==floor['floor']]:
   for prefix,delta in groups.items():
    if row['name']==prefix or row['name'].startswith(prefix+'-'):row['pos'][1]+=delta
 # Keep a real shallow wardrobe at the bedroom's north/east corner, away from the bed.
 for row in rows:
  if row['asset']!='wardrobe':continue
  floor=next(f for f in C['building']['small_shop_design']['floors'] if f['floor']==row['floor'])
  if floor['private_rooms']:
   a,bb,c,d=floor['private_rooms'][0]['bounds_m'];row['pos'][:2]=[c-.45,d-1.0];row['yaw']=0
   # Short bedrooms cannot put the bed in the wardrobe's operator zone.
   # Keep the west door swing clear while reserving 0.6m in front of the cabinet.
   for bed in rows:
    if bed['floor']!=row['floor'] or bed['asset']!='bed':continue
    bw,bd,_=asset_size('bed');bw*=bed['scale'][0];bd*=bed['scale'][1]
    if bed['yaw']%180:bw,bd=bd,bw
    if min(bed['pos'][1]+bd/2,row['pos'][1]+.25)>max(bed['pos'][1]-bd/2,row['pos'][1]-.25):
     limit=row['pos'][0]-.425-.6-.1-bw/2
     if bed['pos'][0]>limit:bed['pos'][0]=limit
 # A local enclosed kitchen changes which side is usable. Tripo/board sinks
 # have the faucet/back at local +Y; face their controls into the work area.
 for row in rows:
  if row['asset'] not in ('double-sink','handwash','sink'):continue
  floor=next(f for f in C['building']['small_shop_design']['floors'] if f['floor']==row['floor']);x,y,_=row['pos']
  for room in floor['private_rooms']:
   a,bb,c,d=room['bounds_m']
   if not (a<x<c and bb<y<d):continue
   distances=[(x-a,90),(c-x,-90),(y-bb,180),(d-y,0)]
   distance,yaw=min(distances)
   if distance<=asset_size(row['asset'])[1]/2+.5:row['yaw']=yaw
 return rows,equipment
