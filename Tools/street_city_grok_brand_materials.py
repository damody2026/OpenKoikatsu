"""Bake genuine Grok albedos/logos into metric wall maps; no mesh changes."""
import json,hashlib,math
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'output/street-social-city-production/shop-brand-grok-v2'

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()

def active(bid):
 path=OUT/'activation.json'
 if not path.exists():return None
 entry=json.loads(path.read_text('utf8'))['buildings'].get(bid)
 if entry is None:return None
 result=dict(entry);result['baker_sha256']=sha(__file__);result['placement_sha256']=sha(ROOT/'Tools/street_city_grok_brand_placement.py')
 localized=ROOT/'output/street-social-city-production/localization-v1/integrated-logos/jobs'/bid/'variants.json'
 if localized.exists():result['integrated_logo_receipt_sha256']=sha(localized)
 for kind in ('logo','material'):
  p=OUT/'assets'/bid/kind
  assert sha(p/'concept.jpg')==entry['assets'][kind]['image_sha256'],(bid,kind)
 return result

def periodic(array,band=24):
 """Equal opposite edge pixels without mirroring the complete photograph."""
 import numpy as np
 a=array.copy();n=a.shape[0]
 for axis in (1,0):
  v=np.swapaxes(a,axis,0);edge=(v[0]+v[-1])*.5
  for k in range(band):
   strength=(1-k/band)**2
   v[k]=v[k]*(1-strength)+edge*strength
   v[-1-k]=v[-1-k]*(1-strength)+edge*strength
 return np.clip(a,0,255)

