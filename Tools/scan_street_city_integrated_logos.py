"""Independent Windows OCR evidence; OCR never replaces visual brand review."""
import argparse,json,re,subprocess,unicodedata
from pathlib import Path
from street_city_localization import ROOT,OUT,sha,catalogue
BASE=OUT/'integrated-logos'
def normalized(t):return re.sub(r'[^a-z0-9\u3400-\u9fff]','',unicodedata.normalize('NFKC',t).lower())
def main():
 requests=[]
 for p in sorted((BASE/'jobs').glob('*/variants.json')):
  row=json.loads(p.read_text('utf8'));bid=row['building'];cache=p.parent/'ocr.json';old=json.loads(cache.read_text('utf-8-sig')) if cache.exists() else {}
  for locale in ('zh-Hans','en'):
   variant=row['variants'][locale];digest=sha(variant['path'])
   if old.get(locale,{}).get('sha256')!=digest:requests.append(dict(id=bid+'|'+locale,path=variant['path'],sha256=digest))
 if not requests:return
 request=BASE/'ocr-request.json';output=BASE/'ocr-output.json';request.write_text(json.dumps(requests,ensure_ascii=False,indent=2),'utf8');subprocess.run([str(ROOT/'Tools/lua/lua.exe'),str(ROOT/'Tools/ocr_street_city_logos.lua'),'--request',str(request),'--output',str(output)],check=True)
 raw=json.loads(output.read_text('utf-8-sig'));raw=[raw] if isinstance(raw,dict) else raw;results={r['id']:r for r in raw}
 for r in requests:
  bid,locale=r['id'].split('|');path=BASE/'jobs'/bid/'ocr.json';old=json.loads(path.read_text('utf8')) if path.exists() else {};expected=catalogue()['entries']['building.'+bid]['translations'][locale];texts=results[r['id']]['texts'];exact=any(normalized(t)==normalized(expected) for t in texts.values());forbidden=[]
  if locale=='zh-Hans':
   names=catalogue()['entries']['building.'+bid]['translations'];forbidden=[a for a,b in zip(names['zh-Hant'],names['zh-Hans']) if a!=b and any(a in t for t in texts.values())]
  old[locale]=dict(sha256=r['sha256'],expected=expected,recognized=texts,exact_reading=exact,possible_traditional_residue=forbidden,status='ocr_exact_requires_visual_review' if exact and not forbidden else 'needs_visual_review');path.write_text(json.dumps(old,ensure_ascii=False,indent=2),'utf8')
 print('OCR',len(requests),'localized logos',flush=True)
if __name__=='__main__':main()
