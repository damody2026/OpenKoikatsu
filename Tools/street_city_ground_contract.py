"""Metre-space terrain contract, shared world samples and SVG-derived features."""
import json,re,math,hashlib
from pathlib import Path
import numpy as np
from PIL import Image,ImageDraw,ImageFilter
import xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'output/street-social-city-production/world-ground-v1'
SIZE=1100;CHUNK=275;STEP=2.5;KINDS=['grass','urban','campus','forest','riverbank','sand']
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def path_points(path):
 tokens=re.findall(r'[MLHVCSQTZmlhvcsqtz]|[-+]?(?:\d*\.\d+|\d+)(?:[eE][-+]?\d+)?',path);i=0;cmd=None;p=np.array([0.,0.]);start=p.copy();last=None;points=[]
 while i<len(tokens):
  if tokens[i].isalpha():cmd=tokens[i];i+=1
  upper=cmd.upper();relative=cmd.islower();old=p.copy()
  if upper=='Z':points.append(start.tolist());p=start.copy();cmd=None;continue
  count={'M':2,'L':2,'H':1,'V':1,'C':6,'S':4,'Q':4,'T':2}[upper];values=list(map(float,tokens[i:i+count]));i+=count
  def pt(v):return np.array(v)+old if relative else np.array(v)
  if upper in ('M','L'):p=pt(values);points.append(p.tolist());last=None
  elif upper in ('H','V'):
   k=0 if upper=='H' else 1;p[k]=values[0]+old[k] if relative else values[0];points.append(p.tolist());last=None
  else:
   if upper=='C':a,b,p=pt(values[:2]),pt(values[2:4]),pt(values[4:])
   elif upper=='S':a,b,p=2*old-last if last is not None else old,pt(values[:2]),pt(values[2:])
   elif upper=='Q':a,p=pt(values[:2]),pt(values[2:])
   else:a,p=2*old-last if last is not None else old,pt(values)
   steps=max(8,int(np.linalg.norm(p-old)/4))
   for t in np.linspace(0,1,steps+1)[1:]:
    q=(1-t)**3*old+3*(1-t)**2*t*a+3*(1-t)*t*t*b+t**3*p if upper in ('C','S') else (1-t)**2*old+2*(1-t)*t*a+t*t*p
    points.append(q.tolist())
   last=b if upper in ('C','S') else a
  if upper=='M':start=p.copy();cmd='l' if relative else 'L'
 return np.array(points)/2
