"""Real D3D offscreen checks of adopted floor assets and saved material overrides."""
import unreal,json,sys,traceback,os
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_world_wall_art import ROOT,OUT,sha
EAL=unreal.EditorAssetLibrary
native=json.loads((OUT/'native-install.json').read_text('utf8'));assert native['status']=='passed'
source=json.loads((OUT.parent/'scene-integration-v1/native-inventory.json').read_text('utf8'));assets={q['id']:q['asset'] for q in source['actors']}
language={r['actor']:r for g in ['remaining-all','campus-all','shops-all','mall'] for r in json.loads((OUT.parent/'localization-v1'/('unreal-'+g+'.json')).read_text('utf8'))['actors']}
world=unreal.EditorLevelLibrary.get_editor_world();sub=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);labels=['City_'+b+'_F01' for b in ['H1','R1','R2','O01','O02','O03','CV','TR']]+['Campus_IN_F01','S01_F01','Mall_M1_F01'];results=[];frame=0;busy=False;current=None;phase='setup';hold=0;targets=[]
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
def setup(label):
 mesh=EAL.load_asset(assets[label]);assert mesh;old=language[label];plan=json.loads((OUT.parent/'scene-integration-v1/inventory.json').read_text('utf8'));q=next(q for q in plan['rows'] if q['id']==label)
 actor=sub.spawn_actor_from_object(mesh,unreal.Vector(*q['position_cm']),unreal.Rotator(pitch=0,yaw=q['yaw_deg'],roll=0));assert actor
 for i,path in enumerate(old['all_materials']):actor.static_mesh_component.set_material(i,EAL.load_asset(path))
 origin,extent=actor.get_actor_bounds(False);span=max(extent.x,extent.y)*2;center=origin;lights=[]
 location=center+unreal.Vector(-.8,-1,1.5)*span;rotation=unreal.MathLibrary.find_look_at_rotation(location,center);cap=sub.spawn_actor_from_class(unreal.SceneCapture2D,location,rotation);component=cap.capture_component2d
 target=unreal.RenderingLibrary.create_render_target2d(world,1600,1100,unreal.TextureRenderTargetFormat.RTF_RGBA8);component.set_editor_property('texture_target',target);component.set_editor_property('projection_type',unreal.CameraProjectionMode.ORTHOGRAPHIC);component.set_editor_property('ortho_width',span*1.65);component.set_editor_property('capture_source',unreal.SceneCaptureSource.SCS_BASE_COLOR);component.set_editor_property('capture_every_frame',False)
 targets.append(target)
 return dict(label=label,mesh=mesh,actor=actor,cap=cap,component=component,target=target,old=old,lights=lights)
def export_case(q):
 folder=OUT/'native-renders';folder.mkdir(exist_ok=True);unreal.RenderingLibrary.export_render_target(world,q['target'],str(folder),q['label']+'.png');p=folder/(q['label']+'.png');assert p.is_file() and p.stat().st_size>2000
 results.append(dict(actor=q['label'],image=str(p),sha256=sha(p),source_asset=q['mesh'].get_path_name(),material_override_count=len(q['old']['all_materials'])))
 for obj in [q['cap'],q['actor'],*q['lights']]:assert sub.destroy_actor(obj)
def tick(delta):
 global frame,busy,current,phase,hold
 if busy:return
 frame+=1
 if frame<60:return
 if hold>0:hold-=1;return
 busy=True
 try:
  if phase=='setup':
   current=setup(labels[len(results)]);phase='capture';hold=15
  elif phase=='capture':
   unreal.AutomationLibrary.finish_loading_before_screenshot();current['component'].capture_scene();phase='export';hold=5
  elif phase=='export':
   export_case(current);current=None;phase='setup';hold=5
   if len(results)==len(labels):phase='finish';hold=30
  elif phase=='finish':
   unreal.unregister_slate_post_tick_callback(handle);(OUT/'native-render.json').write_text(json.dumps(dict(status='captured_real_rhi_images',images=results,source_install_sha256=sha(OUT/'native-install.json'),rhi=os.environ.get('STREET_CITY_CAPTURE_RHI','d3d12'),rendering='Real Unreal offscreen GBuffer BaseColor; deferred frame captures, temporary Entry studio, no lighting approximation and no map saves'),indent=2),'utf8');unreal.SystemLibrary.quit_editor()
 except Exception:
  (OUT/'native-render-error.txt').write_text(traceback.format_exc(),'utf8');unreal.log_error(traceback.format_exc());unreal.unregister_slate_post_tick_callback(handle);unreal.SystemLibrary.quit_editor()
 finally:busy=False
handle=unreal.register_slate_post_tick_callback(tick)
