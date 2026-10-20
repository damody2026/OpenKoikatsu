"""Complete floor partitions in a shared local frame; no central-box fallback.

Solve rectangular regions using rooms, full-width cross aisles and longitudinal
spines. Rotate the same solver for courtyard wings. Distinguish physical annex
rooms from the stable 1,000 interactive program IDs.
"""
import math
from street_city_geometry import compact_core, wing_geometry, design_room_dimensions
from street_city_dimensions import CORRIDOR_M, SIDE_AISLE_GROSS_M, DOOR_M, hall_depth
from street_city_sports_layout import complete_side_bands

WALL=.2
AISLE=SIDE_AISLE_GROSS_M  # 2m clear, plus two 0.2m boundary allocations
CORE=6+AISLE

CATALOG={
 'desk':('雙人工作桌',[1.6,.8,.75]),
 'shelf':('商品與書籍陳列架',[1.8,.6,1.8]),
 'seat':('休息座椅',[.6,.6,.9]),
 'rack':('器材收納架',[1.2,.5,1.8]),
 'locker':('更衣置物櫃',[.5,.5,1.8]),
 'mat':('伸展練習墊',[2,1,.05]),
 'bed':('獨立睡眠單人床',[1.1,2.1,1.1]),
 'lounge':('休息躺椅',[.8,1.9,.8]),
 'dining':('四人用餐桌組',[2,1.8,.9]),
 'prep':('備餐工作檯',[1.8,.8,.9]),
 'treatment':('照護檢查床',[2,.8,.8]),
 'console':('資訊服務終端',[1.2,.6,1.3]),
}
from street_city_functional_program import EXTRA_EQUIPMENT, finish_partition, frontage_dimensions, purpose
CATALOG.update(EXTRA_EQUIPMENT)
from street_city_store_program import CATALOG_ADDITIONS
CATALOG.update(CATALOG_ADDITIONS)
from street_city_campus_program import CATALOG_ADDITIONS as CAMPUS_CATALOG
CATALOG.update(CAMPUS_CATALOG)

