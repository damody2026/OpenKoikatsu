"""Measured display LOD sources. The caller must subsequently run real Rust remesh."""
import bpy,bmesh,sys,argparse,math
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_streetscape import OUT,write,sha
p=argparse.ArgumentParser();p.add_argument('--source',required=True);p.add_argument('--asset',required=True);p.add_argument('--level',type=int,required=True);p.add_argument('--cap',type=int,required=True);p.add_argument('--error',type=float,required=True);a=p.parse_args(sys.argv[sys.argv.index('--')+1:]);folder=OUT/'assets'/a.asset/f'lod{a.level}';folder.mkdir(parents=True,exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=a.source);bpy.context.view_layer.update();objects=[o for o in bpy.context.scene.objects if o.type=='MESH'];total=0;originals={}
def tree(mesh):mesh.calc_loop_triangles();return BVHTree.FromPolygons([v.co for v in mesh.vertices],[list(t.vertices) for t in mesh.loop_triangles],all_triangles=True)
for o in objects:
 transform=o.matrix_world.copy();o.parent=None;o.matrix_world.identity();o.data.transform(transform);o.data.calc_loop_triangles();total+=len(o.data.loop_triangles);originals[o.name]=o.data.copy()
protected={};adjacency={}
for name,m in originals.items():
 limits=[(min(v.co[k] for v in m.vertices),max(v.co[k] for v in m.vertices)) for k in range(3)]
 protected[name]={v.index for v in m.vertices if any(min(abs(v.co[k]-low),abs(v.co[k]-high))<.0001 for k,(low,high) in enumerate(limits))};adjacency[name]={v.index:set() for v in m.vertices}
 for edge in m.edges:
  u,v=edge.vertices;adjacency[name][u].add(v);adjacency[name][v].add(u)
trials=[];chosen=None
for attempt in range(8):
 counts=0;error=0;bounds=0
 for o in objects:
  original=originals[o.name];o.data=original.copy();original.calc_loop_triangles();n=len(original.loop_triangles);bpy.context.view_layer.objects.active=o
  for g in list(o.vertex_groups):o.vertex_groups.remove(g)
  g=o.vertex_groups.new(name='Measured thin-feature protection');g.add(list(range(len(original.vertices))),1,'REPLACE')
  if protected[o.name]:g.add(list(protected[o.name]),0,'REPLACE')
  ratio=min(1,a.cap/total);mod=o.modifiers.new('Explicit distance LOD before Rust','DECIMATE');mod.ratio=ratio;mod.use_collapse_triangulate=True;mod.vertex_group=g.name;mod.vertex_group_factor=1000;bpy.ops.object.modifier_apply(modifier=mod.name)
  # glTF prunes vertices which are not referenced by any face. Measure the same
  # renderable surface rather than allowing loose extrema to fake a stable size.
  bm=bmesh.new();bm.from_mesh(o.data);bmesh.ops.delete(bm,geom=[v for v in bm.verts if not v.link_faces],context='VERTS');bm.to_mesh(o.data);bm.free()
  lo=min(v.co.z for v in o.data.vertices);hi=max(v.co.z for v in o.data.vertices);oldlo=min(v.co.z for v in original.vertices);oldhi=max(v.co.z for v in original.vertices);factor=(oldhi-oldlo)/(hi-lo);assert .85<factor<1.15,'LOD vertical silhouette changed excessively'
  for v in o.data.vertices:v.co.z=oldlo+(v.co.z-lo)*factor
  o.data.update();o.data.calc_loop_triangles();counts+=len(o.data.loop_triangles)
  before=tree(original);after=tree(o.data);distances=[after.find_nearest(v.co)[3] for v in original.vertices];error=max(error,max(distances),max(before.find_nearest(v.co)[3] for v in o.data.vertices))
  bad={i for i,d in enumerate(distances) if d>a.error*.7}
  for _ in range(2):bad|={j for i in list(bad) for j in adjacency[o.name][i]}
  protected[o.name]|=bad
  bounds=max(bounds,max(abs(f(original.vertices)-f(o.data.vertices)) for k in range(3) for f in [lambda vs,k=k:min(v.co[k] for v in vs),lambda vs,k=k:max(v.co[k] for v in vs)]))
 trials.append(dict(triangles=counts,sampled_surface_error_m=error,bounds_error_m=bounds))
 if counts<=a.cap and error<=a.error and bounds<=a.error:chosen=trials[-1];break
assert chosen,(a.asset,a.level,trials)
target=folder/'normalized-source.glb';bpy.ops.export_scene.gltf(filepath=str(target),export_format='GLB',export_extras=True,export_animations=False)
write(folder/'lod-source.json',dict(status='source_requires_rust',asset=a.asset,level=a.level,source=a.source,source_sha256=sha(a.source),path=str(target),sha256=sha(target),triangle_cap=a.cap,maximum_source_deviation_m=a.error,measurement=chosen,trials=trials,generator_sha256=sha(__file__)))
