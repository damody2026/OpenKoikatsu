"""Campus typologies shared by the compiler, drawings and image briefs.

Published Taiwanese cases establish uses, never unmarked metre dimensions.
Replace equipment inside already verified bays; do not expand exterior shells
or erase embedded support rooms to accommodate an attractive image.
"""
from collections import Counter

ACADEMIC=('E1','EE','MA','T4','T2','IB','T1','RB')
CAMPUS=ACADEMIC+('AD','LI','SC','IN','SP','D1','D2','D3','D4')
EQUIPMENT={
 'campus_wardrobe':('寢室個人衣物置物櫃',[.65,.6,1.9],'four individual labelled lockable cabinets, hinged panels, handles and ventilation slots'),
 'campus_drafting':('製圖與模型評圖工作檯',[2.4,1.2,.9],'open steel legs, tilted drawing board, cutting mat, scale model and stools'),
 'campus_optics':('光學量測教學工作檯',[2.4,1.2,1.5],'open-leg optical breadboard, separate lens mounts and enclosed low-power teaching light source'),
 'campus_material':('材料試片觀察工作檯',[2.4,1.2,1.5],'open legs, specimen trays, microscope and compact benchtop measuring instrument'),
 'campus_language':('語言教學雙人桌椅與耳機',[1.2,1.6,1.05],'two individual chairs, thin tabletop, headset and small fold-flat control panel'),
 'campus_business':('商業決策教學雙人桌椅',[1.2,1.6,1.05],'two separate seats, laptop and group decision worksheets on open-leg table'),
 'campus_exhibit':('校園成果低展示台',[1.8,.6,1.8],'open display shelves, separately modelled scale prototypes, labels, no opaque block'),
}
CATALOG_ADDITIONS={k:(v[0],v[1]) for k,v in EQUIPMENT.items()}

