"""Reuse actual Tripo flowers as planting, removing the florist's metal bucket."""
import bpy,bmesh,sys
from pathlib import Path
from mathutils import Matrix
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_streetscape import ROOT,BASE,OUT,write,sha
from street_city_remesh_gate import verify
source=verify(BASE/'shop-street-components-v1/flowers/remesh');bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=source['path']);bpy.context.view_layer.update();count=0
for ob in [o for o in bpy.context.scene.objects if o.type=='MESH']:
 transform=ob.matrix_world.copy();ob.parent=None;ob.matrix_world.identity();ob.data.transform(transform);bm=bmesh.new();bm.from_mesh(ob.data)
 bmesh.ops.bisect_plane(bm,geom=list(bm.verts)+list(bm.edges)+list(bm.faces),plane_co=(0,0,.30),plane_no=(0,0,1),clear_inner=True,clear_outer=False,dist=.00001)
 bmesh.ops.delete(bm,geom=[v for v in bm.verts if not v.link_faces],context='VERTS');bm.to_mesh(ob.data);bm.free()
 for v in ob.data.vertices:v.co.z-=.30
 ob.data.update();ob.data.calc_loop_triangles();count+=len(ob.data.loop_triangles)
assert 100<count<6000;folder=OUT/'assets/bed-flowers';folder.mkdir(parents=True,exist_ok=True);path=folder/'normalized-source.glb';bpy.ops.export_scene.gltf(filepath=str(path),export_format='GLB',export_extras=True)
write(folder/'source.json',dict(source=source,operation='remove every surface below z=0.30m, including metal bucket and handle; preserve flower/leaf geometry, materials and UV; rebase planting stems to z=0',triangles=count,path=str(path),sha256=sha(path),generator_sha256=sha(__file__),requires='real Rust remesh followed by measured 800-triangle planted LOD'))
