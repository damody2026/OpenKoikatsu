"""Recover a completed image tool receipt without paying for another image.

The CLI may hang after image_gen has succeeded. Match the exact original user
prompt, tool call and session-local image before importing its receipt. A changed
design remains stale; recovery never upgrades it to the latest design.
"""
import argparse,hashlib,json,time,shutil
from pathlib import Path
from urllib.parse import quote
from PIL import Image
from generate_street_city_store_concepts import CACHE,ROOT,digest

def completed(folder,expected):
 base=Path.home()/'.grok/sessions'/quote(str(folder.resolve()),safe='')
 if not base.is_dir():return None
 for session in sorted(base.iterdir(),key=lambda p:p.stat().st_mtime,reverse=True):
  log=session/'chat_history.jsonl'
  if not log.is_file():continue
  events=[]
  for line in log.read_text('utf8').splitlines():
   try:events.append(json.loads(line))
   except ValueError:continue
  users=[e for e in events if e.get('type')=='user']
  matched=False
  for user in users:
   content=user.get('content','')
   if isinstance(content,list):content='\n'.join(v.get('text','') for v in content if isinstance(v,dict))
   if '<user_query>\n' in content:content=content.split('<user_query>\n',1)[1].rsplit('\n</user_query>',1)[0]
   if hashlib.sha256(content.encode()).hexdigest()==expected:matched=True
  if not matched:continue
  calls={c['id'] for e in events for c in e.get('tool_calls',[]) if c.get('name') in ('image_gen','image_edit')}
  if len(calls)!=1:continue
  for e in events:
   if e.get('type')!='tool_result' or e.get('tool_call_id') not in calls:continue
   try:data=json.loads(e['content']);source=Path(data['path']).resolve()
   except (KeyError,ValueError,TypeError):continue
   if not source.is_relative_to(session.resolve()) or not source.is_file():continue
   try:
    with Image.open(source) as im:im.verify()
   except Exception:continue
   return dict(source=source,session_id=session.name,event_log=log,tool_call_id=e['tool_call_id'])

def recover(folder):
 meta=folder/'generation.json'
 if not meta.is_file():return False
 old=json.loads(meta.read_text('utf8'))
 if old.get('status')!='failed':return False
 receipt=completed(folder,old['prompt_sha256'])
 if not receipt:return False
 archive=folder/'history'/str(time.time_ns());archive.mkdir(parents=True)
 shutil.copy2(meta,archive/meta.name)
 target=folder/'concept.jpg'
 if target.exists():shutil.copy2(target,archive/target.name)
 with Image.open(receipt['source']) as im:im.convert('RGB').save(target,quality=96)
 old.update(status='generated_review_pending',image_file=target.relative_to(ROOT).as_posix(),image_sha256=digest(target),grok_original_image_file=str(receipt['source']),grok_original_image_sha256=digest(receipt['source']),session_id=receipt['session_id'],recovered_from_completed_tool_result=True,tool_call_id=receipt['tool_call_id'],tool_event_log=str(receipt['event_log']),tool_event_log_sha256=digest(receipt['event_log']),cli_failure_preserved=str(archive/meta.name),geometry_approved=False)
 meta.write_text(json.dumps(old,ensure_ascii=False,indent=2),'utf8')
 print(folder.name+': recovered existing completed image; original design hash retained',flush=True)
 return True

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--watch',action='store_true');a=p.parse_args()
 while True:
  for folder in CACHE.iterdir():
   if folder.is_dir():recover(folder)
  if not a.watch:break
  time.sleep(20)
