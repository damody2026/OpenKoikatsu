"""Import department-store floors without replacing school or shop actors."""
import unreal,sys,json,math,traceback
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_mall import OUT,ROOT,sha,transform
from street_city_unreal_import import import_mesh,ensure_mesh_materials,xyz
SOURCE_PATH=ROOT/'Tools/assemble_street_city_unreal_mall.py';SOURCE_SHA=sha(SOURCE_PATH)
STATUS=OUT/'unreal-task.json';STATUS.write_text(json.dumps(dict(status='running')),'utf8');audit_only='-MallAuditOnly' in unreal.SystemLibrary.get_command_line();results=[];rows=[]
def check(ok,name,**data):results.append(dict(passed=bool(ok),name=name,**data))
def snapshot(actors):
 data={}
 for o in actors:
  if not isinstance(o,unreal.StaticMeshActor) or o.get_actor_label().startswith('Mall_'):continue
  comp=o.static_mesh_component;mesh=comp.static_mesh
  rotation=o.get_actor_rotation();data[o.get_actor_label()]=dict(mesh=mesh.get_path_name() if mesh else None,position=xyz(o.get_actor_location()),rotation=[float(rotation.pitch),float(rotation.yaw),float(rotation.roll)],scale=xyz(o.get_actor_scale3d()),collision=str(comp.get_collision_profile_name()))
 return data
