"""Offline geometry-only repair. UE retains skeletons, weights and animation.

Run: blender --background --factory-startup --python this.py -- in.json out.json
The exchange contains triangle corners [position, UV channels]. No rig roundtrip.
"""
import bpy
import bmesh
import json
import sys
from pathlib import Path
from mathutils import Vector
from mathutils.kdtree import KDTree


def simplify_micro_islands(triangles, budget):
    """Replace sub-0.5 cm disconnected beads with authored planar triangles.

    A closed island has a topological collapse floor even when the camera sees
    only a few pixels. UE still checks bidirectional 0.25 cm surface distance
    and seven animation poses after importing this bounded approximation.
    """
    parent, corner_at = {}, {}

    def root(key):
        parent.setdefault(key, key)
        while parent[key] != key:
            parent[key] = parent[parent[key]]
            key = parent[key]
        return key

    for triangle in triangles:
        keys = [tuple(c[:3]) for c in triangle]
        for key, corner in zip(keys, triangle):
            corner_at.setdefault(key, corner)
        for key in keys[1:]:
            parent[root(key)] = root(keys[0])
    islands, faces = {}, {}
    for key in parent:
        islands.setdefault(root(key), []).append(key)
    for triangle in triangles:
        faces.setdefault(root(tuple(triangle[0][:3])), []).append(triangle)
    if len(islands) < 2 or budget >= len(triangles):
        return triangles
    output, replaced = [], 0
    for key, vertices in islands.items():
        diagonal = Vector([max(p[i] for p in vertices) - min(p[i] for p in vertices) for i in range(3)]).length
        if diagonal > 0.5 or len(vertices) < 3:
            output.extend(faces[key])
            continue
        a = vertices[0]
        b = max(vertices, key=lambda p: (Vector(p) - Vector(a)).length_squared)
        a = max(vertices, key=lambda p: (Vector(p) - Vector(b)).length_squared)
        c = max(vertices, key=lambda p: (Vector(b) - Vector(a)).cross(Vector(p) - Vector(a)).length_squared)
        if a == b or a == c or b == c:
            output.extend(faces[key])
            continue
        output.append([corner_at[a], corner_at[b], corner_at[c]])
        replaced += 1
    print(f'BLENDER_MICRO_ISLANDS components={len(islands)} replaced={replaced} triangles={len(output)}', flush=True)
    return output


