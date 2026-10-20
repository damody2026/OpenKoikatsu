"""Bounded, resumable English translation via Grok Build; no tool use allowed."""
import concurrent.futures,json,subprocess,hashlib,re,sys
from street_city_localization import OUT,inventory,simplified,sha
EXE='C:/Users/Damody/.grok/bin/grok.exe'

def job(pair):
 no,values=pair;folder=OUT/'translation-jobs'/str(no);folder.mkdir(parents=True,exist_ok=True)
 request={str(i):t for i,t in enumerate(values)}
 prompt='Translate every Traditional Chinese display name below into concise, natural English for fictional Taiwanese university-city signage. Preserve room numbers, letters, 50m pool length, court counts and A/B suffixes. Department and office names must be meaningful, not transliterated. Shops use short attractive English names. Do not add commentary, do not call tools, do not use subagents. Return ONLY a JSON object with exactly the same numeric keys and English string values. No Chinese characters in English.\n'+json.dumps(request,ensure_ascii=False)
 (folder/'prompt.txt').write_text(prompt,'utf8');target=folder/'translations.json';receipt=folder/'receipt.json'
 if target.exists() and receipt.exists() and json.loads(receipt.read_text('utf8'))['prompt_sha256']==sha(folder/'prompt.txt'):return json.loads(target.read_text('utf8'))
 for attempt in range(3):
  with (folder/'cli.json').open('wb') as out,(folder/'stderr.txt').open('wb') as err:
   p=subprocess.run([EXE,'--prompt-file',str(folder/'prompt.txt'),'--cwd',str(folder),'--model','grok-4.7','--effort','medium','--tools','read_file','--no-subagents','--disable-web-search','--always-approve','--max-turns','3','--output-format','json'],stdout=out,stderr=err,timeout=600)
  try:
   assert p.returncode==0
   native=json.loads((folder/'cli.json').read_text('utf8'));raw=native['text'];match=re.search(r'\{.*\}',raw,re.S);result=json.loads(match[0]);assert set(result)==set(request)
   assert all(isinstance(t,str) and t.strip() and not re.search('[\u3400-\u9fff]',t) for t in result.values())
   data={request[k]:v for k,v in result.items()};target.write_text(json.dumps(data,ensure_ascii=False,indent=2),'utf8');receipt.write_text(json.dumps(dict(prompt_sha256=sha(folder/'prompt.txt'),output_sha256=sha(target),session_id=native.get('sessionId'),cli_exit_code=p.returncode),indent=2),'utf8');print(no,len(data),'translated',flush=True);return data
  except (AssertionError,ValueError,KeyError,TypeError):continue
 raise RuntimeError(('Translation failed',folder))

def main():
 rows=inventory();values=sorted({r['text'] for r in rows});groups=[values[i:i+110] for i in range(0,len(values),110)];english={}
 with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
  for result in pool.map(job,enumerate(groups)):english.update(result)
 entries={r['id']:{**r,'translations':{'zh-Hant':r['text'],'zh-Hans':simplified(r['text']),'en':english[r['text']]}} for r in rows}
 overrides=OUT/'curated-overrides.json'
 if overrides.exists():
  for key,override in json.loads(overrides.read_text('utf8')).items():
   entries[key]['source_display_name']=entries[key]['text'];entries[key]['translations']=override['translations'];entries[key]['text']=override['translations']['zh-Hant']
 OUT.mkdir(parents=True,exist_ok=True);(OUT/'catalogue.json').write_text(json.dumps(dict(schema=1,default_locale='zh-Hant',locales=['zh-Hant','zh-Hans','en'],entries=entries),ensure_ascii=False,indent=2),'utf8');print('CATALOGUE',len(entries),flush=True)
if __name__=='__main__':main()
