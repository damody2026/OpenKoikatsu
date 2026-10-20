"""Actual Blender geometry top views, retaining incoming stairs as context."""
import bpy,sys,argparse
from pathlib import Path
from mathutils import Vector
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_building_context import context
p=argparse.ArgumentParser();p.add_argument('--building',default='S01');a=p.parse_args(sys.argv[sys.argv.index('--')+1:]);C=context(a.building);BID=C['id'];OUT=C['out']
bpy.ops.wm.open_mainfile(filepath=str(OUT/f'{BID}-complete-2k.blend'));sc=bpy.context.scene;meshes=[o for o in sc.objects if o.type=='MESH'];sc.render.engine='CYCLES';sc.cycles.samples=24;sc.render.resolution_x=1200;sc.render.resolution_y=1400;sc.render.resolution_percentage=100
sc.world=bpy.data.worlds.new('plan studio');sc.world.use_nodes=True;sc.world.node_tree.nodes['Background'].inputs[0].default_value=(.65,.65,.65,1)
bpy.ops.object.light_add(type='AREA',location=(0,-3,28));o=bpy.context.object;o.data.energy=4500;o.data.size=12
bpy.ops.object.camera_add();camera=bpy.context.object;sc.camera=camera;camera.data.type='ORTHO';camera.data.ortho_scale=22
for floor,z in [(1,0),(2,3.8),(3,7.6)]:
 sc.frame_set({1:1,2:80,3:160}[floor]);bpy.context.view_layer.update()
 for o in meshes:
  context=o.get('floor')==floor-1 and o.get('part_kind') in ('stair_tread','stair_landing','handrail','stringer')
  o.hide_render=(o.get('floor')!=floor and not context) or o.get('part_kind')=='ceiling' or o.get('asset_id') in ('ceiling-light','signboard','facade-band')
 camera.location=(0,-7,z+52);target=Vector((0,0,z+1));camera.rotation_euler=(target-camera.location).to_track_quat('-Z','Y').to_euler();sc.render.filepath=str(OUT/f'{BID}-F{floor:02d}-plan.png');bpy.ops.render.render(write_still=True)
