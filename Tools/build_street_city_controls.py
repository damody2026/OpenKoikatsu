"""Build and verify every building's modular control assembly, offline only."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys,time
from street_city_io import write_json

ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'output/street-social-city-production'
p=argparse.ArgumentParser();p.add_argument('--blender',default='C:/Program Files/Blender Foundation/Blender 5.2/blender.exe')
p.add_argument('--prioritize',nargs='*',default=[])
args=p.parse_args();source=ROOT/'Docs/StreetSocialCity/production/assembly-manifest.json'
refresh=subprocess.run([sys.executable,str(ROOT/'Tools/refresh_street_city_control_metadata.py')],cwd=ROOT)
if refresh.returncode==0:raise SystemExit(0)
if refresh.returncode!=2:raise SystemExit(refresh.returncode)
m=json.loads(source.read_text('utf-8'));digest=hashlib.sha256(source.read_bytes()).hexdigest();results=[]
parts=json.loads((source.parent/'manufacturing-manifest.json').read_text('utf-8'))
snapshot_dir=OUT/'control-input-snapshots';snapshot_dir.mkdir(exist_ok=True)
exporter_sha=hashlib.sha256((ROOT/'Tools/assemble_street_city_parts.py').read_bytes()).hexdigest()
write_json(snapshot_dir/(digest+'.json'),dict(manifest=m,parts=parts,exporter_sha256=exporter_sha))
try:previous={q['building']:q for q in json.loads((OUT/'control-assemblies-acceptance.json').read_text('utf-8'))['buildings']}
except (OSError,ValueError,KeyError):previous={}
from street_city_control_inputs import control_inputs
def inputs_for(manifest,mod,bid):
    subset=dict(manifest,layouts=[q for q in manifest['layouts'] if q.get('building')==bid])
    return control_inputs(subset,mod)
snapshot_cache={}
buildings=[q for q in m['layouts'] if q.get('kind')=='building']
buildings.sort(key=lambda b:(args.prioritize.index(b['building']) if b['building'] in args.prioritize else len(args.prioritize)))
for b in buildings:
    bid=b['building'];log=OUT/f'control-assembly-{bid}.log';start=time.monotonic()
    reused=False;stem=OUT/'assemblies'/f'{bid}-layout-proxy'
    if bid in previous:
        try:
            meta=json.loads(stem.with_suffix('.json').read_text('utf-8'));old_digest=meta['source_manifest_sha256']
            if old_digest not in snapshot_cache:snapshot_cache[old_digest]=json.loads((snapshot_dir/(old_digest+'.json')).read_text('utf-8'))
            old=snapshot_cache[old_digest]
            assert old['exporter_sha256']==exporter_sha
            assert old['parts']['source_manifest_sha256']==old_digest
            assert hashlib.sha256(stem.with_suffix('.glb').read_bytes()).hexdigest()==previous[bid]['sha256']
            assert inputs_for(old['manifest'],old['parts'],bid)==inputs_for(m,parts,bid)
            assert stem.with_suffix('.blend').is_file()
            meta.update(source_manifest_sha256=digest,reused_control_geometry=True,previous_source_manifest_sha256=old_digest)
            write_json(stem.with_suffix('.json'),meta);reused=True
        except (OSError,ValueError,KeyError,AssertionError):pass
    if not reused:
        with log.open('w',encoding='utf-8') as stream:
            subprocess.run([args.blender,'--background','--factory-startup','--python-exit-code','1','--python',str(ROOT/'Tools/assemble_street_city_parts.py'),'--','--building',bid,'--proxy'],cwd=ROOT,stdout=stream,stderr=subprocess.STDOUT,check=True)
    stem=OUT/'assemblies'/f'{bid}-layout-proxy';report=json.loads(stem.with_suffix('.json').read_text('utf-8'))
    assert report['source_manifest_sha256']==digest and report['status']=='layout_proxy_not_tripo'
    assert report['rooms']==sum(q.get('building')==bid and bool(q.get('room_id')) for q in m['layouts'])
    assert stem.with_suffix('.glb').stat().st_size>1000 and stem.with_suffix('.blend').is_file()
    results.append(dict(building=bid,rooms=report['rooms'],glb=str(stem.with_suffix('.glb').relative_to(ROOT)),
        sha256=hashlib.sha256(stem.with_suffix('.glb').read_bytes()).hexdigest(),elapsed_seconds=round(time.monotonic()-start,2)))
    write_json(OUT/'control-assemblies-acceptance.json',dict(status='building' if len(results)<len(buildings) else 'passed',source_manifest_sha256=digest,buildings=results,
        actual_AI_models_verified=False,game_navigation_verified=False,contains_display_only_furniture_bounds=True))
    print(f'{len(results)}/{len(buildings)} {bid}: {report["rooms"]} rooms, modular controls '+('reused after exact input comparison' if reused else 'exported'),flush=True)
subprocess.run([sys.executable,str(ROOT/'Tools/verify_street_city_control_exports.py')],cwd=ROOT,check=True)
write_json(OUT/'control-source-manifest.json',m)
write_json(OUT/'control-source-parts.json',json.loads((ROOT/'Docs/StreetSocialCity/production/manufacturing-manifest.json').read_text('utf-8')))
print('All building control vertices and transforms verified. They are NOT generated Tripo models or approved runtime collision meshes.')
