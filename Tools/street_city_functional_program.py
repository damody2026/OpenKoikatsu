"""Purpose-aware physical programmes, separate from the 1,000 interaction IDs.

All dimensions are explicit game design dimensions, not manufacturer selections
or measured copies of source buildings. Support is finite per tenant/department;
additional floor area becomes actual activity or accommodation, never a modulo
cycle of unlimited stores and lounges. Internal partitions share their parent's
coordinate frame, and every internal corridor is 2m clear.
"""
import math
from collections import Counter, defaultdict

EXTRA_EQUIPMENT={
 'workstation':('雙人桌椅工作站',[1.8,1.8,1.05]),
 'student_station':('雙人課桌與兩張獨立座椅設計包絡',[1.2,1.6,1.05]),
 'lectern':('教師講桌',[1.6,.8,1.05]),
 'teaching_screen':('教學白板與投影幕',[3,.12,1.5]),
 'study_desk':('寢室個人書桌',[.8,.6,.75]),
 'bunk':('上下舖雙人床組',[1.1,2.1,1.9]),
 'king':('雙人床',[2,2.1,1.1]), 'single':('單人床',[1.1,2.1,1.1]),
 'wardrobe':('衣櫃',[1.4,.6,2.1]), 'sofa':('雙人沙發',[2,.85,.9]),
 'toilet':('坐式馬桶',[.75,.75,.85]), 'shower':('淋浴隔間',[1.5,1.5,2.3]),
 'basin':('洗手檯',[1.2,.6,.85]), 'tub':('浴缸',[1.8,1.1,.7]),
 'kitchen':('櫥櫃爐檯',[3,.75,.9]), 'fridge':('冷藏櫃',[1.2,.8,2]),
 'checkout':('結帳服務檯',[2.4,.8,1.1]), 'sink':('雙槽清洗檯',[1.8,.75,.9]),
 'cookline':('烹調排煙工作線',[3,.9,2.2]), 'oven':('烘焙烤箱',[1.2,1.1,1.9]),
 'washer':('商用洗衣機設計包絡',[1,1.2,1.5]), 'dryer':('商用烘衣機設計包絡',[1,1.2,1.8]),
 'salon':('美髮椅鏡與工作站',[1.8,2.2,1.9]), 'hairwash':('洗髮躺椅',[.8,1.8,1.1]),
 'bike':('展示單車與支架',[.7,1.9,1.4]), 'repair':('單車維修工作站',[2.4,2.4,2]),
 'piano':('直立鋼琴',[1.5,.65,1.3]), 'record':('錄音調音工作站',[2,1.6,1.2]),
 'camera':('攝影燈與腳架',[1.2,1.2,2.4]), 'backdrop':('攝影背景架',[5,.4,2.7]),
 'dispense':('調劑工作檯',[2.4,.8,.9]), 'medical':('診療檢查床',[2,.8,.8]),
 'dental':('牙科診療椅設計包絡',[2.2,2,1.5]), 'treadmill':('跑步機',[1,2.2,1.7]),
 'weights':('重量訓練機',[1.5,2,2.2]), 'play':('親子遊戲軟墊',[3,3,.3]),
 'tank':('獨立展示水槽與承托座',[4,2.5,2.5]), 'stage':('展演講台模組',[3,2,.4]),
 'parcel':('郵件分揀工作檯',[2.4,1,.9]), 'secure':('保管金庫櫃',[1.2,.8,2]),
 'maker':('通用低風險實作工作檯',[2.4,1.2,.9]), 'computer':('電腦工作站',[1.4,1.2,1.3]),
 'tatami':('和式寢具',[1.1,2.1,.2]), 'spa_basin':('私人泡湯空心池設計包絡',[3,3,.9]),
 'printer3d':('封閉式3D列印工作站設計包絡',[1.8,1.2,1.8]),
 'electronics':('電子量測示波器工作站設計包絡',[2.4,1.2,1.5]),
 'robot':('機械手臂圍護實作工作站設計包絡',[4,4,2.5]),
 'microscope':('顯微觀察工作站設計包絡',[1.8,1.2,1.5]),
}

