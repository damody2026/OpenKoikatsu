"""Dimensioned campus adapter. Official NTUST programmes, explicit fictional dimensions."""
import copy,json,math,hashlib,re
from functools import lru_cache
from street_city_building_context import ROOT,load_manifest
from street_city_parts import transform
IDS=('E1','EE','MA','T4','T2','IB','T1','RB','AD','LI','SC','IN','SP','D1','D2','D3','D4')
OUT=ROOT/'output/street-social-city-production/campus-v1'
UNITS={
 'E1':['機械工程系','材料科學與工程系'], 'EE':['電子工程系','電機工程系','光電工程研究所'],
 'MA':['工業管理系','企業管理系','資訊管理系'], 'T4':['人文社會學院','應用外語系','數位學習與教育研究所','師資培育中心','資訊工程系'],
 'T2':['電資學院','電資共同教學','設計工作室與攝影'], 'RB':['設計學院','建築系','設計系','電子計算機中心'],
 'IB':['國際事務處','研發處','語言中心','教學發展中心'], 'T1':['工程學院','自動化及控制研究所','通識教學'],
 'AD':['校長室','秘書室','教務處','總務處','人事室','主計室'], 'LI':['流通與諮詢','開架書庫','閱覽','討論與視聽'],
 'SC':['學生事務處','學生社團','學生餐廳','交誼與活動'], 'IN':['跨域研究','智慧製造與材料','營建與工程視覺化'],
 'SP':['游泳','羽球與籃球','桌球','健身與健康服務'],
 'D1':['女生宿舍'], 'D2':['男生宿舍'], 'D3':['研究生宿舍'], 'D4':['國際學生宿舍']}
SOURCES={
 'campus':dict(url='https://rpage.ntust.edu.tw/p/404-1000-88778.php?Lang=zh-tw',title='臺科大校內配置文字說明',status='read',facts=['官方院系與行政館舍關係；部分單位跨館使用'],dimensions='未提供實測建物長寬'),
 'design':dict(url='https://www.dt.ntust.edu.tw/index.php/webpage/view?id=8',title='臺科大設計系系所空間',status='read',facts=['RB四樓有系辦、電腦與PBL討論室；T4有學生工作室及模型工廠；T2有攝影及工作室'],dimensions='未提供各室實測尺寸'),
 'library':dict(url='https://library-r.ntust.edu.tw/p/412-1049-10184.php?Lang=zh-tw',title='臺科大圖書館樓層配置',status='read',facts=['前棟三層、後棟八層，流通諮詢、開架、閱覽及視聽各有用途'],dimensions='本案五層整合館是遊戲改編，並非原館復刻'),
 'materials':dict(url='https://mse.ntust.edu.tw/p/412-1019-249.php?Lang=zh-tw',title='臺科大材料系公用實驗室',status='search_primary',facts=['工程一館含材料測試、結構分析與3D列印等分區'],dimensions='公開系況PDF提到材料化學實驗室70平方米；不當作整館尺寸'),
 'language':dict(url='https://afl.ntust.edu.tw/p/405-1121-129332,c11880.php?Lang=zh-tw',title='臺科大應外系系所空間',status='search_primary',facts=['多媒體語言、口譯亭、閱讀與討論分區；普通教室有數位講桌及投影'],dimensions='未提供實測尺寸'),
 'electrical':dict(url='https://www.ceecs.ntust.edu.tw/',title='臺科大電資學院',status='search_primary',facts=['院辦位於第二教學大樓201室；會議及研討室另配置'],dimensions='未提供實測尺寸'),
 'dorm':dict(url='https://student.ntust.edu.tw/p/404-1053-149719.php?Lang=zh-tw',title='臺科大115學年度新生宿舍須知',status='search_primary_page_redirect',facts=['新生二舍男、三舍女主要為六人房；一樓有管理、交誼、自修與垃圾區','原校二舍15層、三舍11層；床墊約0.91×1.88m'],dimensions='本案四棟六層及四人房採既有遊戲地塊，明確標為改編'),
 'sport':dict(url='https://sport-r.ntust.edu.tw/var/file/69/1069/img/2634/730715831.pdf',title='臺科大運動場地開放管理辦法',status='search_primary',facts=['體育館二樓為羽球場，另有桌球、健身、綜合球場與游泳池'],dimensions='本案三座50m池及多層綜合館是玩家指定擴充，非原館規格'),
 'civil':dict(url='https://www.ct.ntust.edu.tw/ct/app/webroot/files/files/113_1/1131_0828_01.pdf',title='臺科大營建系教學試驗室與設備',status='search_primary',facts=['大地、岩石、結構、材料、測量及工程視覺創意各有專用空間'],dimensions='未提供實測建物長寬')}
REFS={bid:['campus'] for bid in IDS}
for key,bids in [('materials',['E1','IN']),('design',['RB','T4','T2','IB']),('language',['T4','IB']),('electrical',['EE','T2']),('library',['LI']),('sport',['SP']),('dorm',['D1','D2','D3','D4']),('civil',['IN'])]:
 for bid in bids:REFS[bid].append(key)

