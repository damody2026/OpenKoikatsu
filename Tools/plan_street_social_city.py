from street_city_io import write_text,text_stream
"""Generate the reviewable SVG city plan and room-by-room Tripo briefs. No API calls."""
from pathlib import Path
import csv
import json
import re
import html
from collections import Counter
import xml.etree.ElementTree as ET
from street_city_site import EXTRA_SHOPS,OFFICES,WORLD_SIZE_M,MALL_SIZE_M

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'Docs/StreetSocialCity'
OUT.mkdir(parents=True, exist_ok=True)
(OUT / 'buildings').mkdir(exist_ok=True)
buildings, rooms = [], []
COLORS = {'教學':'#237c80', '校務':'#526dba', '住宿':'#947047', '商業':'#bd6757', '生活':'#8271a7', '戶外':'#56835b'}

# Dimensions are clear interior dimensions in metres, independent of schematic parcel cards.
TYPES = {
 'class': ([12,10,3.6], ['上課','個人讀書','雙人讀書','課後討論','使用筆電'], ['課桌椅','教學投影','書架','討論桌'], '無'),
 'lab': ([12,10,3.6], ['預約實作','研究課題','雙人協作','操作專業設備','整理成果'], ['工作檯','專業設備','安全收納','展示板'], '無'),
 'office': ([12,10,3.6], ['查詢課表','申請成績單','申請轉系','申請休學復學','報名入學考'], ['辦事櫃檯','辦公桌','等候椅','公告欄'], '無'),
 'admin': ([12,10,3.6], ['預約辦事','提交申請','查詢公告','等候交談'], ['服務桌','等候椅','檔案櫃','自助機'], '無'),
 'study': ([12,10,3.6], ['閱讀','雙人讀書','查資料','使用筆電','低聲交談'], ['書架','閱讀桌','插座','單人椅'], '無'),
 'club': ([12,10,3.6], ['社團練習','雙人合作','參加活動','聊天','整理器材'], ['活動桌','器材架','展示牆','座椅'], '無'),
 'sport': ([16,12,4.5], ['運動訓練','雙人練習','參加比賽','補水休息'], ['運動器材','防護地墊','飲水機','置物櫃'], '無'),
 'dorm': ([7,6,3.2], ['睡覺','午睡','換衣','使用手機','讀書','邀請交談','收納物品'], ['單人床×2','書桌×2','衣櫃×2','床頭燈','椅子'], '過夜／午睡；需住戶或受邀權限'),
 'apartment': ([10,8,3.2], ['睡覺','雙人休息','做飯','用餐','洗澡','洗衣','看電視','居家讀書'], ['雙人床','廚房','餐桌','沙發','衛浴','洗衣機'], '過夜／午睡；需租約或受邀權限'),
 'deluxe': ([16,12,3.8], ['入住睡覺','雙人休息','泡澡','客房用餐','看電視','使用迷你吧','陽台聊天'], ['特大床','獨立浴室','浴缸','雙人沙發','餐桌','迷你吧','陽台'], '過夜／午睡；需有效訂房'),
 'executive': ([18,14,3.8], ['入住睡覺','雙人休息','泡澡','客房用餐','客廳交談','遠端工作','觀景'], ['特大床','獨立客廳','辦公桌','雙人浴缸','餐桌','景觀窗'], '過夜／午睡；需有效訂房'),
 'suite': ([24,18,4.2], ['入住睡覺','雙人休息','私人泡湯','客房用餐','露台約會','鋼琴演奏','小型聚會'], ['主臥','獨立客廳','私人湯池','露台','鋼琴','六人餐桌','更衣室'], '過夜／午睡；需有效訂房'),
 'inn': ([10,8,3.2], ['睡覺','鋪被休息','喝茶','看庭院','雙人聊天','讀書'], ['榻榻米','日式床墊×2','茶桌','衣櫃','庭院窗'], '過夜／午睡；需訂房或住戶許可'),
 'nap': ([12,10,3.4], ['預約午睡','躺椅休息','放鬆','聽音樂','補水'], ['隔屏','可躺椅','閱讀燈','置物櫃','飲水機'], '短暫午睡；無過夜，依營業時間'),
 'food': ([12,10,3.8], ['點餐','用餐','雙人約會','聊天','外帶','兼職服務'], ['點餐檯','廚房','餐桌','座椅','菜單板'], '無'),
 'cafe': ([12,10,3.8], ['點飲料','雙人約會','聊天','讀書','使用筆電','兼職服務'], ['咖啡吧台','雙人桌','沙發','插座','書架'], '無'),
 'retail': ([12,10,3.8], ['瀏覽商品','購物','送禮挑選','雙人逛店','兼職整理'], ['商品架','收銀檯','展示櫃','試用桌'], '無'),
 'fashion': ([12,10,3.8], ['瀏覽服飾','試穿','換裝預覽','購物','搭配建議','送禮挑選'], ['服飾架','試衣間','全身鏡','收銀檯'], '無'),
 'game': ([12,10,3.8], ['單人遊戲','雙人對戰','挑戰高分','觀戰聊天','兌換獎勵'], ['遊戲設備','雙人座椅','積分屏','服務檯'], '無'),
 'spa': ([16,12,3.8], ['預約護理','泡湯','淋浴','更衣','躺椅休息','雙人聊天'], ['湯池或護理床','淋浴','更衣櫃','休息椅'], '無；休息椅僅坐休'),
 'health': ([12,10,3.6], ['掛號','諮詢','接受治療','領藥','等候休息'], ['診療桌','檢查床','藥櫃','等候椅'], '無；檢查床不作住宿'),
 'service': ([12,10,3.6], ['預約服務','辦理業務','付款','等候聊天'], ['服務檯','工作檯','等候椅','設備櫃'], '無'),
 'outdoor': ([24,18,0], ['散步','雙人交談','拍照','坐著休息','觀景'], ['長椅','照明','植栽','告示牌'], '無'),
 'exhibit': ([16,12,4.5], ['觀看展覽','閱讀導覽','雙人討論','拍照','參加導覽'], ['展示設施','導覽面板','觀賞座椅','展示照明'], '無'),
 'transit': ([16,12,4.0], ['購票','候車','查詢路線','搭乘交通','與同伴集合'], ['售票機','候車椅','路線圖','電子看板'], '無'),
}

def add(code, name, category, floors, per, x, y, kind, names=None, legacy=None):
    b = dict(id=code,name=name,category=category,floors=floors,rooms_per_floor=per,
             parcel=[x,y,260,145],footprint_m=[62,36] if category=='教學' else [80,60],
             legacy_place=legacy,entrance='南側接街道／步行連廊',status='planned',room_ids=[])
    buildings.append(b)
    for f in range(1,floors+1):
        for n in range(1,per+1):
            nm, typ = names(f,n) if names else (f'{name} {f}樓 {n:02d}室',kind)
            dim, acts, props, sleep = TYPES[typ]
            rid=f'{code}-{f:02d}{n:02d}'
            r=dict(id=rid,building=code,building_name=name,floor=f,slot=n,name=nm,type=typ,
                   size_m=dim,interactions=list(acts),required_props=list(props),sleep=sleep,
                   access='公共開放／依營業時間',status='planned',tripo_task_id=None)
            if typ in ('dorm','apartment','deluxe','executive','suite','inn'):
                r['access']='本人、同意邀請的訪客；飯店民宿另需付費入住'
            if typ=='lab':r['access']='修課、預約或研究授權；危險設備需訓練'
            b['room_ids'].append(rid); rooms.append(r)
    return b

