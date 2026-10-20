"""Import accepted campus floors, preserve shops, verify actual UE collision."""
import unreal,json,sys,math,traceback,hashlib
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_unreal_import import import_mesh,ensure_mesh_materials,sha,xyz
from street_city_campus_contract import IDS,context,digest,OUT as CAMPUS
from street_city_remesh_gate import verify
ROOT=Path('C:/portable/OpenKoikatsu');SOURCE_SHA=sha(ROOT/'Tools/assemble_street_city_unreal_campus.py');OUT=ROOT/'output/street-social-city-production/world-ground-v1';MAP='/Game/UniversityCity/Maps/UniversityCity';rows=[];checks=[];issues=[];sub=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert unreal.EditorLevelLibrary.load_level(MAP);current={o.get_actor_label():o for o in sub.get_all_level_actors()};original_shop_labels={k for k in current if k.startswith('S') and '_F' in k}
def check(ok,name,**data):
 row=dict(passed=bool(ok),name=name,**data);checks.append(row)
 if not ok:issues.append(row)
def place(mesh,label,position,water=False):
 actor=current.get(label)
 if actor is None:actor=sub.spawn_actor_from_object(mesh,unreal.Vector(*position),unreal.Rotator(pitch=0,yaw=90,roll=0));assert actor;actor.set_actor_label(label)
 actor.static_mesh_component.set_static_mesh(mesh);actor.set_actor_location_and_rotation(unreal.Vector(*position),unreal.Rotator(pitch=0,yaw=90,roll=0),False,True);actor.static_mesh_component.set_editor_property('override_materials',ensure_mesh_materials(mesh));actor.static_mesh_component.set_collision_profile_name('NoCollision' if water else 'BlockAll');actor.set_editor_property('tags',['StreetCityManaged','CampusManaged' if label.startswith('Campus_') else 'GroundManaged',label]);actor.set_folder_path('Campus/'+label.split('_')[1] if label.startswith('Campus_') else 'Terrain');return actor
for bid in IDS:
 c=context(bid);folder=c['out'];r=json.loads((folder/'assembly-report.json').read_text('utf8'));a=json.loads((folder/'acceptance.json').read_text('utf8'));assert a['status']=='passed' and r['contract_sha256']==digest(c) and sha(folder/f'{bid}-complete-2k.glb')==a['sha256'];x,y,z=c['building']['world_position_m']
 for f in c['building']['floors']:
  no=f['floor'];label=f'Campus_{bid}_F{no:02d}';source=folder/f'{bid}-F{no:02d}-complete-2k.glb';mesh=import_mesh(source,'/Game/UniversityCity/Campus/'+bid,label);actor=place(mesh,label,[y*100,x*100,z*100]);check(unreal.EditorAssetLibrary.get_metadata_tag(mesh,'StreetCitySourceSHA256')==sha(source),label+' current GLB identity');check(mesh.get_editor_property('body_setup').get_editor_property('collision_trace_flag')==unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE,label+' exact triangle collision');row=dict(id=label,building=bid,floor=no,source=str(source),source_sha256=sha(source),asset=mesh.get_path_name(),position_cm=xyz(actor.get_actor_location()),yaw_deg=90);rows.append(row);(OUT/'unreal-campus-import.json').write_text(json.dumps(dict(status='importing',floors=rows),indent=2),'utf8')
 if bid=='SP':
  source=folder/'SP-water-2k.glb';mesh=import_mesh(source,'/Game/UniversityCity/Campus/SP','Campus_SP_Water');place(mesh,'Campus_SP_Water',[y*100,x*100,z*100],True)
 unreal.log(bid+' campus floors imported');unreal.SystemLibrary.collect_garbage()
assert len(rows)==89;(OUT/'unreal-campus-import.json').write_text(json.dumps(dict(status='passed',floors=rows),indent=2),'utf8')
ground_sha=sha(OUT/'ground-acceptance.json');ground=json.loads((OUT/'ground-acceptance.json').read_text('utf8'));assert ground['status']=='passed'
for chunk in ground['chunks']:
 receipt=verify(Path(chunk['ground']['remesh_report']).parent,chunk['ground']['sha256']);x,y,_=chunk['world_origin_m'];mesh=import_mesh(receipt['path'],'/Game/UniversityCity/Ground',chunk['id']);place(mesh,chunk['id'],[y*100,x*100,0]);check(unreal.EditorAssetLibrary.get_metadata_tag(mesh,'StreetCitySourceSHA256')==receipt['sha256'],chunk['id']+' current campus foundation terrain')
 if chunk.get('water'):
  receipt=verify(Path(chunk['water']['remesh_report']).parent,chunk['water']['sha256']);label=chunk['id']+'_Water';mesh=import_mesh(receipt['path'],'/Game/UniversityCity/Water',label);actor=place(mesh,label,[y*100,x*100,0],True);check(unreal.EditorAssetLibrary.get_metadata_tag(mesh,'StreetCitySourceSHA256')==receipt['sha256'],label+' current remeshed water');check(str(actor.static_mesh_component.get_collision_profile_name())=='NoCollision',label+' nonblocking water surface')
from street_city_unreal_outdoor import load as load_outdoor,audit as audit_outdoor
outdoor_state=load_outdoor(place)
check(original_shop_labels=={o.get_actor_label() for o in sub.get_all_level_actors() if o.get_actor_label().startswith('S') and '_F' in o.get_actor_label()},'preserve all shop floor actors');check(unreal.EditorLevelLibrary.save_current_level(),'persist campus in shared city map');world=unreal.EditorLevelLibrary.get_editor_world()
def point(c,p):
 x,y,z=c['building']['world_position_m'];xx,yy,zz=p;return unreal.Vector((y+yy)*100,(x+xx)*100,(z+zz)*100)
