"""Grok image-edit the original finished logo into integrated language artwork.

No generic typeset replacement: the existing symbol, palette, stroke treatment
and composed lettering are the actual image-edit reference for every shop.
"""
import argparse,concurrent.futures,json,shutil
from pathlib import Path
from PIL import Image
from street_city_localization import ROOT,OUT,LOCALES,catalogue,sha
import generate_street_city_store_concepts as engine
DEST=OUT/'integrated-logos'

def prepare(bid):
 original=ROOT/'output/street-social-city-production/shop-brand-grok-v2/assets'/bid/'logo/concept.jpg';assert original.exists()
 names=catalogue()['entries']['building.'+bid]['translations'];folder=DEST/'jobs'/bid;folder.mkdir(parents=True,exist_ok=True)
 differences='; '.join(f'write {b}, NOT {a}' for a,b in zip(names['zh-Hant'],names['zh-Hans']) if a!=b)
 brief=dict(building=bid,original=str(original),original_sha256=sha(original),names=names,layout={'zh-Hant':'top-left','zh-Hans':'top-right','en':'bottom-left','symbol_only':'bottom-right'})
 prompt=f'''Use image_edit exactly ONCE with this existing local reference image: {original.as_posix()}
Do not use image_gen, shell, subagents or any other tool. You are localizing this finished original brand logo, NOT redesigning it and NOT putting a generic font below an unrelated icon.
Generate ONE square 2 by 2 comparison sheet, exactly FOUR equal square cells aligned in a clean grid, identical flat ivory background, no dividing lines, no captions, no language labels, no IDs, no watermark. Every full logo must have generous inset margins so no strokes cross a cell edge.
TOP LEFT: faithfully reproduce the original complete logo, with the exact Traditional Chinese shop name: {names['zh-Hant']}
TOP RIGHT: the SAME complete branded logo with the name integrated into the original artwork in Simplified Chinese: {names['zh-Hans']}. Character corrections required: {differences or 'The spelling is unchanged; preserve the same original lettering.'}
BOTTOM LEFT: the SAME complete branded logo, with the exact English shop name integrated into the original artwork: {names['en']}
BOTTOM RIGHT: repeat the original Traditional Chinese logo unchanged, for comparison only.
Preserve the original distinctive icon, silhouette, colors, badge contour, composition, hand lettering character, brush/engraving/pixel/ornamental treatment, line weight and decorative flourishes. EVERY localized cell must retain the original emblem/icon as well as its frame; do not silently omit it in English. Adapt lettering width and arrangement naturally for English while retaining the same brand. This is four versions of ONE brand, never four new brands. Simplified and English cells must not retain Traditional Chinese lettering. The name is part of the logo itself, including in a seal, wordmark or integrated badge where the original uses one. Spell every character and word exactly once. No stray Chinese words in English. Produce graphic artwork only, no wall, no signage mockup, no lighting or shadows. Return only the absolute resulting image file path.'''
 (folder/'brief.json').write_text(json.dumps(brief,ensure_ascii=False,indent=2),'utf8');(folder/'grok-prompt.txt').write_text(prompt,'utf8')
 return dict(id=bid,tools='image_edit',design_sha256=sha(folder/'brief.json'),prompt_sha256=sha(folder/'grok-prompt.txt'))

def crop(bid):
 folder=DEST/'jobs'/bid;meta=json.loads((folder/'generation.json').read_text('utf8'));assert meta['status']=='generated_review_pending' and sha(folder/'concept.jpg')==meta['image_sha256']
 variants={};original=ROOT/'output/street-social-city-production/shop-brand-grok-v2/assets'/bid/'logo/concept.jpg'
 with Image.open(folder/'concept.jpg') as image:
  assert .95<image.width/image.height<1.05,(bid,'Expected square 2x2 artwork sheet',image.size)
  for locale,(x,y) in {'zh-Hans':(1,0),'en':(0,1),'symbol':(1,1)}.items():
   box=(round(x*image.width/2),round(y*image.height/2),round((x+1)*image.width/2),round((y+1)*image.height/2));path=DEST/'artwork'/locale/(bid+'.png');path.parent.mkdir(parents=True,exist_ok=True);image.crop(box).save(path);variants[locale]=dict(path=str(path),sha256=sha(path),crop_px=list(box))
 path=DEST/'artwork/zh-Hant'/(bid+'.jpg');path.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(original,path);variants['zh-Hant']=dict(path=str(path),sha256=sha(path),original_unchanged=True)
 receipt=dict(building=bid,status='generated_requires_visual_review',variants=variants,generation=meta,brief_sha256=sha(folder/'brief.json'))
 (folder/'variants.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2),'utf8');return receipt

def publish():
 rows=[]
 for p in sorted((DEST/'jobs').glob('*/variants.json')):rows.append(json.loads(p.read_text('utf8')))
 (DEST/'inventory.json').write_text(json.dumps(dict(status='generated_requires_visual_review',completed=len(rows),total=96,buildings=rows),ensure_ascii=False,indent=2),'utf8')
 return rows

def main():
 parser=argparse.ArgumentParser();parser.add_argument('--ids',nargs='*');args=parser.parse_args();ids=args.ids or [f'S{n:02d}' for n in range(1,97)];records=[prepare(b) for b in ids];engine.CACHE=DEST/'jobs';exe=shutil.which('grok.exe') or shutil.which('grok');assert exe
 def one(r):
  for _ in range(3):
   result=engine.one(r,exe)
   if result['status']=='generated_review_pending':return crop(r['id'])
  raise RuntimeError(r['id'])
 with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
  for future in concurrent.futures.as_completed([pool.submit(one,r) for r in records]):future.result();publish()
if __name__=='__main__':main()