# name, physical role, equipment keys, actions. These are bounded requirements.
SUPPORT={
 'teaching':[], # Preparation/storage are shared department facilities, not two private rooms per classroom.
 'research':[('實驗準備洗手室','support',['sink','maker'],'準備、清洗'),('研究器材室','support',['rack'],'借用、歸還')],
 'retail':[('補貨倉儲','support',['rack'],'盤點、補貨'),('員工及包裝室','support',['workstation'],'包裝、工作')],
 'fashion':[('獨立試衣及搭配室','activity',['wardrobe','sofa'],'試穿、搭配'),('服裝後倉','support',['rack'],'盤點、補貨')],
 'food':[('備料烹調及乾貨餐具間','support',['cookline','prep','fridge','rack'],'備料、烹調'),('回盤清洗室','support',['sink'],'回盤、清洗')],
 'bakery':[('烘焙製作間','support',['oven','prep'],'烘焙、製作'),('洗滌冷藏間','support',['sink','fridge'],'清洗、冷藏')],
 'fruit':[('果品冷藏整理室','support',['fridge','sink'],'整理、清洗'),('包裝補貨室','support',['prep'],'包裝、補貨')],
 'flower':[('花材清洗與保鮮室','support',['sink','fridge'],'清洗、保鮮'),('花藝包裝工作間','activity',['maker'],'插花、包裝')],
 'pet':[('寵物用品後倉','support',['rack'],'補貨、盤點'),('用品清潔室','support',['sink'],'清潔、整理')],
 'salon':[('洗髮濕區','activity',['hairwash','sink'],'洗髮、沖洗'),('工具清潔消毒室','support',['sink','rack'],'消毒、整理')],
 'music':[('隔音試奏室','activity',['piano'],'試奏、練習'),('樂器維護室','support',['maker'],'維護、調整')],
 'photo':[('更衣化妝室','activity',['wardrobe','basin'],'更衣、化妝'),('器材準備室','support',['rack'],'取用器材、佈光')],
 'bike':[('維修工坊','activity',['repair'],'維修、保養'),('零件庫','support',['rack'],'領用、補貨')],
 'pharmacy':[('獨立調劑室','support',['dispense','fridge','sink'],'調劑、洗手'),('私密藥事諮詢室','activity',['workstation'],'諮詢、衛教'),('藥品保管室','support',['rack'],'保管、盤點')],
 'laundry':[('設備維護與排風室','support',['rack'],'維護、檢修'),('摺衣修補室','activity',['prep'],'摺衣、修補')],
 'clinic':[('消毒清潔準備室','support',['sink','rack'],'清洗、消毒'),('醫療用品室','support',['rack'],'領用、整理')],
 'spa':[('更衣室','activity',['locker'],'更衣、寄物'),('淋浴及廁所','activity',['shower','toilet','basin'],'淋浴、如廁'),('水處理設備室','support',['rack'],'水處理、維護')],
 'hotel':[], # Linen and guest service are shared floor facilities.
 'dorm':[], # Shared laundry belongs to the floor, never one per bedroom.
 'home':[],
 'library':[('館藏整理室','support',['rack'],'整理、歸還')],
 'admin':[('檔案保管室','support',['shelf'],'歸檔、查詢')],
 'game':[('遊戲器材借還室','support',['rack'],'借用、歸還')],
 'club':[('活動器材室','support',['rack'],'借用、歸還')],
 'transit':[('站務及監控室','support',['computer'],'站務、調度'),('行李寄放室','activity',['locker'],'寄放、領取')],
 'post':[('郵件分揀後場','support',['parcel'],'分揀、配送'),('郵件暫存室','support',['rack'],'保管、領件')],
 'bank':[('保管金庫室','support',['secure'],'保管、存取'),('獨立洽談室','activity',['workstation'],'諮詢、辦理')],
 'lottery':[('票券保管室','support',['secure'],'保管、盤點')],
 'exhibit':[('展品準備室','support',['maker'],'佈展、準備')],
 'service':[('工作準備室','support',['workstation'],'工作、整理')],
 'reception':[('接待員工作準備室','support',['workstation'],'工作、整理'),('行李備品保管室','support',['locker'],'寄放、領取')],
 'sport':[('運動更衣淋浴室','activity',['locker','shower','toilet'],'更衣、淋浴'),('器材維護室','support',['rack'],'維護、歸還')],
}

ACTIVITY={
 'teaching':('教學分組實作教室','student_station','上課、分組討論、共同學習'),
 'research':('研究實作工作室','maker','實作、測試、研究'),
 'retail':('商品展售廳','shelf','購物、瀏覽、交流'),
 'fashion':('服裝展售廳','shelf','選購、搭配'),
 'food':('用餐廳','dining','用餐、聊天'), 'bakery':('烘焙展售與座席','dining','選購、用餐'),
 'fruit':('果品展售廳','shelf','挑選、秤重、購物'), 'flower':('花材展售與工作坊','maker','選花、插花'),
 'pet':('人寵用品體驗廳','shelf','選購、用品試用'), 'salon':('美髮服務廳','salon','剪髮、造型'),
 'music':('隔音練習室','piano','練習、合奏'), 'photo':('攝影棚','camera','拍攝、佈光'),
 'bike':('單車展售量測廳','bike','選購、量測、試騎準備'),
 'pharmacy':('健康用品展售廳','shelf','購物、衛教'), 'laundry':('洗烘衣服務廳','washer','洗衣、烘衣、等候'),
 'clinic':('獨立診療室','medical','問診、檢查、治療'),
 'spa':('私人湯屋','spa_basin','泡湯、放鬆'),
 'hotel':('獨立旅宿套房','king','入住、睡覺、洗澡、休息'),
 'dorm':('獨立寢室單元','single','入住、睡覺、讀書、洗澡'),
 'home':('獨立住宅單元','king','居住、睡覺、做飯、洗澡'),
 'library':('開架閱覽室','shelf','找書、閱讀、學習'), 'admin':('辦公與服務室','workstation','辦理、工作、諮詢'),
 'game':('遊戲體驗廳','console','遊戲、競賽'), 'club':('社團活動室','workstation','活動、共同製作'),
 'transit':('旅客候車廳','seat','候車、休息、查詢'),
 'post':('郵政客用服務廳','checkout','寄件、領件'), 'bank':('金融服務廳','checkout','辦理、諮詢'),
 'lottery':('選號刮票座席廳','dining','選號、刮票、觀賽'),
 'exhibit':('展覽展示廳','shelf','參觀、交流'), 'service':('客用服務廳','console','諮詢、使用服務'),
 'reception':('接待交誼區','sofa','等候、交誼、休息'),
 'sport':('健身練習廳','treadmill','運動、健身'),
}

