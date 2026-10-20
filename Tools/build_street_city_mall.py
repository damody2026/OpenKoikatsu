"""Assemble all ten floors from actual Rust-certified component vertices."""
import bpy,sys,json,math
from pathlib import Path
from mathutils import Matrix,Vector
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_mall import OUT,ROOT,plan,sha,component_folders
from street_city_remesh_gate import verify
files={str(ROOT/'Tools'/name):sha(ROOT/'Tools'/name) for name in ['street_city_mall.py','street_city_vertical_transport.py','build_street_city_mall.py','street_city_campus_recipes.py','street_city_parts.py','street_city_small_shop.py','street_city_surface_materials.py','street_city_mall_architecture.py','street_city_mall_boutiques.py','street_city_mall_stock.py','street_city_mall_merchandise.py']}
files[str(ROOT/'Tools/street_city_product_normals.py')]=sha(ROOT/'Tools/street_city_product_normals.py')
files[str(ROOT/'Tools/street_city_product_repairs.py')]=sha(ROOT/'Tools/street_city_product_repairs.py')
OUT.mkdir(parents=True,exist_ok=True);G=plan();C=G['contract'];bpy.ops.wm.read_factory_settings(use_empty=True);sc=bpy.context.scene;sc.unit_settings.system='METRIC';roots={};sources={};cache={};meshes={}
colors={'wood':(.38,.24,.12),'paint':(.8,.79,.75),'tile':(.7,.69,.63),'steel':(.4,.42,.43),'black':(.025,.03,.032),'ivory':(.85,.82,.75),'fabric':(.32,.4,.38),'glass':(.65,.8,.82),'screen':(.1,.25,.32),'book0':(.17,.3,.27),'book1':(.46,.21,.15),'book2':(.72,.63,.4),'light':(.94,.93,.82),'mirror':(.85,.88,.9)}
mats={}
for k,col in colors.items():
 m=bpy.data.materials.new('M1 '+k);m.use_nodes=True;bs=m.node_tree.nodes['Principled BSDF'];bs.inputs['Base Color'].default_value=(*col,1);bs.inputs['Roughness'].default_value=.12 if k in ('glass','mirror') else .6
 if k in ('steel','mirror'):bs.inputs['Metallic'].default_value=.85
 if k=='glass':bs.inputs['Alpha'].default_value=.2;m.surface_render_method='DITHERED'
 if k=='light':bs.inputs['Emission Color'].default_value=(*col,1);bs.inputs['Emission Strength'].default_value=3
 mats[k]=m
wood_path=ROOT/'output/street-social-city-production/campus-v1/materials/oak-2k.png'
if wood_path.exists():
 m=mats['wood'];tex=m.node_tree.nodes.new('ShaderNodeTexImage');tex.image=bpy.data.images.load(str(wood_path));tex.image.pack();m.node_tree.links.new(tex.outputs['Color'],m.node_tree.nodes['Principled BSDF'].inputs['Base Color'])
from street_city_mall_architecture import materials as architectural_materials,finish as architectural_finish,SPECS,receipt
mats.update(architectural_materials(OUT/'materials'))
from street_city_mall_boutiques import materials as boutique_materials,specifications,OUT as BOUTIQUES
mats.update(boutique_materials());SPECS.update(specifications())

def load(key):
 if key in cache:return cache[key]
 dirs=component_folders()
 receipt=verify(dirs[key]);sources[key]=receipt;before=set(bpy.data.objects);bpy.ops.import_scene.gltf(filepath=str(Path(receipt['path']).resolve()));new=set(bpy.data.objects)-before;bpy.context.view_layer.update();r=[]
 for o in new:
  if o.type=='MESH':
   mesh=o.data.copy()
   if key.startswith('product-'):
    from street_city_product_normals import apply as product_normals
    product_normals(mesh)
    from street_city_product_repairs import style as product_style
    product_style(key.removeprefix('product-'),mesh)
   basis=o.matrix_world.copy()
   if key.startswith('product-'):
    from street_city_product_repairs import metric_basis
    basis=metric_basis(key.removeprefix('product-'),basis)
   r.append((mesh,basis))
 for o in new:bpy.data.objects.remove(o,do_unlink=True)
 cache[key]=r;return r
def object_(name,no,mesh,kind,pos=(0,0,0),yaw=0):
 o=bpy.data.objects.new(name,mesh);sc.collection.objects.link(o);o.parent=roots[no];o.location=pos;o.rotation_euler.z=math.radians(yaw);o['floor']=no;o['part_kind']=kind;o['mall_id']='M1';return o
