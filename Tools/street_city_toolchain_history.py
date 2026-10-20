"""Content-addressed ancestry for assets accepted before a tool upgrade.

No unknown asset is grandfathered. A passed historical acceptance must pin a
carrier JSON containing the exact tool/source/output receipt, or pin both the
original remesh report and output. Source/output validation still runs normally.
"""
import json,hashlib
from pathlib import Path
from functools import lru_cache
ROOT=Path(__file__).resolve().parents[1];ARCHIVE=ROOT/'output/street-social-city-production/toolchain-history'
def digest(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
@lru_cache(maxsize=256)
def _read(path,mtime,size):return json.loads(Path(path).read_text('utf8'))
def read(p):p=Path(p);s=p.stat();return _read(str(p),s.st_mtime_ns,s.st_size)
def identity(p):return str(Path(p).resolve()).casefold()
def receipts(obj):
 if isinstance(obj,dict):
  if all(k in obj for k in ('path','sha256','remesh_report','source_sha256','tool_sha256')):yield obj
  for v in obj.values():yield from receipts(v)
 elif isinstance(obj,list):
  for v in obj:yield from receipts(v)
def store(p):
 data=Path(p).read_bytes();h=hashlib.sha256(data).hexdigest();target=ARCHIVE/(h+'.json');ARCHIVE.mkdir(parents=True,exist_ok=True)
 if target.exists():assert digest(target)==h
 else:target.write_bytes(data)
 return h
def register(proof_path,carrier_path=None):
 proof_path=Path(proof_path);proof=read(proof_path);assert proof['status']=='passed' and proof.get('checks',0)>0
 ph=store(proof_path);carriers=[]
 if carrier_path is not None:
  carrier_path=Path(carrier_path);assert proof['assembly_sha256']==digest(carrier_path);carriers.append((carrier_path,proof['assembly_sha256'],'assembly'))
 else:
  for path,h in proof['files'].items():
   p=Path(path)
   if p.suffix.lower()=='.json' and p.exists() and digest(p)==h:carriers.append((p,h,'files'))
 index=read(ARCHIVE/'index.json') if (ARCHIVE/'index.json').exists() else dict(version=1,entries=[]);seen={json.dumps(q,sort_keys=True) for q in index['entries']}
 for p,h,mode in carriers:
  data=read(p);rr=list(receipts(data))
  if 'records' in data and 'tool_sha256' in data:
   output=p.parent/'remeshed-candidate.glb';expected=data.get('output_sha256') or proof.get('files',{}).get(str(output))
   if output.exists() and expected==digest(output):rr.append(dict(path=str(output),sha256=expected,remesh_report=str(p),source_sha256=data['source_sha256'],tool_sha256=data['tool_sha256']))
  if not rr:continue
  ch=store(p)
  for q in rr:
   output=Path(q['path']);report=Path(q['remesh_report'])
   if not output.exists() or digest(output)!=q['sha256'] or not report.exists():continue
   raw=read(report)
   if raw.get('status')!='passed' or raw.get('tool_sha256')!=q['tool_sha256'] or raw.get('source_sha256')!=q['source_sha256']:continue
   row=dict(candidate=identity(output),output_sha256=q['sha256'],tool_sha256=q['tool_sha256'],source_sha256=q['source_sha256'],proof=ph,carrier=ch,carrier_original=str(p),anchor=mode,report_sha256=digest(report));key=json.dumps(row,sort_keys=True)
   if key not in seen:index['entries'].append(row);seen.add(key)
 (ARCHIVE/'index.json').write_text(json.dumps(index,indent=2),'utf8');return len(index['entries'])
def proven(folder,report,candidate):
 if not (ARCHIVE/'index.json').is_file():return False
 output_hash=digest(candidate);report_hash=digest(Path(folder)/'remesh-report.json')
 for q in read(ARCHIVE/'index.json')['entries']:
  if q['candidate']!=identity(candidate) or q['output_sha256']!=output_hash or q['tool_sha256']!=report['tool_sha256'] or q['source_sha256']!=report['source_sha256'] or q['report_sha256']!=report_hash:continue
  proof_path=ARCHIVE/(q['proof']+'.json');carrier_path=ARCHIVE/(q['carrier']+'.json')
  if digest(proof_path)!=q['proof'] or digest(carrier_path)!=q['carrier']:continue
  proof=read(proof_path);carrier=read(carrier_path)
  if proof.get('status')!='passed' or not proof.get('checks',0):continue
  expected=proof.get('assembly_sha256') if q['anchor']=='assembly' else proof.get('files',{}).get(q['carrier_original'])
  if expected!=q['carrier']:continue
  found=any(identity(r['path'])==q['candidate'] and r['sha256']==output_hash and r['tool_sha256']==report['tool_sha256'] and r['source_sha256']==report['source_sha256'] for r in receipts(carrier))
  if not found and 'records' in carrier:found=q['carrier']==report_hash and carrier.get('tool_sha256')==report['tool_sha256'] and carrier.get('source_sha256')==report['source_sha256']
  if found:return True
 return False
if __name__=='__main__':
 print(register(ROOT/'output/street-social-city-production/campus-v1/delivery-acceptance.json'))
 print(register(ROOT/'output/street-social-city-production/mall-v1/acceptance.json',ROOT/'output/street-social-city-production/mall-v1/assembly-report.json'))
