"""Real cutout foliage on certified remeshed panels/cylinders, with three authored LODs."""
import bpy,sys,math,random,hashlib
from pathlib import Path
from mathutils import Vector,Matrix
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_streetscape import ROOT,BASE,OUT,read,write,sha
from street_city_remesh_gate import verify
primitives={};certificates={}
for aid in ['panel','cylinder']:
 source=verify(BASE/'campus-v1/components'/('unit-'+aid)/'remesh');certificates[aid]=source;bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=source['path']);bpy.context.view_layer.update();primitives[aid]=[dict(vertices=[list(o.matrix_world@v.co) for v in o.data.vertices],faces=[list(p.vertices) for p in o.data.polygons]) for o in bpy.context.scene.objects if o.type=='MESH']
rows=[]
for aid,width,depth,height in [('camphor',7,6.5,8),('cherry',5.5,5,6)]:
 texture=OUT/'textures'/(aid+'-leaves.png');signature=hashlib.sha256((sha(__file__)+sha(texture)+''.join(q['sha256'] for q in certificates.values())).encode()).hexdigest();folder=OUT/'assets'/aid/'card-trees'/signature[:16];folder.mkdir(parents=True,exist_ok=True);levels=[]
 for level,clusters,cards in [(0,12,8),(1,6,6),(2,3,0)]:
  bpy.ops.wm.read_factory_settings(use_empty=True);rng=random.Random(20261020);vv=[];ff=[];mi=[];uvs=[];parts=[]
  bark=bpy.data.materials.new('Street foliage bark');bark.use_nodes=True;bark.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value=(.16,.085,.035,1);bark.node_tree.nodes['Principled BSDF'].inputs['Roughness'].default_value=.9
  leaves=bpy.data.materials.new('Street masked '+aid+' leaves');leaves.use_nodes=True;leaves.use_backface_culling=False;leaves.surface_render_method='DITHERED';bs=leaves.node_tree.nodes['Principled BSDF'];bs.inputs['Roughness'].default_value=.85;n=leaves.node_tree.nodes.new('ShaderNodeTexImage');n.image=bpy.data.images.load(str(texture));n.image.pack();leaves.node_tree.links.new(n.outputs['Color'],bs.inputs['Base Color']);clip=leaves.node_tree.nodes.new('ShaderNodeMath');clip.operation='GREATER_THAN';clip.inputs[1].default_value=.33;leaves.node_tree.links.new(n.outputs['Alpha'],clip.inputs[0]);leaves.node_tree.links.new(clip.outputs[0],bs.inputs['Alpha'])
  def append(kind,transform,material):
   parts.append(dict(primitive=kind,matrix=[list(row) for row in transform],material=material))
   for primitive in primitives[kind]:
    off=len(vv);vv.extend(transform@Vector(p) for p in primitive['vertices']);ff.extend([off+j for j in face] for face in primitive['faces']);mi.extend([material]*len(primitive['faces']))
    for face in primitive['faces']:
     local=[Vector(primitive['vertices'][j]) for j in face];normal=(local[1]-local[0]).cross(local[2]-local[0]);front=abs(normal.y)>abs(normal.x)+abs(normal.z)
     uvs.extend([(p.x+.5,p.z) if material==1 and front else (0,0) if material==1 else (p.x+p.y,p.z) for p in local])
  def branch(start,end,diameter):
   start,end=Vector(start),Vector(end);direction=end-start;transform=Matrix.Translation(start)@direction.to_track_quat('Z','Y').to_matrix().to_4x4()@Matrix.Diagonal(Vector((diameter,diameter,direction.length,1)));append('cylinder',transform,0)
  if level<2:branch((0,0,0),(0,0,2.2),.34);branch((0,0,2.2),(.10,0,3.5),.23)
  else:branch((0,0,0),(0,0,3.5),.32)
  anchors=[]
  for j in range(clusters):
   angle=j/clusters*2*math.pi;radius=2.0+.35*math.sin(j*1.7);z=4.5+(j%3)*.85;point=Vector((math.cos(angle)*radius,math.sin(angle)*radius,z));anchors.append(point);branch((0,0,2.4),point,.07 if level==0 else .08)
   for c in range(cards):
    pos=point+Vector((rng.uniform(-.55,.55),rng.uniform(-.55,.55),rng.uniform(-.25,.45)));yaw=angle+c*math.pi/cards;pitch=math.radians(rng.uniform(-35,35));size=1.9 if level==0 else 2.3;transform=Matrix.Translation(pos-Vector((0,0,size/2)))@Matrix.Rotation(yaw,4,'Z')@Matrix.Rotation(pitch,4,'X')@Matrix.Diagonal(Vector((size,.0008,size,1)));append('panel',transform,1)
  if level==2:
   for j in range(8):
    angle=j*math.pi/4;pos=Vector((math.cos(angle)*1.9,math.sin(angle)*1.9,5.4+(j%2)*.4));transform=Matrix.Translation(pos-Vector((0,0,1.6)))@Matrix.Rotation(angle+math.pi/2,4,'Z')@Matrix.Rotation(math.radians(-15+(j%3)*15),4,'X')@Matrix.Diagonal(Vector((3.3,.0008,3.2,1)));append('panel',transform,1)
  # Preserve the same physical envelope and root for every authored tree LOD.
  fx=width/2/max(abs(v.x) for v in vv);fy=depth/2/max(abs(v.y) for v in vv);fz=height/max(v.z for v in vv);normalization=Matrix.Diagonal(Vector((fx,fy,fz,1)));vv=[normalization@v for v in vv]
  mesh=bpy.data.meshes.new(aid+'-LOD'+str(level));mesh.from_pydata(vv,[],ff);mesh.update();mesh.materials.append(bark);mesh.materials.append(leaves);uv=mesh.uv_layers.new(name='Leaf card UV')
  for poly,index in zip(mesh.polygons,mi):poly.material_index=index;poly.use_smooth=index==0
  for loop,value in zip(uv.data,uvs):loop.uv=value
  ob=bpy.data.objects.new(aid+'-card-tree-LOD'+str(level),mesh);bpy.context.scene.collection.objects.link(ob);ob['remeshed_primitive_sources']=str([q['sha256'] for q in certificates.values()]);mesh.calc_loop_triangles();count=len(mesh.loop_triangles);cap=[2600,1000,340][level];assert count<=cap,(aid,level,count)
  path=folder/f'LOD{level}.glb';bpy.ops.export_scene.gltf(filepath=str(path),export_format='GLB',export_extras=True);bpy.ops.wm.save_as_mainfile(filepath=str(path.with_suffix('.blend')));levels.append(dict(level=level,path=str(path),sha256=sha(path),triangles=count,triangle_cap=cap,certified_sources=list(certificates.values()),primitive_sources=certificates,parts=parts,normalization_matrix=[list(row) for row in normalization],texture_sha256=sha(texture),texture=str(texture),construction='Authored cutout leaf cards and branches using exact Rust-certified primitive vertices'))
 rows.append(dict(id=aid,lods=levels))
write(OUT/'foliage-assets.json',dict(status='built_requires_independent_acceptance',assets=rows,generator_sha256=sha(__file__),texture_provider='Built-in GPT imagegen; original transparent botanical textures'))
