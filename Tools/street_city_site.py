"""Compact city layout in real metres; independent of SVG drawing resolution."""
from street_city_dimensions import CORRIDOR_M, SIDE_AISLE_OFFSET_M, MAIN_DOOR_M
WORLD_SIZE_M=1100
MAP_UNITS_PER_M=2
MALL_SIZE_M=[150,100]
EXTRA_SHOPS=[
 ('晨光咖啡','S01'),('校園茶飲','S02'),('巷口麵包房','S03'),('學生家常菜','S04'),
 ('四季鮮果','S05'),('大學生活便利店','S06'),('二手書店','S07'),('毛孩選物','S08'),
 ('綠意花坊','S09'),('青年服飾','S10'),('造型髮廊','S11'),('音樂小舖','S12'),
 ('桌遊聚落','S13'),('底片相館','S14'),('城市單車','S15'),('健康藥妝','S16'),
 ('自助洗衣坊','S17'),('閱光咖啡','S01'),('青葉茶飲','S02'),('奶油甜點屋','S03'),
 ('同學食堂','S04'),('田園果舖','S05'),('日常便利店','S06'),('文學書房','S07'),
 ('鮮花工作坊','S09'),('街頭選衣','S10'),('指尖美髮','S11'),('唱片小室','S12'),
 ('窄巷咖啡','S01'),('純茶研究所','S02'),('晨麥烘焙','S03'),('南街食堂','S04'),
 ('果日選物','S05'),('學園生活站','S06'),('轉角舊書','S07'),('尾巴生活','S08'),
 ('小森林花舖','S09'),('南街衣室','S10'),('日光髮藝','S11'),('拾音唱片','S12'),
 ('骰子桌遊社','S13'),('日常攝影所','S14'),('輪轉單車','S15'),('晴日藥妝','S16'),
 ('洗好日子','S17'),('幸運選號屋','S20'),('慢讀咖啡','S01'),('青山茶研','S02'),
 ('香草甜點房','S03'),('小巷家常菜','S04'),('旬味水果行','S05'),('街坊便利屋','S06'),
 ('午後書房','S07'),('毛球用品社','S08'),('花日工作室','S09'),('新街選衣','S10'),
 ('木梳髮廊','S11'),('湖畔唱片','S12'),('聚友桌遊店','S13'),('銀鹽相機舖','S14'),
 ('前輪單車店','S15'),('南苑藥妝','S16'),('泡泡洗衣屋','S17'),('好運彩券社','S20'),
 ('樹蔭咖啡','S01'),('半糖茶屋','S02'),('酵母麵包所','S03'),('同桌餐館','S04'),
 ('青田鮮果','S05'),('南門便利店','S06'),('頁間書店','S07'),('爪印選物','S08'),
 ('雨後花店','S09'),('日常衣櫥','S10'),('剪影髮藝','S11'),('回聲音樂店','S12')]
STREET_SHOP_IDS=[f'S{i:02d}' for i in range(1,21+len(EXTRA_SHOPS))]
OFFICES=[('O01','澄川商務大樓'),('O02','青年創業中心'),('O03','河岸企業大樓')]

def site_positions(sizes):
    positions={
      'D4':(250,168),'D1':(370,168),'D3':(490,168),'D2':(610,168),
      'SP':(300,330),'SC':(450,405),'E1':(460,295),
      'EE':(620,270),'MA':(620,330),'T2':(710,330),
      'T4':(450,460),'T1':(530,460),'RB':(450,520),'IN':(530,520),
      'LI':(625,520),'AD':(700,430),'IB':(710,520),
      'M1':(655,640),'R1':(125,645),'R2':(125,765),
      'H1':(705,805),'CV':(600,785),'TR':(125,890),
      'O01':(824,620),'O02':(824,690),'O03':(824,760)}
    regular=[b for b in STREET_SHOP_IDS[:48] if b not in ('S18','S19')]
    rows=[regular[:23]+['S18'],regular[23:]+['S19']]
    southern=STREET_SHOP_IDS[48:]
    rows.extend(southern[i:i+24] for i in range(0,len(southern),24))
    row_y=[620,660]+[758+40*i for i in range(len(rows)-2)]
    for bids,y in zip(rows,row_y):
        total=sum(sizes[b][0] for b in bids)+.6*(len(bids)-1);x=367.5-total/2
        for bid in bids:
            w=sizes[bid][0];positions[bid]=(x+w/2,y);x+=w+.6
    return positions,rows


