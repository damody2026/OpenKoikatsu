"""Reuse an identical accepted programme; preserve geometry and independently accept new export."""
import bpy,sys,json,struct,copy
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_building_context import context
from street_city_remesh_gate import verify as verify_remesh,sha
from mathutils import Vector
old,new=sys.argv[sys.argv.index('--')+1:];source=context(old)['out'];C=context(new);OUT=C['out']
approved=json.loads((source/'acceptance.json').read_text('utf-8'))
prototype=json.loads((source/'assembly-report.json').read_text('utf-8'))
assert approved['status']=='passed' and prototype.get('approved')
assert sha(source/f'{old}-complete-2k.glb')==approved['sha256']
for asset in prototype['sources'].values():verify_remesh(Path(asset['remesh_report']).parent,asset['sha256'])
def rename(value):
 if isinstance(value,str):return value.replace(old,new)
 if isinstance(value,list):return [rename(v) for v in value]
 if isinstance(value,dict):return {k:rename(v) for k,v in value.items()}
 return value
def glb(path,target):
 raw=path.read_bytes();length=struct.unpack_from('<I',raw,12)[0];g=json.loads(raw[20:20+length]);start=20+length;binary=bytearray(raw[start+8:]);g=rename(g)
 # Replace only the building-specific sign image; all other PBR images remain bit-identical.
 for mat in g.get('materials',[]):
  if mat.get('name')!=new+' original storefront lettering':continue
  tex=g['textures'][mat['pbrMetallicRoughness']['baseColorTexture']['index']];image=g['images'][tex['source']];view=g['bufferViews'][image['bufferView']]
  binary.extend(b'\0'*((-len(binary))%4));data=(OUT/'sign-2k.png').read_bytes();view['byteOffset']=len(binary);view['byteLength']=len(data);binary.extend(data)
 binary.extend(b'\0'*((-len(binary))%4));g['buffers'][0]['byteLength']=len(binary);j=json.dumps(g,separators=(',',':'),ensure_ascii=False).encode();j+=b' '*((-len(j))%4)
 target.write_bytes(struct.pack('<4sII',b'glTF',2,12+8+len(j)+8+len(binary))+struct.pack('<I4s',len(j),b'JSON')+j+struct.pack('<I4s',len(binary),b'BIN\0')+binary)
for suffix in ['complete-2k.glb','F01-complete-2k.glb','F02-complete-2k.glb','F03-complete-2k.glb']:glb(source/f'{old}-{suffix}',OUT/f'{new}-{suffix}')
report=json.loads((source/'assembly-report.json').read_text('utf-8'));report['building']=new
for key in ['instances','doors','lift']:report[key]=rename(report[key])
report.update(approved=False,status='assembled_requires_acceptance',equivalent_programme_source=old)
report['world_placement']=dict(position_m=C['building'].get('world_position_m'),rotation_z_deg=C.get('world_yaw_deg',0),local_front='south',units='metres')
(OUT/'assembly-report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),'utf-8')
(OUT/'material-report.json').write_bytes((source/'material-report.json').read_bytes())
bpy.ops.wm.open_mainfile(filepath=str(source/f'{old}-complete-2k.blend'))
for o in bpy.data.objects:o.name=o.name.replace(old,new)
for mat in bpy.data.materials:
 if mat.name==old+' original storefront lettering':
  mat.name=new+' original storefront lettering'
  for node in mat.node_tree.nodes:
   if node.type=='TEX_IMAGE':node.image=bpy.data.images.load(str(OUT/'sign-2k.png'));node.image.pack()
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/f'{new}-complete-2k.blend'))
sc=bpy.context.scene;sc.frame_set(1);meshes=[o for o in sc.objects if o.type=='MESH'];sc.render.engine='CYCLES';sc.cycles.samples=8;sc.render.resolution_x=700;sc.render.resolution_y=800;sc.render.resolution_percentage=100
sc.world=bpy.data.worlds.new('Studio');sc.world.use_nodes=True;sc.world.node_tree.nodes['Background'].inputs[0].default_value=(.6,.6,.6,1);target=Vector((0,0,5))
for loc,power in [((-8,-15,23),4000),((12,8,24),3500)]:
 bpy.ops.object.light_add(type='AREA',location=loc);o=bpy.context.object;o.data.energy=power;o.data.size=10;o.rotation_euler=(target-o.location).to_track_quat('-Z','Y').to_euler()
bpy.ops.object.camera_add(location=(25,-35,23));camera=bpy.context.object;camera.rotation_euler=(target-camera.location).to_track_quat('-Z','Y').to_euler();camera.data.type='ORTHO';camera.data.ortho_scale=27;sc.camera=camera
sc.render.filepath=str(OUT/f'{new}-exterior.png');bpy.ops.render.render(write_still=True)
for no in [1,2,3]:
 for o in meshes:
  o.hide_render=o['floor']!=no or o.get('part_kind')=='ceiling' or o.get('asset_id')=='ceiling-light'
  if o.get('part_kind')=='wall':
   pts=[o.matrix_world@v.co for v in o.data.vertices]
   if max(q.y for q in pts)<-C['depth']/2+.3 or min(q.x for q in pts)>C['width']/2-.3:o.hide_render=True
 camera.location=(23,-30,38+[0,3.8,7.6][no-1]);target=Vector((0,0,[0,3.8,7.6][no-1]+1.2));camera.rotation_euler=(target-camera.location).to_track_quat('-Z','Y').to_euler();camera.data.ortho_scale=25;sc.render.filepath=str(OUT/f'{new}-F{no:02d}-cutaway.png');bpy.ops.render.render(write_still=True)
