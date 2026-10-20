"""Reusable metric department-store contract, independent of school geometry."""
import json,math,hashlib,copy,struct
from pathlib import Path
from street_city_building_context import ROOT,load_manifest
from street_city_parts import walls,slab,stairs,transform
from street_city_vertical_transport import distributed_lift_banks,crisscross_escalators
from street_city_small_shop import stair_slab_opening
from street_city_campus_recipes import p,shift,desk,shelf,cabinet,terminal,chair,recipe
OUT=ROOT/'output/street-social-city-production/mall-v1'
SOURCES=[dict(title='板橋大遠百官方樓層導覽',url='https://origin.feds.com.tw/tw/54/MallInfo?tab=floor-6F',use='美妝、服裝、家居、親子及餐飲的樓層分類；非尺寸測繪'),dict(title='遠百服務設施',url='https://esg.feds.com.tw/%E7%B6%A0%E8%89%B2%E6%9C%8D%E5%8B%99/',use='親子廁所、哺乳室、輪椅及友善電梯'),dict(title='BELLAVITA官方樓層導覽',url='https://www.bellavita.com.tw/cht/floorguide.php',use='開放零售及餐飲分層交叉參考')]
SOURCES += [dict(title='BELLAVITA官方建築介紹',url='https://www.bellavita.com.tw/cht/about.php',use='天然石材、立體石材分格與大片玻璃的立面組成；非複刻其樓高'),dict(title='BELLAVITA官方環境欣賞',url='https://www.bellavita.com.tw/cht/tour.php',use='商場公共區材質及入口層次參考')]
SOURCES += [dict(title='KONE escalator planning guide',url='https://distributors.kone.com/en/Images/KONE-Escalator-Planning-Guide_tcm90-100695.pdf',use='交錯連續動線、分離上下行及轉乘平台；遊戲尺寸另依本案推導'),dict(title='Schindler escalator planning guide',url='https://www.schindler.co.uk/content/dam/website/uk/docs/escalators-moving-walks/schindler-escalator-planning-guide.pdf/_jcr_content/renditions/original./schindler-escalator-planning-guide.pdf',use='交錯連續式雙向配置；非設備施工圖')]
THEMES=['美妝珠寶','女裝時尚','男裝機能','鞋包配件','居家生活','書店文創','數位家電','親子玩具','美食廣場','景觀餐飲']
COLUMN_X=[-62.5,-52.5,-42.5,-34.5,-22.5,-18.5,18.5,22.5,34.5,42.5,52.5,62.5]
COLUMN_Y=[-42,-32,-22,-12,12,22,32]
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def component_folders():
 base=ROOT/'output/street-social-city-production'
 from street_city_mall_stock import folders
 return {'panel':base/'campus-v1/components/unit-panel/remesh','cylinder':base/'campus-v1/components/unit-cylinder/remesh','toilet':base/'campus-v1/components/lod-toilet-compact/remesh','urinal':base/'campus-v1/components/lod-urinal/remesh','basin':base/'campus-v1/components/tripo-basin-economy/remesh','mannequin':base/'shop-street-components-v1/display-mannequin/remesh','flowers':base/'shop-street-components-v1/flowers/remesh',**folders()}
