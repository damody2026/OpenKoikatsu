"""Open-legged furniture and explicit equipment, assembled from remeshed boards.

Every panel uses the certified unit-panel source; cut sizes are instance
transforms. Curved sanitary objects are independent Tripo sources, never boxes.
"""
import math
def p(size,pos,finish='wood',yaw=0):return dict(size=list(size),pos=list(pos),finish=finish,yaw=yaw)
def shift(parts,x=0,y=0,z=0,yaw=0):
 c,s=math.cos(math.radians(yaw)),math.sin(math.radians(yaw))
 return [dict(q,pos=[x+q['pos'][0]*c-q['pos'][1]*s,y+q['pos'][0]*s+q['pos'][1]*c,z+q['pos'][2]],yaw=q.get('yaw',0)+yaw) for q in parts]
def desk(w=1.2,d=.6,h=.75,finish='wood'):
 return [p((w,d,.04),(0,0,h-.04),finish)]+[p((.04,.04,h-.04),(x,y,0),'steel') for x in [-w/2+.05,w/2-.05] for y in [-d/2+.05,d/2-.05]]
def chair():
 return [p((.44,.44,.035),(0,0,.44)),p((.44,.035,.32),(0,.205,.64))]+[p((.035,.035,.44),(x,-.18,0),'steel') for x in [-.18,.18]]+[p((.035,.035,.96),(x,.205,0),'steel') for x in [-.2,.2]]
def shelf(w=1.8,d=.4,h=1.8,books=False):
 q=[p((.035,d,h),(x,0,0)) for x in [-w/2+.0175,w/2-.0175]]+[p((w,d,.035),(0,0,z)) for z in [0,.44,.88,1.32,h-.035]]
 if books:
  for row in range(4):
   for col in range(6):q.append(p((.24,.25,.3),(-w/2+.16+col*(w-.32)/5,0,row*.44+.04),'book'+str(col%3)))
 return q
def cabinet(w=.65,d=.6,h=1.9):
 return [p((w,d,.035),(0,0,z)) for z in [0,h-.035]]+[p((.035,d,h),(x,0,0)) for x in [-w/2+.0175,w/2-.0175]]+[p((w,.025,h),(0,d/2-.0125,0))]+[p((w/2-.02,.025,h-.08),(x,-d/2,.04)) for x in [-w/4,w/4]]+[p((.02,.035,.16),(x,-d/2-.025,h/2),'steel') for x in [-.05,.05]]
def terminal():return [p((.28,.2,.025),(0,0,0),'black'),p((.025,.025,.2),(0,.03,.025),'steel'),p((.44,.035,.28),(0,.03,.225),'black'),p((.4,.008,.24),(0,.007,.245),'screen')]
def cubicle(w=1.4,d=1.9,h=2.4):
 return [p((.05,d,h),(x,0,0),'paint') for x in [-w/2+.025,w/2-.025]]+[p((w,.05,h),(0,d/2-.025,0),'paint'),p((w,.05,.2),(0,-d/2+.025,h-.2),'paint')]+[p(((w-1.2)/2,.05,h-.2),(x,-d/2+.025,0),'paint') for x in [-(w+1.2)/4,(w+1.2)/4]]
def bunk():
 return [p((.055,.055,1.9),(x,y,0),'steel') for x in [-.49,.49] for y in [-.98,.98]]+[p((.99,1.98,.07),(0,0,z),'steel') for z in [.18,1.18]]+[p((.91,1.88,.12),(0,0,z),'fabric') for z in [.25,1.25]]+[p((.6,.32,.08),(0,.7,z),'fabric') for z in [.37,1.37]]+[p((.035,1.98,.035),(x,0,1.75),'steel') for x in [-.49,.49]]+[p((.035,.035,1.45),(x,-1.02,0),'steel') for x in [-.2,.2]]+[p((.44,.035,.035),(0,-1.02,z),'steel') for z in [.16,.43,.7,.97,1.24]]
def shower(w=1.2,d=1.4,h=2.4):
 return [p((w,d,.04),(0,0,0),'tile'),p((w,.05,h),(0,d/2-.025,0),'tile')]+[p((.05,d,h),(x,0,0),'glass') for x in [-w/2+.025,w/2-.025]]+[p((.025,.025,1.1),(.15,d/2-.07,1),'steel'),p((.18,.18,.025),(.15,d/2-.18,2.15),'steel'),p((.06,.04,.16),(.15,d/2-.06,1.05),'steel')]

