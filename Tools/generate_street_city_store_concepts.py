"""Bounded, resumable Grok Build image jobs. A completed image is a review draft."""
import argparse,concurrent.futures,hashlib,json,os,re,shutil,subprocess,time,uuid
from pathlib import Path
from PIL import Image
ROOT=Path(__file__).resolve().parents[1];PROD=ROOT/'Docs/StreetSocialCity/production';CACHE=ROOT/'output/street-social-city-production/store-concepts'
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def one(record,exe):
 key=record['id'];folder=CACHE/key;prompt=folder/'grok-prompt.txt';meta=folder/'generation.json';target=folder/'concept.jpg'
 image_design_sha=record.get('image_origin',{}).get('design_sha256',record['design_sha256'])
 from recover_street_city_store_concepts import completed,recover
 recover(folder)
 assert digest(prompt)==record['prompt_sha256'],'Prompt changed after job registration: '+key
 if meta.is_file():
  old=json.loads(meta.read_text('utf8'))
  if old.get('status')=='generated_review_pending' and old.get('prompt_sha256')==digest(prompt) and target.is_file() and digest(target)==old.get('image_sha256'):
   if not old.get('completion_evidence'):
    receipt=completed(folder,digest(prompt))
    assert receipt and receipt['source'].resolve()==Path(old['grok_original_image_file']).resolve(),'Missing native receipt for cached image: '+key
    assert digest(receipt['source'])==old['grok_original_image_sha256']
    old.update(tool_event_log=str(receipt['event_log']),tool_event_log_sha256=digest(receipt['event_log']),tool_call_id=receipt['tool_call_id'],completion_evidence='exact_prompt_and_completed_image_tool_receipt')
    from street_city_io import write_json
    write_json(meta,old)
   if old.get('design_sha256')!=image_design_sha:
    old.setdefault('unchanged_prompt_rebindings',[]).append(dict(from_design_sha256=old.get('design_sha256'),to_design_sha256=image_design_sha,prompt_sha256=digest(prompt),reason='Exact rendering prompt unchanged; rebind input metadata only, never relabel a corrected output plan as the generation input.'))
    old['design_sha256']=image_design_sha;meta.write_text(json.dumps(old,ensure_ascii=False,indent=2),'utf8')
   print(key+': cached exact design/prompt/image',flush=True);return old
  history=folder/'history'/str(time.time_ns());history.mkdir(parents=True)
  for name in ('generation.json','concept.jpg','grok-run.json','grok-stderr.log'):
   oldfile=folder/name
   if oldfile.is_file():shutil.copy2(oldfile,history/name)
 result=dict(id=key,design_sha256=image_design_sha,prompt_sha256=digest(prompt),status='running',actual_tripo_model=False,geometry_approved=False)
 meta.write_text(json.dumps(result,ensure_ascii=False,indent=2),'utf8');start=time.monotonic()
 env=os.environ.copy();env['GROK_DISABLE_AUTOUPDATER']='1';env['PYTHONIOENCODING']='utf-8';env['GROK_MEMORY']='0'
 try:
  with (folder/'grok-run.json').open('wb') as output,(folder/'grok-stderr.log').open('wb') as error:
   sid=str(uuid.uuid4());receipt=None;receipt_at=None
   proc=subprocess.Popen([exe,'--session-id',sid,'--prompt-file',str(prompt),'--cwd',str(folder),'--model','grok-4.7','--effort','medium','--tools',record.get('tools','image_gen'),'--no-subagents','--disable-web-search','--always-approve','--max-turns','5','--output-format','json'],stdout=output,stderr=error,env=env)
   try:
    while proc.poll() is None:
     receipt=completed(folder,result['prompt_sha256'])
     if receipt and receipt['session_id']==sid:
      receipt_at=receipt_at or time.monotonic()
      if time.monotonic()-receipt_at>30:
       proc.terminate();proc.wait(timeout=20);break
     if time.monotonic()-start>900:raise TimeoutError('No completed image receipt after 900s')
     time.sleep(3)
   finally:
    if proc.poll() is None:proc.terminate();proc.wait(timeout=20)
  receipt=completed(folder,result['prompt_sha256'])
  if receipt and receipt['session_id']==sid:
   source=receipt['source'];data={'sessionId':sid}
   result.update(tool_event_log=str(receipt['event_log']),tool_event_log_sha256=digest(receipt['event_log']),tool_call_id=receipt['tool_call_id'],completion_evidence='exact_prompt_and_completed_image_tool_receipt',cli_exit_code=proc.returncode)
  else:
   assert proc.returncode==0,'Grok exit '+str(proc.returncode)
   data=json.loads((folder/'grok-run.json').read_text('utf8'));text=data.get('text','');paths=re.findall(r'[A-Za-z]:[\\/].*?\.(?:jpg|png|webp)',text,re.I)
   candidates=[Path(q) for q in paths if Path(q).is_file()]
   assert candidates,'Grok returned no existing image path'
   source=candidates[-1].resolve()
  allowed=Path.home()/'.grok/sessions';assert source.is_relative_to(allowed.resolve()),'Image path outside Grok session output'
  with Image.open(source) as im:im.verify()
  with Image.open(source) as im:im.convert('RGB').save(target,quality=96)
  result.update(status='generated_review_pending',image_file=target.relative_to(ROOT).as_posix(),image_sha256=digest(target),grok_original_image_file=str(source),grok_original_image_sha256=digest(source),session_id=data['sessionId'],elapsed_seconds=round(time.monotonic()-start,1))
 except Exception as e:result.update(status='failed',error=type(e).__name__+': '+str(e)[:500],elapsed_seconds=round(time.monotonic()-start,1))
 meta.write_text(json.dumps(result,ensure_ascii=False,indent=2),'utf8');print(key+': '+result['status'],flush=True);return result
def main():
 p=argparse.ArgumentParser();p.add_argument('--ids',nargs='*');p.add_argument('--workers',type=int,default=3);p.add_argument('--defer-gallery',action='store_true');args=p.parse_args()
 assert 1<=args.workers<=4
 records=json.loads((PROD/'store-design-register.json').read_text('utf8'))['stores'];exe=shutil.which('grok.exe') or shutil.which('grok');assert exe
 if args.ids:records=[r for r in records if r['id'] in args.ids];assert {r['id'] for r in records}==set(args.ids)
 # A retrievable primary case must precede each job. Unsupported URLs are not
 # silently replaced with the same anonymous shop template.
 blocked=[r['id'] for r in records if r['source_page_status'] not in ('downloaded','read_via_web')]
 assert not blocked,('Research not retrieved',blocked)
 with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
  results=list(pool.map(lambda r:one(r,exe),records))
 report=dict(status='all_generated_review_pending' if all(r['status']=='generated_review_pending' for r in results) else 'incomplete',jobs=results,geometry_approved=False)
 (CACHE/'batch-receipt.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),'utf8')
 if not args.defer_gallery:subprocess.run([os.sys.executable,str(ROOT/'Tools/build_street_city_store_review.py')],cwd=ROOT,check=True)
 print(json.dumps(dict(status=report['status'],jobs=len(results)),ensure_ascii=False))
if __name__=='__main__':main()
