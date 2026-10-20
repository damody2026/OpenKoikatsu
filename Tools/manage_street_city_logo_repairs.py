"""Persist visual decisions and bounded GPT repair queue; never auto-approve."""
import argparse,json,shutil
from pathlib import Path
from street_city_localization import ROOT,OUT,catalogue,sha
BASE=OUT/'integrated-logos';QUEUE=BASE/'repair-queue.json'

def queue():return json.loads(QUEUE.read_text('utf8')) if QUEUE.exists() else []

def add(bid,locales,reason):
 rows=queue()
 for locale in locales:
  key=bid+'|'+locale
  if any(r['key']==key for r in rows):continue
  name=catalogue()['entries']['building.'+bid]['translations'][locale];source=ROOT/'output/street-social-city-production/shop-brand-grok-v2/assets'/bid/'logo/concept.jpg'
  prompt=f'Edit this finished original brand logo into {"SIMPLIFIED CHINESE" if locale=="zh-Hans" else "ENGLISH" if locale=="en" else "TRADITIONAL CHINESE"}. Replace the actual shop-name lettering INSIDE the existing artwork IN PLACE with EXACTLY: {name}. Preserve the original distinctive icon, badge silhouette, frame, palette, decorative flourishes and integrated hand lettering style. Do not add a separate generic caption. Remove old letters before replacing them. {reason} Every name must be spelled exactly, including Simplified Chinese character anatomy. No extra letters, no duplicate character, no stray old-language words. All secondary readable text must also match the selected language; turn meaningless micro-lettering into non-text ornamental strokes. Proper visual symbols, monograms and language-neutral numbers may remain. Same square canvas and original ivory background, one complete logo, no grid, no language label, no watermark.'
  rows.append(dict(key=key,building=bid,locale=locale,name=name,source=str(source),prompt=prompt,status='pending',reason=reason))
  path=BASE/'jobs'/bid/'variants.json';data=json.loads(path.read_text('utf8'));data.setdefault('locale_reviews',{})[locale]='repair_pending';data['status']='requires_language_repair';path.write_text(json.dumps(data,ensure_ascii=False,indent=2),'utf8')
 QUEUE.write_text(json.dumps(rows,ensure_ascii=False,indent=2),'utf8')

def approve(bid,locales=None):
 p=BASE/'jobs'/bid/'variants.json';d=json.loads(p.read_text('utf8'));d.setdefault('locale_reviews',{})
 for l in locales or ['zh-Hant','zh-Hans','en']:d['locale_reviews'][l]='passed_visual_review'
 if all(d['locale_reviews'].get(l)=='passed_visual_review' for l in ['zh-Hant','zh-Hans','en']):d['status']='passed_visual_review'
 p.write_text(json.dumps(d,ensure_ascii=False,indent=2),'utf8')

def record(outputs):
 rows=queue()
 for output in outputs:
  row=next(r for r in rows if r['key']==output['key']);target=BASE/'gpt-repairs'/row['building']/(row['locale']+'.png');target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(output['source'],target);row.update(status='generated_requires_visual_review',output=str(target),output_sha256=sha(target),native_output=output['source'],generator='OpenAI built-in image_gen edit')
 QUEUE.write_text(json.dumps(rows,ensure_ascii=False,indent=2),'utf8')

def adopt(keys):
 rows=queue()
 for key in keys:
  r=next(r for r in rows if r['key']==key);assert r['status']=='generated_requires_visual_review';bid=r['building'];locale=r['locale'];path=BASE/'jobs'/bid/'variants.json';d=json.loads(path.read_text('utf8'));target=BASE/'artwork'/locale/(bid+'.png');assert sha(r['output'])==r['output_sha256'];shutil.copy2(r['output'],target);d['variants'][locale]=dict(path=str(target),sha256=sha(target),generator=r['generator'],repair_receipt=str(QUEUE),native_output=r['native_output'],prompt=r['prompt']);d.setdefault('locale_reviews',{})[locale]='passed_visual_review'
  if all(d['locale_reviews'].get(l)=='passed_visual_review' for l in ['zh-Hant','zh-Hans','en']):d['status']='passed_visual_review'
  path.write_text(json.dumps(d,ensure_ascii=False,indent=2),'utf8');r['status']='passed_visual_review'
 QUEUE.write_text(json.dumps(rows,ensure_ascii=False,indent=2),'utf8')

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--next',type=int);p.add_argument('--record');p.add_argument('--approve',nargs='*');a=p.parse_args()
 if a.next:print(json.dumps([r for r in queue() if r['status']=='pending'][:a.next],ensure_ascii=False))
 if a.record:record(json.loads(Path(a.record).read_text('utf8')))
 if a.approve:adopt(a.approve)