def load_logo(bid,locale='zh-Hant'):
 import numpy as np
 from PIL import Image,ImageDraw,ImageOps
 receipt=ROOT/'output/street-social-city-production/localization-v1/integrated-logos/jobs'/bid/'variants.json'
 data=json.loads(receipt.read_text('utf8'));assert data['status']=='passed_visual_review'
 variant=data['variants'][locale];source=Path(variant['path']);assert sha(source)==variant['sha256']
 with Image.open(source) as im:logo=im.convert('RGB')
 colors=np.array(logo).astype(float);edges=np.concatenate([colors[:12].reshape(-1,3),colors[-12:].reshape(-1,3),colors[:,:12].reshape(-1,3),colors[:,-12:].reshape(-1,3)])
 bg=np.median(edges,axis=0);distance=np.sqrt(((colors-bg)**2).sum(2))
 # L-mode fromarray may be read-only; floodfill silently catches the write
 # error. Copy to writable storage and verify the exterior was actually filled.
 mask=Image.fromarray(np.where(distance<44,255,0).astype('uint8')).copy()
 for corner in [(0,0),(mask.width-1,0),(0,mask.height-1),(mask.width-1,mask.height-1)]:
  if mask.getpixel(corner)==255:ImageDraw.floodfill(mask,corner,128)
 outside=np.array(mask)==128;assert outside.mean()>.03,(bid,'No exterior background removed')
 alpha=np.ones(distance.shape);alpha[outside]=np.clip((distance[outside]-22)/22,0,1)
 # Only remove connected exterior paper. Ivory inside a badge is part of its
 # designed contrast field and must not become transparent against noisy walls.
 rgba=np.dstack([colors.astype('uint8'),(alpha*255).astype('uint8')]);logo=Image.fromarray(rgba,'RGBA')
 bbox=logo.getchannel('A').point(lambda v:255 if v>50 else 0).getbbox();assert bbox
 logo=logo.crop(bbox)
 # Geometry reserves a 2:1 identity area. Lettering is never stretched to fit it.
 art=ImageOps.contain(logo,(2048,1024),Image.Resampling.LANCZOS)
 canvas=Image.new('RGBA',(2048,1024),(0,0,0,0));canvas.paste(art,((2048-art.width)//2,(1024-art.height)//2))
 return canvas

def bake(bid,folder,p,placement=None):
 import numpy as np
 from PIL import Image,ImageFilter,ImageDraw
 b=p.get('grok_branding')
 if not b:return
 source=OUT/'assets'/bid
 with Image.open(source/'material/concept.jpg') as im:raw=im.convert('RGB').resize((1024,1024),Image.Resampling.LANCZOS)
 tile=Image.fromarray(periodic(np.array(raw,dtype=float)).astype('uint8'))
 logo=load_logo(bid)
 h=p['wall_height_m'];number=int(bid[1:]);layout=number%4
 material=b['material'].lower();rough=.3 if any(v in material for v in ('glossy','glazed','stainless','brass')) else .85 if any(v in material for v in ('fabric','rattan','rammed','stucco')) else .68
 for role in ('main','feature','quiet','facade'):
  n=2048 if role=='feature' else 1024;period=p['feature_period_m'] if role=='feature' else p['period_m'];w,hh=period
  if role in ('quiet','facade'):
   # Upstairs remains calm and exterior stays weather-resistant, even if the
   # interior feature uses rattan/fabric. Keep their certified mineral finishes.
   continue
  a=np.asarray(tile);u=((np.arange(n)/n*w/1.5)%1*1023).astype(int);v=(((1-np.arange(n)/n)*hh/1.5)%1*1023).astype(int)
  tex=a[v[:,None],u[None,:]].copy();image=Image.fromarray(tex)
  if role=='feature':
   maxw=min(4.2,w*.42);maxh=min(1.9,h*.53);scale=min(maxw/logo.width,maxh/logo.height)
   lw,lh=logo.width*scale,logo.height*scale
   x=[.8,max(.6,(w-lw)/2),max(.6,w-lw-.8),min(2.8,w-lw-.6)][layout]
   z=[1.15,max(1.0,h-lh-.5),1.05,.85][layout];z=min(z,h-lh-.25)
   if placement:
    y0,z0,y1,z1=placement['logo_bounds_yz_m'];lw=y1-y0;lh=z1-z0;x=y0+w/2;z=z0-placement['floor_z_m']
   width=max(1,round(lw/w*n));height=max(1,round(lh/hh*n));xpx=round(x/w*n);ypx=round((1-(z+lh)/hh)*n)
   art=logo.resize((width,height),Image.Resampling.LANCZOS);image.paste(art,(xpx,ypx),art)
  image.save(folder/f'{role}-basecolor.png')
  # PBR companions are explicitly approximate maps derived from the albedo,
  # never represented as independent AI measurements of physical height.
  lum=np.asarray(Image.fromarray(tex).convert('L'),dtype=float)/255
  blur=np.asarray(Image.fromarray((lum*255).astype('uint8')).filter(ImageFilter.GaussianBlur(2)),dtype=float)/255
  relief=(lum-blur)*.0005
  dx=(np.roll(relief,-1,1)-np.roll(relief,1,1))/(2*w/n);dy=(np.roll(relief,-1,0)-np.roll(relief,1,0))/(2*hh/n)
  normals=np.stack([-dx,dy,np.ones_like(dx)],2);normals/=np.linalg.norm(normals,axis=2)[:,:,None]
  Image.fromarray(((normals*.5+.5)*255).astype('uint8')).save(folder/f'{role}-normal.png')
  Image.fromarray(np.clip((rough+(lum-lum.mean())*.08)*255,0,255).astype('uint8')).save(folder/f'{role}-roughness.png')
  metallic=np.ones(lum.shape) if 'stainless' in material else np.clip((tex[:,:,0].astype(float)-tex[:,:,2].astype(float)-12)/65,0,1) if 'brass' in material else np.zeros(lum.shape)
  Image.fromarray((metallic*255).astype('uint8')).save(folder/f'{role}-metallic.png')
 (folder/'grok-provenance.json').write_text(json.dumps(dict(building=bid,inputs=b,roles=['main','feature'],normal_and_roughness='Approximate albedo-derived microdetail and material-class roughness; not independently generated physical maps',geometry_changed=False,feature_placement=placement),ensure_ascii=False,indent=2),'utf8')

