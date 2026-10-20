"""Exact modular control geometry. Boxes are dimension controls, never AI meshes.

All boxes use metre coordinates and a bottom-centre pivot. Subtraction occurs
before tiling, so door/shaft voids remain empty and edge tiles never overhang.
This module has no Blender or service dependencies and is independently tested.
"""
import math
import hashlib,json
from street_city_dimensions import DOOR_M
from street_city_envelope import subtract_rect

MAX_PART_M=6.0

def geometry_signature(asset):
    fields=('id','name','role','size_m','pivot','clear_size_m','opening','additional_wall_openings','open_display_edges','required_openings','walkable_geometry','required_submeshes','control_components')
    value={k:asset[k] for k in fields if k in asset}
    return hashlib.sha256(json.dumps(value,sort_keys=True,separators=(',',':'),ensure_ascii=False).encode()).hexdigest()

def tiled_rect(bounds,holes=(),limit=MAX_PART_M):
    pieces=[bounds]
    for hole in holes:pieces=[p for r in pieces for p in subtract_rect(r,hole)]
    for a,b,c,d in pieces:
        nx=max(1,math.ceil((c-a)/limit));ny=max(1,math.ceil((d-b)/limit))
        for ix in range(nx):
            for iy in range(ny):
                x0=a+(c-a)*ix/nx;x1=a+(c-a)*(ix+1)/nx
                y0=b+(d-b)*iy/ny;y1=b+(d-b)*(iy+1)/ny
                yield [x0,y0,x1,y1]

def slab(size,holes=(),z=0,kind='floor'):
    w,d,t=size
    cuts=[[q['center_m'][0]-q['size_m'][0]/2,q['center_m'][1]-q['size_m'][1]/2,
           q['center_m'][0]+q['size_m'][0]/2,q['center_m'][1]+q['size_m'][1]/2] for q in holes]
    return [dict(kind=kind,position_m=[(a+c)/2,(b+e)/2,z],size_m=[c-a,e-b,t]) for a,b,c,e in tiled_rect([-w/2,-d/2,w/2,d/2],cuts)]

def walls(clear_size,openings=(),thickness=.2):
    """Four independent wall runs; subtract true apertures in wall coordinates."""
    w,d,h=clear_size;parts=[]
    for side,length in [('south',w),('north',w),('west',d),('east',d)]:
        cuts=[]
        for q in openings:
            if q['side']!=side:continue
            p=q.get('local_position_m',q.get('center_local_m',[0,0,0]))
            along=p[0] if side in ('south','north') else p[1]
            width=q.get('width_m',q.get('clear_width_m'));height=q.get('height_m',q.get('clear_height_m'))
            sill=p[2] if len(p)>2 else 0
            if width is None or height is None:raise ValueError('Missing aperture dimensions')
            if along-width/2<-length/2-1e-5 or along+width/2>length/2+1e-5 or sill+height>h+1e-5:
                raise ValueError(f'Aperture outside {side} wall: {q}')
            cuts.append([along-width/2,sill,along+width/2,sill+height])
        for a,b,c,e in tiled_rect([-length/2,0,length/2,h],cuts):
            if side in ('south','north'):
                pos=[(a+c)/2,(-1 if side=='south' else 1)*(d/2+thickness/2),b];dim=[c-a,thickness,e-b]
            else:
                pos=[(-1 if side=='west' else 1)*(w/2+thickness/2),(a+c)/2,b];dim=[thickness,c-a,e-b]
            parts.append(dict(kind='wall',side=side,position_m=pos,size_m=dim))
    # Corner posts fill the .2m square not covered by the four clear-length runs.
    for x in (-1,1):
        for y in (-1,1):
            for a,b,c,e in tiled_rect([0,0,thickness,h]):
                parts.append(dict(kind='wall_corner',position_m=[x*(w/2+thickness/2),y*(d/2+thickness/2),b],size_m=[thickness,thickness,e-b]))
    return parts

