"""Extend the existing three-language delivery without rebuilding or replacing prior models."""
import json
from PIL import Image
from street_city_remaining_contract import IDS,context,OUT
from street_city_localization import OUT as LANG,LOCALES,path_for,sha
from build_street_city_language_models import convert
from audit_street_city_localization import model

def main():
 catalogue=json.loads((LANG/'catalogue.json').read_text('utf8'));assets=json.loads((LANG/'assets.json').read_text('utf8'));rows={r['id']:r for r in assets['rows']}
 for key in json.loads((OUT/'localization-sources.json').read_text('utf8'))['entries']:
  e=catalogue['entries'][key];variants={}
  for locale in LOCALES:
   path=path_for(key,locale)
   with Image.open(path) as im:size=list(im.size)
   variants[locale]=dict(path=str(path),sha256=sha(path),size=size)
  rows[key]=dict(id=key,building=e['building'],kind='room',variants=variants)
 assets['rows']=list(rows.values());assets['catalogue_sha256']=sha(LANG/'catalogue.json');(LANG/'assets.json').write_text(json.dumps(assets,ensure_ascii=False,indent=2),'utf8')
 manifest=json.loads((LANG/'models.json').read_text('utf8'));by={r['building']:r for r in manifest['buildings']};results=[]
 for bid in IDS:
  c=context(bid);acc=json.loads((c['out']/'acceptance.json').read_text('utf8'));assert acc['status']=='passed';source=c['out']/f'{bid}-complete-2k.glb';assert sha(source)==acc['sha256'];row=convert((bid,source));results.append(model(row));by[bid]=row
 manifest['buildings']=list(by.values());(LANG/'models.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),'utf8')
 (OUT/'portable-language-acceptance.json').write_text(json.dumps(dict(status='passed',buildings=results,models_manifest_sha256=sha(LANG/'models.json'),assets_manifest_sha256=sha(LANG/'assets.json')),indent=2),'utf8')

if __name__=='__main__':main()
