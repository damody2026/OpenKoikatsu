"""Normalize Tripo ground assets, or bake verified remesh into reusable surfaces."""
import bpy,sys,json,argparse,math,hashlib
import numpy as np
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_remesh_gate import verify,sha
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'output/street-social-city-production/world-ground-v1'
p=argparse.ArgumentParser();p.add_argument('--stage',choices=['prepare','bake'],required=True);p.add_argument('--ids',nargs='+',default=['urban','grass','campus','forest','riverbank','sand']);a=p.parse_args(sys.argv[sys.argv.index('--')+1:])
for kind in a.ids:
 folder=OUT/'assets'/kind;folder.mkdir(parents=True,exist_ok=True);bpy.ops.wm.read_factory_settings(use_empty=True)
 if a.stage=='prepare':
  raw=OUT/'raw'/f'Ground-{kind}-Tripo-v1.glb';assert raw.exists();bpy.ops.import_scene.gltf(filepath=str(raw));meshes=[o for o in bpy.context.scene.objects if o.type=='MESH'];assert meshes
  bpy.ops.object.select_all(action='DESELECT')
  for o in meshes:o.select_set(True)
  bpy.context.view_layer.objects.active=meshes[0];bpy.ops.object.join();o=bpy.context.object;world=o.matrix_world.copy();coords=np.array([world@v.co for v in o.data.vertices],dtype=np.float64);center=coords.mean(axis=0);cov=np.cov((coords-center).T);values,vectors=np.linalg.eigh(cov);normal=Vector(vectors[:,0]);normal=normal if normal.z>=0 else -normal;rot=normal.rotation_difference(Vector((0,0,1))).to_matrix();coords=(np.asarray(rot)@(coords-center).T).T
  # Find the square's minimum-area frame rather than arbitrary PCA yaw.
  best=None
  for angle in np.linspace(0,math.pi/2,181):
   c,s=math.cos(angle),math.sin(angle);xy=coords[:,:2]@np.array([[c,-s],[s,c]]);area=np.ptp(xy[:,0])*np.ptp(xy[:,1])
   if best is None or area<best[0]:best=(area,xy.copy())
  coords[:,:2]=best[1];lo=coords.min(axis=0);hi=coords.max(axis=0);assert min(hi-lo)>1e-6;coords-=(lo+hi)/2;coords[:,0]*=20/(hi[0]-lo[0]);coords[:,1]*=20/(hi[1]-lo[1]);thickness=.6 if kind=='forest' else .4 if kind=='riverbank' else .2;coords[:,2]*=thickness/(hi[2]-lo[2]);coords[:,2]+=thickness/2
  for v,co in zip(o.data.vertices,coords):v.co=co
  o.parent=None;o.matrix_world.identity();o.name='tripo-ground-'+kind;o['ground_kind']=kind;o['source_route']='GPT two views / Tripo Model';o.data.update();bpy.context.view_layer.update();o.data.calc_loop_triangles();assert len(o.data.loop_triangles)<=50000
  for im in bpy.data.images:
   if im.size[0] and max(im.size)>2048:
    scale=2048/max(im.size);im.scale(max(1,round(im.size[0]*scale)),max(1,round(im.size[1]*scale)))
  target=folder/'normalized-source.glb';bpy.ops.export_scene.gltf(filepath=str(target),export_format='GLB',export_extras=True,export_animations=False)
  (folder/'source.json').write_text(json.dumps(dict(kind=kind,raw=str(raw),raw_sha256=sha(raw),source_sha256=sha(target),triangles=len(o.data.loop_triangles),size_m=[20,20,thickness],normalization='PCA plane normal, minimum-area square frame, exact metric fit; original UV preserved'),indent=2),'utf-8');print(kind,'normalized',flush=True)
 else:
  receipt=verify(folder/'remesh');bpy.ops.import_scene.gltf(filepath=receipt['path']);objects=[o for o in bpy.context.scene.objects if o.type=='MESH'];assert len(objects)==1;o=objects[0];sc=bpy.context.scene;sc.render.engine='CYCLES';sc.cycles.samples=4;sc.render.resolution_x=sc.render.resolution_y=2048;sc.render.resolution_percentage=100;sc.render.image_settings.file_format='PNG';sc.view_settings.view_transform='Standard';sc.view_settings.look='None';sc.view_settings.exposure=0;sc.view_settings.gamma=1
  sc.world=bpy.data.worlds.new('Bake background');sc.world.color=(0,0,0);bpy.ops.object.camera_add(location=(0,0,10));cam=bpy.context.object;cam.data.type='ORTHO';cam.data.ortho_scale=16;cam.rotation_euler=(0,0,0);sc.camera=cam;original=list(o.data.materials);maps={}
  for channel in ['basecolor','roughness','normal']:
   sc.view_settings.view_transform='Standard' if channel=='basecolor' else 'Raw'
   for i,mat in enumerate(original):
    m=mat.copy();m.use_nodes=True;nodes=m.node_tree.nodes;bs=next(n for n in nodes if n.type=='BSDF_PRINCIPLED');output=next(n for n in nodes if n.type=='OUTPUT_MATERIAL');em=nodes.new('ShaderNodeEmission');em.inputs['Strength'].default_value=1
    if channel=='normal':
     normal_input=bs.inputs['Normal'];normal_socket=normal_input.links[0].from_socket if normal_input.is_linked else nodes.new('ShaderNodeNewGeometry').outputs['Normal'];multiply=nodes.new('ShaderNodeVectorMath');multiply.operation='SCALE';multiply.inputs['Scale'].default_value=.5;m.node_tree.links.new(normal_socket,multiply.inputs[0]);add=nodes.new('ShaderNodeVectorMath');add.operation='ADD';add.inputs[1].default_value=(.5,.5,.5);m.node_tree.links.new(multiply.outputs[0],add.inputs[0]);m.node_tree.links.new(add.outputs[0],em.inputs[0])
    else:
     source=bs.inputs['Base Color' if channel=='basecolor' else 'Roughness']
     if source.is_linked:m.node_tree.links.new(source.links[0].from_socket,em.inputs[0])
     else:
      value=source.default_value;em.inputs[0].default_value=tuple(value) if channel=='basecolor' else (value,value,value,1)
    m.node_tree.links.new(em.outputs[0],output.inputs['Surface']);o.data.materials[i]=m
   sc.render.filepath=str(folder/(channel+'-2k.png'));bpy.ops.render.render(write_still=True);maps[channel]=dict(path=sc.render.filepath,sha256=sha(sc.render.filepath),pixels=[2048,2048]);print(kind,channel,'baked',flush=True)
  verts=[o.matrix_world@v.co for v in o.data.vertices];o.data.calc_loop_triangles();bvh=BVHTree.FromPolygons(verts,[list(t.vertices) for t in o.data.loop_triangles],all_triangles=True);field=np.full((65,65),np.nan)
  for iy,y in enumerate(np.linspace(-8,8,65)):
   for ix,x in enumerate(np.linspace(-8,8,65)):
    hit,n,index,distance=bvh.ray_cast(Vector((x,y,5)),Vector((0,0,-1)),10)
    if hit is not None:field[iy,ix]=hit.z
  valid=np.isfinite(field);assert valid.mean()>.99,(kind,'Tripo top surface incomplete',float(valid.mean()))
  if not valid.all():field[~valid]=np.median(field[valid])
  field-=np.median(field);np.save(folder/'height-field.npy',field)
  (folder/'surface.json').write_text(json.dumps(dict(kind=kind,source=receipt,usable_period_m=16,top_coverage=float(valid.mean()),raw_height_range_m=[float(field.min()),float(field.max())],maps=maps,height_field_sha256=sha(folder/'height-field.npy'),bake='Emission-only orthographic 16m interior; Tripo basecolor/roughness/normal, no generated image used as texture directly'),indent=2),'utf-8');print(kind,'surface ready',flush=True)
