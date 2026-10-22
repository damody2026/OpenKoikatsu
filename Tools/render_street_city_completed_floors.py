"""Actual cutaway review for every floor, including newly separated water surfaces."""
import bpy,sys,json,math
from pathlib import Path
from mathutils import Vector
from bpy_extras.object_utils import world_to_camera_view
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_remaining_contract import IDS,context,OUT
from street_city_remesh_gate import sha
from street_city_render_device import configure
images=[]
for bid in IDS:
 c=context(bid);folder=c['out'];acc=json.loads((folder/'acceptance.json').read_text('utf8'));source=folder/f'{bid}-complete-2k.glb';assert acc['status']=='passed' and sha(source)==acc['sha256'];bpy.ops.wm.open_mainfile(filepath=str(source.with_suffix('.blend')));sc=bpy.context.scene;sc.render.engine='CYCLES';sc.cycles.samples=12;sc.cycles.use_denoising=True;device=configure(sc);sc.render.resolution_x=1600;sc.render.resolution_y=1100;sc.render.resolution_percentage=100;W,D=c['building']['footprint_m'];span=max(W,D)
 sc.world=bpy.data.worlds.new('Floor review daylight');sc.world.use_nodes=True;sc.world.node_tree.nodes['Background'].inputs[0].default_value=(.7,.77,.84,1);sc.world.node_tree.nodes['Background'].inputs[1].default_value=.55
 for off,power in [((-1,-1,2),12),((1,1,2),9)]:
  bpy.ops.object.light_add(type='AREA',location=Vector(off)*span);o=bpy.context.object;o.data.energy=power*span*span;o.data.size=span;o.rotation_euler=(-o.location).to_track_quat('-Z','Y').to_euler()
 bpy.ops.object.camera_add();cam=bpy.context.object;cam.data.type='ORTHO';sc.camera=cam
 for floor in c['building']['floors']:
  n=floor['floor'];z=floor['z_m'];target=Vector((0,0,z+1));cam.location=target+Vector((.7,-1.05,1.6))*span;cam.rotation_euler=(target-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.ortho_scale=span*1.5
  for o in sc.objects:
   if o.type=='MESH':o.hide_render=o.get('floor')!=n or o.get('part_kind') in ('ceiling','light')
  bpy.context.view_layer.update();pts=[o.matrix_world@Vector(v) for o in sc.objects if o.type=='MESH' and not o.hide_render for v in o.bound_box];radius=max(max(abs(p.x*2-1),abs(p.y*2-1)) for v in pts for p in [world_to_camera_view(sc,cam,v)]);cam.data.ortho_scale*=radius*1.1
  path=folder/f'F{n:02d}-review.png';sc.render.filepath=str(path);bpy.ops.render.render(write_still=True);images.append(dict(building=bid,floor=n,image=str(path),image_sha256=sha(path),source_sha256=acc['sha256'],device=device))
(OUT/'floor-render-acceptance.json').write_text(json.dumps(dict(status='rendered_actual_models',floors=len(images),images=images,renderer_sha256=sha(__file__)),indent=2),'utf8')
