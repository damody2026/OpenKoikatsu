"""Blender: audit the actual candidate mesh; this command NEVER approves it.

Clearance boxes are explicit design requirements, not inferred from asset names.
Triangle/box SAT plus a BVH centre containment test catches crossing and solid fill.
"""
import argparse
import hashlib
import json
import math
import sys
from pathlib import Path

import bmesh
import bpy
from mathutils import Vector
from mathutils.bvhtree import BVHTree

sys.path.insert(0, str(Path(__file__).resolve().parent))
from street_city_io import write_json, write_text
from street_city_mesh_checks import triangle_intersects_box, valid_uv_triangle

p = argparse.ArgumentParser()
p.add_argument('--input', type=Path, required=True)
p.add_argument('--out', type=Path, required=True)
p.add_argument('--clearances', type=Path)
args = p.parse_args(sys.argv[sys.argv.index('--')+1:])
source = args.input.resolve()
out = args.out.resolve()
clearances = json.loads(args.clearances.resolve().read_text('utf-8')) if args.clearances else []
if source.suffix.lower() == '.blend':
    bpy.ops.wm.open_mainfile(filepath=str(source))
else:
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    if source.suffix.lower() == '.glb':
        bpy.ops.import_scene.gltf(filepath=str(source))
    else:
        raise SystemExit('Audit a canonical BLEND or GLB, not an unoriented source')

records = []
all_points = []
all_triangles = []
all_polygons = []
errors = []
for obj in bpy.context.scene.objects:
    if obj.type != 'MESH':
        continue
    if obj.modifiers:
        errors.append(obj.name+': unapplied modifiers; inspected mesh must equal exported mesh')
    mesh = obj.data
    mesh.calc_loop_triangles()
    points = [tuple(obj.matrix_world @ v.co) for v in mesh.vertices]
    offset = len(all_points)
    all_points.extend(points)
    for tri in mesh.loop_triangles:
        indices = tuple(offset+i for i in tri.vertices)
        all_polygons.append(indices)
        all_triangles.append(tuple(all_points[i] for i in indices))
    bm = bmesh.new()
    bm.from_mesh(mesh)
    # glTF duplicates vertices along UV/material/normal seams. Inspect a separate
    # position-welded topology copy; never edit the source or smooth its normals.
    raw_boundary = sum(e.is_boundary for e in bm.edges)
    bmesh.ops.remove_doubles(bm, verts=list(bm.verts), dist=1e-7)
    unseen = set(bm.verts)
    components = []
    while unseen:
        pending = [unseen.pop()]
        count = 0
        while pending:
            v = pending.pop()
            count += 1
            for edge in v.link_edges:
                other = edge.other_vert(v)
                if other in unseen:
                    unseen.remove(other)
                    pending.append(other)
        components.append(count)
    stats = dict(name=obj.name, vertices=len(mesh.vertices), triangles=len(mesh.loop_triangles),
        topology_vertices_after_seam_weld=len(bm.verts), raw_boundary_edges=raw_boundary,
        topology_weld_tolerance_m=1e-7,
        boundary_edges=sum(e.is_boundary for e in bm.edges),
        nonmanifold_edges=sum(not e.is_manifold for e in bm.edges),
        inconsistent_winding_edges=sum(e.is_manifold and not e.is_contiguous for e in bm.edges),
        loose_vertices=sum(not v.link_faces for v in bm.verts),
        degenerate_faces=sum(f.calc_area() < 1e-12 for f in bm.faces),
        components=len(components), component_vertex_counts=sorted(components, reverse=True),
        nonfinite_vertices=sum(not all(math.isfinite(x) for x in v) for v in points),
        nonfinite_normals=sum(not all(math.isfinite(x) for x in f.normal) for f in bm.faces),
        uv_layers=len(mesh.uv_layers), material_slots=len(mesh.materials))
    bm.free()
    stats['uv_faces_valid'] = False
    if mesh.uv_layers:
        uv = mesh.uv_layers.active.data
        valid = []
        for tri in mesh.loop_triangles:
            a, b, c = [uv[i].uv for i in tri.loops]
            # Tiny real triangles can have tiny but valid UVs. Detect collapsed
            # mappings relative to physical triangle area, not an absolute UV cutoff.
            valid.append(valid_uv_triangle((a,b,c),tri.area))
        stats['uv_faces_valid'] = bool(valid) and all(valid)
        stats['invalid_uv_triangles'] = sum(not x for x in valid)
    materials = []
    for material in mesh.materials:
        nodes = material.node_tree.nodes if material and material.use_nodes else []
        textures = [n.image for n in nodes if n.type == 'TEX_IMAGE' and n.image]
        outputs = [n for n in nodes if n.type == 'OUTPUT_MATERIAL' and n.is_active_output]
        shaders = [link.from_node for n in outputs for link in n.inputs['Surface'].links if link.from_node.type == 'BSDF_PRINCIPLED']
        bound_images = [link.from_node.image for n in shaders for link in n.inputs['Base Color'].links
                        if link.from_node.type == 'TEX_IMAGE' and link.from_node.image]
        bound_valid = bool(bound_images) and all(min(i.size)>0 and (i.packed_file or Path(bpy.path.abspath(i.filepath)).is_file()) for i in bound_images)
        materials.append(dict(name=material.name if material else None,
            principled=any(n.type == 'BSDF_PRINCIPLED' for n in nodes),
            base_color_image_bound_and_available=bound_valid,
            image_textures=[dict(name=i.name, size=list(i.size), packed=bool(i.packed_file)) for i in textures]))
    stats['materials'] = materials
    stats['material_faces_valid'] = bool(materials) and all(
        poly.material_index < len(materials) and materials[poly.material_index]['principled']
        and materials[poly.material_index]['base_color_image_bound_and_available'] for poly in mesh.polygons)
    for key in ('nonmanifold_edges', 'inconsistent_winding_edges', 'loose_vertices',
                'degenerate_faces', 'nonfinite_vertices', 'nonfinite_normals'):
        if stats[key]:
            errors.append(f'{obj.name}: {key}={stats[key]}')
    if not stats['uv_faces_valid']:
        errors.append(obj.name+': missing or degenerate UV coordinates')
    if not stats['material_faces_valid']:
        errors.append(obj.name+': every face needs a Principled material and real image texture')
    records.append(stats)

