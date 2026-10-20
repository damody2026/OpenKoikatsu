"""Real UE interior review, separate from the external overview."""
import unreal,json,math,traceback,sys
from pathlib import Path
ROOT=Path('C:/portable/OpenKoikatsu');OUT=ROOT/'output/street-social-city-production/world-ground-v1';REVIEW=OUT.parent/'interior-repair-v1';REVIEW.mkdir(exist_ok=True)
sys.path.insert(0,str(ROOT/'Tools'))
from street_city_unreal_snapshot import capture
AUDIT_STAGE='InteriorAuditCollision' if '-CityStage=InteriorAuditCollision' in unreal.SystemLibrary.get_command_line() else 'InteriorAudit'
assert unreal.EditorLevelLibrary.load_level('/Game/UniversityCity/Maps/UniversityCity');world=unreal.EditorLevelLibrary.get_editor_world();sub=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
placements=json.loads((ROOT/'output/street-social-city-production/shop-street-v1/world-assembly.json').read_text('utf-8'));issues=[];checks=[]
audit_snapshot=capture(ROOT)
def point(b,x,y,z):
 angle=math.radians(b['rotation_z_deg']);co,si=math.cos(angle),math.sin(angle);east=co*x-si*y+b['position_m'][0];north=si*x+co*y+b['position_m'][1];return unreal.Vector(north*100,east*100,(z+b['position_m'][2])*100)
def audit_collision():
 style_path=OUT/'unreal-shop-style-bindings.json'
 if style_path.exists():
  bindings=json.loads(style_path.read_text('utf8'));actors={o.get_actor_label():o for o in sub.get_all_level_actors()};assert bindings['status']=='passed' and len(bindings['actors'])==288
  language_path=OUT.parent/'localization-v1/native-bindings.json';localized={}
  if language_path.exists():
   language=json.loads(language_path.read_text('utf8'))
   if language.get('status')=='passed_reopened_native_materials_and_runtime_parameter':
    import hashlib
    manifest=OUT.parent/'localization-v1/models.json'
    assert language['models_manifest_sha256']==hashlib.sha256(manifest.read_bytes()).hexdigest(),'Stale language binding audit'
    localized={r['actor']:r for r in language['actors']}
  for binding in bindings['actors']:
   actor=actors[binding['actor']];comp=actor.static_mesh_component;actual=[comp.get_material(i).get_path_name() if comp.get_material(i) else None for i in range(comp.get_num_materials())]
   current=localized.get(binding['actor']);expected=current['all_materials'] if current and current['mesh']==binding['asset'] else binding['materials']
   passed=comp.static_mesh.get_path_name()==binding['asset'] and actual==expected;row=dict(building=binding['actor'],kind='actual current owner wall material bindings',passed=passed,localized=bool(current));checks.append(row)
   if not passed:issues.append(row)
 for b in placements['buildings']:
  report=json.loads((Path(b['source']).parent/'assembly-report.json').read_text('utf-8'))
  for door in report['doors']:
   if 'center_m' in door:
    x,y=door['center_m'];z=(door['floor']-1)*3.8+1.2;axis=door['axis'];normal=(0,1) if axis==0 else (1,0);along=(1,0) if axis==0 else (0,1)
   else:
    x,y,base_z=door['position_m'];z=base_z+1.2;angle=math.radians(door['yaw_deg']);along=(math.cos(angle),math.sin(angle));normal=(-math.sin(angle),math.cos(angle))
   for sign in ([] if 'entry-front' in door['id'] else [-1,1]):
    offset=door['width_m']/2+(.025 if 'cubicle-door' in door['id'] else .15);xx=x+along[0]*sign*offset;yy=y+along[1]*sign*offset
    hit=unreal.SystemLibrary.line_trace_single(world,point(b,xx-normal[0]*.6,yy-normal[1]*.6,z),point(b,xx+normal[0]*.6,yy+normal[1]*.6,z),unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,[],unreal.DrawDebugTrace.NONE,True)
    passed=hit is not None and bool(hit.to_tuple()[0]);row=dict(building=b['building'],door=door['id'],jamb=sign,passed=passed);checks.append(row)
    if not passed:issues.append(row)
   base_z=z-1.2
   # Floor GLBs intentionally capture animation frame 1: the single cab is
   # at F01 and upper landing panels are CLOSED. Require their safety barrier;
   # opening them without a cab would create an unsafe empty-shaft entrance.
   closed_lift=door['floor']>1 and (door.get('kind')=='lift' or door.get('kind')=='sliding' and 'shaft-landing-door' in door['id'])
   lanes=[-door['width_m']/3,door['width_m']/3] if closed_lift else [-door['width_m']/3,0,door['width_m']/3]
   for lane in lanes:
    for height in [.2,1.1,2.0]:
     xx=x+along[0]*lane;yy=y+along[1]*lane;zz=base_z+height
     hit=unreal.SystemLibrary.line_trace_single(world,point(b,xx-normal[0]*.45,yy-normal[1]*.45,zz),point(b,xx+normal[0]*.45,yy+normal[1]*.45,zz),unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,[],unreal.DrawDebugTrace.NONE,True)
     blocked=hit is not None and bool(hit.to_tuple()[0]);passed=blocked if closed_lift else not blocked;row=dict(building=b['building'],door=door['id'],kind='closed_lift_barrier' if closed_lift else 'clear_aperture',lane_m=lane,height_m=height,expected_collision=closed_lift,passed=passed);checks.append(row)
     if not passed:issues.append(row)
