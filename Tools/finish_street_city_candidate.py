"""Blender: bounded topology cleanup, UV unwrap and explicitly authored finishes.

Never replaces downloaded geometry with a proxy, and never issues approval.
Recipes describe a per-asset material design; source textures are not fabricated.
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
from street_city_io import write_json
from street_city_model_gate import inspect_glb, geometry_errors
from street_city_parts import geometry_signature
from street_city_catalog import load_asset_catalog

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument('--input', type=Path, required=True)
p.add_argument('--recipe', type=Path, required=True)
p.add_argument('--out', type=Path, required=True)
args = p.parse_args(sys.argv[sys.argv.index('--')+1:])
source, recipe_file, out = args.input.resolve(), args.recipe.resolve(), args.out.resolve()
recipe = json.loads(recipe_file.read_text('utf-8'))
spec = load_asset_catalog(ROOT/'Docs/StreetSocialCity/production')[recipe['asset_id']]
if source.suffix.lower() != '.blend':
    raise SystemExit('Finish a canonical imported BLEND candidate')
if recipe.get('geometry_signature') != geometry_signature(spec):
    raise SystemExit('Finishing recipe no longer matches asset geometry')
if source.parent == out:
    raise SystemExit('Keep the original canonical candidate separate from postprocessing')
bpy.ops.wm.open_mainfile(filepath=str(source))
meshes = [o for o in bpy.context.scene.objects if o.type == 'MESH']
if not meshes:
    raise SystemExit('No source mesh')
out.mkdir(parents=True, exist_ok=True)
original_points, original_triangles = [], []
for obj in meshes:
    obj.data.calc_loop_triangles()
    offset = len(original_points)
    original_points.extend(obj.matrix_world @ v.co for v in obj.data.vertices)
    original_triangles.extend(tuple(offset+i for i in tri.vertices) for tri in obj.data.loop_triangles)
tree = BVHTree.FromPolygons(original_points, original_triangles, all_triangles=True)
limit = recipe.get('max_repair_displacement_m', .001)
if not 0 < limit <= .002:
    raise SystemExit('Bounded repair must stay within two millimetres')
repair_records = []
for obj in meshes:
    if obj.modifiers or obj.matrix_world != obj.matrix_world.Identity(4):
        raise SystemExit('Apply canonical transforms/modifiers before finishing')
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    before = len(bm.verts)
    bad = {v for e in bm.edges if not e.is_manifold for v in e.verts}
    unseen = set(bad)
    clusters = []
    while unseen:
        pending = [unseen.pop()]
        cluster = []
        while pending:
            v = pending.pop()
            cluster.append(v)
            for edge in v.link_edges:
                other = edge.other_vert(v)
                if not edge.is_manifold and other in unseen:
                    unseen.remove(other)
                    pending.append(other)
        clusters.append(cluster)
    repaired = []
    for cluster in clusters:
        center = sum((v.co for v in cluster), Vector())/len(cluster)
        radius = max((v.co-center).length for v in cluster)
        if radius > limit:
            bm.free()
            raise SystemExit('Defect exceeds bounded local repair; regenerate or review manually')
        repaired.append(dict(vertices=len(cluster), max_displacement_m=radius, center_m=list(center)))
        bmesh.ops.pointmerge(bm, verts=cluster, merge_co=center)
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    # Split faces at specified finish boundaries. Choosing a whole triangle by
    # its centroid alone leaves a jagged material line across an otherwise flat leg.
    cuts = sorted({q['height_min_ratio']*spec['size_m'][2] for q in recipe['materials']
                   if 0 < q.get('height_min_ratio', 0) < 1})
    for height in cuts:
        bmesh.ops.bisect_plane(bm, geom=list(bm.verts)+list(bm.edges)+list(bm.faces),
            plane_co=Vector((0, 0, height)), plane_no=Vector((0, 0, 1)), dist=1e-8,
            clear_inner=False, clear_outer=False)
    bmesh.ops.triangulate(bm, faces=list(bm.faces))
    remaining = sum(not e.is_manifold for e in bm.edges)
    if remaining:
        bm.free()
        raise SystemExit(f'Bounded repair left {remaining} nonmanifold edges; no candidate exported')
    bm.to_mesh(obj.data)
    bm.free()
    obj.data.update()
    repair_records.append(dict(object=obj.name, original_vertices=before,
                               final_vertices=len(obj.data.vertices), repairs=repaired,
                               material_boundary_heights_m=cuts))

materials = []
for finish in recipe['materials']:
    resolution = 512
    image = bpy.data.images.new(finish['name']+' authored base colour', width=resolution, height=resolution)
    rgb = finish['base_color_linear']
    pixels = []
    for y in range(resolution):
        for x in range(resolution):
            u, v = x/resolution, y/resolution
            if finish['pattern'] == 'oak_grain':
                grain = math.sin(v*170+2.1*math.sin(u*9)+.3*math.sin(u*31))
                pores = math.sin(v*680+math.sin(u*27))
                factor = 1+.065*grain+.018*pores
            elif finish['pattern'] == 'plain':
                factor = 1
            else:
                raise SystemExit('Unknown authored texture pattern')
            pixels.extend([min(1, max(0, c*factor)) for c in rgb]+[1])
    image.pixels.foreach_set(pixels)
    image.filepath_raw = str(out/(finish['name']+'-basecolor.png'))
    image.file_format = 'PNG'
    image.save()
    image.pack()
    material = bpy.data.materials.new(finish['name']+' locally authored PBR')
    material.use_nodes = True
    shader = material.node_tree.nodes.get('Principled BSDF')
    shader.inputs['Roughness'].default_value = finish['roughness']
    shader.inputs['Metallic'].default_value = finish['metallic']
    texture = material.node_tree.nodes.new('ShaderNodeTexImage')
    texture.image = image
    coordinates = material.node_tree.nodes.new('ShaderNodeTexCoord')
    material.node_tree.links.new(coordinates.outputs['Generated'], texture.inputs['Vector'])
    material.node_tree.links.new(texture.outputs['Color'], shader.inputs['Base Color'])
    materials.append(material)

for obj in meshes:
    obj.data.materials.clear()
    for material in materials:
        obj.data.materials.append(material.copy())
    for face in obj.data.polygons:
        ratio = face.center.z/spec['size_m'][2]
        matches = [i for i, rule in enumerate(recipe['materials'])
                   if rule.get('height_min_ratio', -math.inf) <= ratio <= rule.get('height_max_ratio', math.inf)]
        if len(matches) != 1:
            raise SystemExit('Each source face must match exactly one explicit material rule')
        face.material_index = matches[0]
        face.use_smooth = True
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(66), island_margin=.015)
    bpy.ops.object.mode_set(mode='OBJECT')

# Bake coherent object-space colour into an ordinary UV image. Directly tiling
# the grain separately on every smart-projected island makes visible seams.
bpy.context.scene.render.engine = 'CYCLES'
bpy.context.scene.cycles.samples = 1
for obj in meshes:
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    atlas = bpy.data.images.new(obj.name+' authored UV atlas', width=1024, height=1024)
    temporary = []
    for material in obj.data.materials:
        nodes = material.node_tree.nodes
        output = next(n for n in nodes if n.type == 'OUTPUT_MATERIAL')
        shader = nodes.get('Principled BSDF')
        original_texture = next(n for n in nodes if n.type == 'TEX_IMAGE')
        emission = nodes.new('ShaderNodeEmission')
        material.node_tree.links.new(original_texture.outputs['Color'], emission.inputs['Color'])
        material.node_tree.links.new(emission.outputs['Emission'], output.inputs['Surface'])
        target = nodes.new('ShaderNodeTexImage')
        target.image = atlas
        nodes.active = target
        temporary.append((material, output, shader, emission, target, original_texture))
    bpy.ops.object.bake(type='EMIT', margin=8)
    atlas.filepath_raw = str(out/(obj.name+'-uv-basecolor.png'))
    atlas.file_format = 'PNG'
    atlas.save()
    atlas.pack()
    for material, output, shader, emission, target, original_texture in temporary:
        material.node_tree.links.new(shader.outputs['BSDF'], output.inputs['Surface'])
        material.node_tree.links.new(target.outputs['Color'], shader.inputs['Base Color'])
        material.node_tree.nodes.remove(emission)
        material.node_tree.nodes.remove(original_texture)

# New vertices on material cuts lie on original triangle edges, so nearest-vertex
# distance is not mesh drift. Only the explicitly moved repair vertices count.
maximum_displacement = max(
    max(tree.find_nearest(obj.matrix_world @ v.co)[3] for obj in meshes for v in obj.data.vertices),
    max((r['max_displacement_m'] for obj in repair_records for r in obj['repairs']), default=0))
if maximum_displacement > limit+1e-7:
    raise SystemExit('Final mesh drift exceeded repair limit')
bpy.context.scene['production_status'] = 'actual_tripo_geometry_locally_finished_UNAPPROVED'
bpy.ops.object.select_all(action='DESELECT')
for obj in meshes:
    obj.select_set(True)
bpy.ops.wm.save_as_mainfile(filepath=str(out/'candidate.blend'))
bpy.ops.export_scene.gltf(filepath=str(out/'candidate.glb'), export_format='GLB', use_selection=True)
actual = inspect_glb(out/'candidate.glb')
errors = geometry_errors(spec, actual)
report = dict(schema=1, approved=False, asset_id=spec['id'], geometry_signature=geometry_signature(spec),
    status='locally_finished_candidate_pending_quality_review',
    source_file=str(source), source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
    recipe_file=str(recipe_file), recipe_sha256=hashlib.sha256(recipe_file.read_bytes()).hexdigest(),
    material_origin='Locally authored PBR image textures; NOT downloaded Tripo textures.',
    topology_repairs=repair_records, max_repair_displacement_m=maximum_displacement,
    errors=errors, **actual)
write_json(out/'finishing-report.json', report)
print(json.dumps(report))
if errors:
    raise SystemExit(1)
