"""Read completed Grok shop illustrations before deriving vector design decisions.

Images supply visible materials and motifs, never unlabelled metre dimensions.
Each decision is bound to the image, design input and actual Grok read receipt.
"""
import argparse, concurrent.futures, hashlib, json, os, re, subprocess, uuid, time, msvcrt
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
from street_city_io import write_json, write_text

ROOT=Path(__file__).resolve().parents[1]
PROD=ROOT/'Docs/StreetSocialCity/production'
CACHE=ROOT/'output/street-social-city-production/store-concepts'
OUT=ROOT/'output/street-social-city-production/image-to-svg'
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def completed_reply(folder,job,session_id=None):
 from urllib.parse import quote
 base=Path.home()/'.grok/sessions'/quote(str(folder),safe='')
 candidates=list(base.glob((session_id or '*')+'/chat_history.jsonl'))
 for event in sorted(candidates,key=lambda p:p.stat().st_mtime,reverse=True):
  try:
   rows=[json.loads(s) for s in event.read_text('utf8').splitlines()]
   if not any(v.get('type')=='user' and (folder/'prompt.txt').read_text('utf8') in ''.join(q.get('text','') for q in v.get('content',[]) if isinstance(q,dict)).replace('\r\n','\n') for v in rows):continue
   if not any(v.get('type')=='tool_result' and v.get('images') and str(folder/'illustrations.jpg') in v.get('content','') for v in rows):continue
   for v in reversed(rows):
    if v.get('type')!='assistant' or v.get('tool_calls'):continue
    raw=v.get('content','');start=raw.find('{"stores"')
    if start<0:start=raw.find('{\n')
    if start<0:continue
    data,_=json.JSONDecoder().raw_decode(raw[start:])
    if len(data['stores'])==len(job['sources']) and {x['id'] for x in data['stores']}=={x['id'] for x in job['sources']}:
     return dict(sessionId=event.parent.name,text=raw,completion_evidence='exact_prompt_successful_image_read_and_structured_assistant_reply')
  except (ValueError,KeyError,OSError):continue
 return None

