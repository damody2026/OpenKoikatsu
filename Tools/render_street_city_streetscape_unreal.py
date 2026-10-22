"""Real lit screenshots of the saved city and automatic instanced foliage LODs."""
import unreal,sys,math,traceback
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_streetscape import ROOT,OUT,read,write,sha
assert unreal.EditorLevelLibrary.load_level('/Game/UniversityCity/Maps/UniversityCity');unreal.AutomationLibrary.finish_loading_before_screenshot();world=unreal.EditorLevelLibrary.get_editor_world();sub=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);camera=read(OUT/'camera-plan.json');assert camera['status']=='passed' and all(q['clearance_m']>=1.69 for q in camera['views']);views=[(q['name'],q['eye'],q['target']) for q in camera['views']];results=[];current=None;phase='setup';frames=0;hold=0;busy=False;targets=[]
def pt(p):return unreal.Vector(p[1]*100,p[0]*100,p[2]*100)
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
def tick(dt):
 global current,phase,frames,hold,busy
 if busy:return
 frames+=1
 if frames<90:return
 if hold>0:hold-=1;return
 busy=True
 try:
  if phase=='setup':
   unreal.log('STREET_CAPTURE setup '+views[len(results)][0])
   name,eye,target=views[len(results)];position=pt(eye);rotation=unreal.MathLibrary.find_look_at_rotation(position,pt(target))
   actor=sub.spawn_actor_from_class(unreal.SceneCapture2D,position,rotation);unreal.log('STREET_CAPTURE spawned '+name)
   comp=actor.capture_component2d;comp.set_editor_property('capture_every_frame',False);comp.set_editor_property('capture_on_movement',False)
   rt=unreal.RenderingLibrary.create_render_target2d(world,1600,1000,unreal.TextureRenderTargetFormat.RTF_RGBA8);unreal.log('STREET_CAPTURE target '+name)
   comp.set_editor_property('texture_target',rt);comp.set_editor_property('capture_source',unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR);comp.set_editor_property('fov_angle',75);comp.set_editor_property('max_view_distance_override',65000.0);targets.append(rt);current=dict(name=name,eye=eye,target=target,actor=actor,comp=comp,rt=rt);phase='capture';hold=30
   unreal.log('STREET_CAPTURE setup complete '+name)
  elif phase=='capture':
   unreal.log('STREET_CAPTURE capture '+current['name'])
   unreal.AutomationLibrary.finish_loading_before_screenshot();current['comp'].capture_scene();phase='export';hold=8
  elif phase=='export':
   unreal.log('STREET_CAPTURE export '+current['name'])
   folder=OUT/'native-renders';folder.mkdir(exist_ok=True);path=folder/(current['name']+'.png');unreal.RenderingLibrary.export_render_target(world,current['rt'],str(folder),path.name);assert path.is_file() and path.stat().st_size>5000;results.append(dict(view=current['name'],image=str(path),sha256=sha(path),eye_m=current['eye'],target_m=current['target']));sub.destroy_actor(current['actor']);phase='setup';hold=8
   if len(results)==len(views):phase='finish';hold=30
  elif phase=='finish':
   unreal.unregister_slate_post_tick_callback(handle);write(OUT/'native-render.json',dict(status='captured_requires_clean_exit_and_visual_review',images=results,delivery_sha256=sha(OUT/'delivery.json'),map_sha256=sha(ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap'),renderer_sha256=sha(__file__),rendering='Actual saved world, persistent daylight, D3D12 lit scene captures with current HISM/LOD/culling; no preview lighting, actor changes or map saves.'));unreal.SystemLibrary.quit_editor()
 except Exception:write(OUT/'native-render-error.json',dict(error=traceback.format_exc()));unreal.log_error(traceback.format_exc());unreal.unregister_slate_post_tick_callback(handle);unreal.SystemLibrary.quit_editor()
 finally:busy=False
handle=unreal.register_slate_post_tick_callback(tick)