def estimate_triangles(g):
 from street_city_remesh_gate import verify
 counts={};folders=component_folders()
 for key in {'panel',*(q['primitive'] for q in g['parts']),*(q['asset'] for q in g['curves'])}:
  r=verify(folders[key]);data=Path(r['path']).read_bytes();sz=struct.unpack_from('<I',data,12)[0];doc=json.loads(data[20:20+sz]);n=[sum(doc['accessors'][p['indices']]['count']//3 for p in mesh['primitives']) for mesh in doc['meshes']];counts[key]=sum(n[node['mesh']] for node in doc['nodes'] if 'mesh' in node)
 result={f['floor']:24*counts['panel'] for f in g['contract']['floors']}
 for q in g['parts']:result[q['floor']]+=counts[q['primitive']]
 for q in g['curves']:result[q['floor']]+=counts[q['asset']]
 for q in g['doors']:result[q['floor']]+=(3+(0 if q.get('kind')=='lift' else 2 if q['width_m']>1.5 else 1))*counts['panel']
 return result
def prepare_signs():
 from street_city_mall_boutiques import prepare_signs as premium_signs
 premium_signs()
def contract():
 m=load_manifest();b=next(q for q in m['layouts'] if q.get('kind')=='building' and q['building']=='M1');assets={q['id']:q for q in m['assets']};rooms=[]
 columns=[[-63,-33],[-31,-2],[2,31],[33,63]]
 for f in range(1,11):
  for j in range(8):
   rid=f'M1-{f:02d}{j+1:02d}';a,c=columns[j%4];bb,dd=(17,36) if j<4 else (-44,-17)
   rooms.append(dict(id=rid,floor=f,name=assets[rid+'-shell']['name'].split('空房外殼')[0],bounds=[a,bb,c,dd],access='open_concourse',interactions=['逛店','試用或體驗','選購結帳'] if f<9 else ['排隊點餐','取餐','用餐']))
 world=b['world_position_m'][:];world[2]+=.06
 return dict(id='M1',footprint_m=b['footprint_m'],world_position_m=world,site_ground_datum_m=b['world_position_m'][2],finished_floor_offset_m=.06,north_access='closed retaining facade; measured exterior ground rises 1.17m in first metre',height_m=42,wall_m=.2,slab_m=.3,corridor_min_m=2,door_width_m=1.2,main_door_width_m=2.4,floors=[dict(floor=f,z_m=(f-1)*4.2,height_m=4.2,theme=THEMES[f-1]) for f in range(1,11)],rooms=rooms,atrium_m=[24,24],vertical_transport_revision=3,escalators=crisscross_escalators(),lift_banks=4,passenger_lifts_per_bank=3,stairs=[dict(id=f'{side}{end}',position=[x,y],yaw=0 if y>0 else 180) for side,x in [('W',-71.8),('E',71.8)] for end,y in [('N',40),('S',-40)]],lifts=distributed_lift_banks()+[dict(id=f'G{i+1}',bank='service',x=x,y=46,yaw=0,size=[4,5],car=[3.2,3.4],service=True) for i,x in enumerate([-10,10])],sources=SOURCES,dimension_basis='150x100m existing game-world parcel, finished floor +60mm above terrain datum; not a measured replica',limitations=['Static escalator steps are walkable; motor, shopping and elevator gameplay controllers not connected'])
def plan():
 c=contract();c['column_grid_m']=dict(x=COLUMN_X,y=COLUMN_Y,column_size=[.6,.6],atrium_transfer_beam_depth_m=1.2,design_basis='fictional metric layout, no structural certification');parts=[];curves=[];doors=[];routes=[];features=[]
 def put(no,name,size,pos,finish='wood',kind='furniture',yaw=0,primitive='panel'):
  parts.append(dict(floor=no,name=name,size=list(size),pos=list(pos),finish=finish,kind=kind,yaw=yaw,primitive=primitive))
 def pieces(no,name,ps,origin=(0,0,0),yaw=0,kind=None):
  for k,q in enumerate(ps):put(no,name+f'-{k}',q.get('size',q.get('size_m')),transform(q.get('pos',q.get('position_m')),origin,yaw),q.get('finish','paint'),kind or q.get('kind','furniture'),yaw+q.get('yaw',0),q.get('primitive','panel'))
 def curved(no,aid,name,pos,yaw=0):curves.append(dict(floor=no,asset=aid,name=name,pos=pos,yaw=yaw))
 def room(no,name,xy,size,z,doorx=0,yaw=0):
  x,y=xy;w,d=size;pieces(no,name,walls([w-.4,d-.4,3.9],[dict(side='south',local_position_m=[doorx,0,0],width_m=1.36,height_m=2.48)]),(x,y,z),yaw,kind='wall');doors.append(dict(floor=no,id=name,position_m=transform([doorx,-d/2+.1,0],[x,y,z],yaw),yaw_deg=yaw,width_m=1.2,height_m=2.4))
 for f in c['floors']:
  no=f['floor'];z=f['z_m'];h=f['height_m'];holes=[dict(center_m=[0,0],size_m=c['atrium_m'],kind='atrium')]
  g=dict(flight_count=2,clear_flight_width_m=1.5,landing_clear_depth_m=1.5,riser_count=26,riser_height_m=h/26,tread_depth_m=.28,flight_run_m=3.36)
  for core in c['stairs']:
   x,y=core['position'];co=dict(position_m=[x,y,z],walkable_geometry=g,size_m=[6,8,h]);hole=stair_slab_opening(co,g,g)
   if core['yaw']:
    hx,hy=hole['center_m'];hole['center_m']=[2*x-hx,2*y-hy]
   holes.append(hole)
  holes += [dict(center_m=[l['x'],l['y']],size_m=l['size'][::-1] if l['yaw'] else l['size'],kind='lift') for l in c['lifts']]
  pieces(no,'floor',slab([150,100,.3],holes if no>1 else [q for q in holes if q['kind']=='lift'],z=z-.3),kind='floor')
  if no==10:pieces(no,'roof',slab([149.6,99.6,.3],z=41.7,kind='ceiling'),kind='ceiling')
  cuts=[]
  if no==1:
   for side,pt,yaw in [('south',[0,-50,z],0),('west',[-75,0,z],-90),('east',[75,0,z],90)]:cuts.append(dict(side=side,local_position_m=[0,0,0],width_m=2.56,height_m=2.48));doors.append(dict(floor=no,id='entry-'+side,position_m=pt,yaw_deg=yaw,width_m=2.4,height_m=2.4))
   cuts.append(dict(side='east',local_position_m=[0,-25,0],width_m=2.56,height_m=2.48));doors.append(dict(floor=no,id='service-entry-east',position_m=[75,-25,z],yaw_deg=90,width_m=2.4,height_m=2.4,purpose='receiving and freight lift access'))
   for side,xy,dx,dy,delta in [('south',[0,-50],0,-1,-.07),('west',[-75,0],-1,0,-.07),('east',[75,0],1,0,-.05),('receiving',[75,-25],1,0,-.05)]:
    sx,sy=xy;end=[sx+dx*1.5,sy+dy*1.5,delta];put(no,'entrance-apron-'+side,[1.5 if dx else 3.6,3.6 if dx else 1.5,.12],[sx+dx*.75,sy+dy*.75,-.12],'tile','entrance_ramp');parts[-1].update(grade_start=[sx,sy,0],grade_end=end)
  for side in ['south','north']:
   for xx in [-60,-50,-40,-30,-20,-10,10,20,30,40,50,60]:
    cuts.append(dict(side=side,local_position_m=[xx,0,1],width_m=8.6,height_m=2.9));put(no,'facade-glass',[8.6,.025,2.9],[xx,-49.9 if side=='south' else 49.9,z+1],'glass','window')
  for side in ['west','east']:
   for yy in [-30,-15,15,30]:
    if no==1 and side=='east' and yy==-30:continue
    cuts.append(dict(side=side,local_position_m=[0,yy,1],width_m=8.6,height_m=2.9));put(no,'side-glass',[.025,8.6,2.9],[-74.9 if side=='west' else 74.9,yy,z+1],'glass','window')
  pieces(no,'envelope',walls([149.6,99.6,3.9],cuts,thickness=.12),(0,0,z),kind='wall')
  # Four independent escape cores. Upper landings remain part of the slab.
  for core in c['stairs']:
   x,y=core['position'];yaw=core['yaw'];name=f'F{no:02d}-stairs-{core["id"]}'
   pieces(no,name,walls([5.6,7.6,3.9],[dict(side='south',local_position_m=[0,0,0],width_m=1.36,height_m=2.48)]),(x,y,z),yaw,'wall')
   dp=transform([0,-3.9,0],[x,y,z],yaw);doors.append(dict(floor=no,id=name,position_m=dp,yaw_deg=yaw,width_m=1.2,height_m=2.4))
   if no<10:
    for flight in [0,1]:
     for edge in [-.68,.68]:
      put(no,name+'-stringer',[.08,math.hypot(3.36,2.1),.14],transform([(-.9 if flight==0 else .9)+edge,0,.6+flight*2.1],[x,y,z],yaw),'steel','stair_structure',yaw);parts[-1]['pitch_x']=(1 if flight==0 else -1)*math.degrees(math.atan2(2.1,3.36))
    for q in stairs([6,8,h],g):
     q=copy.deepcopy(q)
     if q['kind']=='stair_tread':q['position_m'][2]+=q['size_m'][2]-.08;q['size_m'][2]=.08
     elif q['position_m'][2]<0 or abs(q['position_m'][2]+q['size_m'][2]-h)<.001:continue
     pieces(no,name,[q],[x,y,z],yaw)
    for flight in [0,1]:
     for edge in [-.77,.77]:
      for k in range(12):
       xx=(-.9 if flight==0 else .9)+edge;yy=(1 if flight==0 else -1)*(-1.68+k*.28);zz=(flight*13+k)*h/26
       put(no,name+'-rail',[.035,.035,.95],transform([xx,yy,zz],[x,y,z],yaw),'steel','handrail')
       put(no,name+'-rail-cap',[.04,.3,.04],transform([xx,yy,zz+.93],[x,y,z],yaw),'steel','handrail')
   if no>1:
    for xx in [-1.69,1.69]:put(no,name+'-void-rail',[.04,4.86,1.1],transform([xx,.75,0],[x,y,z],yaw),'steel','handrail')
    for xx in [-2.225,2.225]:put(no,name+'-void-front-guard',[1.15,.04,1.1],transform([xx,-1.7,0],[x,y,z],yaw),'steel','void_guard',yaw)
    if no==10:put(no,name+'-unused-flight-guard',[1.5,.04,1.1],transform([-.9,-1.7,0],[x,y,z],yaw),'steel','void_guard',yaw)
  # Four distributed banks of three cars, plus independent freight shafts.
  for l in c['lifts']:
   x,y=l['x'],l['y'];w,d=l['size'];yaw=l['yaw'];name=f'F{no:02d}-lift-{l["id"]}'
   def lp(tag,size,pos,finish='steel',kind='lift_car'):
    put(no,name+'-'+tag,size,transform(pos,[x,y,z],yaw),finish,kind,yaw)
   room(no,name,[x,y],[w,d],z,yaw=yaw)
   doors[-1].update(kind='lift',shaft=l['id'],bank=l['bank'])
   if no>1:lp('closed-door',[1.196,.04,2.4],[0,-d/2+.1,0],kind='lift_barrier')
   else:
    cw,cd=l['car'];lp('car-floor',[cw,cd,.05],[0,0,-.05])
    lp('car-ceiling',[cw,cd,.05],[0,0,2.4])
    lp('car-back',[cw,.06,2.4],[0,cd/2-.03,0])
    for sign in [-1,1]:lp('car-side',[.06,cd,2.4],[sign*(cw/2-.03),0,0])
    for sign in [-1,1]:lp('car-front',[(cw-1.2)/2,.06,2.4],[sign*(cw+1.2)/4,-cd/2+.03,0])
   lp('threshold',[1.2,2,.05],[0,-d/2,-.05],kind='threshold')
  # Public toilets are separate enclosed wet areas, never scattered on the sales floor.
  for sign in [-1,1]:
   x=47*sign;room(no,f'F{no:02d}-WC-{sign}',[x,45],[26,9],z)
   for j in range(7 if sign==-1 else 4):
    xx=x-10+j*3;pieces(no,'cubicle',recipe('COMMON-WC-standard_cubicle',[1.4,1.9,2.4]),[xx,47.8,z],kind='wc_partition');curved(no,'toilet','toilet',[xx,48.0,z],0)
    put(no,'cubicle-open-door',[.035,1.196,2.2],[xx-.6,47.448,z+.15],'ivory','wc_door')
   if sign==1:
    for xx in [x+3,x+5,x+7]:curved(no,'urinal','urinal',[xx,48.8,z+.4],0)
   # Accessible stall opens onto a continuous 2m internal aisle.
   pieces(no,'accessible-stall',recipe('COMMON-WC-accessible_cubicle',[2.3,2.3,2.4]),[x+9.8,44,z],90,'wc_partition');curved(no,'toilet','accessible-toilet',[x+9.2,44,z],90)
   put(no,'accessible-open-door',[1.196,.035,2.2],[x+10.35,43.4,z+.15],'ivory','wc_door')
   for yy in [43.55,44.45]:
    put(no,'accessible-grab-bar',[.8,.035,.035],[x+9.35,yy,z+.7],'steel','grab_rail');put(no,'accessible-grab-support',[.035,.035,.7],[x+8.97,yy,z],'steel','grab_rail')
   for dx in [-8,-5,-2]:curved(no,'basin','washbasin',[x+dx,41.0,z+.45],180);put(no,'mirror',[.65,.04,.8],[x+dx,40.75,z+1.15],'mirror')
   routes.append(dict(floor=no,name=f'WC-{sign}-aisle',start=[x-10,45.5,z],end=[x+8,45.5,z],width=2))
  for x,label in [(-16,'親子哺乳室'),(16,'樓層管理與備品')]:
   room(no,f'F{no:02d}-{label}',[x,46],[5,5],z);pieces(no,label,recipe('COMMON-CITY-sofa',[1.8,.9,1]) if x<0 else cabinet(1.8,.6,1.8),[x,47.8,z]);pieces(no,label,desk(1.4,.65),[x-1,45.2,z])
  # Four paired crisscross banks. Both end decks have 2.4m wide
  # transverse transfer paths OUTSIDE the void; no turn over an open shaft.
  if no>1:
   for yy in [-14,14]:put(no,'atrium-transfer-beam',[28.6,.6,1.2],[0,yy,z-1.5],'paint','transfer_beam')
   for xx in [-14,14]:put(no,'atrium-transfer-beam',[.6,27.4,1.2],[xx,0,z-1.5],'paint','transfer_beam')
   for yy in [-11.95,11.95]:put(no,'atrium-guard',[24,.06,1.1],[0,yy,z],'glass','void_guard')
   lanes=sorted({e['y'] for e in c['escalators']})
   for xx in [-11.95,11.95]:
    cursor=-12
    for yy in lanes+[12.7]:
     stop=yy-.7
     if stop>cursor:put(no,'atrium-side-guard',[.06,stop-cursor,1.1],[xx,(cursor+stop)/2,z],'glass','void_guard')
     cursor=yy+.7
  for e in c['escalators']:
   run=e['run_m'];yy=e['y'];sgn=e['slope_sign'];name=e['id']
   if no==e['lower_floor']:
    put(no,name+'-truss',[math.hypot(run,h),1.35,.24],[0,yy,z+1.5],'steel','escalator_structure');parts[-1]['pitch_y']=-sgn*30
    for k in range(24):
     put(no,name+f'-step-{k:02d}',[run/24,1.2,.08],[sgn*(-run/2+(k+.5)*run/24),yy,z+(k+1)*h/24-.08],'steel','escalator_tread')
    for edge in [-.66,.66]:
     put(no,name+'-balustrade',[run,.06,1.05],[0,yy+edge,z+h/2],'glass','handrail');parts[-1]['slope_x']=sgn*h/run
     put(no,name+'-handrail',[run,.07,.05],[0,yy+edge,z+h/2+1.05],'black','handrail');parts[-1]['slope_x']=sgn*h/run
   if no in (e['lower_floor'],e['upper_floor']):
    upper=no==e['upper_floor'];sg=sgn if upper else -sgn;length=12-run/2
    put(no,name+('-arrival' if upper else '-departure'),[length,1.2,.1],[sg*(12+run/2)/2,yy,z-.1],'steel','escalator_landing')
    for side in [-1,1]:put(no,name+'-landing-guard',[length,.06,1.1],[sg*(12+run/2)/2,yy+side*.66,z],'glass','void_guard')
  for xx in [-13.4,13.4]:routes.append(dict(floor=no,name='escalator transfer deck',start=[xx,-10.5,z],end=[xx,10.5,z],width=2.4))
  # Open bays share the full rectangle. Furniture rows leave >2m between actual bodies.
  for shop in [r for r in c['rooms'] if r['floor']==no]:
   a,b,cc,dd=shop['bounds'];cx=(a+cc)/2;cy=(b+dd)/2;rid=shop['id'];tenant=int(rid[-2:])-1;front=b+2 if cy>0 else dd-2
   put(no,rid+'-finish',[(cc-a)-.2,(dd-b)-.2,.006],[cx,cy,z],'wood' if no in [5,6] else 'ivory' if no in [1,4,7,8] else 'fabric','floor_finish')
   pieces(no,rid+'-checkout',recipe('COMMON-CITY-checkout',[2.4,.8,1.1]),[a+3,front,z])
   spacing=5.5 if no>=9 else 6.0
   for i,xx in enumerate([a+4+k*spacing for k in range(7)]):
    for j,yy in enumerate([b+5+k*spacing for k in range(7)]):
     if yy>dd-2 or xx>cc-2:continue
     if no>=9 and ((cy>0 and yy>dd-8) or (cy<0 and yy<b+8)):continue
     if any(abs(xx-colx)<2 and abs(yy-coly)<2 for colx in COLUMN_X for coly in COLUMN_Y):continue
     q=[]
     if no in [2,3]:
      q=[p((1.8,.6,.04),(0,0,0),'steel'),p((.04,.04,1.7),(-.8,0,0),'steel'),p((.04,.04,1.7),(.8,0,0),'steel'),p((1.7,.04,.04),(0,0,1.7),'steel')]
      q += [p((.12,.32,.9),(-.5+k,0,.7),'fabric') for k in range(2)]
     elif no==6:
      q=shelf(1.8,.45,1.8,False)+[p((.7,.25,.3),(x,0,row*.44+.04),'book'+str((row+j)%3)) for row in range(4) for x in [-.43,.43]]
     elif no==8:
      q=desk(1.8,.8)+[p((.25,.2,.3),(-.6+k*.4,0,.75),'book'+str(k%3)) for k in range(4)]
     elif no>=9:q=desk(1.4,.8)+shift(chair(),x=-.45,y=.75)+shift(chair(),x=.45,y=.75)+shift(chair(),x=-.45,y=-.75,yaw=180)+shift(chair(),x=.45,y=-.75,yaw=180)
     elif no==5:
      if i%2==0:q=recipe('COMMON-CITY-sofa',[2,.9,1])
      else:q=desk(1.8,.8)+[p((.5,.3,.02),(0,0,.75),'ivory')]
     elif no==7:q=desk(2,.8)+shift(terminal(),x=-.5,z=.75)+shift(terminal(),x=.5,z=.75)
     else:
      q=cabinet(2.4,1.1,.9)+[p((2.4,1.1,.02),(0,0,1.2),'glass')]+[p((.025,1.1,.3),(x,0,.9),'glass') for x in [-1.18,1.18]]
      q += [p((.08,.08,.18),(-.6+k*.24,0,.92),'book'+str(k%3)) for k in range(6)]
     if no==5 and tenant==2:q=[p((2.2,2.4,.2),(0,0,.18)),p((2,2.2,.22),(0,0,.38),'fabric'),p((2.2,.08,.9),(0,1.16,0))]+[p((.5,.35,.1),(x,.65,.6),'ivory') for x in [-.55,.55]]+[p((.08,.08,.18),(x,y,0),'steel') for x in [-.9,.9] for y in [-1,1]]
     if no==5 and tenant==1:q=[p((.45,.45,.04),(0,0,0),'steel'),p((.035,.035,1.65),(0,0,.04),'steel'),p((.6,.6,.25),(0,0,1.69),'ivory'),p((.3,.3,.025),(0,0,1.68),'light')]
     if no==6 and tenant==6:q=recipe('COMMON-CITY-dining',[2.4,2.4,1.05])
     if no==7 and tenant==3:q=cabinet(.6,.5,1.4)+shift(cabinet(.6,.5,1.4),x=1.4)+[p((.4,.02,.4),(x,-.26,.5),'black') for x in [0,1.4]]
     if no in [1,5] and tenant==6:q=[p((1.8,.3,.06),(0,y,h)) for y,h in [(-.3,.5),(0,.8),(.3,1.1)]]+[p((.04,.04,1.16),(x,y,0),'steel') for x in [-.8,.8] for y in [-.3,.3]]
     if no==8 and tenant==6:q=cabinet(1,.9,.6)+[p((1,.9,.04),(0,0,2),'steel')]+[p((.04,.04,1.4),(x,y,.6),'steel') for x in [-.48,.48] for y in [-.43,.43]]+[p((.02,.85,1.3),(x,0,.65),'glass') for x in [-.48,.48]]+[p((.95,.02,1.3),(0,y,.65),'glass') for y in [-.43,.43]]+[p((.03,.03,.7),(0,0,1.2),'steel')]+[p((.2,.03,.03),(x,0,1.18),'steel') for x in [-.1,.1]]
     pieces(no,rid+f'-display-{i}-{j}',q,[xx,yy,z])
   if no in [1,5] and tenant==6:curved(no,'flowers',rid+'-flowers',[a+8.5,b+5,z+.86],0)
   if no==6 and tenant==6:pieces(no,rid+'-coffee-machine',cabinet(1.8,.75,.9)+[p((.6,.5,.5),(0,0,.9),'steel'),p((.5,.02,.2),(0,-.26,1.12),'black')]+[p((.03,.12,.1),(x,-.3,1.02),'steel') for x in [-.15,.15]],[cx,dd-2 if cy>0 else b+2,z],0 if cy>0 else 180)
   if no in [2,3]:curved(no,'mannequin',rid+'-mannequin',[cc-2.5,front,z],180 if cy>0 else 0)
   if no>=9:
    rear=dd-1.2 if cy>0 else b+1.2;kitchen_yaw=0 if cy>0 else 180
    room(no,rid+'-kitchen',[cx,dd-3 if cy>0 else b+3],[cc-a-2,5],z,yaw=kitchen_yaw)
    pieces(no,rid+'-cookline',recipe('COMMON-CITY-cookline',[4,.8,2.2]),[cx,rear,z],kitchen_yaw);pieces(no,rid+'-fridge',recipe('COMMON-CITY-fridge',[1.2,.8,2]),[a+3,rear,z],kitchen_yaw);curved(no,'basin',rid+'-kitchen-basin',[cc-3,rear,z+.45],kitchen_yaw)
   # Per-bay sign and lights; no floating architectural door at open storefront.
   features.append(dict(**shop,sign_position=[cx,front,z+2.5]))
   for xx in [a+5,cc-5]:
    for yy in [b+5,dd-5]:put(no,'sales-light',[2.4,.4,.04],[xx,yy,z+3.84],'light','light')
  if no==1:pieces(no,'information',recipe('COMMON-CITY-checkout',[4,1,1.1]),[-45,-5,z])
  # Structural column grid is outside the reserved public through-routes.
  for xx in COLUMN_X:
   for yy in COLUMN_Y:put(no,'column',[.6,.6,3.9],[xx,yy,z],'paint','column')
  for yy in [-15,15,38.5,-47]:routes.append(dict(floor=no,name='public cross aisle',start=[-63,yy,z],end=[63,yy,z],width=2))
  for xx in [-66,0,66]:
   for lo,hi in [(-47,-15),(15,40)]:routes.append(dict(floor=no,name='public longitudinal aisle',start=[xx,lo,z],end=[xx,hi,z],width=2))
 from street_city_mall_architecture import decorate
 decorate(parts,c)
 from street_city_mall_boutiques import decorate as decorate_boutiques
 decorate_boutiques(parts,c)
 from street_city_mall_stock import stock
 stock(parts,curves,c)
 c['boutique_design_revision']=1
 c['architecture_revision']=2
 c['wall_design']=dict(podium='honed limestone',upper='ivory ceramic rainscreen',feature='charcoal stone and bronze fins',glass='subdivided 8.6m glazed bays',public='travertine columns with charcoal bases and bronze capitals',wet='300x600mm porcelain',envelope='all facade details inside original150x100m')
 return dict(contract=c,parts=parts,curves=curves,doors=doors,routes=routes,shops=features)
