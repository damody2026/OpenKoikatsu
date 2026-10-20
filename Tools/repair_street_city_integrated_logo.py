"""Single-logo repair using the original art and an exact glyph reference."""
import argparse,json,shutil
from pathlib import Path
from street_city_localization import ROOT,OUT,sha,catalogue,render
import generate_street_city_store_concepts as engine
parser=argparse.ArgumentParser();parser.add_argument('--building',required=True);parser.add_argument('--locale',required=True,choices=['zh-Hans','en']);parser.add_argument('--revision',default='glyph-guide-v2');parser.add_argument('--correction',default='');a=parser.parse_args();bid=a.building;locale=a.locale
folder=OUT/'integrated-logos/repairs'/(bid+'-'+locale+'-'+a.revision);folder.mkdir(parents=True,exist_ok=True);key='building.'+bid;guide=folder/'exact-lettering.png';render(key,locale,guide,size=(2048,512),background=(245,241,228),foreground=(36,26,19))
original=ROOT/'output/street-social-city-production/shop-brand-grok-v2/assets'/bid/'logo/concept.jpg';names=catalogue()['entries'][key]['translations'];pairs='; '.join(f'{b} instead of {c}' for c,b in zip(names['zh-Hant'],names['zh-Hans']) if c!=b)
prompt=f'''Use image_edit exactly ONCE with TWO local reference images, in this order:
1. Original brand art: {original.as_posix()}
2. Exact target glyph reference: {guide.as_posix()}
No image_gen, no shell, no other tools, no subagents.
Create ONE final square brand logo, no comparison sheet. The output must preserve the first reference's original symbol, frame, colors, overall arrangement and custom lettering treatment, but replace ALL shop-name lettering with the EXACT text shown in reference 2: {names[locale]}.
Reference 2 is an anatomical SPELLING GUIDE, not a caption to paste below the logo. Integrate its exact characters into the lettering area of reference 1, with the same ink color, stroke character and perspective as the existing logo. Do NOT copy wrong Traditional Chinese character anatomy from the first reference. Correct forms are {pairs if locale=='zh-Hans' else names[locale]}. For Simplified Chinese, 饮 uses the THREE-STROKE radical 饣, not the complex radical 飠. Inspect each glyph before finishing. No original Chinese may remain in English. Retain every original decorative icon even when English text needs two lines. {a.correction}
Same flat ivory background, graphic art only, generous outer margins, no ID, no extra letters, no watermark. Return only the absolute final image file path.'''
(folder/'grok-prompt.txt').write_text(prompt,'utf8');(folder/'brief.json').write_text(json.dumps(dict(building=bid,locale=locale,original_sha256=sha(original),guide_sha256=sha(guide),text=names[locale]),ensure_ascii=False,indent=2),'utf8');record=dict(id=folder.name,tools='image_edit',design_sha256=sha(folder/'brief.json'),prompt_sha256=sha(folder/'grok-prompt.txt'));engine.CACHE=folder.parent;exe=shutil.which('grok.exe') or shutil.which('grok');assert exe
for _ in range(3):
 result=engine.one(record,exe)
 if result['status']=='generated_review_pending':break
assert result['status']=='generated_review_pending'
