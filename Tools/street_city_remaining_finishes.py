"""Distinct city building finishes and frontage, using certified panel instances."""
import math
from pathlib import Path
from street_city_campus_recipes import p

def apply(C,scene,roots,obj,mesh_for,mats,matkeys):
 import bpy
 from street_city_mall_architecture import materials,SPECS
 from street_city_localization import path_for,register_material
 bid=C['id'];B=C['building'];W,D=B['footprint_m'];base=Path(__file__).resolve().parents[1]/'output/street-social-city-production'
 stone=materials(base/'mall-v1/materials');mats.update(stone)
 for key in stone:
  if key not in matkeys:matkeys.append(key)
 palette={'H1':('limestone','interiorstone','bronze'),'R1':('ceramic','mallfloor','wood'),'R2':('limestone','mallfloor','wood'),'O01':('darkstone','mallfloor','steel'),'O02':('ceramic','mallfloor','wood'),'O03':('limestone','mallfloor','bronze'),'CV':('ceramic','mallfloor','wood'),'TR':('darkstone','mallfloor','steel')}
 facade,floor,trim=palette[bid]
 for o in list(scene.objects):
  if o.type!='MESH' or not str(o.get('asset_id','')).startswith('structure-F'):continue
  kind=o.get('part_kind');o.data=o.data.copy();o.data.materials.clear()
  keys=['mallpaint',facade,floor,'wetwall']
  for key in keys:o.data.materials.append(mats[key])
  uv=o.data.uv_layers.active
  for poly in o.data.polygons:
   co=o.matrix_world@poly.center;edge=abs(co.x)>W/2-.3 or abs(co.y)>D/2-.3
   key=floor if kind in ('floor','stair_tread','stair_landing') else facade if edge else 'mallpaint'
   poly.material_index=keys.index(key);axes=[i for i in range(3) if i!=max(range(3),key=lambda k:abs(poly.normal[k]))];period=SPECS.get(key,{}).get('repeat_m',[1,1])
   for li in poly.loop_indices:
    v=o.data.vertices[o.data.loops[li].vertex_index].co;uv.data[li].uv=(v[axes[0]]/period[0],v[axes[1]]/period[1])
 # Slab-edge cornices and restrained vertical frontage fins give a human scale.
 for f in B['floors']:
  no=f['floor'];z=f['z_m'];h=f['height_m'];parts=[]
  for sign in [-1,1]:
   parts += [p((W,.12,.18),(0,sign*(D/2+.06),z+h-.3),trim),p((.12,D,.18),(sign*(W/2+.06),0,z+h-.3),trim)]
   for index in range(0,int(W//8)):
    xx=-W/2+2+8*index;parts.append(p((.18,.28,h-.5),(xx,sign*(D/2+.14),z+.1),trim))
  if no==len(B['floors']):
   parts += [p((W,.2,.75),(0,sign*(D/2-.1),z+h),facade) for sign in [-1,1]]+[p((.2,D,.75),(sign*(W/2-.1),0,z+h),facade) for sign in [-1,1]]
  obj(f'{bid}-F{no:02d}-frontage',no,mesh_for(f'{bid}-F{no:02d}-frontage',parts),kind='facade_detail')
 # One canopy for each actual ground entrance; never a detached decorative door.
 for e in B['entrances']:
  x,y,z=e['position_m'];yaw={'N':180,'S':0,'E':90,'W':-90}[e['facade_side']]
  obj(e['id']+'-canopy',1,mesh_for('canopy-'+bid,[p((7,2.4,.18),(0,-1.25,3),trim),p((6.8,2.2,.035),(0,-1.25,2.96),'light')]),(x,y,z),yaw,kind='canopy')
  # Building identity is real image-bearing geometry and receives the same locale MPC as room signs.
  key='building.'+bid;mat=bpy.data.materials.new(bid+' exterior identity');mat.use_nodes=True;tex=mat.node_tree.nodes.new('ShaderNodeTexImage');tex.image=bpy.data.images.load(str(path_for(key,'zh-Hant')));tex.image.pack();mat.node_tree.links.new(tex.outputs['Color'],mat.node_tree.nodes['Principled BSDF'].inputs['Base Color']);register_material(mat,key)
  mkey='identity-'+bid;mats[mkey]=mat
  if mkey not in matkeys:matkeys.append(mkey)
  sign=obj(e['id']+'-identity',1,mesh_for('identity-'+bid,[p((6,.06,.9),(0,-.06,3.3),mkey)]),(x,y,z),yaw,kind='sign')
  for poly in sign.data.polygons:
   for li in poly.loop_indices:
    v=sign.data.vertices[sign.data.loops[li].vertex_index].co;sign.data.uv_layers.active.data[li].uv=(v.x/6+.5,(v.z-3.3)/.9)
 # H1 lobby: stone reception backdrop, bronze battens, suspended coffer lights and planters.
 if bid=='H1':
  parts=[]
  for x in [-49,-39,-29]:
   parts += [p((3.2,.6,1.05),(x,19,0),'interiorstone'),p((3.3,.65,.06),(x,19,1.05),'bronze')]
  for x in [-51,-27]:
   parts += [p((1.6,1.6,.1),(x,20,0),'darkstone')]+[p((1.6,.1,.9),(x,20+dy,.1),'darkstone') for dy in [-.75,.75]]+[p((.1,1.4,.9),(x+dx,20,.1),'darkstone') for dx in [-.75,.75]]
  for x in [-46,-33]:
   for y in [9,17]:
    parts += [p((3.6,2.2,.08),(x,y,3.9),'bronze'),p((3.3,1.9,.025),(x,y,3.87),'light')]
  obj('H1-lobby-focal-details',1,mesh_for('H1-lobby-details',parts),kind='interior_detail')
