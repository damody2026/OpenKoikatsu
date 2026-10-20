"""Normalize an immutable Tripo source to its catalog envelope before remesh."""
import argparse,json,sys,hashlib
from pathlib import Path
import bpy,bmesh
from mathutils import Vector,Matrix
from mathutils.bvhtree import BVHTree
from mathutils.geometry import barycentric_transform
p=argparse.ArgumentParser();p.add_argument('--id',required=True);p.add_argument('--source',type=Path,required=True);p.add_argument('--yaw-deg',type=float,default=0);p.add_argument('--solid-source',type=float,default=0)
p.add_argument('--catalog',type=Path);p.add_argument('--output',type=Path)
p.add_argument('--lod-triangles',type=int,default=0)
p.add_argument('--align-footprint',action='store_true',help='Align the dominant horizontal footprint axis before metric scaling; optional for catalogue merchandise')
a=p.parse_args(sys.argv[sys.argv.index('--')+1:])
root=Path(__file__).resolve().parents[1];base=root/'output/street-social-city-production/S01-modular-components-v1'
entry=next(x for x in json.loads((a.catalog or base/'components.json').read_text('utf-8'))['components'] if x['id']==a.id)
folder=a.output or base/'generated-components'/a.id;folder.mkdir(parents=True,exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=str(a.source.resolve()))
bpy.context.view_layer.update();meshes=[o for o in bpy.context.scene.objects if o.type=='MESH']
import math
rotation=Matrix.Rotation(math.radians(a.yaw_deg),4,'Z')
pts=[rotation@o.matrix_world@v.co for o in meshes for v in o.data.vertices]
lo=Vector(tuple(min(v[k] for v in pts) for k in range(3)));hi=Vector(tuple(max(v[k] for v in pts) for k in range(3)))
if a.align_footprint and max(entry['size'][:2])/min(entry['size'][:2])>1.4:
    original=hi-lo
    if (original.x>original.y)!=(entry['size'][0]>entry['size'][1]):
        a.yaw_deg+=90;rotation=Matrix.Rotation(math.radians(a.yaw_deg),4,'Z')
        pts=[rotation@o.matrix_world@v.co for o in meshes for v in o.data.vertices]
        lo=Vector(tuple(min(v[k] for v in pts) for k in range(3)));hi=Vector(tuple(max(v[k] for v in pts) for k in range(3)))
