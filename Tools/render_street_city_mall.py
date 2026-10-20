"""Actual model review, including all ten occupied floors."""
import bpy,sys,math,json
from pathlib import Path
from mathutils import Vector
from bpy_extras.object_utils import world_to_camera_view
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_mall import OUT,sha
a=json.loads((OUT/'acceptance.json').read_text('utf8'));assert a['status']=='passed' and a['model_sha256']==sha(OUT/'M1-complete-2k.glb')
bpy.ops.wm.open_mainfile(filepath=str(OUT/'M1-complete-2k.blend'));sc=bpy.context.scene;sc.render.engine='CYCLES';sc.cycles.samples=12;sc.cycles.use_denoising=True;sc.render.resolution_x=1600;sc.render.resolution_y=1100;sc.render.resolution_percentage=100
sc.world=bpy.data.worlds.new('Mall studio');sc.world.use_nodes=True;sc.world.node_tree.nodes['Background'].inputs[0].default_value=(.7,.76,.82,1);sc.world.node_tree.nodes['Background'].inputs[1].default_value=.65
from street_city_render_device import configure
configure(sc)
for xyz,energy in [((0,-100,200),350000),((120,80,150),250000)]:
 bpy.ops.object.light_add(type='AREA',location=xyz);o=bpy.context.object;o.data.energy=energy;o.data.size=150;o.rotation_euler=(Vector((0,0,15))-o.location).to_track_quat('-Z','Y').to_euler()
bpy.ops.object.camera_add(location=(135,-175,85));cam=bpy.context.object;cam.data.type='ORTHO';cam.data.clip_end=10000;sc.camera=cam
def render(name,target):
 cam.rotation_euler=(Vector(target)-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.ortho_scale=210;bpy.context.view_layer.update();pts=[o.matrix_world@Vector(v) for o in sc.objects if o.type=='MESH' and not o.hide_render for v in o.bound_box];inv=cam.matrix_world.inverted();ps=[inv@v for v in pts];lo=[min(p[k] for p in ps) for k in [0,1]];hi=[max(p[k] for p in ps) for k in [0,1]];cam.location+=cam.matrix_world.to_quaternion()@Vector(((lo[0]+hi[0])/2,(lo[1]+hi[1])/2,0));bpy.context.view_layer.update();ratio=max(max(abs(p.x*2-1),abs(p.y*2-1)) for v in pts for p in [world_to_camera_view(sc,cam,v)]);cam.data.ortho_scale*=ratio*1.1;sc.render.filepath=str(OUT/name);bpy.ops.render.render(write_still=True)
render('building-preview.png',(0,0,18))
cam.data.type='PERSP';cam.data.lens=30;cam.location=(19,-70,8);cam.rotation_euler=(Vector((0,-49.8,5))-cam.location).to_track_quat('-Z','Y').to_euler();sc.render.filepath=str(OUT/'entrance-closeup-preview.png');bpy.ops.render.render(write_still=True)
cam.data.type='ORTHO'
for n in range(1,11):
 for o in sc.objects:
  if o.type=='MESH':o.hide_render=o.get('floor')!=n or o.get('part_kind') in ('ceiling','boutique_ceiling','boutique_canopy')
 cam.location=(90,-130,(n-1)*4.2+180);render(f'F{n:02d}-preview.png',(0,0,(n-1)*4.2))
for name,n,pos,target in [('retail-closeup-preview.png',1,(-57,15,1.7),(-47,23,1.5)),('dining-closeup-preview.png',9,(-56,15,35.3),(-48,23,35.0)),('escalator-closeup-preview.png',2,(22,-28,23),(0,0,6.3))]:
 for o in sc.objects:
  if o.type=='MESH':
   o.hide_render=(o.get('floor') not in [2,3] if name.startswith('escalator-') else o.get('floor')!=n) or o.get('part_kind')=='ceiling'
   # Two-storey cutaway: hide the top storey's outgoing bodies, whose
   # destination is outside this review. Keep the actual F03 arrival decks.
   if name.startswith('escalator-') and o.get('floor')==3 and o.get('part_kind') in ('escalator_tread','escalator_structure','handrail','stair_tread','stair_structure'):o.hide_render=True
 cam.data.type='PERSP';cam.data.lens=24;cam.location=pos;cam.rotation_euler=(Vector(target)-cam.location).to_track_quat('-Z','Y').to_euler();sc.render.filepath=str(OUT/name);bpy.ops.render.render(write_still=True)
(OUT/'render-receipt.json').write_text(json.dumps(dict(status='passed',model_sha256=a['model_sha256'],renderer_sha256=sha(Path(__file__)),images={p.name:sha(p) for p in OUT.glob('*-preview.png')}),indent=2),'utf8')
