"""Eighty model renders at standing eye height; never AI concept substitutes."""
import bpy,sys,json,math,os
from pathlib import Path
from mathutils import Vector
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_mall import OUT as MALL,sha
from street_city_mall_boutiques import OUT,profiles,publish
bpy.ops.wm.open_mainfile(filepath=str(MALL/'M1-complete-2k.blend'))
sc=bpy.context.scene;sc.render.engine='CYCLES';sc.cycles.samples=12;sc.cycles.use_denoising=True;sc.cycles.max_bounces=5
from street_city_render_device import configure
device=configure(sc)
sc.render.resolution_x=1100;sc.render.resolution_y=760;sc.render.resolution_percentage=100
sc.world=bpy.data.worlds.new('Boutique ambient');sc.world.use_nodes=True;sc.world.node_tree.nodes['Background'].inputs[0].default_value=(.8,.85,.9,1);sc.world.node_tree.nodes['Background'].inputs[1].default_value=.35
bpy.ops.object.camera_add();cam=bpy.context.object;cam.data.lens=24;cam.data.clip_start=.05;sc.camera=cam
lights=[]
for i in range(2):
 bpy.ops.object.light_add(type='AREA');o=bpy.context.object;o.data.shape='DISK';o.data.size=8;o.data.energy=1700;lights.append(o)
(OUT/'previews').mkdir(exist_ok=True);receipt=dict(status='rendering',model_sha256=sha(MALL/'M1-complete-2k.glb'),renderer_sha256=sha(__file__),device=device,device_config_sha256=sha(Path(__file__).with_name('street_city_render_device.py')),images={})
previous={}
if (OUT/'render-receipt.json').exists():
 old=json.loads((OUT/'render-receipt.json').read_text('utf8'))
 if old.get('model_sha256')==receipt['model_sha256'] and old.get('renderer_sha256')==receipt['renderer_sha256']:previous=old.get('images',{})
selection=os.environ.get('STREET_CITY_BOUTIQUE_SAMPLE','').split(',')
for p in profiles().values():
 if selection!=[''] and p['id'] not in selection:continue
 if p['id'] in previous and Path(previous[p['id']]['path']).exists() and sha(previous[p['id']]['path'])==previous[p['id']]['sha256']:
  receipt['images'][p['id']]=previous[p['id']];continue
 n=p['floor'];z=(n-1)*4.2;a,b,c,d=p['bounds'];cx=(a+c)/2;cy=(b+d)/2;positive=cy>0;front=b+1.1 if positive else d-1.1
 for o in sc.objects:
  if o.type=='MESH':o.hide_render=o.get('floor')!=n or o.get('part_kind')=='ceiling'
 # Look across the trading floor from a clear front corner. The old centred
 # back-wall view could put both new display islands outside the frame.
 cam.location=(a+2,b+3.3 if positive else d-3.3,z+1.75);target=Vector((cx,cy,z+1.15))
 if n>=9:
  cam.location=(cx-4,cy+(-6 if positive else 6),z+1.75);target=Vector((c-1,cy+(-1 if positive else 1),z+1.6))
 cam.rotation_euler=(target-cam.location).to_track_quat('-Z','Y').to_euler()
 for light,xy in zip(lights,[(cx-4,cy-3),(cx+4,cy+3)]):
  light.location=(*xy,z+3.5);light.rotation_euler=(Vector((cx,cy,z))-light.location).to_track_quat('-Z','Y').to_euler()
 path=OUT/'previews'/(p['id']+'.png');sc.render.filepath=str(path);bpy.ops.render.render(write_still=True)
 receipt['images'][p['id']]=dict(path=str(path),sha256=sha(path),floor=n,eye_height_m=1.75,ceiling_rafts_hidden_for_cutaway=False)
 (OUT/'render-receipt.json').write_text(json.dumps(receipt,indent=2),'utf8');print('BOUTIQUE RENDERED',p['id'],flush=True)
receipt['status']='passed' if len(receipt['images'])==80 else 'sample_preview';(OUT/'render-receipt.json').write_text(json.dumps(receipt,indent=2),'utf8');publish()
