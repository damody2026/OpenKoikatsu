"""Certified analytic board/round primitives for exact metric instancing."""
import bpy,sys,json,argparse
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--shape',choices=['panel','cylinder','ring','ring-low'],default='panel');a=p.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
ROOT=Path('C:/portable/OpenKoikatsu');OUT=ROOT/f'output/street-social-city-production/campus-v1/components/unit-{a.shape}';OUT.mkdir(parents=True,exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True)
if a.shape=='panel':bpy.ops.mesh.primitive_cube_add(size=1,location=(0,0,.5))
elif a.shape=='cylinder':bpy.ops.mesh.primitive_cylinder_add(vertices=16,radius=.5,depth=1,location=(0,0,.5))
else:
 bpy.ops.mesh.primitive_torus_add(major_segments=16 if a.shape=='ring-low' else 32,minor_segments=4 if a.shape=='ring-low' else 8,major_radius=.465,minor_radius=.035)
 for v in bpy.context.object.data.vertices:v.co.z=(v.co.z+.035)/.07
o=bpy.context.object;o.name='certified-'+a.shape;o['source_route']='metric analytic component; declared cut-size transforms';m=bpy.data.materials.new('Campus primitive finish');m.use_nodes=True;o.data.materials.append(m)
bpy.ops.export_scene.gltf(filepath=str(OUT/'source.glb'),export_format='GLB',export_extras=True)
(OUT/'source.json').write_text(json.dumps(dict(size_m=[1,1,1],pivot='bottom-centre',shape=a.shape,usage='verified primitive reused by declared metric transforms')),encoding='utf8')
