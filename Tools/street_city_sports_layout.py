"""Partition the full 180x200 sports building, retaining regulation activity boxes.

The original forty interactive units retain their activity dimensions and identity. Adjacent
physical rooms belong to those units; they are not additional scene transitions.
All rectangles describe gross wall allocations, with explicit clear dimensions.
"""
import math
from street_city_dimensions import CORRIDOR_M, CORE_BAND_M, SIDE_AISLE_GROSS_M, DOOR_M
from street_city_envelope import box, subtract_rect

EQUIPMENT={
 'mat':('個人伸展墊',[2,1,.05]),
 'bench':('訓練凳',[1.5,.6,.5]),
 'seat':('觀眾休息座椅',[.6,.6,.9]),
 'locker':('個人物品櫃',[.5,.5,1.8]),
 'rack':('器材收納架',[1.2,.5,1.8]),
 'table':('員工工作桌',[1.4,.7,.75]),
 'change_bench':('更衣長凳',[2,.45,.45]),
 'shower':('淋浴隔間含可開門',[1.2,1.4,2.4]),
 'wc_cubicle':('廁所隔間含馬桶',[1.4,1.9,2.4]),
 'accessible_wc':('無障礙廁所含扶手',[2.3,2.3,2.4]),
 'family_shower':('無障礙淋浴隔間',[2.2,2.2,2.4]),
 'basin':('洗手台',[.65,.55,.85]),
 'filter':('泳池過濾設備',[6,3,2.4]),
 'pump':('泳池循環泵浦',[3,2,1.5]),
 'chemical':('通風上鎖藥劑櫃',[2,1.2,2]),
 'worktop':('維修工作檯',[2,.8,.9]),
 'treatment':('急救檢查床',[2,.8,.8]),
 'aid_cabinet':('急救設備櫃',[1,.6,1.8]),
 'water':('飲水設備',[.8,.7,1.3]),
}

def furnish(q):
    """A dimensioned equipment grid, with a clear door-side path and cross aisles."""
    w,d,_=q['size_m'];name=q['name']
    if '觀眾' in name or '休息廳' in name:key,pitch='seat',(1.2,1.8)
    elif '更衣' in name or '寄放' in name:key,pitch='locker',(1.2,2.4)
    elif '庫' in name or '備品' in name:key,pitch='rack',(2.4,2.4)
    elif '員工' in name:key,pitch='table',(2.8,2.8)
    elif '體能' in name or '循環' in name:key,pitch='bench',(3.2,3)
    else:key,pitch='mat',(3.2,3)
    # All circulation lanes keep at least2m clear, in addition to furniture dimensions.
    usable_w=w-3.6;usable_d=d-6
    nx=max(0,math.floor(usable_w/pitch[0]));ny=max(0,math.floor(usable_d/pitch[1]))
    east=q['entrance_side']=='east';result=[]
    for j in range(ny):
        # Every fourth row is an actual unobstructed transverse aisle.
        if j%4==3:continue
        for i in range(nx):
            x=-w/2+.6+pitch[0]*(i+.5)
            if not east:x=-x
            y=-d/2+3+pitch[1]*(j+.5)
            result.append(dict(equipment=key,room_local_position_m=[x,y,0],size_m=EQUIPMENT[key][1]))
    q['equipment_layout']=result
    q['equipment_counts']={key:len(result)}
    q['clear_door_side_aisle_m']=CORRIDOR_M
    q['clear_transverse_aisle_m']=CORRIDOR_M
    q['furnishing_status']='dimensioned_equipment_grid; actual_mesh_collision_and_use_pose_pending'
    return q

def room_record(rid,name,bounds,z,h,side,owner,activities):
    x0,y0,x1,y1=bounds
    w,d=x1-x0,y1-y0
    center=[(x0+x1)/2,(y0+y1)/2,z]
    door=[x1-.2,(y0+y1)/2,z] if side=='east' else [x0+.2,(y0+y1)/2,z]
    return furnish(dict(id=rid,name=name,owner_unit=owner,gross_bounds_m=bounds,
        position_m=center,size_m=[w-.4,d-.4,h],
        wall_allowance_m=.2,entrance_side=side,entrance_world_local_m=door,
        door_clear_width_m=DOOR_M,door_clear_height_m=2.4,
        connects_to='longitudinal_spine',interactions=activities.split('、'),
        furnishing_status='dimensioned_room; detailed_equipment_and_collision_pending'))