def student_street_frontages(sizes):
    """Metre-space storefronts shared by terrain, floor plans and assembly.

    Paired dense opposing rows avoid dual-front shops with a blocked rear core.
    Each frontage aperture meets its pair's continuous pedestrian street.
    Map Y points south; building-local Y points north.
    """
    from street_city_small_shop import PROFILES
    positions,rows=site_positions(sizes)
    routes=[];frontages={bid:[] for row in rows for bid in row}
    if len(rows)%2:raise ValueError('Student shop rows must be paired')
    for lane,(upper,lower) in enumerate(((i,i+1) for i in range(0,len(rows),2)),1):
        south_edge=max(positions[b][1]+sizes[b][1]/2 for b in rows[upper])
        north_edge=min(positions[b][1]-sizes[b][1]/2 for b in rows[lower])
        if north_edge-south_edge<CORRIDOR_M:
            raise ValueError('Opposing storefronts require a clear pedestrian street at least2m wide')
        y=(south_edge+north_edge)/2
        route=dict(id=f'student-lane-{lane}',centerline_map_m=[[200,y],[535,y]],clear_width_m=CORRIDOR_M,
                   rows=[upper,lower],building_gap_m=north_edge-south_edge)
        routes.append(route)
        for row_index,side,sign,opposite in ((upper,'S',-1,lower),(lower,'N',1,upper)):
            for bid in rows[row_index]:
                w,d=sizes[bid];cx,cy=positions[bid]
                local=[-(w/2-SIDE_AISLE_OFFSET_M),sign*d/2,0]
                revised=bid in PROFILES and [w,d]==PROFILES[bid]['size']
                if revised:local=[(-w/2+3.4)*(-1 if side=='N' else 1),sign*d/2,0]
                portal=[cx+local[0],cy-local[1]]
                frontages[bid].append(dict(id=f'{bid}-entry-front-{side}',facade_side=side,
                    position_m=local,rotation_z_deg=0 if side=='S' else 180,
                    width_m=MAIN_DOOR_M,height_m=2.7,route_id=route['id'],
                    door_map_m=portal,path_map_m=[portal,[portal[0],y]],path_clear_width_m=CORRIDOR_M,
                    opposite_shop_ids=list(rows[opposite]),
                    opens_to='outside',connects_to=f'{bid}-F01-service-aisleA',
                    threshold_clear_width_m=MAIN_DOOR_M,threshold_depth_m=.4,beyond_threshold_clear_width_m=CORRIDOR_M,purpose='street_main',level_transition=False))
                if revised:
                    frontages[bid][-1]['height_m']=2.4
                    frontages[bid][-1]['connects_to']='main_corridor'
    plazas=[]
    for index,row in enumerate(rows):
        left=min(positions[b][0]-sizes[b][0]/2 for b in row);right=max(positions[b][0]+sizes[b][0]/2 for b in row)
        y=positions[row[0]][1];depth=max(sizes[b][1] for b in row)
        for side,a,c in [('W',200,left-2),('E',right+2,535)]:
            if c-a>=4:plazas.append(dict(id=f'student-row-{index+1}-{side}-plaza',bounds_map_m=[a,y-depth/2,c,y+depth/2],name='街口集合與休憩廣場',kind='open_pedestrian_plaza'))
    return dict(routes=routes,frontages=frontages,plazas=plazas)
