"""Native import and collision audit of accepted outdoor campus geometry."""
import unreal,json
from pathlib import Path
from street_city_outdoor_contract import OUT,ROOT
from street_city_unreal_import import import_mesh,sha,xyz
from street_city_remesh_gate import verify
from street_city_outdoor_gate import verify_outdoor
def load(place):
 if not (OUT/'acceptance.json').exists():return None
 data=json.loads((OUT/'acceptance.json').read_text('utf8'));assert data['status']=='passed'
 rows=[]
 for q in data['assets']:
  r=verify_outdoor(q['id'],q['remesh']['sha256']);label='Outdoor_'+q['id'].replace('-','_');mesh=import_mesh(r['path'],'/Game/UniversityCity/Campus/Outdoor',label);x,y,z=q['origin'];actor=place(mesh,label,[y*100,x*100,z*100]);actor.set_folder_path('Campus/Outdoor')
  assert unreal.EditorAssetLibrary.get_metadata_tag(mesh,'StreetCitySourceSHA256')==r['sha256']
  rows.append(dict(id=q['id'],asset=mesh.get_path_name(),source=r['path'],source_sha256=r['sha256'],position_cm=xyz(actor.get_actor_location()),yaw_deg=90))
 return dict(acceptance=data,rows=rows,verifier_sha256=sha(Path(__file__)),acceptance_sha256=sha(OUT/'acceptance.json'))
def audit(state,world):
 if state is None:return []
 checks=[]
 def check(ok,name,**data):checks.append(dict(passed=bool(ok),name=name,**data))
 def trace(p,d,length):
  x,y,z=p;dx,dy,dz=d;start=unreal.Vector(y*100,x*100,z*100);end=unreal.Vector((y+dy*length)*100,(x+dx*length)*100,(z+dz*length)*100);h=unreal.SystemLibrary.line_trace_single(world,start,end,unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,[],unreal.DrawDebugTrace.NONE,True)
  return h.to_tuple() if h is not None and h.to_tuple()[0] else None
 for q in state['acceptance']['assets']:
  fid=q['id'];x,y,z=q['origin']
  if fid=='connection':
   for xx in range(0,176,5):
    sample=min(174.999,max(.001,xx));zz=z-max(0,sample-15)/20;h=trace([x+sample,y,zz+.12],[0,0,-1],.25);check(h is not None and abs(h[4].z/100-zz)<.004,'continuous ramp '+str(xx))
  elif fid=='stadium':
   for i,co in enumerate([f for f in q['features'] if f['kind'] in ('tennis','volleyball')]):
    cx,cy,_=co['center'];w,d=co['playing_m'];ew,ed=co['safety_m']
    for dx in [-w/2+.3,0,w/2-.3]:
     for dy in [-d/2+.3,-2,d/2-.3]:
      h=trace([x+cx+dx,y+cy+dy,z+.2],[0,0,-1],.4);check(h is not None and abs(h[4].z/100-z-.024)<.006,'court surface '+str((i,dx,dy)))
    if co['kind']=='tennis':
     for dx in [-ew/2,ew/2]:
      for dy in [-1.19,-.9,0,.9,1.19]:
       for hh in [.3,1,1.8]:check(trace([x+cx+dx-.25,y+cy+dy,z+hh],[1,0,0],.5) is None,'court gate clearance '+str((i,dx,dy,hh)))
   for yy in range(-100,101,10):
    h=trace([x,y+yy,z+.2],[0,0,-1],.4);check(h is not None and abs(h[4].z/100-z-.05)<.006,'stadium central route '+str(yy))
  elif fid=='campus-track':
   for xx in [-44.7,-20,0,20,44.7]:
    for yy in [-29.7,-15,7,15,29.7]:
     h=trace([x+xx,y+yy,z+.15],[0,0,-1],.3);check(h is not None and abs(h[4].z/100-z-.024)<.006,'native campus football surface '+str((xx,yy)))
   r=q['curb_radius'];s=q['straight']
   for lane in range(8):
    for sign in [-1,1]:
     for xx in [-s/2+.5,0,s/2-.5]:
      yy=sign*(r+(lane+.5)*1.22);h=trace([x+xx,y+yy,z+.2],[0,0,-1],.4);check(h is not None and abs(h[4].z/100-z-.012)<.005,'native campus lane '+str((lane,sign,xx)))
   for xx in [-70,-30,0,30,70]:
    for yy in [-49.5,49.5]:
     h=trace([x+xx,y+yy,z+.2],[0,0,-1],.4);check(h is not None and abs(h[4].z/100-z-.05)<.004,'native campus perimeter walk '+str((xx,yy)))
  elif fid=='campus-track-access':
   for f in q['features']:
    if f['kind']=='grade_link':
     p,pp=f['start'],f['end']
     for t in [.01,.25,.5,.75,.99]:
      xx,yy,zz=[p[k]+t*(pp[k]-p[k]) for k in range(3)];h=trace([x+xx,y+yy,z+zz+.12],[0,0,-1],.25);check(h is not None and abs(h[4].z/100-z-zz)<.005,'native campus entrance ramp '+str((p,t)))
    else:
     xx,yy,zz=f['center'];h=trace([x+xx,y+yy,z+.12],[0,0,-1],.25);check(h is not None and abs(h[4].z/100-z)<.005,'native campus entrance flat')
  elif fid=='annex-ground':
   for xx,yy,expected in [(-130,0,12),(125,0,12),(30,195,12)]:
    h=trace([x+xx,y+yy,expected+.2],[0,0,-1],.4);check(h is not None and abs(h[4].z/100-expected)<.005,'native annex terrain '+str((xx,yy)))
  elif fid=='campus-links':
   for f in q['features']:
    xx,yy,_=f['center'];h=trace([x+xx,y+yy,z+.2],[0,0,-1],.4);check(h is not None and abs(h[4].z/100-z-.031)<.005,'native park link '+str((xx,yy)))
  elif fid!='annex-ground':
   for yy in range(-int(q['size'][1]/2)+2,int(q['size'][1]/2)-1,2):
    for dx in [-.8,0,.8]:
     h=trace([x+dx,y+yy,z+.2],[0,0,-1],.4);check(h is not None and abs(h[4].z/100-z-.031)<.005,'park clear path '+str((fid,dx,yy)))
 snapshots=[]
 for row in state['rows']:
  p=ROOT/'StreetSocialCity/Content'/Path(row['asset'].split('.')[0][len('/Game/'):]+'.uasset');snapshots.append(dict(path=str(p),sha256=sha(p)))
 check(sha(Path(__file__))==state['verifier_sha256'],'outdoor verifier unchanged');check(sha(OUT/'acceptance.json')==state['acceptance_sha256'],'outdoor acceptance unchanged')
 result=dict(status='passed' if all(q['passed'] for q in checks) else 'failed',checks=len(checks),results=checks,issues=[q for q in checks if not q['passed']],imports=state['rows'],source_sha256=state['verifier_sha256'],acceptance_sha256=state['acceptance_sha256'],saved_assets=snapshots)
 (OUT/'unreal-acceptance.json').write_text(json.dumps(result,indent=2),'utf8');assert not result['issues'],result['issues'][:10]
 return snapshots
