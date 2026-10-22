"""Named outdoor facilities tied to the current terrain, not obsolete 2200m positions."""
import json,math,random
from pathlib import Path
import numpy as np
from street_city_parts import walls
def sample(array,x,y):
 x=max(0,min(1100,float(x)));y=max(0,min(1100,float(y)));i,j=int(x),int(y);ii,jj=min(1100,i+1),min(1100,j+1);u,v=x-i,y-j
 return float((1-u)*(1-v)*array[j,i]+u*(1-v)*array[j,ii]+(1-u)*v*array[jj,i]+u*v*array[jj,ii])
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'output/street-social-city-production/world-completion-v1/outdoor'
BASE=OUT.parent.parent

def programme():
 layout=json.loads((ROOT/'Docs/StreetSocialCity/world-terrain-layout.json').read_text('utf8'));effective=json.loads((ROOT/'Docs/StreetSocialCity/production/effective-city-plan.json').read_text('utf8'));names={r['id']:r['name'] for r in effective['rooms'] if r['building']=='PK'}
 field=np.load(BASE/'world-ground-v1/world-field.npz');height=field['height_map'];rows=[]
 for f in layout['features']:
  if not f['id'].startswith('PK-'):continue
  w=f['world_center_m'];x,y=w['east'],w['north'];fid=f['id'];origin=[x,y,sample(height,x,1100-y)+.04]
  if fid=='PK-0111':origin=[85,1033,sample(height,85,67)+.04]
  if fid=='PK-0113':origin=[600,635,sample(height,600,465)+.04]
  q=dict(id=fid,name=names[fid],origin=origin,size=[24,18],parts=[],curves=[],features=[],minimum_path_m=2)
  if fid=='PK-0114':q.update(existing_asset='stadium',origin=[1270,772,12],existing_feature='four full-size tennis courts')
  if fid=='PK-0115':q.update(existing_asset='campus-track',origin=[300,606,20.06],existing_feature='400m track and football pitch; 800m annex retained separately')
  rows.append(q)
 return rows,field

