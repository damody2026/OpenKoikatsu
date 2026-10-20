"""Render identical close-up cameras from the actual before/after GLBs."""
import bpy,sys
from pathlib import Path
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'output/street-social-city-production/interior-repair-v1'
for version,source in [('before',OUT/'before/S01/S01-complete-2k.glb'),('after',ROOT/'output/street-social-city-production/S01-modular-components-v1/assembly-building/S01-complete-2k.glb')]:
 bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=str(source));scene=bpy.context.scene;scene.frame_set(1);scene.render.engine='CYCLES';scene.cycles.samples=24;scene.render.resolution_x=700;scene.render.resolution_y=700;scene.render.resolution_percentage=100;scene.world=bpy.data.worlds.new('Interior review');scene.world.use_nodes=True;scene.world.node_tree.nodes['Background'].inputs[0].default_value=(.5,.5,.5,1);scene.world.node_tree.nodes['Background'].inputs[1].default_value=.35
 target=Vector((.2,-8,8.25));camera_position=Vector((-1.05,-6.35,9.2))
 for location,power in [((-1,-6.5,10.3),80),((2.5,-7,10),50)]:
  bpy.ops.object.light_add(type='AREA',location=location);o=bpy.context.object;o.data.energy=power;o.data.size=2;o.rotation_euler=(target-o.location).to_track_quat('-Z','Y').to_euler()
 bpy.ops.object.camera_add(location=camera_position);camera=bpy.context.object;camera.rotation_euler=(target-camera_position).to_track_quat('-Z','Y').to_euler();camera.data.lens=55;scene.camera=camera;scene.render.filepath=str(OUT/f'S01-kitchen-basin-{version}.png');bpy.ops.render.render(write_still=True)
