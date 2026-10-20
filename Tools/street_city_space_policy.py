"""Use-based enclosure policy and connected open-zone wall apertures, in metres."""
import math
from street_city_geometry import main_hall
from street_city_envelope import box

PRIVATE=('私人','私密','獨立','包廂','客房','套房','寢室','睡眠','午睡','哺乳','更衣','試衣','試穿','淋浴','廁所','衛浴')
ISOLATED=('教室','教育室','工作室','實習','實作','實驗','診','治療','照護','隔音','練習室','錄音','放映','劇場','影廳','會議','討論','研討','安靜','靜音','烘焙製作','廚房','烹調','備料','清洗','洗滌','清潔','水處理','機電','設備','保管','後倉','後場','倉儲','器材','物流','分揀','包裝','行李','檔案','典藏')
OPEN_PROGRAMS={'retail','fashion','food','bakery','fruit','flower','pet','salon','bike','pharmacy','laundry','library','game','transit','post','bank','lottery','exhibit'}

def enclosure(kind,name,program=None,role=None,bid=None):
    if role=='support':return False,'後場、設備或工作準備需要獨立隔間'
    if any(t in name for t in PRIVATE):return False,'隱私、住宿或衛浴用途需要隔間'
    if any(t in name for t in ISOLATED):return False,'隔音、教學、安全、衛生或後勤用途需要隔間'
    if kind in ('dorm','apartment','inn','deluxe','executive','suite','nap','health','lab','class','spa','sport') or bid=='SP':return False,'住宿、診療、教學或運動乾濕分隔用途'
    if kind in ('retail','fashion','food','cafe','exhibit','transit','study','game') or program in OPEN_PROGRAMS:return True,'公共展示、用餐、閱覽或候車採連通開放分區'
    if any(t in name for t in ('大廳','交誼','接待','櫃檯','服務台','客用服務廳','休憩','展廳','展售','展覽','閱讀','閱覽','郵局','銀行服務','禮賓','洗衣店','洗衣坊','洗衣服務','理髮美容店','髮廊','美髮','共享辦公','自行車租借','旅遊資訊')):return True,'接待、交流與公共服務採開放分區'
    return False,'辦公、專用活動或受管理空間保留隔間'

def mark_space_policy(bid,rooms,annex):
    from street_city_functional_program import purpose
    parents={r['id']:r for r in rooms}
    for r in rooms:
        r.pop('open_plan',None)
        opened,reason=enclosure(r['type'],r['name'],bid=bid)
        r['enclosure_policy']=dict(mode='open' if opened else 'enclosed',reason=reason)
        if opened:r['open_plan']=dict(kind='connected_public_space',local_open_side='south',reason=reason)
    for q in annex:
        q.pop('open_plan',None)
        opened,reason=enclosure('annex',q['name'].rsplit('／',1)[-1],q.get('programme_kind'),q.get('functional_role'),bid)
        parent=parents[q['owner_unit']]
        if parent['type'] in ('retail','fashion','food','cafe','exhibit','study','service','game') and any(t in parent['name'] for t in PRIVATE):
            opened=False;reason='包廂或私人活動的延伸區繼承所屬用途隱私'
        if not opened and reason=='辦公、專用活動或受管理空間保留隔間' and parent.get('open_plan') and q.get('programme_kind')==purpose(bid,parent):opened=True;reason='同一公共用途的連通延伸分區'
        if q.get('accommodation_kind'):opened=False;reason='住宿單元需要獨立隔間'
        q['enclosure_policy']=dict(mode='open' if opened else 'enclosed',reason=reason)
        if opened:q['open_plan']=dict(kind='connected_public_space',local_open_side=q['entrance_side'],reason=reason)

def open_edge_aperture(size,side):
    w,d,h=size
    return dict(side=side,local_position_m=[0,0,0],width_m=w if side in ('north','south') else d,height_m=h,
                kind='full_height_open_display_edge_no_wall_no_door')

def segments(origin,size,yaw,opening):
    w,d=size[:2];side=opening['side'];p=opening['local_position_m'];half=opening['width_m']/2
    if side in ('north','south'):a,b=[p[0]-half,d/2*(1 if side=='north' else -1)],[p[0]+half,d/2*(1 if side=='north' else -1)]
    else:a,b=[w/2*(1 if side=='east' else -1),p[1]-half],[w/2*(1 if side=='east' else -1),p[1]+half]
    angle=math.radians(yaw)
    return [[origin[0]+x*math.cos(angle)-y*math.sin(angle),origin[1]+x*math.sin(angle)+y*math.cos(angle)] for x,y in (a,b)]

