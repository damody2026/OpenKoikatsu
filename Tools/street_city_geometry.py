"""Shared demand sizing and core/envelope geometry; all values are metres."""
import math
from street_city_site import MALL_SIZE_M
from street_city_dimensions import CORRIDOR_M, CORE_BAND_M, hall_depth

def compact_core(bid):return bid.startswith('S') and bid[1:].isdigit()
def cores(bid):return [('A',-1)] if compact_core(bid) else [('A',-1),('B',1)]
def main_hall(bid,w,cy=0,z=0,h=3.6):
    from street_city_small_shop import PROFILES
    if bid in PROFILES:
        return dict(center_m=[0,cy,z],size_m=[w-.4,2,h],clear_width_m=CORRIDOR_M,kind='corridor')
    return dict(center_m=[3 if compact_core(bid) else 0,cy,z],size_m=[w-(6 if compact_core(bid) else 12),hall_depth(bid),h],clear_width_m=CORRIDOR_M,kind='public_concourse' if hall_depth(bid)>CORRIDOR_M else 'corridor')
def courtyard(footprint):
    w,d=footprint;half=max(15,w/2-CORE_BAND_M-21)
    return [-half,-d/4+16,half,d/2]
def wing_geometry(footprint):
    w,d=footprint;c=courtyard(footprint);cy=-d/4
    return dict(courtyard=c,cy=cy,edge=c[2]+3,corridor_x=c[2]+2,bridge_half=c[2]+1)
def design_room_dimensions(r):
    t=r['type'];w,d,h=r['size_m']
    if r.get('building')=='SP':return [w,d,h]
    if t in ('dorm','nap'):w,d=8,9
    elif t=='apartment':w,d=10,12
    elif t in ('inn','deluxe'):w,d=12,10
    elif t=='executive':w,d=14,12
    elif t=='suite':w,d=20,16
    return [w,d,h]
def demand_footprint(bid,rooms):
    from street_city_small_shop import PROFILES
    if bid in PROFILES:return PROFILES[bid]['size'][:]
    if bid=='SP':return [180,200]
    if bid=='E1':return [92,88]
    if bid=='S18':return [32,40]
    if bid=='S19':return [36,36]
    if compact_core(bid):raise ValueError('Missing dimensioned street-store typology: '+bid)
    if bid=='M1':return MALL_SIZE_M[:]
    width=0;depth=0
    for floor in sorted({r['floor'] for r in rooms}):
        rs=[r for r in rooms if r['floor']==floor];n=math.ceil(len(rs)/2)
        for row in (rs[:n],rs[n:]):
            if row:
                width=max(width,sum(design_room_dimensions(r)[0]+3.8 for r in row)+18.8)
                depth=max(depth,2*max(design_room_dimensions(r)[1] for r in row)+4.8)
    # Cores include usable stairs, WC, lifts and finite service rooms. Dorm
    # private showers remove the need for duplicated public shower blocks.
    min_depth=64 if bid in ('R1','R2') else 50
    return [math.ceil(width/2)*2,math.ceil(max(depth,min_depth)/2)*2]
