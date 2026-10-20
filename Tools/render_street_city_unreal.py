"""Render the saved Unreal map with real materials, using offscreen scene capture."""
import unreal,json,math,sys
from pathlib import Path
ROOT=Path('C:/portable/OpenKoikatsu');OUT=ROOT/'output/street-social-city-production/world-ground-v1';result=json.loads((OUT/'unreal-acceptance.json').read_text('utf-8'));assert result['status']=='passed';assert unreal.EditorLevelLibrary.load_level(result['map']);world=unreal.EditorLevelLibrary.get_editor_world();sub=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);actors=sub.get_all_level_actors();records=[]
managed={a.get_actor_label():a for a in actors if 'StreetCityManaged' in [str(t) for t in a.tags]};assert set(managed)=={row['id'] for row in result['actors']},'Saved map did not reload all verified actors'
for row in result['actors']:
 location=managed[row['id']].get_actor_location();assert max(abs(a-b) for a,b in zip([location.x,location.y,location.z],row['position_cm']))<.005,'Saved actor position changed'
binding_report=json.loads((OUT/'unreal-material-bindings.json').read_text('utf-8'));assert binding_report['status']=='passed'
by_mesh={actor.static_mesh_component.static_mesh.get_path_name():actor for actor in managed.values()}
for binding in binding_report['bindings']:
 material=by_mesh[binding['mesh']].static_mesh_component.get_material(binding['slot'])
 assert material is not None and material.get_path_name()==binding['material'],'Saved scene material binding changed'
material_probe={}
for label in ['S01_F03','S25_F03','S26_F03','S27_F03']:
 c=managed[label].static_mesh_component
 material_probe[label]=dict(overrides=[str(v) for v in c.get_editor_property('override_materials')],materials=[str(c.get_material(i)) for i in range(c.get_num_materials())])
for label in material_probe:
 c=managed[label].static_mesh_component
 material_probe[label]['graphs']=[dict(path=c.get_material(i).get_path_name(),expressions=unreal.MaterialEditingLibrary.get_num_material_expressions(c.get_material(i)),base_input=str(unreal.MaterialEditingLibrary.get_material_property_input_node(c.get_material(i),unreal.MaterialProperty.MP_BASE_COLOR))) for i in range(c.get_num_materials()) if isinstance(c.get_material(i),unreal.Material)]
(OUT/'unreal-material-probe.json').write_text(json.dumps(material_probe,indent=2),'utf-8')
materials={}
for actor in managed.values():
 c=actor.static_mesh_component
 for i in range(c.get_num_materials()):
  material=c.get_material(i);assert material is not None,(actor.get_actor_label(),i,'Empty persisted material slot')
  if isinstance(material,unreal.Material):materials[material.get_path_name()]=material
for material in materials.values():unreal.MaterialEditingLibrary.recompile_material(material)
unreal.AutomationLibrary.finish_loading_before_screenshot()
def dot(a,b):return a.x*b.x+a.y*b.y+a.z*b.z
def spawn(cls,location,rotation):
 actor=sub.spawn_actor_from_class(cls,location,rotation);assert actor is not None,'Full Editor context required';return actor
def render_views():
 for name,selection in [('unreal-world-overview',[a for a in actors if a.get_actor_label().startswith('G') or a.get_actor_label().startswith('S') and '_F' in a.get_actor_label()]),('unreal-shop-street',[a for a in actors if a.get_actor_label().startswith('S') and '_F' in a.get_actor_label()])]:
  points=[]
  for actor in selection:
   origin,extent=actor.get_actor_bounds(False)
   for x in (-1,1):
    for y in (-1,1):
     for z in (-1,1):points.append(origin+unreal.Vector(x*extent.x,y*extent.y,z*extent.z))
  lo=unreal.Vector(*[min(getattr(p,k) for p in points) for k in ['x','y','z']]);hi=unreal.Vector(*[max(getattr(p,k) for p in points) for k in ['x','y','z']]);center=(lo+hi)*.5;extent=max(hi.x-lo.x,hi.y-lo.y);location=center+unreal.Vector(-extent*.55,-extent*.55,extent*.9);rotation=unreal.MathLibrary.find_look_at_rotation(location,center);right=unreal.MathLibrary.get_right_vector(rotation);up=unreal.MathLibrary.get_up_vector(rotation);horizontal=[dot(p-location,right) for p in points];vertical=[dot(p-location,up) for p in points];location+=right*((min(horizontal)+max(horizontal))/2)+up*((min(vertical)+max(vertical))/2);width=max(max(horizontal)-min(horizontal),(max(vertical)-min(vertical))*16/9)*1.08
  actor=spawn(unreal.SceneCapture2D,location,rotation);component=actor.capture_component2d;target=unreal.RenderingLibrary.create_render_target2d(world,1920,1080,unreal.TextureRenderTargetFormat.RTF_RGBA8);component.set_editor_property('texture_target',target);component.set_editor_property('projection_type',unreal.CameraProjectionMode.ORTHOGRAPHIC);component.set_editor_property('ortho_width',width);component.set_editor_property('capture_source',unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR);component.set_editor_property('capture_every_frame',False);component.capture_scene();unreal.RenderingLibrary.export_render_target(world,target,str(OUT),name+'.png');path=OUT/(name+'.png');assert path.exists() and path.stat().st_size>100;records.append(dict(file=str(path),bytes=path.stat().st_size,ortho_width_cm=width,location_cm=[location.x,location.y,location.z],rotation=[rotation.pitch,rotation.yaw,rotation.roll]));unreal.log('Rendered '+name)
  # Capture actor is transient for the render run; never save it to the game map.
  sub.destroy_actor(actor)
 (OUT/'unreal-render.json').write_text(json.dumps(dict(status='passed',images=records,renderer='Unreal Engine SceneCapture2D FinalColorLDR, aspect-fitted actual actor bounds'),indent=2),'utf-8')

CITY_ASYNC_RENDER=True
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
frames=0

def on_tick(delta):
 global frames
 frames+=1
 if frames<60:return
 unreal.unregister_slate_post_tick_callback(tick_handle)
 task=dict(stage='Render',script='render_street_city_unreal.py')
 try:
  render_views()
  task['status']='succeeded'
 except Exception:
  import traceback
  task.update(status='failed',error=traceback.format_exc())
  unreal.log_error(task['error'])
 finally:
  (OUT/'unreal-render-task.json').write_text(json.dumps(task,indent=2),'utf-8')
  unreal.SystemLibrary.quit_editor()

tick_handle=unreal.register_slate_post_tick_callback(on_tick)
