"""Independent measured sanitary LOD sources, followed by a mandatory external remesh."""
import bpy,sys,json
from pathlib import Path
from mathutils.bvhtree import BVHTree
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_remesh_gate import verify
BASE=Path(__file__).resolve().parents[1]/'output/street-social-city-production'
for aid,source,budget,tolerance in [('toilet','lod-toilet-compact',500,.015),('basin','tripo-basin-economy',300,.02)]:
 receipt=verify(BASE/'campus-v1/components'/source/'remesh');bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=receipt['path']);records=[]
 for o in list(bpy.context.scene.objects):
  if o.type!='MESH':continue
  o.data.calc_loop_triangles();vv=[v.co.copy() for v in o.data.vertices];tt=[tuple(t.vertices) for t in o.data.loop_triangles];tree=BVHTree.FromPolygons(vv,tt,all_triangles=True)
  bpy.context.view_layer.objects.active=o;mod=o.modifiers.new('Measured source LOD','DECIMATE');mod.ratio=budget/len(tt);mod.use_collapse_triangulate=True;bpy.ops.object.modifier_apply(modifier=mod.name);o.data.calc_loop_triangles()
  after=BVHTree.FromPolygons([v.co.copy() for v in o.data.vertices],[tuple(t.vertices) for t in o.data.loop_triangles],all_triangles=True);error=max(max(after.find_nearest(v)[3] for v in vv),max(tree.find_nearest(v.co)[3] for v in o.data.vertices))
  assert error<=tolerance,(aid,error)
  records.append(dict(source=receipt,triangles=len(o.data.loop_triangles),max_surface_error_m=error,tolerance_m=tolerance))
 out=BASE/'world-completion-v1/components'/aid;out.mkdir(parents=True,exist_ok=True);bpy.ops.export_scene.gltf(filepath=str(out/'measured-lod-source.glb'),export_format='GLB');(out/'lod-measurement.json').write_text(json.dumps(records,indent=2),'utf8')
