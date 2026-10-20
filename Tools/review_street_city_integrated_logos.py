"""Four brands per sheet, original/Hans/English at readable resolution."""
import json
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
from street_city_localization import OUT,catalogue,sha
BASE=OUT/'integrated-logos';folder=BASE/'review';folder.mkdir(exist_ok=True);font=ImageFont.truetype('C:/Windows/Fonts/msjh.ttc',19)
for first in range(1,97,4):
 ids=[f'S{n:02d}' for n in range(first,first+4)]
 if not all((BASE/'jobs'/b/'variants.json').exists() for b in ids):continue
 image=Image.new('RGB',(1536,2160),(28,40,41));draw=ImageDraw.Draw(image)
 for row,bid in enumerate(ids):
  meta=json.loads((BASE/'jobs'/bid/'variants.json').read_text('utf8'));e=catalogue()['entries']['building.'+bid]
  for col,locale in enumerate(['zh-Hant','zh-Hans','en']):
   v=meta['variants'][locale];assert sha(v['path'])==v['sha256'];draw.text((col*512+8,row*540+3),bid+' '+locale+' '+e['translations'][locale],font=font,fill='white')
   with Image.open(v['path']) as im:image.paste(im.convert('RGB').resize((512,512),Image.Resampling.LANCZOS),(col*512,row*540+28))
 image.save(folder/f'{first:02d}-{first+3:02d}.jpg',quality=95)
print('Review boards updated',flush=True)
