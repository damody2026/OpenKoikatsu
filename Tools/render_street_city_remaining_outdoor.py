"""Render the exact adopted outdoor Blender assemblies using the actual Cycles GPU."""
import bpy,sys,json,math
from pathlib import Path
from mathutils import Vector
from bpy_extras.object_utils import world_to_camera_view
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_remaining_outdoor import OUT
from street_city_render_device import configure
from street_city_remesh_gate import sha
r=json.loads((OUT/'assembly-report.json').read_text('utf8'));a=json.loads((OUT/'acceptance.json').read_text('utf8'));assert a['status']=='passed' and a['assembly_sha256']==sha(OUT/'assembly-report.json');images=[]
for row in r['models']:
 if not row['id'].startswith('PK-'):continue
 assert sha(row['source'])==row['sha256'];bpy.ops.wm.open_mainfile(filepath=str(Path(row['source']).with_suffix('.blend')));sc=bpy.context.scene;sc.render.engine='CYCLES';sc.cycles.samples=16;sc.cycles.use_denoising=True;device=configure(sc);sc.render.resolution_x=1600;sc.render.resolution_y=1100;sc.render.resolution_percentage=100
 pts=[o.matrix_world@Vector(v) for o in sc.objects if o.type=='MESH' for v in o.bound_box];lo=Vector(tuple(min(v[k] for v in pts) for k in range(3)));hi=Vector(tuple(max(v[k] for v in pts) for k in range(3)));target=(lo+hi)/2;span=max(hi.x-lo.x,hi.y-lo.y,10)
 sc.world=bpy.data.worlds.new('Outdoor daylight');sc.world.use_nodes=True;sc.world.node_tree.nodes['Background'].inputs[0].default_value=(.69,.77,.85,1);sc.world.node_tree.nodes['Background'].inputs[1].default_value=.5
 for off,power in [((-1,-1,2),12),((1,1,2),8)]:
  bpy.ops.object.light_add(type='AREA',location=target+Vector(off)*span);ob=bpy.context.object;ob.data.energy=power*span*span;ob.data.size=span;ob.rotation_euler=(target-ob.location).to_track_quat('-Z','Y').to_euler()
 bpy.ops.object.camera_add(location=target+Vector((.85,-1.15,1.15))*span);cam=bpy.context.object;cam.rotation_euler=(target-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.type='ORTHO';cam.data.ortho_scale=span*1.8;sc.camera=cam;bpy.context.view_layer.update();radius=max(max(abs(p.x*2-1),abs(p.y*2-1)) for v in pts for p in [world_to_camera_view(sc,cam,v)]);cam.data.ortho_scale*=radius*1.12
 path=OUT/(row['id']+'-preview.png');sc.render.filepath=str(path);bpy.ops.render.render(write_still=True);images.append(dict(id=row['id'],source_sha256=row['sha256'],image=str(path),image_sha256=sha(path),device=device))
(OUT/'render-receipt.json').write_text(json.dumps(dict(status='rendered_actual_models',images=images),indent=2),'utf8')
