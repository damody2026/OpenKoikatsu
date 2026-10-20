"""Contact sheets of actual Grok outputs, preserving each separate original."""
import json
from PIL import Image,ImageDraw,ImageFont
from generate_street_city_grok_brands import OUT,spec

font=ImageFont.truetype('C:/Windows/Fonts/msjh.ttc',21)
for group in range(8):
 board=Image.new('RGB',(2000,1000),(32,43,45));dr=ImageDraw.Draw(board)
 for i,n in enumerate(range(group*12+1,group*12+13)):
  bid=f'S{n:02d}';p=spec(bid);x=i%4*500;y=i//4*330
  dr.text((x+8,y+8),bid+' '+p['name'],font=font,fill=(245,237,223))
  dr.text((x+8,y+38),p['direction'],font=font,fill=(190,211,207))
  for j,kind in enumerate(('logo','material')):
   path=OUT/'assets'/bid/kind/'concept.jpg'
   if not path.exists():continue
   with Image.open(path) as im:
    im=im.convert('RGB');im.thumbnail((238,238));board.paste(im,(x+8+j*245,y+75))
 board.save(OUT/f'contact-{group+1:02d}.jpg',quality=94)