unreal.AutomationLibrary.finish_loading_before_screenshot();unreal.EditorPythonScripting.set_keep_python_script_alive(True);CITY_ASYNC_RENDER=True;frames=0
def on_tick(delta):
 global frames
 frames+=1
 if frames<60:return
 unreal.unregister_slate_post_tick_callback(handle)
 try:
  audit_collision()
  if '-CityStage=InteriorAuditCollision' not in unreal.SystemLibrary.get_command_line():
   b=next(b for b in placements['buildings'] if b['building']=='S10');location=point(b,-1.5,-5,1.65);target_point=point(b,2,-1.5,1.65);rotation=unreal.MathLibrary.find_look_at_rotation(location,target_point)
   light=sub.spawn_actor_from_class(unreal.PointLight,point(b,0,-.5,2.7),unreal.Rotator());light.light_component.set_intensity(30);light.light_component.set_editor_property('attenuation_radius',2000)
   camera=sub.spawn_actor_from_class(unreal.SceneCapture2D,location,rotation);c=camera.capture_component2d;target=unreal.RenderingLibrary.create_render_target2d(world,1280,720,unreal.TextureRenderTargetFormat.RTF_RGBA8);c.set_editor_property('texture_target',target);c.set_editor_property('capture_source',unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR);c.set_editor_property('capture_every_frame',False);c.capture_scene();unreal.RenderingLibrary.export_render_target(world,target,str(REVIEW),'S10-unreal-interior-'+('after' if '-CityInteriorAfter' in unreal.SystemLibrary.get_command_line() else 'before')+'.png')
  assert capture(ROOT)==audit_snapshot,'Native assets changed during collision audit'
  after='-CityInteriorAfter' in unreal.SystemLibrary.get_command_line();result=dict(status='passed' if not after or not issues else 'failed',checks=len(checks),issues=issues,results=checks,saved_assets=audit_snapshot);(OUT/'unreal-interioraudit.json').write_text(json.dumps(result,indent=2),'utf-8');status=dict(status='succeeded' if result['status']=='passed' else 'failed',stage=AUDIT_STAGE,error=None if result['status']=='passed' else str(issues[:10]))
 except Exception:status=dict(status='failed',error=traceback.format_exc());unreal.log_error(status['error'])
 finally:(OUT/('unreal-interiorauditcollision-task.json' if '-CityStage=InteriorAuditCollision' in unreal.SystemLibrary.get_command_line() else 'unreal-interioraudit-task.json')).write_text(json.dumps(status,indent=2),'utf-8');unreal.SystemLibrary.quit_editor()
handle=unreal.register_slate_post_tick_callback(on_tick)