if not all_points:
    raise SystemExit('No actual mesh to inspect')
tree = BVHTree.FromPolygons(all_points, all_polygons, all_triangles=True)
clearance_results = []
for box in clearances:
    hits = sum(triangle_intersects_box(tri, box['center_m'], box['size_m']) for tri in all_triangles)
    origin = Vector(box['center_m'])
    direction = Vector((1, .37139, .11783)).normalized()
    crossings = 0
    # Closed-solid containment: surface-only overlap misses a box inside solid furniture.
    for _ in range(len(all_triangles)+1):
        point, normal, face, distance = tree.ray_cast(origin, direction)
        if point is None:
            break
        crossings += 1
        origin = point+direction*1e-6
    else:
        errors.append(box['id']+': containment ray did not terminate')
    filled = crossings % 2 == 1
    result = dict(box, intersecting_triangles=hits, center_inside_solid=filled,
                  passed=hits == 0 and not filled)
    clearance_results.append(result)
    if not result['passed']:
        errors.append(box['id']+': required usable volume intersects actual mesh')

report = dict(schema=1, status='candidate_checks_passed_pending_visual_review' if not errors else 'candidate_checks_failed',
    approved=False, source_file=str(source), source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
    mesh_objects=records, clearances=clearance_results, errors=errors,
    limitations=['No approval is issued.', 'Self-intersection and shading still require visual review.',
                 'Clearance boxes must come from a reviewed interaction design; they do not prove runtime collision.'])
out.mkdir(parents=True, exist_ok=True)
write_json(out/'mesh-audit.json', report)
lines = ['# 真實候選網格檢查', '', '狀態：'+report['status'], '',
         '本工具不批准模型，不代替實際遊戲碰撞與視覺驗收。', '',
         '| 網格 | 三角形 | 非流形邊 | 繞序錯誤 | UV有效 | 材質有效 |', '|---|---:|---:|---:|---|---|']
for r in records:
    lines.append(f"|{r['name']}|{r['triangles']}|{r['nonmanifold_edges']}|{r['inconsistent_winding_edges']}|{r['uv_faces_valid']}|{r['material_faces_valid']}|")
lines += ['', '## 待修項目', '']+[f'- {e}' for e in errors]
write_text(out/'mesh-audit.md', '\n'.join(lines)+'\n')
print(json.dumps(dict(status=report['status'], errors=errors, clearances=clearance_results)))
if errors:
    raise SystemExit(1)
