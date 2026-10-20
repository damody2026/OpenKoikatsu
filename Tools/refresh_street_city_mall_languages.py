"""Refresh only current mall language assets; retain other building evidence."""
import json
from street_city_localization import OUT,ROOT,catalogue
from build_street_city_language_assets import one,publish
from build_street_city_language_models import convert

def main():
 assets=json.loads((OUT/'assets.json').read_text('utf8'))
 entries=catalogue()['entries']
 assets['rows']=[one(entries[r['id']]) if r['id'].startswith('tenant.M1-') else r for r in assets['rows']]
 (OUT/'assets.json').write_text(json.dumps(assets,ensure_ascii=False,indent=2),'utf8');publish(assets['rows'])
 row=convert(('M1',ROOT/'output/street-social-city-production/mall-v1/M1-complete-2k.glb'))
 manifest=json.loads((OUT/'models.json').read_text('utf8'));manifest['buildings']=[row if b['building']=='M1' else b for b in manifest['buildings']]
 (OUT/'models.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),'utf8')
 print('MALL PREMIUM SIGNS AND THREE-LANGUAGE GLB CURRENT',flush=True)

if __name__=='__main__':main()
