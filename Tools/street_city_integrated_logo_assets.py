"""Package complete localized artwork without re-typesetting its lettering."""
import json
from pathlib import Path
from PIL import Image,ImageOps,ImageChops
from street_city_localization import OUT,LOCALES,sha,path_for

def artwork(bid,locale,size):
 receipt=OUT/'integrated-logos/jobs'/bid/'variants.json';row=json.loads(receipt.read_text('utf8'));assert row['status']=='passed_visual_review',(bid,'Integrated logos not visually approved')
 source=Path(row['variants'][locale]['path']);assert sha(source)==row['variants'][locale]['sha256']
 with Image.open(source) as im:image=im.convert('RGB')
 points=[image.getpixel(p) for p in [(5,5),(image.width-6,5),(5,image.height-6),(image.width-6,image.height-6)]];background=tuple(sorted(c[k] for c in points)[len(points)//2] for k in range(3))
 difference=ImageChops.difference(image,Image.new('RGB',image.size,background));channels=difference.split();mask=ImageChops.lighter(ImageChops.lighter(channels[0],channels[1]),channels[2]).point(lambda v:255 if v>42 else 0);box=mask.getbbox();assert box
 art=image.crop(box);art=ImageOps.contain(art,(size[0]-48,size[1]-36),Image.Resampling.LANCZOS);canvas=Image.new('RGB',size,background);canvas.paste(art,((size[0]-art.width)//2,(size[1]-art.height)//2))
 return canvas,dict(integrated_lettering=True,artwork_path=str(source),artwork_sha256=sha(source),artwork_receipt=str(receipt),generator=row['variants'][locale].get('generator','Grok Build image_edit' if locale!='zh-Hant' else 'Original Grok Build image_gen'))

def adopt(bid):
 metadata=OUT/'assets.json';data=json.loads(metadata.read_text('utf8'));key='building.'+bid;row=next(r for r in data['rows'] if r['id']==key);brand=next(r for r in data['brands'] if r['building']==bid)
 for locale in LOCALES:
  for target,size,variant in [(path_for(key,locale),(2048,420),row['variants'][locale]),(OUT/'brands'/locale/(bid+'.png'),(2048,1024),brand['variants'][locale])]:
   image,evidence=artwork(bid,locale,size);image.save(target);variant.update(path=str(target),sha256=sha(target),size=list(size),**evidence)
 metadata.write_text(json.dumps(data,ensure_ascii=False,indent=2),'utf8')
 return row

if __name__=='__main__':
 import argparse
 p=argparse.ArgumentParser();p.add_argument('ids',nargs='+');a=p.parse_args()
 for bid in a.ids:adopt(bid);print(bid,'complete integrated artwork packaged')
