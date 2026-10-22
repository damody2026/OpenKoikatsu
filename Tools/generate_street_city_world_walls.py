"""Bounded real Grok Build image jobs; original tool receipts remain mandatory."""
import sys,json,shutil,concurrent.futures
from pathlib import Path
from street_city_world_wall_art import OUT,prepare
import generate_street_city_store_concepts as engine
def main():
 records=prepare();engine.CACHE=OUT/'assets';exe=shutil.which('grok.exe') or shutil.which('grok');assert exe
 def one(r):
  for attempt in range(3):
   result=engine.one(r,exe)
   if result['status']=='generated_review_pending':return result
  return result
 completed=[]
 with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
  for future in concurrent.futures.as_completed([pool.submit(one,r) for r in records]):
   completed.append(future.result());(OUT/'batch.json').write_text(json.dumps(dict(status='running',completed=len(completed),total=len(records),results=completed),ensure_ascii=False,indent=2),'utf8')
 bad=[r for r in completed if r['status']!='generated_review_pending']
 (OUT/'batch.json').write_text(json.dumps(dict(status='failed' if bad else 'generated_review_pending',completed=len(completed),total=len(records),results=completed),ensure_ascii=False,indent=2),'utf8');assert not bad,bad
if __name__=='__main__':main()
