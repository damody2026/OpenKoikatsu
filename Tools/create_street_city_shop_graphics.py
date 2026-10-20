"""Original 2K game shop typography; all prices are fictional in-world menu text."""
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
from street_city_building_context import context
out=context('S02')['out']/'furniture';out.mkdir(parents=True,exist_ok=True)
import shutil
from street_city_localization import path_for,LOCALES
# Graphics are generated centrally; every later rebuild preserves the same
# approved integrated logo and the translated menu instead of hard-coded text.
for aid,key in [('signboard','building.S02'),('menu-board','shared.tea.menu')]:
 for locale in LOCALES:
  source=path_for(key,locale);assert source.exists(),source
  shutil.copy2(source,out/(aid+'-'+locale+'-2k.png'))
 shutil.copy2(path_for(key,'zh-Hant'),out/(aid+'-2k.png'))
im=Image.new('RGB',(2048,2048),(192,199,194));d=ImageDraw.Draw(im)
for k in range(0,2048,512):d.line((k,0,k,2048),fill=(128,137,132),width=6);d.line((0,k,2048,k),fill=(128,137,132),width=6)
im.save(out/'floor-2k.png')