def service_furnish(q,kind):
    """Purpose-specific layouts, leaving the centre and door path clear."""
    w,d,_=q['size_m'];items=[]
    def add(key,x,y,z=0):
        items.append(dict(equipment=key,size_m=EQUIPMENT[key][1],room_local_position_m=[x,y,z]))
    if kind=='change':
        for x in (-6,-4,-2,0,2,4):
            for sign in (-1,1):add('locker',x,sign*(d/2-.5))
        for x in (-4,0,4):add('change_bench',x,d/2-1.2)
    elif kind=='shower':
        for x in (-7,-5.4,-3.8,-2.2,-.6,1,2.6,4.2):add('shower',x,d/2-.9)
    elif kind=='wc':
        for x in (-6,-4, -2):add('wc_cubicle',x,d/2-1.1)
        for x in (1,3,4.5):add('basin',x,d/2-.5)
    elif kind=='family':
        add('accessible_wc',-5,d/2-1.35);add('family_shower',0,d/2-1.3)
        add('change_bench',4,d/2-.6)
    elif kind=='plant':
        add('filter',-4,2.35);add('pump',3.5,2.5);add('chemical',-6,-2.9);add('worktop',1,-3.3)
    elif kind=='aid':
        add('treatment',-4,d/2-.8);add('basin',3,d/2-.5);add('aid_cabinet',-6,d/2-.5)
    elif kind=='refresh':
        for x in (-5,0,4.5):add('water',x,d/2-.6)
    elif kind=='staff':
        for x in (-5,0,4.4):add('table',x,d/2-.8)
    else:
        for x in (-6,-3,0,3,4.5):add('rack',x,d/2-.5)
    q['equipment_layout']=items
    q['equipment_counts']={key:sum(i['equipment']==key for i in items) for key in {i['equipment'] for i in items}}
    q['clear_door_side_aisle_m']=CORRIDOR_M;q['clear_transverse_aisle_m']=CORRIDOR_M
    q['programme_kind']=kind
    return q


def plan_floor(rooms,footprint,z,floor,height):
    w,d=footprint;left=-w/2+CORE_BAND_M;right=w/2-CORE_BAND_M;outer=d/2-.2
    assert len(rooms)==8 and footprint==[180,200]
    positions={};annex=[];spines=[];service_doors={}
    def route(rid,x0,v0,x1,v1,sign):
        bounds=[x0,v0,x1,v1] if sign==1 else [x0,-v1,x1,-v0]
        spines.append(dict(id=rid,center_m=[(bounds[0]+bounds[2])/2,(bounds[1]+bounds[3])/2,z],
            size_m=[x1-x0,v1-v0,height-.4],clear_width_m=CORRIDOR_M,wall_allowance_m=.2,
            connects='main corridor, sport entrance and service doors',kind='sports_service_spine'))
    for row_index,row in enumerate((rooms[:4],rooms[4:])):
        sign=1 if row_index==0 else -1
        for col,r in enumerate(row):
            cursor=left+(right-left)*col/4;end=left+(right-left)*(col+1)/4;spine_left=end-SIDE_AISLE_GROSS_M
            rw,rd,rh=r['size_m'];assert abs(spine_left-cursor-rw-.4)<1e-6
            positions[r['id']]=[cursor+.2+rw/2,sign*(CORRIDOR_M/2+.2+rd/2),z]
            route(r['id']+'-spine',spine_left,CORRIDOR_M/2,end,outer,sign)
            if r['sport_geometry']['activity']=='pool':
                # The deck has an actual wall opening to its own wet-service spine.
                # Both rows have the spine on world east; southern halls are rotated.
                service_doors[r['id']]=dict(side='east' if sign==1 else 'west',
                    local_position_m=[sign*rw/2,sign*(rd/2-2),0],width_m=DOOR_M,height_m=2.4,
                    world_local_position_m=[spine_left-.2,sign*(CORRIDOR_M/2+.2+rd-2),z],
                    connects_to=r['id']+'-spine',purpose='更衣、淋浴與廁所專用通路至池畔')
            front_end=CORRIDOR_M/2+rd+.4
            if outer-front_end<.01:continue
            route(r['id']+'-cross',cursor,front_end,spine_left,front_end+SIDE_AISLE_GROSS_M,sign)
            rear_start=front_end+SIDE_AISLE_GROSS_M;bay_width=(spine_left-cursor-SIDE_AISLE_GROSS_M)/2
            route(r['id']+'-rear-spine',cursor+bay_width,rear_start,cursor+bay_width+SIDE_AISLE_GROSS_M,outer,sign)
            activity=r['sport_geometry']['activity']
            if activity=='pool':
                programme=[('男更衣室','change','更衣、寄放衣物'),('女更衣室','change','更衣、寄放衣物'),
                  ('男淋浴室','shower','淋浴、清潔'),('女淋浴室','shower','淋浴、清潔'),
                  ('男廁所','wc','如廁、洗手'),('女廁所','wc','如廁、洗手'),
                  ('無障礙家庭更衣衛浴','family','家庭更衣、淋浴、如廁'),('水處理設備間','plant','水質檢查、設備維修')]
            elif activity=='gallery':
                programme=[('男觀眾廁所','wc','如廁、洗手'),('女觀眾廁所','wc','如廁、洗手'),
                  ('無障礙家庭廁所','family','如廁、更衣'),('賽務計時室','staff','計時、賽務工作'),
                  ('觀賽用品庫','store','領用物品、整理'),('清潔備品室','store','清潔、領用備品'),
                  ('觀眾急救室','aid','接受照護、急救'),('飲水補給室','refresh','補水、購買飲品')]
            else:
                programme=[('男更衣室','change','更衣、寄放衣物'),('女更衣室','change','更衣、寄放衣物'),
                  ('男淋浴室','shower','淋浴、清潔'),('女淋浴室','shower','淋浴、清潔'),
                  ('球具器材庫','store','借用器材、歸還器材'),('運動急救室','aid','接受照護、急救')]
            count=len(programme)//2;step=(outer-rear_start)/count
            for k,(name,kind,actions) in enumerate(programme):
                column=k%2;row=k//2;x0=cursor+column*(bay_width+SIDE_AISLE_GROSS_M)
                a=rear_start+step*row;b=rear_start+step*(row+1)
                bounds=[x0,a,x0+bay_width,b] if sign==1 else [x0,-b,x0+bay_width,-a]
                q=room_record(r['id']+f'-service{k+1}',name,bounds,z,min(4,height-.4),'east',r['id'],actions)
                annex.append(service_furnish(q,kind))
    return dict(positions=positions,annex_rooms=annex,spines=spines,service_doors=service_doors)


