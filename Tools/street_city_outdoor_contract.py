"""Metric outdoor programme. Coordinates share the existing campus world."""
import math,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'output/street-social-city-production/campus-v1/outdoor'
R=55.0
S=(800-2*math.pi*(R+.3))/2
FACILITIES=[
 dict(id='stadium',name='河東運動園區 · 800m一圈',origin=[1270,772,12],size=[150,376],lane_width=1.22,lanes=8,curb_radius=R,straight=S,
 tennis=[[66-90,685-772],[114-90,685-772],[66-90,727-772],[114-90,727-772]],
 volleyball=[[44-90,797-772],[136-90,797-772],[44-90,843-772],[136-90,843-772]]),
 dict(id='reading-park',name='學生閱讀公園',origin=[545,800,20.06],size=[42,66]),
 dict(id='active-park',name='學生活動公園',origin=[706,844,20.06],size=[72,66]),
 dict(id='connection',name='橋頭至運動園區緩坡步道',origin=[1020,775,20],size=[175,4]),
 dict(id='annex-ground',name='河東擴充地形與操場基底',origin=[1240,772,0],size=[280,406]),
 dict(id='campus-links',name='學生公園連接步道',origin=[625.5,844.25,20.06],size=[164,91.5]),
 dict(id='campus-track',name='校內操場 · 400m一圈／足球場',origin=[300,606,20.06],size=[194,106],track_lap_m=400,curb_radius=36.5,straight=(400-2*math.pi*36.8)/2,lanes=8,lane_width=1.22,long_axis='X',pitch_m=[90,60],foundation_depth_m=3),
 dict(id='campus-track-access',name='校內操場至校園主步道',origin=[349.25,661,20.06],size=[101.5,11])]
def publish():
 OUT.mkdir(parents=True,exist_ok=True)
 d=dict(schema=1,facilities=FACILITIES,world_bounds_m=[0,0,1380,1100],adaptation='800m single-lap noncompetition game expansion, not a surveyed NTUST replica',ramp_slope=1/20,minimum_path_width_m=2)
 (OUT/'programme.json').write_text(json.dumps(d,ensure_ascii=False,indent=2),'utf8')
 return d
