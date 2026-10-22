-- Independent native-frame checks: original image hashes and visible time variation.
package.path='Tools/lua/?.lua;'..package.path
local json=require('ok.json')
local out='C:/portable/OpenKoikatsu/output/street-social-city-production/activity-scenes-v1'
local code=[====[
import json,hashlib
from pathlib import Path
import numpy as np
from PIL import Image
b=Path('C:/portable/OpenKoikatsu/output/street-social-city-production/activity-scenes-v1')
r=json.loads((b/'render.json').read_text());install=json.loads((b/'install.json').read_text());audit=json.loads((b/'audit.json').read_text())
assert r['status']==audit['status']=='passed'
assert r['map_sha256']==install['map_sha256']==audit['map_sha256']
for q in r['images']:
 assert hashlib.sha256(Path(q['path']).read_bytes()).hexdigest()==q['sha256']
rows=[]
for name,rect in [('05-fireworks',(0,0,1440,900)),('04-sunset',(0,270,1440,540)),('02-fountain',(320,280,1120,650)),('06-fish',(0,360,1440,900))]:
 a=np.asarray(Image.open(b/'renders'/(name+'.png')).convert('RGB').crop(rect),dtype=np.float32)
 c=np.asarray(Image.open(b/'renders'/(name+'-later.png')).convert('RGB').crop(rect),dtype=np.float32)
 d=np.abs(a-c);fraction=float((d.max(axis=2)>8).mean());mean=float(d.mean())
 rows.append(dict(scene=name,region=list(rect),mean_rgb_difference=mean,changed_fraction=fraction,passed=mean>.1 and fraction>.005))
assert all(q['passed'] for q in rows),rows
(b/'art-frame-verification.json').write_text(json.dumps(dict(status='passed',map_sha256=r['map_sha256'],native_images=len(r['images']),comparison=rows),indent=2))
print(json.dumps(rows))
]====]
local f=assert(io.open(out..'/verify-frames-code.txt','wb'));f:write(code);f:close()
-- A command string reads the saved diagnostic text; no new Python script is added to the project.
assert(os.execute([[python -c "exec(open(']]..out..[[/verify-frames-code.txt',encoding='utf-8').read())"]]))