try:
 a=json.loads((OUT/'acceptance.json').read_text('utf8'));assert a['status']=='passed' and a['model_sha256']==sha(OUT/'M1-complete-2k.glb');G=json.loads((OUT/'geometry.json').read_text('utf8'));C=G['contract'];sub=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);assert unreal.EditorLevelLibrary.load_level('/Game/UniversityCity/Maps/UniversityCity');current={o.get_actor_label():o for o in sub.get_all_level_actors()};before=snapshot(sub.get_all_level_actors());x,y,z=C['world_position_m']
 for f in C['floors']:
  n=f['floor'];label=f'Mall_M1_F{n:02d}';source=OUT/f'M1-F{n:02d}-complete-2k.glb';folder='/Game/UniversityCity/Mall/M1';asset=folder+'/'+label
  mesh=current[label].static_mesh_component.static_mesh if audit_only and label in current else None if audit_only else import_mesh(source,folder,label)
  assert mesh;check(unreal.EditorAssetLibrary.get_metadata_tag(mesh,'StreetCitySourceSHA256')==sha(source),label+' exact source identity');check(mesh.get_editor_property('body_setup').get_editor_property('collision_trace_flag')==unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE,label+' triangle collision')
  actor=current.get(label)
  if not audit_only:
   if actor is None:actor=sub.spawn_actor_from_object(mesh,unreal.Vector(y*100,x*100,z*100),unreal.Rotator(pitch=0,yaw=90,roll=0));actor.set_actor_label(label)
   actor.static_mesh_component.set_static_mesh(mesh);actor.static_mesh_component.set_editor_property('override_materials',ensure_mesh_materials(mesh));actor.static_mesh_component.set_collision_profile_name('BlockAll');actor.set_actor_location_and_rotation(unreal.Vector(y*100,x*100,z*100),unreal.Rotator(pitch=0,yaw=90,roll=0),False,True);actor.set_folder_path('Mall/M1');actor.set_editor_property('tags',['StreetCityManaged','MallManaged',label])
  assert actor;check(actor.static_mesh_component.static_mesh==mesh,label+' current actor mesh');check(max(abs(v-e) for v,e in zip(xyz(actor.get_actor_location()),[y*100,x*100,z*100]))<.1,label+' metre world transform');check(abs(actor.get_actor_rotation().yaw-90)<.01,label+' correct axes');rows.append(dict(floor=n,label=label,source=str(source),source_sha256=sha(source),asset=mesh.get_path_name()))
 from street_city_mall_lighting_unreal import install as install_boutique_lights,audit as audit_boutique_lights
 if not audit_only:install_boutique_lights(current,sub)
 check(before==snapshot(sub.get_all_level_actors()),'preserve all existing school, shops and outdoor actors');check(len([k for k in current if k.startswith('Campus_') and '_F' in k])==89,'preserve 89 school floors');check(len([k for k in current if k.startswith('S') and '_F' in k])==288,'preserve 288 shop floors')
 if not audit_only:check(unreal.EditorLevelLibrary.save_current_level(),'save mall into shared map')
 world=unreal.EditorLevelLibrary.get_editor_world()
 def trace(p,q):
  def pt(v):return unreal.Vector((y+v[1])*100,(x+v[0])*100,(z+v[2])*100)
  hit=unreal.SystemLibrary.line_trace_single(world,pt(p),pt(q),unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,[],unreal.DrawDebugTrace.NONE,True);return hit if hit is not None and hit.to_tuple()[0] else None
 def audit():
  audit_boutique_lights({a.get_actor_label():a for a in sub.get_all_level_actors()})
  for route in G['routes']:
   p,q=route['start'],route['end'];length=math.dist(p,q);dx=(q[0]-p[0])/length;dy=(q[1]-p[1])/length
   for i in range(math.ceil(length/2)):
    t=(i+.5)/math.ceil(length/2)
    for lane in [-route['width']/2+.1,0,route['width']/2-.1]:
     xx=p[0]+t*(q[0]-p[0])-dy*lane;yy=p[1]+t*(q[1]-p[1])+dx*lane;zz=p[2]
     check(trace([xx,yy,zz+.05],[xx,yy,zz+2.2]) is None,f'F{route["floor"]} actual corridor headroom {i}/{lane}');check(trace([xx,yy,zz+.05],[xx,yy,zz-.05]) is not None,f'F{route["floor"]} actual corridor floor {i}/{lane}')
  for d in G['doors']:
   xx,yy,zz=d['position_m'];angle=math.radians(d['yaw_deg']);tx,ty=math.cos(angle),math.sin(angle);nx,ny=-ty,tx;closed=d.get('kind')=='lift' and d['floor']>1
   for lane in [-d['width_m']/3,0,d['width_m']/3]:
    for hh in [.2,1.1,2]:
     px,py=xx+tx*lane,yy+ty*lane;hit=trace([px-nx*.25,py-ny*.25,zz+hh],[px+nx*.25,py+ny*.25,zz+hh]);check((hit is not None)==closed,d['id']+' native doorway '+str((lane,hh)))
  for q in G['parts']:
   if q['kind']=='void_guard':
    xx,yy,zz=q['pos'];w,d,h=q['size'];dx,dy=(1,0) if w<d else (0,1);check(trace([xx-dx*.12,yy-dy*.12,zz+.9],[xx+dx*.12,yy+dy*.12,zz+.9]) is not None,q['name']+' native fall-protection guard')
   if q['kind'] not in ('stair_tread','escalator_tread'):continue
   xx,yy,zz=q['pos'];zz+=q['size'][2]
   for lane in [-.5,0,.5] if q['kind']=='escalator_tread' else [0]:
    sy=yy+lane;check(trace([xx,sy,zz+.02],[xx,sy,zz-.02]) is not None,q['name']+' native tread '+str(lane));check(trace([xx,sy,zz+.02],[xx,sy,zz+2.2]) is None,q['name']+' native overhead '+str(lane))
  for lift in C['lifts']:
   for distance in [-3.8,-3.2,-2.6,-2.2,-1.8,-1.4,-.5,.5]:
    for edge in [-.45,0,.45]:
     px,py,pz=transform([edge,distance,0],[lift['x'],lift['y'],0],lift['yaw'])
     check(trace([px,py,.04],[px,py,-.04]) is not None,lift['id']+' native car access support '+str((distance,edge)))
     check(trace([px,py,.04],[px,py,2.2]) is None,lift['id']+' native car access headroom '+str((distance,edge)))
  for e in C['escalators']:
   for end in ['lower_point','upper_point']:
    px,py,pz=e[end];sign=1 if px>0 else -1
    for distance in [.01,.2,1,2,4,6]:
     xx=px+sign*distance
     for edge in [-.49,0,.49]:
      yy=py+edge
      check(trace([xx,yy,pz+.02],[xx,yy,pz-.02]) is not None,e['id']+' native deck support '+end+str((distance,edge)))
      check(trace([xx,yy,pz+.02],[xx,yy,pz+2.2]) is None,e['id']+' native deck headroom '+end+str((distance,edge)))
  for side,p,normal in [('S',[0,-50,0],[0,-1]),('W',[-75,0,0],[-1,0]),('E',[75,0,0],[1,0]),('Receiving',[75,-25,0],[1,0])]:
   for lane in [-.8,0,.8]:
    xx=p[0]+normal[0]-normal[1]*lane;yy=p[1]+normal[1]+normal[0]*lane;check(trace([xx,yy,.5],[xx,yy,-.1]) is not None,'exterior entrance support '+side+str(lane))
  for q in G['parts']:
   if 'grade_start' not in q:continue
   aa,bb=q['grade_start'],q['grade_end'];length=math.dist(aa[:2],bb[:2]);check(abs(bb[2]-aa[2])/length<=.05,q['name']+' slope <=5%')
   for t in [.01,.25,.5,.75,.99]:
    xx,yy,zz=[aa[k]+t*(bb[k]-aa[k]) for k in range(3)];hit=trace([xx,yy,zz+.03],[xx,yy,zz-.03]);check(hit is not None and abs(hit.to_tuple()[4].z/100-z-zz)<.008,q['name']+' actual graded walking surface '+str(t))
  issues=[q for q in results if not q['passed']];snapshots=[]
  for row in rows:
   path=ROOT/'StreetSocialCity/Content'/Path(row['asset'].split('.')[0][len('/Game/'):]+'.uasset');snapshots.append(dict(path=str(path),sha256=sha(path)))
  path=ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap';snapshots.append(dict(path=str(path),sha256=sha(path)))
  check(sha(SOURCE_PATH)==SOURCE_SHA,'native verifier unchanged during execution');issues=[q for q in results if not q['passed']]
  r=dict(status='failed' if issues else 'passed',checks=len(results),issues=issues,results=results,imports=rows,saved_assets=snapshots,source_sha256=SOURCE_SHA,acceptance_sha256=sha(OUT/'acceptance.json'),preserved_actor_count=len(before),scope='Static seamless mall geometry; game controllers pending');(OUT/'unreal-acceptance.json').write_text(json.dumps(r,indent=2),'utf8');assert not issues,issues[:12]
 unreal.AutomationLibrary.finish_loading_before_screenshot();unreal.EditorPythonScripting.set_keep_python_script_alive(True);frames=0
 def tick(delta):
  global frames
  frames+=1
  if frames<60:return
  unreal.unregister_slate_post_tick_callback(handle)
  try:audit();status=dict(status='succeeded')
  except Exception:status=dict(status='failed',error=traceback.format_exc());unreal.log_error(status['error'])
  finally:STATUS.write_text(json.dumps(status,indent=2),'utf8');unreal.SystemLibrary.quit_editor()
 handle=unreal.register_slate_post_tick_callback(tick)
except Exception:
 STATUS.write_text(json.dumps(dict(status='failed',error=traceback.format_exc()),indent=2),'utf8');unreal.log_error(traceback.format_exc());unreal.SystemLibrary.quit_editor()
