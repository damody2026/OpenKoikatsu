"""Render only actual certified product meshes, never their concept images."""
import bpy,sys,json,math,os
from pathlib import Path
from mathutils import Vector
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_mall_merchandise import OUT,publish,folder_for
from street_city_remesh_gate import verify,sha
from street_city_render_device import configure
data=publish();dest=OUT/'previews';override={}
if os.environ.get('STREET_CITY_PRODUCT_LOD_REVIEW')=='1':
 bank=json.loads((OUT/'lods/acceptance.json').read_text('utf8'))['products'];data['components']=[q for q in data['components'] if q['id'] in bank];data['unique_product_models']=len(data['components']);override={aid:OUT/'lods'/aid/'remesh' for aid in bank};dest=OUT/'lod-previews'
elif os.environ.get('STREET_CITY_PRODUCT_REVIEW_IDS'):
 selected=os.environ['STREET_CITY_PRODUCT_REVIEW_IDS'].split(',');data['components']=[q for q in data['components'] if q['id'] in selected];data['unique_product_models']=len(data['components']);dest=OUT/'preview-samples'
dest.mkdir(exist_ok=True)
receipt=dest/'receipt.json';old=json.loads(receipt.read_text('utf8')) if receipt.exists() else {};images=old.get('images',{})
for row in data['components']:
 aid=row['id'];folder=override.get(aid,folder_for(row)/'remesh')
 try:r=verify(folder)
 except (OSError,ValueError,AssertionError):continue
 path=dest/(aid+'.png');previous=images.get(aid,{})
 helper=Path(__file__).with_name('street_city_product_repairs.py')
 if path.exists() and previous.get('source_sha256')==r['sha256'] and previous.get('renderer_sha256')==sha(Path(__file__)) and previous.get('repairs_sha256')==sha(helper) and previous.get('image_sha256')==sha(path):continue
 bpy.ops.wm.read_factory_settings(use_empty=True);sc=bpy.context.scene;sc.render.engine='CYCLES';device=configure(sc);sc.cycles.samples=20;sc.cycles.use_denoising=True;sc.render.resolution_x=600;sc.render.resolution_y=600;sc.render.resolution_percentage=100;sc.world=bpy.data.worlds.new('Product review world');sc.world.color=(.23,.23,.23)
 bpy.ops.import_scene.gltf(filepath=r['path'])
 from street_city_product_repairs import preview,style,metric_basis
 for obj in list(sc.objects):
  if obj.type=='MESH':obj.matrix_world=metric_basis(aid,obj.matrix_world);style(aid,obj.data)
 preview(aid)
 bpy.context.view_layer.update();obs=[o for o in sc.objects if o.type=='MESH']
 from street_city_product_normals import apply as product_normals
 for obj in obs:product_normals(obj.data)
 points=[o.matrix_world@v.co for o in obs for v in o.data.vertices];lo=Vector(tuple(min(p[k] for p in points) for k in range(3)));hi=Vector(tuple(max(p[k] for p in points) for k in range(3)));center=(lo+hi)/2;extent=max(hi-lo)
 floor=hi.z-lo.z
 bpy.ops.mesh.primitive_plane_add(size=extent*200,location=(0,0,lo.z-.001));ground=bpy.context.object
 mat=bpy.data.materials.new('review studio only');mat.diffuse_color=(.18,.19,.21,1);ground.data.materials.append(mat)
 bpy.ops.object.camera_add(location=center+Vector((extent*1.7,-extent*2.5,extent*1.7)));camera=bpy.context.object;camera.rotation_euler=(center-camera.location).to_track_quat('-Z','Y').to_euler();camera.data.type='ORTHO';camera.data.ortho_scale=extent*1.8;camera.data.clip_start=max(.0001,extent*.001);camera.data.clip_end=max(100,extent*100);sc.camera=camera
 for label,pos,power in [('key',(-2,-3,4),420),('fill',(3,1,2),250)]:
  light=bpy.data.lights.new(label,'AREA');light.energy=power*extent*extent;light.shape='DISK';light.size=extent*3;o=bpy.data.objects.new(label,light);sc.collection.objects.link(o);o.location=center+Vector(pos)*extent;o.rotation_euler=(center-o.location).to_track_quat('-Z','Y').to_euler()
 sc.render.filepath=str(path);bpy.ops.render.render(write_still=True)
 images[aid]=dict(source_sha256=r['sha256'],renderer_sha256=sha(Path(__file__)),repairs_sha256=sha(helper),image_sha256=sha(path),actual_size_m=list(hi-lo),device=device)
 receipt.write_text(json.dumps(dict(status='complete' if len(images)==data['unique_product_models'] else 'in_progress',images=images),indent=2),'utf8');print('rendered',aid,flush=True)