def trace(c,start,end):
 hit=unreal.SystemLibrary.line_trace_single(world,point(c,start),point(c,end),unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,[],unreal.DrawDebugTrace.NONE,True)
 return hit if hit is not None and hit.to_tuple()[0] else None
def audit():
 for bid in IDS:
  c=context(bid);r=json.loads((c['out']/'assembly-report.json').read_text('utf8'))
  for d in r['doors']:
   x,y,z=d['position_m'];t=math.radians(d['yaw_deg']);tx,ty=math.cos(t),math.sin(t);nx,ny=-ty,tx;closed=d.get('kind')=='lift' and d['floor']>1
   for lane in ([-d['width_m']/3,d['width_m']/3] if closed else [-d['width_m']/3,0,d['width_m']/3]):
    for h in [.2,1.1,2]:
     xx=x+tx*lane;yy=y+ty*lane;hit=trace(c,[xx-nx*.35,yy-ny*.35,z+h],[xx+nx*.35,yy+ny*.35,z+h]);check((hit is not None)==closed,d['id']+f' actual UE passage {lane}/{h}')
  for f in c['building']['floors'][:-1]:
   for core in [q for q in f['vertical_circulation'] if q['kind']=='stairs']:
    g=core['walkable_geometry'];per=g['riser_count']//g['flight_count'];rise=f['height_m']/g['riser_count'];cx,cy,z=core['position_m']
    for flight in range(g['flight_count']):
     xx=cx+(-.9 if flight%2==0 else .9);sign=1 if flight%2==0 else -1
     for step in range(per-1):
      yy=cy+sign*(-g['flight_run_m']/2+(step+.5)*g['tread_depth_m']);zz=z+(flight*per+step+1)*rise
      for lane in [-.6,0,.6]:
       check(trace(c,[xx+lane,yy,zz+.02],[xx+lane,yy,zz-.02]) is not None,core['id']+f' UE tread {flight}/{step}/{lane}');check(trace(c,[xx+lane,yy,zz+.02],[xx+lane,yy,zz+2.2]) is None,core['id']+f' UE stair headroom {flight}/{step}/{lane}')
  for entry in c['building']['entrances']:
   x,y,z=entry['position_m'];dx,dy={'S':(0,-1),'N':(0,1),'W':(-1,0),'E':(1,0)}[entry['facade_side']]
   for lane in [-.8,0,.8]:check(trace(c,[x+dx-dy*lane,y+dy+dx*lane,z+.5],[x+dx-dy*lane,y+dy+dx*lane,z-.05]) is not None,entry['id']+f' continuous outdoor support {lane}')
  if bid=='SP':
   for i in c['instances']:
    asset=c['assets'][i['asset_id']]
    if asset['name']!='空心池體':continue
    x,y,z=i['position_m'];hit=trace(c,[x,y,z+1],[x,y,z-.2]);expected=(c['building']['world_position_m'][2]+z+.2)*100;check(hit is not None and abs(hit.to_tuple()[4].z-expected)<.5,i['id']+' actual pool bottom, terrain clear')
 check(len([o for o in sub.get_all_level_actors() if o.get_actor_label().startswith('Campus_') and '_F' in o.get_actor_label()])==89,'all 89 campus floors placed')
 snapshots=audit_outdoor(outdoor_state,world)
 for row in rows:
  path=ROOT/'StreetSocialCity/Content'/Path(row['asset'].split('.')[0][len('/Game/'):]+'.uasset');snapshots.append(dict(path=str(path),sha256=sha(path)))
 check(sha(ROOT/'Tools/assemble_street_city_unreal_campus.py')==SOURCE_SHA,'unchanged executing campus verifier');check(sha(OUT/'ground-acceptance.json')==ground_sha,'unchanged ground acceptance during native audit');mapfile=ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap';snapshots.append(dict(path=str(mapfile),sha256=sha(mapfile)));result=dict(source_sha256=SOURCE_SHA,ground_acceptance_sha256=ground_sha,status='failed' if issues else 'passed',map=MAP,floors=89,checks=len(checks),issues=issues,results=checks,saved_assets=snapshots,scope='Actual static campus and terrain; gameplay controllers remain unconnected');(CAMPUS/'unreal-acceptance.json').write_text(json.dumps(result,indent=2),'utf8');(OUT/'unreal-campus.json').write_text(json.dumps(result,indent=2),'utf8');assert not issues,str(issues[:15])
unreal.AutomationLibrary.finish_loading_before_screenshot();unreal.EditorPythonScripting.set_keep_python_script_alive(True);CITY_ASYNC_RENDER=True;frames=0
def tick(delta):
 global frames
 frames+=1
 if frames<60:return
 unreal.unregister_slate_post_tick_callback(handle)
 try:audit();status=dict(status='succeeded',stage='Campus')
 except Exception:status=dict(status='failed',stage='Campus',error=traceback.format_exc());unreal.log_error(status['error'])
 finally:(OUT/'unreal-campus-task.json').write_text(json.dumps(status,indent=2),'utf8');unreal.SystemLibrary.quit_editor()
handle=unreal.register_slate_post_tick_callback(tick)