source=(ROOT/'Source/OpenKoikatsu/OKStreetSocialCampus.h').read_text('utf-8-sig')
colleges=re.findall(r'\{TEXT\("([^"]+)"\), TEXT\("([^"]+)"\), TEXT\("(campus_[^"]+)"\), TEXT\("([^"]+)"\), TEXT\("([^"]+)"\)\}',source)
depts=re.findall(r'\{TEXT\("([^"]+)"\), TEXT\("([^"]+)"\), TEXT\("([^"]+)"\), (\d+), TEXT\("([^"]+)"\)\}',source)
offices=[]
for i,(cid,cn,legacy,code,bname) in enumerate(colleges):
    ds=[d for d in depts if d[2]==cid]
    office_names=[d[1]+'辦公室' for d in ds]
    if not ds:office_names=['通識教育中心','語言教學中心','體育教學中心','教學發展中心','共同教育委員會辦公室']
    while len(office_names)<5:office_names.append(cn+('院辦公室' if len(office_names)==len(ds) else '教學諮詢室'))
    def academic(f,n,ons=office_names,cn=cn,ds=ds):
        if n==6:return ons[f-1],'office' if f<=len(ds) else 'admin'
        topic=ds[(f-1)%len(ds)][4].split('|')[(n-1)%4] if ds else ['國文','英文','數學','自然科學','人文藝術'][n-1]
        return f'{topic}'+('實作教室' if n==5 else '教室'), 'lab' if n==5 else 'class'
    b=add(code,bname,'教學',5,6,40+(i%4)*300,260+(i//4)*195,'class',academic,legacy)
    b['college']=cn
    for f,d in enumerate(ds,1):offices.append(dict(department_id=d[0],department=d[1],building=code,floor=f,room=f'{code}-{f:02d}06'))

def listed(floors):
    return lambda f,n: floors[f-1][n-1] if isinstance(floors[f-1][n-1],tuple) else (floors[f-1][n-1],'admin')

admin=[
 ['綜合服務櫃檯','教務處註冊組','教務處課務組','招生中心','繳費與出納組','證件補發室','校務資訊室','訪客接待室'],
 ['學務處','住宿服務組','生活輔導組','助學金服務組','職涯發展中心','實習服務組','校安中心','失物招領室'],
 ['總務處','營繕組','採購組','環境安全組','交通管理組','資訊服務中心','校園網路支援室','檔案閱覽室'],
 ['國際事務處','交換學生服務室','研究發展處','產學合作處','校友中心','推廣教育中心','無障礙服務室','申訴諮詢室'],
 ['校長室','副校長室','秘書室','人事室','主計室','校務會議室','貴賓接待室','公共事務室']]
admin[0][0]=('綜合服務接待大廳','service')
admin[0][7]=('訪客接待大廳','service')
add('AD','學校行政中心','校務',5,8,40,650,'admin',listed(admin),'campus_ad')
lib=[[(n,'study') for n in row] for row in [
 ['借還書服務室','新書閱覽室','期刊室','報紙閱覽室','數位檢索室','館藏導覽室','無障礙閱讀室','親子閱讀室'],
 ['文學書庫','歷史書庫','語言書庫','哲學書庫','藝術書庫','社科書庫','讀書會室','雙人閱讀室'],
 ['工程書庫','電資書庫','管理書庫','自然科學書庫','建築書庫','研究資料室','論文閱覽室','資料庫教室'],
 ['小組研討室A','小組研討室B','小組研討室C','小組研討室D','個人研究室A','個人研究室B','影音欣賞室','錄音閱讀室'],
 ['安靜自習室A','安靜自習室B','考前共讀室','夜間自習室','景觀閱讀室','屋頂書房','閱覽休憩室','午睡預約室']]]
lib[4][7]=('午睡預約室','nap')
lib[0][0]=('借還書服務大廳','study')
add('LI','中央圖書館','校務',5,8,340,650,'study',listed(lib),'library')
student=[]
for row in [
 ['學生餐廳','早餐食堂','素食食堂','輕食廚房','校園咖啡廳','飲品吧','烘焙坊','餐飲實習室'],
 ['學生會辦公室','社團聯合辦公室','活動企畫室','志工服務室','學生媒體室','廣播錄音室','服務學習室','學生議事室'],
 ['音樂社練習室','吉他社練習室','舞蹈社練習室','戲劇社排練室','攝影社工作室','動漫社活動室','桌遊社活動室','棋藝社活動室'],
 ['美術社畫室','手作社工坊','烹飪社教室','電影社放映室','電競社訓練室','天文社活動室','園藝社活動室','讀書社討論室'],
 ['大型活動廳','小型演講廳','學生展覽室','同伴交誼室','安靜午睡室','學生心理諮詢室','團體諮詢室','休憩閱讀室']]:
    student.append([(n,'food' if len(student)==0 else 'club') for n in row])
student[0][4]=('校園咖啡廳','cafe');student[4][4]=('安靜午睡室','nap')
student[4][5]=('學生心理諮詢室','health');student[4][6]=('團體諮詢室','health')
add('SC','學生中心','校務',5,8,640,650,'club',listed(student),'cafeteria')
research_topics=['創客','機器人','智慧製造','人工智慧','生醫科技']
add('IN','跨域研究中心','校務',5,8,940,650,'lab',lambda f,n:(research_topics[f-1]+['設備操作室','協作實驗室','原型工作室','測試實驗室','成果展示室','研究討論室','計算工作室','安全訓練室'][n-1],'lab'))
sports=[[(n,'sport') for n in row] for row in [
 ['綜合球場A','綜合球場B','羽球室A','羽球室B','桌球室A','桌球室B','器材訓練室','體適能評估室'],
 ['重量訓練室','有氧訓練室','瑜伽室','皮拉提斯室','舞蹈教室','拳擊訓練室','攀岩訓練室','伸展室'],
 ['室內泳池','游泳教學池','水中復健池','救生訓練室','泳池更衣室A','泳池更衣室B','泳池淋浴室','泳池休息室'],
 ['武術教室','擊劍教室','射箭模擬室','室內單車室','體育戰術室','運動研究室','運動錄影室','賽事觀賞室'],
 ['運動醫務室','物理治療室','按摩恢復室','冰敷恢復室','運動員午睡室A','運動員午睡室B','健康餐飲室','運動交誼室']]]
for k in (0,1):sports[4][k]=(sports[4][k][0],'health')
for k in (4,5):sports[4][k]=(sports[4][k][0],'nap')
sports[4][6]=('健康餐飲室','food')
add('SP','運動健康館','生活',5,8,40,850,'sport',listed(sports),'playground')
for i,nm in enumerate(['女生宿舍A','男生宿舍B','研究生宿舍C','國際學生宿舍D']):
    def dorm_names(f,n):
        if f>1:return f'{f}樓寢室{n:02d}','dorm'
        return [('住宿管理室','admin'),('訪客交誼廳','club'),('共用廚房','food'),('共用餐廳','food'),('自習室','study'),('洗衣服務室','service'),('包裹領取室','service'),('器材借用室','service'),('訪客住宿房A','dorm'),('訪客住宿房B','dorm')][n-1]
    b=add(f'D{i+1}',nm,'住宿',6,10,40+i*300,65,'dorm',dorm_names,['girls_dorm','boys_dorm',None,None][i]);b['footprint_m']=[62,30]

mall_rows=[
 ['香水專櫃','彩妝專櫃','保養品專櫃','美妝工具店','珠寶專櫃','手錶專櫃','花禮店','精品禮品店'],
 ['休閒女裝','都會女裝','設計師女裝','洋裝店','運動女裝','內著專門店','訂製服工作室','女裝配件店'],
 ['休閒男裝','商務男裝','街頭服飾','運動男裝','訂製西服店','男士配件店','牛仔服飾店','機能服飾店'],
 ['運動鞋店','皮鞋店','女鞋店','手袋店','旅行箱店','皮革工作室','眼鏡店','飾品店'],
 ['家具展示店','燈飾店','寢具店','廚具店','生活雜貨店','香氛店','植栽店','家飾店'],
 ['綜合書店','漫畫店','學術書店','文具店','手帳店','美術用品店','閱讀咖啡館','桌遊專賣店'],
 ['手機店','電腦店','相機店','音響店','家電店','遊戲主機店','智慧家居店','電器維修店'],
 ['模型玩具店','積木店','收藏公仔店','益智玩具店','卡牌店','遊戲體驗館','抓娃娃店','親子手作店'],
 ['拉麵店','咖哩店','台式食堂','韓式食堂','義大利麵店','素食餐廳','甜點店','茶飲店'],
 ['景觀法式餐廳','景觀日式餐廳','景觀中式餐廳','牛排館','地中海餐廳','高空咖啡館','景觀酒吧','慶祝包廂餐廳']]
def mall_names(f,n):
    typ='fashion' if f in (2,3) else 'retail'
    if f>=9:typ='food'
    if (f,n) in ((6,7),(10,6)):typ='cafe'
    if (f,n)==(7,8):typ='service'
    if f==8 and n in (6,7):typ='game'
    return mall_rows[f-1][n-1],typ
mall=add('M1','大學城百貨','商業',10,8,940,850,'retail',mall_names,'department');mall['footprint_m']=[96,60]
shop_rows=[
 ('咖啡書屋','cafe','共讀包廂','study','店主套房','apartment','cafe'),
 ('手搖飲店','retail','飲品教室','food','員工休息室','nap','drinkshop'),
 ('烘焙甜點店','food','烘焙教室','food','店主套房','apartment','snack'),
 ('家庭餐館','food','聚餐包廂','food','主廚套房','apartment','restaurant'),
 ('水果店','retail','果醬教室','food','店主套房','apartment','fruitshop'),
 ('便利商店','retail','物流整理室','service','員工休息室','nap','conveni'),
 ('街角書局','retail','讀書會室','study','漫畫閱讀室','study','bookstore'),
 ('寵物用品店','retail','寵物照護室','service','店主套房','apartment','petshop'),
 ('花藝店','retail','花藝教室','club','店主套房','apartment',None),
 ('服飾選物店','fashion','搭配工作室','fashion','店主套房','apartment',None),
 ('理髮美容店','service','美甲工作室','service','員工休息室','nap',None),
 ('唱片樂器店','retail','樂器練習室','club','錄音工作室','club',None),
 ('街角遊戲店','game','桌遊包廂','game','電競練習室','game',None),
 ('相機沖印店','retail','攝影工作室','club','店主套房','apartment',None),
 ('自行車店','retail','修車工作室','service','店主套房','apartment',None),
 ('藥妝店','retail','健康諮詢室','health','員工休息室','nap',None),
 ('洗衣店','service','衣物修補室','service','店主套房','apartment',None),
 ('日式民宿','service','庭院和室A','inn','庭院和室B','inn','minshuku'),
 ('溫泉會館','spa','獨立湯屋','spa','會館休憩室','nap','onsen'),
 ('星運彩券行','retail','競技轉播室','club','店主套房','apartment','lottery')]
original_shop_rows=shop_rows[:]
for name,template in EXTRA_SHOPS:
    original=original_shop_rows[int(template[1:])-1]
    shop_rows.append((name,*original[1:6],None))
for i,(nm,t,a,at,b,bt,legacy) in enumerate(shop_rows):
    sb=add(f'S{i+1:02d}',nm,'商業',3,1,40+(i%5)*170,1050+(i//5)*105,t,
           lambda f,n,nm=nm,t=t,a=a,at=at,b=b,bt=bt:[(nm,t),(a,at),(b,bt)][f-1],legacy)
    sb['parcel'][2:]=[152,88];sb['footprint_m']=[16,14]
    if sb['id']=='S18':
        reception=next(r for r in rooms if r['building']=='S18' and r['floor']==1)
        reception['name']='民宿接待交誼大廳'
        reception['interactions']=['辦理入住','退房','等候','交誼聊天','喝茶','查詢旅遊資訊']
    if i>=20:
        template=EXTRA_SHOPS[i-20][1];sb['programme_template_building']=template
        for r in rooms:
            if r['building']==sb['id']:r['programme_template_building']=template
for oi,(bid,name) in enumerate(OFFICES):
    def office_names(f,n):
        return ([('接待與訪客服務','admin'),('共享辦公室','admin'),('商務會議室','admin'),('茶水休憩室','food')] if f==1 else [(f'{f}樓企業辦公室A','admin'),(f'{f}樓企業辦公室B','admin'),(f'{f}樓會議洽談室','admin'),(f'{f}樓員工茶水休憩室','food')])[n-1]
    add(bid,name,'商業',6,4,1600+oi*170,850,'admin',office_names)
for i,nm in enumerate(['青年租屋公寓','河景家庭公寓']):
    b=add(f'R{i+1}',nm,'住宿',5,8,340+i*300,850,'apartment',lambda f,n:(f'{f}樓住家{n:02d}','apartment'));b['footprint_m']=[62,42]
hotel_names=[
 [('挑高接待大廳','service'),('禮賓服務室','service'),('全日餐廳','food'),('大堂咖啡廳','cafe'),('婚宴廳','food'),('私人宴會廳','food'),('商務會議室','admin'),('行李與客務中心','service')],
 [('景觀溫泉池','spa'),('私人雙人湯屋','spa'),('精油護理室','spa'),('美容護理室','spa'),('桑拿休憩室','spa'),('健身房','sport'),('泳池會館','sport'),('靜音午睡沙龍','nap')],
 [(f'豪華景觀客房{n:02d}','deluxe') for n in range(1,9)],
 [(f'行政景觀客房{n:02d}','executive') for n in range(1,9)],
 [(n,'suite') for n in ['總統套房','皇家套房','天空套房','庭園套房','藝術套房','音樂套房','蜜月套房','家庭套房']]]
hotel=add('H1','星澄豪華溫泉飯店','住宿',5,8,940,1090,'deluxe',listed(hotel_names),'hotel');hotel['footprint_m']=[116,72]
civic_rows=[
 [('社區診所','health'),('牙科診間','health'),('家庭醫學診間','health'),('物理治療室','health'),('社區藥局','retail'),('健康教育室','study')],
 [('郵局','service'),('銀行服務室','service'),('警務服務站','service'),('市民服務室','admin'),('法律諮詢室','admin'),('就業服務室','admin')],
 [('海洋大水槽展廳','exhibit'),('水母展廳','exhibit'),('海洋生態教室','study'),('海洋研究展示室','study'),('水族館紀念品店','retail'),('海洋咖啡廳','cafe')],
 [('小劇場','club'),('電影放映室','club'),('美術展廳','club'),('城市歷史展廳','study'),('陶藝工坊','club'),('公共音樂室','club')],
 [('社區活動室','club'),('城市閱讀室','study'),('長者交誼室','club'),('家庭活動室','club'),('志工辦公室','admin'),('社區午睡室','nap')]]
cv=add('CV','市民文化與生活中心','生活',5,6,940,1290,'service',listed(civic_rows),'aquarium');cv['footprint_m']=[92,64]
tr=add('TR','大學城交通站','生活',2,5,40,1510,'transit',listed([
 [('站前售票廳','transit'),('公車候車室','transit'),('接駁車候車室','transit'),('自行車租借室','service'),('旅遊資訊室','service')],
 [('鐵道候車室','transit'),('轉乘資訊室','transit'),('車站咖啡廳','cafe'),('旅客休憩室','nap'),('站務服務室','service')]]));tr['footprint_m']=[70,40]
outdoor_names=['校門廣場','中央草坪','北庭院','南庭院','校園湖畔','湖畔木平台','河岸步道','河岸釣魚平台','櫻花步道','野餐草地','小神社','戶外舞台','籃球場','網球場','田徑場','自行車廣場','觀景亭','夜市廣場','露營營位A','露營營位B']
od=add('PK','公園與戶外網路','戶外',1,20,340,1510,'outdoor',lambda f,n:(outdoor_names[n-1],'outdoor'),'park');od['parcel'][2]=860;od['footprint_m']=None
sports_program=json.loads((OUT/'sports-program.json').read_text('utf-8'))
sports_overrides={r['id']:r for r in sports_program['rooms']}
from street_city_small_shop import PROFILES,ORIGINAL_PROFILES
for r in rooms:
    if r['building'] in PROFILES and r['building'] not in ORIGINAL_PROFILES:
        r['name']=PROFILES[r['building']]['uses'][r['floor']-1]
        if r['floor']==3:
            r.update(type='apartment',sleep='私人住宅；需住戶或受邀權限',access='住戶或受邀訪客',interactions=['睡覺','做飯','用餐','洗澡','休息'])
        elif r['floor']==2:r['interactions']=['使用設備','實作','整理','休息']
    template=r.get('programme_template_building',r['building'])
    if r['building'].startswith('O') and r['type']=='admin':
        r['interactions']=['接待訪客','預約會議','商務洽談'] if '接待' in r['name'] else (['預約會議','簡報討論','遠端會議','整理會議紀錄'] if '會議' in r['name'] else ['辦公工作','協作專案','遠端工作','整理文件'])
    if r['building'].startswith('O') and r['type']=='food':r['interactions']=['泡茶沖咖啡','加熱餐點','用餐休息','同事聊天','清洗杯具']
    if r['id'] in sports_overrides:
        r.update(sports_overrides[r['id']])
    if r['building']=='H1':
        hotel_dimensions={'H1-0101':[30,24,4.6],'H1-0103':[24,18,4.2],'H1-0104':[20,16,4.2],
          'H1-0105':[36,24,4.6],'H1-0106':[24,18,4.2],'H1-0107':[20,16,4.2],
          'H1-0201':[30,20,4.2],'H1-0202':[18,14,4.0],'H1-0206':[24,18,4.2],'H1-0207':[35,20,4.5]}
        r['size_m']=hotel_dimensions.get(r['id'],r['size_m'])
    if r['building']=='PK':
        n=r['slot'];r['site_position_m']=[100+((n-1)%5)*280,220+((n-1)//5)*400]
        if n in (13,14,15):r['interactions']=['運動','雙人練習','比賽','補水休息'];r['required_props']=['場地標線','球網或球架','休息椅','照明']
        if n==8:r['interactions']+=['釣魚'];r['required_props']+=['釣竿架']
        if n==11:r['interactions']+=['參拜','許願'];r['required_props']+=['鳥居','祈願牌架']
        if n in (19,20):r['interactions']+=['搭帳篷','露營睡覺','煮食'];r['required_props']+=['帳篷','睡袋×2','露營桌'];r['sleep']='過夜／午睡；需租借營位，天氣允許'
    if template=='S02' and r['floor']==1:r['interactions']=['點飲料','調整甜度冰量','外帶','雙人喝飲料','兼職調飲']
    if template=='S08' and r['floor']==1:r['interactions']+=['觀察寵物','挑選寵物用品']
    if template=='S20' and r['floor']==1:r['interactions']=['查看開獎','購買彩券','刮刮卡','兌獎','查看競技賽事']
    if template=='S11':r['interactions']=['預約美容','選擇造型','接受理髮或美甲','查看造型','付款'] if r['floor']<3 else r['interactions']
    specialties=[
      ('音樂','演奏樂器','樂器與譜架'),('吉他','合奏練習','吉他與音箱'),('舞蹈','排練舞步','舞蹈鏡與音響'),
      ('戲劇','排練劇本','舞台與道具箱'),('攝影','拍攝作品','相機與攝影燈'),('錄音','錄製音訊','麥克風與錄音控制台'),
      ('美術','繪畫創作','畫架與畫材'),('陶藝','製作陶器','拉坯機與工作檯'),('手作','製作手工藝','手作工具與材料櫃'),
      ('電競','雙人遊戲對戰','遊戲電腦與耳機'),('天文','觀測星象','天文望遠鏡'),('園藝','照顧植物','種植檯與園藝工具'),
      ('機器人','編程與測試機器人','機器人平台與安全罩'),('智慧製造','設定製造模擬','製造訓練設備'),
      ('人工智慧','訓練與評估模型','GPU工作站'),('生醫','分析醫學資料','生醫模擬設備'),
      ('電子','組裝與量測電路','電子工作檯'),('電路','量測電路','示波器與低壓電源'),
      ('資料結構','程式練習','教學電腦'),('演算法','程式解題','教學電腦'),('建築','製作建築模型','模型工作檯'),
      ('設計','製作設計作品','設計工作站'),('雷射','光學模擬實驗','封閉光學教學設備'),
      ('拉麵','選擇湯底與麵量','拉麵料理檯'),('咖哩','選擇辣度與配菜','咖哩料理檯'),
      ('水槽','觀察魚群','大型水槽'),('水母','觀察水母','水母展示缸')]
    if r['building']!='SP' and r['type'] not in ('dorm','apartment','office','admin','nap','suite','deluxe','executive'):
        for keyword,action,prop in specialties:
            if keyword in r['name']:
                if action not in r['interactions']:r['interactions'].append(action)
                r['required_props'].append(prop)
    if r['type']=='office':r['required_props']+=['系務終端']
    # Design-level action contracts; timings are proposed game minutes, not implemented code.
    r['interaction_rules']=[]
    for action in r['interactions']:
        minutes=30;cost='無；需購買或預約時依商品／服務價目'
        conditions=[r['access'],'設備未佔用；同伴互動需對方同意']
        result='完成對應活動、推進時間；符合條件的同行互動寫入關係記憶'
        if any(k in action for k in ['睡覺','午睡','睡休息']):
            minutes='玩家選擇；午睡15–90，過夜至指定起床時間';conditions.append(r['sleep']);result='恢復體力，推進時鐘與NPC日程；醒來回到床邊安全站位'
        elif any(k in action for k in ['讀書','閱讀','查資料']):
            result='推進學習時間並依現有學業規則增加智力；雙人讀書另結算同伴收益'
        elif action=='上課':minutes=180;conditions.append('該房間課表正在上課且已選課');result='記錄出席與學習時數，納入學期成績'
        elif any(k in action for k in ['購物','點餐','外帶','點飲料','付款','入住','租借','購票','購買']):
            minutes=10;conditions.append('資金足夠；商品、房間或服務有可用名額');cost='依價目扣款';result='扣除費用並取得商品／餐點／票券／有效服務權利；交易存檔'
        elif any(k in action for k in ['申請','報名','提交','辦理']):
            minutes=15;conditions.append('符合業務申請資格');result='顯示申請結果並更新學籍或服務狀態；失敗不誤扣款'
        elif any(k in action for k in ['運動','訓練','比賽','練習']):
            result='依活動消耗體力、增加對應熟練度；比賽結算勝負與同伴記憶'
        elif any(k in action for k in ['用餐','喝','補水']):result='消耗餐點或飲料，恢復飽食／水分並推進時間'
        elif any(k in action for k in ['拍照','拍攝','錄製']):result='保存對應照片或作品，附房間位置與時間；同伴拍攝先確認同意'
        r['interaction_rules'].append(dict(action=action,game_minutes=minutes,requirements=conditions,cost=cost,result=result,cancel='可中斷；耗材、已完成消費與已過時間不回溯'))
    r['anchors']=[{'id':f'{r["id"]}:entry','purpose':'進出與尋路'}, {'id':f'{r["id"]}:talk','purpose':'雙人交談'}]+[{'id':f'{r["id"]}:act{i+1:02d}','purpose':a} for i,a in enumerate(r['interactions'])]
    if r['sleep']!='無' and not r['sleep'].startswith('無；'):
        r['anchors'].append({'id':f'{r["id"]}:sleep','purpose':'睡眠／醒來／同行者站位；依睡眠條件開放'})

assert len({b['id'] for b in buildings})==len(buildings)
assert len({r['id'] for r in rooms})==len(rooms)
assert len(offices)==len(depts)==21
byid={r['id']:r for r in rooms}
for b in buildings:
    b['scene_count']=len(b['room_ids'])
    x,y,w,h=b['parcel'];b['world_center_m']={'north':1700-y-h/2,'east':x+w/2,'up':0}
    b['floor_height_m']=4.8 if b['id']=='H1' else (4.5 if b['id']=='M1' else 4.0)
    b['circulation']='每層：連通走廊、電梯、兩座分離樓梯、公共衛浴、設備與清潔空間；不計入1000個互動空間'
    if b['category']=='商業' and b['id']!='M1':b['circulation']='各層獨立房間；側邊樓梯、無障礙升降平台、衛浴與設備空間；不計入場景數'

def esc(s):return html.escape(str(s),quote=True)
def text(x,y,s,size=18,fill='#203c45',weight=400):return f'<text x="{x}" y="{y}" font-size="{size}" fill="{fill}" font-weight="{weight}">{esc(s)}</text>'
def rect(x,y,w,h,fill,stroke='none',rx=12):return f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}" stroke="{stroke}"/>'
def svg(w,h,body,title):
    return f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}" role="img"><title>{esc(title)}</title><style>text{{font-family:"Microsoft JhengHei","Noto Sans TC",sans-serif}}a:hover rect{{stroke:#162d39;stroke-width:3}}</style>{body}</svg>'

body=rect(0,0,2220,1960,'#f5f3ed',rx=0)
body+=text(55,60,'STREET SOCIAL  /  UNIVERSITY CITY',20,'#52716e',600)
body+=text(55,118,'大學城・千景重製總規畫',44,weight=700)
body+=text(57,155,'44 個建築／空間群  ·  1,000 個獨立互動空間  ·  21 個系所辦公室  ·  2026.10.11',20)
body+=rect(30,190,1250,1720,'#e4ece2')
# Roads and green corridors are schematic; parcel cards are not building footprints.
for y in (235,425,620,820,1015,1465,1680):
    body+=rect(35,y,1235,22,'#fffdf7',rx=3)
for x in (315,615,915):body+=rect(x,240,16,1200,'#fffdf7',rx=3)
body+=text(56,218,'北 N ↑  宿舍區 → 教學區 → 校務區 → 商業生活區 → 南側交通門戶',16)
for b in buildings:
    x,y,w,h=b['parcel'];y+=190
    c=COLORS[b['category']]
    card=rect(x,y,w,h,'#ffffff',c)
    card+=rect(x,y,6,h,c,rx=2)
    small=w<200
    card+=text(x+14,y+25,b['id']+' '+b['name'],14 if small else 18,weight=700)
    card+=text(x+14,y+48,f'{b["floors"]}層 × {b["rooms_per_floor"]}間 = {b["scene_count"]}景',13 if small else 17,c,600)
    if b['category']=='教學':
        card+=text(x+14,y+73,'每層：5教室＋1辦公室',16)
        obs=[o for o in offices if o['building']==b['id']]
        for j,o in enumerate(obs):card+=text(x+14,y+95+j*14,f'{o["floor"]}F {o["department"]}',12)
        if not obs:card+=text(x+14,y+96,'1–5F 通識／語言／體育／教發／委員會',11)
    elif not small:
        desc={'AD':['1F註冊招生｜2F學務住宿','3F總務資訊｜4F國際研發','5F校長、人事、主計'], 'M1':['每層8間獨立店鋪','1–8F購物｜9F美食｜10F景觀餐飲'],
              'H1':['1F接待餐宴｜2F溫泉SPA','3F豪華房｜4F行政房｜5F套房','套房24×18m；另有私人露台'],
              'PK':['20個分散節點；含露營、神社、河岸','此卡代表戶外網路，實際節點見詳圖'],
              'LI':['書庫、研討、共讀、夜間自習','5F午睡預約室'],
              'SC':['1F食堂｜2–4F社團｜5F活動諮詢'],
              'SP':['球場、健身、泳池、復健與午睡'],
              'IN':['創客、機器人、製造、AI、生醫'],
              'CV':['1F醫療｜2F市民服務｜3F水族館','4F藝文｜5F社區與午睡'],
              'TR':['1F公車接駁｜2F鐵道候車']} .get(b['id'],['1F公共服務＋2間訪客住宿房','2–6F每層10間寢室'] if b['id'].startswith('D') else ['每層8間獨立住家；可租屋過夜'])
        for j,line in enumerate(desc):card+=text(x+14,y+75+j*21,line,12 if w<300 else 14)
    body+=f'<a href="buildings/{b["id"]}.svg"><title>{esc(b["name"])}：點選逐層詳圖</title>{card}</a>'

body+=rect(1310,190,865,1720,'#fffdf8')
body+=text(1345,240,'01 / 空間預算',27,weight=700)
summary=[('八棟學院教學館',240),('行政／圖書／學生／研究／運動',200),('四棟六層宿舍',240),('十層百貨・80間店',80),('20棟街邊店・各3個空間',60),('兩棟租屋公寓',80),('豪華溫泉飯店',40),('市民文化生活中心',30),('交通站',10),('公園與戶外節點',20)]
for j,(n,c) in enumerate(summary):
    yy=285+j*34;body+=text(1345,yy,n,20)+text(2070,yy,c,22,weight=700)
body+=rect(1345,635,795,66,'#1e4549')+text(1368,678,'總計  1,000 個互動空間',28,'white',700)
body+=text(1345,750,'02 / 百貨・逐層業種',27,weight=700)
themes=['美妝・精品・禮品','女裝・訂製服','男裝・商務・街頭','鞋包・眼鏡・飾品','家具・家飾・生活','書店・文具・閱讀','電器・攝影・維修','玩具・遊戲・手作','美食街・八間餐飲','景觀餐廳・咖啡・酒吧']
for j,t in enumerate(themes):body+=text(1345,793+j*31,f'{j+1:02d}F  {t}  /  8店',20)
body+=text(1345,1140,'03 / 住宿與睡眠',27,weight=700)
sleep_count=sum(any(a in r['interactions'] for a in ['睡覺','入住睡覺','預約午睡','露營睡覺','鋪被休息']) for r in rooms)
for j,t in enumerate([f'{sleep_count}個空間提供過夜或指定午睡功能','宿舍：每棟50間寢室＋2間訪客住宿房','公寓：80戶；店主住家與民宿另行列冊','飯店：8豪華房＋8行政房＋8主題套房','午睡：圖書館、學生中心、運動館與交通站','戶外：兩處可預約露營營位；長椅僅坐休']):body+=text(1345,1180+j*31,t,19)
body+=text(1345,1420,'04 / 製作與落地規則',27,weight=700)
for j,t in enumerate(['每個房間有ID、尺寸、互動、家具與進出條件。','Grok控制三視圖 → Tripo拆件 → 組裝與互動。','走廊、電梯、樓梯、衛浴與設備另製，不灌入景數。','總圖是分區／基地卡片示意，非按比例建築施工圖。','規畫基地約1.3 × 1.7 km；右圖與樓層圖可點選。','每棟樓層詳圖列出全部房間；索引可搜尋互動。','狀態：規畫完成；尚未送出Tripo生成或替換遊戲。']):body+=text(1345,1465+j*34,t,18)
body+=text(55,1940,'MASTERPLAN V1   /   點選建築開啟樓層配置與房間用途   /   SCHEMATIC PARCEL PLAN',16,'#52716e')
write_text(OUT/'masterplan.svg',svg(2220,1960,body,'StreetSocial 大學城1000互動空間規畫'))

# Per-floor cards are schematic room adjacency diagrams with explicit access and interactions.
for b in buildings:
    per=b['rooms_per_floor'];cols=4 if per>=8 else (3 if per>=3 else 1)
    rows_per=(per+cols-1)//cols;floor_h=110+rows_per*155+65
    width=1460;height=155+b['floors']*floor_h+100
    s=rect(0,0,width,height,'#f5f3ed',rx=0)+text(40,55,f'{b["id"]} / {b["name"]}',32,weight=700)
    fp='戶外分散節點' if not b['footprint_m'] else '建築建議尺度 '+str(b['footprint_m'][0])+'×'+str(b['footprint_m'][1])+'m'
    s+=text(40,92,f'{b["floors"]}層｜{b["scene_count"]}個獨立互動空間｜{fp}｜室內淨尺寸見房間卡',17)
    s+=text(40,121,'配置為鄰接示意；走廊／衛浴／核心另計；點擊房間可於索引查看完整互動與家具。',16)
    for f in range(1,b['floors']+1):
        y=155+(f-1)*floor_h;s+=rect(25,y,1410,floor_h-15,'#fffdf8')
        s+=text(45,y+36,('戶外節點' if b['id']=='PK' else f'{f}F')+' / '+('每層5教室＋1辦公室' if b['category']=='教學' else f'{per}個互動空間'),24,weight=700)
        s+=rect(45,y+50,1370,30,'#dfe9e5')+text(60,y+72,'公共通道 ⇄ 電梯／無障礙設施 ⇄ 樓梯A、B ⇄ 衛浴／設備；所有房間獨立開門',15)
        for j,r in enumerate(r for r in rooms if r['building']==b['id'] and r['floor']==f):
            x=45+(j%cols)*(1370/cols);yy=y+95+(j//cols)*155;cw=1370/cols-12
            tile=rect(x,yy,cw,142,'#f0f4f0',COLORS[b['category']])
            tile+=text(x+12,yy+23,r['id'],16,COLORS[b['category']],700)
            tile+=text(x+12,yy+49,r['name'],16,weight=700)
            tile+=text(x+12,yy+72,' × '.join(map(str,r['size_m']))+'m',14)
            for k in range(2):tile+=text(x+12,yy+94+k*20,'／'.join(r['interactions'][k*3:k*3+3]),12)
            if r['sleep']!='無':tile+=text(x+12,yy+134,'休息條件詳見房間清單',11,'#986a35')
            s+=f'<a href="../index.html?room={r["id"]}#rooms">{tile}</a>'
        if b['category']=='教學':
            rr=byid[f'{b["id"]}-{f:02d}06'];s+=text(45,y+floor_h-40,'辦公室位置：'+rr['id']+' '+rr['name']+'；06室在當層公共走廊東端。',16)
    s+=text(40,height-45,'規畫狀態：planned｜室內模型、互動錨點與引擎整合均待製作｜回總圖：../masterplan.svg',16)
    building_svg=OUT/'buildings'/f'{b["id"]}.svg'
    building_temp=building_svg.with_suffix('.svg.tmp')
    write_text(building_temp,svg(width,height,s,b['name']+'逐層空間規畫'))
    building_temp.replace(building_svg)

from street_city_geometry import design_room_dimensions,demand_footprint
for r in rooms:
    if r['building']!='PK':r['size_m']=design_room_dimensions(r)
    from street_city_small_shop import PROFILES
    if r['building'] in PROFILES:r['name']=PROFILES[r['building']]['uses'][r['floor']-1]
for b in buildings:
    if b['id']!='PK':b['footprint_m']=demand_footprint(b['id'],[r for r in rooms if r['building']==b['id']])

manifest=dict(schema=1,date='2026-10-12',status='planning_only',target_scene_count=len(rooms),
              coordinates='metres; +X north, +Y east, +Z up; UE centimetres = metres*100; world centres are proposed, not existing scene coordinates',
              buildings=buildings,department_offices=offices,rooms=rooms)
write_text(OUT/'city-plan.json',json.dumps(manifest,ensure_ascii=False,indent=2))
fields=['id','building_name','floor','slot','name','type','size_m','interactions','required_props','sleep','access','interaction_rules','status']
with text_stream(OUT/'room-schedule.csv',encoding='utf-8-sig',newline='') as fh:
    w=csv.DictWriter(fh,fieldnames=fields);w.writeheader()
    for r in rooms:w.writerow({k:(json.dumps(r[k],ensure_ascii=False) if k=='interaction_rules' else (' / '.join(map(str,r[k])) if isinstance(r[k],list) else r[k])) for k in fields})

with text_stream(OUT/'tripo-room-briefs.jsonl') as fh:
    for seq,r in enumerate(rooms,1):
        brief=dict(sequence=seq,room_id=r['id'],status='design_only_use_production_part_jobs',task_id=None,
          target_asset=f'City/{r["building"]}/{r["id"]}/visual',
          prompt=f"Create a coherent stylized contemporary Taiwanese university city room: {r['building_name']} / {r['name']}. Clear interior dimensions {r['size_m']} metres. Required furnishings: {'; '.join(r['required_props'])}. Support these affordances with separate usable furniture: {'; '.join(r['interactions'])}. Open entrance, human scale, consistent realistic proportions, PBR materials, no people, no baked lighting, no text labels. Provide inspectable interior; ceilings and enclosing shell must be separable during cleanup. Keep clear navigation between doorway and each interaction station. This is one specific room, not an entire city.",
          required_engine_work=['尺度校正','外殼/天花/門/可用家具分離','材質與UV檢查','簡化碰撞','NavMesh與門口連通','逐互動錨點與雙人位置','LOD與串流','進出權限與睡眠條件'],
          interactions=r['interactions'],interaction_rules=r['interaction_rules'],anchors=r['anchors'],access=r['access'],sleep=r['sleep'])
        fh.write(json.dumps(brief,ensure_ascii=False)+'\n')

data=json.dumps(manifest,ensure_ascii=False).replace('</','<\\/')
page='''<!doctype html><html lang="zh-Hant"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>StreetSocial 大學城 1000景規畫</title><style>
body{margin:0;background:#f5f3ed;color:#203c45;font:16px "Microsoft JhengHei",sans-serif}main{max-width:1450px;margin:auto;padding:32px}h1{font-size:36px}a{color:#237c80}nav{display:flex;gap:22px;flex-wrap:wrap;margin:24px 0}iframe{width:100%;height:850px;border:1px solid #d6dfd7;background:white}input,select{padding:12px;font:inherit;border:1px solid #b8cdc4;border-radius:8px;margin:6px}table{border-collapse:collapse;width:100%;background:#fffdf8}th,td{text-align:left;padding:12px;border-bottom:1px solid #dde5dd;vertical-align:top}th{background:#dfe9e5;position:sticky;top:0}small{color:#677e78}.cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:12px}.card{background:#fffdf8;padding:18px;border:1px solid #c7d7ce;border-radius:10px}details{margin:8px 0}summary{cursor:pointer}</style><main>
<small>STREET SOCIAL / UNIVERSITY CITY / 2026.10.11 / PLANNED</small><h1>大學城・1,000個互動空間</h1><p>44個建築／空間群、21個系所辦公室。總圖與各棟逐層配置皆為SVG，可放大、另存與逐間核對。</p>
<nav><a href="masterplan.svg">開啟SVG總圖</a><a href="#buildings">44棟逐層SVG</a><a href="#offices">系所辦公室</a><a href="#rooms">逐間互動／睡眠清單</a><a href="room-schedule.csv">CSV清單</a><a href="city-plan.json">完整JSON</a><a href="tripo-room-briefs.jsonl">Tripo逐間製作規格</a><a href="production/index.html">V2 Grok三視圖／拆件組裝</a><a href="README.md">重製與整合說明</a></nav>
<iframe src="masterplan.svg" title="SVG大學城總圖"></iframe><h2 id="buildings">建築與逐層配置</h2><div class="cards" id="cards"></div><h2 id="offices">系所辦公室位置</h2><p>各系辦依本層房間編號定位，實際座標見當前樓層圖。校級行政集中AD，完整單位見AD逐層圖。</p><div id="office-list"></div>
<h2 id="rooms">每個場景的互動與住宿條件</h2><p>全部空間均已指定用途。走廊、電梯、樓梯、一般衛浴和機電空間額外製作，不計入1,000景。</p><input id="search" placeholder="搜尋房間、系所、互動或編號"><select id="building"><option value="">所有建築</option></select><label><input id="sleep" type="checkbox">僅看可睡覺／午睡空間</label><p id="count"></p><table><thead><tr><th>編號／位置</th><th>用途／尺寸</th><th>可做的互動／家具</th><th>睡眠／進入條件</th></tr></thead><tbody id="rows"></tbody></table></main><script>
const plan=DATA;const $=id=>document.getElementById(id);const escape=s=>String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
for(const b of plan.buildings){$('cards').insertAdjacentHTML('beforeend',`<a class="card" href="${b.id==='PK'?'world-terrain.svg':'production/floor-layouts/'+b.id+'.svg'}"><b>${escape(b.id+' '+b.name)}</b><p>${b.floors}層 × ${b.rooms_per_floor}間 = ${b.scene_count}景</p><small>逐層SVG配置 →</small></a>`);$('building').add(new Option(b.id+' '+b.name,b.id));}
$('office-list').innerHTML='<table><tr><th>系所</th><th>樓層／房間</th></tr>'+plan.department_offices.map(o=>`<tr><td>${escape(o.department)}</td><td><a href="production/floor-layouts/${o.building}.svg">${o.building} ${o.floor}F / ${o.room}</a></td></tr>`).join('')+'</table>';
function render(){const q=$('search').value.toLowerCase(),b=$('building').value;let rs=plan.rooms.filter(r=>(!b||r.building===b)&&JSON.stringify(r).toLowerCase().includes(q)&&(!$('sleep').checked||r.anchors.some(a=>a.id.endsWith(':sleep'))));$('count').textContent=`顯示 ${rs.length} / ${plan.rooms.length}個空間`;$('rows').innerHTML=rs.map(r=>`<tr><td><a href="${r.building==='PK'?'world-terrain.svg#outdoor-'+r.id:'production/floor-layouts/'+r.building+'.svg'}">${r.id}</a><br>${escape(r.building_name)} ${r.floor}F</td><td><b>${escape(r.name)}</b><br>${r.size_m.join('×')}m</td><td>${escape(r.interactions.join('、'))}<details><summary>必要家具與互動錨點</summary>${escape(r.required_props.join('、'))}<p>${r.anchors.map(a=>escape(a.id+' — '+a.purpose)).join('<br>')}</p></details><details><summary>每個互動的條件、時間與結果</summary>${r.interaction_rules.map(a=>'<p><b>'+escape(a.action)+'</b> / '+escape(a.game_minutes)+'遊戲分鐘<br>'+escape(a.requirements.join('；'))+'<br>費用：'+escape(a.cost)+'<br>結果：'+escape(a.result)+'<br>'+escape(a.cancel)+'</p>').join('')}</details></td><td>${escape(r.sleep)}<br><small>${escape(r.access)}</small></td></tr>`).join('');}
for(const id of ['search','building','sleep'])$(id).addEventListener('input',render);const room=new URLSearchParams(location.search).get('room');if(room)$('search').value=room;render();</script></html>'''.replace('DATA',data)
page=page.replace('44個',f'{len(buildings)}個').replace('44棟',f'{len(buildings)-1}棟').replace('1,000',f'{len(rooms):,}').replace('1000景',str(len(rooms))+'景')
write_text(OUT/'index.html',page)
if (OUT/'world-terrain.svg').is_file():
    page=page.replace('<a href="masterplan.svg">開啟SVG總圖</a>', '<a href="world-terrain.svg">開啟完整世界地形SVG</a><a href="masterplan.svg">建築功能與景數規畫</a>')
    page=page.replace('<iframe src="masterplan.svg" title="SVG大學城總圖">', '<iframe src="world-terrain.svg" title="完整世界地形圖">')
    page=page.replace('44個',f'{len(buildings)}個').replace('44棟',f'{len(buildings)-1}棟').replace('1,000',f'{len(rooms):,}').replace('1000景',str(len(rooms))+'景')
write_text(OUT/'index.html',page)

readme='''# StreetSocial 大學城重製規畫 V1

日期：2026-10-11。狀態：完成規畫；尚未送出 Tripo 生成、尚未替換遊戲。

先開啟 index.html；masterplan.svg 是總圖，buildings/ 內44張SVG包含所有樓層與全部1,000個房間。room-schedule.csv 是逐間互動清冊，city-plan.json 保存建築、房間、21個系所辦公室與互動錨點。tripo-room-briefs.jsonl 是1,000筆尚未提交的逐間生成規格。規畫工具為 Tools/plan_street_social_city.py，可重建所有輸出。

## 現況與重製範圍

- StreetSocial.cmd 呼叫 Tools/run_street_social_development.lua，啟動已驗證的分期建置與資源補丁；它不是地圖內容本身。
- Source/OpenKoikatsu/OKStreetSocialPlaces.h 管理地點、模型、碰撞、室內範圍與活動錨點。現有模型由 Tools/street_models/build_street_models.py 產生 .okmesh。
- 校園以 OKStreetSocialCampus.h 的八學院與21系所為來源。現有 campus.py 為1F系辦、2–5F每層4教室；本案重新規畫為每層5教室加1辦公室。每個系所一間獨立系辦；剩餘樓層辦公室分給院辦與教學服務。既有 Docs/STREET_SOCIAL_CAMPUS_PLAN.md 描述舊版，未覆寫。
- 現有 dept_store.py 是十層、每層一個開放空間，18×20m；本案改為96×60m、每層8間真正獨立店舖，完整80店。
- 現有住宿、商店、溫泉、水族館與神社功能在本案重新配置。legacy_place 是遷移線索，不代表原場景已經接入；未列出的舊地點、活動和存檔仍需遷移核對。

## 空間計數與尺度

八棟教學240 + 五棟校園服務200 + 宿舍240 + 百貨80 + 街邊店60 + 公寓80 + 飯店40 + 市民中心30 + 交通10 + 戶外20 = 1000。

共有980個室內空間與20個戶外節點。場景定義為可獨立進出／啟動用途的房間或戶外活動區，不是家具數或互動次數。宿舍每棟6層×10間：1F八間公共服務＋兩間訪客住宿房；2–6F每層10間寢室，共50間寢室。每棟52間可住宿房，四棟208間。每個房間都會有獨立資產與狀態，即使共用家具樣式。

masterplan.svg 為用途索引；world-terrain.svg 與 production/floor-layouts 為同一公尺外框。世界中心採北向X、東向Y、上向Z，單位公尺，進UE乘100。建議基地約1.3×1.7km。當前精確樓層圖在 production/floor-layouts；房間標示為淨尺寸，核心另記含牆外框與扣牆淨空，外牆及隔牆設計厚度0.2m。所有基地尺寸、樓高與世界位置都是提案值。

飯店外框依需求求解：1F接待餐宴、2F溫泉SPA、3F八間12×10m豪華客房、4F八間14×12m行政客房、5F八間20×16m套房；實際外框與附屬客房見當前配置。客房淨尺寸包含客廳、衛浴與室內更衣空間，陽台／露台另加。所有樓層另留通道與兩個分離疏散核心；套房需再拆分獨立臥室、客廳與湯池模型以維持可用室內。

## 逐間互動的落地契約

每筆房間都有可執行互動清單、必要家具、進入條件與互動錨點。這是設計清單，非已完成遊戲功能。每個動作錨點後續必須綁定實際家具座標、角色姿勢、同伴站位、導航與可用時間，不得只有選單文字。

- 睡眠：先驗證住戶／有效訂房／營位權限及床位是否空閒；選擇睡眠時長或起床時間，角色走至床／躺椅／睡袋，睡醒恢復體力並推進時間和日程。可取消、可被事件喚醒，同行者依同意與床位安排。私人空間不可因持有邀請就永久開放。
- 午睡：只允許標記的午睡室與員工休息室，限時休息；咖啡廳沙發、公共長椅不預設可過夜。公共衛浴、診療床不作住宿。
- 讀書／雙人讀書：檢查座位、開放時間、同伴同意；沿用學業系統的時間與智力收益，留存同行記憶。
- 上課：依課表、系所與房間編號檢查出席；需將舊 classK_R 和課程地點映射到新房間。
- 系務／校務：接對應服務，系辦辦轉系、休復學、入學及成績單；AD各單位只提供所屬業務。預約與申請應有成功、拒絕、排隊、費用和完成提示。
- 餐飲／購物／服務：營業時間、價格、金錢、庫存／服務時段與結果均需綁定經濟系統；試穿為預覽，付費後才取得物品。兼職另驗證職缺與班表。
- 運動／遊戲／研究／社團：器材、預約、能力門檻、時間消耗、體力與技能結果獨立配置；不能讓每個專業實驗室只有同一個泛用按鈕。
- SPA／更衣／淋浴：私人使用權、設備佔用、衣著切換、角色隱私與同行同意需要明確狀態；使用時有中斷與離開處理。
- 戶外：散步、攝影、釣魚、參拜、運動與露營各綁定對應節點；營位加入租借與天氣條件。

## Tripo製作順序

**2026-10-11 V2更新：** 實際製作以production/index.html及production/README.md為準：控制幾何 → Grok透視三視圖 → 逐零件獨立圖 → Tripo拆件生成 → 清理與座標組裝。下列是V1的分批範圍；tripo-room-briefs.jsonl僅保留房間用途設計，不能作為直接生成完整房間的提交工單。

1. 先驗證六個代表空間：EE-0101教室、EE-0306資訊工程系系辦、D1-0201寢室、M1-0201女裝店、H1-0301豪華房、H1-0501總統套房。先確認封閉室內可檢視、尺寸、可用家具、材質及角色站位。
2. 每間空間獨立送件，以room_id追蹤任務、版本、成本、原始輸出、清理後檔案與引擎匯入紀錄。JSONL目前只是規格，沒有隱藏API執行或消耗額度。
3. 校園240 → 校園服務200 → 宿舍240 → 百貨80 → 店舖60 → 公寓80 → 飯店40 → 市民中心30 → 交通10 → 戶外20；在完成室內驗證後，分批製作各建築外觀與公共核心，最後接街道、景觀與地圖。
4. 建築外殼、各層走廊、樓梯、電梯、衛浴、清潔／機電、街道、地形、河道、橋梁、植栽、路燈、標誌是額外資產，不能漏掉，也不算入1,000景。逐間清單並非整個世界的全部Tripo任務數。
5. Tripo負責視覺資產來源；可行走室內、精確牆洞、開門、碰撞、尋路、互動、串流與存檔由引擎與資產清理完成。不能將一次生成結果當成可直接運行的完整房間。正式執行前核對當前Tripo功能、匯出格式、帳戶額度與成本。

## 引擎整合限制與完成條件

- .okmesh v2目前每模型最多64個rooms與64個anchors。百貨80間且每間多個錨點，不能打包為同一個舊模型；應以建築外殼＋樓層＋獨立房間資產載入，或正式升級格式和載入器。
- 現行.okmesh以色彩區段與簡單材質為主，Tripo PBR貼圖不能假設可直接保留；需獨立的PBR匯入管線或升級模型格式，不可默默丟棄貼圖。
- 建立穩定room_id，區分建築、樓層、房間與互動狀態。保存舊存檔、NPC行程、系辦和電梯停靠點的映射。
- 不同建築與樓層分區串流；保留相鄰通道與NPC路徑資料，遠方以LOD／外殼表示。1,000景不是同時載入1,000套完整室內。
- 每間逐項驗證：入口可走、門可開、碰撞無卡點、同行者能進出、所有宣告互動真的有結果、睡眠條件正確、存讀檔一致、日夜照明與鏡頭正常。
- 全圖驗證：21系辦可找到、80店皆可進入、宿舍60間／棟完整、飯店40間完整、1000個ID無重複且無不可達孤島；評估串流峰值記憶體、幀率和讀取時間後才替換StreetSocial正式啟動資源。
'''
readme=readme.replace('街邊店60 + 公寓80',f'街邊店{len(shop_rows)*3} + 辦公樓{len(OFFICES)*24} + 公寓80').replace('交通10 + 戶外20 = 1000',f'交通10 + 戶外20 = {len(rooms)}').replace('建議基地約1.3×1.7km','現行世界1.1×1.1km').replace('1000個ID',str(len(rooms))+'個ID').replace('每個房間都會有獨立資產與狀態，即使共用家具樣式','每個房間都有獨立位置、用途與狀態；相同家具共用資產模型').replace('96×60m',str(MALL_SIZE_M[0])+'×'+str(MALL_SIZE_M[1])+'m').replace('共有980個室內空間與20個戶外節點',f'共有{len(rooms)-20}個室內空間與20個戶外節點').replace('2.2×2.2km','1.1×1.1km').replace('與74棟實際尺寸建築輪廓',f'與{len(buildings)-1}棟實際尺寸建築輪廓').replace('masterplan.svg 為用途索引','masterplan.svg與world-terrain.svg是同一份地形總圖')
readme=readme.replace('buildings/ 內44張SVG',f'buildings/ 內{len(buildings)}張SVG').replace('全部1,000個房間',f'全部{len(rooms):,}個主要單元').replace('1,000筆尚未提交的逐間生成規格',f'{len(rooms):,}筆用途規格（不能直接提交完整房間）').replace('不算入1,000景',f'不算入{len(rooms):,}景').replace('1,000景不是同時載入1,000套',f'{len(rooms):,}景不是同時載入{len(rooms):,}套')
readme='# 大學城緊湊街區更新（2026-10-12）\n\n'+f'世界1.1×1.1km；各軸為前輪50%，面積為25%。{len(buildings)-1}棟建築、48棟街屋、三棟六層辦公樓、百貨150×100m，{len(rooms)}個主要互動單元。建築不同比例縮小；現行尺寸以production/effective-city-plan.json為準。\n\n'+readme
write_text(OUT/'README.md',readme)
if (OUT/'world-terrain.svg').is_file():
    terrain_note='\n**完整世界地形圖：** [world-terrain.svg](world-terrain.svg)。這張圖呈現1.1×1.1km的山地、等高線、河流、湖泊、濕地、海灣沙灘、道路、橋梁、鐵道與74棟實際尺寸建築輪廓。masterplan.svg與world-terrain.svg共用最新地形圖；地理位置以world-terrain-layout.json為準，取代V1基地卡片的位置。產線組裝器已讀取新世界座標；地形線與海拔是新世界設計，非現有umap測繪或已完成高度場。\n'
    write_text(OUT/'README.md',readme.replace('## 現況與重製範圍',terrain_note+'\n## 現況與重製範圍'))
for path in OUT.rglob('*.svg'):ET.parse(path)
report=dict(buildings=len(buildings),rooms=len(rooms),department_offices=len(offices),mall_shops=sum(r['building']=='M1' for r in rooms),
            sleep_spaces=sum(any(a['id'].endswith(':sleep') for a in r['anchors']) for r in rooms),svg_files=len(list(OUT.rglob('*.svg'))),
            category_totals=dict(Counter(r['type'] for r in rooms)),status='SVG XML and counts validated; planning only')
write_text(OUT/'validation.json',json.dumps(report,ensure_ascii=False,indent=2))
print(json.dumps(report,ensure_ascii=False))