@lru_cache(maxsize=1)
def room_specs():return {r['id']:r for r in json.loads((ROOT/'Docs/StreetSocialCity/production/effective-city-plan.json').read_text('utf8'))['rooms']}

@lru_cache(maxsize=17)
def context(bid):
 assert bid in IDS; m=load_manifest();b=copy.deepcopy(next(q for q in m['layouts'] if q.get('kind')=='building' and q['building']==bid))
 rooms={q['room_id']:copy.deepcopy(q) for q in m['layouts'] if q.get('room_id') and q['building']==bid};owners={bid,*rooms};all_assets={q['id']:q for q in m['assets']};instances=[]
 for src in m['instances']:
  if src['owner'] not in owners:continue
  q=copy.deepcopy(src);room=rooms.get(q['owner'])
  if room:q['position_m']=transform(q['position_m'],room['position_m'],room['rotation_z_deg']);q['rotation_z_deg']+=room['rotation_z_deg']
  match=re.search(r'-F(\d\d)(?:-|$)',q['asset_id'])
  q['floor']=room['floor'] if room else int(match[1]) if match else max((f['floor'] for f in b['floors'] if f['z_m']<=q['position_m'][2]+.001),default=1);instances.append(q)
 used={q['asset_id'] for q in instances};assets={aid:copy.deepcopy(all_assets[aid]) for aid in used};frames={}
 for q in instances:
  a=assets[q['asset_id']];opening=a.get('opening',a.get('entrance_aperture'))
  if not opening or a['role'] not in ('room_shell','annex_room','support_room','public_restroom','internal_partition'):continue
  stem=a['id'].removesuffix('-shell').removesuffix('-partitions');did=stem+'-door'
  if did not in used:continue
  side=opening['side'];pos=transform(opening['local_position_m'],q['position_m'],q['rotation_z_deg'])
  frames[did]=dict(position_m=pos,yaw_deg=q['rotation_z_deg']+{'south':0,'north':180,'east':90,'west':-90}[side],width_m=opening.get('width_m',opening.get('clear_width_m')),height_m=opening.get('height_m',opening.get('clear_height_m')),floor=q['floor'])
 for e in b['entrances']:
  frames[e['id']]=dict(position_m=e['position_m'],yaw_deg={'S':0,'N':180,'E':90,'W':-90}[e['facade_side']],width_m=2.4,height_m=2.4,floor=1)
 # Programmes are adapted per building; identities and approved metric room bays stay stable.
 for room in rooms.values():
  n=int(room['room_id'][-2:])-1;floor=room['floor'];room['campus_units']=UNITS[bid]
  room['display_name']=assets[room['room_id']+'-shell']['name'].split('空房外殼')[0]
  spec=room_specs()[room['room_id']]
  room['interactions']=spec['interactions'];room['room_type']=spec['type'];room['sport_geometry']=copy.deepcopy(spec.get('sport_geometry',{}))
  if bid in ('T4','T2','RB','IB'):
   unit=({'T4':['人文社會共同教學','設計系跨館工作室','應用外語系','數位學習與師資培育','資訊工程系'], 'T2':['電資共同教學','電資學院','設計工作室與攝影','電資研究討論','智慧計算'], 'RB':['建築與設計展示','建築系','設計模型與原型','設計系','電子計算機中心'], 'IB':['語言中心','國際事務處','研究發展處','教學發展中心','國際會議與應用研究']}[bid])[floor-1]
   use='辦公室' if room['functional_programme']['kind']=='admin' else '實作與討論室' if room['functional_programme']['kind']=='research' else '教學工作室' if bid=='RB' or bid=='T4' and floor==2 or bid=='T2' and floor==3 else '教室'
   room['display_name']=unit+' '+use+' '+str(n+1)
   if bid=='RB' and floor==4 and use=='辦公室':room['display_name']='設計系辦公室（參考RB-405）'
  # The campus reference places student affairs in the student centre. Keep
  # its primary service office distinct from the shared administration desk.
  correction={
   'SC-0201':('學務處學生服務中心',['學生事務諮詢','獎助與宿舍服務','申請與表單辦理']),
   'AD-0201':('學務聯合服務窗口',['行政服務導引','學務案件轉介','表單補件']),
   'EE-0306':('資訊工程跨館專題支援辦公室',['跨館專題諮詢','研究會議','設備借用'])}.get(room['room_id'])
  if correction:
   room['display_name'],room['interactions']=correction;room['functional_programme']['kind']='admin';room['room_type']='administration'
 for q in instances:
  if q['asset_id']=='COMMON-WC-basin':q['rotation_z_deg']=(q['rotation_z_deg']+180)%360
  # Human/social sciences retain some real design studios, while language seats move to T4.
  if bid=='T4' and q['asset_id']=='COMMON-CITY-student_station' and q['floor']==3:q['asset_id']='COMMON-CITY-campus_language'
  if bid=='T2' and q['asset_id']=='COMMON-CITY-campus_language':q['asset_id']='COMMON-CITY-student_station'
  if bid=='RB' and q['asset_id']=='COMMON-CITY-electronics':q['asset_id']='COMMON-CITY-campus_drafting'
  if bid=='IB' and q['asset_id'] in ('COMMON-CITY-campus_material','COMMON-CITY-microscope'):q['asset_id']='COMMON-CITY-computer'
  if q['asset_id'] not in assets:assets[q['asset_id']]=copy.deepcopy(all_assets[q['asset_id']])
 # Cabinet backs face their own room boundary, not a hard-coded world axis.
 enclosures=[]
 for q in instances:
  a=assets[q['asset_id']]
  if a['role'] not in ('room_shell','annex_room','internal_partition','support_room'):continue
  w,d=a.get('clear_size_m',a['size_m'])[:2]
  if q['rotation_z_deg']%180:w,d=d,w
  x,y=q['position_m'][:2];enclosures.append((w*d,q['floor'],[x-w/2,y-d/2,x+w/2,y+d/2]))
 adjustments=[]
 for q in instances:
  if q['asset_id'].split('-')[-1] not in ('wardrobe','campus_wardrobe','locker','fridge'):continue
  x,y=q['position_m'][:2];containing=[e for e in enclosures if e[1]==q['floor'] and e[2][0]<x<e[2][2] and e[2][1]<y<e[2][3]]
  if not containing:continue
  aa,bb,cc,dd=min(containing)[2];distance,yaw=min([(x-aa,90),(cc-x,-90),(y-bb,180),(dd-y,0)])
  if distance<=1.0 and yaw%360!=q['rotation_z_deg']%360:adjustments.append(dict(instance=q['id'],reason='back toward own room wall',old_yaw=q['rotation_z_deg'],yaw=yaw));q['rotation_z_deg']=yaw
 if 'COMMON-WC-toilet' in assets:
  assets['COMMON-WC-toilet']['planned_clearance_size_m']=assets['COMMON-WC-toilet']['size_m'];assets['COMMON-WC-toilet']['size_m']=[.4,.72,.85]
 from street_city_fixture_placement import repair_private_toilets
 adjustments.extend(repair_private_toilets(instances,assets,frames))
 # Back-to-wall cabinets must also keep the actual door approach clear.
 for q in instances:
  a=assets[q['asset_id']]
  if a['id'].split('-')[-1] not in ('fridge','wardrobe','campus_wardrobe','locker'):continue
  w,d=a['size_m'][:2];qa=math.radians(q['rotation_z_deg']);ex=abs(math.cos(qa))*w/2+abs(math.sin(qa))*d/2;ey=abs(math.sin(qa))*w/2+abs(math.cos(qa))*d/2
  for door in frames.values():
   if door['floor']!=q['floor']:continue
   angle=math.radians(door['yaw_deg']);tx,ty=math.cos(angle),math.sin(angle);nx,ny=-ty,tx;dx=q['position_m'][0]-door['position_m'][0];dy=q['position_m'][1]-door['position_m'][1];along=dx*tx+dy*ty;normal=dx*nx+dy*ny;ae=abs(tx)*ex+abs(ty)*ey;ne=abs(nx)*ex+abs(ny)*ey;needed=door['width_m']/2+ae+.08
   if abs(normal)<ne+.5 and abs(along)<needed:
    delta=(needed if along>=0 else -needed)-along;old=q['position_m'][:];q['position_m'][0]+=tx*delta;q['position_m'][1]+=ty*delta;adjustments.append(dict(instance=q['id'],reason='cabinet clear of door and approach',old_position_m=old,position_m=q['position_m'][:]))
 return dict(id=bid,building=b,rooms=rooms,instances=instances,assets=assets,door_frames=frames,interior_adjustments=adjustments,units=UNITS[bid],references=[SOURCES[k] for k in REFS[bid]],dimension_basis='existing game-world metre contract; NTUST programme reference, not measured replica',out=OUT/'buildings'/bid)

def digest(c):return hashlib.sha256(json.dumps({k:v for k,v in c.items() if k!='out'},ensure_ascii=False,sort_keys=True).encode()).hexdigest()

def publish_research():
 OUT.mkdir(parents=True,exist_ok=True);data=dict(date='2026-10-16',priority='NTUST official sources',sources=SOURCES,buildings=[dict(id=b,units=UNITS[b],sources=REFS[b],footprint_m=context(b)['building']['footprint_m'],floors=len(context(b)['building']['floors']),dimension_basis=context(b)['dimension_basis']) for b in IDS])
 (OUT/'research.json').write_text(json.dumps(data,ensure_ascii=False,indent=2),'utf8')
 (OUT/'RESEARCH.md').write_text('# 台科大優先的校園參考查核\n\n公開資料決定院系關係、用途及設備；沒有實測尺寸的照片與平面不得冒充測量。遊戲保留既有17棟、89層地塊與尺寸，圖書館、宿舍及運動館為明確改編。\n\n'+'\n'.join(f'- [{q["title"]}]({q["url"]})：'+ '；'.join(q['facts'])+'。'+q['dimensions'] for q in SOURCES.values()),'utf8')
if __name__=='__main__':publish_research();print('17 buildings / 89 floors researched')
