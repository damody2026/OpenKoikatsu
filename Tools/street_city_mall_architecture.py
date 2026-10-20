"""Mall-specific facade hierarchy and metric exportable PBR, never shared paint overrides.

All architectural geometry is transformed from the certified remeshed panel.
Cladding is contained in the original wall/slab thickness; apertures stay real.
"""
from pathlib import Path
import hashlib

SPECS={
 'limestone':dict(name='M1 honed limestone 1200x600',repeat_m=[1.2,.6],grout_m=.004,roughness=.55,color=[193,178,149]),
 'ceramic':dict(name='M1 upper ivory rainscreen 1200x900',repeat_m=[1.2,.9],grout_m=.008,roughness=.42,color=[217,211,194]),
 'darkstone':dict(name='M1 charcoal stone plinth 1200x600',repeat_m=[1.2,.6],grout_m=.004,roughness=.48,color=[59,66,67]),
 'bronze':dict(name='M1 satin bronze architectural metal',repeat_m=[1,1],grout_m=0,roughness=.32,color=[128,105,68]),
 'wetwall':dict(name='M1 sanitary porcelain 300x600',repeat_m=[.3,.6],grout_m=.002,roughness=.3,color=[219,224,218]),
 'interiorstone':dict(name='M1 public travertine 1200x600',repeat_m=[1.2,.6],grout_m=.003,roughness=.5,color=[215,201,176]),
 'mallfloor':dict(name='M1 terrazzo floor 1200mm',repeat_m=[1.2,1.2],grout_m=.003,roughness=.4,color=[201,198,184]),
 'mallpaint':dict(name='M1 upper washable plaster',repeat_m=[1,1],grout_m=0,roughness=.72,color=[229,225,214]),
}

def generate(folder):
 import numpy as np
 from PIL import Image
 folder=Path(folder);folder.mkdir(parents=True,exist_ok=True);n=2048
 yy,xx=np.mgrid[:n,:n];rng=np.random.default_rng(20261017)
 for key,s in SPECS.items():
  noise=rng.normal(0,1,(n,n)).astype('float32')
  vein=np.sin(yy*.014+np.sin(xx*.003)*3)+.4*np.sin(yy*.049+xx*.004)
  strength=2 if key in ('limestone','interiorstone','darkstone') else .5
  v=vein*strength+noise*.7
  albedo=np.clip(np.array(s['color'])[None,None,:]+v[:,:,None],0,255).astype('uint8')
  if key=='mallfloor':
   # Fine aggregate in a seamless original terrazzo map; no baked illumination.
   chip=(np.sin(xx*.37+np.cos(yy*.18)) * np.cos(yy*.41+np.sin(xx*.13)))>.94
   albedo[chip]=[153,151,143]
  height=noise*.000007;grout=np.zeros((n,n),dtype=bool)
  if s['grout_m']:
   gx=max(1,round(n*s['grout_m']/s['repeat_m'][0]/2));gy=max(1,round(n*s['grout_m']/s['repeat_m'][1]/2))
   grout[:gy,:]=True;grout[-gy:,:]=True;grout[:,:gx]=True;grout[:,-gx:]=True
   albedo[grout]=np.array(s['color'])*.58;height[grout]=-.0005
  dx=(np.roll(height,-1,1)-np.roll(height,1,1))/(2*s['repeat_m'][0]/n);dy=(np.roll(height,-1,0)-np.roll(height,1,0))/(2*s['repeat_m'][1]/n)
  normal=np.stack([-dx,dy,np.ones_like(dx)],2);normal/=np.linalg.norm(normal,axis=2)[:,:,None]
  rough=np.clip(s['roughness']*255+noise,0,255).astype('uint8');rough[grout]=225
  for suffix,array in [('basecolor',albedo),('normal',np.clip((normal*.5+.5)*255,0,255).astype('uint8')),('roughness',rough)]:Image.fromarray(array).save(folder/f'{key}-{suffix}-2k.png')

def materials(folder):
 import bpy
 folder=Path(folder);result={}
 for key,s in SPECS.items():
  m=bpy.data.materials.new(s['name']);m.use_nodes=True;bs=m.node_tree.nodes['Principled BSDF'];links=m.node_tree.links
  bs.inputs['Metallic'].default_value=.78 if key=='bronze' else 0
  for suffix,socket in [('basecolor','Base Color'),('roughness','Roughness'),('normal','Normal')]:
   tex=m.node_tree.nodes.new('ShaderNodeTexImage');tex.image=bpy.data.images.load(str(folder/f'{key}-{suffix}-2k.png'),check_existing=True);tex.image.pack();tex.extension='REPEAT'
   if suffix!='basecolor':tex.image.colorspace_settings.name='Non-Color'
   if suffix=='normal':
    nm=m.node_tree.nodes.new('ShaderNodeNormalMap');links.new(tex.outputs['Color'],nm.inputs['Color']);links.new(nm.outputs['Normal'],bs.inputs[socket])
   else:links.new(tex.outputs['Color'],bs.inputs[socket])
  m['finish_role']=key;m['repeat_m']=s['repeat_m'];result[key]=m
 for key,filename in [('mallbrand','mallbrand-2k.png'),('campaign','campaign-2k.png')]:
  m=bpy.data.materials.new('M1 '+key+' architectural graphic');m.use_nodes=True;bs=m.node_tree.nodes['Principled BSDF'];tex=m.node_tree.nodes.new('ShaderNodeTexImage');tex.image=bpy.data.images.load(str(folder/filename));tex.image.pack();m.node_tree.links.new(tex.outputs['Color'],bs.inputs['Base Color']);bs.inputs['Roughness'].default_value=.5;result[key]=m
 return result

