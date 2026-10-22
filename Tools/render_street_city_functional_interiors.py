"""Photographs of the actual revised models, without illustration substitutions."""
import bpy,sys,math,argparse,json
from pathlib import Path
from mathutils import Vector,Matrix
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_functional_revision import ROOT,OUT,read,sha
from street_city_render_device import configure
p=argparse.ArgumentParser();p.add_argument('--building',required=True);a=p.parse_args(sys.argv[sys.argv.index('--')+1:]);bid=a.building;r=read(read(OUT/'buildings'/bid/'latest.json')['receipt']);actual_model=r['model'];candidate=OUT.parent/'world-wall-grok-v1/buildings'/bid/'model.json'
if candidate.exists():
 themed=read(candidate)
 if themed['source_sha256']==r['model_sha256']:actual_model=themed['model']
bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=actual_model);sc=bpy.context.scene;sc.frame_set(1);sc.render.engine='CYCLES';sc.cycles.samples=24;sc.cycles.use_denoising=True;configure(sc);sc.render.resolution_x=1400;sc.render.resolution_y=1000;sc.render.resolution_percentage=100
sc.world=bpy.data.worlds.new('Review ambient');sc.world.use_nodes=True;sc.world.node_tree.nodes['Background'].inputs[0].default_value=(.65,.72,.8,1);sc.world.node_tree.nodes['Background'].inputs[1].default_value=.35
images=[];folder=OUT/'previews';folder.mkdir(exist_ok=True)
def camera(pos,target,lens=22):
 if sc.camera:bpy.data.objects.remove(sc.camera,do_unlink=True)
 bpy.ops.object.camera_add(location=pos);sc.camera=bpy.context.object;sc.camera.rotation_euler=(Vector(target)-Vector(pos)).to_track_quat('-Z','Y').to_euler();sc.camera.data.lens=lens;sc.camera.data.clip_end=1000
def area(pos,target,power,size):
 bpy.ops.object.light_add(type='AREA',location=pos);o=bpy.context.object;o.data.energy=power;o.data.size=size;o.rotation_euler=(Vector(target)-Vector(pos)).to_track_quat('-Z','Y').to_euler()
def save(stem):
 path=folder/(stem+'.png');sc.render.filepath=str(path);bpy.ops.render.render(write_still=True);images.append(dict(path=str(path),sha256=sha(path),model_sha256=sha(actual_model)))
if bid=='M1':
 camera((170,-205,105),(0,0,17),42);area((0,-100,160),(0,0,15),450000,130);area((-140,60,110),(0,0,15),220000,120);save('M1-opaque-exterior')
else:
 from street_city_campus_contract import IDS,context as campus
 from street_city_remaining_contract import context as remaining
 c=campus(bid) if bid in IDS else remaining(bid)
 if r['classrooms']:
  chosen=next((q for q in r['classrooms'] if q['tiered']),r['classrooms'][0]);rid=chosen['room'];room=c['rooms'][rid];origin=Vector(room['position_m']);rotation=Matrix.Rotation(math.radians(room['rotation_z_deg']),4,'Z');h=room['clear_size_m'][2]
  def xyz(v):return origin+rotation@Vector(v)
  area(xyz((0,0,h-.35)),xyz((0,0,0)),850,5);area(xyz((0,2,h-.4)),xyz((0,-1,0)),650,4);camera(xyz((0,-4.25,2.7)),xyz((0,2.4,1.7)),18);save(bid+'-centered-blackboard-screen')
  if chosen['tiered']:camera(xyz((0,3.6,2.8)),xyz((0,-1.3,.9)),18);save(bid+'-lecture-tiers')
 else:
  chosen=r['administration'][0];rid=chosen['room'];room=c['rooms'][rid];origin=Vector(room['position_m']);rotation=Matrix.Rotation(math.radians(room['rotation_z_deg']),4,'Z');h=room['clear_size_m'][2]
  def xyz(v):return origin+rotation@Vector(v)
  computers=[q for q in r['additions'] if q['owner']==rid and q['kind']=='office_computer'];target=Vector(computers[0]['position_m'])+Vector((0,0,.95));yaw=math.radians(computers[0]['yaw']);direction=Vector((math.sin(yaw),-math.cos(yaw),0));camera(target+direction*3+Vector((1,0,1.25)),target,28);area(xyz((0,0,h-.35)),target,1300,6);area(target+Vector((0,0,1.7)),target,350,2);save(bid+'-office-computer')
(folder/(bid+'-receipt.json')).write_text(json.dumps(dict(building=bid,model=actual_model,model_sha256=sha(actual_model),geometry_source_sha256=r['model_sha256'],images=images),indent=2),'utf8')
