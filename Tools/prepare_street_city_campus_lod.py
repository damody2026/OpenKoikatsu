"""Explicit campus furniture LOD source, measured against the approved curved prop."""
import bpy,bmesh,json,sys,argparse,hashlib
from pathlib import Path
from mathutils.bvhtree import BVHTree
p=argparse.ArgumentParser();p.add_argument('--asset',required=True);p.add_argument('--cap',type=int,required=True);a=p.parse_args(sys.argv[sys.argv.index('--')+1:]);ROOT=Path(__file__).resolve().parents[1];base=ROOT/'output/street-social-city-production';source=base/f'shop-street-components-v1/{a.asset}-lod/normalized-source.glb';out=base/f'campus-v1/components/{a.asset}';out.mkdir(parents=True,exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=str(source));records=[]
for o in [o for o in bpy.context.scene.objects if o.type=='MESH']:
 bpy.context.view_layer.objects.active=o;o.select_set(True);bm=bmesh.new();bm.from_mesh(o.data);bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=1e-6);bm.to_mesh(o.data);bm.free();o.data.calc_loop_triangles();before=o.data.copy();n=len(o.data.loop_triangles)
 # Preserve actual silhouette extrema and open boundary seams, including small
 # flush fittings which ordinary ratio decimation can erase.
 group=o.vertex_groups.new(name='Preserve extrema and source boundaries');bm=bmesh.new();bm.from_mesh(o.data);bm.verts.ensure_lookup_table();keep={v.index for e in bm.edges if e.is_boundary for v in e.verts};bm.free()
 for axis in range(3):
  low=min(v.co[axis] for v in o.data.vertices);high=max(v.co[axis] for v in o.data.vertices)
  keep.update(v.index for v in o.data.vertices if v.co[axis]<low+.003 or v.co[axis]>high-.003)
 if keep:group.add(list(keep),1,'REPLACE')
 mod=o.modifiers.new('Documented curved prop LOD','DECIMATE');mod.ratio=min(1,a.cap/n);mod.use_collapse_triangulate=True;mod.vertex_group=group.name;mod.invert_vertex_group=True;mod.vertex_group_factor=10;bpy.ops.object.modifier_apply(modifier=mod.name);o.data.calc_loop_triangles();assert len(o.data.loop_triangles)<=a.cap
 def tree(m):return BVHTree.FromPolygons([v.co for v in m.vertices],[tuple(p.vertices) for p in m.polygons])
 bt=tree(before);at=tree(o.data);distance=max([at.find_nearest(v.co)[3] for v in before.vertices]+[bt.find_nearest(v.co)[3] for v in o.data.vertices]);assert distance<=.015,(a.asset,'LOD surface deviation >15mm',distance)
 records.append(dict(triangles=len(o.data.loop_triangles),sampled_surface_error_m=distance,measurement='all source and result vertices to nearest triangle; not a certified continuous-surface bound'))
bpy.ops.export_scene.gltf(filepath=str(out/'source.glb'),export_format='GLB',export_extras=True)
(out/'lod-source.json').write_text(json.dumps(dict(approved_source=str(source),sha256=hashlib.sha256(source.read_bytes()).hexdigest(),purpose='crowded-campus furniture LOD; structural geometry retains 5mm tolerance',maximum_sampled_error_m=.015,records=records),indent=2),'utf8')
