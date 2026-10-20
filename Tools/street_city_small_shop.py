"""Data-driven small Taiwanese shop floor plates, in metres.

This profile is deliberately separate from the large-building half-floor solver.
The same cells generate the floor drawing, structure, equipment and checks.
"""
import math
from collections import Counter
from street_city_parts import walls,stairs,slab

PROFILES={
 'S01':dict(size=[12,18],name='咖啡書屋',colour=[.12,.34,.32],uses=['咖啡吧台與開放閱讀座席','開放共讀與選書區','店主住宅'],source='https://2booksdesign.com.tw/works/2020/black-house'),
 'S02':dict(size=[10,16],name='手搖飲店',colour=[.47,.58,.38],uses=['點餐、製作、取餐與短暫等候','茶料備製、包裝與員工工作區','員工休息與午睡'],source='https://knrndesign.co/project/rairai-tea'),
 'S03':dict(size=[10,18],name='烘焙甜點店',colour=[.66,.32,.19],uses=['開放麵包甜點展售與少量座席','烘焙製作工坊','店主住宅'],source='https://www.weiwustudio.com/新北flourishbakery'),
}
ORIGINAL_PROFILES=set(PROFILES)
for _key,_profile in PROFILES.items():_profile['open_floors']=[True,_key=='S01',False]
from street_city_compact_store import profiles as compact_profiles
PROFILES.update(compact_profiles())
WALL=.2
HEIGHTS=[3.8,3.8,3.4]
ACTIVITIES={
 'S01':[['點咖啡','選書','閱讀','同伴聊天','吧台兼職'],['共讀','使用筆電','找書','參加讀書會'],['睡覺','做飯','用餐','洗澡','居家閱讀']],
 'S02':[['點飲料','等候','取餐','觀看調飲','櫃台兼職'],['煮茶備料','調飲','原料盤點','外送包裝','清洗器具'],['午睡','休息','換衣','洗澡','使用茶水檯']],
 'S03':[['選購麵包','挑選甜點','結帳','內用','包裝兼職'],['揉麵整形','烘烤','冷卻','包裝','清洗器具'],['睡覺','做飯','用餐','洗澡','休息']],
}

def box(bounds,z=0,h=3.4):
 a,b,c,d=bounds
 return dict(position_m=[(a+c)/2,(b+d)/2,z],size_m=[c-a,d-b,h])

def overlap(a,b,tol=1e-7):
 return min(a[2],b[2])-max(a[0],b[0])>tol and min(a[3],b[3])-max(a[1],b[1])>tol

def equipment(name,rect,h=.9,acts=None):
 return dict(name=name,bounds_m=rect,height_m=h,interactions=acts or ['使用','整理'])

