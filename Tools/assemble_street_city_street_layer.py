"""Portable Blender layer with shared meshes; Unreal uses the same HISM manifest."""
import bpy,sys,math
from pathlib import Path
from mathutils import Matrix,Vector
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_streetscape import OUT,read,write,sha
d=read(OUT/'delivery.json');assert read(OUT/'asset-acceptance.json')['status']=='passed';bpy.ops.wm.read_factory_settings(use_empty=True);cache={};library=bpy.data.collections.new('Certified LOD library');bpy.context.scene.collection.children.link(library);library.hide_render=True;library.hide_viewport=True
for a in d['assets']:
 for lod in a['lods']:
  before=set(bpy.data.objects);bpy.ops.import_scene.gltf(filepath=lod['path']);bpy.context.view_layer.update();new=set(bpy.data.objects)-before;items=[]
  for ob in new:
   if ob.type=='MESH':items.append((ob.data.copy(),ob.matrix_world.copy()))
  for ob in new:bpy.data.objects.remove(ob,do_unlink=True)
  cache[(a['id'],lod['level'])]=items
  for j,(mesh,mat) in enumerate(items):
   ob=bpy.data.objects.new(a['id']+'-LOD'+str(lod['level'])+'-'+str(j),mesh);library.objects.link(ob);ob.matrix_world=mat;ob['certified_source_sha256']=lod['sha256']
count=0;regions={}
for group in d['groups']:
 if not group['visible']:continue
 for p in group['instances']:
  collection=regions.get(p['region'])
  if collection is None:collection=bpy.data.collections.new(p['region']);bpy.context.scene.collection.children.link(collection);regions[p['region']]=collection
  for j,(mesh,basis) in enumerate(cache[(p['asset'],0)]):
   ob=bpy.data.objects.new(p['id']+'-'+str(j),mesh);collection.objects.link(ob);ob.matrix_world=Matrix.Translation(Vector(p['position_m']))@Matrix.Rotation(math.radians(p['yaw_deg']),4,'Z')@Matrix.Diagonal(Vector((*p['scale'],1)))@basis;ob['street_asset']=p['asset'];ob['instance_id']=p['id'];ob['lod_manifest']='delivery.json';count+=1
sc=bpy.context.scene;sc.unit_settings.system='METRIC';bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'street-dressing-layer.blend'));bpy.ops.object.select_all(action='DESELECT')
for region in regions.values():
 for ob in region.objects:ob.select_set(True)
bpy.ops.export_scene.gltf(filepath=str(OUT/'street-dressing-layer.glb'),export_format='GLB',use_selection=True,export_extras=True,export_animations=False)
write(OUT/'portable-layer.json',dict(status='passed',instances=count,delivery_sha256=sha(OUT/'delivery.json'),blend=str(OUT/'street-dressing-layer.blend'),blend_sha256=sha(OUT/'street-dressing-layer.blend'),glb=str(OUT/'street-dressing-layer.glb'),glb_sha256=sha(OUT/'street-dressing-layer.glb'),note='Visible LOD0 layer with shared mesh nodes; actual runtime automatic LODs, culling and hidden trunk collisions are delivered through the Unreal manifest, not claimed as portable glTF runtime features.'))
