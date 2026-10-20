"""Metric sports equipment recipes. Rings and posts have certified primitive sources."""
from street_city_campus_recipes import p,desk,chair,cabinet,shift,shelf
def primitive(aid,size,pos,finish='steel',yaw=0):return dict(primitive=aid,size=list(size),pos=list(pos),finish=finish,yaw=yaw)
def recipe(name,size):
 w,d,h=size
 if name=='空心池體':return [p((w,d,.2),(0,0,0),'tile')]+[p((w,.3,h),(0,y,0),'tile') for y in [-d/2+.15,d/2-.15]]+[p((.3,d-.6,h),(x,0,0),'tile') for x in [-w/2+.15,w/2-.15]]
 if name=='獨立水面':return [p((w,d,.008),(0,0,0),'water')]
 if '護欄' in name or name=='舞蹈把杆':
  axis=0 if w>d else 1;length=max(w,d);result=[p((w,.035,.035) if axis==0 else (.035,d,.035),(0,0,h-.035),'steel')]
  for i in range(int(length/1.2)+2):
   along=-length/2+i*length/(int(length/1.2)+1);result.append(p((.035,.035,h),(along if axis==0 else 0,along if axis==1 else 0,0),'steel'))
  return result
 if name in ('池畔渡假躺椅','恢復睡眠躺椅'):
  return [p((.7,1.35,.045),(0,-.15,.4),'wood'),dict(p((.7,.6,.045),(0,.5,.43),'wood'),pitch=35)]+[p((.05,.05,.4),(x,y,0),'steel') for x in [-.32,.32] for y in [-.65,.55]]
 if name in ('觀眾座椅',):return chair()
 if name=='救生椅':return [p((.08,.08,1.7),(x,y,0),'steel') for x in [-.4,.4] for y in [-.4,.4]]+shift(chair(),z=1.35)+[p((.8,.08,.08),(0,-.5,z),'steel') for z in [.2,.55,.9,1.25]]
 if name=='池梯':return [p((.045,.045,h),(x,0,0),'steel') for x in [-.3,.3]]+[p((.65,.16,.035),(0,0,z),'steel') for z in [.25,.55,.85,1.15,1.45,1.75,2.05]]
 if name=='籃球架':
  return [p((1.2,1.5,.18),(0,1,0),'steel'),p((.16,.16,3.5),(0,1,.18),'steel'),p((.12,2,.12),(0,.1,3.5),'steel'),p((1.8,.06,1.05),(0,-.85,2.9),'glass'),primitive('ring-low',(.48,.48,.025),(0,-1.15,3.05),'orange')]+[p((.01,.01,.35),(x,-1.15+y,2.7),'net') for x,y in [(-.2,0),(.2,0),(0,-.2),(0,.2)]]
 if name in ('球網','桌球網'):
  return [p((w,.012,.012),(0,0,h),'net'),p((w,.008,h),(0,0,0),'netgrid')]
 if name=='球柱':return [primitive('cylinder',(.08,.08,h),(0,0,0),'steel')]
 if name=='桌球桌':return desk(w,d,.76,'court')+[p((.02,d,.002),(0,0,.76),'ivory')]
 if name=='壁球場空心隔牆組':return [p((w,.1,h),(0,y,0),'paint') for y in [-d/2+.05,d/2-.05]]+[p((.1,d,h),(x,0,0),'glass' if x>0 else 'paint') for x in [-w/2+.05,w/2-.05]]
 if name=='足球球門':return [p((.08,.08,h),(x,0,0),'ivory') for x in [-w/2+.04,w/2-.04]]+[p((w,.08,.08),(0,0,h-.08),'ivory')]+shift(recipe('球網',(w,.05,h)),y=d/2)
 if name in ('重量訓練站','功能訓練架'):
  return [p((.08,.08,h),(x,y,0),'steel') for x in [-w/2+.1,w/2-.1] for y in [-d/2+.1,d/2-.1]]+[p((w,.08,.08),(0,y,h-.08),'steel') for y in [-d/2+.1,d/2-.1]]+[p((.7,1.2,.12),(0,0,.45),'black'),p((.08,.08,1.8),(0,d/2-.1,0),'steel')]+[p((.7,.4,.06),(0,d/2-.2,z),'black') for z in [.2,.3,.4,.5,.6,.7]]
 if name=='跑步機':return [p((w,d,.16),(0,0,0),'black'),p((w-.2,d-.3,.02),(0,0,.16),'rubber')]+[p((.06,.06,1.2),(x,.65,.16),'steel') for x in [-.4,.4]]+[p((.8,.35,.08),(0,.65,1.4),'black'),p((.65,.25,.008),(0,.65,1.48),'screen')]
 if name=='拳擊台含緩衝':return [p((w,d,.5),(0,0,0),'fabric')]+[p((.1,.1,h),(x,y,.5),'steel') for x in [-w/2+.1,w/2-.1] for y in [-d/2+.1,d/2-.1]]+[p((w,.025,.025),(0,y,z),'net') for y in [-d/2+.1,d/2-.1] for z in [.85,1.15,1.45]]+[p((.025,d,.025),(x,0,z),'net') for x in [-w/2+.1,w/2-.1] for z in [.85,1.15,1.45]]
 if name in ('復健治療床','皮拉提斯器械'):return desk(w,d,.5,'fabric')+[p((w-.2,d-.15,.12),(0,0,.5),'fabric')]
 if name=='體適能測試工作站':return desk(w,d,.75)+[p((.4,.04,1.2),(0,d/2-.1,.75),'black')]
 if name in ('瑜伽墊','武術軟墊區','落地保護墊'):return [p((w,d,h),(0,0,0),'fabric')]
 if name=='攀岩牆':return [p((w,.15,h),(0,.2,0),'drafting')]+[p((.16,.15,.1),(-w/2+.5+(i*1.37)%(w-1),.07,.5+(i*1.13)%(h-1)),'book'+str(i%3)) for i in range(int(w*h/2))]
 if name=='確保器材架':return shelf(w,d,h)
 if name=='飛輪車':return None # independent Tripo stationary bicycle
 raise ValueError('Unknown sporting prop '+name)
