"""Independent reopened Unreal collision audit; no gameplay/controller code."""
import unreal,sys,json,math,traceback
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_remaining_contract import IDS,context,OUT
from street_city_remesh_gate import sha
VERIFIER_SHA=sha(Path(__file__));MAP='/Game/UniversityCity/Maps/UniversityCity';checks=[];issues=[]
assert unreal.EditorLevelLibrary.load_level(MAP)
world=unreal.EditorLevelLibrary.get_editor_world()
def check(ok,name,**kw):
 r=dict(name=name,passed=bool(ok),**kw);checks.append(r)
 if not ok:issues.append(r)
def point(origin,local):return unreal.Vector((origin[1]+local[1])*100,(origin[0]+local[0])*100,(origin[2]+local[2])*100)
def trace(origin,a,b):
 h=unreal.SystemLibrary.line_trace_single(world,point(origin,a),point(origin,b),unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,[],unreal.DrawDebugTrace.NONE,True)
 return h.to_tuple() if h is not None and h.to_tuple()[0] else None
def audit():
 actors={a.get_actor_label():a for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()}
 for bid in IDS:
  c=context(bid);origin=c['building']['world_position_m'];r=json.loads((c['out']/'assembly-report.json').read_text('utf8'))
  for f in c['building']['floors']:
   label=f'City_{bid}_F{f["floor"]:02d}';actor=actors[label];check(isinstance(actor,unreal.StaticMeshActor),label+' exists');check(str(actor.static_mesh_component.get_collision_profile_name())=='BlockAll',label+' collision enabled')
  for d in r['doors']:
   x,y,z=d['position_m'];angle=math.radians(d['yaw_deg']);tx,ty=math.cos(angle),math.sin(angle);nx,ny=-ty,tx;closed=d.get('kind')=='lift' and d['floor']>1
   for lane in ([-d['width_m']/3,d['width_m']/3] if closed else [-d['width_m']/3,0,d['width_m']/3]):
    for h in [.2,1.1,2]:
     xx=x+tx*lane;yy=y+ty*lane;hit=trace(origin,[xx-nx*.35,yy-ny*.35,z+h],[xx+nx*.35,yy+ny*.35,z+h]);check((hit is not None)==closed,d['id']+f' native door {lane}/{h}')
  for f in c['building']['floors'][:-1]:
   for core in [q for q in f['vertical_circulation'] if q['kind']=='stairs']:
    g=core['walkable_geometry'];per=g['riser_count']//g['flight_count'];rise=f['height_m']/g['riser_count'];cx,cy,z=core['position_m']
    for flight in range(g['flight_count']):
     xx=cx+(-.9 if flight%2==0 else .9);sign=1 if flight%2==0 else -1
     for step in range(per-1):
      yy=cy+sign*(-g['flight_run_m']/2+(step+.5)*g['tread_depth_m']);zz=z+(flight*per+step+1)*rise
      for lane in [-.6,0,.6]:
       h=trace(origin,[xx+lane,yy,zz+.025],[xx+lane,yy,zz-.025]);check(h is not None and abs(h[4].z/100-(origin[2]+zz))<.008,core['id']+f' native tread connection {flight}/{step}/{lane}');check(trace(origin,[xx+lane,yy,zz+.02],[xx+lane,yy,zz+2.2]) is None,core['id']+f' native standing headroom {flight}/{step}/{lane}')
  for inst in c['instances']:
   asset=c['assets'][inst['asset_id']]
   if asset['role']!='circulation' or not any(t in asset['name'].lower() for t in ['corridor','aisle','走廊','通道']):continue
   w,d,h=asset['size_m'];x,y,z=inst['position_m'];angle=math.radians(inst['rotation_z_deg']);co,si=math.cos(angle),math.sin(angle)
   for along in [-max(w,d)/2+.5,0,max(w,d)/2-.5]:
    for lane in [-.9,0,.9]:
     xx,yy=(along,lane) if w>=d else (lane,along);pt=[x+co*xx-si*yy,y+si*xx+co*yy,z+.05];check(trace(origin,pt,[pt[0],pt[1],pt[2]+2.13]) is None,inst['id']+f' native 2m corridor {along}/{lane}')
 outdoors=json.loads((OUT/'outdoor/assembly-report.json').read_text('utf8'))
 for q in outdoors['nodes']:
  if q.get('existing_asset'):continue
  origin=q['origin'];fid=q['id']
  for yy in [-7,-5,-3,-1]:
   if fid=='PK-0113':continue
   for xx in [-.9,0,.9]:
    h=trace(origin,[xx,yy,.15],[xx,yy,-.1]);check(h is not None,fid+f' native outdoor floor {xx}/{yy}')
    check(trace(origin,[xx,yy,.1],[xx,yy,2.26]) is None,fid+f' native clear 2m approach {xx}/{yy}')
  if fid=='PK-0112':
   for t in [.05,.25,.5,.75,.95]:
    yy=-5+t*5.5;zz=t*.45;h=trace(origin,[8.2,yy,zz+.1],[8.2,yy,zz-.1]);check(h is not None and abs(h[4].z/100-origin[2]-zz)<.008,'stage native continuous side ramp '+str(t))
 result=dict(status='failed' if issues else 'passed',checks=len(checks),issues=issues,results=checks,map=MAP,map_sha256=sha(OUT.parents[2]/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap'),scope='static model collision only; interactions deferred',verifier_sha256=VERIFIER_SHA)
 (OUT/'native-collision-acceptance.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),'utf8');unreal.log('CITY COMPLETION COLLISION '+result['status']+' '+str(len(issues)));assert not issues,issues[:15]
unreal.AutomationLibrary.finish_loading_before_screenshot();unreal.EditorPythonScripting.set_keep_python_script_alive(True);frames=0
def tick(delta):
 global frames
 frames+=1
 if frames<60:return
 unreal.unregister_slate_post_tick_callback(handle)
 try:audit()
 except Exception:(OUT/'native-collision-error.txt').write_text(traceback.format_exc(),'utf8');unreal.log_error(traceback.format_exc())
 finally:unreal.SystemLibrary.quit_editor()
handle=unreal.register_slate_post_tick_callback(tick)