for f in C['floors']:
 n=f['floor'];root=bpy.data.objects.new(f'M1-F{n:02d}',None);sc.collection.objects.link(root);root['floor']=n;roots[n]=root
 for kind in sorted({q['kind'] for q in G['parts'] if q['floor']==n}):
  vv=[];ff=[];indices=[];graphic_uv={};keys=sorted({architectural_finish(q) for q in G['parts'] if q['floor']==n and q['kind']==kind}|({'bronze'} if kind=='floor' else set()))
  for q in [p for p in G['parts'] if p['floor']==n and p['kind']==kind]:
   pos=q['pos'];size=q['size'];matrix=Matrix.Translation(Vector(pos))@Matrix.Rotation(math.radians(q.get('yaw',0)),4,'Z')@Matrix.Rotation(math.radians(q.get('pitch_y',0)),4,'Y')@Matrix.Rotation(math.radians(q.get('pitch_x',0)),4,'X')@Matrix.Diagonal(Vector((*size,1)))
   for mesh,basis in load(q['primitive']):
    start=len(vv);new_vertices=[matrix@basis@v.co for v in mesh.vertices]
    if 'slope_x' in q:
     for v in new_vertices:v.z+=q['slope_x']*(v.x-pos[0])
    if 'grade_start' in q:
     aa,bb=q['grade_start'],q['grade_end'];dx,dy=bb[0]-aa[0],bb[1]-aa[1]
     for v in new_vertices:v.z+=aa[2]+((v.x-aa[0])*dx+(v.y-aa[1])*dy)/(dx*dx+dy*dy)*(bb[2]-aa[2])
    if q['finish'] in ('mallbrand','campaign'):
     a=math.radians(q.get('yaw',0))
     for vi,v in enumerate(new_vertices):
      dx,dy=v.x-pos[0],v.y-pos[1];u=(dx*math.cos(a)+dy*math.sin(a))/size[0]+.5 if size[0]>size[1] else (-dx*math.sin(a)+dy*math.cos(a))/size[1]+.5
      if q['name'].endswith('-west'):u=1-u
      graphic_uv[start+vi]=(u,(v.z-pos[2])/size[2])
    vv.extend(new_vertices);ff.extend([start+i for i in f.vertices] for f in mesh.polygons);indices.extend([keys.index(architectural_finish(q))]*len(mesh.polygons))
  mesh=bpy.data.meshes.new(f'F{n:02d}-{kind}');mesh.from_pydata([tuple(round(float(v[k]),5) for k in range(3)) for v in vv],[],ff);mesh.update()
  for k in keys:mesh.materials.append(mats[k])
  uv=mesh.uv_layers.new(name='Metric UV')
  for poly,index in zip(mesh.polygons,indices):
   # Give existing perimeter slab faces the metal spandrel finish, without a coincident overlay.
   if kind=='floor' and abs(poly.normal.z)<.5 and (abs(poly.center.x)>74.98 or abs(poly.center.y)>49.98):index=keys.index('bronze')
   poly.material_index=index;axis=max(range(3),key=lambda k:abs(poly.normal[k]));axes=[k for k in range(3) if k!=axis]
   for li in poly.loop_indices:co=mesh.vertices[mesh.loops[li].vertex_index].co;period=SPECS.get(keys[index],{}).get('repeat_m',[1,1]);uv.data[li].uv=graphic_uv.get(mesh.loops[li].vertex_index,(co[axes[0]]/period[0],co[axes[1]]/period[1]))
  o=object_(mesh.name,n,mesh,kind);o['asset_id']=f'structure-F{n:02d}' if kind in ('floor','wall','ceiling','stair_tread','stair_landing') else kind
for q in G['curves']:
 for j,(mesh,basis) in enumerate(load(q['asset'])):
  o=object_(q['name']+str(j),q['floor'],mesh,q.get('kind','fixture' if q['asset']!='mannequin' else 'merchandise'),q['pos'],q['yaw']);o.matrix_basis=Matrix.Translation(Vector(q['pos']))@Matrix.Rotation(math.radians(q['yaw']),4,'Z')@basis;o['asset_id']=q['asset']
  for key in ['product_id','tenant_id','support_id']:
   if key in q:o[key]=q[key]
  if q.get('finish_override'):
   o.data=mesh.copy();o.data.materials.clear();o.data.materials.append(mats[q['finish_override']])
  if 'product_id' in q:o['placement_id']=q['name']
# Door frames and open leaves are made from certified panels as well.
panel,basis=load('panel')[0]
def board(name,no,size,pos,yaw=0,mat='wood',kind='doorframe'):
 if name.startswith('entry-') and mat=='wood':mat='glass' if kind=='doorleaf' else 'bronze'
 mesh=panel.copy();matrix=Matrix.Diagonal(Vector((*size,1)))@basis;mesh.transform(matrix);mesh.materials.clear();mesh.materials.append(mats[mat]);o=object_(name,no,mesh,kind,pos,yaw);return o
