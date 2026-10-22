"""Deterministic low-cost street dressing in the adopted metre coordinate system."""
import json,math,hashlib,random
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];BASE=ROOT/'output/street-social-city-production';OUT=BASE/'streetscape-v1'
def read(p):return json.loads(Path(p).read_text('utf8'))
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def write(p,x):Path(p).parent.mkdir(parents=True,exist_ok=True);Path(p).write_text(json.dumps(x,ensure_ascii=False,indent=2),'utf8')
def sample(a,x,y):
 x=max(0,min(1100,float(x)));y=max(0,min(1100,float(y)));i,j=int(x),int(y);u,v=x-i,y-j
 return float((1-u)*(1-v)*a[j,i]+u*(1-v)*a[j,min(i+1,1100)]+(1-u)*v*a[min(j+1,1100),i]+u*v*a[min(j+1,1100),min(i+1,1100)])
def distance(p,a,b):
 dx,dy=b[0]-a[0],b[1]-a[1];t=max(0,min(1,((p[0]-a[0])*dx+(p[1]-a[1])*dy)/max(1e-10,dx*dx+dy*dy)))
 return math.hypot(p[0]-a[0]-t*dx,p[1]-a[1]-t*dy)
def asset_definitions():
 # Bounds are actual construction envelopes, including appendages.
 return {
 'camphor':([7,6.5,8],2600,'tree'),'cherry':([5.5,5,6],2600,'tree'),
 'bicycle':([.6,1.8,1.1],13000,'reuse'),'flowers':([.55,.45,.8],800,'reuse'),'bed-flowers':([.4,.4,.4],800,'reuse'),
 'scooter':([.68,1.8,1.15],6000,'tripo'),
 'bench-oak':([1.9,.62,.94],600,'panel'),'bench-stone':([2,.6,.5],180,'panel'),
 'planter-long':([2.8,.8,.58],300,'panel'),'planter-square':([1.2,1.2,.65],240,'panel'),
 'tree-grate':([2.4,2.4,.08],600,'panel'),'lamp-modern':([1.3,.5,5.2],300,'panel'),
 'lamp-lantern':([.55,.55,3.5],300,'panel'),'bollard':([.16,.16,.8],100,'panel'),
 'recycling':([1.35,.5,1.1],400,'panel'),'cycle-rack':([2.4,.6,.8],240,'panel'),
 'drain':([1,.35,.025],260,'panel'),'utility-box':([.8,.45,1.25],240,'panel'),
 'bus-shelter':([4,1.8,2.8],900,'panel'),'paving':([2,2,.06],100,'panel'),
 'curb':([2,.22,.16],100,'panel'),'tactile':([.4,2,.015],240,'panel'),
 'parking-mark':([.75,2,.008],100,'panel'),'railing':([2,.12,1.1],180,'panel'),
 'tree-trunk-collision':([.32,.32,2.5],100,'panel'),
 'lane-dash':([3,.12,.003],20,'panel'),'crosswalk':([3,3.6,.003],180,'panel'),
 'traffic-light':([.45,.45,3.2],240,'panel')}
