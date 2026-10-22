"""Measured soft-furniture LOD; its output still requires the real Rust pass."""
import bpy,sys,json,math
from pathlib import Path
from mathutils.bvhtree import BVHTree
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_mall import component_folders
from street_city_remesh_gate import verify,sha
folder=Path(__file__).resolve().parents[1]/'output/street-social-city-production/world-completion-v1/components/bed'
receipt=verify(component_folders()['product-bed'])
bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=receipt['path'])
records=[]
for o in list(bpy.context.scene.objects):
 if o.type!='MESH':continue
 bpy.context.view_layer.objects.active=o;o.data.calc_loop_triangles()
 before=[v.co.copy() for v in o.data.vertices];tri=[tuple(t.vertices) for t in o.data.loop_triangles];tree=BVHTree.FromPolygons(before,tri,all_triangles=True)
 for budget in [650,750,850,950,1050]:
  original=o.data.copy();mod=o.modifiers.new('Soft furniture measured LOD','DECIMATE');mod.ratio=budget/len(tri);mod.use_collapse_triangulate=True;bpy.ops.object.modifier_apply(modifier=mod.name);o.data.calc_loop_triangles()
  after=BVHTree.FromPolygons([v.co.copy() for v in o.data.vertices],[tuple(t.vertices) for t in o.data.loop_triangles],all_triangles=True)
  deviation=max(max(after.find_nearest(v)[3] for v in before),max(tree.find_nearest(v.co)[3] for v in o.data.vertices))
  if deviation<=.05:break
  o.data=original
 assert deviation<=.05,deviation
 records.append(dict(original_triangles=len(tri),lod_triangles=len(o.data.loop_triangles),maximum_surface_deviation_m=deviation,source=receipt))
folder.mkdir(parents=True,exist_ok=True)
bpy.ops.export_scene.gltf(filepath=str(folder/'measured-lod-source.glb'),export_format='GLB')
(folder/'lod-measurement.json').write_text(json.dumps(records,indent=2),'utf8')
