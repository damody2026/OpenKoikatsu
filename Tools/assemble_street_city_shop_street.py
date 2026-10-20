"""Place all accepted, previously remeshed buildings at canonical map coordinates."""
import bpy,sys,json,math,hashlib,array,argparse
from pathlib import Path
from mathutils import Vector
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_shop_street import inventory,ROOT,OUT
from street_city_remesh_gate import verify,sha
p=argparse.ArgumentParser();p.add_argument('--smoke-ids',nargs='+');args=p.parse_args(sys.argv[sys.argv.index('--')+1:]);buildings=inventory();accepted=[]
if args.smoke_ids:buildings=[b for b in buildings if b['building'] in args.smoke_ids];OUT=OUT/'smoke'
for b in buildings:
 bid=b['building'];folder=ROOT/f'output/street-social-city-production/{bid}-modular-components-v1/assembly-building';a=json.loads((folder/'acceptance.json').read_text('utf-8'));r=json.loads((folder/'assembly-report.json').read_text('utf-8'));path=folder/f'{bid}-complete-2k.glb'
 assert a['status']=='passed' and r.get('approved') and a['sha256']==sha(path),(bid,'unaccepted building')
 for source in r['sources'].values():verify(Path(source['remesh_report']).parent,source['sha256'])
 accepted.append((b,path,a,r))
assert len(accepted)==(len(args.smoke_ids) if args.smoke_ids else 96)
bpy.ops.wm.read_factory_settings(use_empty=True);sc=bpy.context.scene;sc.unit_settings.system='METRIC';image_pool={};material_pool={};mesh_pool={};records=[]
def image_key(im):
 raw=bytes(im.packed_file.data) if im.packed_file else Path(bpy.path.abspath(im.filepath)).read_bytes();digest=hashlib.sha256(raw).hexdigest();im['source_png_sha256']=digest
 return (digest,im.colorspace_settings.name,im.alpha_mode)
def value(v):
 if isinstance(v,(str,int,float,bool)) or v is None:return v
 try:return list(v)
 except TypeError:return str(type(v))
def mat_key(mat):
 if not mat.use_nodes:return json.dumps([list(mat.diffuse_color),mat.roughness,mat.metallic])
 nodes=list(mat.node_tree.nodes);rows=[]
 for n in nodes:
  row=dict(type=n.bl_idname,inputs=[(s.name,value(s.default_value)) for s in n.inputs if hasattr(s,'default_value')])
  for k in ['operation','blend_type','data_type','distribution','interpolation','extension','projection','uv_map','layer_name']:
   if hasattr(n,k):row[k]=value(getattr(n,k))
  if n.type=='TEX_IMAGE' and n.image:row['image']=[n.image.get('source_png_sha256'),n.image.colorspace_settings.name,n.image.alpha_mode]
  rows.append(row)
 links=[(nodes.index(q.from_node),q.from_socket.name,nodes.index(q.to_node),q.to_socket.name) for q in mat.node_tree.links]
 return hashlib.sha256(json.dumps([rows,links],sort_keys=True).encode()).hexdigest()
def mesh_key(mesh):
 digest=hashlib.sha256()
 for data,prop,count,code in [(mesh.vertices,'co',3,'f'),(mesh.loops,'vertex_index',1,'i'),(mesh.polygons,'loop_start',1,'i'),(mesh.polygons,'loop_total',1,'i'),(mesh.polygons,'material_index',1,'i'),(mesh.polygons,'use_smooth',1,'i')]:
  buf=array.array(code,[0])*(len(data)*count);data.foreach_get(prop,buf);digest.update(buf.tobytes())
 for uv in mesh.uv_layers:
  buf=array.array('f',[0])*(len(uv.data)*2);uv.data.foreach_get('uv',buf);digest.update(buf.tobytes())
 for color in mesh.color_attributes:
  digest.update(json.dumps([color.name,color.domain,color.data_type]).encode());buf=array.array('f',[0])*(len(color.data)*4);color.data.foreach_get('color',buf);digest.update(buf.tobytes())
 normals=array.array('f',[0])*(len(mesh.corner_normals)*3);mesh.corner_normals.foreach_get('vector',normals);digest.update(normals.tobytes());digest.update(json.dumps([m.get('canonical_material_sha256') for m in mesh.materials]).encode());return digest.hexdigest()