def purpose(bid,r):
    bid=r.get('programme_template_building',bid)
    t=r['type'];f=r.get('floor',1);name=r.get('name','')
    if t=='apartment':return 'home'
    if t in ('deluxe','executive','suite','inn'):return 'hotel'
    if t=='dorm':return 'dorm'
    if t=='nap':return 'hotel' if bid in ('S18','H1') else 'dorm'
    if t=='service' and any(s in name for s in ('接待','交誼','禮賓','大廳')):return 'reception'
    if bid=='S19':return 'spa'
    if bid=='S18' and f>1:return 'hotel'
    if bid.startswith('S') and bid[1:].isdigit() and bid!='SC':
        specific={'S03':'bakery','S05':'fruit','S08':'pet','S09':'flower','S11':'salon','S12':'music','S14':'photo','S15':'bike','S16':'pharmacy','S17':'laundry','S20':'lottery'}
        if bid in specific:return specific[bid]
    if bid=='CV' and f==2:
        if '郵局' in name:return 'post'
        if '銀行' in name:return 'bank'
    if bid=='CV' and f==1 and t=='retail':return 'pharmacy'
    if bid=='TR':return 'transit'
    return {'class':'teaching','office':'admin','lab':'research','food':'food','cafe':'food','study':'library','health':'clinic',
            'retail':'retail','fashion':'fashion','game':'game','admin':'admin','club':'club','sport':'sport','spa':'spa','exhibit':'exhibit'}.get(t,'service')

def frontage_dimensions(bid,r,maxw,maxd):
    w,d=r['size_m'][:2]
    # Eight actual large tenants use the majority of the mall floor, with a
    # bounded rear service band. Other public shops expand into their frontage.
    if bid=='M1':return maxw,max(d,maxd*.72)
    if bid.startswith('S') and bid[1:].isdigit() and purpose(bid,r) not in ('home','hotel','dorm'):
        return maxw,max(d,maxd*.62)
    if purpose(bid,r) in ('dorm','home','hotel'):return min(maxw,w),min(maxd,d)
    return w,d

def equipment(key,size,position,rotation=0):
    return dict(equipment=key,size_m=list(size),room_local_position_m=[round(v,5) for v in position],rotation_z_deg=rotation)

from street_city_dimensions import CORRIDOR_M

def grid(q,key,catalog,station_size=None):
    """Two banks, 2m centre aisle, 2m entrance band, 1m working gaps."""
    w,d,_=q['size_m'];yaw={'south':0,'north':180,'east':90,'west':270}[q['entrance_side']]
    if yaw%180:w,d=d,w
    sw,sd,sh=station_size or catalog[key][1];items=[]
    for sign in (-1,1):
        x=sign*(1.2+sw/2)
        while abs(x)+sw/2<=w/2-.2+1e-6:
            y=d/2-.2-sd/2
            while y-sd/2>=-d/2+2.2-1e-6:
                if abs(y)>=1.2+sd/2:
                    px,py=rotate(x,y,yaw);dim=[sd,sw,sh] if yaw%180 else [sw,sd,sh]
                    items.append(equipment(key,dim,[px,py,0],yaw))
                y-=sd+1+(2.4 if len(items)%4==3 else 0)
            x+=sign*(sw+1)
    if not items:
        # Small support rooms retain a clear entrance strip, without declaring
        # their entire working area a corridor.
        if sw<=w-.4 and sd<=d-2.4:
            x=-1.2-sw/2 if w>=2.4+2*sw+.4 else 0
            px,py=rotate(x,d/2-.2-sd/2,yaw)
            items=[equipment(key,[sd,sw,sh] if yaw%180 else [sw,sd,sh],[px,py,0],yaw)]
    return items