def complete_side_bands(fl,footprint,bid='SP'):
    """Use full-length service aisles; subdivide actual remaining core bays."""
    w,d=footprint;z=fl['z_m'];h=fl['height_m']-.4
    added=[];structure=[]
    for side,sign in __import__('street_city_geometry').cores(bid):
        # The 2m clear access path continues along the whole rectangular floor.
        aisle=next(q for q in fl['service_access_corridors'] if q['id'].endswith(side))
        aisle['position_m'][1]=0;aisle['size_m'][1]=d-.4
        aisle['connects']='main corridor, all core doors and full-length service bays'
        source=[-w/2+.2,-d/2+.2,-w/2+6,d/2-.2] if sign==-1 else [w/2-6,-d/2+.2,w/2-.2,d/2-.2]
        existing=[q for q in fl['vertical_circulation'] if q['core']==side]+[q for q in fl['support_rooms'] if q['core']==side]
        cuts=[box(q['position_m'],q['size_m']) for q in existing]
        ys=sorted({source[1],source[3]}|{max(source[1],min(source[3],v)) for c in cuts for v in (c[1],c[3])})
        pieces=[]
        for ya,yb in zip(ys,ys[1:]):
            band=[[source[0],ya,source[2],yb]]
            for cut in cuts:band=[p for s in band for p in subtract_rect(s,cut)]
            for r in band:
                previous=next((p for p in pieces if abs(p[0]-r[0])<1e-7 and abs(p[2]-r[2])<1e-7 and abs(p[3]-r[1])<1e-7),None)
                if previous is None:pieces.append(r)
                else:previous[3]=r[3]
        for j,r in enumerate(pieces):
            x0,y0,x1,y1=r
            # Narrow residuals beside shafts are explicit structural/pipe bands.
            if x1-x0<2.4 or y1-y0<3:
                structure.append(dict(name='核心牆體／立管帶',gross_bounds_m=r,position_m=[(x0+x1)/2,(y0+y1)/2,z],size_m=[x1-x0,y1-y0,h]))
                continue
            n=max(1,math.ceil((y1-y0)/12))
            for k in range(n):
                bounds=[x0,y0+(y1-y0)*k/n,x1,y0+(y1-y0)*(k+1)/n]
                name=('樓層備品與器材庫','清潔與耗材庫','員工更衣休息室','維修備品室')[k%4]
                q=room_record(f'{bid}-F{fl["floor"]:02d}-core{side}-{j}-{k}',name,bounds,z,min(3.6,h),'east' if sign==-1 else 'west',bid,'領用物品、工作值勤')
                q['connects_to']='full_length_service_aisle';added.append(q)
    fl['core_annex_rooms']=added;fl['structural_bands']=structure