def reduce_group(group):
    binding_hints = bool(group.get('binding_hints'))
    raw_corners = [c for triangle in group['triangles'] for c in triangle]
    binding_parent = {}
    def binding_root(key):
        binding_parent.setdefault(key, key)
        while binding_parent[key] != key:
            binding_parent[key] = binding_parent[binding_parent[key]]
            key = binding_parent[key]
        return key
    if binding_hints:
        for triangle in group['triangles']:
            keys = [tuple(c[:3]) for c in triangle]
            for key in keys[1:]:
                binding_parent[binding_root(key)] = binding_root(keys[0])
        roots = {root: i for i, root in enumerate(dict.fromkeys(binding_root(k) for k in binding_parent))}
        islands = [roots[binding_root(tuple(c[:3]))] for c in raw_corners]
        original_tree = KDTree(len(raw_corners))
        for i, c in enumerate(raw_corners):
            original_tree.insert(c[:3], i)
        original_tree.balance()
        island_trees = {}
        for island in set(islands):
            points = [i for i, value in enumerate(islands) if value == island]
            tree = KDTree(len(points))
            for i in points:
                tree.insert(raw_corners[i][:3], i)
            tree.balance()
            island_trees[island] = tree
    vertices, faces, corners = [], [], []
    lookup = {}
    for triangle in simplify_micro_islands(group['triangles'], int(group['budget'])):
        face = []
        for corner in triangle:
            key = tuple(corner[:3])
            if key not in lookup:
                lookup[key] = len(vertices)
                vertices.append(key)
            face.append(lookup[key])
        if len(set(face)) == 3:
            faces.append(face)
            corners.extend(triangle)
    if not faces:
        raise ValueError(f"Empty surface {group['id']}")
    mesh = bpy.data.meshes.new('surface')
    mesh.from_pydata(vertices, [], faces)
    mesh.update()
    channels = (len(corners[0]) - 3) // 2
    for channel in range(channels):
        layer = mesh.uv_layers.new(name=f'UV{channel}')
        for loop, corner in zip(layer.data, corners):
            loop.uv = corner[3 + channel * 2:5 + channel * 2]
    # Weld positions while retaining corner UV seams; remove duplicate faces.
    bm = bmesh.new()
    bm.from_mesh(mesh)
    # Coordinates are UE centimetres. A 0.01 cm weld removes coincident seams
    # that otherwise leave non-manifold edges locked during collapse.
    if binding_hints:
        # Never weld nearby vertices from separate authored islands: that
        # would interpolate the island id and invalidate its binding hint.
        weld_groups = {}
        for v in bm.verts:
            island = islands[original_tree.find(v.co)[1]]
            weld_groups.setdefault(island, []).append(v)
        for members in weld_groups.values():
            bmesh.ops.remove_doubles(bm, verts=members, dist=0.01)
    else:
        bmesh.ops.remove_doubles(bm, verts=list(bm.verts), dist=0.01)
    bmesh.ops.dissolve_degenerate(bm, edges=list(bm.edges), dist=0.00001)
    seen, duplicates = set(), []
    bm.verts.index_update()
    for face in bm.faces:
        key = tuple(sorted(v.index for v in face.verts))
        if key in seen:
            duplicates.append(face)
        else:
            seen.add(key)
    bmesh.ops.delete(bm, geom=duplicates, context='FACES_ONLY')
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    bm.to_mesh(mesh)
    bm.free()
    if binding_hints:
        layer = mesh.attributes.new('source_binding_island', 'FLOAT', 'POINT')
        for v, value in zip(mesh.vertices, layer.data):
            value.value = islands[original_tree.find(v.co)[1]]
    obj = bpy.data.objects.new('surface', mesh)
    bpy.context.collection.objects.link(obj)
    budget = int(group['budget'])
    source_count = len(mesh.polygons)
    ratio = min(1.0, budget / max(1, source_count))
    result = None
    best_result, best_count, best_bindings = None, sys.maxsize, None
    for attempt in range(12):
        modifier = obj.modifiers.new('bounded decimation', 'DECIMATE')
        modifier.ratio = ratio
        modifier.use_collapse_triangulate = True
        evaluated = obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
        reduced = evaluated.to_mesh()
        reduced.calc_loop_triangles()
        count = len(reduced.loop_triangles)
        print(f"BLENDER_ATTEMPT id={group['id']} ratio={ratio:.8f} triangles={count} budget={budget}", flush=True)
        if 0 < count < best_count:
            candidate, candidate_bindings = [], []
            for triangle in reduced.loop_triangles:
                output, bindings = [], []
                for index in triangle.loops:
                    loop = reduced.loops[index]
                    corner = list(reduced.vertices[loop.vertex_index].co)
                    for layer in reduced.uv_layers:
                        corner.extend(layer.data[index].uv)
                    output.append(corner)
                    if binding_hints:
                        island = round(reduced.attributes['source_binding_island'].data[loop.vertex_index].value)
                        candidates = island_trees[island].find_n(corner[:3], 32)
                        best = min(candidates, key=lambda hit: hit[2] ** 2 + sum((a-b)**2 for a,b in zip(corner[3:5], raw_corners[hit[1]][3:5])) * 40000)
                        bindings.append(best[1])
                candidate.append(output)
                candidate_bindings.append(bindings)
            best_result, best_count = candidate, count
            best_bindings = candidate_bindings
        if 0 < count <= budget:
            result = best_result
        evaluated.to_mesh_clear()
        obj.modifiers.remove(modifier)
        if result is not None:
            break
        ratio *= min(0.9, budget / max(1, count) * 0.98)
    retained = not result
    if retained and best_result:
        result = best_result
        print(f"BLENDER_RETAIN_COLLAPSE_FLOOR id={group['id']} triangles={best_count} target={budget}", flush=True)
    if retained and not result:
        # Keep the cleaned authored surface when topology prevents collapse.
        # Do not remove an island or keep a failed collapsed approximation.
        mesh.calc_loop_triangles()
        result = []
        for triangle in mesh.loop_triangles:
            output = []
            for index in triangle.loops:
                loop = mesh.loops[index]
                corner = list(mesh.vertices[loop.vertex_index].co)
                for layer in mesh.uv_layers:
                    corner.extend(layer.data[index].uv)
                output.append(corner)
            result.append(output)
        print(f"BLENDER_RETAIN_SURFACE id={group['id']} triangles={len(result)} target={budget}", flush=True)
    bpy.data.objects.remove(obj, do_unlink=True)
    bpy.data.meshes.remove(mesh)
    print(f"BLENDER_SURFACE id={group['id']} source={source_count} triangles={len(result)} budget={budget}", flush=True)
    response = {'id': group['id'], 'triangles': result, 'retained': retained}
    if binding_hints:
        if best_bindings is None or len(best_bindings) != len(result):
            raise ValueError('Missing topology binding hints')
        response['binding_indices'] = best_bindings
    return response


if __name__ == '__main__':
    arguments = sys.argv[sys.argv.index('--') + 1:]
    source = json.loads(Path(arguments[0]).read_text(encoding='utf-8-sig'))
    groups = [reduce_group(g) for g in source['groups']]
    for retry in range(3):
        total = sum(len(g['triangles']) for g in groups)
        if total <= source['reserve']:
            break
        retained_count = sum(len(g['triangles']) for g in groups if g['retained'])
        flexible_count = total - retained_count
        remaining = source['reserve'] - retained_count
        if remaining <= 0 or not flexible_count:
            break
        replacement = []
        for request, result in zip(source['groups'], groups):
            if result['retained']:
                replacement.append(result)
                continue
            request = dict(request)
            request['budget'] = max(4, int(len(result['triangles']) * remaining / flexible_count * 0.98))
            replacement.append(reduce_group(request))
        groups = replacement
    output = {'schema': 1, 'groups': groups}
    total = sum(len(g['triangles']) for g in groups)
    if total > source['reserve']:
        raise ValueError(f"Protected surfaces need {total} triangles, reserve is {source['reserve']}")
    Path(arguments[1]).write_text(json.dumps(output, separators=(',', ':')), encoding='utf-8')
