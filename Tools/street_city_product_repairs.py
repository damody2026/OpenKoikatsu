"""Metric structural members assembled from certified remeshed primitives.

AI curvature remains the source of the product, but long load-bearing members
must have an explicit minimum section rather than relying on a painted line.
"""
REPAIRS={'floor-lamp':[dict(primitive='cylinder',size=[.03,.03,1.39],pos=[0,0,.045],finish='bronze',reason='Continuous 30mm brass post between the actual base and shade; original AI post is too thin')]}

def members(aid):return REPAIRS.get(aid,[])

def material(finish):
 import bpy,json
 from pathlib import Path
 from street_city_building_context import ROOT
 from street_city_remesh_gate import sha
 name='Product member '+finish;existing=bpy.data.materials.get(name)
 if existing:return existing
 mat=bpy.data.materials.new(name);mat.use_nodes=True;bs=mat.node_tree.nodes['Principled BSDF']
 if finish.startswith('lux_'):
  row=json.loads((ROOT/'output/street-social-city-production/mall-v1/boutiques-v1/materials.json').read_text('utf8'))['materials'][finish]
  for role,info in row['maps'].items():
   assert sha(info['path'])==info['sha256'];tex=mat.node_tree.nodes.new('ShaderNodeTexImage');tex.image=bpy.data.images.load(info['path'],check_existing=True);tex.image.pack()
   if role!='basecolor':tex.image.colorspace_settings.name='Non-Color'
   if role=='normal':normal=mat.node_tree.nodes.new('ShaderNodeNormalMap');mat.node_tree.links.new(tex.outputs['Color'],normal.inputs['Color']);mat.node_tree.links.new(normal.outputs['Normal'],bs.inputs['Normal'])
   else:mat.node_tree.links.new(tex.outputs['Color'],bs.inputs['Base Color' if role=='basecolor' else 'Roughness'])
 else:
  bs.inputs['Base Color'].default_value={'bronze':(.5,.31,.09,1),'ivory':(.9,.87,.8,1),'black':(.02,.025,.03,1)}[finish];bs.inputs['Metallic'].default_value=.82 if finish=='bronze' else 0;bs.inputs['Roughness'].default_value=.28 if finish=='bronze' else .8
 return mat

def style(aid,mesh):
 return None

def metric_basis(aid,basis):
 return basis

def preview(aid):
 import bpy,sys
 from pathlib import Path
 from mathutils import Matrix,Vector
 from street_city_building_context import ROOT
 from street_city_remesh_gate import verify
 for q in members(aid):
  folder=ROOT/'output/street-social-city-production/campus-v1/components'/('unit-'+q['primitive'])/'remesh';r=verify(folder);before=set(bpy.data.objects);bpy.ops.import_scene.gltf(filepath=r['path']);new=set(bpy.data.objects)-before;bpy.context.view_layer.update()
  mat=material(q['finish'])
  for o in new:
   if o.type!='MESH':continue
   import math
   o.matrix_world=Matrix.Translation(Vector(q['pos']))@Matrix.Rotation(math.radians(q.get('yaw',0)),4,'Z')@Matrix.Diagonal(Vector((*q['size'],1)))@o.matrix_world;o.data.materials.clear();o.data.materials.append(mat)
 return True
