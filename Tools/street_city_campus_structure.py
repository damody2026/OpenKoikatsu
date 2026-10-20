"""Whole-building wall union, explicit apertures, slab voids and thin stairs."""
import copy,math,re
from collections import defaultdict
from street_city_parts import asset_geometry,walls,slab,stairs,transform
from street_city_small_shop import stair_slab_opening
from street_city_envelope import subtract_rect

def plan(C):
 b=C['building'];W,D=b['footprint_m'];floors=b['floors'];parts=[];doors=dict(C['door_frames']);windows=[]
 def put(q,origin=(0,0,0),yaw=0,owner='structure'):
  pos=transform(q['position_m'],origin,yaw);size=q['size_m'][:]
  if round(yaw)%180:size[0],size[1]=size[1],size[0]
  match=re.search(r'(?:^|-)F(\d\d)',owner)
  floor_hint=int(match[1]) if match else max((f['floor'] for f in floors if f['z_m']<=origin[2]+.001),default=1)
  parts.append(dict(pos=pos,size=size,kind='wall' if q['kind']=='wall_corner' else q['kind'],owner=owner,floor_hint=floor_hint))
 # Certify true slab holes against adjacent stair flights and intermediate landings.
 for f in floors:
  no=f['floor'];z=f['z_m'];h=f['height_m'];holes=[]
  original=next(C['assets'][i['asset_id']] for i in C['instances'] if i['floor']==no and C['assets'][i['asset_id']]['role']=='floor_plate')
  holes=[copy.deepcopy(x) for x in original.get('required_openings',[]) if x['kind']!='stair_flights']
  if no>1:
   prev=floors[no-2]
   for core in [x for x in f['vertical_circulation'] if x['kind']=='stairs']:
    pc=next(x for x in prev['vertical_circulation'] if x['kind']=='stairs' and x['core']==core['core']);holes.append(stair_slab_opening(core,core['walkable_geometry'],pc['walkable_geometry']))
  for q in slab([W,D,.3],holes,z=z-.3):put(q,owner=f'F{no:02d}-slab')
  if no==len(floors):
   for q in slab([W,D,.3],[x for x in holes if x['kind']=='open_courtyard'],z=z+h-.3,kind='ceiling'):put(q,owner=f'F{no:02d}-roof')
  exterior_cuts=[]
  for e in b['entrances'] if no==1 else []:
   side={'S':'south','N':'north','E':'east','W':'west'}[e['facade_side']];p=e['position_m'][:];p[2]=0;exterior_cuts.append(dict(side=side,local_position_m=p,width_m=2.56,height_m=2.48))
  for side,length in [('south',W),('north',W),('west',D),('east',D)]:
   step=8 if C['id']=='SP' else 4;ww=6 if C['id']=='SP' else 2;hh=3 if C['id']=='SP' else 1.6
   for index in range(1,int(length//step)):
    along=-length/2+index*step
    if any(c['side']==side and abs(c['local_position_m'][0 if side in ('south','north') else 1]-along)<ww/2+1.5 for c in exterior_cuts):continue
    pos=[along,0,1] if side in ('south','north') else [0,along,1];exterior_cuts.append(dict(side=side,local_position_m=pos,width_m=ww,height_m=hh));windows.append(dict(floor=no,side=side,along=along,z=z+1,width_m=ww,height_m=hh))
  for q in walls([W-.4,D-.4,h-.3],exterior_cuts):put(q,(0,0,z),owner='exterior')
 for inst in C['instances']:
  a=copy.deepcopy(C['assets'][inst['asset_id']]);role=a['role'];pos=inst['position_m'];yaw=inst['rotation_z_deg'];no=inst['floor'];f=floors[no-1];h=f['height_m'];aid=a['id']
  if role not in ('room_shell','annex_room','support_room','public_restroom','internal_partition','circulation','structural_band'):continue
  if role=='structural_band':put(dict(position_m=[0,0,0],size_m=a['size_m'],kind='wall'),pos,yaw,aid);continue
  if role=='circulation' and 'riser_count' in a.get('walkable_geometry',{}):
   g=a['walkable_geometry'];ps=walls([a['size_m'][0]-.4,a['size_m'][1]-.4,h-.3],[dict(side='south',local_position_m=[0,0,0],width_m=1.36,height_m=2.48)])
   doors[aid+'-door']=dict(position_m=transform([0,-a['size_m'][1]/2+.1,0],pos,yaw),yaw_deg=yaw,width_m=1.2,height_m=2.4,floor=no)
   if no<len(floors):
    flight_parts=stairs(a['size_m'],g)
    for q in flight_parts:
     if q['kind']=='stair_tread':q['position_m'][2]+=q['size_m'][2]-.08;q['size_m'][2]=.08
     if q['kind']=='stair_landing' and (q['position_m'][2]<0 or abs(q['position_m'][2]+q['size_m'][2]-h)<.001):continue
     put(q,pos,yaw,aid)
    # Rails follow each actual flight in the multi-flight sports core too.
    per=g['riser_count']//g['flight_count'];rise=h/g['riser_count'];run=g['flight_run_m']
    for j in range(g['flight_count']):
     xx=-.9 if j%2==0 else .9;sign=1 if j%2==0 else -1
     for edge in [-.77,.77]:
      for k in range(per):
       y=sign*(-run/2+k*g['tread_depth_m']);zz=j*per*rise+k*rise
       put(dict(position_m=[xx+edge,y,zz],size_m=[.035,.035,.95],kind='handrail'),pos,yaw,aid)
       if k<per-1:put(dict(position_m=[xx+edge,y,zz+.93],size_m=[.035,g['tread_depth_m']+.02,.035],kind='handrail'),pos,yaw,aid)
  elif role=='circulation' and '-shaft' in aid:
   ps=walls([a['size_m'][0]-.4,a['size_m'][1]-.4,h-.3],[dict(side='north',local_position_m=[0,0,0],width_m=1.36,height_m=2.48)])
   doors[aid+'-door']=dict(position_m=transform([0,a['size_m'][1]/2-.1,0],pos,yaw),yaw_deg=yaw+180,width_m=1.2,height_m=2.4,floor=no,kind='lift')
  elif role=='circulation':continue
  else:
   for key in ('opening','entrance_aperture'):
    if key in a:
     cut=a[key];wk='width_m' if 'width_m' in cut else 'clear_width_m';hk='height_m' if 'height_m' in cut else 'clear_height_m';cut[wk]+=.16;cut[hk]+=.08
   for cut in a.get('additional_wall_openings',[]):
    wk='width_m' if 'width_m' in cut else 'clear_width_m';hk='height_m' if 'height_m' in cut else 'clear_height_m';cut[wk]+=.16;cut[hk]+=.08
   ps,_=asset_geometry(a)
  for q in ps:
   wp=transform(q['position_m'],pos,yaw);sz=q['size_m'];wx,wy=(sz[1],sz[0]) if round(yaw)%180 else sz[:2]
   if abs(wp[0])+wx/2>=W/2-.199 or abs(wp[1])+wy/2>=D/2-.199:continue # outer wall is built once with windows
   put(q,pos,yaw,aid)
 # Union coplanar source wall surfaces; then apply ALL doors to BOTH sides.
 groups=defaultdict(list);other=[]
 for q in parts:
  if q['kind']!='wall':other.append(q);continue
  x,y,z=q['pos'];w,d,h=q['size'];axis=0 if w<d else 1;fixed=x if axis==0 else y;along=y if axis==0 else x;length=d if axis==0 else w
  groups[(axis,round(fixed,4),round(min(w,d),4))].append([along-length/2,z,along+length/2,z+h])
 for (axis,fixed,thick),rects in groups.items():
  cuts=[]
  for door in doors.values():
   x,y,z=door['position_m'];normal_axis=1 if round(door['yaw_deg'])%180==0 else 0
   if normal_axis!=axis or abs((x if axis==0 else y)-fixed)>.25:continue
   along=y if axis==0 else x;half=(door['width_m']+.16)/2;cuts.append([along-half,z,along+half,z+door['height_m']+.08])
  accepted=[]
  for r in sorted(rects,key=lambda r:-(r[2]-r[0])*(r[3]-r[1])):
   pieces=[r]
   for cut in cuts+accepted:pieces=[v for p in pieces for v in subtract_rect(p,cut)]
   accepted.extend(pieces)
  for aa,z0,bb,z1 in accepted:
   for f in floors:
    low=max(z0,f['z_m']);high=min(z1,f['z_m']+f['height_m'])
    if high-low<1e-6:continue
    pos=[fixed,(aa+bb)/2,low] if axis==0 else [(aa+bb)/2,fixed,low];size=[thick,bb-aa,high-low] if axis==0 else [bb-aa,thick,high-low]
    other.append(dict(pos=pos,size=size,kind='wall',owner='union-wall'))
 # Keep the explicitly U-shaped engineering courtyard open at every height.
 if C['id']=='E1':
  hole=next(h for q in C['assets'].values() if q['role']=='floor_plate' for h in q.get('required_openings',[]) if h['kind']=='open_courtyard');cx,cy=hole['center_m'];hw,hd=hole['size_m'];cut=[cx-hw/2,cy-hd/2,cx+hw/2,cy+hd/2];fixed=[]
  for q in other:
   x,y,z=q['pos'];w,d,h=q['size']
   for aa,bb,cc,dd in subtract_rect([x-w/2,y-d/2,x+w/2,y+d/2],cut):fixed.append(dict(q,pos=[(aa+cc)/2,(bb+dd)/2,z],size=[cc-aa,dd-bb,h]))
  other=fixed
 for q in other:
  z=q['pos'][2];q['floor']=q.get('floor_hint',max((f['floor'] for f in floors if f['z_m']<=z+.001),default=1))
 # Coalesce adjacent coplanar board cuts without changing any occupied volume
 # or aperture. Repeated slab subtraction otherwise leaves thousands of slivers.
 for axis in (0,1,2,0,1,2):
  grouped=defaultdict(list);rest=[]
  for q in other:
   if q['kind'] not in ('floor','ceiling','wall'):rest.append(q);continue
   x,y,z=q['pos'];w,d,h=q['size'];lo=[x-w/2,y-d/2,z];hi=[x+w/2,y+d/2,z+h];key=(q['floor'],q['kind'],*[(round(lo[k],5),round(hi[k],5)) for k in range(3) if k!=axis]);grouped[key].append((lo,hi,q))
  for rows in grouped.values():
   rows.sort(key=lambda row:row[0][axis]);merged=[]
   for lo,hi,q in rows:
    if merged and abs(merged[-1][1][axis]-lo[axis])<1e-5:merged[-1][1][axis]=hi[axis]
    else:merged.append((lo[:],hi[:],q))
   for lo,hi,q in merged:rest.append(dict(q,pos=[(lo[0]+hi[0])/2,(lo[1]+hi[1])/2,lo[2]],size=[hi[k]-lo[k] for k in range(3)]))
  other=rest
 return dict(parts=other,doors=[dict(id=k,**v) for k,v in doors.items()],windows=windows)