def transform(position,origin,yaw):
    angle=math.radians(yaw);x,y,z=position
    return [origin[0]+x*math.cos(angle)-y*math.sin(angle),origin[1]+x*math.sin(angle)+y*math.cos(angle),origin[2]+z]

def stairs(size,geometry):
    """Separate solid risers/treads and landings. No enclosing opaque core box."""
    w,d,h=size;n=geometry['riser_count'];flights=geometry['flight_count'];per=n//flights
    rise=h/n;tread=geometry['tread_depth_m'];run=(per-1)*tread
    width=geometry['clear_flight_width_m'];landing=geometry['landing_clear_depth_m'];parts=[]
    for j in range(flights):
        sign=1 if j%2==0 else -1;x=-.9 if j%2==0 else .9;base=j*per*rise
        for k in range(per-1):
            parts.append(dict(kind='stair_tread',position_m=[x,sign*(-run/2+(k+.5)*tread),base],size_m=[width,tread,(k+1)*rise]))
        # Landing begins after the final riser and spans both flights.
        parts.append(dict(kind='stair_landing',position_m=[0,sign*(run/2+landing/2),base+per*rise-.15],size_m=[3.3,landing,.15]))
    parts.append(dict(kind='stair_landing',position_m=[0,-run/2-landing/2,-.15],size_m=[3.3,landing,.15]))
    return parts

def asset_geometry(a):
    """Returns (physical structural pieces, blocking reason for unhandled content).

    Furniture is intentionally not turned into approved solid cuboids. Its
    dimensions remain a render/control bound until a real generated mesh exists.
    """
    role=a['role'];size=a['size_m']
    if 'control_components' in a:return a['control_components'],None
    if role in ('floor_plate','roof_module'):
        return slab(size,a.get('required_openings',()),kind='roof' if role=='roof_module' else 'floor'),None
    if role in ('room_shell','annex_room','support_room','public_restroom','internal_partition'):
        clear=a.get('clear_size_m',size if role in ('annex_room','support_room') else [size[0]-.4,size[1]-.4,size[2]])
        openings=[]
        if role=='room_shell':
            openings=[a.get('opening',dict(side='south',local_position_m=[-clear[0]/2+1.5,-clear[1]/2,0],width_m=DOOR_M,height_m=2.4))]
            openings+=a.get('additional_wall_openings',[])
        elif role=='public_restroom':openings=[a['entrance_aperture']]
        else:openings=[a['opening']]
        openings+=a.get('open_display_edges',[])
        return walls(clear,openings),None
    if role=='circulation' and 'walkable_geometry' in a and 'riser_count' in a['walkable_geometry']:
        clear=[size[0]-.4,size[1]-.4,size[2]]
        return walls(clear,[dict(side='south',clear_width_m=DOOR_M,clear_height_m=2.4)])+stairs(size,a['walkable_geometry']), 'handrails_and_fire_door_require_separate_meshes'
    if role=='circulation' and '-shaft' in a['id']:
        return walls([size[0]-.4,size[1]-.4,size[2]],[dict(side='north',clear_width_m=DOOR_M,clear_height_m=2.4)]),'lift_landing_door_and_call_panel_pending'
    if role=='circulation' and a['id'].endswith('-car'):
        # Hollow cabin, bottom-centre origin, independent of the shaft walls.
        return walls([size[0]-.2,size[1]-.4,2.4],[dict(side='north',clear_width_m=DOOR_M,clear_height_m=2.4)],.1)+slab([size[0],size[1],.1],z=-.1),'lift_car_door_and_motion_pending'
    if role=='circulation':return [],None # corridor/lobby = navigable space, never an opaque volume mesh
    if role in ('facade_module','structural_band','outdoor_node'):
        return [],'assembly_requires_detailed_geometry; never_generate_as_solid_bound'
    return [],'requires_real_object_reference_and_separate_moving_parts'
