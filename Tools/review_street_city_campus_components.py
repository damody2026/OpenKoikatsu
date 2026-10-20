"""Actual adopted Tripo geometry, front and back review before mass placement."""
import bpy,json,sys
from pathlib import Path
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'output/street-social-city-production/campus-v1/components';bpy.ops.wm.read_factory_settings(use_empty=True)
for i,aid in enumerate(['urinal','toilet-compact','basin-economy']):
 source_id=('lod-' if aid in ('urinal','toilet-compact') else 'tripo-')+aid
 before=set(bpy.data.objects);bpy.ops.import_scene.gltf(filepath=str(OUT/f'{source_id}/remesh/remeshed-candidate.glb'));new=set(bpy.data.objects)-before
 for o in [q for q in new if q.parent is None]:o.location.x+=i
sc=bpy.context.scene;sc.render.engine='CYCLES';sc.cycles.samples=20;sc.render.resolution_x=1200;sc.render.resolution_y=700;sc.render.resolution_percentage=100;sc.world=bpy.data.worlds.new('studio');sc.world.use_nodes=True;sc.world.node_tree.nodes['Background'].inputs[0].default_value=(.6,.6,.6,1);target=Vector((1,0,.4))
for loc in [(-1,-3,4),(3,2,4)]:
 bpy.ops.object.light_add(type='AREA',location=loc);light=bpy.context.object;light.data.energy=250;light.data.size=4;light.rotation_euler=(target-light.location).to_track_quat('-Z','Y').to_euler()
for view,loc in [('front',(3,-5,3)),('back',(-1,5,3))]:
 bpy.ops.object.camera_add(location=loc);camera=bpy.context.object;camera.rotation_euler=(target-camera.location).to_track_quat('-Z','Y').to_euler();camera.data.type='ORTHO';camera.data.ortho_scale=3.5;sc.camera=camera;sc.render.filepath=str(OUT/f'sanitary-{view}.png');bpy.ops.render.render(write_still=True)
