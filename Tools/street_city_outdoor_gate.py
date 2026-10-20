"""Verify a reusable-remeshed-primitive assembly without inventing a remesh receipt."""
import json
from pathlib import Path
from street_city_outdoor_contract import OUT,ROOT
from street_city_remesh_gate import verify,sha
def verify_outdoor(fid,expected=None):
 folder=OUT/fid;r=json.loads((folder/'adoption.json').read_text('utf8'));assert r['status']=='passed'
 for p,v in r['primitive_sources'].items():verify(OUT.parent/'components'/p/'remesh',v['sha256'])
 if r.get('track'):verify(folder/'track-remesh',r['track']['sha256'])
 assert sha(r['source'])==r['source_sha256'];assert sha(ROOT/'Tools/build_street_city_outdoor.py')==r['builder_sha256'];assert sha(folder/'adopted.glb')==r['sha256']
 assert sha(ROOT/'Tools/street_city_rectangular_paths.py')==r['path_union_sha256']
 if expected:assert r['sha256']==expected
 return dict(path=str(folder/'adopted.glb'),sha256=r['sha256'],assembly_receipt=str(folder/'adoption.json'),source_sha256=r['source_sha256'],primitive_sources=r['primitive_sources'],track=r.get('track'))
