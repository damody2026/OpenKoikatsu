"""Independently import final curved assets, measure and render at real scale."""
import bpy,bmesh,json,math,hashlib
from pathlib import Path
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[1];BASE=ROOT/'output/street-social-city-production/S01-modular-components-v1'
catalog=json.loads((BASE/'components.json').read_text('utf-8'));records=[]
bpy.ops.wm.read_factory_settings(use_empty=True)
for i,aid in enumerate(['chair','toilet','sink']):
    folder=BASE/'generated-components'/aid/'remesh-production';report=json.loads((folder/'remesh-report.json').read_text('utf-8'));assert report['status']=='passed'
    path=folder/'remeshed-candidate.glb';old=set(bpy.data.objects);bpy.ops.import_scene.gltf(filepath=str(path));objects=list(set(bpy.data.objects)-old);meshes=[o for o in objects if o.type=='MESH'];bpy.context.view_layer.update()
    pts=[o.matrix_world@v.co for o in meshes for v in o.data.vertices];size=[max(q[k] for q in pts)-min(q[k] for q in pts) for k in range(3)]
    target=next(c['size'] for c in catalog['components'] if c['id']==aid);assert max(abs(x-y) for x,y in zip(size,target))<.002
    triangles=0;nonmanifold=0;images={}
    for o in meshes:
        o.data.calc_loop_triangles();triangles+=len(o.data.loop_triangles)
        bm=bmesh.new();bm.from_mesh(o.data);bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=1e-6);nonmanifold+=sum(not e.is_manifold for e in bm.edges);bm.free()
        assert o.data.uv_layers.active is not None
        for mat in o.data.materials:
            assert mat is not None
            if mat.use_nodes:
                for node in mat.node_tree.nodes:
                    if node.type=='TEX_IMAGE' and node.image:images[node.image.name]=list(node.image.size)
        o.location.x+=i*1.1
    assert triangles==report['triangles'] and triangles<=report['triangle_cap'];assert images and all(max(s)<=2048 for s in images.values())
    records.append(dict(id=aid,status='passed_export_reimport',size_m=size,triangles=triangles,cap=report['triangle_cap'],nonmanifold_edges=nonmanifold,watertight=nonmanifold==0,images=images,path=str(path),sha256=hashlib.sha256(path.read_bytes()).hexdigest(),reference_topology_recovery=any(q.get('reference_topology_recovery') for r in report['records'] for q in r['repairs'])))
sc=bpy.context.scene;sc.render.engine='CYCLES';sc.cycles.samples=32;sc.render.resolution_x=1500;sc.render.resolution_y=850;sc.render.resolution_percentage=100
sc.world=bpy.data.worlds.new('studio');sc.world.use_nodes=True;sc.world.node_tree.nodes['Background'].inputs[0].default_value=(.55,.55,.55,1)
target=Vector((1.1,0,.45))
for loc,power in [((-2,-3,5),400),((3,2,4),350)]:
    bpy.ops.object.light_add(type='AREA',location=loc);o=bpy.context.object;o.data.energy=power;o.data.size=4;o.rotation_euler=(target-o.location).to_track_quat('-Z','Y').to_euler()
bpy.ops.object.camera_add(location=(3.5,-5,3.5));o=bpy.context.object;o.rotation_euler=(target-o.location).to_track_quat('-Z','Y').to_euler();o.data.type='ORTHO';o.data.ortho_scale=3.8;sc.camera=o
sc.render.filepath=str(BASE/'generated-components/components-review.png');bpy.ops.render.render(write_still=True)
(BASE/'generated-components/acceptance.json').write_text(json.dumps(dict(status='passed',records=records,limitations=['Chair/toilet retain source open seams; not certified watertight solids','Sanitary assets use documented reference-connectivity reconstruction after the mandatory Rust pass']),ensure_ascii=False,indent=2),'utf-8')