def prepare(records):
 OUT.mkdir(parents=True,exist_ok=True)
 jobs=[]
 existing={}
 for path in OUT.glob('batch-*/decisions.json'):
  doc=json.loads(path.read_text('utf8'));existing[tuple(x['id'] for x in doc['sources'])]=path.parent
 for start in range(0,len(records),4):
  group=records[start:start+4];ids=tuple(x['id'] for x in group)
  folder=existing.get(ids,OUT/f'batch-{group[0]["id"]}');folder.mkdir(exist_ok=True)
  board=Image.new('RGB',(1600,480*len(group)),'#f5f1e7');draw=ImageDraw.Draw(board)
  font=ImageFont.truetype('C:/Windows/Fonts/msjh.ttc',24)
  sources=[]
  for row,r in enumerate(group):
   f=CACHE/r['id'];g=json.loads((f/'generation.json').read_text('utf8'));im=f/'concept.jpg'
   assert g['status']=='generated_review_pending' and g['design_sha256']==r.get('image_origin',{}).get('design_sha256',r['design_sha256']) and g['prompt_sha256']==r['prompt_sha256']
   assert digest(im)==g['image_sha256']
   image=Image.open(im).convert('RGB');image.thumbnail((1580,430));board.paste(image,((1600-image.width)//2,row*480+45))
   draw.text((12,row*480+6),r['id']+' '+r['name'],font=font,fill='#203c45')
   sources.append(dict(id=r['id'],name=r['name'],category=r['category'],image_sha256=digest(im),design_sha256=g['design_sha256']))
  contact=folder/'illustrations.jpg';board.save(contact,quality=96)
  prompt=('Read the attached local contact sheet with read_file BEFORE answering. Use only read_file; no shell, no writes, no image generation. The sheet contains four already generated Grok architectural illustrations, each labelled with its store ID. Inspect ALL rows.\n'
   +'Contact sheet absolute path: '+str(contact)+'\nStores: '+json.dumps(sources,ensure_ascii=False)+'\n'
   +'Return ONLY a JSON object {"stores":[...]}. Exactly one record per listed ID. Each record: id; observed_features (3 concise Traditional Chinese strings describing concrete objects, facade materials or shapes ACTUALLY VISIBLE); adopt (2 concise Traditional Chinese strings explaining visible details that can be used in a dimensioned SVG); reject (1-3 concise Traditional Chinese strings for perspective or geometry problems, or visible limits); palette {"wall":"#RRGGBB","frame":"#RRGGBB","wood":"#RRGGBB","accent":"#RRGGBB"} from visible image colours; canopy one of flat,striped,recessed,none; sign_shape one of fascia,blade,round; glazing one of framed,arched,full_height; interior_motif one of timber,metal,stone,soft,colourful. Do not infer unknown dimensions. Do not approve image geometry. Do not say you inspected any other file. Preserve exactly the store IDs. For indoor mall tenants, report the visible tenant fit-out, not a fictional outdoor building. Real metre dimensions, complete stairs/lift/WC/2m routes and floor count will be supplied separately by the host.')
  write_text(folder/'prompt.txt',prompt)
  jobs.append(dict(folder=folder,sources=sources,contact_sha256=digest(contact),prompt_sha256=digest(folder/'prompt.txt')))
 return jobs

def run(job):
 # The OS releases this lock if a worker exits unexpectedly; no stale PID flag.
 with (job['folder']/'batch.lock').open('a+b') as lock:
  if lock.tell()==0:lock.write(b'0');lock.flush()
  started=time.monotonic()
  while True:
   try:lock.seek(0);msvcrt.locking(lock.fileno(),msvcrt.LK_NBLCK,1);break
   except OSError:
    if time.monotonic()-started>600:raise TimeoutError('Another worker holds '+job['folder'].name)
    time.sleep(2)
  try:return _run(job)
  finally:lock.seek(0);msvcrt.locking(lock.fileno(),msvcrt.LK_UNLCK,1)

def _run(job):
 folder=job['folder'];target=folder/'decisions.json'
 if target.is_file():
  old=json.loads(target.read_text('utf8'))
  if old.get('prompt_sha256')==job['prompt_sha256'] and old.get('contact_sha256')==job['contact_sha256']:
   print(folder.name+': current',flush=True);return old
 env=os.environ.copy();env.update(GROK_DISABLE_AUTOUPDATER='1',GROK_MEMORY='0',PYTHONIOENCODING='utf-8')
 response=None
 from urllib.parse import quote
 if (folder/'run.json').is_file():
  try:
   candidate=json.loads((folder/'run.json').read_text('utf8'));sid=candidate['sessionId']
   old_event=Path.home()/'.grok/sessions'/quote(str(folder),safe='')/sid/'chat_history.jsonl'
   lines=[json.loads(s) for s in old_event.read_text('utf8').splitlines()]
   # Reuse only a response to this exact prompt, including current image hashes.
   if any(v.get('type')=='user' and (folder/'prompt.txt').read_text('utf8') in ''.join(q.get('text','') for q in v.get('content',[]) if isinstance(q,dict)).replace('\r\n','\n') for v in lines):response=candidate
  except (ValueError,KeyError,FileNotFoundError):pass
 if response is None:response=completed_reply(folder,job)
 if response is not None:sid=response['sessionId'];write_json(folder/'run.json',response)
 if response is None:
  sid=str(uuid.uuid4());exe='C:/Users/Damody/.grok/bin/grok.exe'
  run_file=folder/f'run-{sid}.json'
  with run_file.open('wb') as out,(folder/f'stderr-{sid}.log').open('wb') as err:
   p=subprocess.Popen([exe,'--session-id',sid,'--prompt-file',str(folder/'prompt.txt'),'--cwd',str(folder),'--model','grok-4.7','--effort','medium','--tools','read_file','--no-subagents','--disable-web-search','--always-approve','--max-turns','5','--output-format','json'],stdout=out,stderr=err,env=env)
   started=time.monotonic();done_at=None
   try:
    while p.poll() is None:
     response=completed_reply(folder,job,sid)
     if response:
      done_at=done_at or time.monotonic()
      if time.monotonic()-done_at>30:p.terminate();p.wait(timeout=20);break
     if time.monotonic()-started>480:raise TimeoutError('No completed image interpretation receipt')
     time.sleep(5)
   finally:
    if p.poll() is None:p.terminate();p.wait(timeout=20)
  response=completed_reply(folder,job,sid) or json.loads(run_file.read_text('utf8'));write_json(folder/'run.json',response)
 raw=response['text'].strip();start=raw.find('{"stores"')
 if start<0:start=raw.find('{\n')
 assert start>=0,'No structured stores object in Grok response'
 parsed,_=json.JSONDecoder().raw_decode(raw[start:]);rows=parsed['stores']
 assert {r['id'] for r in rows}=={r['id'] for r in job['sources']} and len(rows)==len(job['sources'])
 # Prove that this session actually opened the contact image before accepting prose.
 event=Path.home()/'.grok/sessions'/quote(str(folder),safe='')/sid/'chat_history.jsonl'
 assert event.is_file(),event
 events=[json.loads(s) for s in event.read_text('utf8').splitlines()]
 assert any(v.get('type')=='tool_result' and v.get('images') and 'Read image file:' in v.get('content','') and 'illustrations.jpg' in v.get('content','') for v in events),'Missing successful image-read receipt'
 for r in rows:
  assert len(r['observed_features'])>=3 and len(r['adopt'])>=2 and r['reject']
  assert all(re.fullmatch('#[0-9A-Fa-f]{6}',r['palette'][k]) for k in ('wall','frame','wood','accent'))
  assert r['canopy'] in ('flat','striped','recessed','none') and r['sign_shape'] in ('fascia','blade','round') and r['glazing'] in ('framed','arched','full_height')
 result=dict(schema=1,stores=rows,sources=job['sources'],contact_sha256=job['contact_sha256'],prompt_sha256=job['prompt_sha256'],session_id=sid,event_log=str(event),event_log_sha256=digest(event),image_geometry_approved=False)
 write_json(target,result);print(folder.name+': interpreted '+str(len(rows)),flush=True);return result

def main():
 p=argparse.ArgumentParser();p.add_argument('--batch',type=int);p.add_argument('--workers',type=int,default=3);p.add_argument('--ids',nargs='*');args=p.parse_args()
 records=json.loads((PROD/'store-design-register.json').read_text('utf8'))['stores']
 if args.ids:
  records=[r for r in records if r['id'] in args.ids];assert {r['id'] for r in records}==set(args.ids)
 jobs=prepare(records)
 if args.batch:jobs=[jobs[args.batch-1]]
 with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:results=list(pool.map(run,jobs))
 print(json.dumps(dict(batches=len(results),stores=sum(len(x['stores']) for x in results))))
if __name__=='__main__':main()
