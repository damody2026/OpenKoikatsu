"""Material-only derivative models and real Blender renders; certified geometry stays fixed."""
import bpy,json,sys,math,hashlib,argparse
from pathlib import Path
from mathutils import Vector
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_world_wall_art import ROOT,OUT,CITY,sha
from street_city_world_wall_rules import target,maps_for,bounds,read_materials
from street_city_render_device import configure
from build_street_city_language_models import files
P=argparse.ArgumentParser();P.add_argument('--ids',nargs='*');args=P.parse_args(sys.argv[sys.argv.index('--')+1:])
DATA=read_materials();BOUNDS=bounds();plans={b['id']:b for b in json.loads((ROOT/'Docs/StreetSocialCity/production/building-floor-audit.json').read_text('utf8'))['buildings']};SCRIPT=sha(__file__)
adopt=json.loads((ROOT/'Docs/StreetSocialCity/COMPLETED_WORLD_ADOPTION.json').read_text('utf8'));origins={q['id']:q['world_position_m'] for q in adopt['buildings']}
def geometry():
 h=hashlib.sha256();count=0
 for o in sorted((o for o in bpy.context.scene.objects if o.type=='MESH'),key=lambda o:o.name):
  o.data.calc_loop_triangles();count+=len(o.data.loop_triangles);h.update(o.name.encode());h.update(bytes(str([tuple(v.co) for v in o.data.vertices]),'utf8'));h.update(bytes(str([tuple(p.vertices) for p in o.data.polygons]),'utf8'));h.update(bytes(str([tuple(row) for row in o.matrix_world]),'utf8'))
 return h.hexdigest(),count
def make_mat(key,cache):
 if key in cache:return cache[key]
 q=DATA[key];m=bpy.data.materials.new('Grok wall '+key);m.use_nodes=True;bs=m.node_tree.nodes['Principled BSDF'];bs.inputs['Metallic'].default_value=0
 for kind,socket in [('basecolor','Base Color'),('roughness','Roughness'),('normal','Normal')]:
  n=m.node_tree.nodes.new('ShaderNodeTexImage');n.image=bpy.data.images.load(q['maps'][kind]['path'],check_existing=True);n.image.pack();n.extension='REPEAT'
  if kind!='basecolor':n.image.colorspace_settings.name='Non-Color'
  if kind=='normal':
   normal=m.node_tree.nodes.new('ShaderNodeNormalMap');m.node_tree.links.new(n.outputs['Color'],normal.inputs['Color']);m.node_tree.links.new(normal.outputs['Normal'],bs.inputs[socket])
  else:m.node_tree.links.new(n.outputs['Color'],bs.inputs[socket])
 m['grok_texture_id']=key;m['period_m']=2;cache[key]=m;return m