SOURCES={
 'mechanical':('臺北科大機械系空間總覽','https://me1.ntut.edu.tw/p/412-1062-10313.php?Lang=zh-tw','加工、材料觀察、控制與系辦分開；重型加工在低樓層。'),
 'materials':('臺科大材料系公用實驗室','https://mse.ntust.edu.tw/p/412-1019-249.php?Lang=zh-tw','材料化學、金相與儀器各有專用空間；本案僅建低風險桌上觀察教學。'),
 'electrical':('臺科大電機系教學實驗室','https://www.ee.ntust.edu.tw/p/412-1027-7590.php?Lang=zh-tw','控制、微算機、電機機械、電路與電磁波分別配置；搜尋可讀，直接開頁502。'),
 'management':('臺科大工管系教學環境','https://www.im.ntust.edu.tw/var/file/14/1014/img/900741857.pdf','教學、實習、研究分開；智慧製造實習與管理討論不是同一種教室。PDF直接開啟逾時，僅搜尋摘要。'),
 'design':('臺科大設計系系所空間','https://www.dt.ntust.edu.tw/index.php/webpage/view?id=8','學生工作室、電腦教室、PBL討論、模型工廠、攝影、教師研究室與系辦分開。'),
 'language':('臺科大應外系系所空間','https://afl.ntust.edu.tw/p/405-1121-129332,c11880.php?Lang=zh-tw','多媒體語言、口譯亭、排練、閱讀及討論空間各有用途。'),
 'campus':('臺科大校內配置文字說明','https://rpage.ntust.edu.tw/p/404-1000-88778.php?Lang=zh-tw','各院系、行政、研究、國際事務及學生服務有明確分工；本案為虛構校園改編。'),
 'library':('臺科大圖書館空間使用狀況','https://library.ntust.edu.tw/p/412-1049-12983.php?Lang=zh-tw','團體討論與視聽有可預約專用空間，開架閱覽與隔音小間分開。'),
 'student':('臺大第二學生活動中心八樓平面','https://www.active.ntu.edu.tw/AC2_8F_Floor','社團房間沿公共通道，配有廁所、電梯、樓梯；沒有公尺標註。'),
 'dorm':('臺科大宿舍設施說明','https://student.ntust.edu.tw/p/412-1053-8395.php?Lang=en','個人床、櫃、桌、椅；逐層洗烘與衛浴，共用交誼、自習及管理；本案四人套房不是照抄其六人房。'),
 'sport':('臺中北區運動中心泳池','https://tndcsc.com.tw/swimPool.aspx','50×25m競賽池及50×20m訓練池；本案三池及球場數是遊戲設計，非該館平面。'),
}
SOURCE_KEYS={
 'E1':['mechanical','materials'],'EE':['electrical'],'MA':['management','campus'],
 'T4':['design'],'T2':['language'],'IB':['materials','campus'],'T1':['language','campus'],
 'RB':['mechanical','design'],'AD':['campus'],'LI':['library'],'SC':['student'],
 'IN':['mechanical','materials','design'],'SP':['sport'],
 **{b:['dorm'] for b in ('D1','D2','D3','D4')},
}
ART={
 'E1':'U-shaped engineering court, exposed concrete structural rhythm, brick infill, shaded arcades; heavy work remains on ground floor',
 'EE':'rectangular electrical faculty, pale ceramic tile, repeated recessed laboratory windows, horizontal sunshades, cable/service risers',
 'MA':'rectangular management faculty, warm brick and light stone, shaded public entry, regular classroom window bays',
 'T4':'rectangular design faculty, visible open studio glazing behind vertical shading fins, ground-floor model display, workshop shutters',
 'T2':'rectangular language and education faculty, quiet recessed teaching windows, colonnaded entry, acoustic rehearsal behind solid wall',
 'IB':'rectangular international and applied science faculty, pale stone with green fins, bilingual entry, controlled lab and public waiting separated',
 'T1':'rectangular general education faculty, Taiwanese red-brick teaching block, regular windows and shaded passage, no invented grand atrium',
 'RB':'rectangular graduate research block, concrete and terracotta fins, restrained glazing and separate loading access',
 'AD':'rectangular administrative building, shaded civic entrance, open counter and waiting below, private offices above',
 'LI':'rectangular library, warm brick and screened reading windows, open stacks and reading, enclosed acoustic group rooms',
 'SC':'rectangular student centre, open ground-floor dining and social concourse, shaded terrace appearance within envelope, acoustic clubs above',
 'IN':'rectangular interdisciplinary centre, robust workshop facade below, shaded dry computing labs above, separate service and pedestrian access',
 'SP':'large rectangular multi-hall sports complex; facade rhythm reveals separate pools and courts, high windows, service louvers; second-floor pool viewing voids',
 'D1':'rectangular student residence, warm beige tile and rose entry accents, repetitive bedroom windows, shaded lobby and screened drying areas',
 'D2':'rectangular student residence, grey ceramic tile and blue entry accents, repetitive bedroom windows, shaded lobby and screened drying areas',
 'D3':'rectangular graduate residence, brick and sage accents, quiet study lounge below and regular bedroom window bays above',
 'D4':'rectangular international residence, pale tile and terracotta accents, bilingual shaded lobby, regular bedroom window bays and shared lounge',
}

def keys_for(bid,r):
    name=r['name'];f=r.get('floor',1)
    if bid=='IN':
        if any(t in name for t in ('計算','討論','安全訓練')) or f==4:return ['computer']
        if '展示' in name:return ['campus_exhibit']
        return {1:['printer3d','maker'],2:['robot','electronics'],3:['robot','electronics'],5:['microscope','campus_material']}[f]
    if r['type']=='class':
        return ['campus_language'] if bid=='T2' else ['campus_business'] if bid=='MA' else None
    if r['type']=='lab':
        if bid=='EE':return ['computer'] if f==3 else ['campus_optics','electronics'] if f==4 else ['electronics','computer']
        if bid=='T4':return ['campus_drafting','printer3d']
        if bid=='T2':return ['computer']
        if bid=='MA':return ['computer']
        if bid in ('IB','E1'):return ['campus_material','microscope'] if f in (2,4) or bid=='IB' else ['maker','electronics']
        if bid=='RB':return ['computer'] if f%2==0 else ['electronics','printer3d']
    if bid=='LI' and r['type']=='study':
        if '借還書' in name:return ['checkout','console','book']
        if '錄音' in name:return ['record','seat']
        if '影音' in name:return ['seat','console']
        if any(t in name for t in ('研討','研究室','自習','共讀','閱讀','休憩')):return ['workstation','seat']
        if '檢索' in name or '資料庫' in name:return ['computer']
        return ['book','workstation','console']
    return None