def recipe(aid,size):
 key=aid.split('COMMON-CITY-')[-1].split('COMMON-SPORT-')[-1].split('COMMON-WC-')[-1];w,d,h=size
 if key in ('student_station','campus_language','campus_business'):
  # Two-place fixed lecture bench: two human seating places share their
  # support, as in a real teaching bench. Avoid hundreds of redundant legs.
  q=[p((1.2,.55,.04),(0,.3,.71))]+[p((.04,.5,.71),(x,.3,0),'steel') for x in [-.53,.53]]
  q+=[p((1.08,.42,.04),(0,-.35,.44)),p((1.08,.035,.32),(0,-.56,.63))]
  q+=[p((.04,.04,.76),(x,-.54,0),'steel') for x in [-.48,.48]]+[p((.04,.5,.04),(x,-.35,0),'steel') for x in [-.48,.48]]
  if key!='student_station':
   q+=shift(terminal(),z=.75,y=.3)
  return q
 if key in ('study_desk','table','worktop','prep','maker','campus_drafting'):
  q=desk(w,d,min(.9,h))
  if key=='campus_drafting':q+=[p((w-.2,d-.2,.025),(0,0,.9),'drafting')]
  return q
 if key in ('workstation','computer','campus_optics','campus_material','electronics','microscope'):
  q=desk(w,min(d,.75),.75 if key in ('workstation','computer') else .9)+shift(chair(),y=-d/2+.25,yaw=180)
  q+=shift(terminal(),y=.18,z=.75 if key in ('workstation','computer') else .9)
  if key in ('electronics','campus_optics','campus_material','microscope'):
   q+=[p((.35,.25,.22),(-w/3,.08,.9),'ivory'),p((.26,.015,.12),(-w/3,-.052,.97),'screen'),p((.6,.3,.03),(w/4,0,.9),'black')]
   if key=='campus_optics':q+=[p((.04,.04,.25),(w/4+i*.16,0,.93),'steel') for i in [-1,0,1]]
  return q
 if key in ('shelf','book','campus_exhibit','rack'):return shelf(w,d,h,key=='book')
 if key=='locker':return [p((w,d,h),(0,0,0),'steel'),p((w-.025,.012,h-.025),(0,-d/2-.008,.012),'ivory'),p((.02,.03,.12),(w/3,-d/2-.03,h*.55),'black')]
 if key in ('campus_wardrobe','wardrobe','aid_cabinet','chemical'):return cabinet(w,d,h)
 if key=='bunk':return bunk()
 if key in ('standard_cubicle','wc_cubicle','accessible_cubicle','accessible_wc'):return cubicle(w,d,h)
 if key in ('shower','family_shower'):return shower(w,d,h)
 if key in ('seat',):return chair()
 if key=='dining':return desk(1.4,.75)+shift(chair(),x=-.45,y=.65)+shift(chair(),x=.45,y=.65)+shift(chair(),x=-.45,y=-.65,yaw=180)+shift(chair(),x=.45,y=-.65,yaw=180)
 if key in ('sofa','change_bench','treatment','medical'):
  q=[p((w,d,.12),(0,0,.35),'fabric')]+[p((.04,.04,.35),(x,y,0),'steel') for x in [-w/2+.1,w/2-.1] for y in [-d/2+.1,d/2-.1]]
  if key=='sofa':q+=[p((w,.12,.5),(0,d/2-.06,.4),'fabric')]+[p((.12,d,.25),(x,0,.45),'fabric') for x in [-w/2+.06,w/2-.06]]
  if key in ('treatment','medical'):q+=[p((.5,.3,.08),(0,d/2-.15,.47),'fabric')]
  return q
 if key in ('lectern','checkout','console','record'):
  q=desk(w,d,min(.95,h))
  if key in ('lectern','checkout'):q+=[p((w,.03,.7),(0,-d/2,.2),'wood')]
  return q+shift(terminal(),z=min(.95,h))
 if key in ('fridge','cookline','water'):
  q=[dict(part,finish='ivory' if part['finish']=='wood' else part['finish']) for part in cabinet(w,d,h)]
  if key=='fridge':
   for i in range(3):q.append(p((w-.1,.015,.025),(0,-d/2-.018,.15+i*.04),'black'))
  if key=='cookline':q=[dict(part,finish='steel') for part in cabinet(w,d,.85)]+[p((w,d,.05),(0,0,.85),'steel'),p((w,d,.15),(0,0,1.85),'steel'),p((.5,.4,.2),(0,d/4,2),'steel')]+[p((.35,.35,.025),(x,0,.9),'black') for x in [-w/3,0,w/3]]
  if key=='water':q+=[p((.1,.04,.08),(x,-d/2-.02,1.05),'black') for x in [-.15,.15]]+[p((.35,.15,.02),(0,-d/2-.07,.88),'steel')]
  return q
 if key=='printer3d':
  q=desk(w,d,.75)
  q+=[p((.035,.035,.9),(x,y,.75),'steel') for x in [-.55,.55] for y in [-.4,.4]]+[p((1.15,.85,.035),(0,0,1.62),'steel'),p((.8,.65,.025),(0,0,.87),'black'),p((1.1,.035,.035),(0,0,1.4),'steel'),p((.1,.1,.15),(0,0,1.23),'black')]
  return q
 if key=='robot':
  return [p((2,2,.08),(0,0,0),'steel'),p((.5,.5,.5),(0,0,.08),'ivory'),p((.25,.25,1),(0,0,.58),'ivory'),p((1.2,.22,.22),(.5,0,1.5),'ivory'),p((.18,.18,.6),(1,0,.9),'ivory')]+[p((.08,.1,.2),(1,y,.72),'steel') for y in [-.12,.12]]
 if key=='piano':
  return cabinet(w,d,.8)+[p((w,.15,.4),(0,d/2-.075,.8),'black')]+[p((.035,.28,.025),(-w/2+.05+i*.04,-d/2-.04,.78),'ivory') for i in range(int((w-.1)/.04))]
 if key in ('mat',):return [p((w,d,h),(0,0,0),'fabric')]
 if key in ('mirror',):return [p((w,d,h),(0,0,0),'wood'),p((w-.05,.01,h-.05),(0,-d/2-.005,.025),'mirror')]
 if key=='bin':return cabinet(w,d,h)
 if key=='grab_rails':return [p((w,.035,.035),(0,0,.35),'steel')]+[p((.035,.1,.35),(x,0,0),'steel') for x in [-w/2+.02,w/2-.02]]
 if key in ('pump','filter'):
  return [p((w,d,.12),(0,0,0),'steel')]+[dict(primitive='cylinder',size=[min(w/3,d-.2),min(w/3,d-.2),h-.2],pos=[x,0,.12],finish='court') for x in [-w/3,w/3]]+[p((w,.1,.1),(0,-d/2+.1,h/2),'steel')]
 if '-fixture' in aid:
  return cabinet(w,d,h)
 if key in ('sink','basin','toilet','urinal'):return None # real curved components, separately loaded
 raise ValueError('Unknown campus equipment: '+aid)