for b,path,a,r in accepted:
 bid=b['building'];before=set(bpy.data.objects);oldimages=set(bpy.data.images);oldmats=set(bpy.data.materials);bpy.ops.import_scene.gltf(filepath=str(path));new=set(bpy.data.objects)-before
 for image in set(bpy.data.images)-oldimages:
  key=image_key(image)
  if key in image_pool:
   target=image_pool[key]
   for mat in set(bpy.data.materials)-oldmats:
    if mat.use_nodes:
     for node in mat.node_tree.nodes:
      if node.type=='TEX_IMAGE' and node.image==image:node.image=target
   if not image.users:bpy.data.images.remove(image)
  else:image_pool[key]=image
 for mat in list(set(bpy.data.materials)-oldmats):
  key=mat_key(mat)
  if key in material_pool:
   for obj in new:
    if obj.type=='MESH':
     for i,q in enumerate(obj.data.materials):
      if q==mat:obj.data.materials[i]=material_pool[key]
   if not mat.users:bpy.data.materials.remove(mat)
  else:mat['canonical_material_sha256']=key;material_pool[key]=mat
 local={}
 for obj in new:
  if obj.type!='MESH':continue
  mesh=obj.data;pointer=mesh.as_pointer()
  if pointer not in local:local[pointer]=mesh_key(mesh)
  key=local[pointer]
  if key in mesh_pool:obj.data=mesh_pool[key]
  else:mesh_pool[key]=mesh
 root=bpy.data.objects.new(bid+'-world',None);sc.collection.objects.link(root);root['building']=bid;root['source_sha256']=a['sha256'];root['footprint_m']=b['footprint_m'];root['world_yaw_deg']=r.get('world_placement',{}).get('rotation_z_deg',0)
 for obj in new:
  if obj.parent is None:obj.parent=root
 root.location=b['world_position_m'];root.rotation_euler.z=math.radians(root['world_yaw_deg']);records.append(dict(building=bid,source=str(path),sha256=a['sha256'],position_m=b['world_position_m'],rotation_z_deg=root['world_yaw_deg'],footprint_m=b['footprint_m'],height_m=r['height_m'],floor_triangles=a['floor_triangles']))
 print(bid,'placed',len(mesh_pool),'unique meshes',flush=True)
 for mesh in list(bpy.data.meshes):
  if not mesh.users:bpy.data.meshes.remove(mesh)
sc.frame_end=160;sc.frame_set(1);bpy.context.view_layer.update();OUT.mkdir(exist_ok=True)
# Save a useful opening viewport and a reproducible overview camera in Blender.
meshes=[o for o in sc.objects if o.type=='MESH'];points=[o.matrix_world@Vector(c) for o in meshes for c in o.bound_box];lo=Vector([min(v[k] for v in points) for k in range(3)]);hi=Vector([max(v[k] for v in points) for k in range(3)]);center=(lo+hi)/2;extent=max(hi.x-lo.x,hi.y-lo.y)
sc.world=bpy.data.worlds.new('Overview daylight');sc.world.use_nodes=True;sc.world.node_tree.nodes['Background'].inputs[0].default_value=(.65,.65,.65,1);bpy.ops.object.light_add(type='SUN',rotation=(.5,-.6,-.4));bpy.context.object.data.energy=3
bpy.ops.object.camera_add(location=center+Vector((extent*.15,-extent*.4,extent*.8)));cam=bpy.context.object;cam.rotation_euler=(center-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.type='ORTHO';cam.data.ortho_scale=extent*1.1;cam.data.clip_end=max(1000,extent*10);sc.camera=cam;sc.render.engine='CYCLES';sc.cycles.samples=8;sc.render.resolution_x=1800;sc.render.resolution_y=1000;sc.render.resolution_percentage=100;sc.render.filepath=str(OUT/'shop-street-overview.png')
from street_city_scene_review import frame_overview
shift,fit_scale=frame_overview(cam,points,sc.render.resolution_x/sc.render.resolution_y);center+=shift
for screen in bpy.data.screens:
 for area in screen.areas:
  if area.type=='VIEW_3D':
   space=area.spaces.active;space.clip_end=max(1000,extent*10);space.region_3d.view_location=center;space.region_3d.view_distance=fit_scale;space.region_3d.view_rotation=cam.rotation_euler.to_quaternion();space.region_3d.view_perspective='ORTHO';space.shading.type='MATERIAL'
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'shop-street-complete.blend'))
bpy.ops.export_scene.gltf(filepath=str(OUT/'shop-street-complete.glb'),export_format='GLB',export_extras=True,export_animation_mode='SCENE',export_anim_scene_split_object=False,export_cameras=False,export_lights=False)
report=dict(status='smoke_test_only' if args.smoke_ids else 'assembled_requires_world_acceptance',buildings=records,building_count=len(records),unique_meshes=len(mesh_pool),unique_materials=len(material_pool),unique_images=len(image_pool),glb_sha256=sha(OUT/'shop-street-complete.glb'),view_center_m=list(center),extent_m=extent,scope='Shop buildings at canonical continuous-world positions; existing terrain, roads and game controllers are separate')
(OUT/'world-assembly.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),'utf-8');bpy.ops.render.render(write_still=True)
