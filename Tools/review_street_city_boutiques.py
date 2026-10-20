"""Versioned contact boards of all 80 ACTUAL model renders."""
import json
from PIL import Image,ImageDraw,ImageFont
from street_city_mall_boutiques import OUT,profiles
from street_city_localization import sha,FONT

def main():
 r=json.loads((OUT/'render-receipt.json').read_text('utf8'));assert r['status']=='passed' and set(r['images'])==set(profiles())
 folder=OUT/'review';folder.mkdir(exist_ok=True);font=ImageFont.truetype(FONT,19);boards=[]
 for floor in range(1,11):
  image=Image.new('RGB',(1060,1620),(25,29,31));draw=ImageDraw.Draw(image)
  draw.text((15,8),f'M1 {floor}F — actual Blender model renders',font=font,fill=(239,230,209))
  tenants=[p for p in profiles().values() if p['floor']==floor]
  for i,p in enumerate(tenants):
   v=r['images'][p['id']];assert sha(v['path'])==v['sha256'];x=(i%2)*530;y=40+(i//2)*395
   draw.text((x+8,y+5),p['id']+' '+p['name']+' · '+p['style'],font=font,fill=(239,230,209))
   with Image.open(v['path']) as im:image.paste(im.convert('RGB').resize((518,358)),(x+6,y+34))
  path=folder/f'F{floor:02d}.jpg';image.save(path,quality=92);boards.append(dict(floor=floor,path=str(path),sha256=sha(path),tenants=[p['id'] for p in tenants]))
 (folder/'boards.json').write_text(json.dumps(dict(status='requires_visual_review',model_sha256=r['model_sha256'],boards=boards),indent=2),'utf8')
 print('10 boards / 80 actual tenant views')

if __name__=='__main__':main()
