"""Reimport exported remesh assets, verify metric dimensions, render review library.

This is an asset library, NOT an accepted assembled floor.
"""
import bpy,bmesh,json,math
from pathlib import Path
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'output/street-social-city-production/S01-modular-components-v1/local-components'
sources=json.loads((OUT/'sources.json').read_text('utf-8'))
sources.append(dict(id='bookshelf',size_target_m=[1,.6,2],triangle_cap=800,source=str(OUT.parent/'board-bookshelf/bookshelf-boards.glb')))
bpy.ops.wm.read_factory_settings(use_empty=True)
records=[];groups=[]
for entry in sources:
    aid=entry['id'];folder=OUT/aid/'remesh' if aid!='bookshelf' else OUT.parent/'board-bookshelf/remesh-box'
    report=json.loads((folder/'remesh-report.json').read_text('utf-8'))
    assert report['status']=='passed',aid
    old=set(bpy.data.objects);bpy.ops.import_scene.gltf(filepath=str(folder/'remeshed-candidate.glb'))
    objs=list(set(bpy.data.objects)-old);meshes=[o for o in objs if o.type=='MESH']
    bpy.context.view_layer.update()
    pts=[o.matrix_world@v.co for o in meshes for v in o.data.vertices]
    lo=Vector(tuple(min(v[k] for v in pts) for k in range(3)));hi=Vector(tuple(max(v[k] for v in pts) for k in range(3)));size=hi-lo
    assert max(abs(x-y) for x,y in zip(size,entry['size_target_m']))<.00002,(aid,list(size))
    triangles=0
    for o in meshes:
        o.data.calc_loop_triangles();triangles+=len(o.data.loop_triangles)
        bm=bmesh.new();bm.from_mesh(o.data);bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=1e-6)
        assert all(e.is_manifold for e in bm.edges),(aid,o.name,'exported topology open')
        assert bm.calc_volume()>0,(aid,o.name,'inverted volume')
        bm.free()
        assert len(o.data.materials)>0,(aid,o.name,'missing material')
        assert o.data.uv_layers.active is not None,(aid,o.name,'missing UV')
    assert triangles<=entry['triangle_cap'],(aid,triangles)
    assert triangles==report['triangles'],(aid,'exported face count mismatch')
    images={n.image.name: list(n.image.size) for o in meshes for m in o.data.materials if m and m.use_nodes for n in m.node_tree.nodes if n.type=='TEX_IMAGE' and n.image}
    assert all(max(s)<=2048 for s in images.values()),(aid,'texture exceeds 2K')
    group=bpy.data.objects.new(aid,None);bpy.context.collection.objects.link(group)
    for o in objs:
        if o.parent not in objs:o.parent=group
    groups.append((group,lo,hi))
    records.append(dict(id=aid,status='passed_export_reimport',size_m=list(size),triangles=triangles,cap=entry['triangle_cap'],parts=len(meshes),images=images,path=str(folder/'remeshed-candidate.glb')))
    report['adopted']=True;report['adoption_scope']='verified component library only; floor assembly pending curved assets'
    (folder/'remesh-report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),'utf-8')
# Store editable library at true metre scale; no proxy replacements for missing assets.
for i,(g,lo,hi) in enumerate(groups):g.location=(i%4*25,i//4*25,0)
bpy.context.scene.unit_settings.system='METRIC'
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'component-library.blend'))
# A separate normalized contact sheet is explicitly for visual review only.
for i,(g,lo,hi) in enumerate(groups):
    s=2.8/max(hi-lo);g.scale=(s,s,s);g.location=(i%4*4-(lo.x+hi.x)*s/2,-i//4*4-(lo.y+hi.y)*s/2,-lo.z*s)
    bpy.ops.object.text_add(location=(i%4*4-1.4,-i//4*4-1.8,0));t=bpy.context.object;t.data.body=records[i]['id']+' / '+str(records[i]['triangles'])+' tris';t.data.size=.22
    labelmat=bpy.data.materials.get('Label ink') or bpy.data.materials.new('Label ink');labelmat.diffuse_color=(.025,.035,.035,1);t.data.materials.append(labelmat)
scene=bpy.context.scene;scene.render.engine='CYCLES';scene.cycles.samples=24
scene.render.resolution_x=1800;scene.render.resolution_y=1600;scene.render.resolution_percentage=100
scene.world=bpy.data.worlds.new('Studio');scene.world.use_nodes=True;scene.world.node_tree.nodes['Background'].inputs[0].default_value=(.65,.65,.65,1)
target=Vector((6,-6,0))
for loc,power in [((0,-4,14),2200),((13,-8,12),1600)]:
    bpy.ops.object.light_add(type='AREA',location=loc);o=bpy.context.object;o.data.energy=power;o.data.size=10;o.rotation_euler=(target-o.location).to_track_quat('-Z','Y').to_euler()
bpy.ops.object.camera_add(location=(19,-26,30));o=bpy.context.object;o.rotation_euler=(target-o.location).to_track_quat('-Z','Y').to_euler();o.data.type='ORTHO';o.data.ortho_scale=24;scene.camera=o
scene.render.filepath=str(OUT/'component-review.png');bpy.ops.render.render(write_still=True)
result=dict(status='component_geometry_checks_passed_floor_incomplete',verified=len(records),pending=['chair','toilet','sink'],records=records,limitations=['Visual review required separately','No complete floor assembly or walking/collision acceptance yet','Door leaf has no curved handle; pending hardware detail','Library contact sheet uses normalized preview scale; library blend uses actual metres'])
(OUT/'acceptance.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),'utf-8')