def design():
 import numpy as np
 with np.load(BASE/'world-ground-v1/world-field.npz') as compressed:field={k:compressed[k] for k in ['height_map','water_map']}
 buildings=read(ROOT/'Docs/StreetSocialCity/production/building-floor-audit.json')['buildings'];routes=read(BASE/'world-ground-v1/ground-plan.json')['roads'];rng=random.Random(20261020);segments=[];entries=[];approaches=[]
 for b in buildings:
  x,y,z=b['world_position_m']
  for e in b['entrances']:
   p=e.get('position_m',e.get('local_position_m'))
   if p:entries.append([x+p[0],y+p[1],b['id']])
   path=e.get('path_map_m')
   if path:
    path=[[q[0],1100-q[1]] for q in path]
    approaches.extend((a,c,2,'entry',b['id']) for a,c in zip(path,path[1:]))
 for route in routes:
  points=[[p[0],1100-p[1]] for p in route['points_map_m']]
  for a,b in zip(points,points[1:]):
   if math.dist(a,b)>.05:segments.append((a,b,route['width_m'],route['kind'],route['id']))
 segments+=approaches;grid={}
 for seg in segments:
  a,b,w,_,_=seg;pad=w/2+10
  for gx in range(math.floor((min(a[0],b[0])-pad)/30),math.floor((max(a[0],b[0])+pad)/30)+1):
   for gy in range(math.floor((min(a[1],b[1])-pad)/30),math.floor((max(a[1],b[1])+pad)/30)+1):grid.setdefault((gx,gy),[]).append(seg)
 facilities=read(BASE/'world-completion-v1/outdoor/assembly-report.json')['nodes']
 specs=asset_definitions();rows=[];rejected={};occupied=[]
 def reject(reason):rejected[reason]=rejected.get(reason,0)+1;return False
 def place(asset,x,y,yaw=0,scale=1,source='street',check=True,z=None,zone=None):
  sx,sy,sz=specs[asset][0];sc=[scale]*3 if isinstance(scale,(int,float)) else scale;sx*=sc[0];sy*=sc[1];sz*=sc[2]
  footprint=max(sx,sy)/2;radius=max(.18,footprint if asset not in ('camphor','cherry') else .55)
  if not (2<x<1378 and 2<y<1098):return reject('outside_world')
  if check:
   if x>1098:return reject('annex_reserved')
   iy,ix=round(1100-y),round(x)
   if field['water_map'][iy,ix]:return reject('water')
   # Full crown is excluded from all building envelopes, not only its trunk.
   pad=math.hypot(sx,sy)/2+.5 if asset in ('camphor','cherry') else radius+.25
   if any(abs(x-b['world_position_m'][0])<b['footprint_m'][0]/2+pad and abs(y-b['world_position_m'][1])<b['footprint_m'][1]/2+pad for b in buildings):return reject('building_envelope')
   if any(abs(x-f['origin'][0])<f['size'][0]/2+pad and abs(y-f['origin'][1])<f['size'][1]/2+pad for f in facilities):return reject('existing_outdoor_facility')
   if any(math.hypot(x-ex,y-ey)<4.0+radius for ex,ey,_ in entries):return reject('door_approach')
   # Keep every route's central 2m clear; vehicles retain their entire carriageway.
   for a,b,width,kind,rid in grid.get((math.floor(x/30),math.floor(y/30)),[]):
    clearance=width/2+.15 if kind=='asphalt' else 1.15
    ar=math.radians(yaw);angle=math.atan2(b[1]-a[1],b[0]-a[0]);cross_extent=abs(math.sin(ar-angle))*sx/2+abs(math.cos(ar-angle))*sy/2
    protected_radius=radius if asset in ('camphor','cherry') else cross_extent
    if distance((x,y),a,b)<clearance+protected_radius:return reject('carriageway_or_clear_path')
   if asset not in ('paving','curb','drain','tactile','parking-mark','tree-grate','flowers') and any(math.hypot(x-xx,y-yy)<radius+rr+.35 for xx,yy,rr in occupied):return reject('existing_prop')
   corners=[sample(field['height_map'],x+dx,1100-y-dy) for dx,dy in [(-min(radius,1),0),(min(radius,1),0),(0,-min(radius,1)),(0,min(radius,1))]]
   allowed=.02 if asset in ('paving','curb') else .035 if asset in ('bench-oak','bench-stone','planter-long','planter-square','bicycle','scooter','cycle-rack','bus-shelter') else .20
   if max(corners)-min(corners)>allowed:return reject('uneven_foundation')
  if z is None:
   z=sample(field['height_map'],min(x,1100),1100-y)+.002
   if check and asset=='paving':z=max(corners)+.025
  region=zone or ('shop' if 190<x<545 and 260<y<525 else 'campus' if 170<x<900 and y>525 else 'mall' if 545<x<900 and 250<y<525 else 'riverside')
  rows.append(dict(id=f'ST-{len(rows)+1:05d}',asset=asset,position_m=[round(x,5),round(y,5),round(z,5)],yaw_deg=round(yaw,4),scale=sc,region=region,origin=source,placement_radius_m=radius,footprint_radius_m=footprint))
  if asset not in ('paving','curb','drain','tactile','parking-mark','tree-grate','flowers','lane-dash','crosswalk'):occupied.append((x,y,radius))
  return rows[-1]
 # Sample by cumulative arclength so subdivided SVG curves do not multiply props.
 for route in routes:
  w=route['width_m'];kind=route['kind'];pts=[[p[0],1100-p[1]] for p in route['points_map_m']];lengths=[math.dist(a,b) for a,b in zip(pts,pts[1:])];total=sum(lengths)
  if total<8:continue
  spacing=14 if kind=='asphalt' else 11;offset=w/2+(2.2 if kind=='asphalt' else 2.4)
  for k,at in enumerate(np.arange(7,total-4,spacing)):
   passed=0
   for j,length in enumerate(lengths):
    if passed+length>=at:
     a,b=pts[j:j+2];t=(at-passed)/max(length,1e-9);x=a[0]+(b[0]-a[0])*t;y=a[1]+(b[1]-a[1])*t;dx,dy=(b[0]-a[0])/length,(b[1]-a[1])/length;break
    passed+=length
   yaw=math.degrees(math.atan2(dy,dx))
   for side in [-1,1]:
    xx,yy=x-dy*offset*side,y+dx*offset*side
    tree=place('cherry' if kind!='asphalt' and k%4==0 else 'camphor',xx,yy,rng.uniform(0,360),rng.uniform(.65,.88),route['id']) if k%2==0 else False
    if tree:
     place('tree-grate',xx,yy,yaw,source=route['id'],check=False,z=tree['position_m'][2]-.01,zone=tree['region'])
    if k%4==1:
     xx,yy=x-dy*(offset+.7)*side+dx*3,y+dx*(offset+.7)*side+dy*3
     place('lamp-modern' if kind=='asphalt' else 'lamp-lantern',xx,yy,yaw,source=route['id'])
    if k%4==3:
     xx,yy=x-dy*(offset+.7)*side,y+dx*(offset+.7)*side
     seat=place('bench-stone' if 550<x<900 and y<525 else 'bench-oak',xx,yy,yaw+(180 if side<0 else 0),source=route['id'])
     if seat:
      planter=place('planter-long',xx+dx*3.3,yy+dy*3.3,yaw,source=route['id'])
      if planter:
       for f in [-.8,0,.8]:place('bed-flowers',planter['position_m'][0]+dx*f,planter['position_m'][1]+dy*f,yaw,1,source=route['id'],check=False,z=planter['position_m'][2]+.53,zone=planter['region'])
    if k%6==1:place('recycling',x-dy*(offset+1.2)*side+dx*5,y+dx*(offset+1.2)*side+dy*5,yaw,source=route['id'])
    if kind=='asphalt' and k%4==0:place('drain',x-dy*(w/2+1.1)*side,y+dx*(w/2+1.1)*side,yaw,source=route['id'])
    if kind=='asphalt' and k%6==3:place('utility-box',x-dy*(offset+1)*side,y+dx*(offset+1)*side,yaw,source=route['id'])
 # Shop forecourts: small seating/planters and clearly parked vehicles, never at doors.
 for b in buildings:
  if not b['id'].startswith('S') or not b['id'][1:].isdigit():continue
  x,y,z=b['world_position_m'];w,d=b['footprint_m'];side=1 if b['entrances'][0]['facade_side']=='N' else -1;yy=y+side*(d/2+2.2)
  for xx in [x-w/2+1.7,x+w/2-1.7]:
   planter=place('planter-square',xx,yy,source=b['id'])
   if planter:place('camphor',xx,yy,0,.22,source=b['id'],check=False,z=planter['position_m'][2]+.56,zone='shop')
  place('cycle-rack',x+w/2-1.4,yy+side*2.2,90,source=b['id'])
  vehicle=place('scooter' if int(b['id'][1:])%3==0 else 'bicycle',x+w/2-1.4,yy+side*5,90,source=b['id'])
  if vehicle:place('parking-mark',*vehicle['position_m'][:2],90,source=b['id'],check=False,z=vehicle['position_m'][2]+.006,zone='shop')
 # Contiguous paved verges and edge stones use real shallow panels, not decals
 # that would float over the graded terrain. Skip steep or occupied locations.
 for route in routes:
  if route['kind']!='asphalt':continue
  pts=[[p[0],1100-p[1]] for p in route['points_map_m']];lengths=[math.dist(a,b) for a,b in zip(pts,pts[1:])];total=sum(lengths);w=route['width_m']
  for at in np.arange(4,total-4,2.005):
   passed=0
   for j,length in enumerate(lengths):
    if passed+length>=at:
     a,b=pts[j:j+2];t=(at-passed)/max(length,1e-9);x=a[0]+(b[0]-a[0])*t;y=a[1]+(b[1]-a[1])*t;dx,dy=(b[0]-a[0])/length,(b[1]-a[1])/length;break
    passed+=length
   yaw=math.degrees(math.atan2(dy,dx))
   for side in [-1,1]:
    pad=place('paving',x-dy*(w/2+1.4)*side,y+dx*(w/2+1.4)*side,yaw,source=route['id'])
    if pad:
     place('curb',x-dy*(w/2+.32)*side,y+dx*(w/2+.32)*side,yaw,source=route['id'])
     place('tactile',*pad['position_m'][:2],yaw+90,source=route['id'],check=False,z=pad['position_m'][2]+.003,zone=pad['region'])
 # Paint has no collision. Keep markings out of buildings and use the road's
 # existing grade; this is geometric dressing, not a traffic controller.
 asphalt=[r for r in routes if r['kind']=='asphalt'];crossings=[]
 for r in asphalt:
  if len(r['points_map_m'])!=2:continue
  a,b=[[p[0],1100-p[1]] for p in r['points_map_m']];dx,dy=b[0]-a[0],b[1]-a[1];length=math.hypot(dx,dy);angle=math.degrees(math.atan2(dy,dx))
  for at in np.arange(10,length-8,8):
   x,y=a[0]+dx*at/length,a[1]+dy*at/length
   if any(distance((x,y),c,d)<w/2+7 for c,d,w,k,rid in segments if k=='asphalt' and rid!=r['id']):continue
   if any(abs(x-q['world_position_m'][0])<q['footprint_m'][0]/2+2 and abs(y-q['world_position_m'][1])<q['footprint_m'][1]/2+2 for q in buildings):continue
   place('lane-dash',x,y,angle,source=r['id'],check=False,z=sample(field['height_map'],x,1100-y)+.032)
  for other in asphalt:
   if other['id']<=r['id'] or len(other['points_map_m'])!=2:continue
   c,d=[[p[0],1100-p[1]] for p in other['points_map_m']];ux,uy=d[0]-c[0],d[1]-c[1];den=dx*uy-dy*ux
   if abs(den)<1e-6:continue
   t=((c[0]-a[0])*uy-(c[1]-a[1])*ux)/den;s=((c[0]-a[0])*dy-(c[1]-a[1])*dx)/den
   if not (.01<t<.99 and .01<s<.99):continue
   ix,iy=a[0]+t*dx,a[1]+t*dy
   for sign in [-1,1]:
    xx,yy=ix+sign*dx/length*(other['width_m']/2+3),iy+sign*dy/length*(other['width_m']/2+3)
    if 5<xx<1095 and 5<yy<1095:place('crosswalk',xx,yy,angle,[1,(r['width_m']-.8)/3.6,1],source=r['id'],check=False,z=sample(field['height_map'],xx,1100-yy)+.033)
   crossings.append([ix,iy])
 for ix,iy in crossings:
  for xx,yy in [(ix+10,iy+10),(ix-10,iy-10)]:place('traffic-light',xx,yy,180 if xx>ix else 0,source='intersection')
 for b in buildings:
  if b['id'] not in ('TR','M1','CV'):continue
  x,y,z=b['world_position_m'];w,d=b['footprint_m']
  for sign in [-1,1]:
   place('bus-shelter',x+sign*(w/2+8),y+12,90,source=b['id'])
   place('railing',x+sign*(w/2+4),y+8,90,source=b['id'])
 # Carry every original regional tree to an instanced LOD without losing positions.
 old=read(BASE/'world-completion-v1/outdoor/assembly-report.json');oldtrees=[]
 for row in old['models']:
  if not row['id'].startswith('Scenery-'):continue
  oldtrees.append(dict(label='World_'+row['id'].replace('-','_'),source_sha256=row['sha256'],position_m=row['position_m'],source=row['source']))
 # The original deterministic placement recipe is reproduced separately by extraction
 # from exported GLBs, so legacy transforms are checked against the actual meshes.
 plan=dict(schema=1,status='planned',source_files={str(ROOT/'Docs/StreetSocialCity/production/building-floor-audit.json'):sha(ROOT/'Docs/StreetSocialCity/production/building-floor-audit.json'),str(BASE/'world-ground-v1/world-field.npz'):sha(BASE/'world-ground-v1/world-field.npz'),str(BASE/'world-ground-v1/ground-plan.json'):sha(BASE/'world-ground-v1/ground-plan.json')},instances=rows,entrance_keepouts_m=entries,entrance_approaches_m=approaches,rejected_candidates=rejected,legacy_tree_clusters=oldtrees,minimum_walkway_m=2,generation_limit=100,reference='https://store.steampowered.com/app/2215410/Anime_City/',policy='Reference-inspired original environment dressing. Full carriageways, crown/building envelopes and door approaches excluded.')
 plan['generator_sha256']=sha(__file__);write(OUT/'plan.json',plan);return plan
if __name__=='__main__':print(len(design()['instances']),'street placements planned')
