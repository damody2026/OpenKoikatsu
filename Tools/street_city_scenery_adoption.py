"""Explicit reversible legacy-tree retirement after certified HISM replacement."""
from pathlib import Path
import json,hashlib
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'output/street-social-city-production/streetscape-v1'
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def apply(rows):
 activation=OUT/'activation.json'
 if not activation.exists():return rows
 a=json.loads(activation.read_text('utf8'));assert a['status'] in ('installed_pending_independent_audit','passed') and a['delivery_sha256']==sha(OUT/'delivery.json'),'Stale street scenery adoption'
 replacements={q['label']:q for q in a['legacy_replacements']};facilities={q['label']:q for q in a.get('facility_sources',[])};assert set(replacements)|set(facilities)<={q['id'] for q in rows}
 result=[]
 for q in rows:
  if q['id'] in replacements:
   assert q['source_sha256']==replacements[q['id']]['source_sha256'],'Changed legacy vegetation baseline requires re-extraction'
   q=dict(q,collision='NoCollision',visible=False,hidden_in_game=True,scenery_replacement_sha256=a['delivery_sha256'])
  if q['id'] in facilities:
   f=facilities[q['id']];assert q['source_sha256']==f['baseline_sha256'],'Changed facility baseline requires re-extraction';assert sha(f['path'])==f['sha256'];q=dict(q,source=f['path'],source_sha256=f['sha256'],scenery_facility_sha256=a['delivery_sha256'])
  result.append(q)
 return result