target=Vector(entry['size']);scale=Vector(tuple(target[k]/(hi[k]-lo[k]) for k in range(3)))
origin=Vector(((lo.x+hi.x)/2,(lo.y+hi.y)/2,lo.z));cleaning=[]
for o in meshes:
    world=rotation@o.matrix_world.copy();o.parent=None;o.matrix_world.identity()
    for v in o.data.vertices:
        q=world@v.co-origin;v.co=Vector(tuple(q[k]*scale[k] for k in range(3)))
    bm=bmesh.new();bm.from_mesh(o.data);bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=1e-6);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(o.data);bm.free()
    if a.solid_source:
        original=o.data;original.calc_loop_triangles();ov=[v.co.copy() for v in original.vertices];ot=[tuple(t.vertices) for t in original.loop_triangles];ol=[tuple(t.loops) for t in original.loop_triangles];om=[t.material_index for t in original.loop_triangles];materials=list(original.materials)
        ouv=[Vector((*q.uv,0)) for q in original.uv_layers.active.data];ob=BVHTree.FromPolygons(ov,ot,all_triangles=True)
        bpy.context.view_layer.objects.active=o
        mod=o.modifiers.new('Repair AI self-overlap and microscopic surface gaps','REMESH');mod.mode='VOXEL';mod.voxel_size=a.solid_source;mod.use_remove_disconnected=False;bpy.ops.object.modifier_apply(modifier=mod.name)
        o.data.calc_loop_triangles();budget=max(len(ot),3000)
        if len(o.data.loop_triangles)>budget:
            mod=o.modifiers.new('Manufacturing source density','DECIMATE');mod.ratio=budget/len(o.data.loop_triangles);bpy.ops.object.modifier_apply(modifier=mod.name)
        nv=[v.co.copy() for v in o.data.vertices];nlo=Vector(tuple(min(v[k] for v in nv) for k in range(3)));nhi=Vector(tuple(max(v[k] for v in nv) for k in range(3)))
        for v in o.data.vertices:v.co=Vector(tuple(-target[k]/2+(v.co[k]-nlo[k])*target[k]/(nhi[k]-nlo[k]) if k<2 else (v.co[k]-nlo[k])*target[k]/(nhi[k]-nlo[k]) for k in range(3)))
        o.data.materials.clear()
        for mat in materials:o.data.materials.append(mat)
        uv=o.data.uv_layers.active or o.data.uv_layers.new(name='Recovered Tripo material UV')
        for face in o.data.polygons:
            q=sum((o.data.vertices[i].co for i in face.vertices),Vector())/len(face.vertices);ti=ob.find_nearest(q)[2];face.material_index=om[ti]
            for li in face.loop_indices:
                co=o.data.vertices[o.data.loops[li].vertex_index].co;ids=ot[ti];ls=ol[ti];value=barycentric_transform(co,ov[ids[0]],ov[ids[1]],ov[ids[2]],ouv[ls[0]],ouv[ls[1]],ouv[ls[2]]);uv.data[li].uv=(value.x,value.y)
        o.data.calc_loop_triangles();nv=[v.co.copy() for v in o.data.vertices];nt=[tuple(t.vertices) for t in o.data.loop_triangles];nb=BVHTree.FromPolygons(nv,nt,all_triangles=True)
        cleaning.append(dict(voxel_m=a.solid_source,original_triangles=len(ot),manufacturing_source_triangles=len(nt),raw_to_clean_max_m=max(nb.find_nearest(q)[3] for q in ov),clean_to_raw_max_m=max(ob.find_nearest(q)[3] for q in nv),reason='Remove AI self-overlap/internal surfaces and close microscopic cracks before mandatory external remesh'))
    if a.lod_triangles:
        o.data.calc_loop_triangles();before_count=len(o.data.loop_triangles)
        if before_count>a.lod_triangles:
            original_vertices=[v.co.copy() for v in o.data.vertices];original_triangles=[tuple(t.vertices) for t in o.data.loop_triangles];original_bvh=BVHTree.FromPolygons(original_vertices,original_triangles,all_triangles=True)
            source_mesh=o.data.copy();source_mesh.use_fake_user=True
            limits=[(min(v[k] for v in original_vertices),max(v[k] for v in original_vertices)) for k in range(3)]
            protected={i for i,v in enumerate(original_vertices) if any(min(abs(v[k]-lo),abs(v[k]-hi))<1e-6 for k,(lo,hi) in enumerate(limits))}
            adjacency={i:set() for i in range(len(original_vertices))}
            for edge in source_mesh.edges:
                u,v=edge.vertices;adjacency[u].add(v);adjacency[v].add(u)
            for attempt in range(8):
                o.data=source_mesh.copy()
                for group in list(o.vertex_groups):o.vertex_groups.remove(group)
                group=o.vertex_groups.new(name='Simplify away from measured thin features');group.add(list(range(len(original_vertices))),1,'REPLACE');group.add(list(protected),0,'REPLACE')
                bpy.context.view_layer.objects.active=o;mod=o.modifiers.new('Measured reusable asset LOD','DECIMATE');mod.ratio=a.lod_triangles/before_count;mod.use_collapse_triangulate=True;mod.vertex_group=group.name;mod.vertex_group_factor=1000;bpy.ops.object.modifier_apply(modifier=mod.name)
                o.data.calc_loop_triangles();result_bvh=BVHTree.FromPolygons([v.co.copy() for v in o.data.vertices],[tuple(t.vertices) for t in o.data.loop_triangles],all_triangles=True)
                distances=[result_bvh.find_nearest(v)[3] for v in original_vertices]
                deviation=max(max(distances),max(original_bvh.find_nearest(v.co)[3] for v in o.data.vertices))
                if deviation<=.005:break
                bad={i for i,distance in enumerate(distances) if distance>.0035}
                for _ in range(2):bad|={j for i in list(bad) for j in adjacency[i]}
                protected|=bad
            assert deviation<=.005,(a.id,'LOD lost >5mm feature',deviation)
            cleaning.append(dict(operation='budgeted source LOD before mandatory remesh',before_triangles=before_count,after_triangles=len(o.data.loop_triangles),maximum_surface_deviation_m=deviation))
    o['source_route']='Tripo Model multi-view';o['source_sha256']=hashlib.sha256(a.source.read_bytes()).hexdigest();o['target_size_m']=list(target)
    for mat in o.data.materials:
        if not mat or not mat.use_nodes:continue
        for n in mat.node_tree.nodes:
            if n.type=='TEX_IMAGE' and n.image:
                if max(n.image.size)>2048:n.image.scale(min(2048,n.image.size[0]),min(2048,n.image.size[1]))
                n.image.pack()
bpy.ops.export_scene.gltf(filepath=str(folder/'normalized-source.glb'),export_format='GLB',export_extras=True)
bpy.ops.wm.save_as_mainfile(filepath=str(folder/'normalized-source.blend'))
report=dict(id=a.id,original_source=str(a.source.resolve()),source_sha256=hashlib.sha256(a.source.read_bytes()).hexdigest(),canonical_yaw_deg=a.yaw_deg,original_size=list(hi-lo),normalized_size=list(target),scale=list(scale),source_surface_repairs=cleaning,status='normalized_before_remesh',route='Tripo Model')
(folder/'source.json').write_text(json.dumps(report,indent=2),'utf-8')
