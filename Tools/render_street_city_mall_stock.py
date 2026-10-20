"""Actual assembled product close-ups for every tenant, not isolated mockups."""
import bpy,sys,json,math
from pathlib import Path
from mathutils import Vector
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_mall import OUT as MALL,sha
from street_city_mall_merchandise import OUT
from street_city_render_device import configure
geometry=json.loads((MALL/'geometry.json').read_text('utf8'));stock=geometry['contract']['merchandise'];bpy.ops.wm.open_mainfile(filepath=str(MALL/'M1-complete-2k.blend'));sc=bpy.context.scene;sc.render.engine='CYCLES';device=configure(sc);sc.cycles.samples=20;sc.cycles.use_denoising=True;sc.cycles.max_bounces=6;sc.render.resolution_x=1100;sc.render.resolution_y=760;sc.render.resolution_percentage=100
sc.world=bpy.data.worlds.new('Actual stocked concession review');sc.world.use_nodes=True;sc.world.node_tree.nodes['Background'].inputs[0].default_value=(.8,.85,.9,1);sc.world.node_tree.nodes['Background'].inputs[1].default_value=.35
bpy.ops.object.camera_add();camera=bpy.context.object;camera.data.lens=45;camera.data.clip_start=.02;sc.camera=camera;lights=[]
for i in range(2):
 bpy.ops.object.light_add(type='AREA');o=bpy.context.object;o.data.energy=1100;o.data.shape='DISK';o.data.size=5;lights.append(o)
dest=OUT/'stores';dest.mkdir(exist_ok=True);report=dict(status='rendering',model_sha256=sha(MALL/'M1-complete-2k.glb'),renderer_sha256=sha(__file__),device=device,images={});previous={}
if (dest/'receipt.json').exists():
 old=json.loads((dest/'receipt.json').read_text('utf8'))
 if old['model_sha256']==report['model_sha256'] and old['renderer_sha256']==report['renderer_sha256']:previous=old['images']
for room in geometry['contract']['rooms']:
 rid=room['id'];no=room['floor'];z=(no-1)*4.2;path=dest/(rid+'.png')
 if rid in previous and path.exists() and sha(path)==previous[rid]['sha256']:report['images'][rid]=previous[rid];continue
 candidates=[]
 for surface in stock['surfaces']:
  if surface['tenant']!=rid:continue
  items=[q for q in stock['placements'] if q['support_id']==surface['id']]
  if items:candidates.append((len(items),surface,items))
 _,surface,items=max(candidates,key=lambda q:q[0]);points=[]
 for q in items:
  x,y,zz=q['pos'];w,d,h=q['size_m'];points.extend([Vector((x-w/2,y-d/2,zz)),Vector((x+w/2,y+d/2,zz+h))])
 lo=Vector(tuple(min(p[k] for p in points) for k in range(3)));hi=Vector(tuple(max(p[k] for p in points) for k in range(3)));target=(lo+hi)/2;extent=max(hi-lo);distance=max(.8,extent*1.7);sign=-1 if room['bounds'][1]>0 else 1
 camera.location=target+Vector((distance*.35,sign*distance*.92,max(.35,extent*.5)));camera.rotation_euler=(target-camera.location).to_track_quat('-Z','Y').to_euler()
 for obj in sc.objects:
  if obj.type=='MESH':obj.hide_render=obj.get('floor')!=no or obj.get('part_kind')=='ceiling'
 for light,delta in zip(lights,[(-2,sign*2,2.4),(2,-sign,2.5)]):
  light.location=target+Vector(delta);light.rotation_euler=(target-light.location).to_track_quat('-Z','Y').to_euler()
 sc.render.filepath=str(path);bpy.ops.render.render(write_still=True);report['images'][rid]=dict(path=str(path),sha256=sha(path),support_id=surface['id'],product_ids=sorted({q['product_id'] for q in items}),actual_camera_m=list(camera.location),view='actual assembled concession product close-up')
 (dest/'receipt.json').write_text(json.dumps(report,indent=2),'utf8');print('STOCK CLOSEUP',rid,flush=True)
report['status']='passed' if len(report['images'])==80 else 'incomplete';(dest/'receipt.json').write_text(json.dumps(report,indent=2),'utf8')
