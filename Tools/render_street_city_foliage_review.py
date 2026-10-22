import bpy,sys,math
from pathlib import Path
from mathutils import Vector
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_streetscape import OUT,read,write,sha
from street_city_render_device import configure
rows=[]
for asset in read(OUT/'foliage-assets.json')['assets']:
 bpy.ops.wm.read_factory_settings(use_empty=True);sc=bpy.context.scene;sc.render.engine='CYCLES';sc.cycles.samples=24;sc.cycles.transparent_max_bounces=24;sc.cycles.use_denoising=True;configure(sc);sc.render.resolution_x=1500;sc.render.resolution_y=850;sc.render.resolution_percentage=100;sc.world=bpy.data.worlds.new('Soft sky');sc.world.use_nodes=True;sc.world.node_tree.nodes['Background'].inputs[0].default_value=(.7,.8,.9,1);sc.world.node_tree.nodes['Background'].inputs[1].default_value=.65
 for level in asset['lods']:
  before=set(sc.objects);bpy.ops.import_scene.gltf(filepath=level['path']);bpy.context.view_layer.update()
  for ob in set(sc.objects)-before:
   if ob.parent is None:ob.location.x+=(level['level']-1)*9
 bpy.ops.mesh.primitive_plane_add(size=200);ground=bpy.context.object;m=bpy.data.materials.new('Preview floor');m.diffuse_color=(.42,.48,.36,1);ground.data.materials.append(m)
 bpy.ops.object.light_add(type='SUN',location=(0,-15,30));sun=bpy.context.object;sun.rotation_euler=(math.radians(20),math.radians(-25),math.radians(-30));sun.data.energy=2;sun.data.angle=.15
 bpy.ops.object.camera_add(location=(14,-40,14));camera=bpy.context.object;camera.rotation_euler=(Vector((0,0,3.7))-camera.location).to_track_quat('-Z','Y').to_euler();camera.data.type='ORTHO';camera.data.ortho_scale=30;sc.camera=camera;folder=OUT/'previews';folder.mkdir(exist_ok=True);p=folder/(asset['id']+'-lod-cards.png');sc.render.filepath=str(p);bpy.ops.render.render(write_still=True);rows.append(dict(asset=asset['id'],image=str(p),sha256=sha(p),model_sha256=[q['sha256'] for q in asset['lods']]))
write(OUT/'foliage-renders.json',dict(status='rendered_requires_visual_review',images=rows))
