"""Independent two-metre public access graph for planned open boundaries.

A pair of open zones is usable only when both facing walls have matching
openings. An opening into another isolated zone is not public access.
"""
from street_city_envelope import box
from street_city_geometry import main_hall
from street_city_space_policy import segments
from street_city_dimensions import CORRIDOR_M,WALL_M

MIN_WIDTH=CORRIDOR_M-.001 # published asset coordinates are rounded to millimetres
MAX_WALL_BAND=2*WALL_M+.00001

def rectangle_edges(rect):
    x0,y0,x1,y1=rect
    return [(0,y0,x0,x1,-1),(1,x1,y0,y1,1),(0,y1,x0,x1,1),(1,x0,y0,y1,-1)]

def aperture_edge(zone,opening):
    a,b=segments(zone['center_m'],zone['size_m'],zone['rotation_z_deg'],opening)
    along=0 if abs(a[0]-b[0])>abs(a[1]-b[1]) else 1
    across=1-along;position=(a[across]+b[across])/2
    sign=1 if position>zone['center_m'][across] else -1
    return along,position,min(a[along],b[along]),max(a[along],b[along]),sign

def connects(left,right):
    for axis,p,a,b,sign in left:
        for other,q,c,d,opposite in right:
            if axis!=other or sign!=-opposite:continue
            gap=(q-p)*sign
            if -.001<=gap<=MAX_WALL_BAND and min(b,d)-max(a,c)>=MIN_WIDTH:return True
    return False

def public_access_paths(building,floor):
    hall=main_hall(building['building'],building['footprint_m'][0],floor['corridor_y_m'])
    routes=[dict(id='main-route',center_m=hall['center_m'],size_m=hall['size_m'])]
    routes+=floor['branch_corridors']+floor['service_access_corridors']
    rectangles={};edges={}
    for i,r in enumerate(routes):
        identity=r.get('id',f'branch-{i}')
        rect=box(r.get('center_m',r.get('position_m')),r['size_m'])
        rectangles[identity]=rect;edges[identity]=rectangle_edges(rect)
    zones=floor['open_activity_zones']
    for zone in zones:edges[zone['id']]=[aperture_edge(zone,o) for o in zone['open_edges']]
    graph={key:set() for key in edges};keys=list(edges)
    for i,left in enumerate(keys):
        for right in keys[i+1:]:
            linked=connects(edges[left],edges[right])
            if left in rectangles and right in rectangles:
                a,b=rectangles[left],rectangles[right]
                linked|=all(min(a[k+2],b[k+2])-max(a[k],b[k])>=MIN_WIDTH for k in (0,1))
            if linked:graph[left].add(right);graph[right].add(left)
    paths={'main-route':['main-route']};pending=['main-route']
    for current in pending:
        for neighbor in sorted(graph[current]):
            if neighbor in paths:continue
            paths[neighbor]=paths[current]+[neighbor];pending.append(neighbor)
    return {z['id']:paths.get(z['id'],[]) for z in zones}
