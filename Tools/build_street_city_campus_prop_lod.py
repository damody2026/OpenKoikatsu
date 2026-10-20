"""Measured LOD of new native Tripo props; reject shape error rather than hiding it."""
import bpy,bmesh,json,sys,argparse,hashlib
from pathlib import Path
from mathutils.bvhtree import BVHTree
p=argparse.ArgumentParser();p.add_argument('--source',required=True);p.add_argument('--output',required=True);p.add_argument('--caps',nargs='+',type=int,required=True);a=p.parse_args(sys.argv[sys.argv.index('--')+1:]);out=Path(a.output);out.mkdir(parents=True,exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=a.source);records=[]
def tree(m):return BVHTree.FromPolygons([v.co for v in m.vertices],[tuple(p.vertices) for p in m.polygons])
for o in [o for o in bpy.context.scene.objects if o.type=='MESH']:
 bpy.context.view_layer.objects.active=o;bpy.ops.object.select_all(action='DESELECT');o.select_set(True);bm=bmesh.new();bm.from_mesh(o.data);bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=1e-6);bm.to_mesh(o.data);bm.free();before=o.data.copy();bt=tree(before);before.calc_loop_triangles();n=len(before.loop_triangles);trials=[]
 for cap in a.caps:
  o.data=before.copy();mod=o.modifiers.new('Measured game LOD','DECIMATE');mod.ratio=min(1,cap/n);mod.use_collapse_triangulate=True;bpy.ops.object.modifier_apply(modifier=mod.name);o.data.calc_loop_triangles();at=tree(o.data);distance=max([at.find_nearest(v.co)[3] for v in before.vertices]+[bt.find_nearest(v.co)[3] for v in o.data.vertices]);bounds=max(abs(f(before.vertices)-f(o.data.vertices)) for k in range(3) for f in [lambda v,k=k:min(p.co[k] for p in v),lambda v,k=k:max(p.co[k] for p in v)]);trials.append(dict(cap=cap,triangles=len(o.data.loop_triangles),sampled_surface_error_m=distance,bounds_error_m=bounds))
  if distance<=.015 and bounds<=.006:break
 else:raise RuntimeError('No valid LOD '+str(trials))
 records.append(dict(selected=trials[-1],trials=trials))
bpy.ops.export_scene.gltf(filepath=str(out/'source.glb'),export_format='GLB',export_extras=True)
(out/'lod-source.json').write_text(json.dumps(dict(source=a.source,source_sha256=hashlib.sha256(Path(a.source).read_bytes()).hexdigest(),maximum_sampled_error_m=.015,bounds_tolerance_m=.006,measurement='all source and output vertices to nearest triangle; sampled, not continuous bound',records=records),indent=2),'utf8')
