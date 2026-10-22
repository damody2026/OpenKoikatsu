"""Eye-level close-ups of actual revised walls, with metric pattern scale and occlusion checks."""
import bpy,sys,json,math
from pathlib import Path
from mathutils import Vector
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_world_wall_art import ROOT,OUT,CITY,sha
from street_city_render_device import configure
rows=[]
for bid in CITY:
 r=json.loads((OUT/'buildings'/bid/'model.json').read_text('utf8'));source=r['model'];bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=source);bpy.context.view_layer.update();sc=bpy.context.scene;deps=bpy.context.evaluated_depsgraph_get();candidates=[]
 for o in sc.objects:
  if o.type!='MESH':continue
  matrix=o.matrix_world.to_3x3().inverted().transposed()
  for p in o.data.polygons:
   m=o.data.materials[p.material_index];key=str(m.get('grok_texture_id',''));co=o.matrix_world@p.center;n=(matrix@p.normal).normalized()
   if key=='city/'+bid+'/lobby' and 1<co.z<3.8 and abs(n.z)<.2 and p.area>1:candidates.append((p.area,co,n,key))
 assert candidates,bid
 chosen=None
 for area,target,n,key in sorted(candidates,key=lambda x:x[0],reverse=True):
  for distance in [2.2,1.6,1.0]:
   location=target+n*distance;direction=(target-location).normalized();hit=sc.ray_cast(deps,location,direction,distance=distance-.04)
   if not hit[0]:chosen=(target,n,location,key);break
  if chosen:break
 assert chosen,('No unobstructed wall camera',bid)
 target,n,location,key=chosen
 # Temporary review cutaway only. Never save geometry or hide flags to delivery models.
 for o in sc.objects:
  if o.type=='MESH':
   zs=[(o.matrix_world@Vector(v)).z for v in o.bound_box]
   if sum(zs)/len(zs)>4.1:o.hide_render=True
 sc.render.engine='CYCLES';sc.cycles.samples=24;sc.cycles.use_denoising=True;configure(sc);sc.render.resolution_x=1400;sc.render.resolution_y=1050;sc.render.resolution_percentage=100
 sc.world=bpy.data.worlds.new('Neutral wall studio');sc.world.use_nodes=True;sc.world.node_tree.nodes['Background'].inputs[0].default_value=(.8,.85,.9,1);sc.world.node_tree.nodes['Background'].inputs[1].default_value=.35
 side=n.cross(Vector((0,0,1))).normalized()
 for off,power in [(side*1.2+Vector((0,0,.5)),300),(side*-.7+Vector((0,0,1.2)),200)]:
  bpy.ops.object.light_add(type='AREA',location=location+off);l=bpy.context.object;l.data.energy=power;l.data.size=2;l.rotation_euler=(target-l.location).to_track_quat('-Z','Y').to_euler()
 bpy.ops.object.camera_add(location=location);cam=bpy.context.object;cam.rotation_euler=(target-location).to_track_quat('-Z','Y').to_euler();cam.data.type='ORTHO';cam.data.ortho_scale=3.2;cam.data.clip_start=.05;sc.camera=cam
 path=OUT/'buildings'/bid/'wall-closeup.png';sc.render.filepath=str(path);bpy.ops.render.render(write_still=True);rows.append(dict(building=bid,image=str(path),image_sha256=sha(path),source_sha256=sha(source),texture_id=key,view_width_m=3.2,pattern_repeat_m=2,renderer='Actual Blender PBR wall, no conceptual image substitution'))
(OUT/'closeup-renders.json').write_text(json.dumps(dict(status='passed_actual_models',images=rows),indent=2),'utf8')
