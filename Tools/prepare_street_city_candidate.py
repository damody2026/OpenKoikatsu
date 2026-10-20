"""Blender: import a real download, bake transforms and normalize its bounds.

Candidate output is explicitly unapproved. Anisotropic scaling is recorded for
review, never hidden as a claim that AI respected physical dimensions.
"""
import argparse,hashlib,json,math,sys
from pathlib import Path
import bpy
from mathutils import Vector,Matrix
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_parts import geometry_signature
from street_city_io import write_json
from street_city_model_gate import inspect_glb,geometry_errors,normalization_errors
from street_city_catalog import load_asset_catalog
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--asset',required=True);p.add_argument('--input',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--rotate-x',type=float,default=0);p.add_argument('--rotate-z',type=float,default=0);args=p.parse_args(sys.argv[sys.argv.index('--')+1:])
args.input=args.input.resolve();args.output=args.output.resolve()
spec=load_asset_catalog(ROOT/'Docs/StreetSocialCity/production')[args.asset]
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
ext=args.input.suffix.lower()
if ext=='.glb':bpy.ops.import_scene.gltf(filepath=str(args.input))
elif ext=='.stl':bpy.ops.wm.stl_import(filepath=str(args.input))
elif ext=='.obj':bpy.ops.wm.obj_import(filepath=str(args.input))
else:raise SystemExit('Supported sources: GLB, STL, OBJ')
meshes=[o for o in bpy.context.scene.objects if o.type=='MESH']
if not meshes:raise SystemExit('No mesh in source')
rotation=Matrix.Rotation(math.radians(args.rotate_z),4,'Z') @ Matrix.Rotation(math.radians(args.rotate_x),4,'X')
points=[]
for o in meshes:
    world=rotation @ o.matrix_world.copy();o.data=o.data.copy()
    for v in o.data.vertices:v.co=world @ v.co;points.append(v.co.copy())
    o.parent=None;o.matrix_world=Matrix.Identity(4)
low=Vector([min(v[k] for v in points) for k in range(3)]);high=Vector([max(v[k] for v in points) for k in range(3)])
original=high-low
if min(original)<=0:raise SystemExit('Degenerate bounds')
scale=Vector([spec['size_m'][k]/original[k] for k in range(3)])
origin=Vector([(low.x+high.x)/2,(low.y+high.y)/2,low.z])
if spec['pivot']=='hinge_bottom':origin.x=low.x
elif spec['pivot']!='floor_center':raise SystemExit('Unsupported pivot')
for o in meshes:
    for v in o.data.vertices:v.co=Vector([(v.co[k]-origin[k])*scale[k] for k in range(3)])
    o.data.update();o['actual_source']='Imported candidate; provenance and mesh quality pending review'
bpy.context.scene['production_status']='actual_download_candidate_NOT_APPROVED'
args.output.mkdir(parents=True,exist_ok=True)
bpy.ops.wm.save_as_mainfile(filepath=str(args.output/'candidate.blend'))
bpy.ops.export_scene.gltf(filepath=str(args.output/'candidate.glb'),export_format='GLB')
actual=inspect_glb(args.output/'candidate.glb');errors=geometry_errors(spec,actual)+normalization_errors(list(original),spec['size_m'])
report=dict(asset_id=spec['id'],geometry_signature=geometry_signature(spec),approved=False,status='normalized_candidate_pending_mesh_quality_review',source_file=str(args.input.resolve()),source_sha256=hashlib.sha256(args.input.read_bytes()).hexdigest(),original_size_m=list(original),rotation_x_deg=args.rotate_x,rotation_z_deg=args.rotate_z,scale_xyz=list(scale),anisotropic_ratio=max(scale)/min(scale),source_has_materials=any(o.data.materials for o in meshes),errors=errors,**actual)
if errors:report['status']='rejected_candidate_dimensions_or_distortion'
write_json(args.output/'candidate.json',report);print(json.dumps(report))
if errors:raise SystemExit(1)
