"""Fail closed before importing any asset into a production scene."""
import hashlib,json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
ENGINE=Path('E:/Paper/instant-meshes-rs/target/release/instant-meshes.exe')

def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def verify(folder,expected_sha=None):
    folder=Path(folder);report=folder/'remesh-report.json'
    r=json.loads(report.read_text('utf-8'));candidate=folder/'remeshed-candidate.glb'
    source=Path(r['source']);source=source if source.is_absolute() else ROOT/source
    assert r['status']=='passed',f'{folder}: remesh did not pass'
    assert Path(r['tool']).resolve()==ENGINE.resolve(),f'{folder}: incorrect remesh engine'
    if sha(ENGINE)!=r['tool_sha256']:
        from street_city_toolchain_history import proven
        assert proven(folder,r,candidate),f'{folder}: remesh engine changed and no exact previously accepted provenance exists'
    assert sha(source)==r['source_sha256'],f'{folder}: geometry changed after remesh'
    assert r['records'] and all(q['status']=='passed' and q.get('command') for q in r['records']),f'{folder}: missing per-mesh execution evidence'
    assert sum(q['triangles'] for q in r['records'])==r['triangles']<=r['triangle_cap'],f'{folder}: invalid face budget'
    assert candidate.read_bytes()[:4]==b'glTF',f'{folder}: missing remeshed GLB'
    digest=sha(candidate)
    if r.get('output_sha256'):
        assert digest==r['output_sha256'],f'{folder}: remesh output changed'
    else:
        # Older receipts predate output hashes. They are reusable only while the
        # candidate predates that receipt; exported assemblies pin its hash too.
        assert candidate.stat().st_mtime_ns<=report.stat().st_mtime_ns,f'{folder}: legacy output modified after receipt'
    if expected_sha:assert digest==expected_sha,f'{folder}: adopted asset version changed'
    return dict(path=str(candidate),sha256=digest,remesh_report=str(report),source_sha256=r['source_sha256'],tool_sha256=r['tool_sha256'])
