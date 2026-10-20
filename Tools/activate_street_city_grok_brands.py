"""Adopt only Grok images with exact prompt, tool-call and output evidence."""
import argparse,json,os
from pathlib import Path
from generate_street_city_grok_brands import OUT,ROOT,spec
from street_city_shop_wall_styles import sha
from recover_street_city_store_concepts import completed

def verify(bid):
 p=spec(bid);assets={}
 for kind in ('logo','material'):
  folder=OUT/'assets'/bid/kind;r=json.loads((folder/'generation.json').read_text('utf8'))
  assert r['status']=='generated_review_pending' and sha(folder/'grok-prompt.txt')==r['prompt_sha256']
  assert json.loads((folder/'brief.json').read_text('utf8'))==p
  receipt=completed(folder,r['prompt_sha256']);assert receipt and receipt['session_id']==r['session_id']
  assert sha(receipt['source'])==r['grok_original_image_sha256'] and sha(folder/'concept.jpg')==r['image_sha256']
  assert sha(receipt['event_log'])==r['tool_event_log_sha256']
  assets[kind]={k:r[k] for k in ('image_sha256','grok_original_image_file','grok_original_image_sha256','prompt_sha256','session_id','tool_event_log','tool_event_log_sha256','tool_call_id')}
 from PIL import Image
 import numpy as np
 with Image.open(OUT/'assets'/bid/'material/concept.jpg') as im:main=np.array(im.convert('RGB').resize((64,64))).mean(axis=(0,1)).astype(int).tolist()
 with Image.open(OUT/'assets'/bid/'logo/concept.jpg') as im:
  rgb=np.array(im.convert('RGB').resize((128,128)));sat=rgb.max(2).astype(float)-rgb.min(2);chosen=rgb[(sat>35)&(rgb.min(2)<190)]
  accent=np.median(chosen,axis=0).astype(int).tolist() if len(chosen) else [55,58,54]
 return dict(**p,assets=assets,main_rgb=main,accent_rgb=accent)

def activate(ids):
 path=OUT/'activation.json';r=json.loads(path.read_text('utf8')) if path.exists() else dict(status='adopting',buildings={})
 for bid in ids:r['buildings'][bid]=verify(bid)
 r['status']='active' if len(r['buildings'])==96 else 'adopting'
 temp=path.with_suffix('.tmp');temp.write_text(json.dumps(r,ensure_ascii=False,indent=2),'utf8');os.replace(temp,path)
 print('Genuine Grok brands activated:',len(r['buildings']),flush=True)

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('ids',nargs='+');a=p.parse_args();activate(a.ids)