for d in G['doors']:
 n=d['floor'];x,y,z=d['position_m'];w=d['width_m'];h=d['height_m'];yaw=d['yaw_deg'];angle=math.radians(yaw)
 def point(dx,dy,dz=0):return [x+dx*math.cos(angle)-dy*math.sin(angle),y+dx*math.sin(angle)+dy*math.cos(angle),z+dz]
 for sign in [-1,1]:board(d['id']+'-jamb',n,[.08,.16,h],point(sign*(w/2+.04),0),yaw)
 board(d['id']+'-lintel',n,[w+.16,.16,.08],point(0,0,h),yaw)
 if d.get('kind')!='lift':
  for sign in ([-1,1] if w>1.5 else [-1]):
   width=w/2-.004 if w>1.5 else w-.004
   # Interior leaves open into their rooms; keep public aisles clear. Exterior
   # building entrances open outward. Both hinge axes stay outside the aperture.
   side=-1 if d['id'].startswith('entry-') else 1
   board(d['id']+'-open-leaf',n,[width,.04,h-.03],point(sign*(w/2+.03),side*(width/2+.12)),yaw+90,kind='doorleaf')
# Purpose-specific materials are already assigned per part; never overwrite every wall with paint.
finish=receipt(OUT/'materials')
# Tenant identities are real textured signs, not only labels in the HTML.
sign_dir=OUT/'signs'
for shop in G['shops']:
 rid=shop['id'];path=sign_dir/(rid+'.png');assert path.is_file(),path;m=bpy.data.materials.new(rid+' identity');m.use_nodes=True;tex=m.node_tree.nodes.new('ShaderNodeTexImage');tex.image=bpy.data.images.load(str(path));tex.image.pack();m.node_tree.links.new(tex.outputs['Color'],m.node_tree.nodes['Principled BSDF'].inputs['Base Color']);m.node_tree.nodes['Principled BSDF'].inputs['Roughness'].default_value=.6
 from street_city_localization import register_material
 register_material(m,'tenant.'+rid)
 x,y,z=shop['sign_position'];o=board(rid+'-identity',shop['floor'],[4,.04,1],[x,y,z],0 if y>0 else 180,kind='sign');o.data.materials.clear();o.data.materials.append(m);o.data.materials.append(mats['bronze']);uv=o.data.uv_layers.active or o.data.uv_layers.new(name='Sign UV')
 for poly in o.data.polygons:
  poly.material_index=0 if poly.normal.y<-.5 else 1
  for li in poly.loop_indices:co=o.data.vertices[o.data.loops[li].vertex_index].co;uv.data[li].uv=(co.x/4+.5,co.z)
 for dx in [-1.5,1.5]:board(rid+'-sign-suspension',shop['floor'],[.015,.015,.39],[x+dx,y,z+1],mat='steel',kind='sign_support')
counts={}
for n,root in roots.items():
 count=0
 for o in root.children_recursive:
  if o.type=='MESH':o.data.calc_loop_triangles();count+=len(o.data.loop_triangles)
 counts[n]=count
 assert count<=100000,(n,count)
 bpy.ops.object.select_all(action='DESELECT')
 for o in [root,*root.children_recursive]:o.select_set(True)
 bpy.ops.export_scene.gltf(filepath=str(OUT/f'M1-F{n:02d}-complete-2k.glb'),export_format='GLB',use_selection=True,export_extras=True,export_animations=False)
bpy.ops.object.select_all(action='SELECT');bpy.ops.export_scene.gltf(filepath=str(OUT/'M1-complete-2k.glb'),export_format='GLB',export_extras=True,export_animations=False);bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'M1-complete-2k.blend'))
assert all(sha(path)==digest for path,digest in files.items()),'Geometry source changed during build; rerun, never certify a mixed version'
(OUT/'programme.json').write_text(json.dumps(C,ensure_ascii=False,indent=2),'utf8');(OUT/'geometry.json').write_text(json.dumps(G,ensure_ascii=False),'utf8')
(OUT/'assembly-report.json').write_text(json.dumps(dict(status='assembled_requires_acceptance',floor_triangles=counts,sources=sources,source_files=files,model_sha256=sha(OUT/'M1-complete-2k.glb'),geometry_sha256=sha(OUT/'geometry.json'),materials=finish,boutiques_sha256=sha(BOUTIQUES/'materials.json'),limitations=C['limitations']),ensure_ascii=False,indent=2),'utf8');print('M1 assembled',counts,flush=True)
