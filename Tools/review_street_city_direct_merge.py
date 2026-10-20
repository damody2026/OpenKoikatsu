"""Assemble the accepted editable sources at the verified GLB world transforms."""
import bpy,sys,json,math,array,hashlib
from pathlib import Path
from mathutils import Vector
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_shop_street import OUT
from street_city_scene_review import frame_overview
from street_city_remesh_gate import sha
def main():
 import time,msvcrt
 folder=OUT/'direct';folder.mkdir(exist_ok=True);lock=(folder/'review.lock').open('a+b')
 if lock.tell()==0:lock.write(b'0');lock.flush()
 while True:
  try:lock.seek(0);msvcrt.locking(lock.fileno(),msvcrt.LK_NBLCK,1);break
  except OSError:time.sleep(.5)
 try:
  execute(folder)
 finally:
  lock.seek(0);msvcrt.locking(lock.fileno(),msvcrt.LK_UNLCK,1);lock.close()

def execute(OUT):
 path=OUT/'shop-street-complete.glb';before=sha(path)
 cache=OUT/'review-cache.json';version=dict(model_sha256=before,renderer_sha256=sha(Path(__file__)),frame_helper_sha256=sha(Path(__file__).parent/'street_city_scene_review.py'))
 if cache.exists():
  receipt=json.loads(cache.read_text('utf8'))
  if receipt.get('version')==version and all((OUT/name).is_file() and sha(OUT/name)==digest for name,digest in receipt['files'].items()) and all(sha(q['path'])==q['sha256'] for q in receipt['blend_sources']):
   r=json.loads((OUT/'world-assembly.json').read_text('utf8'));r.update(receipt['view']);(OUT/'world-assembly.json').write_text(json.dumps(r,ensure_ascii=False,indent=2),'utf8');print('Exact current whole-street render reused',flush=True);return
 assembly=json.loads((OUT/'world-assembly.json').read_text('utf8'));assert assembly['glb_sha256']==before
 bpy.ops.wm.read_factory_settings(use_empty=True);sc=bpy.context.scene;sc.unit_settings.system='METRIC';sc.frame_end=160;blend_sources=[];instances=[]
 def fingerprint(objects):
  h=hashlib.sha256()
  for obj,matrix in sorted(objects,key=lambda q:q[0].name):
   h.update(obj.name.encode());co=array.array('f',[0])*(len(obj.data.vertices)*3);obj.data.vertices.foreach_get('co',co);h.update(co.tobytes());ii=array.array('i',[0])*len(obj.data.loops);obj.data.loops.foreach_get('vertex_index',ii);h.update(ii.tobytes());h.update(str([tuple(row) for row in matrix]).encode())
  return h.hexdigest()
 for row in assembly['buildings']:
  bid=row['building'];source=Path(row['source']);folder=source.parent;accepted=json.loads((folder/'acceptance.json').read_text('utf8'));report=json.loads((folder/'assembly-report.json').read_text('utf8'))
  assert accepted['status']=='passed' and report['approved'] and accepted['sha256']==row['sha256']==sha(source)
  source_blend=folder/f'{bid}-complete-2k.blend';digest=sha(source_blend)
  with bpy.data.libraries.load(str(source_blend),link=True) as (available,loaded):loaded.scenes=available.scenes
  assert len(loaded.scenes)==1,(bid,'one canonical source scene')
  linked_scene=loaded.scenes[0];collection=linked_scene.collection;objects=list(collection.all_objects)
  assert all(o.type in ('EMPTY','MESH') for o in objects)
  root=bpy.data.objects.new(bid,None);root.instance_type='COLLECTION';root.instance_collection=collection;sc.collection.objects.link(root);root['building']=bid;root['accepted_glb_sha256']=accepted['sha256'];root['source_blend']=str(source_blend.resolve())
  assert sha(source_blend)==digest
  instances.append((row,root,report));blend_sources.append(dict(building=bid,path=str(source_blend.resolve()),sha256=digest,accepted_glb_sha256=accepted['sha256']))
  print('Accepted linked building assembled',bid,flush=True)
 sc.frame_set(1);bpy.context.view_layer.update();evaluated={row['building']:[] for row,root,report in instances};dg=bpy.context.evaluated_depsgraph_get()
 for instance in dg.object_instances:
  if not instance.is_instance or instance.object.type!='MESH':continue
  bid=instance.parent.original.get('building') if instance.parent else None
  if bid in evaluated:evaluated[bid].append((instance.object.original,instance.matrix_world.copy()))
 points=[];local_bounds={}
 for row,root,report in instances:
  bid=row['building'];objects=evaluated[bid];assert objects,(bid,'empty collection instance')
  if report.get('wall_style',{}).get('geometry_fingerprint'):assert fingerprint(objects)==report['wall_style']['geometry_fingerprint'],(bid,'instance geometry differs from accepted source')
  local=[matrix@Vector(c) for obj,matrix in objects for c in obj.bound_box]
  for axis,size in enumerate(row['footprint_m']):assert abs(max(v[axis] for v in local)-min(v[axis] for v in local)-size)<.002,(bid,axis)
  local_bounds[bid]=local;root.location=row['position_m'];root.rotation_euler.z=math.radians(row['rotation_z_deg'])
 bpy.context.view_layer.update()
 for row,root,report in instances:points.extend(root.matrix_world@v for v in local_bounds[row['building']])
 lo=Vector([min(v[k] for v in points) for k in range(3)]);hi=Vector([max(v[k] for v in points) for k in range(3)]);center=(lo+hi)/2;extent=max(hi.x-lo.x,hi.y-lo.y)
 sc.world=bpy.data.worlds.new('Overview daylight');sc.world.use_nodes=True;sc.world.node_tree.nodes['Background'].inputs[0].default_value=(.65,.65,.65,1);bpy.ops.object.light_add(type='SUN',rotation=(.5,-.6,-.4));bpy.context.object.data.energy=3
 bpy.ops.object.camera_add(location=center+Vector((extent*.15,-extent*.4,extent*.8)));cam=bpy.context.object;cam.rotation_euler=(center-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.type='ORTHO';cam.data.clip_end=max(1000,extent*10);sc.camera=cam;sc.render.engine='CYCLES';sc.cycles.samples=8;sc.render.resolution_x=1800;sc.render.resolution_y=1000;sc.render.resolution_percentage=100;sc.render.filepath=str(OUT/'shop-street-overview.png')
 shift,scale=frame_overview(cam,points,1.8);center+=shift
 for screen in bpy.data.screens:
  for area in screen.areas:
   if area.type=='VIEW_3D':
    space=area.spaces.active;space.clip_end=max(1000,extent*10);space.region_3d.view_location=center;space.region_3d.view_distance=scale;space.region_3d.view_rotation=cam.rotation_euler.to_quaternion();space.region_3d.view_perspective='ORTHO';space.shading.type='MATERIAL'
 bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'shop-street-complete.blend'));bpy.ops.render.render(write_still=True);assert sha(path)==before
 r=json.loads((OUT/'world-assembly.json').read_text('utf-8'));r.update(view_center_m=list(center),extent_m=extent,review_ortho_scale_m=scale,review_frame_fit='All actual bounding-box corners, aspect-correct, 8% margin',review_assembly='Linked instances of accepted editable building sources; original geometry fingerprints and GLB world transforms verified');(OUT/'world-assembly.json').write_text(json.dumps(r,ensure_ascii=False,indent=2),'utf-8')
 
 view={k:r[k] for k in ['view_center_m','extent_m','review_ortho_scale_m','review_frame_fit','review_assembly']}
 cache.write_text(json.dumps(dict(version=version,blend_sources=blend_sources,view=view,files={name:sha(OUT/name) for name in ['shop-street-complete.blend','shop-street-overview.png']}),indent=2),'utf8')

if __name__=='__main__':main()