def design():
 rows,field=programme();height=field['height_map']
 for q in rows:
  if q.get('existing_asset'):continue
  P=q['parts'];C=q['curves'];F=q['features'];fid=q['id'];x0,y0,z0=q['origin']
  def box(name,size,pos,mat='wood',yaw=0,pitch=0,primitive='panel'):
   P.append(dict(name=name,size=list(size),pos=list(pos),finish=mat,yaw=yaw,pitch=pitch,primitive=primitive))
  def curve(asset,pos,yaw=0,scale=1):C.append(dict(asset=asset,pos=list(pos),yaw=yaw,scale=scale))
  def bench(x,y):
   box('bench-seat',[1.8,.5,.06],[x,y,.43]);box('bench-back',[1.8,.05,.5],[x,y+.22,.49])
   for dx in [-.65,.65]:box('bench-leg',[.06,.4,.43],[x+dx,y,0],'steel')
  def table(x,y):
   box('picnic-table',[1.8,.9,.06],[x,y,.74])
   for dx in [-.65,.65]:box('picnic-frame',[.08,.8,.74],[x+dx,y,0],'steel')
   bench(x,y-1);bench(x,y+1)
  def rail(x,y,w,yaw=0):
   for i in range(int(w/1.2)+2):
    off=-w/2+i*w/(int(w/1.2)+1);aa=math.radians(yaw);box('guard-post',[.05,.05,1.15],[x+off*math.cos(aa),y+off*math.sin(aa),0],'steel')
   box('guard-top',[w,.06,.06],[x,y,1.12],'steel',yaw)
  def roof(x,y,w,d,z):
   run=d/2;rise=1.25;length=math.hypot(run,rise);angle=math.degrees(math.atan2(rise,run))
   for sign in [-1,1]:box('pitched-roof',[w,length,.12],[x,y+sign*d/4,z+rise/2],'roof',pitch=-sign*angle)
   box('ridge',[w,.2,.2],[x,y,z+rise],'roof')
  def pavilion(x,y,w=7,d=5):
   for dx in [-w/2+.2,w/2-.2]:
    for dy in [-d/2+.2,d/2-.2]:box('timber-column',[.2,.2,3],[x+dx,y+dy,0])
   roof(x,y,w+.8,d+.8,3);bench(x,y+d/2-.7)
  def pad(w=24,d=18):
   # A walkable platform is supported down to actual terrain at sampled supports.
   corners=[sample(height,x0+dx,1100-y0-dy) for dx in [-w/2,w/2] for dy in [-d/2,d/2]]
   q['origin'][2]=max([z0,*corners])+.08
   box('supported-platform',[w,d,.16],[0,0,-.16],'stone')
   for dx in [-w/2+1+j*(w-2)/4 for j in range(5)]:
    for dy in [-d/2+1+j*(d-2)/3 for j in range(4)]:
     low=sample(height,x0+dx,1100-y0-dy)-q['origin'][2];box('foundation-pier',[.3,.3,max(.2,-low)],[dx,dy,min(low,-.2)],'stone')
   for dy in [-d/2+1+j*(d-2)/3 for j in range(4)]:box('deck-support-beam',[w,.25,.35],[0,dy,-.51],'wood')
   start=sample(height,x0,1100-y0+d/2+3)-q['origin'][2]
   P.append(dict(name='entry-grade-ramp',size=[3,3,.12],pos=[0,-d/2-1.5,min(start,0)-.12],finish='stone',yaw=0,pitch=0,primitive='panel',slope_y=-start/3,slope_center_z=start/2-.12))
   F.append(dict(kind='supported_metric_platform',size=[w,d],terrain_corners_m=corners,clear_path_m=2))
  def bathroom(x,y):
   for j in range(3):
    cx=x+(j-1)*2.5
    for a in walls([2.1,3.4,2.7],[dict(side='south',local_position_m=[0,0,0],width_m=1.2,height_m=2.3)]):box('camp-sanitary-wall',a['size_m'],[a['position_m'][0]+cx,a['position_m'][1]+y,a['position_m'][2]],'tile')
    box('sanitary-roof',[2.5,3.8,.12],[cx,y,2.7],'roof')
    for dx in [-.64,.64]:box('sanitary-door-jamb',[.08,.12,2.3],[cx+dx,y-1.75,0])
    box('sanitary-door-lintel',[1.36,.12,.08],[cx,y-1.75,2.3])
    box('sanitary-open-door',[.04,1.196,2.27],[cx-.65,y-1.1,0])
    if j==0:curve('toilet',[cx-.5,y+1.15,0]);curve('basin',[cx+.55,y+1.2,.45],0)
    else:
     box('shower-pipe',[.03,.03,1.5],[cx,y+1.5,.65],'steel');box('shower-head',[.2,.2,.04],[cx,y+1.35,2.1],'steel');box('shower-tray',[1.6,1.6,.05],[cx,y+.8,0],'tile')
   F.append(dict(kind='camp_wc_basin_two_showers',door_clear_m=1.2,approach_width_m=2))
  # All node entries reserve a 2m unobstructed central route.
  if fid not in ('PK-0107','PK-0109'):pad()
  for xx in [-10,10]:
   box('lamp-post',[.09,.09,3.6],[xx,-7,0],'steel');box('lamp-head',[.65,.45,.08],[xx,-7,3.6],'light');bench(xx,5)
  if fid in ('PK-0101','PK-0102','PK-0103','PK-0104','PK-0105','PK-0110'):
   if fid in ('PK-0102','PK-0110'):
    for xx in [-6,6]:box('lawn',[8,12,.015],[xx,0,.005],'grass')
    for xx in [-6,6]:table(xx,2)
   else:pavilion(0,3,7,5)
   for xx in [-9,9]:curve('camphor',[xx,0,0],scale=.6)
   F.append(dict(kind='public_plaza_and_seating',clear_entrance_width_m=2))
  elif fid in ('PK-0106','PK-0108'):
   for i in range(36):box('deck-plank',[24,.47,.08],[0,-8.5+i*.48,-.02])
   rail(0,8,23);rail(-11.5,1,14,90);rail(11.5,1,14,90)
   for xx in [-7,-3,3,7]:box('fishing-rod-rest',[.05,.05,.85],[xx,7.4,0],'steel')
   F.append(dict(kind='waterfront_deck',guard_height_m=1.15,open_entry_width_m=3))
  elif fid in ('PK-0107','PK-0109'):
   # Terrain-following 4m path: every 2m segment uses endpoint heights, no buried flat ribbon.
   for j in range(20):
    y=-20+j*2;z1=sample(height,x0,1100-y0-y)-z0;z2=sample(height,x0,1100-y0-y-2)-z0
    P.append(dict(name='graded-path',size=[4,2,.1],pos=[0,y+1,min(z1,z2)-.1],finish='stone',yaw=0,pitch=0,primitive='panel',slope_y=(z2-z1)/2,slope_center_z=(z1+z2)/2-.1))
   for y in [-15,-5,5,15]:
    for x in [-6,6]:curve('cherry' if fid=='PK-0109' else 'camphor',[x,y,sample(height,x0+x,1100-y0-y)-z0],scale=.8)
   F.append(dict(kind='terrain_following_promena​de',width_m=4,length_m=40))
  elif fid=='PK-0111':
   for xx in [-2.5,2.5]:box('torii-post',[.35,.35,4],[xx,-6,0])
   box('torii-crossbeam',[6.8,.4,.35],[0,-6,3.8]);box('torii-tie',[6,.25,.18],[0,-6,2.9])
   pavilion(0,4,8,6)
   for i in range(7):box('shrine-rear-screen',[.8,.1,2.4],[-3+i,6.8,.3])
   box('offering-table',[2,.8,.7],[0,5.5,0]);box('chozu-bottom',[1.2,.8,.12],[-7,-1,0],'stone')
   for dx in [-.54,.54]:box('chozu-side',[.12,.8,.48],[-7+dx,-1,.12],'stone')
   for dy in [-.34,.34]:box('chozu-rim',[.96,.12,.48],[-7,-1+dy,.12],'stone')
   box('chozu-spout',[.6,.04,.04],[-7,-.9,.78],'wood')
   for y in [-3,2]:
    for x in [-4.5,4.5]:box('stone-lantern-column',[.4,.4,1],[x,y,0],'stone');box('stone-lantern-head',[.8,.8,.3],[x,y,1],'stone')
   F.append(dict(kind='shrine',elements=['torii','haiden','offering-table','handwashing-basin','stone-lanterns'],reference='Taoyuan shrine, fictional small timber adaptation'))
  elif fid=='PK-0112':
   box('performance-stage',[14,7,.45],[0,4,0]);pavilion(0,4,14,7)
   for y in [-5,-2]:
    for x in [-6,-3,3,6]:bench(x,y)
   for i in range(3):box('stage-step',[8,.35,.15*(i+1)],[0,.15+i*.35,0])
   P.append(dict(name='stage-side-ramp',primitive='panel',size=[2,5.5,.12],pos=[8.2,-2.25,-.12],finish='stone',yaw=0,pitch=0,slope_y=.45/5.5,slope_center_z=.225-.12))
   box('stage-side-landing',[2.7,2,.12],[7.85,1.5,.33],'wood')
   F.append(dict(kind='covered_stage_with_audience',stage_m=[14,7],ramp_width_m=2,ramp_length_m=5.5,ramp_rise_m=.45,access='three 150mm steps and a continuous side ramp'))
  elif fid=='PK-0113':
   q['size']=[36,23];P.clear();C.clear();pad(36,23)
   for xx in [-16,16]:bench(xx,0);box('court-light',[.1,.1,6],[xx,8,0],'steel');box('court-floodlight',[1,.5,.1],[xx,8,6],'light')
   box('court',[28,15,.01],[0,0,.005],'court')
   for y in [-7.5,7.5]:box('sideline',[28,.05,.005],[0,y,.02],'white')
   for x in [-14,0,14]:box('end-and-centre-line',[.05,15,.005],[x,0,.02],'white')
   for x,sign in [(-12.8,-1),(12.8,1)]:
    box('basket-post',[.16,.16,3.9],[x+sign*.5,0,0],'steel');box('backboard',[.06,1.8,1.05],[x,0,2.9],'glass');P.append(dict(name='basket-hoop',primitive='ring-low',size=[.48,.48,.025],pos=[x-sign*.3,0,3.05],finish='orange',yaw=0,pitch=0))
   F.append(dict(kind='basketball',playing_m=[28,15],runoff_m=2))
  elif fid=='PK-0116':
   pavilion(0,4,10,6)
   for x in [-8,-6,-4,4,6,8]:curve('bicycle',[x,2,0],90);box('cycle-dock',[.08,1,.8],[x,2,0],'steel')
   box('rental-kiosk',[1.2,.5,1.2],[0,5,0],'steel');F.append(dict(kind='bicycle_rental',bikes=6,central_clearance_m=3))
  elif fid=='PK-0117':pavilion(0,2,10,7);rail(0,7,22);F.append(dict(kind='viewing_pavilion',roof_m=[10.8,7.8],guard_m=1.15))
  elif fid=='PK-0118':
   for y in [-5,5]:
    for x in [-8,-4,4,8]:
     box('market-counter',[2.8,1,.9],[x,y,0],'wood');roof(x,y,3.2,2.2,2.4)
     for xx in [-1.3,1.3]:box('stall-post',[.06,.06,2.4],[x+xx,y,0],'steel')
     curve('product-meal',[x,y,.9],scale=1);curve('product-tea',[x+.6,y,.9],scale=1)
   F.append(dict(kind='night_market',stalls=8,centre_aisle_m=6))
  elif fid in ('PK-0119','PK-0120'):
   for x in [-5,5]:curve('tent',[x,1,0],180);box('sleeping-mat',[.65,1.9,.04],[x-.4,1,.03],'fabric');box('sleeping-mat',[.65,1.9,.04],[x+.4,1,.03],'fabric')
   table(7,-4);bathroom(0,6)
   F.append(dict(kind='campsite',tents=2,sleeping_mats=4,platform_m=[24,18],sanitary_blocks=1,water_and_wastewater_connections_reserved=True))
  q['sources_policy']='Only actual Rust-certified component vertices are instantiated; all new Tripo components require remesh gates.'
 return rows

if __name__=='__main__':
 OUT.mkdir(parents=True,exist_ok=True);rows=design();(OUT/'programme.json').write_text(json.dumps(dict(schema=1,nodes=rows,scope='physical facilities only; game interactions deferred'),ensure_ascii=False,indent=2),'utf8');print(len(rows),'named nodes')
