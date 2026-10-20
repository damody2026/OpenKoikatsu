"""Reuse controls only if all indoor exporter inputs and actual GLB hashes match.

Exit2 means a full export is required. Always reruns the actual-vertex validator.
This never approves a Tripo asset or changes geometry in a model.
"""
from pathlib import Path
import hashlib,json,subprocess,sys
from street_city_control_inputs import control_inputs
from street_city_io import write_json,write_text
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'output/street-social-city-production';PROD=ROOT/'Docs/StreetSocialCity/production'
try:
    oldraw=(OUT/'control-source-manifest.json').read_bytes();old=json.loads(oldraw)
    oldparts=json.loads((OUT/'control-source-parts.json').read_text('utf-8'))
    report=json.loads((OUT/'control-assemblies-acceptance.json').read_text('utf-8'))
    verified=json.loads((OUT/'control-glb-acceptance.json').read_text('utf-8'))
    previous_digest=hashlib.sha256(oldraw).hexdigest()
    assert report['status']==verified['status']=='passed'
    assert report['source_manifest_sha256']==verified['source_manifest_sha256']==oldparts['source_manifest_sha256']==previous_digest
    raw=(PROD/'assembly-manifest.json').read_bytes();m=json.loads(raw);digest=hashlib.sha256(raw).hexdigest()
    parts=json.loads((PROD/'manufacturing-manifest.json').read_text('utf-8'))
    assert parts['source_manifest_sha256']==digest
    assert control_inputs(old,oldparts)==control_inputs(m,parts)
    assert len(report['buildings'])==sum(q.get('kind')=='building' for q in m['layouts'])
    for q in report['buildings']:
        assert hashlib.sha256((ROOT/q['glb']).read_bytes()).hexdigest()==q['sha256']
        meta=json.loads((ROOT/q['glb']).with_suffix('.json').read_text('utf-8'))
        assert meta['source_manifest_sha256']==previous_digest
except (AssertionError,KeyError,ValueError,OSError):
    print('Indoor inputs or cached evidence changed; full control export required.');raise SystemExit(2)
for q in report['buildings']:
    path=(ROOT/q['glb']).with_suffix('.json');meta=json.loads(path.read_text('utf-8'))
    meta.update(source_manifest_sha256=digest,reused_control_geometry=True,previous_source_manifest_sha256=previous_digest)
    write_json(path,meta)
report.update(source_manifest_sha256=digest,reused_controls=len(report['buildings']),reuse_reason='all indoor exporter inputs unchanged; actual GLB hashes match')
write_json(OUT/'control-assemblies-acceptance.json',report)
subprocess.run([sys.executable,str(ROOT/'Tools/verify_street_city_control_exports.py')],cwd=ROOT,check=True)
write_text(OUT/'control-source-manifest.json',raw.decode('utf-8'));write_json(OUT/'control-source-parts.json',parts)
print('Indoor controls reused with complete input equality and fresh actual-vertex acceptance.')
