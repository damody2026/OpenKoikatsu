"""Deterministic quarter-turn bathroom placement with real use/swing clearances."""
import math

def overlap(a,b):return min(a[2],b[2])-max(a[0],b[0])>.015 and min(a[3],b[3])-max(a[1],b[1])>.015
def rect(x,y,w,d,yaw):
 if round(yaw/90)%2:w,d=d,w
 return [x-w/2,y-d/2,x+w/2,y+d/2]
def operator_rect(x,y,yaw,depth=.72):
 angle=math.radians(yaw);x+=math.sin(angle)*(depth/2+.35);y-=math.cos(angle)*(depth/2+.35)
 return rect(x,y,.5,.6,yaw)
def inside(rectangle,bounds):return all(rectangle[k]>=bounds[k]-1e-6 for k in [0,1]) and all(rectangle[k]<=bounds[k]+1e-6 for k in [2,3])

def repair_private_toilets(instances,assets,door_frames):
 changes=[]
 for fixture in instances:
  if fixture['asset_id']!='COMMON-WC-toilet':continue
  x,y,z=fixture['position_m'];rooms=[]
  for q in instances:
   a=assets[q['asset_id']]
   if a['role']!='internal_partition' or q['floor']!=fixture['floor']:continue
   if q['rotation_z_deg']%90:raise ValueError('Bathroom solver needs a quarter-turn room transform')
   w,d,_=a['size_m'];bounds=rect(*q['position_m'][:2],w,d,q['rotation_z_deg'])
   if bounds[0]<x<bounds[2] and bounds[1]<y<bounds[3]:rooms.append((w*d,q,bounds))
  if not rooms:continue
  _,room,bounds=min(rooms,key=lambda v:v[0]);clear=[bounds[0]+.2,bounds[1]+.2,bounds[2]-.2,bounds[3]-.2];obstacles=[]
  for q in instances:
   a=assets[q['asset_id']]
   if q is fixture or q['floor']!=fixture['floor'] or a['role'] not in ['sanitary_fixture','annex_furniture','service_fixture']:continue
   if q['position_m'][2]>z+1.8:continue
   size=a.get('size_m')
   if size:obstacles.append(rect(*q['position_m'][:2],size[0],size[1],q['rotation_z_deg']))
  for frame in door_frames.values():
   px,py,pz=frame['position_m']
   if abs(pz-z)>.01:continue
   yaw=math.radians(frame['yaw_deg']);width=frame['width_m'];hx=px-(width/2+.03)*math.cos(yaw);hy=py-(width/2+.03)*math.sin(yaw);ex=hx+(width+.06)*math.cos(yaw+math.pi/2);ey=hy+(width+.06)*math.sin(yaw+math.pi/2)
   obstacles.append([min(hx,ex)-.04,min(hy,ey)-.04,max(hx,ex)+.04,max(hy,ey)+.04])
  def valid(px,py,yaw):
   body=rect(px,py,.4,.72,yaw);standing=operator_rect(px,py,yaw)
   return inside(body,clear) and inside(standing,clear) and not any(overlap(body,o) or overlap(standing,o) for o in obstacles)
  old_yaw=fixture['rotation_z_deg']%360
  if valid(x,y,old_yaw):continue
  candidates=[]
  for yaw in [0,90,180,270]:
   if yaw in [0,180]:
    py=clear[3]-.36-.05 if yaw==0 else clear[1]+.36+.05
    values=[x]+[clear[0]+.25+i*.25 for i in range(max(0,math.floor((clear[2]-clear[0]-.5)/.25))+1)];points=[(px,py) for px in values]
   else:
    px=clear[0]+.36+.05 if yaw==90 else clear[2]-.36-.05
    values=[y]+[clear[1]+.25+i*.25 for i in range(max(0,math.floor((clear[3]-clear[1]-.5)/.25))+1)];points=[(px,py) for py in values]
   for px,py in points:
    if valid(px,py,yaw):
     angle=abs((yaw-old_yaw+180)%360-180);cost=math.hypot(px-x,py-y)+angle/180*.8;candidates.append((cost,px,py,yaw))
  assert candidates,('No usable toilet placement',fixture['id'],clear)
  _,px,py,yaw=min(candidates);changes.append(dict(instance=fixture['id'],reason='Keep toilet body and operator inside bathroom, clear other fixtures and inward door',old_position_m=fixture['position_m'][:],old_yaw_deg=old_yaw,position_m=[px,py,z],yaw_deg=yaw));fixture['position_m']=[px,py,z];fixture['rotation_z_deg']=yaw
 return changes