PROFILES={
 'academic':[('分組討論室','desk','討論、共同學習'),('專題實作室','desk','製作專題、同伴合作'),('教學器材庫','rack','借用器材、歸還器材'),('教師諮詢室','desk','諮詢、討論學習'),('系所資料閱覽室','shelf','閱讀、查詢資料')],
 'admin':[('行政服務洽談室','desk','洽詢、辦理服務'),('行政職員辦公室','desk','工作、會議'),('行政檔案室','shelf','查閱檔案、歸檔'),('民眾等候休息室','seat','等候、休息')],
 'study':[('安靜閱覽室','desk','閱讀、學習'),('圖書開架室','shelf','找書、借閱'),('小組研讀室','desk','共同學習、討論'),('館藏整理室','rack','整理書籍、工作')],
 'lab':[('研究工作室','desk','研究、同伴合作'),('研究器材庫','rack','領用器材、整理器材'),('研究討論室','desk','討論、成果交流'),('研究資料室','shelf','查閱資料、紀錄')],
 'retail':[('商品展售分區','shelf','瀏覽商品、購物'),('商品展售分區','shelf','瀏覽商品、購物'),('商品展售分區','shelf','瀏覽商品、購物'),('後場補貨倉儲','rack','盤點、補貨'),('客服與員工工作室','desk','諮詢、工作')],
 'fashion':[('服裝展售分區','shelf','選購服裝、搭配'),('更衣試穿室','locker','試穿、更衣'),('服裝展售分區','shelf','瀏覽商品、購物'),('服裝與包裝倉儲','rack','盤點、補貨'),('顧客搭配諮詢室','desk','諮詢、搭配')],
 'game':[('遊戲體驗室','console','遊戲、同伴競賽'),('桌遊交流室','dining','桌遊、同伴聊天'),('遊戲器材庫','rack','借用器材、整理'),('玩家休息室','seat','休息、交流')],
 'club':[('社團共同活動室','desk','社團活動、共同製作'),('社團器材庫','rack','借用器材、整理'),('社團交流室','seat','交流、聚會'),('成果展示室','shelf','展示、參觀')],
 'exhibit':[('主題展示室','shelf','參觀、交流'),('展示準備室','desk','布展、工作'),('展品保存室','rack','保存、整理')],
 'food':[('用餐分區','dining','用餐、同伴聊天'),('用餐分區','dining','用餐、休息'),('備餐與洗滌間','prep','備餐、清洗'),('乾貨與餐具庫','rack','領用餐具、盤點')],
 'dorm':[('宿舍共同自習室','desk','自習、共同學習'),('宿舍交誼室','seat','休息、同伴聊天'),('衣物整理與儲物室','locker','寄放物品、整理衣物'),('住戶共享工作室','desk','製作專題、共同工作')],
 'residence':[('住戶共享書房','desk','閱讀、共同工作'),('住戶交誼休息室','seat','休息、聊天'),('住戶衣物儲藏室','locker','整理、收納'),('住戶共享餐室','dining','用餐、聚會')],
 'hotel':[('套房獨立會客室','seat','接待、同伴聊天'),('套房書房','desk','閱讀、工作'),('套房衣帽與行李室','locker','更衣、收納行李'),('住客休息廳','lounge','休息、短暫睡眠')],
 'inn':[('民宿和式交誼室','seat','休息、同伴聊天'),('民宿住客閱讀室','desk','閱讀、工作'),('住客行李與衣物室','locker','寄放行李、更衣'),('民宿休息室','lounge','休息、短暫睡眠')],
 'spa':[('乾區休息室','lounge','休息、補水'),('更衣準備室','locker','更衣、寄放物品'),('伸展恢復室','mat','伸展、放鬆'),('護理休息室','treatment','護理、休息')],
 'health':[('候診休息室','seat','等候、休息'),('照護諮詢室','treatment','照護、諮詢'),('醫療器材庫','rack','領用器材、整理'),('健康資料室','desk','查詢、紀錄')],
 'transit':[('旅客候車室','seat','等候、休息'),('行李寄放室','locker','寄放行李、領取行李'),('旅客服務室','console','查詢路線、購票'),('站務工作室','desk','站務、調度')],
 'service':[('服務洽談室','desk','諮詢、洽談'),('用品與備品庫','rack','領用物品、盤點'),('顧客休息室','seat','等候、休息'),('員工工作室','desk','工作、整理')],
 'sport':[('體能與伸展室','mat','伸展、體能練習'),('運動器材庫','rack','領用器材、歸還器材'),('運動休息室','seat','休息、補水')],
}

def profile(bid,r):
    bid=r.get('programme_template_building',bid)
    t=r['type']
    if bid=='S18':return 'inn'
    if bid=='S19':return 'spa'
    if bid=='AD':return 'admin'
    if bid=='LI':return 'study'
    if bid=='TR':return 'transit'
    if t=='retail':return 'retail'
    if t in ('food','cafe'):return 'food'
    if t=='dorm':return 'dorm'
    if t=='apartment':return 'residence'
    if t in ('deluxe','executive','suite'):return 'hotel'
    if t in ('class','office'):return 'academic'
    if t in PROFILES:return t
    if t=='nap':return 'spa' if bid in ('S19','H1') else 'dorm'
    return 'service'

def map_point(frame,u,v,z):
    ox,oy,ux,uy,vx,vy=frame
    return [ox+u*ux+v*vx,oy+u*uy+v*vy,z]

def map_box(frame,bounds):
    u0,v0,u1,v1=bounds
    corners=[map_point(frame,u,v,0) for u in (u0,u1) for v in (v0,v1)]
    return [min(q[0] for q in corners),min(q[1] for q in corners),max(q[0] for q in corners),max(q[1] for q in corners)]