def rotate(x,y,yaw):
    return [(x,y),(-y,x),(-x,-y),(y,-x)][int(yaw//90)%4]

def dorm_studio(q,catalog,hotel=False):
    """Four-person studio: two bunks, four desk/chair pairs and a wet room.

    Internal 2m clear passage joins a front cross passage and the wet-cell
    door. No large lounge or apartment is substituted for a dorm room.
    """
    w,d,h=q['size_m'];yaw={'south':0,'north':180,'east':90,'west':270}[q['entrance_side']]
    if yaw%180:w,d=d,w
    assert w>=(7.2 if hotel else 8)-1e-5 and d>=8.8-1e-5,(q['id'],w,d,'sleeping studio cannot fit beds, seated study and bath')
    def rect(a,b,c,e):
        ps=[rotate(x,y,yaw) for x in (a,c) for y in (b,e)]
        return [min(p[0] for p in ps),min(p[1] for p in ps),max(p[0] for p in ps),max(p[1] for p in ps)]
    bath_start=d/2-3.2;fixtures=[]
    def add(k,x,y,local_yaw=0):
        sw,sd,sh=catalog[k][1];px,py=rotate(x,y,yaw)
        angle=(yaw+local_yaw)%360
        fixtures.append(equipment(k,[sd,sw,sh] if angle%180 else [sw,sd,sh],[px,py,0],angle))
        return len(fixtures)-1
    study_pairs=[]
    for sign in (-1,1):
        if hotel:
            x=sign*(1.2+1)
            add('king' if sign<0 else 'sofa',x,-d/2+(4.25 if sign<0 else 3.7))
            if sign>0:add('wardrobe',x,-d/2+5.1)
        else:
            # Across each side bank: rotated bunk toward the entrance, two
            # seated desks behind it. This fits the narrow 8x9m type while
            # preserving the central 2m passage and the front 2m cross aisle.
            bank_center=sign*(w/4+.5)
            add('bunk',bank_center,-d/2+3.55,90)
            for offset in (-.38,.38):
                add('campus_wardrobe',bank_center+offset,-d/2+2.4)
            for offset in (-.5,.5):
                x=bank_center+offset
                desk_index=add('study_desk',x,d/2-3.75)
                chair_index=add('seat',x,d/2-4.4,180)
                study_pairs.append(dict(desk_equipment_index=desk_index,chair_equipment_index=chair_index,
                    chair_faces_desk=True,required_chair_size_m=list(catalog['seat'][1])))
    add('toilet',-w/2+.7,bath_start+.7)
    add('shower',w/2-1,bath_start+1)
    add('basin',0,d/2-.55)
    door=rotate(0,bath_start,yaw);b=rect(-w/2+.2,bath_start,w/2-.2,d/2-.2)
    q.update(internal_rooms=[dict(id=q['id']+'-bath',kind='bathroom',name='私人淋浴廁所',bounds_m=b,
        clear_size_m=[b[2]-b[0],b[3]-b[1],h],door=dict(position_m=list(door)+[0],width_m=1.2,height_m=2.2,opens_to='internal_2m_corridor'),wall_thickness_m=.2)],
        internal_corridors=[dict(bounds_m=rect(-1.0,-d/2,1.0,bath_start-.2),clear_width_m=CORRIDOR_M),dict(bounds_m=rect(-w/2,-d/2,w/2,-d/2+2),clear_width_m=CORRIDOR_M)],
        equipment_layout=fixtures,study_pairs=study_pairs,accommodation_kind='hotel' if hotel else 'dorm',capacity_beds=2 if hotel else 4,private_bathrooms=1,private_kitchens=0)

def accommodation(q,kind,catalog):
    """Actual sleeping rooms and wet cells, not a bed symbol in an empty hall.

    The 2m corridor and entrance cross aisle are in the parent unit; each
    bedroom has a 1.2m door and real partition opening onto that corridor.
    """
    w,d,h=q['size_m'];yaw={'south':0,'north':180,'east':90,'west':270}[q['entrance_side']]
    if yaw%180:w,d=d,w
    if kind=='dorm' or (kind=='hotel' and (w<16 or d<14)):
        return dorm_studio(q,catalog,hotel=kind=='hotel')
    assert w>=(9.5 if kind=='home' else 8) and d>=8.5,(q['id'],kind,w,d,'accommodation unit too small')
    start=-d/2+2.2;bank=w/2-1.2;depth=d/2-.2-start
    spaces=[];fixtures=[];corridors=[]
    def rect(a,b,c,e):
        points=[rotate(x,y,yaw) for x in (a,c) for y in (b,e)]
        return [min(p[0] for p in points),min(p[1] for p in points),max(p[0] for p in points),max(p[1] for p in points)]
    corridors=[dict(bounds_m=rect(-1.0,-d/2,1.0,d/2),clear_width_m=CORRIDOR_M),dict(bounds_m=rect(-w/2,-d/2,w/2,-d/2+2),clear_width_m=CORRIDOR_M)]
    def add(key,x,y):
        sx,sy,sz=catalog[key][1];px,py=rotate(x,y,yaw)
        fixtures.append(equipment(key,[sy,sx,sz] if yaw%180 else [sx,sy,sz],[px,py,0],yaw))
    # Each side has two rooms, or three on the kitchen/bathroom side of homes.
    for side in (-1,1):
        kinds=(['living','bedroom'] if side<0 else ['kitchen','bathroom']) if kind=='home' else (['bedroom','bedroom'] if side<0 else ['bathroom','study']) if kind=='dorm' else (['living','bedroom'] if side<0 else ['bathroom','bedroom'])
        for j,use in enumerate(kinds):
            a,c=(-w/2+.2,-1.2) if side<0 else (1.2,w/2-.2)
            b=start+depth*j/len(kinds)+.1;e=start+depth*(j+1)/len(kinds)-.1
            cx,cy=(a+c)/2,(b+e)/2;rw,rd=c-a,e-b
            bounds=rect(a,b,c,e);door=rotate(c if side<0 else a,cy,yaw)
            spaces.append(dict(id=q['id']+f'-cell{len(spaces)+1}',kind=use,name={'bedroom':'獨立臥室','bathroom':'私人衛浴','kitchen':'廚房與餐區','living':'起居會客室','study':'寢室共用書房'}[use],
                bounds_m=bounds,clear_size_m=[bounds[2]-bounds[0],bounds[3]-bounds[1],h],
                door=dict(position_m=list(door)+[0],width_m=1.2,height_m=2.2,opens_to='internal_2m_corridor'),wall_thickness_m=.2))
            if use=='bedroom':
                if kind=='dorm':
                    for x in (a+.75,c-.75):add('single',x,b+1.15)
                    add('wardrobe',a+.8,e-.4)
                    # Narrow dormitory banks put the desk at the foot of one
                    # bed, rather than intersecting a wardrobe on the rear wall.
                    if rw>=3.5:add('desk',c-1,e-.5)
                    else:add('desk',cx,b+2.8)
                else:
                    add('king',cx,b+1.25);add('wardrobe',a+.8,e-.4)
                    if rd>5:add('sofa',cx,e-1.5)
            elif use=='bathroom':
                add('toilet',a+.7,b+.7);add('shower',c-.85,b+.85);add('basin',cx,e-.4)
                if kind=='hotel' and rd>4.2:add('tub',cx,cy+.4)
            elif use=='kitchen':
                add('kitchen',cx,e-.5);add('fridge',a+.7,b+.5)
            elif use=='living':
                add('sofa',cx,e-.7);add('desk',cx,b+.7)
            else:add('desk',cx,e-.6);add('wardrobe',cx,b+.5)
    q.update(internal_rooms=spaces,internal_corridors=corridors,equipment_layout=fixtures,
        accommodation_kind=kind,capacity_beds=sum(f['equipment'] in ('single','king','tatami') for f in fixtures),
        private_bathrooms=sum(s['kind']=='bathroom' for s in spaces),private_kitchens=sum(s['kind']=='kitchen' for s in spaces))

def furnish(q,program,catalog,keys=None):
    q['programme_kind']=program
    q['equipment_layout']=[]
    if program=='teaching' and not keys and not q.get('activity_keys'):
        teaching(q,catalog)
    elif program in ('hotel','home','dorm') and not keys:
        accommodation(q,program,catalog)
    elif program=='reception' and not keys:
        # Waiting and conversation use small seating groups with spare public
        # floor area, rather than filling the lobby with computer terminals.
        bays=grid(q,'sofa',catalog,station_size=[2.4,1.8,1.1])
        capacity=min(len(bays),max(1,min(12,int(q['size_m'][0]*q['size_m'][1]/30))))
        chosen=[bays[round(i*(len(bays)-1)/max(1,capacity-1))] for i in range(capacity)]
        for i,f in enumerate(chosen):
            key='checkout' if i==0 and len(chosen)>1 and not q.get('owner_unit') else 'sofa'
            dim=catalog[key][1];f['equipment']=key;f['size_m']=[dim[1],dim[0],dim[2]] if f['rotation_z_deg']%180 else list(dim)
        q['equipment_layout']=chosen
        q['operational_requirements']=['接待櫃檯與候位交誼座席分區','中央與入口2m通路暢通','行李及員工準備另設封閉後場','保留開放集合與排隊面積，不以密集設備填滿']
    else:
        defaults={'teaching':['workstation'],'research':['maker','computer','sink'],'retail':['shelf','checkout'],
          'fashion':['shelf','checkout','wardrobe'],'food':['dining','checkout'],'bakery':['dining','checkout'],
          'fruit':['shelf','checkout'],'flower':['maker','checkout'],'pet':['shelf','checkout'],
          'salon':['salon','basin'],'music':['piano','shelf'],'photo':['camera','backdrop'],
          'bike':['bike','checkout'],'pharmacy':['shelf','checkout'],'laundry':['washer','dryer','prep'],
          'clinic':['medical','workstation','basin'],'spa':['spa_basin','lounge'],
          'library':['shelf','workstation','console'],'admin':['workstation','console'],
          'game':['console','dining'],'club':['workstation'],'transit':['seat','console'],
          'post':['checkout','seat','console'],'bank':['checkout','seat','console'],
          'lottery':['dining','checkout','console'],'exhibit':['shelf','seat'],'service':['console','seat'],'sport':['treadmill','weights']}
        default_mode=keys is None
        keys=keys or q.get('activity_keys') or defaults.get(program,[ACTIVITY[program][1]])
        cw,cd=q['size_m'][:2]
        if q['entrance_side'] in ('east','west'):cw,cd=cd,cw
        keys=[k for k in keys if catalog[k][1][0]<=cw-.4+1e-6 and catalog[k][1][1]<=cd-2.4+1e-6] or ['seat']
        key=keys[0]
        station=[max(catalog[k][1][i] for k in keys) for i in range(3)] if keys else None
        q['equipment_layout']=grid(q,key,catalog,station)
        # Multiple functional devices have separate real positions, rather than
        # overlapping an entire repeated grid for every equipment type.
        if keys and len(keys)>1:
            items=q['equipment_layout']
            for i,f in enumerate(items):
                replacement=keys[0] if default_mode and i<len(items)-len(keys)+1 else keys[(i-len(items)+len(keys))%len(keys)]
                dim=catalog[replacement][1]
                f['equipment']=replacement;f['size_m']=[dim[1],dim[0],dim[2]] if f['rotation_z_deg']%180 else list(dim)
        q['operational_requirements']=operational(program)
    q['equipment_counts']=dict(Counter(f['equipment'] for f in q['equipment_layout']))
    q['furnishing_status']='purpose_specific_dimensioned_layout; actual_mesh_pending'

def teaching(q,catalog):
    """Dimensioned seating, front teaching zone and a continuous 2m centre aisle.

    No imaginary classroom capacity inferred merely from a teacher's desk.
    Narrow annexes use one bank; every station includes two seat envelopes.
    """
    w,d,h=q['size_m'];yaw={'south':0,'north':180,'east':90,'west':270}[q['entrance_side']]
    if yaw%180:w,d=d,w
    sw,sd,sh=catalog['student_station'][1];items=[]
    aisle=[-1.0,-d/2,1.0,d/2]
    # Front cross corridor connects the actual off-centre room entrance.
    routes=[aisle,[-w/2,-d/2,w/2,-d/2+CORRIDOR_M]]
    for sign in (-1,1):
        x=sign*(1.2+sw/2)
        while abs(x)+sw/2<=w/2-.2+1e-6:
            y=-d/2+2.2+sd/2
            while y+sd/2<=d/2-1.5+1e-6:
                px,py=rotate(x,y,yaw)
                items.append(equipment('student_station',[sd,sw,sh] if yaw%180 else [sw,sd,sh],[px,py,0],yaw))
                y+=sd+.6
            x+=sign*(sw+.8)
    if not items:
        # Narrow seminar annex: one 2m aisle beside one seating bank.
        if w>=4.6:
            routes[0]=[-w/2,-d/2,-w/2+2,d/2]
            x=-w/2+3.2+sw/2;y=-d/2+2.2+sd/2
            while y+sd/2<=d/2-.2+1e-6:
                px,py=rotate(x,y,yaw)
                items.append(equipment('student_station',[sd,sw,sh] if yaw%180 else [sw,sd,sh],[px,py,0],yaw));y+=sd+.6
    sign=-1 if w/2-1.2>=1.6 else 1
    if w>=6.8 and d>=6.8:
        px,py=rotate(sign*(w/2-1),d/2-.6,yaw)
        items.append(equipment('lectern',[.8,1.6,1.05] if yaw%180 else [1.6,.8,1.05],[px,py,0],yaw))
    q['equipment_layout']=items
    q['capacity_students']=2*sum(f['equipment']=='student_station' for f in items)
    q['capacity_policy']='two seats per station; 2m centre and entrance corridors; no private preparation/storage room inside each classroom'
    q['internal_corridors']=[]
    for a,b,c,e in routes:
        pts=[rotate(x,y,yaw) for x in (a,c) for y in (b,e)]
        q['internal_corridors'].append(dict(bounds_m=[min(x for x,y in pts),min(y for x,y in pts),max(x for x,y in pts),max(y for x,y in pts)],clear_width_m=CORRIDOR_M))
    q['operational_requirements']=['教學座位依實際桌椅包絡計數','講桌、學生座席與2m走廊分開','備課與器材借還使用系辦及樓層共用設施']

def operational(program):
    return {
      'food':['收貨→冷藏乾貨→備料烹調→出餐','回盤→清洗→潔淨餐具；與備料分隔','排煙、補風、隔油、地排接維修管線'],
      'bakery':['原料冷藏→備料→烘烤→展示','獨立洗手及回盤洗滌、排熱排氣'],
      'spa':['更衣→淋浴→湯池→淋浴→乾休息','濕區有完整隔牆、防水、地排及獨立水處理'],
      'clinic':['接待候診→私密診療→離場','清潔消毒及醫療用品分隔；不自行增加手術用途'],
      'research':['低風險桌上型實作；設備與洗手準備分隔','大型或危險設備必須另選型，禁止把通用工作桌當專業設備'],
      'laundry':['付款→洗衣→烘衣→摺衣','機門使用面及後側排風維修預留'],
    }.get(program,['客用入口與2m通道保持暢通','固定家具、活動家具、服務設備分件建模'])

def embed_support(q,requirement,catalog,side=-1):
    """Bounded support room alongside an existing 2m centre route.

    Used when assigning a whole large annex would exceed the support quota.
    It replaces furniture in its own footprint; it never sits on top of desks.
    """
    name,role,keys,actions=requirement;w,d,h=q['size_m']
    yaw={'south':0,'north':180,'east':90,'west':270}[q['entrance_side']]
    if yaw%180:w,d=d,w
    is_kitchen='cookline' in keys
    rw=min(w/2-1.9,6 if is_kitchen else 4.4);rd=min(d-2.6,8 if is_kitchen else 4.4)
    if rw<3.5 or rd<3.5:return False
    a,c=(-1.2-rw,-1.2) if side<0 else (1.2,1.2+rw)
    used_slots=sum(x.get('support_side')==side for x in q.get('internal_rooms',[]));e=d/2-.2-used_slots*(rd+.2);b=e-rd;cy=(b+e)/2
    if b<-d/2+2.2:return False
    pts=[rotate(x,y,yaw) for x in (a,c) for y in (b,e)]
    bounds=[min(p[0] for p in pts),min(p[1] for p in pts),max(p[0] for p in pts),max(p[1] for p in pts)]
    def intersects(f):
        x,y,_=f['room_local_position_m'];fw,fd,_=f['size_m']
        return min(x+fw/2,bounds[2]+.25)>max(x-fw/2,bounds[0]-.25) and min(y+fd/2,bounds[3]+.25)>max(y-fd/2,bounds[1]-.25)
    q['equipment_layout']=[f for f in q['equipment_layout'] if not intersects(f)]
    door=list(rotate(c if side<0 else a,cy,yaw))+[0]
    sub=dict(id=q['id']+f'-support{len(q.get("internal_rooms",[]))+1}',kind='support',name=name,bounds_m=bounds,
        clear_size_m=[bounds[2]-bounds[0],bounds[3]-bounds[1],h],door=dict(position_m=door,width_m=1.2,height_m=2.2,opens_to='internal_2m_corridor'),
        wall_thickness_m=.2,support_side=side,interactions=actions.split('、'),required_equipment=keys)
    q.setdefault('internal_rooms',[]).append(sub)
    # Furniture bays lie at the rear and sides of the service room. Reserve
    # 1.2m between working faces; door opens inward at the side centre.
    # Place each separate device along two walls with explicit free working
    # space between them. Kitchen bays are 6m x 8m when the parent allows it.
    placements=[]
    for i,key in enumerate(keys):
        sw,sd,sh=catalog[key][1]
        x=(a+c)/2 if i<2 else a+.2+sw/2
        y=e-.2-sd/2 if i==0 else b+.2+sd/2 if i==1 else (b+e)/2+((1.0 if i==2 else -1.0) if len(keys)>3 else 0)
        if i>=2:x=c-.2-sw/2
        px,py=rotate(x,y,yaw)
        q['equipment_layout'].append(equipment(key,[sd,sw,sh] if yaw%180 else [sw,sd,sh],[px,py,0],yaw))
    q['equipment_counts']=dict(Counter(f['equipment'] for f in q['equipment_layout']))
    q['embedded_support_area_m2']=q.get('embedded_support_area_m2',0)+rw*rd
    if not q.get('internal_corridors'):
        pts=[rotate(x,y,yaw) for x in (-1.0,1.0) for y in (-d/2,d/2)]
        q['internal_corridors']=[dict(bounds_m=[min(p[0] for p in pts),min(p[1] for p in pts),max(p[0] for p in pts),max(p[1] for p in pts)],clear_width_m=CORRIDOR_M)]
    return True

def ensure_activity(q,catalog):
    """A support insertion cannot remove every actual activity station."""
    if q.get('accommodation_kind'):return
    program=q['programme_kind'];key=(q.get('activity_keys') or [ACTIVITY[program][1]])[0]
    def is_activity(f):
        x,y=f['room_local_position_m'][:2]
        return f['equipment']==key and not any(a<=x<=c and b<=y<=d for a,b,c,d in (r['bounds_m'] for r in q.get('internal_rooms',[])))
    if any(is_activity(f) for f in q['equipment_layout']):return
    w,d,h=q['size_m'];yaw={'south':0,'north':180,'east':90,'west':270}[q['entrance_side']]
    if yaw%180:w,d=d,w
    sw,sd,sh=catalog[key][1]
    if sh>h:return
    def box(f):
        x,y=f['room_local_position_m'][:2];fw,fd=f['size_m'][:2]
        return [x-fw/2,y-fd/2,x+fw/2,y+fd/2]
    def hit(a,b,margin=0):return min(a[2],b[2]+margin)>max(a[0],b[0]-margin)+1e-6 and min(a[3],b[3]+margin)>max(a[1],b[1]-margin)+1e-6
    obstacles=[r['bounds_m'] for r in q.get('internal_rooms',[])]+[r['bounds_m'] for r in q.get('internal_corridors',[])]
    added=0
    for sign in (-1,1):
        x=sign*(1.2+sw/2)
        while abs(x)+sw/2<=w/2-.2+1e-6:
            y=-d/2+2.2+sd/2
            while y+sd/2<=d/2-.2+1e-6:
                px,py=rotate(x,y,yaw);f=equipment(key,[sd,sw,sh] if yaw%180 else [sw,sd,sh],[px,py,0],yaw);a=box(f)
                if not any(hit(a,b,.2) for b in obstacles) and not any(hit(a,box(g),.6) for g in q['equipment_layout']):
                    q['equipment_layout'].append(f);added+=1
                y+=sd+1
            x+=sign*(sw+1)
    q['equipment_counts']=dict(Counter(f['equipment'] for f in q['equipment_layout']))

def finish_partition(bid,rooms,annex):
    # Import only at call time to avoid the catalogue/solver import cycle.
    from street_city_floor_solver import CATALOG
    by_owner=defaultdict(list)
    owners={r['id']:r for r in rooms}
    from street_city_campus_program import CAMPUS,keys_for,specialize as campus_specialize
    for q in annex:by_owner[q['owner_unit']].append(q)
    for rid in owners:by_owner.setdefault(rid,[])
    for rid,group in by_owner.items():
        r=owners[rid];program=purpose(bid,r);requirements=list(SUPPORT[program])
        if bid=='IN' and r.get('floor')==4:
            requirements=[('乾式運算設備準備室','support',['computer'],'維護、計算'),('研究器材室','support',['rack'],'借用、歸還')]
        def activity_keys():
            name=r.get('name','')
            if bid in CAMPUS and r['type']!='class':
                specific=keys_for(bid,r)
                if specific:return specific
            if bid=='CV' and r.get('floor')==3 and r['type']=='exhibit':return ['tank','console','seat']
            if any(k in name for k in ('舞蹈','瑜伽','柔道','武術')):return ['mat']
            if any(k in name for k in ('劇場','表演','放映','電影','視聽')):return ['seat','stage','console']
            if any(k in name for k in ('音樂','吉他','鋼琴')):return ['piano','seat']
            if '錄音' in name:return ['record','seat']
            if any(k in name for k in ('電競','人工智慧','計算')):return ['computer']
            if any(k in name for k in ('木作','手作','陶藝','花藝')):return ['maker','sink']
            if '親子' in name:return ['play','seat']
            if program=='research':
                if '創客' in name or '原型' in name:return ['printer3d','maker','computer']
                if '機器人' in name or '智慧製造' in name:return ['robot','electronics','maker']
                if '生技' in name:return ['microscope','maker','sink']
                if bid=='EE':return ['electronics','computer','maker']
                return ['maker','computer','sink']
            if '牙醫' in name:return ['dental','basin','workstation']
            if bid=='S16' and r.get('floor')==2:return ['workstation','basin']
            if bid=='S17' and r.get('floor')==2:return ['maker','prep']
            return None
        keys_override=activity_keys()
        total=sum(q['size_m'][0]*q['size_m'][1] for q in group)+r['size_m'][0]*r['size_m'][1]
        used=0
        # Assign required support to the smallest actual rooms first. Storage
        # and staff preparation may occupy at most 22% of a department/tenant.
        for q in sorted(group,key=lambda q:q['size_m'][0]*q['size_m'][1]):
            selected=None
            if requirements:
                candidate=requirements[0];area=q['size_m'][0]*q['size_m'][1]
                qw,qd=q['size_m'][:2]
                if q['entrance_side'] in ('east','west'):qw,qd=qd,qw
                fits=all(CATALOG[k][1][0]<=qw-.4 and CATALOG[k][1][1]<=qd-2.4 for k in candidate[2])
                if fits and (candidate[1]!='support' or used+area<=total*.22):
                    selected=requirements.pop(0)
                    if selected[1]=='support':used+=area
            if selected:
                name,role,keys,actions=selected;q.update(name=r['name']+'／'+name,functional_role=role,interactions=actions.split('、'))
                # Use the largest station to create nonoverlapping equipment bays.
                keys=sorted(keys,key=lambda k:CATALOG[k][1][0]*CATALOG[k][1][1],reverse=True)
                furnish(q,program,CATALOG,keys)
            else:
                local_program='teaching' if bid=='E1' and '-bridge' in q['id'] else program
                qw,qd=q['size_m'][:2]
                if q['entrance_side'] in ('east','west'):qw,qd=qd,qw
                if local_program=='teaching' and (qw<4.6 or qd<5):local_program='library'
                if local_program in ('dorm','home','hotel') and (qw<({'dorm':8,'home':9.5,'hotel':7.2}[local_program]) or qd<({'dorm':8.8,'home':8.5,'hotel':8.8}[local_program])):local_program='library'
                name,key,actions=ACTIVITY[local_program];q.update(name=r['name']+'／'+name,functional_role='activity',interactions=actions.split('、'))
                if keys_override:q['activity_keys']=keys_override
                furnish(q,local_program,CATALOG)
        # Any remaining facility is inserted as a small real enclosed room,
        # rather than omitted because the available annex is oversized.
        available=sorted((q for q in group if not q.get('accommodation_kind') and q.get('functional_role')=='activity'),key=lambda q:q['size_m'][0]*q['size_m'][1],reverse=True)
        for q in available:
            for repeat in range(3):
                for side in (-1,1):
                    if requirements and embed_support(q,requirements[0],CATALOG,side):requirements.pop(0)
            if not requirements:break
        for q in group:
            if q.get('functional_role')=='activity':ensure_activity(q,CATALOG)
            campus_specialize(q,bid,r,CATALOG)
        used+=sum(q.get('embedded_support_area_m2',0) for q in group)
        r['functional_programme']=dict(kind=program,unallocated_support_requirements=[x[0] for x in requirements],support_area_ratio=round(used/total,5),operational_requirements=operational(program))
    for r in rooms:
        program=purpose(bid,r)
        r.setdefault('functional_programme',dict(kind=program,unallocated_support_requirements=[x[0] for x in SUPPORT[program]],support_area_ratio=0,operational_requirements=operational(program)))
        q=dict(id=r['id'],size_m=r['size_m'],entrance_side='south')
        # Use the same concrete activity rules for the main interaction unit.
        name=r.get('name','')
        for owned in by_owner.get(r['id'],[]):
            if owned.get('activity_keys'):q['activity_keys']=owned['activity_keys'];break
        furnish(q,program,CATALOG)
        outstanding=r['functional_programme']['unallocated_support_requirements']
        requirements=[x for x in SUPPORT[program] if x[0] in outstanding]
        for repeat in range(3):
            for side in (-1,1):
                if requirements and embed_support(q,requirements[0],CATALOG,side):requirements.pop(0)
        r['functional_programme']['unallocated_support_requirements']=[x[0] for x in requirements]
        total=r['size_m'][0]*r['size_m'][1]+sum(a['size_m'][0]*a['size_m'][1] for a in by_owner[r['id']])
        r['functional_programme']['support_area_ratio']+=q.get('embedded_support_area_m2',0)/total
        ensure_activity(q,CATALOG)
        campus_specialize(q,bid,r,CATALOG)
        if program=='clinic':
            q['equipment_layout'].append(equipment('seat',CATALOG['seat'][1],[q['size_m'][0]/2-1,-q['size_m'][1]/2+1.5,0]))
            q['open_waiting_area']=dict(name='入口候診座席',privacy='診療區另有分隔；候診開放於入口側')
        if bid=='M1' or (bid.startswith('S') and bid[1:].isdigit() and r.get('floor',1)==1 and bid not in ('S18','S19')):
            from street_city_store_program import specialize
            specialize(q,bid,name,CATALOG)
            for tenant_zone in by_owner[r['id']]:
                if tenant_zone.get('functional_role')=='activity':
                    specialize(tenant_zone,bid,name,CATALOG)
        r['dimensioned_programme']=q
        # Compiler consumes the exact catalogue and positions, never guessed
        # dimensions from arbitrary label substrings.
        grouped=defaultdict(list)
        for f in q['equipment_layout']:grouped[f['equipment']].append(f)
        r['asset_specs']=[(CATALOG[k][0],CATALOG[k][1],len(v)) for k,v in grouped.items()]
        r['prop_placements']={CATALOG[k][0]:[dict(position_m=v['room_local_position_m'],rotation_z_deg=v.get('rotation_z_deg',0)) for v in values] for k,values in grouped.items()}
        r['required_props']=[CATALOG[k][0]+'×'+str(len(v)) for k,v in grouped.items()]
