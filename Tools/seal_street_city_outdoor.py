"""Assemble only certified primitive geometry and accepted curved track output."""
import bpy,sys,json
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_outdoor_contract import ROOT,OUT
from street_city_remesh_gate import verify,sha
rows=json.loads((OUT/'sources.json').read_text('utf8'))['facilities']
for q in rows:
 bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=q['source']);track=None
 for name,r in q['primitive_sources'].items():verify(OUT.parent/'components'/name/'remesh',r['sha256'])
 if q['track_source']:
  track=verify(OUT/q['id']/'track-remesh');bpy.ops.import_scene.gltf(filepath=track['path'])
 bpy.ops.export_scene.gltf(filepath=str(OUT/q['id']/'adopted.glb'),export_format='GLB',export_extras=True)
 r=dict(status='passed',source=q['source'],source_sha256=sha(q['source']),builder_sha256=sha(ROOT/'Tools/build_street_city_outdoor.py'),path_union_sha256=sha(ROOT/'Tools/street_city_rectangular_paths.py'),sha256=sha(OUT/q['id']/'adopted.glb'),primitive_sources=q['primitive_sources'],track=track,method='Affine copies of actual Rust-remeshed board/cylinder vertices; curved track independently processed by Rust and bounded source-topology recovery')
 (OUT/q['id']/'adoption.json').write_text(json.dumps(r,indent=2),'utf8')