def resolve_open_edges(building,floor,units,assets):
    """Remove only wall spans adjoining circulation or compatible open zones.

    Exterior walls and walls against enclosed rooms remain intact. Neighbouring
    retail tenants keep demising walls; reading sections share one open library.
    """
    zones=[]
    for r in units:
        r['enclosure_policy']=r.get('enclosure_policy',dict(mode='enclosed',reason='專用房間'))
        if r.get('open_plan'):zones.append((r['room_id'],r['position_m'],r['clear_size_m'],r['rotation_z_deg'],r))
    for q in floor['annex_rooms']+floor['core_annex_rooms']:
        if q.get('open_plan'):zones.append((q['id'],q['position_m'],q['size_m'],0,q))
    hall=main_hall(building['building'],building['footprint_m'][0],floor['corridor_y_m'])
    routes=[box(hall['center_m'],hall['size_m'])]+[box(q.get('center_m',q.get('position_m')),q['size_m']) for q in floor['branch_corridors']+floor['service_access_corridors']]
    records=[]
    for identity,origin,size,yaw,q in zones:
        rect=box(origin,size,yaw);owner=q.get('owner_unit',identity)
        allowed=list(routes)
        for other,center,sz,angle,oq in zones:
            if other==identity:continue
            if oq.get('owner_unit',other)==owner or building['building']=='LI':allowed.append(box(center,sz,angle))
        openings=[]
        for si,side in enumerate(('south','east','north','west')):
            vertical=side in ('east','west');along=1 if vertical else 0;across=1-along
            edge=rect[across+(2 if side in ('north','east') else 0)];intervals=[]
            for n in allowed:
                near=n[across+(0 if side in ('north','east') else 2)]
                gap=(near-edge)*(1 if side in ('north','east') else -1)
                a=max(rect[along],n[along]);b=min(rect[along+2],n[along+2])
                if -.00001<=gap<=.40001 and b-a>1e-6:intervals.append([a,b])
            merged=[]
            for a,b in sorted(intervals):
                if merged and a<=merged[-1][1]+1e-6:merged[-1][1]=max(b,merged[-1][1])
                else:merged.append([a,b])
            local_side=('south','east','north','west')[(si-round(yaw/90))%4]
            for a,b in merged:
                gx,gy=(edge,(a+b)/2) if vertical else ((a+b)/2,edge)
                theta=math.radians(-yaw);dx,dy=gx-origin[0],gy-origin[1]
                openings.append(dict(side=local_side,local_position_m=[dx*math.cos(theta)-dy*math.sin(theta),dx*math.sin(theta)+dy*math.cos(theta),0],width_m=b-a,height_m=size[2],kind='open_public_edge_no_wall_no_door'))
        if not openings:raise ValueError(identity+': open zone has no actual connection')
        asset=assets[identity+'-shell']
        actual_clear=asset.get('clear_size_m',asset['size_m'] if asset['role']=='annex_room' else [asset['size_m'][0]-.4,asset['size_m'][1]-.4,asset['size_m'][2]])
        # Asset dimensions are published to millimetres. Clip interval endpoints
        # to that actual manufactured span, rather than widening wall tolerance.
        for e in openings:
            axis=0 if e['side'] in ('north','south') else 1
            along=e['local_position_m'][axis];half=e['width_m']/2;limit=actual_clear[axis]/2
            lo=max(-limit,along-half);hi=min(limit,along+half)
            if e['width_m']-(hi-lo)>.001:raise ValueError(identity+': open edge exceeds manufactured wall')
            e['local_position_m'][axis]=(lo+hi)/2;e['width_m']=hi-lo;e['height_m']=actual_clear[2]
        asset['open_display_edges']=openings
        asset['required_submeshes']=['perimeter_wall_segments_only_where_not_open','ceiling_removable']
        style=asset['grok_prompt'].split('ONE isolated asset:',1)[0]
        asset['grok_prompt']=style+f'ONE isolated asset: open public zone shell, clear size {actual_clear} metres. Three separated perspective views of the SAME geometry: front-left, rear-right, high-top. Preserve attached deterministic control geometry, dimensions and apertures. Open edges {openings}: remove the specified full-height wall spans, with NO door, frame or lintel. Retain exterior walls and enclosed back-of-house boundaries. Do not add walls between display or reception zones. Furnishings and internal private rooms are separate assets. Ceiling removable; never merge adjacent assets or create a complete building.'
        q['open_plan']['open_edges']=openings
        records.append(dict(id=identity,center_m=origin,size_m=size,rotation_z_deg=yaw,open_edges=openings,reason=q['open_plan']['reason']))
    floor['open_activity_zones']=records

def open_zone_svg(zone,ox,oy,scale):
    out=''
    for i,opening in enumerate(zone['open_edges']):
        a,b=segments(zone['center_m'],zone['size_m'],zone['rotation_z_deg'],opening)
        out+=f'<line id="{zone["id"]}-open-edge-{i}" data-open-display-edge="{opening["side"]}" data-clear-span-m="{opening["width_m"]}" x1="{ox+a[0]*scale}" y1="{oy-a[1]*scale}" x2="{ox+b[0]*scale}" y2="{oy-b[1]*scale}" stroke="#dcece8" stroke-width="{.5*scale}"><title>開放分區：此段無隔牆、無門</title></line>'
    return out

