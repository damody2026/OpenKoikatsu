"""Versioned functional corrections, independent of historical accepted assemblies."""
import json,hashlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'output/street-social-city-production/functional-interiors-v1'
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def read(p):return json.loads(Path(p).read_text('utf8'))
def active():
 p=OUT/'adoption.json'
 return read(p) if p.exists() else None
def apply_sources(rows):
 a=active()
 if not a:return rows
 assert a['status']=='passed'
 override={r['id']:r for r in a['floors']}
 for r in override.values():assert sha(r['source'])==r['source_sha256'],r['id']+' revised source changed'
 assert set(override)<={r['id'] for r in rows}
 for row in rows:
  if row['id'] in override:assert row['source_sha256']==override[row['id']]['baseline_source_sha256'],row['id']+' base geometry changed; rebuild and revalidate the functional revision'
 return [dict(r,source=override[r['id']]['source'],source_sha256=override[r['id']]['source_sha256']) if r['id'] in override else r for r in rows]
def apply_buildings(rows):
 a=active()
 if not a:return rows
 lookup={r['building']:r for r in a['buildings']}
 for r in lookup.values():assert sha(r['model'])==r['model_sha256'] and sha(r['receipt'])==r['receipt_sha256']
 for bid,path in rows:
  if bid in lookup:assert sha(path)==read(lookup[bid]['receipt'])['source_sha256'],bid+' base building changed; functional revision is stale'
 return [(b,Path(lookup[b]['model']) if b in lookup else p) for b,p in rows]
