"""One architectural door schedule and SVG convention for every floor.

Coordinates are metres in the building frame. Swing symbols describe the
planned opening; final moving meshes/collision remain a separate approval.
"""
import math,html
from street_city_dimensions import DOOR_M

NORMAL={'south':(0,-1),'east':(1,0),'north':(0,1),'west':(-1,0)}

def rotated(point,origin,yaw):
    a=math.radians(yaw);x,y=point[:2]
    return [origin[0]+x*math.cos(a)-y*math.sin(a),origin[1]+x*math.sin(a)+y*math.cos(a)]

def side_at(yaw):return {0:'south',90:'east',180:'north',270:'west'}[round(yaw)%360]

def floor_doors(building,floor,rooms,assets):
    doors=[]
    def add(did,center,side,width,kind='hinged',owner=None,source='room',height=2.4):
        doors.append(dict(id=did,center_m=list(center[:2]),outward_side=side,width_m=width,height_m=height,
                          kind=kind,opens='inward',owner=owner or did,
                          source=source,hinge_convention='left_when_viewed_from_outside',motion_mesh_verified=False))
    def internal(parent,origin,yaw):
        for sub in parent.get('internal_rooms',[]):
            a,b,c,d=sub['bounds_m'];dx,dy,_=sub['door']['position_m']
            side='west' if abs(dx-a)<.01 else 'east' if abs(dx-c)<.01 else 'south' if abs(dy-b)<.01 else 'north'
            base={'south':0,'east':90,'north':180,'west':270}[side]
            add(sub['id']+'-door',rotated([dx,dy],origin,yaw),side_at(base+yaw),sub['door']['width_m'],owner=sub['id'],source='internal_partition')
    for room in rooms:
        if not room.get('open_plan'):
            add(room['room_id']+'-door',rotated(room['door']['local_position_m'],room['position_m'],room['rotation_z_deg']),
                side_at(room['rotation_z_deg']),room['door']['width_m'],kind='double_hinged' if room['door']['width_m']>DOOR_M else 'hinged',owner=room['room_id'])
        internal(room.get('dimensioned_programme',{}),room['position_m'],room['rotation_z_deg'])
        for i,q in enumerate(room.get('service_doors',[]),1):
            add(room['room_id']+f'-wet-door{i}',q['world_local_position_m'],q['side'],q['width_m'],owner=room['room_id'],source='wet_service')
    for q in floor['annex_rooms']+floor['core_annex_rooms']:
        if not q.get('open_plan'):
            add(q['id']+'-door',q['entrance_world_local_m'],q['entrance_side'],q['door_clear_width_m'],owner=q['id'],source='annex')
        internal(q,q['position_m'],0)
    for q in floor['support_rooms']:
        point=[q['position_m'][i]+q['entrance_room_local_m'][i] for i in (0,1)]
        add(q['id']+'-door',point,q['entrance_side'],q['clear_door_width_m'],owner=q['id'],source='support',height=q['clear_door_height_m'])
    for q in floor['vertical_circulation']:
        x,y,_=q['position_m']
        if q['kind']=='stairs':
            add(q['id']+'-fire-door',[x,y-q['size_m'][1]/2],'south',q['walkable_geometry']['door_clear_width_m'],source='stairs')
        elif q['kind']=='lift':
            add(q['id']+'-landing-door',q['landing_position_m'],'north',q['clear_door_width_m'],kind='sliding',source='lift')
        elif q['kind']=='restroom':
            add(q['id']+'-entrance',q['entrance_position_m'],'east' if q['core']=='A' else 'west',DOOR_M,source='restroom')
            for i,fixture in enumerate(q['fixtures']):
                spec=assets[fixture['asset_id']]
                if 'door_clear_width_m' not in spec:continue
                origin=[x+fixture['room_local_position_m'][0],y+fixture['room_local_position_m'][1]]
                point=rotated([0,-spec['size_m'][1]/2],origin,fixture['rotation_z_deg'])
                add(q['id']+f'-cubicle-{i}',point,side_at(fixture['rotation_z_deg']),spec['door_clear_width_m'],source='wc_cubicle')
    if floor['floor']==1:
        for q in building['entrances']:
            add(q['id'],q['position_m'],{'N':'north','S':'south','E':'east','W':'west'}[q['facade_side']],q['width_m'],
                kind='double_hinged',source='building_entrance',height=q['height_m'])
    assert len({q['id'] for q in doors})==len(doors),'Duplicate floor door identity'
    return doors

def leaves(door):
    """Return hinge/closed/open endpoints in metre coordinates."""
    if door['kind']=='sliding':return []
    n=NORMAL[door['outward_side']];t=(-n[1],n[0]);cx,cy=door['center_m'];width=door['width_m']
    direction=1 if door['opens']=='outward' else -1
    result=[]
    for handed in ((-1,1) if door['kind']=='double_hinged' else (-1,)):
        length=width/2 if door['kind']=='double_hinged' else width
        hinge=[cx+handed*t[0]*width/2,cy+handed*t[1]*width/2]
        closed=[hinge[i]-handed*t[i]*length for i in (0,1)]
        opened=[hinge[i]+direction*n[i]*length for i in (0,1)]
        cross=(closed[0]-hinge[0])*(opened[1]-hinge[1])-(closed[1]-hinge[1])*(opened[0]-hinge[0])
        result.append(dict(hinge=hinge,closed=closed,open=opened,radius_m=length,sweep=0 if cross>0 else 1))
    return result

def door_svg(door,ox,oy,scale):
    def point(p):return [ox+p[0]*scale,oy-p[1]*scale]
    n=NORMAL[door['outward_side']];t=(-n[1],n[0]);center=door['center_m'];w=door['width_m']
    a=point([center[i]-t[i]*w/2 for i in (0,1)]);b=point([center[i]+t[i]*w/2 for i in (0,1)])
    out=f'<g id="{door["id"]}" data-door-kind="{door["kind"]}" data-door-width-m="{w}" data-outward-side="{door["outward_side"]}"><title>{html.escape(door["owner"])}；門淨寬{w}m；'+('雙扇內開' if door['kind']=='double_hinged' else '電梯滑門' if door['kind']=='sliding' else '向室內開啟')+'</title>'
    out+=f'<line x1="{a[0]}" y1="{a[1]}" x2="{b[0]}" y2="{b[1]}" stroke="#fffdf8" stroke-width="{.45*scale}"/>'
    if door['kind']=='sliding':
        for offset in (-.07,.07):
            aa=point([center[i]-t[i]*w/2+n[i]*offset for i in (0,1)]);bb=point([center[i]+t[i]*w/2+n[i]*offset for i in (0,1)])
            out+=f'<line x1="{aa[0]}" y1="{aa[1]}" x2="{bb[0]}" y2="{bb[1]}" stroke="#24675d" stroke-width="1.1" data-sliding-leaf="true"/>'
    for leaf in leaves(door):
        h=point(leaf['hinge']);c=point(leaf['closed']);p=point(leaf['open']);r=leaf['radius_m']*scale
        out+=f'<line x1="{h[0]}" y1="{h[1]}" x2="{p[0]}" y2="{p[1]}" stroke="#24675d" stroke-width="1.2" data-door-leaf="true"/>'
        out+=f'<path d="M{c[0]} {c[1]}A{r} {r} 0 0 {leaf["sweep"]} {p[0]} {p[1]}" fill="none" stroke="#24675d" stroke-width=".9" data-door-swing="true" data-radius-m="{leaf["radius_m"]}"/>'
    return out+'</g>'
