"""Publish the facade language evidence without replacing campus model previews."""
from pathlib import Path
from street_city_localization import ROOT

def publish():
 page=ROOT/'output/street-social-city-production/campus-v1/index.html'
 value=page.read_text('utf8');marker='id="campus-localized-signs"'
 if marker not in value:
  fragment='<p id="campus-localized-signs"><a href="../localization-v1/campus-signs/index.html">17 棟建築入口館名：繁體／簡體／English 三語招牌</a> · <a href="../localization-v1/campus-signs/unreal-acceptance.json">Unreal 招牌切換驗收</a></p>'
  value=value.replace('<h1>',fragment+'<h1>',1)
  temporary=page.with_suffix('.html.tmp');temporary.write_text(value,'utf8');temporary.replace(page)

if __name__=='__main__':publish()