def specialize(q,bid,r,catalog):
    if bid not in CAMPUS:return
    if bid=='AD' and q.get('programme_kind')=='reception' and not q.get('owner_unit') and not q['equipment_counts'].get('checkout'):
        # Embedding preparation rooms can displace the initial reception desk.
        # Restore it only into an actual free seating bay after subdivision.
        for item in q['equipment_layout']:
            if item['equipment']!='sofa':continue
            dim=list(catalog['checkout'][1]);yaw=item.get('rotation_z_deg',0)
            if yaw%180:dim=[dim[1],dim[0],dim[2]]
            x,y=item['room_local_position_m'][:2];w,d=q['size_m'][:2]
            a=[x-dim[0]/2,y-dim[1]/2,x+dim[0]/2,y+dim[1]/2]
            def overlaps(b):return min(a[2],b[2])-max(a[0],b[0])>1e-6 and min(a[3],b[3])-max(a[1],b[1])>1e-6
            obstacles=[c['bounds_m'] for c in q.get('internal_rooms',[])]+[c['bounds_m'] for c in q.get('internal_corridors',[])]
            for other in q['equipment_layout']:
                if other is item:continue
                px,py=other['room_local_position_m'][:2];sw,sd=other['size_m'][:2]
                obstacles.append([px-sw/2,py-sd/2,px+sw/2,py+sd/2])
            if a[0]>=-w/2 and a[1]>=-d/2 and a[2]<=w/2 and a[3]<=d/2 and not any(overlaps(b) for b in obstacles):
                item.update(equipment='checkout',size_m=dim);break
    replacements=keys_for(bid,r);counts=Counter()
    # Replace only activity fixtures, preserving sinks, support storage, private
    # bathrooms and accommodation. A replacement must fit the prior exact bay.
    candidates={'maker','computer','electronics','microscope','printer3d','robot','shelf','student_station'}
    for item in q['equipment_layout']:
        old=item['equipment']
        if not replacements or old not in candidates:continue
        x,y=item['room_local_position_m'][:2]
        if any(cell['bounds_m'][0]-1e-6<=x<=cell['bounds_m'][2]+1e-6 and cell['bounds_m'][1]-1e-6<=y<=cell['bounds_m'][3]+1e-6 for cell in q.get('internal_rooms',[])):
            continue
        yaw=item.get('rotation_z_deg',0);oldsize=item['size_m']
        fitting=[]
        for k in replacements:
            sz=list(catalog[k][1]);sz=[sz[1],sz[0],sz[2]] if yaw%180 else sz
            if all(sz[i]<=oldsize[i]+1e-6 for i in range(3)):fitting.append((k,sz))
        if fitting:
            k,sz=fitting[sum(counts.values())%len(fitting)]
            item.update(equipment=k,size_m=sz);counts[k]+=1
    q['equipment_counts']=dict(Counter(i['equipment'] for i in q['equipment_layout']))
    # Two actual seats remain in each language/business teaching station.
    if q.get('capacity_students') is not None:
        q['capacity_students']=2*sum(i['equipment'] in ('student_station','campus_language','campus_business') for i in q['equipment_layout'])
    q['campus_design']=dict(building=bid,source_keys=SOURCE_KEYS[bid],typology='specialized teaching, research and public uses',
        fixture_shape_requirements={k:EQUIPMENT[k][2] for k in q['equipment_counts'] if k in EQUIPMENT},
        dimensions_are_game_design=True,geometry_status='dimensioned_control; actual_Tripo_pending')
    if bid=='IN' and r.get('floor')==4:
        q['operational_requirements']=['人工智慧為乾式電腦研究；不放水槽、機器手臂或化學實驗設備','伺服器、配電與散熱另接樓層設備房','座位使用範圍外保留2m通道']
    if bid.startswith('D'):
        q.setdefault('operational_requirements',[]).extend(['住宿區門禁與訪客接待分流','禁止寢室烹調；共享廚房獨立排煙','個人床桌椅衣櫃、共用洗烘及晾曬須逐層核對'])
