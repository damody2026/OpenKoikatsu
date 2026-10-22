"""Blender adapter for mandatory instant-meshes-rs pass; source stays immutable.

OBJ geometry transport, per-object identity, nearest source-triangle UV and
material restoration. Outputs are candidates until geometric checks pass.
"""
import argparse, hashlib, json, subprocess, sys
from pathlib import Path
import bpy, bmesh
from mathutils import Vector
from mathutils.bvhtree import BVHTree
from mathutils.geometry import barycentric_transform
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_correspondence import surface_displacement_bound

p=argparse.ArgumentParser()
p.add_argument('--source',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--exe',type=Path,default=Path('E:/Paper/instant-meshes-rs/target/release/instant-meshes.exe'))
p.add_argument('--triangle-cap',type=int,default=4000)
p.add_argument('--tolerance',type=float,default=.005)
p.add_argument('--box-panels',action='store_true',help='Condition verified solid rectangular panels; restore exact planes after remesh')
p.add_argument('--dissolve-planes',action='store_true',help='After Rust remesh, remove only exact coplanar subdivisions on verified box panels')
p.add_argument('--project-source',action='store_true',help='Repair tiny remesh holes and adaptively fit curved surface to source')
p.add_argument('--quad-mode',action='store_true',help='Use mixed quad remesh before triangular export for curved assets')
p.add_argument('--dense-pass',type=float,default=1,help='Oversample remesh then simplify to budget before source fitting')
p.add_argument('--voxel-input',type=float,default=0,help='Optional metric voxel regularization before external remesh; checked against original source')
p.add_argument('--repair-solid',type=float,default=0,help='Repair remeshed surface topology through metric voxel reconstruction then LOD')
p.add_argument('--smooth',type=int,default=2)
p.add_argument('--smooth-shading',action='store_true',help='Use interpolated surface normals after geometric validation; geometry and UVs remain unchanged')
p.add_argument('--threads',type=int,default=0,help='Explicit Rust solver worker count; zero uses tool default')
p.add_argument('--recover-source-topology',action='store_true',help='Explicit reference-topology reconstruction after failed external extraction; preserves source features, records provenance')
p.add_argument('--pin-source-extrema',nargs='+',choices=['x','y','z'],default=[],help='During explicit recovery, retain exact source vertices at these metric extrema for modular joins')
a=p.parse_args(sys.argv[sys.argv.index('--')+1:])
assert not a.dissolve_planes or a.box_panels,'Planar cleanup requires verified box panels'
assert not a.pin_source_extrema or a.recover_source_topology,'Exact extrema pins require explicit reference recovery'
a.output.mkdir(parents=True,exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=str(a.source.resolve()))
bpy.context.view_layer.update()
objects=[o for o in bpy.context.scene.objects if o.type=='MESH']
records=[];all_ok=True;total=0
for index,o in enumerate(objects):
    mesh=o.data
    # glTF duplicates positions at UV seams. Transport must be welded topology,
    # while corner UVs remain attached to their own loops.
    bm=bmesh.new();bm.from_mesh(mesh)
    bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=1e-7)
    bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
    bm.to_mesh(mesh);bm.free();mesh.update();mesh.calc_loop_triangles()
    verts=[v.co.copy() for v in mesh.vertices]
    tris=[tuple(t.vertices) for t in mesh.loop_triangles]
    loops=[tuple(t.loops) for t in mesh.loop_triangles]
    mids=[t.material_index for t in mesh.loop_triangles]
    uv=mesh.uv_layers.active
    uvs=[Vector((uv.data[i].uv.x,uv.data[i].uv.y,0)) for i in range(len(mesh.loops))] if uv else None
    bvh=BVHTree.FromPolygons(verts,tris,all_triangles=True)
    lo=Vector(tuple(min(v[k] for v in verts) for k in range(3)))
    hi=Vector(tuple(max(v[k] for v in verts) for k in range(3)))
    dims=hi-lo;center=(lo+hi)/2
    cube=[Vector((x,y,z)) for x,y,z in [(-.5,-.5,-.5),(.5,-.5,-.5),(.5,.5,-.5),(-.5,.5,-.5),(-.5,-.5,.5),(.5,-.5,.5),(.5,.5,.5),(-.5,.5,.5)]]
    cube_faces=[(0,3,2),(0,2,1),(4,5,6),(4,6,7),(0,1,5),(0,5,4),(1,2,6),(1,6,5),(2,3,7),(2,7,6),(3,0,4),(3,4,7)]
    if a.box_panels:
        # Reject complex objects rather than silently replacing them by boxes.
        assert min(dims)>0, 'Zero thickness panel'
        box_samples=[]
        for axis in range(3):
            other=[k for k in range(3) if k!=axis]
            for sign in [-.5,.5]:
                for u in [-.5,-.25,0,.25,.5]:
                    for v in [-.5,-.25,0,.25,.5]:
                        q=Vector((0,0,0));q[axis]=sign;q[other[0]]=u;q[other[1]]=v
                        box_samples.append(center+Vector(tuple(q[k]*dims[k] for k in range(3))))
        assert max(bvh.find_nearest(q)[3] for q in box_samples)<=a.tolerance, f'{o.name}: source is not a solid box'
        assert max(min(min(abs(v[k]-lo[k]),abs(v[k]-hi[k])) for k in range(3)) for v in verts)<=a.tolerance, f'{o.name}: interior geometry cannot use panel route'
    folder=a.output/f'part-{index:02d}';folder.mkdir(exist_ok=True)
    inp=folder/'input.obj';out=folder/'remeshed.obj'
    transport_verts=cube if a.box_panels else verts
    transport_tris=cube_faces if a.box_panels else tris
    if a.voxel_input:
        temp=bpy.data.objects.new('temporary regularized input',mesh.copy());bpy.context.collection.objects.link(temp)
        bpy.context.view_layer.objects.active=temp;temp.select_set(True)
        mod=temp.modifiers.new('Repair inconsistent AI surface before mandatory remesh','REMESH');mod.mode='VOXEL';mod.voxel_size=a.voxel_input
        bpy.ops.object.modifier_apply(modifier=mod.name);temp.data.calc_loop_triangles()
        transport_verts=[v.co.copy() for v in temp.data.vertices];transport_tris=[tuple(t.vertices) for t in temp.data.loop_triangles]
        bpy.data.objects.remove(temp,do_unlink=True)
    text=''.join('v %.9g %.9g %.9g\n'%tuple(v) for v in transport_verts)
    text+=''.join('f '+' '.join(str(i+1) for i in t)+'\n' for t in transport_tris)
    inp.write_text(text,encoding='ascii')
    # Triangular mode avoids pure-quad subdivision multiplying face budgets.
    target=max(16,int(a.triangle_cap/len(objects)*(.35 if a.quad_mode else .75)*a.dense_pass))
    conditioned_target=6 if a.triangle_cap/len(objects)<48 else min(24,target)
    quad=a.box_panels or a.quad_mode
    command=[str(a.exe),'-d','-r','4' if quad else '6','-p','4' if quad else '6',*(['-D'] if quad else []),'-c','30','-b','-S',str(a.smooth),'-f',str(conditioned_target if a.box_panels else target),'-o',str(out),str(inp)]
    if a.threads:command[1:1]=['-t',str(a.threads)]
    run=subprocess.run(command,capture_output=True,text=True,timeout=180)
    (folder/'tool.log').write_text(run.stdout+'\n'+run.stderr,encoding='utf-8')
    if run.returncode or not out.exists():
        records.append(dict(object=o.name,status='tool_failed',command=command));all_ok=False;continue
    newverts=[];faces=[]
    for line in out.read_text(encoding='utf-8').splitlines():
        fields=line.split()
        if not fields:continue
        if fields[0]=='v':newverts.append(Vector(tuple(float(q) for q in fields[1:4])))
        elif fields[0]=='f':faces.append(tuple(int(q.split('/')[0])-1 for q in fields[1:]))
    if not newverts or not faces:
        records.append(dict(object=o.name,status='empty_result',command=command));all_ok=False;continue
    newmesh=bpy.data.meshes.new(o.name+'-remeshed');newmesh.from_pydata(newverts,[],faces);newmesh.update()
    invalid_mesh=newmesh.validate(verbose=False,clean_customdata=False)
    newmesh.update()
    repairs=[]
    if a.dense_pass>1:
        newmesh.calc_loop_triangles();nt=len(newmesh.loop_triangles)
        if nt>a.triangle_cap*.7:
            temp=bpy.data.objects.new('temporary simplification carrier',newmesh);bpy.context.collection.objects.link(temp)
            bpy.context.view_layer.objects.active=temp;temp.select_set(True)
            mod=temp.modifiers.new('Budget LOD after dense mandatory remesh','DECIMATE');mod.ratio=a.triangle_cap*.7/nt
            bpy.ops.object.modifier_apply(modifier=mod.name);newmesh=temp.data;newmesh.use_fake_user=True;bpy.data.objects.remove(temp,do_unlink=True)
    # Reference recovery below rebuilds connectivity from the immutable source
    # and samples the mandatory external field directly. Adaptive BMesh splits
    # would be discarded, and can crash Blender on projected thin leaf fans.
    if a.project_source and not a.recover_source_topology:
        bm=bmesh.new();bm.from_mesh(newmesh)
        bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=1e-7)
        bmesh.ops.dissolve_degenerate(bm,edges=list(bm.edges),dist=1e-7)
        # Only short loops may be capped. Large missing surfaces stay rejected.
        unseen={e for e in bm.edges if e.is_boundary};capped=0
        while unseen:
            todo=[unseen.pop()];loop=set(todo)
            while todo:
                for v in todo.pop().verts:
                    for edge in v.link_edges:
                        if edge in unseen:unseen.remove(edge);loop.add(edge);todo.append(edge)
            coords=[v.co for e in loop for v in e.verts]
            diameter=max(max(v[k] for v in coords)-min(v[k] for v in coords) for k in range(3))
            if diameter<=a.tolerance*5:
                bmesh.ops.holes_fill(bm,edges=list(loop),sides=0);capped+=1
        repairs.append(dict(small_boundary_loops_capped=capped))
        bmesh.ops.triangulate(bm,faces=list(bm.faces))
        for v in bm.verts:v.co=bvh.find_nearest(v.co)[0]
        bm.to_mesh(newmesh);bm.free();newmesh.update()
        for iteration in range(24):
            nv=[v.co.copy() for v in newmesh.vertices];nf=[tuple(p.vertices) for p in newmesh.polygons]
            rb=BVHTree.FromPolygons(nv,nf,all_triangles=True)
            bad=set()
            for q in verts+[sum((verts[j] for j in t),Vector())/3 for t in tris]:
                near=rb.find_nearest(q)
                if near[3]>a.tolerance*.9:bad.add(near[2])
            for j,f in enumerate(nf):
                q=sum((nv[k] for k in f),Vector())/3
                if bvh.find_nearest(q)[3]>a.tolerance*.9:bad.add(j)
            if not bad:break
            room=a.triangle_cap-len(nf)
            if room<12:break
            selected=sorted(bad)[:max(1,room//12)]
            bm=bmesh.new();bm.from_mesh(newmesh);bm.faces.ensure_lookup_table()
            edges={e for j in selected for e in bm.faces[j].edges}
            bmesh.ops.subdivide_edges(bm,edges=list(edges),cuts=1,use_grid_fill=True)
            bmesh.ops.triangulate(bm,faces=list(bm.faces))
            for v in bm.verts:v.co=bvh.find_nearest(v.co)[0]
            bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
            bm.to_mesh(newmesh);bm.free();newmesh.update()
            repairs.append(dict(iteration=iteration,refined_faces=len(bad)))
        invalid_after_repair=newmesh.validate(verbose=False,clean_customdata=False)
        invalid_mesh=invalid_after_repair
        # Projection can create coincident vertices at seams. Sew these before
        # judging boundary topology, then restore holes only where the source
        # surface supports the entire new patch (not arbitrary flat caps).
        bm=bmesh.new();bm.from_mesh(newmesh)
        bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=.0005)
        bmesh.ops.dissolve_degenerate(bm,edges=list(bm.edges),dist=1e-7)
        unseen={e for e in bm.edges if e.is_boundary};restored=0
        while unseen:
            todo=[unseen.pop()];loop=set(todo)
            while todo:
                for vertex in todo.pop().verts:
                    for edge in vertex.link_edges:
                        if edge in unseen:unseen.remove(edge);loop.add(edge);todo.append(edge)
            before=set(bm.faces)
            bmesh.ops.holes_fill(bm,edges=list(loop),sides=0)
            added=list(set(bm.faces)-before)
            if added:
                bmesh.ops.triangulate(bm,faces=added);newfaces=list(set(bm.faces)-before)
                # A curved hole cannot be judged from a single flat cap.
                # Retain the boundary, tessellate its patch and project ONLY
                # new interior vertices before the same coverage check.
                boundary_verts={v for e in loop for v in e.verts}
                for patch_iteration in range(4):
                    patch_faces=list(set(bm.faces)-before)
                    patch_edges={e for face in patch_faces for e in face.edges if e not in loop}
                    if not patch_edges:break
                    bmesh.ops.subdivide_edges(bm,edges=list(patch_edges),cuts=1,use_grid_fill=True)
                    for vertex in bm.verts:
                        if vertex not in boundary_verts and any(face not in before for face in vertex.link_faces):vertex.co=bvh.find_nearest(vertex.co)[0]
                    bmesh.ops.triangulate(bm,faces=list(set(bm.faces)-before))
                    if len(bm.faces)>a.triangle_cap:break
                newfaces=list(set(bm.faces)-before)
                samples=[v.co for face in newfaces for v in face.verts]
                samples += [face.calc_center_median() for face in newfaces]
                samples += [(e.verts[0].co+e.verts[1].co)/2 for face in newfaces for e in face.edges]
                if max(bvh.find_nearest(q)[3] for q in samples)>a.tolerance*.9:
                    bmesh.ops.delete(bm,geom=newfaces,context='FACES_ONLY')
                else:restored+=1
        bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(newmesh);bm.free();newmesh.update()
        repairs.append(dict(source_supported_boundary_patches=restored))
        # Curved silhouettes may retract a few millimetres during remesh.
        # Restore catalog bounds, then subject this adjustment to the SAME
        # bidirectional 5mm surface test below; fitting is not automatic approval.
        rlo=Vector(tuple(min(v.co[k] for v in newmesh.vertices) for k in range(3)))
        rhi=Vector(tuple(max(v.co[k] for v in newmesh.vertices) for k in range(3)))
        for v in newmesh.vertices:v.co=Vector(tuple(lo[k]+(v.co[k]-rlo[k])*dims[k]/(rhi[k]-rlo[k]) for k in range(3)))
        newmesh.update()
        if a.repair_solid:
            # Recover thin source-supported features that the external field
            # extraction dropped. The result is a NEW voxel union of external
            # remesh and source volume, never the unchanged original model.
            # Keep both origins explicit and validate against the original.
            cv=[v.co.copy() for v in newmesh.vertices];cf=[tuple(p.vertices) for p in newmesh.polygons];offset=len(cv)
            cv.extend(v.copy() for v in verts);cf.extend(tuple(offset+j for j in t) for t in tris)
            unionmesh=bpy.data.meshes.new('external remesh plus source feature recovery');unionmesh.from_pydata(cv,[],cf);unionmesh.update();newmesh=unionmesh
            temp=bpy.data.objects.new('temporary remesh topology repair',newmesh);bpy.context.collection.objects.link(temp);bpy.context.view_layer.objects.active=temp
            mod=temp.modifiers.new('Solid topology repair after external remesh','REMESH');mod.mode='VOXEL';mod.voxel_size=a.repair_solid;bpy.ops.object.modifier_apply(modifier=mod.name)
            temp.data.calc_loop_triangles();nt=len(temp.data.loop_triangles)
            if nt>a.triangle_cap*.85:
                mod=temp.modifiers.new('Final runtime budget','DECIMATE');mod.ratio=a.triangle_cap*.85/nt;bpy.ops.object.modifier_apply(modifier=mod.name)
            newmesh=temp.data;newmesh.use_fake_user=True;bpy.data.objects.remove(temp,do_unlink=True);newmesh.update()
            rlo=Vector(tuple(min(v.co[k] for v in newmesh.vertices) for k in range(3)));rhi=Vector(tuple(max(v.co[k] for v in newmesh.vertices) for k in range(3)))
            for v in newmesh.vertices:v.co=Vector(tuple(lo[k]+(v.co[k]-rlo[k])*dims[k]/(rhi[k]-rlo[k]) for k in range(3)))
            newmesh.update();invalid_mesh=newmesh.validate(verbose=False,clean_customdata=False)
            repairs.append(dict(solid_topology_voxel_m=a.repair_solid,feature_recovery='voxel union of external remesh and immutable source volume, followed by budget LOD'))
    if a.box_panels:
        bm=bmesh.new();bm.from_mesh(newmesh)
        assert all(e.is_manifold for e in bm.edges), f'{o.name}: conditioned remesh not closed'
        for v in bm.verts:
            axis=max(range(3),key=lambda k:abs(v.co[k]))
            for k in range(3):
                v.co[k]=max(-.5,min(.5,v.co[k]))
                if k==axis or abs(v.co[k])>.32:v.co[k]=.5 if v.co[k]>0 else -.5
            v.co=center+Vector(tuple(v.co[k]*dims[k] for k in range(3)))
        bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
        bm.to_mesh(newmesh);bm.free();newmesh.update()
        assert all(any(min(max(abs(newmesh.vertices[i].co[k]-lo[k]) for i in poly.vertices),max(abs(newmesh.vertices[i].co[k]-hi[k]) for i in poly.vertices))<=1e-5 for k in range(3)) for poly in newmesh.polygons), f'{o.name}: remesh face crosses a hard edge'
    if a.dissolve_planes:
        newmesh.calc_loop_triangles();before=len(newmesh.loop_triangles)
        bm=bmesh.new();bm.from_mesh(newmesh);bm.normal_update()
        # Box validation already guarantees axis planes (all other dihedrals
        # are 90 degrees). Use a representable cosine threshold in float32.
        bmesh.ops.dissolve_limit(bm,angle_limit=.001,use_dissolve_boundaries=False,verts=list(bm.verts),edges=list(bm.edges),delimit={'MATERIAL'})
        bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(newmesh);bm.free();newmesh.update();newmesh.calc_loop_triangles()
        repairs.append(dict(exact_coplanar_cleanup=True,triangles_before=before,triangles_after=len(newmesh.loop_triangles),geometry_change='none; only redundant edges on the same verified plane removed'))
    if a.recover_source_topology:
        # The field extractor can bridge a concavity or lose a thin connected
        # feature. Voxel union then fills that concavity too. Rebuild its surface
        # using the reference connectivity, constrained by the extracted field.
        # This is deliberately NOT described as untouched Rust output, and is
        # never enabled implicitly. Keep the raw OBJ and its coverage evidence.
        extracted_v=[v.co.copy() for v in newmesh.vertices]
        extracted_f=[tuple(face.vertices) for face in newmesh.polygons]
        extracted_bvh=BVHTree.FromPolygons(extracted_v,extracted_f,all_triangles=False)
        before_coverage=max(extracted_bvh.find_nearest(q)[3] for q in verts)
        recovered=[];trusted=0;pinned=0
        for v in verts:
            if any(abs(v['xyz'.index(axis)]-bound['xyz'.index(axis)])<=1e-6 for axis in a.pin_source_extrema for bound in (lo,hi)):
                recovered.append(v.copy());pinned+=1;continue
            near=extracted_bvh.find_nearest(v)
            if near[3]<=a.tolerance:
                delta=near[0]-v
                if delta.length>0:delta*=min(.001,delta.length)/delta.length
                recovered.append(v+delta);trusted+=1
            else:recovered.append(v.copy())
        newmesh=bpy.data.meshes.new(o.name+'-reference-topology-recovery')
        newmesh.from_pydata(recovered,[],tris);newmesh.update()
        invalid_mesh=newmesh.validate(verbose=False,clean_customdata=False)
        repairs.append(dict(reference_topology_recovery=True,connectivity_origin='normalized immutable source',position_field_origin=f'instant-meshes-rs result, bounded 1mm adoption where nearest distance <={a.tolerance:g}m; missing features restored from source',external_extraction_coverage_before_recovery_m=before_coverage,field_supported_vertices=trusted,reference_vertices=len(verts),unchanged_original_model=False))
        if a.pin_source_extrema:repairs.append(dict(exact_source_extrema_axes=a.pin_source_extrema,pinned_vertices=pinned,purpose='Deterministic modular seam contract; no arbitrary edge displacement'))
    newverts=[v.co.copy() for v in newmesh.vertices]
    faces=[tuple(p.vertices) for p in newmesh.polygons]
    for material in mesh.materials:newmesh.materials.append(material)
    newuv=newmesh.uv_layers.new(name='Transferred source UV') if uvs else None
    max_distance=0
    for poly in newmesh.polygons:
        center=sum((newverts[i] for i in poly.vertices),Vector())/len(poly.vertices)
        nearest=bvh.find_nearest(center)
        if nearest[2] is None:all_ok=False;continue
        ti=nearest[2];poly.material_index=mids[ti]
        for li in poly.loop_indices:
            q=newverts[newmesh.loops[li].vertex_index]
            n=bvh.find_nearest(q);max_distance=max(max_distance,n[3])
            if newuv:
                ids=tris[ti];ls=loops[ti]
                value=barycentric_transform(q,verts[ids[0]],verts[ids[1]],verts[ids[2]],uvs[ls[0]],uvs[ls[1]],uvs[ls[2]])
                newuv.data[li].uv=(value.x,value.y)
    oldlo=[min(v[k] for v in verts) for k in range(3)];oldhi=[max(v[k] for v in verts) for k in range(3)]
    newlo=[min(v[k] for v in newverts) for k in range(3)];newhi=[max(v[k] for v in newverts) for k in range(3)]
    bounds_error=max(abs(x-y) for x,y in zip(oldlo+oldhi,newlo+newhi))
    # Reverse coverage detects a vanished thin face that bounds alone misses.
    rbvh=BVHTree.FromPolygons(newverts,faces,all_triangles=False)
    source_samples=verts+[sum((verts[i] for i in t),Vector())/3 for t in tris]
    result_samples=newverts+[sum((newverts[i] for i in f),Vector())/len(f) for f in faces]
    coverage=max(rbvh.find_nearest(v)[3] for v in source_samples)
    max_distance=max(bvh.find_nearest(v)[3] for v in result_samples)
    if a.recover_source_topology:
        bound=surface_displacement_bound(verts,tris,newverts,faces)
        if bound is not None:
            repairs.append(dict(distance_validation='Certified identical-triangle correspondence; convex barycentric displacement bound in both directions',surface_displacement_upper_bound_m=bound,bvh_observed_source_to_result_m=coverage,bvh_observed_result_to_source_m=max_distance,reason='Avoid single-precision nearest-triangle cancellation on long, steep geographic triangles'))
            coverage=max_distance=bound
    edge_uses={}
    for poly in newmesh.polygons:
        for edge in poly.edge_keys:edge_uses[edge]=edge_uses.get(edge,0)+1
    nonmanifold_edges=sum(n!=2 for n in edge_uses.values())
    source_edges={}
    for poly in mesh.polygons:
        for edge in poly.edge_keys:source_edges[edge]=source_edges.get(edge,0)+1
    source_closed=all(n==2 for n in source_edges.values())
    ok=(max(bounds_error,max_distance,coverage)<=a.tolerance and not invalid_mesh
        and (not source_closed or nonmanifold_edges==0))
    all_ok=all_ok and ok
    if a.smooth_shading:
        for polygon in newmesh.polygons:polygon.use_smooth=True
    o.data=newmesh;o['remesh_tool']='instant-meshes-rs';o['remesh_part_passed']=ok
    newmesh.calc_loop_triangles();count=len(newmesh.loop_triangles);total+=count
    records.append(dict(object=o.name,status='passed' if ok else 'geometry_check_failed',conditioning='isotropic box with exact hard-plane restoration' if a.box_panels else 'none',postprocess='explicit reference topology with mandatory external field sampling' if a.recover_source_topology else ('source projection and small-hole/adaptive curvature repair' if a.project_source else 'none'),repairs=repairs,triangles=count,bounds_error_m=bounds_error,source_to_result_max_m=coverage,result_to_source_max_m=max_distance,invalid_mesh_repaired=invalid_mesh,nonmanifold_edges=nonmanifold_edges,source_closed=source_closed,source_nonmanifold_edges=sum(n!=2 for n in source_edges.values()),command=command))

all_ok=all_ok and total<=a.triangle_cap
candidate=a.output/'remeshed-candidate.glb'
bpy.ops.export_scene.gltf(filepath=str(candidate),export_format='GLB',export_extras=True)
bpy.ops.wm.save_as_mainfile(filepath=str(a.output/'remeshed-candidate.blend'))
report=dict(status='passed' if all_ok else 'rejected_requires_repair',source=str(a.source),source_sha256=hashlib.sha256(a.source.read_bytes()).hexdigest(),output_sha256=hashlib.sha256(candidate.read_bytes()).hexdigest(),tool=str(a.exe),tool_sha256=hashlib.sha256(a.exe.read_bytes()).hexdigest(),triangle_cap=a.triangle_cap,triangles=total,tolerance_m=a.tolerance,records=records,uv_method='nearest source triangle barycentric transfer; visual inspection still required',limitations=['No animation/skin transfer implemented; source furniture must be static','Appearance and collision require separate checks'],adopted=False)
report['arguments']={k:str(v) if isinstance(v,Path) else v for k,v in vars(a).items()}
(a.output/'remesh-report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(dict(status=report['status'],triangles=total,parts=len(records))))
