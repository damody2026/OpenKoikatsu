"""Render actual accepted Blender geometry; optionally assemble the full campus."""
import bpy,sys,json,math,argparse,hashlib
from pathlib import Path
from mathutils import Vector
from bpy_extras.object_utils import world_to_camera_view
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_remaining_contract import IDS,OUT,context
from street_city_remesh_gate import verify
RENDERER_SHA=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
p=argparse.ArgumentParser();p.add_argument('--building');p.add_argument('--overview-only',action='store_true');a=p.parse_args(sys.argv[sys.argv.index('--')+1:])
def fit(sc,buildings_only=False):
 bpy.context.view_layer.update();cam=sc.camera;inv=cam.matrix_world.inverted();worldpoints=[o.matrix_world@Vector(v) for o in sc.objects if o.type=='MESH' and not o.hide_render and (not buildings_only or not o.get('ground_layer')) for v in o.bound_box];points=[inv@v for v in worldpoints];lo=[min(v[k] for v in points) for k in range(2)];hi=[max(v[k] for v in points) for k in range(2)];cam.location+=cam.matrix_world.to_quaternion()@Vector(((lo[0]+hi[0])/2,(lo[1]+hi[1])/2,0));bpy.context.view_layer.update()
 # Fit against Blender's actual camera projection, not an assumed meaning of
 # ortho_scale. Sensor fit and aspect ratio otherwise crop the outer buildings.
 radius=max(max(abs(p.x*2-1),abs(p.y*2-1)) for v in worldpoints for p in [world_to_camera_view(sc,cam,v)]);cam.data.ortho_scale*=radius*1.12;bpy.context.view_layer.update();projected=[world_to_camera_view(sc,cam,v) for v in worldpoints];bounds=[min(v.x for v in projected),min(v.y for v in projected),max(v.x for v in projected),max(v.y for v in projected)];assert min(bounds[:2])>.02 and max(bounds[2:])<.98,bounds;sc['review_framing_ndc']=bounds
