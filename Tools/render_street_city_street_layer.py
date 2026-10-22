"""Actual assembled game assets photographed with nearby adopted buildings/terrain."""
import bpy,sys,math,argparse
from pathlib import Path
from mathutils import Vector,Matrix
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_streetscape import ROOT,BASE,OUT,read,write,sha
from street_city_render_device import configure
p=argparse.ArgumentParser();p.add_argument('--view',default='shop',choices=['shop','campus','mall','riverside']);a=p.parse_args(sys.argv[sys.argv.index('--')+1:]);views={'shop':([245,459,13.7],[310,459,14]),'campus':([575,680,21.7],[575,735,22]),'mall':([575,527.5,13.7],[650,527.5,15]),'riverside':([785,420,13.7],[785,485,14])};eye,target=map(Vector,views[a.view]);portable=read(OUT/'portable-layer.json');assert portable['delivery_sha256']==sha(OUT/'delivery.json');bpy.ops.wm.open_mainfile(filepath=portable['blend']);sc=bpy.context.scene;sc.render.engine='CYCLES';sc.cycles.samples=24;sc.cycles.use_denoising=True;configure(sc);sc.render.resolution_x=1600;sc.render.resolution_y=1000;sc.render.resolution_percentage=100;sc.world=bpy.data.worlds.new('Street daytime');sc.world.use_nodes=True;sc.world.node_tree.nodes['Background'].inputs[0].default_value=(.63,.76,.89,1);sc.world.node_tree.nodes['Background'].inputs[1].default_value=.6
for ob in sc.objects:
 if ob.get('street_asset'):
  distance=(ob.matrix_world.translation-eye).length;limit=650 if ob['street_asset'] in ('camphor','cherry') else 220 if ob['street_asset'].startswith('lamp') else 100 if ob['street_asset'] in ('flowers','bicycle','scooter') else 180;ob.hide_render=distance>limit
inventory=read(BASE/'scene-integration-v1/inventory.json')['rows'];themes=read(BASE/'world-wall-grok-v1/models.json')['buildings'];floorrows={q['id']:q for q in inventory};imported=[]
def add(path,position,yaw):
 before=set(bpy.data.objects);bpy.ops.import_scene.gltf(filepath=str(path));bpy.context.view_layer.update();transform=Matrix.Translation(Vector(position))@Matrix.Rotation(math.radians(yaw),4,'Z')
 for ob in set(bpy.data.objects)-before:
  if ob.parent is None:ob.matrix_world=transform@ob.matrix_world
 imported.append(str(path))
for building in themes:
 bid=building['building'];label=('Mall_M1_F01' if bid=='M1' else 'Campus_'+bid+'_F01' if 'Campus_'+bid+'_F01' in floorrows else 'City_'+bid+'_F01' if 'City_'+bid+'_F01' in floorrows else bid+'_F01');q=floorrows[label];north,east,z=q['position_cm'];pos=Vector((east/100,north/100,z/100))
 if (Vector((pos.x,pos.y,eye.z))-eye).length<170:add(building['model'],pos,90-q['yaw_deg'])
for q in inventory:
 if q['kind'] not in ('ground','water','outdoor','world_facility') or q.get('visible') is False or 'Scenery_' in q['id']:continue
 north,east,z=q['position_cm'];pos=Vector((east/100,north/100,z/100));radius=420 if q['kind'] in ('ground','water') else 130
 if (Vector((pos.x,pos.y,eye.z))-eye).length<radius:add(q['source'],pos,90-q['yaw_deg'])
bpy.ops.object.light_add(type='SUN',location=(eye+Vector((0,0,100))));sun=bpy.context.object;sun.rotation_euler=(math.radians(28),math.radians(-20),math.radians(-25));sun.data.energy=2;sun.data.angle=.12
bpy.ops.object.camera_add(location=eye);camera=bpy.context.object;camera.rotation_euler=(target-eye).to_track_quat('-Z','Y').to_euler();camera.data.lens=24;camera.data.clip_end=1800;sc.camera=camera;folder=OUT/'previews';folder.mkdir(exist_ok=True);path=folder/(a.view+'.png');sc.render.filepath=str(path);bpy.ops.render.render(write_still=True)
write(folder/(a.view+'.json'),dict(status='passed',image=str(path),image_sha256=sha(path),delivery_sha256=sha(OUT/'delivery.json'),eye_m=list(eye),target_m=list(target),imported_world_sources=imported,kind='Actual Blender rendering of certified street assets, current building models and terrain; not an AI concept image.'))
