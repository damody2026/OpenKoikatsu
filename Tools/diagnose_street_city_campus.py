import bpy,json
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
folder=Path('C:/portable/OpenKoikatsu/output/street-social-city-production/campus-v1/buildings/SC');bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=str(folder/'SC-complete-2k.glb'));bpy.context.scene.frame_set(1);bpy.context.view_layer.update();dg=bpy.context.evaluated_depsgraph_get()
for id in ['SC-0101-support1-door','SC-0104-support1-door']:
 r=json.loads((folder/'assembly-report.json').read_text('utf8'));d=next(d for d in r['doors'] if d['id']==id);x,y,z=d['position_m'];origin=Vector((x+.4,y+.4,z+1.1));res=bpy.context.scene.ray_cast(dg,origin,Vector((-1,0,0)),distance=.8);print('DIAG',id,res[0],res[4].name if res[0] else '',list(res[1]))
 for o in bpy.context.scene.objects:
  if o.type!='MESH' or o.get('part_kind') in ('water','light','sign'):continue
  bt=BVHTree.FromPolygons([o.matrix_world@v.co for v in o.data.vertices],[tuple(p.vertices) for p in o.data.polygons]);hit=bt.ray_cast(origin,Vector((-1,0,0)),.8)
  if hit[0] is not None:print('BVH_HIT',o.name,list(hit[0]),hit[3])
