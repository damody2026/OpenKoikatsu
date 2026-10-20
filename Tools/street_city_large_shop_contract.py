"""World-local transforms for the large inn/spa; never reuse small-shop cores."""
import copy,math,json,hashlib
from street_city_building_context import load_manifest,ROOT
from street_city_parts import transform

def contract_hash(C):
 def normalized(q):
  if isinstance(q,dict):return {k:normalized(v) for k,v in q.items()}
  if isinstance(q,list):return [normalized(v) for v in q]
  return round(q,6) if isinstance(q,float) else q
 data=dict(adapter_schema=4,building=C['building'],rooms=C['rooms'],instances=C['instances'],door_frames=C['door_frames'])
 return hashlib.sha256(json.dumps(normalized(data),sort_keys=True,ensure_ascii=False).encode()).hexdigest()

def context(bid):
 m=load_manifest();b=copy.deepcopy(next(q for q in m['layouts'] if q.get('kind')=='building' and q['building']==bid))
 assert bid in ('S18','S19') and not b.get('small_shop_design')
 rooms={q['room_id']:q for q in m['layouts'] if q.get('building')==bid and q.get('room_id')}
 owners={bid,*rooms};assets={q['id']:q for q in m['assets']};instances=[]
 for source in m['instances']:
  if source['owner'] not in owners:continue
  q=copy.deepcopy(source);owner=rooms.get(q['owner'])
  if owner:
   q['position_m']=transform(q['position_m'],owner['position_m'],owner['rotation_z_deg']);q['rotation_z_deg']+=owner['rotation_z_deg']
  z=q['position_m'][2];q['floor']=max((f['floor'] for f in b['floors'] if f['z_m']<=z+.001),default=1)
  instances.append(q)
 used={q['asset_id'] for q in instances}
 door_frames={}
 for q in instances:
  asset=assets[q['asset_id']];aid=asset['id'];opening=asset.get('opening')
  if not opening or asset['role'] not in ('room_shell','annex_room','support_room','internal_partition'):continue
  stem=aid.removesuffix('-shell').removesuffix('-partitions');doorid=stem+'-door'
  if doorid not in used:continue
  pos=transform(opening['local_position_m'],q['position_m'],q['rotation_z_deg']);side=opening['side']
  door_frames[doorid]=dict(position_m=pos,yaw_deg=q['rotation_z_deg']+{'south':0,'north':180,'east':90,'west':-90}[side],width_m=opening.get('width_m',opening.get('clear_width_m')),height_m=opening.get('height_m',opening.get('clear_height_m')))
 for e in b['entrances']:
  yaw={'S':0,'N':180,'E':90,'W':-90}[e['facade_side']];pos=e['position_m'][:];angle=math.radians(yaw);pos[0]-=.1*math.sin(angle);pos[1]+=.1*math.cos(angle)
  door_frames[e['id']]=dict(position_m=pos,yaw_deg=yaw,width_m=2.4,height_m=2.4)
 adjustments=[]
 for q in instances:
  if q['asset_id']=='COMMON-WC-basin':
   q['rotation_z_deg']=(q['rotation_z_deg']+180)%360;adjustments.append(dict(instance=q['id'],reason='Tripo basin faucet +Y is back; standing side faces restroom interior',yaw_deg=q['rotation_z_deg']))
  if q['asset_id']!='COMMON-CITY-basin':continue
  x,y,z=q['position_m'];angle=math.radians(q['rotation_z_deg']);front=(math.sin(angle),-math.cos(angle));zone=[x+front[0]*.6-.25,y+front[1]*.6-.3,x+front[0]*.6+.25,y+front[1]*.6+.3]
  for partition in instances:
   asset=assets[partition['asset_id']]
   if asset['role']!='internal_partition' or partition['floor']!=q['floor'] or partition['rotation_z_deg']%360:continue
   cx,cy,_=partition['position_m'];w,d,_=asset['size_m']
   if not (cx-w/2<x<cx+w/2 and cy-d/2<y<cy+d/2):continue
   stem=partition['asset_id'].removesuffix('-partitions');frame=door_frames.get(stem+'-door')
   if not frame:continue
   yaw=frame['yaw_deg'];hinge=transform([-.63,0,0],frame['position_m'],yaw);end=transform([1.26,0,0],hinge,yaw+90);leaf=[min(hinge[0],end[0])-.04,min(hinge[1],end[1])-.04,max(hinge[0],end[0])+.04,max(hinge[1],end[1])+.04]
   if min(zone[2],leaf[2])-max(zone[0],leaf[0])>.001 and min(zone[3],leaf[3])-max(zone[1],leaf[1])>.001:
    assert abs(front[0])<.001 and front[1]>.99,'Unsupported basin relocation direction'
    q['position_m'][1]=cy-d/2+.5;adjustments.append(dict(instance=q['id'],reason='Move basin against rear wall, clear inward door and standing zone',position_m=q['position_m'][:]))
 from street_city_fixture_placement import repair_private_toilets
 adjustments.extend(repair_private_toilets(instances,assets,door_frames))
 return dict(id=bid,building=b,rooms=rooms,instances=instances,assets={aid:copy.deepcopy(assets[aid]) for aid in used},door_frames=door_frames,interior_adjustments=adjustments,out=ROOT/f'output/street-social-city-production/{bid}-modular-components-v1/assembly-building')
