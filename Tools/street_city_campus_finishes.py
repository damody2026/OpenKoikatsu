"""Deterministic 2K physical finishes and readable room-name textures."""
from pathlib import Path
import json,hashlib
from street_city_campus_contract import OUT,IDS,context
def generate():
 import numpy as np
 from PIL import Image,ImageDraw,ImageFont
 folder=OUT/'materials';folder.mkdir(parents=True,exist_ok=True);n=2048
 if not (folder/'oak-2k.png').exists():
  yy,xx=np.mgrid[:n,:n]/n;grain=np.sin(yy*125+np.sin(xx*8)*.8+np.sin(xx*39)*.1)+.24*np.sin(yy*900+np.sin(xx*17));base=np.array([177,132,85]);rgb=np.clip(base[None,None,:]+grain[:,:,None]*7,0,255).astype('uint8');Image.fromarray(rgb).save(folder/'oak-2k.png')
 for bid in IDS:
  c=context(bid);directory=folder/bid;directory.mkdir(exist_ok=True)
  for room in c['rooms'].values():
   from street_city_localization import render,LOCALES,path_for
   key='room.'+room['room_id']
   for locale in LOCALES:render(key,locale,path_for(key,locale),prefix=room['room_id'])
   import shutil
   shutil.copy2(path_for(key,'zh-Hant'),directory/(room['room_id']+'.png'))
 (folder/'finish-spec.json').write_text(json.dumps(dict(texture_max_px=2048,wood_repeat_m=[1,.25],source='deterministic pigment/grain; no baked illumination',signs='game-adapted room names; official reference aliases marked'),indent=2),'utf8')
if __name__=='__main__':generate();print('2K campus finishes and room signs ready')