def design(bid,_profile=None):
 """A T-shaped two-metre route joins a front activity plate and rear core.

Core footprints include wall allocations. Corridors and front plate below are
finished-face rectangles. A protected 2m internal path is reserved in open floors.
"""
 if bid not in ORIGINAL_PROFILES and _profile is None:
  from street_city_compact_store import design_store
  return design_store(bid,PROFILES[bid],design)
 p=_profile or PROFILES[bid];w,d=p['size'];x0=-w/2;y0=-d/2
 def r(a,b,c,e):return [a+x0,b+y0,c+x0,e+y0]
 # Last 6.6m houses the stair, shaft and WC. A 2m cross hall is in front.
 core_front=d-6.8;hall_front=core_front-2.2;activity_back=hall_front-.2
 core_left=w-6.4;core_right=w-4
 floors=[];z=0
 for f,h in enumerate(HEIGHTS,1):
  cells=[dict(id=f'{bid}-F{f:02d}-activity',name=p['uses'][f-1],kind='activity',bounds_m=r(.2,.2,w-.2,activity_back),open=f==1 or (bid=='S01' and f==2)),
   dict(id=f'{bid}-F{f:02d}-cross',name='2m公共橫向走廊／電梯前廳',kind='corridor',bounds_m=r(.2,hall_front,w-.2,core_front),clear_width_m=2),
   dict(id=f'{bid}-F{f:02d}-access',name='2m核心通路',kind='corridor',bounds_m=r(core_left+.2,core_front,core_right-.2,d-.2),clear_width_m=2),
   dict(id=f'{bid}-F{f:02d}-stairsA',name='雙跑樓梯',kind='stairs',bounds_m=r(core_right,core_front,w-.2,d-.2)),
   dict(id=f'{bid}-F{f:02d}-restroomA',name='廁所、洗手與管道'+('／淋浴' if f==3 else ''),kind='restroom',bounds_m=r(.2,core_front,core_left,core_front+3.4)),
   dict(id=f'{bid}-EA1-F{f:02d}-shaft',name='電梯井',kind='lift',bounds_m=r(.2,core_front+3.4,core_left,d-.2))]
  activity=cells[0]['bounds_m'];ax0,ay0,ax1,ay1=activity
  # Entry is left of centre. Reserve a 2m route continuously to the cross hall.
  route=r(2.4,.2,4.4,core_front if cells[0]['open'] else activity_back)
  cross_route=None if cells[0]['open'] else r(.2,activity_back-2,4.4,activity_back)
  furn=[];private=[]
  if f==1:
   if bid=='S01':
    furn=[equipment('咖啡點餐吧台',r(.5,1,2.1,3.4)),equipment('備餐清洗檯',r(.5,4,2.1,6.5)),equipment('咖啡機與水槽',r(.6,6.8,2.0,7.6),1.5),equipment('低書架',r(5,6.8,11.2,7.4),1.6)]
    for x in (5.2,8.4):
     for y in (1,4):furn.append(equipment('四人閱讀桌椅組',r(x,y,x+2.4,y+2.0)))
   elif bid=='S02':
    furn=[equipment('點餐收銀檯',r(.5,.6,2.1,1.5)),equipment('取餐檯',r(.5,2,2.1,2.9)),equipment('封口調飲檯',r(5,1,8.9,1.8)),equipment('煮茶與洗滌檯',r(5,4.2,8.9,5)),equipment('製冰冷藏設備',r(8.5,2.2,9.3,3.8),1.8),equipment('短暫等候雙座',r(.5,4.2,2.1,5.1),1)]
   else:
    furn=[equipment('甜點冷藏展示櫃',r(.5,.7,2.1,3.1),1.3),equipment('麵包展示架',r(5,4,7.4,4.8),1.4),equipment('結帳包裝檯',r(7.9,4,9.3,5)),equipment('臨窗雙人座',r(5,.7,7.4,2.7)),equipment('備餐洗滌檯',r(5,6.7,9.3,7.5))]
  elif f==2:
   if bid=='S01':
    furn=[equipment('落地書架',r(.5,1,1.1,7.2),2)]
    for x in (5,8.4):
     for y in (1,4.2):furn.append(equipment('四人共讀桌椅組',r(x,y,x+2.4,y+2.2)))
   elif bid=='S02':
    furn=[equipment('茶料備製水槽檯',r(5,4.2,9.2,5)),equipment('包裝工作桌',r(5,1.3,7.6,2.3)),equipment('原料冷藏櫃',r(8.4,.6,9.3,2.5),1.9),equipment('茶料乾貨架',r(.5,.6,1.2,4.8),1.9)]
   else:
    furn=[equipment('攪拌與整形檯',r(5,1,7.4,2)),equipment('冷藏發酵櫃',r(8.5,1,9.3,3),1.9),equipment('烤箱與排煙設備',r(5,5.7,7.4,7.1),2),equipment('冷卻包裝檯',r(.5,1,2,3.2)),equipment('器具洗滌檯',r(.5,4.5,2.1,6.3)),equipment('出爐推車',r(8.5,5.7,9.3,6.7),1.7)]
  else:
   # Bedrooms flank the public access spine; front is open living/work space.
   split=3.2 if bid=='S02' else 4
   private=[dict(id=bid+'-F03-bedroom',name='獨立睡眠室',bounds_m=r(4.6,split,w-.2,activity_back),door=dict(center_m=r(4.6,split+1.1,4.6,split+1.1)[:2],side='west',width_m=1.2))]
   furn=[equipment('雙人床' if bid!='S02' else '午睡床',r(w-3.2,split+.5,w-1.0,split+2.6),1.1,['睡覺','午睡']),equipment('衣櫃',r(w-1,split+.5,w-.3,split+2),2),equipment('沙發',r(.5,.6,2.1,1.5),1),equipment('書桌椅組',r(.5,2.1,2.1,3.2)),equipment('小廚房與冰箱' if bid!='S02' else '員工茶水檯',r(5,.6,w-.5,1.4))]
  wc=cells[4]['bounds_m'];wa,wb,wc1,wd=wc
  fixtures=[equipment('馬桶',[wa+.3,wd-1.1,wa+1,wd-.3],.9),equipment('洗手檯',[wa+.3,wb+.3,wa+.9,wb+.8],.8)]
  if f==3:fixtures.append(equipment('淋浴間',[wa+.3,wb+1.1,wa+1.2,wb+2],2.2))
  # Door faces are explicit; no hardcoded core orientation in consumers.
  doors=[dict(id=cells[3]['id']+'-fire-door',center_m=r(w-2.1,core_front,w-2.1,core_front)[:2],outward_side='south',width_m=1.2,source='stairs',owner=cells[3]['id'],kind='hinged'),
   dict(id=cells[4]['id']+'-entrance',center_m=[wc1,(wb+wd)/2],outward_side='east',width_m=1.2,source='restroom',owner=cells[4]['id'],kind='hinged'),
   dict(id=cells[5]['id']+'-landing-door',center_m=r(core_left,core_front+4.9,core_left,core_front+4.9)[:2],outward_side='east',width_m=1.2,source='lift',owner=cells[5]['id'],kind='sliding')]
  for sub in private:doors.append(dict(id=sub['id']+'-door',center_m=sub['door']['center_m'],outward_side=sub['door']['side'],width_m=1.2,source='internal_partition',owner=sub['id'],kind='hinged'))
  if not cells[0]['open']:doors.append(dict(id=f'{bid}-{f:02d}01-door',center_m=r(1.4,activity_back,1.4,activity_back)[:2],outward_side='north',width_m=1.2,source='room',owner=f'{bid}-{f:02d}01',kind='hinged'))
  if f==1:doors.append(dict(id=bid+'-entry-front-S',center_m=r(3.4,0,3.4,0)[:2],outward_side='south',width_m=2.4,source='building_entrance',owner=bid,kind='double_hinged'))
  for door in doors:door.update(height_m=2.4,opens='inward',hinge_convention='left_when_viewed_from_outside',motion_mesh_verified=False)
  n=2*math.ceil(h/.17/2);run=(n//2-1)*.28
  stair_geometry=dict(flight_count=2,clear_flight_width_m=1.5,landing_clear_depth_m=1.5,riser_count=n,riser_height_m=h/n,tread_depth_m=.28,flight_run_m=run,minimum_headroom_m=2.2,door_clear_width_m=1.2,walkable=True,flights_width_in_core_m=3.3,flights_center_offset_x_m=.9,external_access_corridor_clear_width_m=2)
  floors.append(dict(floor=f,z_m=z,height_m=h,cells=cells,internal_route_bounds_m=route,internal_cross_route_bounds_m=cross_route,equipment=furn,wc_equipment=fixtures,private_rooms=private,doors=doors,stair_geometry=stair_geometry))
  z+=h
 return dict(schema='small_shop_v2',building=bid,name=p['name'],footprint_m=p['size'],height_m=z,wall_m=.2,source_url=p['source'],floors=floors)

def validate(design):
 """Independent complete-plate, fixture, clear-route and stair-fit checks."""
 errors=[];count=0;w,d=design['footprint_m'];outer=[-w/2+.2,-d/2+.2,w/2-.2,d/2-.2];aligned={}
 def check(ok,msg):
  nonlocal count
  count+=1
  if not ok:errors.append(msg)
 for f in design['floors']:
  tag=design['building']+f'/{f["floor"]}F';cells=f['cells']
  from street_city_envelope import subtract_rect
  missing=[outer]
  # Structural gaps up to 0.2m are actual partition wall allocations.
  for c in cells:
   a,b,e,g=c['bounds_m'];check(a>=outer[0]-1e-6 and b>=outer[1]-1e-6 and e<=outer[2]+1e-6 and g<=outer[3]+1e-6,tag+' cell outside floor')
   missing=[p for q in missing for p in subtract_rect(q,c['bounds_m'])]
   if c['kind'] in ('stairs','lift','restroom'):
    check(c['kind'] not in aligned or aligned[c['kind']]==c['bounds_m'],tag+' misaligned core');aligned[c['kind']]=c['bounds_m']
  for i,c in enumerate(cells):
   for other in cells[i+1:]:check(not overlap(c['bounds_m'],other['bounds_m']),tag+' overlapping floor cells')
  for q in missing:check(min(q[2]-q[0],q[3]-q[1])<=.200001,tag+' unassigned interior region')
  activity=cells[0]['bounds_m'];route=f['internal_route_bounds_m']
  check(abs(route[2]-route[0]-2)<1e-6,tag+' internal route below 2m')
  if not cells[0]['open']:
   check(all(route[k]>=activity[k]-1e-6 and route[k+2]<=activity[k+2]+1e-6 for k in (0,1)),tag+' enclosed floor route crosses its separating wall')
  for c in cells:
   if c['kind']=='corridor':check(min(c['bounds_m'][2]-c['bounds_m'][0],c['bounds_m'][3]-c['bounds_m'][1])>=2-1e-6,tag+' narrow public corridor')
  for i,item in enumerate(f['equipment']):
   a=item['bounds_m'];check(all(a[k]>=activity[k]-1e-6 and a[k+2]<=activity[k+2]+1e-6 for k in (0,1)),tag+' equipment outside activity '+item['name'])
   check(not overlap(a,route) and (not f.get('internal_cross_route_bounds_m') or not overlap(a,f['internal_cross_route_bounds_m'])),tag+' fixture blocks 2m route '+item['name'])
   for extra in f.get('additional_routes',[]):check(not overlap(a,extra),tag+' fixture blocks additional 2m route '+item['name'])
   for other in f['equipment'][i+1:]:check(not overlap(a,other['bounds_m']),tag+' fixtures overlap')
  for sub in f['private_rooms']:
   check(not overlap(sub['bounds_m'],route),tag+' bedroom blocks route')
  g=f['stair_geometry'];st=cells[3]['bounds_m'];check(g['flight_run_m']+3<=st[3]-st[1]-.4+1e-6,tag+' stair run/landings do not fit')
  check(g['riser_height_m']<=.17 and st[2]-st[0]-.4>=3.3-1e-6,tag+' stair width/riser invalid')
  shaft=cells[5]['bounds_m'];check(shaft[2]-shaft[0]>=2.6 and shaft[3]-shaft[1]>=3.2-1e-6,tag+' walk-in lift cannot fit')
  check(Counter(c['kind'] for c in cells)['restroom']==1,tag+' missing WC')
  for q in f['doors']:
   check(q['width_m']==(2.4 if q['source']=='building_entrance' else 1.2),tag+' wrong door width')
   if q['source']!='building_entrance':
    normal={'north':(0,1),'south':(0,-1),'east':(1,0),'west':(-1,0)}[q['outward_side']];point=[q['center_m'][k]+.2*normal[k] for k in (0,1)]
    targets=[c['bounds_m'] for c in cells if c['kind']=='corridor']+[route]+f.get('additional_routes',[])+([f['internal_cross_route_bounds_m']] if f.get('internal_cross_route_bounds_m') else [])
    along=1 if normal[0] else 0;across=1-along
    check(any(r[across]-1e-6<=point[across]<=r[across+2]+1e-6 and r[along]+q['width_m']/2-1e-6<=point[along]<=r[along+2]-q['width_m']/2+1e-6 for r in targets),tag+' door disconnected from clear route '+q['id'])
   from street_city_doors import leaves
   for leaf in leaves(q):
    pts=[leaf[k] for k in ('hinge','closed','open')];sweep=[min(p[k] for p in pts) for k in (0,1)]+[max(p[k] for p in pts) for k in (0,1)]
    for c in cells:
     if c['kind']=='corridor':check(not overlap(sweep,c['bounds_m']),tag+' door swing obstructs public corridor '+q['id'])
    for v in f['equipment']+f['wc_equipment']:check(not overlap(sweep,v['bounds_m']),tag+' door swing hits equipment '+v['name'])
  if f['floor']==3:
   check(bool(f['private_rooms']) and any('床' in v['name'] for v in f['equipment']),tag+' no private sleeping room/bed')
   check(any('淋浴' in v['name'] for v in f['wc_equipment']),tag+' no upper-floor shower')
   if design['building']!='S02':check(any('廚房' in v['name'] for v in f['equipment']),tag+' dwelling has no kitchen')
 return dict(status='passed' if not errors else 'failed',checks=count,errors=errors)

def stair_slab_opening(core, stair_geometry, incoming_stair_geometry=None):
 """Clear both flights and the half landing; retain the arrival-side slab.

 A flight-only opening leaves the half landing under a slab with ~1.6m
 headroom on a 3.8m storey. The far landing must be inside the slab void.
 """
 width=core['size_m'][0]-.4;depth=core['size_m'][1]-.4
 run=max(stair_geometry['flight_run_m'],(incoming_stair_geometry or stair_geometry)['flight_run_m']);far_landing=(depth-run)/2
 assert far_landing>=stair_geometry['landing_clear_depth_m']-1e-6
 centre=core['position_m'][:2];centre[1]+=far_landing/2
 return dict(kind='stair_flights',center_m=centre,size_m=[width,run+far_landing],
             landing_must_remain=True,includes_intermediate_landing=True,
             retained_floor_landing_side='south')

def compile_building(bid,world,reference,room_specs,asset,place,assets,frontages=(),display_name=None):
 """Adapter to the common manufacturing pipeline; no separate interior scale."""
 q=design(bid)
 if display_name:q['name']=display_name
 north=bool(frontages) and all(v['facade_side']=='N' for v in frontages)
 if north:
  for door in q['floors'][0]['doors']:
   if door['source']=='building_entrance':door['id']=bid+'-entry-front-N'
 result=validate(q)
 if result['errors']:raise ValueError(result['errors'])
 w,d=q['footprint_m'];floors=[];units=[]
 original_place=place;created=[]
 def place(*args,**kwargs):
  instance=original_place(*args,**kwargs);created.append(instance);return instance
 def structure(identity,label,parts):
  aid=asset(identity,label,[w,d,q['height_m']],'dimensioned_structure',bid)
  assets[-1]['control_components']=parts;place(aid,bid,[0,0,0])
 def fixture(identity,item,z):
  a,b,c,e=item['bounds_m'];aid=asset(identity,item['name'],[c-a,e-b,item['height_m']],'furniture','COMMON')
  place(aid,bid,[(a+c)/2,(b+e)/2,z])
 all_parts=[];entries=[]
 for f in q['floors']:
  no=f['floor'];z=f['z_m'];h=f['height_m'];rid=f'{bid}-{no:02d}01';activity=f['cells'][0];a,b,c,e=activity['bounds_m']
  spec=next(r for r in room_specs if r['floor']==no);spec['name']=activity['name'];spec['size_m']=[c-a,e-b,h-.4]
  spec['interactions']=ACTIVITIES[bid][no-1][:] if bid in ACTIVITIES else (spec['interactions'] if no==1 else ['使用設備','實作','整理','休息'] if no==2 else ['睡覺','做飯','用餐','洗澡','休息'])
  if bid not in ORIGINAL_PROFILES and no==3:
   spec.update(type='apartment',sleep='私人住宅；需住戶或受邀權限',access='住戶或受邀訪客')
  if no==2 and not activity['open']:spec['access']='員工或受邀體驗者'
  spec['required_props']=[v['name'] for v in f['equipment']];spec['small_shop_profile']='small_shop_v2'
  spec['enclosure_policy']=dict(mode='open' if activity['open'] else 'enclosed',reason='店型專用配置；開放販售與私密／生產隔間分開')
  if activity['open']:spec['open_plan']=dict(kind='connected_public_space',reason='開放式營業與閱覽空間',local_open_side='north')
  else:spec.pop('open_plan',None)
  origin=[(a+c)/2,(b+e)/2,z];size=[c-a,e-b,h-.4]
  programme=dict(id=rid,size_m=size,programme_kind='cafe' if bid=='S01' else 'tea' if bid=='S02' else 'bakery',equipment_layout=[],equipment_counts={},internal_rooms=[],internal_corridors=[])
  if f.get('programme_category'):programme['programme_kind']=f['programme_category'];programme['store_specific_design']=f['store_specific_design']
  for v in f['equipment']:
   bx=box(v['bounds_m']);programme['equipment_layout'].append(dict(equipment=v.get('equipment_key',v['name']),size_m=bx['size_m'][:2]+[v['height_m']],room_local_position_m=[bx['position_m'][0]-origin[0],bx['position_m'][1]-origin[1],0]))
  programme['equipment_counts']=dict(Counter(v['equipment'] for v in programme['equipment_layout']))
  programme['internal_corridors']=[dict(bounds_m=[max(r[0],a)-origin[0],max(r[1],b)-origin[1],min(r[2],c)-origin[0],min(r[3],e)-origin[1]],clear_width_m=2) for r in [f['internal_route_bounds_m']]+f.get('additional_routes',[])]
  for sub in f['private_rooms']:
   r=sub['bounds_m'];dr=sub['door']
   programme['internal_rooms'].append(dict(id=sub['id'],name=sub['name'],kind='kitchen' if 'kitchen' in sub['id'] else 'fitting_room' if 'fitting' in sub['id'] else 'bedroom',bounds_m=[r[0]-origin[0],r[1]-origin[1],r[2]-origin[0],r[3]-origin[1]],clear_size_m=[r[2]-r[0],r[3]-r[1],h-.4],geometry_owner_asset_id=bid+'-dimensioned-structure',door=dict(position_m=[dr['center_m'][0]-origin[0],dr['center_m'][1]-origin[1],0],width_m=1.2,height_m=2.4)))
  local_door=[-w/2+1.4-origin[0],e-origin[1],0]
  unit=dict(room_id=rid,building=bid,floor=no,position_m=origin,rotation_z_deg=0,clear_size_m=size,door=dict(width_m=1.2,height_m=2.4,local_position_m=local_door),dimensioned_programme=programme,functional_programme=dict(kind=programme['programme_kind'],unallocated_support_requirements=[],support_area_ratio=0),reference_ids=reference['source_ids'],enclosure_policy=spec['enclosure_policy'],interaction_bindings=[])
  unit['interaction_bindings']=[dict(id=anchor['id'],purpose=spec['interactions'][i%len(spec['interactions'])],status='needs_verified_furniture_socket',local_position_m=None) for i,anchor in enumerate(spec['anchors'])]
  if activity['open']:unit['open_plan']=spec['open_plan']
  units.append(unit)
  # Shell identity is retained for catalogue/access checks; all actual geometry
  # is owned once by the explicit structural assembly below.
  aid=asset(rid+'-shell',activity['name']+'控制邊界',[size[0]+.4,size[1]+.4,size[2]],'dimensioned_structure',rid)
  assets[-1]['control_components']=[]
  assets[-1]['geometry_owner_asset_id']=bid+'-dimensioned-structure'
  intervals=[(a,c)]
  for sub in f['private_rooms']:
   r=sub['bounds_m']
   if abs(r[3]-e)>1e-6:continue
   left,right=r[0]-.2,r[2]+.2;remaining=[]
   for x0,x1 in intervals:
    if right<=x0 or left>=x1:remaining.append((x0,x1))
    else:
     if left>x0:remaining.append((x0,left))
     if right<x1:remaining.append((right,x1))
   intervals=remaining
  openings=[dict(side='north',local_position_m=[(x0+x1)/2-origin[0],0,0],width_m=x1-x0,height_m=size[2],kind='open_public_edge_no_wall_no_door') for x0,x1 in intervals if x1-x0>=2-1e-6]
  if activity['open']:assets[-1]['open_display_edges']=openings;unit['open_plan']['open_edges']=openings
  place(aid,rid,[0,0,0])
  routes=[];cores=[]
  for cell in f['cells'][1:]:
   dims=box(cell['bounds_m'],z,h-.4);k=cell['kind']
   if k=='corridor':
    routes.append(dict(id=cell['id'],center_m=dims['position_m'],position_m=dims['position_m'],size_m=dims['size_m'],clear_width_m=2))
   else:
    core=dict(id=cell['id'],kind=k,core='A',**dims,clear_size_m=[dims['size_m'][0]-.4,dims['size_m'][1]-.4,h-.4],wall_thickness_m=.2)
    if k=='stairs':core.update(walkable_geometry=f['stair_geometry'],connects_floors=[no]+([no+1] if no<3 else []),ground_exit=no==1)
    if k=='lift':core.update(shaft_id=bid+'-EA1',car_clear_size_m=[1.8,2.4,2.4],serves_floors=[1,2,3],clear_door_width_m=1.2,clear_door_height_m=2.4,walk_in=True,landing_position_m=next(v['center_m']+[z] for v in f['doors'] if v['source']=='lift'),travel_mode='moving_platform_no_level_switch')
    if k=='restroom':core.update(accessible=True,fixtures=[],entrance_position_m=next(v['center_m']+[z] for v in f['doors'] if v['source']=='restroom'))
    cores.append(core)
   if k=='lift' and no==1:
    aid=asset(bid+'-EA1-car','可步入電梯轎廂',[2,2.6,2.5],'circulation',bid)
    assets[-1]['control_components']=walls([1.8,2.4,2.4],[dict(side='east',local_position_m=[.9,0,0],width_m=1.2,height_m=2.2)],.1)+slab([2,2.6,.1],z=-.1)
    assets[-1]['motion']=dict(type='vertical_lift',shaft_id=bid+'-EA1',stops_z_m=[0,3.8,7.6],initial_floor=1)
    assets[-1]['walkable_geometry']=dict(clear_size_m=[1.8,2.4,2.4],door_clear_width_m=1.2,door_clear_height_m=2.2)
    # Offset the cabin to align its opening with the shaft's east landing door.
    shaft_center=dims['position_m'];landing=core['landing_position_m']
    place(aid,bid,[shaft_center[0]+(dims['size_m'][0]-2)/2-.2,landing[1],0])
  # Exact external window apertures are declared before Grok sees the controls.
  apertures=[]
  for door in f['doors']:
   if door['source']=='building_entrance':apertures.append(dict(side=door['outward_side'],local_position_m=door['center_m']+[0],width_m=door['width_m'],height_m=2.4))
  windows=[]
  if no==1:
   windows=[dict(side='south',local_position_m=[-w/2+1.3,0,.8],width_m=1.6,height_m=1.7),dict(side='south',local_position_m=[w/2-2.6,0,.8],width_m=3.6,height_m=1.7)]
  else:
   windows=[dict(side='south',local_position_m=[xx,0,1.0],width_m=2.0,height_m=1.6) for xx in (-w/4,w/4)]
  windows+=[dict(side='north',local_position_m=[w/2-5.2,0,1.4],width_m=1.4,height_m=1.1)]
  f['exterior_openings']=apertures+windows
  for part in walls([w-.4,d-.4,h-.3],apertures+windows):part['position_m'][2]+=z;all_parts.append(part)
  # Thin glazing and frame members occupy declared window apertures. No
  # sealed wall is left behind a painted-on window; party walls stay blank.
  for window in windows:
   wx,_,sill=window['local_position_m'];ww,wh=window['width_m'],window['height_m'];wy=(-1 if window['side']=='south' else 1)*(d/2-.1)
   all_parts.append(dict(kind='window_glazing',position_m=[wx,wy,z+sill+.05],size_m=[ww-.1,.025,wh-.1]))
   for sign in (-1,1):
    all_parts.append(dict(kind='window_frame',position_m=[wx+sign*(ww/2-.025),wy,z+sill],size_m=[.05,.06,wh]))
    all_parts.append(dict(kind='window_frame',position_m=[wx,wy,z+sill+(wh-.05 if sign==1 else 0)],size_m=[ww,.06,.05]))
  all_parts.append(dict(kind='storefront_sign_band',position_m=[0,-d/2+.04,z+2.85],size_m=[w-.8,.08,.4]))
  # Cell walls stay inside their reserved envelopes. Shared wall faces adjacent
  # to the external wall are omitted to avoid a double exterior thickness.
  for cell in f['cells']:
   if cell['kind'] in ('activity','corridor'):continue
   bx=box(cell['bounds_m'],z,h-.3);cw,cd=bx['size_m'][:2];cx,cy,_=bx['position_m'];doors=[v for v in f['doors'] if v['owner']==cell['id']]
   cuts=[dict(side=v['outward_side'],local_position_m=[v['center_m'][0]-cx,v['center_m'][1]-cy,0],width_m=v['width_m'],height_m=v['height_m']) for v in doors]
   for part in walls([cw-.4,cd-.4,h-.3],cuts):
    part['position_m']=[part['position_m'][0]+cx,part['position_m'][1]+cy,part['position_m'][2]+z]
    bounds=cell['bounds_m'];side=part.get('side')
    if (side=='north' and abs(bounds[3]-(d/2-.2))<1e-6) or (side=='east' and abs(bounds[2]-(w/2-.2))<1e-6) or (side=='west' and abs(bounds[0]-(-w/2+.2))<1e-6):continue
    if part['kind']=='wall_corner':continue
    all_parts.append(part)
   if cell['kind']=='stairs':
    for part in stairs([cw,cd,h],f['stair_geometry']):part['position_m']=[part['position_m'][0]+cx,part['position_m'][1]+cy,part['position_m'][2]+z];all_parts.append(part)
  # Enclosed front floors keep a genuine door onto the hall; public front floors
  # have no separator. Bedrooms have their own complete walls and swing door.
  if not activity['open']:
   door=next(v for v in f['doors'] if v['owner']==rid);dx=door['center_m'][0]
   for left,right in ((a,dx-.6),(dx+.6,c)):
    if right>left:all_parts+=slab([right-left,.2,h-.3],z=z,kind='wall') if False else [dict(kind='wall',position_m=[(left+right)/2,e+.1,z],size_m=[right-left,.2,h-.3])]
   all_parts.append(dict(kind='wall',position_m=[dx,e+.1,z+2.4],size_m=[1.2,.2,h-.3-2.4]))
  for sub in f['private_rooms']:
   bx=box(sub['bounds_m'],z,h-.3);cx,cy,_=bx['position_m'];cw,cd=bx['size_m'][:2];dr=sub['door']
   for part in walls([cw,cd,h-.3],[dict(side=dr['side'],local_position_m=[dr['center_m'][0]-cx,dr['center_m'][1]-cy,0],width_m=1.2,height_m=2.4)]):
    side=part.get('side');bounds=sub['bounds_m']
    if (side=='east' and abs(bounds[2]-(w/2-.2))<1e-6) or (side=='north' and abs(bounds[3]-e)<1e-6 and not activity['open']) or part['kind']=='wall_corner':continue
    part['position_m']=[part['position_m'][0]+cx,part['position_m'][1]+cy,part['position_m'][2]+z];all_parts.append(part)
  holes=[]
  for core in cores:
   if core['kind']=='lift':holes.append(dict(kind='lift_shaft',center_m=core['position_m'][:2],size_m=core['size_m'][:2]))
   if core['kind']=='stairs' and no>1:holes.append(stair_slab_opening(core,f['stair_geometry'],q['floors'][no-2]['stair_geometry']))
  for part in slab([w,d,.3],holes,z=z-.3):all_parts.append(part)
  for i,v in enumerate(f['equipment']+f['wc_equipment']):fixture(f'{bid}-F{no:02d}-fixture{i+1}',v,z)
  for dr in f['doors']:
   aid=asset(dr['id'],dr['owner']+'可開門扇',[dr['width_m'],.08,2.4],'door',bid);assets[-1]['pivot']='hinge_bottom'
   if dr['source']=='building_entrance':assets[-1]['entrance_contract']=next(v for v in frontages if v['id']==dr['id'])
   place(aid,bid,dr['center_m']+[z])
  floor=dict(floor=no,z_m=z,height_m=h,corridor_width_m=2,public_concourse_depth_m=2,corridor_y_m=routes[0]['center_m'][1],rows=[[rid]],annex_rooms=[],core_annex_rooms=[],structural_bands=[],support_rooms=[],branch_corridors=routes[1:],service_access_corridors=[],vertical_circulation=cores,floor_openings=holes,door_schedule=f['doors'],open_public_spaces=[],open_activity_zones=[dict(id=rid,center_m=origin,size_m=size,rotation_z_deg=0,open_edges=openings,reason='開放式營業與閱覽空間')] if activity['open'] else [],small_shop_design=f)
  floors.append(floor)
  if no==1:entries=list(frontages)
 # Tile long wall strips through the common six-metre decomposition.
 parts=[]
 for part in all_parts:
  size=part['size_m'];axis=max(range(3),key=lambda k:size[k]);n=max(1,math.ceil(size[axis]/6))
  for i in range(n):
   v=dict(part,position_m=part['position_m'][:],size_m=size[:]);v['size_m'][axis]/=n
   v['position_m'][axis]+=(-size[axis]/2+size[axis]*(i+.5)/n) if axis<2 else size[axis]*i/n;parts.append(v)
 for part in slab([w,d,.3],z=q['height_m']):parts.append(dict(part,kind='roof'))
 structure(bid+'-dimensioned-structure','完整公尺制牆、窗、樓板與樓梯控制幾何',parts)
 building=dict(building=bid,kind='building',layout_profile='small_shop_v2',small_shop_design=q,reference_contract=reference,footprint_m=[w,d],height_m=q['height_m'],floors=floors,entrances=entries,continuous_world=True,world_position_m=[world['east'],world['north'],world.get('up',0)])
 if north:
  # Rotate the complete assembly, not only its map doorway. Room-local fixture
  # data stays local; its parent room receives the same 180 degree transform.
  for inst in created:
   if inst['owner']==bid:
    inst['position_m']=[-inst['position_m'][0],-inst['position_m'][1],inst['position_m'][2]];inst['rotation_z_deg']=(inst['rotation_z_deg']+180)%360
  for unit in units:
   unit['position_m']=[-unit['position_m'][0],-unit['position_m'][1],unit['position_m'][2]];unit['rotation_z_deg']=180
  rotate_floor_data(building['floors'])
  # Floors and design share object references. Rotate once and restore the
  # alias list rather than recursively visiting the same dictionaries twice.
  q['floors']=[fl['small_shop_design'] for fl in building['floors']]
 building['front_orientation']='north' if north else 'south'
 return building,units

def rotate_floor_data(value):
 """Half-turn of building-local metadata; preserves dimensions and handedness."""
 seen=set();opposite={'north':'south','south':'north','east':'west','west':'east'}
 def walk(v):
  if isinstance(v,(dict,list)):
   if id(v) in seen:return
   seen.add(id(v))
  if isinstance(v,list):
   for q in v:walk(q)
  elif isinstance(v,dict):
   for k,q in list(v.items()):
    if k=='open_edges':continue # zone-local openings rotate with their parent
    if k in ('bounds_m','internal_route_bounds_m','internal_cross_route_bounds_m') and q:v[k]=[-q[2],-q[3],-q[0],-q[1]]
    elif k in ('position_m','center_m','landing_position_m','entrance_position_m','local_position_m') and isinstance(q,list):v[k]=[-q[0],-q[1]]+q[2:]
    elif k=='additional_routes':v[k]=[[-r[2],-r[3],-r[0],-r[1]] for r in q]
    elif k in ('side','outward_side') and q in opposite:v[k]=opposite[q]
    elif k=='corridor_y_m':v[k]=-q
    elif k=='rotation_z_deg':v[k]=(q+180)%360
    else:walk(q)
 walk(value)

def draw(building,out,image_style=None):
 import html
 from street_city_doors import door_svg
 from street_city_space_policy import open_zone_svg
 from label_street_city_floor_metrics import floor_metrics_svg
 from street_city_io import write_text
 bid=building['building'];w,d=building['footprint_m'];scale=38;pw=w*scale;pd=d*scale;width=max(900,pw+445);panels=[]
 def text(x,y,s,size=14):return f'<text x="{x}" y="{y}" font-size="{size}" fill="#203c45">{html.escape(str(s))}</text>'
 for i,fl in enumerate(building['floors']):
  f=fl['small_shop_design'];top=160+i*(pd+160);ox=50+pw/2;oy=top+30+pd/2
  s=text(35,top,f'{bid} / {f["floor"]}F　層高 {f["height_m"]}m　Z={f["z_m"]:.1f}m',20)
  s+=f'<rect x="50" y="{top+30}" width="{pw}" height="{pd}" fill="#f5f3ed" stroke="#203c45"/>'
  def rect(r,fill,stroke='#697c79',dash=''):
   a,b,c,e=r
   return f'<rect x="{ox+a*scale}" y="{oy-e*scale}" width="{(c-a)*scale}" height="{(e-b)*scale}" fill="{fill}" stroke="{stroke}" stroke-width="1" {dash}/>'
  for cell in f['cells']:
   k=cell['kind'];r=cell['bounds_m'];col={'activity':'#dcece8','corridor':'#ecddc6','stairs':'#efd3ad','restroom':'#a9d0df','lift':'#ccc0df'}[k]
   if image_style and k=='activity':col='url(#image-derived-floor-finish)'
   s+=rect(r,col);a,b,c,e=r
   s+=text(ox+(a+.2)*scale,oy-(e-.55)*scale,cell['name'],12)
   s+=text(ox+(a+.2)*scale,oy-(e-1)*scale,f'{c-a:.1f}×{e-b:.1f}m'+('（含核心牆帶）' if k in ('stairs','restroom','lift') else '（淨）'),10)
   if k=='stairs':
    cx=(a+c)/2;cy=(b+e)/2;g=f['stair_geometry'];run=g['flight_run_m'];n=g['riser_count']//2
    for sign in (-1,1):
     for j in range(n):
      sy=cy-run/2+run*j/(n-1);xa=cx+sign*.9-.75;xb=xa+1.5
      s+=f'<line x1="{ox+xa*scale}" y1="{oy-sy*scale}" x2="{ox+xb*scale}" y2="{oy-sy*scale}" stroke="#a6814b"/>'
  s+=rect(f['internal_route_bounds_m'],'#f3e5cb','none','opacity=".75"')
  if f.get('internal_cross_route_bounds_m'):s+=rect(f['internal_cross_route_bounds_m'],'#f3e5cb','none','opacity=".75"')
  for extra in f.get('additional_routes',[]):s+=rect(extra,'#f3e5cb','none','opacity=".75"')
  for sub in f['private_rooms']:s+=rect(sub['bounds_m'],'#e9e3d5');s+=text(ox+(sub['bounds_m'][0]+.3)*scale,oy-(sub['bounds_m'][3]-.5)*scale,sub['name'],12)
  labels=[]
  for j,v in enumerate(f['equipment']+f['wc_equipment'],1):
   from street_city_store_symbols import symbol
   a,b,c,e=v['bounds_m'];glyph=symbol(v.get('equipment_key',v['name']),ox+(a+c)/2*scale,oy-(b+e)/2*scale,(c-a)*scale,(e-b)*scale,label=v['name'])
   if image_style:
    glyph=glyph.replace('#d8c6a6',image_style['palette']['wood']).replace('#9ab5a6',image_style['palette']['accent']).replace('#375b59',image_style['palette']['frame'])
   s+=glyph
   s+=text(ox+(a+.1)*scale,oy-(e-.35)*scale,str(j),11)
   labels.append(f'{j:02d} {v["name"]} {(c-a):.1f}×{(e-b):.1f}m')
  if not image_style:
   for zone in fl['open_activity_zones']:s+=open_zone_svg(zone,ox,oy,scale)
  for window in f.get('exterior_openings',[]):
   if window['local_position_m'][2]<=0:continue
   x=window['local_position_m'][0];y=(-1 if window['side']=='south' else 1)*(d/2-.1);ww=window['width_m']
   s+=rect([x-ww/2,y-.08,x+ww/2,y+.08],'#c6e4ea','#568b99')
  for door in f['doors']:s+=door_svg(door,ox,oy,scale)
  for j,label in enumerate(labels):s+=text(pw+115,top+65+j*24,label,12)
  s+=text(pw+115,top+65+len(labels)*24+30,'米色動線淨寬 2m，家具不得侵入。',12)
  s+=text(pw+115,top+65+len(labels)*24+54,'一般門 1.2m；首層正門 2.4m。',12)
  s+=text(pw+115,top+65+len(labels)*24+78,'外牆 0.2m；核心各面預留牆帶。',12)
  s+=floor_metrics_svg(bid,f['floor'],[w,d],scale,50,top+30)
  panels.append(s)
  header=text(35,38,f'{bid} {building["small_shop_design"]["name"]} / 室內外同一外框 {w}×{d}m',24)+text(35,74,'完整樓層、淨空、開放區與核心；非 Tripo 成品。北在上、入口朝'+('北' if building.get('front_orientation')=='north' else '南')+'。',14)
  finish=''
  if image_style:
   palette=image_style['palette'];motif=image_style.get('interior_motif','timber')
   ink=palette['wood'] if motif=='timber' else palette['frame'];pitch=19 if motif=='timber' else 38
   lines=f'<path d="M0 0H{pitch}M0 0V{pitch}" fill="none" stroke="{ink}" stroke-opacity=".15" stroke-width=".7"/>' if motif in ('timber','stone','metal') else ''
   finish=f'<defs><pattern id="image-derived-floor-finish" width="{pitch}" height="{pitch}" patternUnits="userSpaceOnUse"><rect width="{pitch}" height="{pitch}" fill="#fffdf8"/><rect width="{pitch}" height="{pitch}" fill="{palette["wall"]}" fill-opacity=".35"/>{lines}</pattern></defs>'
  shell=lambda body,height:f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}"><style>text{{font-family:Microsoft JhengHei,sans-serif}}</style>{finish}<rect width="100%" height="100%" fill="#fffdf8"/>{body}</svg>'
  write_text(out/f'{bid}-F{f["floor"]:02d}.svg',shell(header+f'<g transform="translate(0,{-i*(pd+160)})">{s}</g>',pd+300))
 write_text(out/f'{bid}.svg',shell(header+''.join(panels),len(panels)*(pd+160)+180))

def assert_building(building):
 result=validate(building['small_shop_design'])
 if result['errors']:raise AssertionError(result['errors'])
 assert building['footprint_m']==building['small_shop_design']['footprint_m']
 assert len(building['floors'])==3
 return result