def generate_graphics(folder):
 from street_city_localization import render,LOCALES,path_for
 import shutil
 folder=Path(folder)
 for key,filename in [('shared.mall.brand','mallbrand-2k.png'),('shared.mall.campaign','campaign-2k.png')]:
  for locale in LOCALES:render(key,locale,path_for(key,locale))
  shutil.copy2(path_for(key,'zh-Hant'),folder/filename)

def finish(q):
 kind=q['kind'];name=q['name'];x,y,z=q['pos']
 if q['finish'].startswith('lux_'):return q['finish']
 if kind=='wall':
  if name.startswith('envelope'):
   # Side elevations use a charcoal central feature field rather than blank white slabs.
   if abs(x)>74 and abs(y)<22:return 'darkstone'
   return 'limestone' if q['floor']<=3 else 'ceramic'
  if 'WC' in name or 'kitchen' in name or 'cubicle' in name:return 'wetwall'
  return 'interiorstone'
 if kind=='column':return 'interiorstone'
 if kind in ('floor','stair_tread','stair_landing'):return 'mallfloor'
 if kind=='ceiling':return 'ceramic'
 if kind=='wc_partition':return 'wetwall'
 return q['finish']

def decorate(parts,c):
 def add(n,name,size,pos,mat,kind='facade_detail',yaw=0):
  parts.append(dict(floor=n,name=name,size=list(size),pos=list(pos),finish=mat,kind=kind,yaw=yaw,primitive='panel'))
 for f in c['floors']:
  n=f['floor'];z=f['z_m']
  # Continuous bronze spandrel ribbons occupy existing slab thickness.
  # Slab edges themselves receive bronze; do not overlay coplanar ribbon boxes.
  windows=[q for q in parts if q['floor']==n and q['kind']=='window']
  for j,q in enumerate(windows):
   x,y,zz=q['pos'];w,d,h=q['size'];horizontal=w>d
   # Three mullions define real glass subdivisions; the opening is not filled with painted white wall.
   for offset in [-1,0,1]:
    if horizontal:add(n,f'window-{j}-mullion',[.065,.09,h],[x+offset*w/3,y,zz],'bronze')
    else:add(n,f'window-{j}-mullion',[.09,.065,h],[x,y+offset*d/3,zz],'bronze')
  for yy in [-49.96,49.96]:
   for xx in [-70,-35,0,35,70]:
    if yy<0 and (xx==0 or (4<=n<=6 and abs(xx)==70)):continue # entrance and multi-storey identity field
    add(n,'stone-pier',[.48,.08,3.9],[xx,yy,z],'darkstone' if xx==0 else 'limestone')
  for xx in [-74.96,74.96]:
   for yy in [-45,-22.5,0,22.5,45]:
    if yy==0:continue # keep the centre identity panel free of coincident fins
    add(n,'side-stone-pier',[.08,.48,3.9],[xx,yy,z],'limestone')
   # Two-storey visual groupings, plus a low-poly vertical metal screen across opaque centre panels.
   for yy in [-8,-6,-4,4,6,8]:
    if xx>0 and n in (6,7) and abs(yy)<=6:continue
    add(n,'side-bronze-fin',[.08,.09,3.7],[xx,yy,z+.1],'bronze')
  # Columns receive a real contrasting stone base and bronze capital, within a 30mm surround.
  # These 24 public-facing columns carry the concourse hierarchy, not every hidden stockroom pillar.
  for xx in c['column_grid_m']['x']:
   for yy in [-12,12]:
    add(n,'public-column-base',[.63,.63,.18],[xx,yy,z],'darkstone','interior_detail')
    add(n,'public-column-cap',[.63,.63,.07],[xx,yy,z+3.72],'bronze','interior_detail')
  # Framed entrance portals and overhead soffit remain inside the 150x100m envelope.
  if n==1:
   for side,x,y,yaw in [('south',0,-49.85,0),('west',-74.85,0,90),('east',74.85,0,90)]:
    for offset in [-4.1,4.1]:
     add(n,'entrance-portal-'+side,[.5,.3,7.4],[x+(offset if yaw==0 else 0),y+(offset if yaw else 0),z],'darkstone',yaw=yaw)
    add(n,'entrance-portal-head-'+side,[8.7,.3,.6],[x,y,z+7.4],'darkstone',yaw=yaw)
    add(n,'entrance-light-'+side,[7.5,.08,.035],[x,y,z+7.34],'light',yaw=yaw)
    # Actual textured bronze-framed fascia, not a floating text annotation.
    add(n,'entrance-mall-identity-'+side,[7.2,.025,2.4],[x+(0 if yaw==0 else -.13 if x<0 else .13),y+(-.13 if yaw==0 else 0),z+4.7],'mallbrand',yaw=yaw)
  if n==4:
   # Large advertising fields are normal retail architecture. Keep true window bays unblocked.
   for xx in [-70,70]:
    add(n,'vertical-campaign-frame',[5.5,.06,11.4],[xx,-49.93,z+.3],'bronze')
    add(n,'vertical-campaign-face',[5.2,.025,11.1],[xx,-49.98,z+.45],'campaign')
   add(n,'upper-mall-identity',[.025,12,5],[74.98,0,z+9],'mallbrand',yaw=0)
  if n==10:
   for yy in [-49.9,49.9]:add(n,'roof-coping',[150,.2,.2],[0,yy,41.8],'darkstone')
   for xx in [-74.9,74.9]:add(n,'roof-coping',[.2,99.6,.2],[xx,0,41.8],'darkstone')

def receipt(folder):
 folder=Path(folder)
 return dict(version=2,texture_resolution=2048,texture_directory=str(folder.resolve()),units='metres',specifications=SPECS,files={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in folder.glob('*-2k.png')},limitations=['Original procedural material maps, not product scans; reference-inspired fictional building'])