def fixture_grid(q,key):
    w,d,h=q['size_m'];sw,sd,sh=CATALOG[key][1]
    vertical=q['entrance_side'] in ('north','south')
    if not vertical:w,d,sw,sd=d,w,sd,sw
    # Continuous 3m centre aisle goes from the door through both furniture banks.
    bank=(w-3)/2-.4
    nx=max(0,int(bank/(sw+.8)));ny=max(0,int((d-6)/(sd+.8)))
    result=[]
    for side in (-1,1):
        for j in range(ny):
            for i in range(nx):
                u=side*(1.5+.4+sw/2+i*(sw+.8));v=-d/2+3+sd/2+j*(sd+.8)+(j//3)*2.2
                if v+sd/2>d/2-3:continue
                x,y=(u,v) if vertical else (v,u)
                result.append(dict(equipment=key,size_m=CATALOG[key][1],room_local_position_m=[x,y,0]))
    if not result:
        # A small enclosed workroom is not a corridor. Give it one usable item
        # against the rear wall instead of silently exporting an empty room.
        fw,fd,fh=CATALOG[key][1];qw,qd,_=q['size_m'];side=q['entrance_side']
        if fw<=qw-.4 and fd<=qd-.4:
            x=0;y=qd/2-.2-fd/2 if side=='south' else -qd/2+.2+fd/2 if side=='north' else 0
            if side=='east':x=-qw/2+.2+fw/2
            if side=='west':x=qw/2-.2-fw/2
            result=[dict(equipment=key,size_m=[fw,fd,fh],room_local_position_m=[x,y,0])]
    return result

class Partition:
    def __init__(self,bid,floor,z,height):
        self.bid=bid;self.floor=floor;self.z=z;self.h=height
        self.positions={};self.annex=[];self.routes=[];self.suites=[]
    def route(self,frame,bounds,label):
        a,b,c,d=map_box(frame,bounds)
        q=dict(id=f'{self.bid}-F{self.floor:02d}-path{len(self.routes)+1:03d}',
            center_m=[(a+c)/2,(b+d)/2,self.z],size_m=[c-a,d-b,self.h-.4],
            clear_width_m=CORRIDOR_M,wall_allowance_m=.2,kind=label,connects='connected floor corridor network')
        self.routes.append(q)
    def room(self,frame,bounds,owner,kind,side='east'):
        # Keep dorms within the recorded area budget after aisle changes.
        # Both subdivisions retain a direct door onto the same adjacent route.
        x0,y0,x1,y1=bounds
        if kind=='dorm' and (x1-x0-.4)*(y1-y0-.4)>132+1e-6:
            along_x=side in ('north','south')
            length=(x1-x0) if along_x else (y1-y0)
            cross=(y1-y0) if along_x else (x1-x0)
            cut=min(length-3.2,132/(cross-.4)+.4)
            if cut>=8.4 and length-cut>=3.2-1e-6:
                first=[x0,y0,x0+cut,y1] if along_x else [x0,y0,x1,y0+cut]
                second=[x0+cut,y0,x1,y1] if along_x else [x0,y0+cut,x1,y1]
                self.room(frame,first,owner,kind,side);self.room(frame,second,owner,kind,side)
                return
        a,b,c,d=map_box(frame,bounds);w,dep=c-a,d-b
        assert min(w,dep)>=2.4-1e-6,(owner['id'],bounds,'annex too narrow')
        names=PROFILES[kind];index=sum(q['owner_unit']==owner['id'] for q in self.annex)
        name,key,acts=names[index%len(names)]
        # Transform local side normal to the global axis.
        normal={'east':(1,0),'west':(-1,0),'south':(0,-1),'north':(0,1)}[side]
        _,_,ux,uy,vx,vy=frame
        nx,ny=normal[0]*ux+normal[1]*vx,normal[0]*uy+normal[1]*vy
        face='east' if nx>.5 else 'west' if nx<-.5 else 'north' if ny>.5 else 'south'
        cx,cy=(a+c)/2,(b+d)/2
        door=[c-.2 if face=='east' else a+.2 if face=='west' else cx,
              d-.2 if face=='north' else b+.2 if face=='south' else cy,self.z]
        q=dict(id=f'{owner["id"]}-aux{index+1:03d}',name=name,owner_unit=owner['id'],profile=kind,
            gross_bounds_m=[a,b,c,d],position_m=[cx,cy,self.z],size_m=[w-.4,dep-.4,min(owner['size_m'][2],self.h-.4)],
            entrance_side=face,entrance_world_local_m=door,door_clear_width_m=DOOR_M,door_clear_height_m=2.4,
            connects_to='floor_corridor_network',interactions=acts.split('、'),
            clear_door_side_aisle_m=CORRIDOR_M,clear_transverse_aisle_m=CORRIDOR_M,
            furnishing_status='dimensioned_equipment_grid; actual_mesh_collision_and_use_pose_pending')
        q['equipment_layout']=fixture_grid(q,key);q['equipment_counts']={key:len(q['equipment_layout'])}
        self.annex.append(q)
    def frontier(self,frame,left,right,depth,rooms):
        """Rectangular half-floor; v=0 joins the parent corridor or open public hall."""
        minimum=sum(r['size_m'][0]+.4+AISLE for r in rooms)
        spare=(right-left-minimum)/len(rooms)
        assert spare>=-.00001,(self.bid,self.floor,'room frontage exceeds exterior')
        cursor=left
        for r in rooms:
            rw,rd,rh=r['size_m'];cellw=rw+.4+AISLE+spare;end=cursor+cellw;edge=end-AISLE
            rw,rd=frontage_dimensions(self.bid,r,edge-cursor-.4,depth-.4)
            r['size_m'][:2]=[rw,rd]
            excess=edge-(cursor+rw+.4)
            sleeping=profile(self.bid,r) in ('dorm','residence','hotel','inn') or r['type']=='nap'
            # A small one-business floor uses the whole activity half-floor;
            # its bounded staff/storage rooms are partitioned inside the shop.
            if compact_core(self.bid) and len(rooms)==1 and not sleeping:
                rd=depth-.4;r['size_m'][1]=rd
            if excess<2.4:
                rw+=excess;r['size_m'][0]=rw;excess=0
            if depth-(rd+.4)<(2.4 if sleeping else AISLE+3):
                rd=depth-.4;r['size_m'][1]=rd
            assert rd+.4<=depth+1e-6
            # A full-depth shop with its own hall door has no rear rooms to
            # serve. Absorb its otherwise blind perimeter spine into the shop.
            if compact_core(self.bid) and excess<2.4 and abs(rd+.4-depth)<1e-6:
                rw+=AISLE;edge=end;r['size_m'][0]=rw
            self.positions[r['id']]=map_point(frame,cursor+.2+rw/2,.2+rd/2,self.z)
            kind=profile(self.bid,r)
            self.suites.append(dict(owner_unit=r['id'],gross_bounds_m=map_box(frame,[cursor,0,end,depth]),profile=kind,primary_clear_size_m=[rw,rd,rh]))
            if end-edge>1e-6:self.route(frame,[edge,0,end,depth],'suite_spine')
            # All frontage annexes get their own door onto the parent hall.
            if excess>=2.4:
                n=max(1,math.ceil(excess/({'dorm':10.5,'home':14,'hotel':16}.get(purpose(self.bid,r),20))))
                for k in range(n):
                    self.room(frame,[cursor+rw+.4+excess*k/n,0,cursor+rw+.4+excess*(k+1)/n,rd+.4],r,kind,'south')
            rear=rd+.4
            if depth-rear<AISLE+3:
                if depth-rear>=2.4:self.room(frame,[cursor,rear,edge,depth],r,kind,'east')
                cursor=end;continue
            self.route(frame,[cursor,rear,edge,rear+AISLE],'cross_aisle')
            rear+=AISLE
            usable=edge-cursor
            max_bay=14 if purpose(self.bid,r) in ('hotel','home','dorm') else 24
            columns=max(1,math.ceil((usable+AISLE)/(max_bay+AISLE)))
            cw=(usable-(columns-1)*AISLE)/columns
            assert cw>=2.4
            rows=max(1,math.ceil((depth-rear)/22))
            for c in range(columns):
                x0=cursor+c*(cw+AISLE);x1=x0+cw
                if c<columns-1:self.route(frame,[x1,rear,x1+AISLE,depth],'rear_spine')
                for k in range(rows):
                    self.room(frame,[x0,rear+(depth-rear)*k/rows,x1,rear+(depth-rear)*(k+1)/rows],r,kind,'south' if rows==1 and depth-rear>=9.2 else 'east')
            cursor=end
        assert abs(cursor-right)<1e-6

def solve_floor(bid,rooms,footprint,z,floor,height):
    for r in rooms:r['size_m']=design_room_dimensions(r)
    w,d=footprint;half=hall_depth(bid)/2;p=Partition(bid,floor,z,height)
    n=math.ceil(len(rooms)/2)
    if bid=='E1':
        # Same algorithm rotated into the two courtyard wings. The courtyard
        # is an actual exterior void, not a missing indoor rectangle.
        wg=wing_geometry(footprint);edge=wg['edge'];cy=wg['cy'];wing_depth=w/2-CORE-edge
        p.frontier((-edge,0,0,1,-1,0),cy+half,d/2-.2,wing_depth,rooms[:n])
        p.frontier((edge,0,0,1,1,0),cy+half,d/2-.2,wing_depth,rooms[n:])
        # The southern connecting wing is an actual full rectangular region.
        owner=rooms[-1];kind=profile(bid,owner)
        for sign in (-1,1):
            # Core hall sits at y=-130; preserve its current clear cross connection.
            p.frontier((0,cy+sign*half,1,0,0,sign),-wg['bridge_half'] if sign>0 else -w/2+CORE,wg['bridge_half'] if sign>0 else w/2-CORE,
                wg['courtyard'][1]-(cy+half) if sign>0 else d/2+cy-half-.2,[dict(owner,id=owner['id']+'-bridge'+str(sign),size_m=[16,10,3.6])])
        # Bridge rooms are annexes; they do not add primary program IDs.
        for key,pos in list(p.positions.items()):
            if '-bridge' not in key:continue
            # Convert their primary control box to a normal physical annex.
            sign=1 if key.endswith('1') and not key.endswith('-1') else -1
            primary=p.suites[next(i for i,q in enumerate(p.suites) if q['owner_unit']==key)]
            for q in p.annex:
                if q['owner_unit']==key:q['owner_unit']=owner['id']
            x,y,_=pos
            rw,rd=primary['primary_clear_size_m'][:2]
            a=[x-rw/2-.2,y-rd/2-.2,x+rw/2+.2,y+rd/2+.2]
            # Populate a direct physical-room record rather than a duplicate interactive ID.
            frame=(0,0,1,0,0,1)
            p.room(frame,a,owner,kind,'south' if sign>0 else 'north')
            del p.positions[key]
        for q in p.suites:
            if '-bridge' in q['owner_unit']:q['owner_unit']=owner['id']
    else:
        left,right=-w/2+CORE,w/2-(.2 if compact_core(bid) else CORE);depth=d/2-half-.2
        single_copy=dict(rooms[0],size_m=list(rooms[0]['size_m'])) if len(rooms)==1 else None
        p.frontier((0,half,1,0,0,1),left,right,depth,rooms[:n])
        south=rooms[n:]
        if south:p.frontier((0,-half,1,0,0,-1),left,right,depth,south)
        else:
            # One-business floors still use the full second half of their building.
            clone=dict(single_copy,id=rooms[0]['id']+'-south',size_m=list(single_copy['size_m']))
            p.frontier((0,-half,1,0,0,-1),left,right,depth,[clone])
            x,y,_=p.positions.pop(clone['id']);rw,rd,_=clone['size_m']
            for q in p.annex:
                if q['owner_unit']==clone['id']:q['owner_unit']=rooms[0]['id']
            p.room((0,0,1,0,0,1),[x-rw/2-.2,y-rd/2-.2,x+rw/2+.2,y+rd/2+.2],rooms[0],profile(bid,rooms[0]),'north')
            for q in p.suites:
                if q['owner_unit']==clone['id']:q['owner_unit']=rooms[0]['id']
    finish_partition(bid,rooms,p.annex)
    from street_city_space_policy import mark_space_policy
    mark_space_policy(bid,rooms,p.annex)
    return dict(positions=p.positions,annex_rooms=p.annex,spines=p.routes,suites=p.suites)