def prepare():
 OUT.mkdir(parents=True,exist_ok=True);svg=ROOT/'Docs/StreetSocialCity/world-terrain.svg';layout=json.loads((ROOT/'Docs/StreetSocialCity/world-terrain-layout.json').read_text('utf-8'));root=ET.parse(svg).getroot();ns={'s':'http://www.w3.org/2000/svg'};group=next(e for e in root if e.tag.endswith('g') and '100 215' in e.get('transform',''));land=Image.new('L',(1101,1101),0);water=Image.new('L',land.size,0);roadmask=Image.new('L',land.size,0);hills=Image.new('L',land.size,0);campus=Image.new('L',land.size,0);roads=[]
 carmask=Image.new('L',land.size,0);campus_seen=False;hill_colors={'#c5d6b5':30,'#b4caa7':50,'#a9c19e':70,'#9db695':95,'#8fac8c':125}
 for e in group:
  tag=e.tag.split('}')[-1];fill=e.get('fill');stroke=e.get('stroke');width=float(e.get('stroke-width','0'))/2
  if tag=='path':
   pts=path_points(e.get('d'));xy=[tuple(v) for v in pts]
   if not xy:continue
   if fill in hill_colors:ImageDraw.Draw(hills).polygon(xy,fill=hill_colors[fill]);ImageDraw.Draw(land).polygon(xy,fill=3)
   elif fill=='url(#sand)':ImageDraw.Draw(land).polygon(xy,fill=5)
   elif fill=='url(#water)':ImageDraw.Draw(land).polygon(xy,fill=5);ImageDraw.Draw(water).polygon(xy,fill=2)
   elif fill=='#8fbcc6' and campus_seen:ImageDraw.Draw(water).polygon(xy,fill=3);ImageDraw.Draw(land).polygon(xy,fill=4)
   elif fill in ('#e8e6d4','#cdddbc'):ImageDraw.Draw(land).polygon(xy,fill=0);ImageDraw.Draw(hills).polygon(xy,fill=0)
   if stroke in ('#86b3bf','#a7ccd2'):
    ImageDraw.Draw(land).line(xy,fill=4,width=max(1,round(width+28)));ImageDraw.Draw(water).line(xy,fill=1,width=max(1,round(width)))
   if stroke in ('#b4b5aa','#cec9b8','#f2e8cd') and e.get('fill')=='none':
    kind='asphalt' if stroke=='#b4b5aa' else 'urban' if stroke=='#cec9b8' else 'campus';roads.append(dict(id=f'path-{len(roads):03d}',kind=kind,width_m=width,points_map_m=pts.tolist()));ImageDraw.Draw(roadmask).line(xy,fill=1,width=max(1,round(width+4)))
    if kind=='asphalt':ImageDraw.Draw(carmask).line(xy,fill=255,width=max(1,round(width+4)))
  elif tag=='rect' and fill=='#d5dfc6':
   x,y,w,h=[float(e.get(k))/2 for k in ['x','y','width','height']];ImageDraw.Draw(campus).rectangle([x,y,x+w,y+h],fill=255);ImageDraw.Draw(land).rectangle([x,y,x+w,y+h],fill=0)
   campus_seen=True
 # Urban parcel surrounds the accepted paired rows. Heights remain authoritative.
 ImageDraw.Draw(land).rectangle([200,590,535,830],fill=1)
 kinds=np.array(land);water_array=np.array(water);road=np.array(roadmask);elevation=12+np.array(hills.filter(ImageFilter.GaussianBlur(18)),dtype=np.float32);weight=np.array(campus.filter(ImageFilter.GaussianBlur(35)),dtype=np.float32)/255;elevation=elevation*(1-weight)+20*weight
 elevation[kinds==5]=1.5;elevation[water_array==2]=-3
 yy,xx=np.indices(elevation.shape);river_z=np.maximum(0,10*(1-yy/1000));elevation[water_array==1]=(river_z-2)[water_array==1];elevation[(kinds==4)&(water_array==0)]=river_z[(kinds==4)&(water_array==0)]+1.5
 # Continuous banks/shore; extend water UNDER the rising shore so its raster
 # boundary is occluded naturally, rather than exposing a jagged vertical lip.
 water_surface=river_z.copy();water_surface[water_array==2]=0;water_surface[water_array==3]=19
 expanded=water_array.copy()
 for code,radius,land_z,bed_z,level in [(1,4,river_z+1.5,river_z-2,river_z),(2,7,1.5,-3,0),(3,4,20,17.5,19)]:
  mask_image=Image.fromarray(((water_array==code)*255).astype(np.uint8));weight=np.array(mask_image.filter(ImageFilter.GaussianBlur(radius)),dtype=np.float32)/255;near=np.array(mask_image.filter(ImageFilter.MaxFilter(radius*6+1)))>0;target=np.asarray(land_z)*(1-weight)+np.asarray(bed_z)*weight
  if code==1:near&=(kinds==4)|(water_array==1)
  if code==2:near&=kinds==5
  elevation[near]=np.broadcast_to(target,elevation.shape)[near];extension=np.array(mask_image.filter(ImageFilter.MaxFilter(13 if code!=2 else 21)))>0;expanded[extension]=code;water_surface[extension]=np.broadcast_to(level,elevation.shape)[extension]
 water_array=expanded
 # The shop district is a 12m terrace; AI must not change doors' approach level.
 elevation[585:840,190:545]=12;kinds[585:840,190:545]=1
 # Concept pad elevations are not a real topographic survey.
 for b in layout['features']:
  if 'footprint_m' not in b:continue
  cx,cy=b['center_map_m'];w,d=b['footprint_m'];z=b['world_center_m']['up'];x0,x1=max(0,int(cx-w/2-2)),min(SIZE,int(math.ceil(cx+w/2+2)));y0,y1=max(0,int(cy-d/2-2)),min(SIZE,int(math.ceil(cy+d/2+2)));elevation[y0:y1+1,x0:x1+1]=z
  if not b['id'].startswith('S') and z==20:kinds[y0:y1+1,x0:x1+1]=2
 # All vehicle paths share one continuous terrace profile (maximum 8% grade).
 # Water stays below bridges; nearby ground follows the same road-bed datum.
 car=np.array(carmask)>0;blend=np.array(carmask.filter(ImageFilter.MaxFilter(61)).filter(ImageFilter.GaussianBlur(10)),dtype=np.float32)/255;blend[car]=1;design=12+8*np.clip((625-yy)/100,0,1);mask=water_array==0;elevation[mask]=(elevation*(1-blend)+(design-.025)*blend)[mask]
 # Fixed building pads win over neighbouring earthworks.
 for b in layout['features']:
  if 'footprint_m' not in b:continue
  cx,cy=b['center_map_m'];w,d=b['footprint_m'];z=b['world_center_m']['up'];x0,x1=max(0,int(cx-w/2)),min(SIZE,int(math.ceil(cx+w/2)));y0,y1=max(0,int(cy-d/2)),min(SIZE,int(math.ceil(cy+d/2)));elevation[y0:y1+1,x0:x1+1]=z
 shared=np.array([[sample(elevation,x,SIZE-y) for x in np.arange(0,SIZE+.1,STEP)] for y in np.arange(0,SIZE+.1,STEP)],dtype=np.float32)
 np.savez_compressed(OUT/'world-field.npz',height_map=elevation,kind_map=kinds,water_map=water_array,river_water_z=river_z,water_surface_z=water_surface,road_map=road,shared_height=shared)
 chunks=[]
 for row in range(4):
  for col in range(4):
   chunks.append(dict(id=f'G{row*4+col+1:02d}',world_origin_m=[col*CHUNK,row*CHUNK,0],size_m=[CHUNK,CHUNK],grid_step_m=STEP,triangle_cap=100000,edge_contract='global shared grid; maximum pairwise gap 0.002m'))
 plan=dict(schema=1,status='planned_requires_tripo_remesh_and_unreal',size_m=[SIZE,SIZE],axes='source Blender east,north,up metres; Unreal north,east,up centimetres',chunks=chunks,ground_kinds=KINDS,prototype_size_m=[20,20],height_note='Authored interpretation of conceptual SVG; not measured survey',roads=roads,source_svg_sha256=sha(svg),shop_world_sha256=sha(ROOT/'output/street-social-city-production/shop-street-v1/shop-street-complete.glb'))
 (OUT/'ground-plan.json').write_text(json.dumps(plan,ensure_ascii=False,indent=2),'utf-8')
 overlay=['<g stroke="#c73932" fill="none" stroke-width="3" stroke-dasharray="12 8">']
 for c in chunks:
  x,y,_=c['world_origin_m'];sx=100+x*2;sy=215+(SIZE-y-CHUNK)*2;overlay.append(f'<rect x="{sx}" y="{sy}" width="550" height="550"/><text x="{sx+15}" y="{sy+36}" stroke="none" fill="#a72520" font-size="26">{c["id"]} · 275×275m</text>')
 overlay.append('</g>');text=svg.read_text('utf-8');(OUT/'ground-chunks.svg').write_text(text.replace('</svg>',''.join(overlay)+'</svg>'),'utf-8');print('Planned',len(chunks),'chunks;',len(roads),'source SVG routes')
def sample(array,x,y):
 x=max(0,min(SIZE,float(x)));y=max(0,min(SIZE,float(y)));i,j=int(x),int(y);ii,jj=min(SIZE,i+1),min(SIZE,j+1);u,v=x-i,y-j
 return float((1-u)*(1-v)*array[j,i]+u*(1-v)*array[j,ii]+(1-u)*v*array[jj,i]+u*v*array[jj,ii])
if __name__=='__main__':prepare()