def render(folder,bid,overview=False):
 sc=bpy.context.scene;sc.render.engine='CYCLES';sc.cycles.samples=12;sc.cycles.use_denoising=True;configure(sc);sc.render.resolution_x=1400 if bid in CITY else 1000;sc.render.resolution_y=1000 if bid in CITY else 800;sc.render.resolution_percentage=100
 points=[o.matrix_world@Vector(v) for o in sc.objects if o.type=='MESH' for v in o.bound_box];lo=Vector([min(p[k] for p in points) for k in range(3)]);hi=Vector([max(p[k] for p in points) for k in range(3)]);span=max(hi.x-lo.x,hi.y-lo.y);center=(lo+hi)/2
 for o in list(sc.objects):
  if o.type in ('LIGHT','CAMERA'):bpy.data.objects.remove(o,do_unlink=True)
 sc.world=bpy.data.worlds.new('Daylight wall review');sc.world.use_nodes=True;sc.world.node_tree.nodes['Background'].inputs[0].default_value=(.68,.76,.84,1);sc.world.node_tree.nodes['Background'].inputs[1].default_value=.6
 for off,power in [((1,-1,2),14),((-1,1,2),8)]:
  bpy.ops.object.light_add(type='AREA',location=center+Vector(off)*span);light=bpy.context.object;light.data.energy=power*span**2;light.data.size=span;light.rotation_euler=(center-light.location).to_track_quat('-Z','Y').to_euler()
 bpy.ops.object.camera_add(location=center+Vector((.75,-1.25,1))*span);cam=bpy.context.object;cam.rotation_euler=(center-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.type='ORTHO';cam.data.ortho_scale=span*1.6;sc.camera=cam
 from bpy_extras.object_utils import world_to_camera_view
 bpy.context.view_layer.update();radius=max(max(abs(p.x*2-1),abs(p.y*2-1)) for v in points for p in [world_to_camera_view(sc,cam,v)]);cam.data.ortho_scale*=radius*1.12;sc.render.filepath=str(folder/'exterior.png');bpy.ops.render.render(write_still=True)
 # First-floor cutaway of the real revised model; hide surfaces above its ceiling.
 height=plans[bid]['floors'][0]['floor_to_floor_height_m'];hidden=[]
 for o in sc.objects:
  if o.type!='MESH':continue
  zs=[(o.matrix_world@Vector(v)).z for v in o.bound_box]
  if sum(zs)/len(zs)>=height-.35:o.hide_render=True;hidden.append(o)
 cam.location=Vector((0,0,height*.4))+Vector((.6,-1.1,1.35))*span;cam.rotation_euler=(Vector((0,0,height*.4))-cam.location).to_track_quat('-Z','Y').to_euler();sc.render.filepath=str(folder/'interior.png');bpy.ops.render.render(write_still=True)
 for o in hidden:o.hide_render=False

rows=[]
for bid,source in files():
 if args.ids and bid not in args.ids:continue
 folder=OUT/'buildings'/bid;folder.mkdir(parents=True,exist_ok=True);receipt=folder/'model.json';contract=hashlib.sha256(json.dumps([SCRIPT,sha(source),[(k,DATA[k]['identity']) for k in sorted({key for f in plans[bid]['floors'] for role in ['interior','facade','wet'] for key in maps_for(bid,f['floor'],role)})]],sort_keys=True).encode()).hexdigest()
 if receipt.exists():
  old=json.loads(receipt.read_text('utf8'))
  if old.get('contract_sha256')==contract and old.get('status')=='passed' and sha(old['model'])==old['model_sha256'] and all(Path(old[k]).exists() for k in ['exterior','interior']):rows.append(old);continue
 folder=folder/'revisions'/contract[:16];folder.mkdir(parents=True,exist_ok=True)
 bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=str(source));bpy.context.view_layer.update();before,triangles=geometry();cache={};changed=0;assignment=[];w,d=BOUNDS[bid];origin=origins.get(bid,plans[bid]['world_position_m'])
 for o in list(bpy.context.scene.objects):
  if o.type!='MESH':continue
  oldmats=list(o.data.materials)
  if not any(m and not m.get('city_text_id') and target(bid,m.name) for m in oldmats):continue
  o.data=o.data.copy();uv=o.data.uv_layers.active or o.data.uv_layers.new(name='UVMap');indices={};normal_matrix=o.matrix_world.to_3x3().inverted().transposed()
  for poly in o.data.polygons:
   old=oldmats[poly.material_index];role=target(bid,old.name) if old and not old.get('city_text_id') else None
   if not role:continue
   normal=(normal_matrix@poly.normal).normalized()
   if abs(normal.z)>=.65:continue
   center=o.matrix_world@poly.center;no=max((q['floor'] for q in plans[bid]['floors'] if q['z_m']<=center.z+.001),default=1);ik,ek=maps_for(bid,no,role);external=role=='facade' or abs(center.x)>w/2-.35 or abs(center.y)>d/2-.35;key=ek if external else ik
   assert key in DATA,('Image not baked yet',key)
   if key not in indices:indices[key]=len(o.data.materials);o.data.materials.append(make_mat(key,cache))
   poly.material_index=indices[key];changed+=1
   for li in poly.loop_indices:
    p=o.matrix_world@o.data.vertices[o.data.loops[li].vertex_index].co;u=p.y+origin[1] if abs(normal.x)>abs(normal.y) else p.x+origin[0];uv.data[li].uv=(u/2,(p.z+origin[2])/2)
  assignment.append(dict(object=o.name,wall_polygons=changed))
 after,actual=geometry();assert before==after and triangles==actual and changed>0,(bid,before,after,changed)
 model=folder/(bid+'-grok-walls-2k.glb');bpy.ops.export_scene.gltf(filepath=str(model),export_format='GLB',export_extras=True,export_animations=True);bpy.ops.wm.save_as_mainfile(filepath=str(model.with_suffix('.blend')));render(folder,bid)
 row=dict(status='passed',building=bid,source=str(source),source_sha256=sha(source),contract_sha256=contract,geometry_before=before,geometry_after=after,triangles=triangles,wall_polygons=changed,texture_ids=sorted(cache),model=str(model),model_sha256=sha(model),exterior=str(folder/'exterior.png'),interior=str(folder/'interior.png'),exterior_sha256=sha(folder/'exterior.png'),interior_sha256=sha(folder/'interior.png'),scope='Material and UV only; exact in-memory source mesh coordinates, polygons and transforms unchanged')
 receipt.write_text(json.dumps(row,ensure_ascii=False,indent=2),'utf8');rows.append(row);print('GROK WALL BUILDING',bid,changed,flush=True)
(OUT/'models.json').write_text(json.dumps(dict(status='passed' if len(rows)==122 else 'partial',buildings=rows),ensure_ascii=False,indent=2),'utf8')
