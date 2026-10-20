"""Re-finish an accepted remeshed building, preserving its full mesh and animation."""
import bpy,json,sys,hashlib,argparse,shutil,array
from pathlib import Path
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'Tools'))
from street_city_building_context import context
from street_city_large_shop_contract import context as large_context
from street_city_shop_wall_styles import apply,OUT as STYLE,sha
p=argparse.ArgumentParser();p.add_argument('--building',required=True);a=p.parse_args(sys.argv[sys.argv.index('--')+1:]);BID=a.building;C=large_context(BID) if BID in ('S18','S19') else context(BID);OUT=C['out'];b=C['building'];W,D=b['footprint_m'];floors=b.get('small_shop_design',{}).get('floors',b.get('floors',[]))
backup=STYLE/'before'/BID;backup.mkdir(parents=True,exist_ok=True)
for path in OUT.iterdir():
 if path.is_file() and path.suffix.lower() in ('.blend','.glb','.json','.html','.png') and not (backup/path.name).exists():shutil.copy2(path,backup/path.name)
r=json.loads((backup/'assembly-report.json').read_text('utf8'));old=json.loads((backup/'acceptance.json').read_text('utf8'));assert old['status']=='passed'
bpy.ops.wm.open_mainfile(filepath=str(backup/f'{BID}-complete-2k.blend'));sc=bpy.context.scene;sc.frame_set(1);bpy.context.view_layer.update()
def fingerprint():
 h=hashlib.sha256()
 for o in sorted([o for o in sc.objects if o.type=='MESH'],key=lambda q:q.name):
  h.update(o.name.encode());co=array.array('f',[0])*(len(o.data.vertices)*3);o.data.vertices.foreach_get('co',co);h.update(co.tobytes());ii=array.array('i',[0])*len(o.data.loops);o.data.loops.foreach_get('vertex_index',ii);h.update(ii.tobytes());h.update(str([tuple(row) for row in o.matrix_world]).encode())
 return h.hexdigest()
before=fingerprint();r['wall_style']=apply(sc,BID,b);after=fingerprint();assert before==after
r['wall_style'].update(geometry_fingerprint=before,restyle_script_sha256=sha(__file__))
r.update(approved=False,status='restyled_requires_actual_export_acceptance')
meshes=[o for o in sc.objects if o.type=='MESH'];roots={f['floor']:bpy.data.objects[f'{BID}-F{f["floor"]:02d}'] for f in floors}
for n,root in roots.items():
 count=0
 for o in meshes:
  if o.get('floor')==n:o.data.calc_loop_triangles();count+=len(o.data.loop_triangles)
 assert count==old['floor_triangles'][str(n)] and count<=100000,(BID,n,count)
 bpy.ops.object.select_all(action='DESELECT')
 for o in [root,*root.children_recursive]:o.select_set(True)
 bpy.ops.export_scene.gltf(filepath=str(OUT/f'{BID}-F{n:02d}-complete-2k.glb'),export_format='GLB',use_selection=True,export_extras=True,export_animations=False)
bpy.ops.object.select_all(action='SELECT');bpy.ops.export_scene.gltf(filepath=str(OUT/f'{BID}-complete-2k.glb'),export_format='GLB',export_extras=True,export_animation_mode='SCENE',export_anim_scene_split_object=False)
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/f'{BID}-complete-2k.blend'))
(OUT/'assembly-report.json').write_text(json.dumps(r,ensure_ascii=False,indent=2),'utf8')
(OUT/'wall-style-report.json').write_text(json.dumps(r['wall_style'],ensure_ascii=False,indent=2),'utf8')
# Render the actual adopted materials, never a separate concept illustration.
sc.render.engine='CYCLES';sc.cycles.samples=8;sc.cycles.use_denoising=True;sc.render.resolution_x=800;sc.render.resolution_y=900;sc.render.resolution_percentage=100
if not sc.world:sc.world=bpy.data.worlds.new('Shop style daylight')
sc.world.use_nodes=True;sc.world.node_tree.nodes['Background'].inputs[0].default_value=(.65,.65,.65,1);sc.world.node_tree.nodes['Background'].inputs[1].default_value=.7
extent=max(W,D)
for xyz in [(-extent,-extent,extent*1.7),(extent,extent,extent*1.5)]:
 bpy.ops.object.light_add(type='AREA',location=xyz);o=bpy.context.object;o.data.energy=6500 if W<20 else 16000;o.data.size=extent;o.rotation_euler=(Vector((0,0,2))-o.location).to_track_quat('-Z','Y').to_euler()
bpy.ops.object.camera_add();cam=bpy.context.object;sc.camera=cam;cam.data.type='ORTHO';cam.data.clip_end=1000;cam.data.ortho_scale=extent*1.5
def render(name,pos,target):
 cam.location=pos;cam.rotation_euler=(Vector(target)-cam.location).to_track_quat('-Z','Y').to_euler();sc.render.filepath=str(OUT/name);bpy.ops.render.render(write_still=True)
for o in meshes:o.hide_render=False
render(f'{BID}-exterior.png',(extent,-extent*1.3,extent),(0,0,4))
for f in floors:
 n=f['floor'];z=f['z_m']
 for o in meshes:
  o.hide_render=o.get('floor')!=n or o.get('part_kind')=='ceiling' or o.get('asset_id')=='ceiling-light'
  if o.get('part_kind')=='wall':
   pts=[o.matrix_world@v.co for v in o.data.vertices]
   if max(q.y for q in pts)<-D/2+.3 or min(q.x for q in pts)>W/2-.3:o.hide_render=True
 render(f'{BID}-F{n:02d}-cutaway.png',(extent,-extent*1.3,extent*1.7+z),(0,0,z+1.2))
for o in meshes:o.hide_render=o.get('floor')!=1 or o.get('part_kind')=='ceiling' or o.get('asset_id')=='ceiling-light'
cam.data.type='PERSP';cam.data.lens=24;sc.render.resolution_x=1100;sc.render.resolution_y=800
placement=r['wall_style'].get('feature_placement')
render(f'{BID}-wall-detail.png',placement['camera_m'] if placement else (0,-D/2+1.5,1.7),placement['target_m'] if placement else (-W/2+.2,-D/2+min(5,D*.3),1.65))
paths=[OUT/f'{BID}-exterior.png',OUT/f'{BID}-wall-detail.png',*[OUT/f'{BID}-F{n:02d}-cutaway.png' for n in roots]]
(OUT/'wall-style-render.json').write_text(json.dumps(dict(model_sha256=sha(OUT/f'{BID}-complete-2k.glb'),renderer_sha256=sha(__file__),images={q.name:sha(q) for q in paths}),indent=2),'utf8')
print(BID,'restyled; geometry unchanged',before,flush=True)