def studio(target,span,location):
 sc=bpy.context.scene;sc.render.engine='CYCLES';sc.cycles.samples=16;sc.cycles.use_denoising=True;sc.render.resolution_x=1600;sc.render.resolution_y=1200;sc.render.resolution_percentage=100
 from street_city_render_device import configure
 configure(sc)
 sc.world=bpy.data.worlds.new('City daylight');sc.world.use_nodes=True;sc.world.node_tree.nodes['Background'].inputs[0].default_value=(.68,.76,.84,1);sc.world.node_tree.nodes['Background'].inputs[1].default_value=.55
 for off,power in [((-1,-1,2),12),((1,1,1.5),8)]:
  bpy.ops.object.light_add(type='AREA',location=target+Vector(off)*span);o=bpy.context.object;o.data.energy=power*span*span;o.data.size=span;o.rotation_euler=(target-o.location).to_track_quat('-Z','Y').to_euler()
 bpy.ops.object.camera_add(location=target+Vector(location)*span);cam=bpy.context.object;cam.rotation_euler=(target-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.type='ORTHO';cam.data.ortho_scale=span*1.42;cam.data.clip_end=10000;sc.camera=cam
 fit(sc);return sc
if a.overview_only:
 bpy.ops.wm.open_mainfile(filepath=str(OUT/'campus-complete-2k.blend'));rows=json.loads((OUT/'world-assembly.json').read_text('utf8'))['buildings'];centers=[r['position_m'] for r in rows];target=Vector(tuple(sum(p[k] for p in centers)/len(centers) for k in range(3)));sc=studio(target,650,(.8,-1.25,1.6));sc.render.resolution_x=2000;sc.render.resolution_y=1600;fit(sc,True);sc.render.filepath=str(OUT/'campus-overview.png');bpy.ops.render.render(write_still=True)
elif a.building:
 c=context(a.building);folder=c['out'];acc=json.loads((folder/'acceptance.json').read_text('utf8'));assert acc['status']=='passed';assert hashlib.sha256((folder/f'{a.building}-complete-2k.glb').read_bytes()).hexdigest()==acc['sha256']
 bpy.ops.wm.open_mainfile(filepath=str(folder/f'{a.building}-complete-2k.blend'));bpy.context.scene.frame_set(1);W,D=c['building']['footprint_m'];H=c['building']['height_m'];sc=studio(Vector((0,0,H/3)),max(W,D),(.9,1.25 if a.building=='H1' else -1.25,1.1));sc.render.filepath=str(folder/'building-preview.png');bpy.ops.render.render(write_still=True)
 for o in sc.objects:
  if o.type=='MESH':o.hide_render=o.get('floor')!=1 or o.get('part_kind')=='ceiling'
 sc.camera.location=Vector((W*.65,-D*.95,max(W,D)*1.5));sc.camera.rotation_euler=(Vector((0,0,0))-sc.camera.location).to_track_quat('-Z','Y').to_euler();fit(sc);sc.render.filepath=str(folder/'interior-preview.png');bpy.ops.render.render(write_still=True)
 (folder/'render-receipt.json').write_text(json.dumps(dict(source_sha256=acc['sha256'],renderer_sha256=RENDERER_SHA,interior_framing_ndc=list(sc['review_framing_ndc']),images={p:hashlib.sha256((folder/p).read_bytes()).hexdigest() for p in ['building-preview.png','interior-preview.png']}),indent=2),'utf8')
else:
 bpy.ops.wm.read_factory_settings(use_empty=True);rows=[]
 for bid in IDS:
  c=context(bid);folder=c['out'];acc=json.loads((folder/'acceptance.json').read_text('utf8'));assert acc['status']=='passed';source=folder/f'{bid}-complete-2k.glb';assert hashlib.sha256(source.read_bytes()).hexdigest()==acc['sha256'];before=set(bpy.data.objects);bpy.ops.import_scene.gltf(filepath=str(source));new=set(bpy.data.objects)-before
  root=bpy.data.objects.new('Campus-'+bid,None);bpy.context.scene.collection.objects.link(root);root['building']=bid;root.location=c['building']['world_position_m']
  for o in new:
   if o.parent is None:o.parent=root
  for o in new:o.animation_data_clear()
  rows.append(dict(building=bid,source=str(source),sha256=acc['sha256'],position_m=list(root.location)))
 # Include the current certified ground underneath the campus, so this is a
 # usable scene assembly rather than a group of floating building models.
 ground_file=OUT.parent/'world-ground-v1/ground-acceptance.json';ground=json.loads(ground_file.read_text('utf8'));assert ground['status']=='passed';terrains=[]
 lo=[min(r['position_m'][k]-context(r['building'])['building']['footprint_m'][k]/2 for r in rows) for k in range(2)];hi=[max(r['position_m'][k]+context(r['building'])['building']['footprint_m'][k]/2 for r in rows) for k in range(2)]
 for chunk in ground['chunks']:
  x,y,_=chunk['world_origin_m']
  if x+275<lo[0] or x>hi[0] or y+275<lo[1] or y>hi[1]:continue
  receipt=verify(Path(chunk['ground']['remesh_report']).parent,chunk['ground']['sha256']);before=set(bpy.data.objects);bpy.ops.import_scene.gltf(filepath=receipt['path']);new=set(bpy.data.objects)-before;root=bpy.data.objects.new('Campus-terrain-'+chunk['id'],None);bpy.context.scene.collection.objects.link(root);root.location=(x,y,0)
  for o in new:
   if o.parent is None:o.parent=root
  terrains.append(dict(id=chunk['id'],sha256=receipt['sha256'],source=receipt['path'],position_m=[x,y,0]))
 sc=bpy.context.scene;sc.unit_settings.system='METRIC';bpy.context.view_layer.update();sc.frame_set(1)
 bpy.ops.export_scene.gltf(filepath=str(OUT/'campus-complete-2k.glb'),export_format='GLB',export_extras=True,export_animations=False)
 bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'campus-complete-2k.blend'))
 centers=[r['position_m'] for r in rows];target=Vector(tuple(sum(p[k] for p in centers)/len(centers) for k in range(3)));sc=studio(target,650,(.8,-1.25,1.6));sc.render.resolution_x=2000;sc.render.resolution_y=1600;fit(sc,True);sc.render.filepath=str(OUT/'campus-overview.png');bpy.ops.render.render(write_still=True)
 (OUT/'world-assembly.json').write_text(json.dumps(dict(status='passed',buildings=rows,terrain=terrains,coordinate_system='X east / Y north / Z up, metres',source_scope='All 17 accepted buildings and current certified campus terrain'),indent=2),'utf8')
