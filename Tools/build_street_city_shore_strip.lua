-- Beach strip v2, along the real waterline in 2D (replaces the per-row strip of Tools/build_street_city_coast_strip.lua,
-- which only knew "first land from the east" per 1 m row and so cut the beach square where the shore turns into the
-- river mouth, A03 2026-10-31). Input world-edge-v1/shore-grid.json (native job world_shore_grid_probe: 1 m raster of
-- the ground top under the water surfaces). Rule:
--   land = ground top >= 1150 cm; blur the land mask (gaussian, sigma `--sigma` m) and take the iso-line at `--level`
--   (below 0.5 = seaward of the 2.5 m stair corners), chain it into polylines (marching squares), drop pieces shorter
--   than 30 m, smooth (Chaikin) and resample every metre;
--   along each polyline lay the beach cross profile on the line normal: flat 1 cm over the 1200 land from `--inland` m
--   inland out to the line, then down to 1060 over `--slope` m into the water (z 1100);
--   where the far bank is within `--channel` m (the river mouth) a quay wall replaces the beach (seawall.glb);
--   every water bed cell is covered by one seabed heightfield: the profile continued by the distance to beach land
--   (`--off` m from the land cells to the line), down to `--bed` cm over `--shelf` m more, then 1015 in open water, never
--   under the original ground + 15 cm (the bed cells kept the grass of the land they were pressed from).
-- Output world-edge-v1/coast-strip/coast-strip.glb (+ seawall.glb) + build.json in the coast strip contract (probes,
-- bounds), so the native job world_coast_strip_install places them as WorldEdge_CoastStrip / WorldEdge_Seawall.
-- Usage: lua Tools/build_street_city_shore_strip.lua [--dir <world-edge-v1 dir>] [--sigma 2.5] [--level 0.2] [--inland 6] [--slope 6] [--shelf 14] [--bed 1030] [--channel 80] [--off 2]
package.path='Tools/lua/?.lua;'..package.path
local j=require('ok.json');local out='C:/portable/OpenKoikatsu/output/street-social-city-production/world-edge-v1'
local o={sigma=2.5,level=0.2,inland=6,slope=6,shelf=14,bed=1030,channel=80,off=2}
for i=1,#arg-1 do local k=arg[i]:match('^%-%-(%w+)$');if k and o[k] then o[k]=tonumber(arg[i+1]) elseif k=='dir' then out=arg[i+1] end end
local code=[====[
import bpy,bmesh,json,hashlib,math
import numpy as np
from pathlib import Path
bpy.ops.wm.read_factory_settings(use_empty=True)
O=Path(TASK['out']);G=json.loads((O/'shore-grid.json').read_text('utf8'));T=TASK['opt']
xs=np.array(G['x_cm'],float)/100;ys=np.array([r['y'] for r in G['rows']],float)/100;st=G['step_cm']/100
Z=np.array([[int(v) for v in r['z'].split()] for r in G['rows']],float)   # [y,x] cm
land=(Z>=1150).astype(float)
# water: under a traced water surface (the ocean actor) or on the water bed (the G??_Water planes have no collision, so
# the probe sees their bed: ground pressed to <= 1000 by prepare_street_city_flat_campus.lua) or with no ground at all
WET=np.array([[ch=='1' for ch in r['wet']] for r in G['rows']])|(Z<1060)
def blur(a,s):
 r=int(3*s);k=np.exp(-0.5*(np.arange(-r,r+1)/s)**2);k/=k.sum()
 a=np.apply_along_axis(lambda v:np.convolve(np.pad(v,r,mode='edge'),k,'valid'),1,a)
 return np.apply_along_axis(lambda v:np.convolve(np.pad(v,r,mode='edge'),k,'valid'),0,a)
F=blur(land,T['sigma']/st);L=T['level']
# marching squares on F at level L: one segment per crossed cell edge pair (saddles split by the cell mean)
ny,nx=F.shape;segs=[]
def cross(p,q,fp,fq):t=(L-fp)/(fq-fp);return (p[0]+(q[0]-p[0])*t,p[1]+(q[1]-p[1])*t)
for i in range(ny-1):
 for k in range(nx-1):
  c=[(k,i),(k+1,i),(k+1,i+1),(k,i+1)];f=[F[i,k],F[i,k+1],F[i+1,k+1],F[i+1,k]];b=[v>=L for v in f]
  if all(b) or not any(b):continue
  pts=[cross(c[e],c[(e+1)%4],f[e],f[(e+1)%4]) for e in range(4) if b[e]!=b[(e+1)%4]]
  if len(pts)==2:segs.append((pts[0],pts[1]))
  elif len(pts)==4:
   if (sum(f)/4>=L)==b[0]:segs+=[(pts[0],pts[1]),(pts[2],pts[3])]
   else:segs+=[(pts[0],pts[3]),(pts[1],pts[2])]
# chain segments into polylines through shared end points
key=lambda p:(round(p[0],4),round(p[1],4));adj={}
for n,(a,b) in enumerate(segs):adj.setdefault(key(a),[]).append(n);adj.setdefault(key(b),[]).append(n)
used=[False]*len(segs);lines=[]
for n in range(len(segs)):
 if used[n]:continue
 used[n]=True;line=[segs[n][0],segs[n][1]]
 for end in (1,0):
  while True:
   p=line[-1] if end else line[0];nxt=None
   for m in adj[key(p)]:
    if not used[m]:nxt=m;break
   if nxt is None:break
   used[nxt]=True;a,b=segs[nxt];q=b if key(a)==key(p) else a
   if end:line.append(q)
   else:line.insert(0,q)
 lines.append(line)
# grid index -> world metres
def w(p):return (xs[0]+p[0]*st,ys[0]+p[1]*st)
def length(l):return sum(math.dist(l[i],l[i+1]) for i in range(len(l)-1))
lines=[[w(p) for p in l] for l in lines];lines=[l for l in lines if length(l)>=30]
def chaikin(l,n):
 closed=math.dist(l[0],l[-1])<1e-6
 for _ in range(n):
  q=[] if closed else [l[0]]
  for a,b in zip(l,l[1:]):q+=[(.75*a[0]+.25*b[0],.75*a[1]+.25*b[1]),(.25*a[0]+.75*b[0],.25*a[1]+.75*b[1])]
  l=q+([q[0]] if closed else [l[-1]])
 return l
def resample(l,d=1.0):
 out=[l[0]];acc=0.0
 for a,b in zip(l,l[1:]):
  s=math.dist(a,b);t=d-acc
  while t<=s:out.append((a[0]+(b[0]-a[0])*t/s,a[1]+(b[1]-a[1])*t/s));t+=d
  acc=(acc+s)%d
 return out
# the 2.5 m stairs still leave 1-2 m bumps after the blur: a +-5 m moving average (ends fixed) keeps the cross sections
# from folding over each other on the inside of a bump
def relax(l,r=5,n=3):
 for _ in range(n):l=[l[0]]+[(sum(q[0] for q in l[max(0,i-r):i+r+1])/len(l[max(0,i-r):i+r+1]),sum(q[1] for q in l[max(0,i-r):i+r+1])/len(l[max(0,i-r):i+r+1])) for i in range(1,len(l)-1)]+[l[-1]]
 return l
lines=[resample(relax(resample(chaikin(l,3)))) for l in lines]
# seaward normal: the side where the blurred land mask is lower
def Fat(x,y):
 k=min(max(int(round((x-xs[0])/st)),0),nx-1);i=min(max(int(round((y-ys[0])/st)),0),ny-1);return F[i,k]
IN,SL,SH,BED=T['inland'],T['slope'],T['shelf'],T['bed']/100
prof=[(-IN,12.01),(0.0,12.01),(SL*.35,11.62),(SL*.7,11.12),(SL,10.60),(SL+SH,BED)]
def normals(l):
 out=[]
 for i,p in enumerate(l):
  a,b=l[max(0,i-2)],l[min(len(l)-1,i+2)];tx,ty=b[0]-a[0],b[1]-a[1];n=math.hypot(tx,ty) or 1;nx_,ny_=ty/n,-tx/n
  if Fat(p[0]+2*nx_,p[1]+2*ny_)>Fat(p[0]-2*nx_,p[1]-2*ny_):nx_,ny_=-nx_,-ny_
  out.append((nx_,ny_))
 return out
def wet_at(x,y):
 k=min(max(int(round((x-xs[0])/st)),0),nx-1);i=min(max(int(round((y-ys[0])/st)),0),ny-1);return WET[i,k]
# only shores of real water: a low pit or a pool basin without a water surface over it gets no beach
kept=[]
for l in lines:
 N=normals(l)
 if sum(wet_at(p[0]+5*n[0],p[1]+5*n[1]) for p,n in zip(l,N))>=.5*len(l):kept.append((l,N))
dropped=len(lines)-len(kept);lines=[l for l,_ in kept]
# channel banks (the far bank within --channel m along the seaward normal) get a quay wall instead of a beach: sand on
# both sides of the ~50 m river mouth read as a flooded beach, and its shelves ended in stairs (A03 2026-11-01)
CH=T['channel']
def far_bank(p,n):return next((s for s in range(2,int(CH)+1) if Fat(p[0]+n[0]*s,p[1]+n[1]*s)>=L),None)
# the lines keep the land part of the cross profile down to the waterline slope (d <= --slope); everything under the
# water is the seabed heightfield below, so no shelf ends in a stair where a line stops
lprof=[q for q in prof if q[0]<=SL]
bm=bmesh.new();wm=bmesh.new();cols_all=0;quay=[]
for l,N in kept:
 runs=[[]]
 for p,n in zip(l,N):
  q=far_bank(p,n) is not None
  if runs[-1] and runs[-1][-1][2]!=q:runs.append([runs[-1][-1][:2]+(q,)])
  runs[-1].append((p,n,q))
 for run in runs:
  if len(run)<2:continue
  if run[-1][2]:
   # quay: battered face from under the seabed to 30 cm over the land top, 1.2 m cap, back face into the land
   cols=[[wm.verts.new((p[0]+n[0]*d,p[1]+n[1]*d,z)) for d,z in ((0.3,BED-.2),(0.0,12.30),(-1.2,12.30),(-1.2,11.90))] for p,n,_ in run]
   for a,b in zip(cols,cols[1:]):
    for k in range(3):wm.faces.new((a[k],a[k+1],b[k+1],b[k]))
   quay+=[p for p,_,_ in run]
  else:
   cols=[[bm.verts.new((p[0]+n[0]*d,p[1]+n[1]*d,z)) for d,z in lprof] for p,n,_ in run]
   for a,b in zip(cols,cols[1:]):
    for k in range(len(lprof)-1):bm.faces.new((a[k],a[k+1],b[k+1],b[k]))
   cols_all+=len(cols)
assert cols_all,'no shore line'
# seabed: one heightfield over every water bed cell (ground < 1060 under water). Height = the cross profile continued
# by the distance to the nearest beach land (--off m from the land cells to the line): slope to 10.60, shelf to --bed
# over --shelf m, then 10.15 in open water; never under the original ground + 15 cm (the bed cells kept the grass of the
# land they were pressed from). Land within 20 m of a quay (4 m still left a shallow shelf along it) is not beach land, so the bed stays deep at a quay face.
# Replaces the per-row sand runs, whose heights followed the old terrace stairs (light / dark rectangles off A03,
# 2026-11-01) and the shelf quads, whose ends drew stairs at the river mouth.
src=land.astype(bool).copy()
for p in quay:
 k=int(round((p[0]-xs[0])/st));i=int(round((p[1]-ys[0])/st));r=int(math.ceil(20/st))
 src[max(0,i-r):i+r+1,max(0,k-r):k+r+1]=False
# chamfer distance (1, sqrt 2) in cells, two passes, then a light blur
INF=1e9;D=np.where(src,0.0,INF).tolist();s2=math.sqrt(2)
for i in range(ny):
 R=D[i];P=D[i-1] if i else None
 for k in range(nx):
  v=R[k]
  if k and R[k-1]+1<v:v=R[k-1]+1
  if P is not None:
   if P[k]+1<v:v=P[k]+1
   if k and P[k-1]+s2<v:v=P[k-1]+s2
   if k+1<nx and P[k+1]+s2<v:v=P[k+1]+s2
  R[k]=v
for i in range(ny-1,-1,-1):
 R=D[i];P=D[i+1] if i+1<ny else None
 for k in range(nx-1,-1,-1):
  v=R[k]
  if k+1<nx and R[k+1]+1<v:v=R[k+1]+1
  if P is not None:
   if P[k]+1<v:v=P[k]+1
   if k+1<nx and P[k+1]+s2<v:v=P[k+1]+s2
   if k and P[k-1]+s2<v:v=P[k-1]+s2
  R[k]=v
Dm=blur(np.minimum(np.array(D),200.0)*st,1.0/st)
ds=[d for d,_ in prof]+[SL+SH+20];zs=[z for _,z in prof]+[10.15]
zf=np.maximum(Z/100+.15,np.interp(Dm-T['off'],ds,zs))
bedc=(Z>=0)&(Z<1060)&WET
# leftover ground seaward of the beach line (the blur drops 1-3 m land patches, which then stood in the water as bare
# terrain boxes next to the lighthouse, A03 2026-11-01): each such cell (ground over the bed, under 13 m) is buried in a
# sand mound, its height + 15 cm falling 25 cm per metre into the seabed around it
isl=(Z>=1060)&(Z<1300)&(F<L)
Iz=np.where(isl,Z/100+.15,-1e9)
for _ in range(10):
 Jz=Iz.copy();Jz[1:,:]=np.maximum(Jz[1:,:],Iz[:-1,:]-.25);Jz[:-1,:]=np.maximum(Jz[:-1,:],Iz[1:,:]-.25);Jz[:,1:]=np.maximum(Jz[:,1:],Iz[:,:-1]-.25);Jz[:,:-1]=np.maximum(Jz[:,:-1],Iz[:,1:]-.25);Iz=Jz
mound=(Iz>zf)&(Iz>10.15)&((WET|isl)|(Z<1060))
zf=np.where(mound,np.maximum(zf,Iz),zf);bedc=bedc|mound
# 2 x 2 cell blocks (a 1 m heightfield was 296k triangles): a block covers its bed cells at their highest height
S=2;by,bx=(ny+S-1)//S,(nx+S-1)//S
zp=np.full((by*S,bx*S),-1e9);zp[:ny,:nx]=np.where(bedc,zf,-1e9);zp=zp.reshape(by,S,bx,S).max(axis=(1,3));bb=zp>-1e8
hm=bmesh.new();cv={}
def corner(i,k):
 # corner between blocks (i-1..i, k-1..k): the highest adjacent bed block, so no cell pokes through
 if (i,k) not in cv:
  z=max(zp[a,b] for a in (i-1,i) for b in (k-1,k) if 0<=a<by and 0<=b<bx and bb[a,b])
  cv[(i,k)]=hm.verts.new((xs[0]+(k*S-.5)*st,ys[0]+(i*S-.5)*st,z))
 return cv[(i,k)]
bed=0
for i in range(by):
 for k in range(bx):
  if bb[i,k]:hm.faces.new((corner(i,k),corner(i,k+1),corner(i+1,k+1),corner(i+1,k)));bed+=1
# merge the flat open water (and every planar run) into large faces
bmesh.ops.dissolve_limit(hm,angle_limit=math.radians(0.3),use_dissolve_boundaries=False,verts=hm.verts,edges=hm.edges)
tmp=bpy.data.meshes.new('bed');hm.to_mesh(tmp);hm.free();bm.from_mesh(tmp)
# inland water beyond the shore grid (lakes and rivers past x0+300 m showed grass squares under the water, A13
# 2026-11-01): world-edge-v1/water-bed.json (native world_water_bed_probe, ground under every water-plane cell) gets the
# same heightfield with a bank profile instead of a beach one (no beach lines inland): 10.85 at the bank, 10.15 from
# 10 m out, never under the ground + 15 cm. Land = every cell outside the water plane.
inland=0
WB=O/'water-bed.json'
if WB.exists():
 R=json.loads(WB.read_text('utf8'));NW=1101;Zw=np.full((NW,NW),-1.0)
 for r in R['rows']:
  z=[int(v) for v in r['z'].split()];Zw[r['x'],r['y0']:r['y0']+len(z)]=z
 wet=Zw>-1;D=np.where(wet,INF,0.0).tolist()
 for i in range(NW):
  Rr=D[i];P=D[i-1] if i else None
  for k in range(NW):
   v=Rr[k]
   if k and Rr[k-1]+1<v:v=Rr[k-1]+1
   if P is not None:
    if P[k]+1<v:v=P[k]+1
    if k and P[k-1]+s2<v:v=P[k-1]+s2
    if k+1<NW and P[k+1]+s2<v:v=P[k+1]+s2
   Rr[k]=v
 for i in range(NW-1,-1,-1):
  Rr=D[i];P=D[i+1] if i+1<NW else None
  for k in range(NW-1,-1,-1):
   v=Rr[k]
   if k+1<NW and Rr[k+1]+1<v:v=Rr[k+1]+1
   if P is not None:
    if P[k]+1<v:v=P[k]+1
    if k+1<NW and P[k+1]+s2<v:v=P[k+1]+s2
    if k and P[k-1]+s2<v:v=P[k-1]+s2
   Rr[k]=v
 Di=blur(np.minimum(np.array(D),60.0),1.0)
 zi=np.maximum(Zw/100+.15,np.interp(Di,[0,2,10],[10.85,10.60,10.15]))
 x_end=int(math.floor(xs[-1]))   # the shore grid's last row: a 1 m overlap leaves no seam
 bi=wet&(Zw>=0)&(Zw<1060);bi[:x_end,:]=False
 by2,bx2=(NW+S-1)//S,(NW+S-1)//S
 zq=np.full((by2*S,bx2*S),-1e9);zq[:NW,:NW]=np.where(bi,zi,-1e9);zq=zq.reshape(by2,S,bx2,S).max(axis=(1,3));bq=zq>-1e8
 im2=bmesh.new();cw={}
 def corner2(i,k):
  # x row i (metres / S), y column k; the highest adjacent bed block
  if (i,k) not in cw:
   z=max(zq[a,b] for a in (i-1,i) for b in (k-1,k) if 0<=a<by2 and 0<=b<bx2 and bq[a,b])
   cw[(i,k)]=im2.verts.new((i*S,k*S,z))
  return cw[(i,k)]
 for i in range(by2):
  for k in range(bx2):
   if bq[i,k]:im2.faces.new((corner2(i,k),corner2(i+1,k),corner2(i+1,k+1),corner2(i,k+1)));inland+=1
 tmp2=bpy.data.meshes.new('inland');im2.to_mesh(tmp2);im2.free();bm.from_mesh(tmp2)
def finish(b,name,matname,rgb):
 bmesh.ops.recalc_face_normals(b,faces=b.faces)
 for f in b.faces:
  if f.normal.z<-1e-3:f.normal_flip()
 me=bpy.data.meshes.new(name);b.to_mesh(me);b.free()
 for p in me.polygons:p.use_smooth=name=='CoastStrip'
 ob=bpy.data.objects.new(name,me);bpy.context.collection.objects.link(ob)
 m=bpy.data.materials.new(matname);m.use_nodes=True;m.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value=(*rgb,1);me.materials.append(m)
 uv=me.uv_layers.new(name='UVMap')
 for lp in me.loops:v=me.vertices[lp.vertex_index].co;uv.data[lp.index].uv=((v.x+v.z)/4,(v.y+v.z)/4)
 me.calc_loop_triangles();return ob
ob=finish(bm,'CoastStrip','S_CoastSand',(.76,.70,.56))
wall=finish(wm,'Seawall','S_Seawall',(.52,.52,.50)) if quay else None
dest=O/'coast-strip';dest.mkdir(parents=True,exist_ok=True)
def export(o,glb):
 bpy.ops.object.select_all(action='DESELECT');o.select_set(True)
 bpy.ops.export_scene.gltf(filepath=str(glb),export_format='GLB',use_selection=True,export_apply=True,export_yup=True)
 me=o.data;xyz=[v.co for v in me.vertices];n=len(xyz)
 # asymmetric probe vertices for the native yaw x mirror search
 probes=[[round(c.x,3),round(c.y,3),round(c.z,3)] for c in (xyz[0],xyz[n//3],xyz[n//2],xyz[-1])]
 return dict(path=str(glb),sha256=hashlib.sha256(glb.read_bytes()).hexdigest(),triangles=len(me.loop_triangles),bounds_m=[[min(c[k] for c in xyz) for k in range(3)],[max(c[k] for c in xyz) for k in range(3)]],probes_m=probes)
# review image (1 px = 1 cell, north up): land green, seabed blue (darker = deeper), quay points red
px=np.zeros((ny,nx,4));px[...,3]=1
px[land.astype(bool)]=(.25,.55,.2,1)
t=np.clip((zf-10.15)/(11.0-10.15),0,1);bl=bedc&~land.astype(bool)
for c,(lo,hi) in enumerate(((.05,.75),(.15,.85),(.35,.95))):px[...,c]=np.where(bl,lo+(hi-lo)*t,px[...,c])
for p in quay:
 k=int(round((p[0]-xs[0])/st));i=int(round((p[1]-ys[0])/st))
 if 0<=i<ny and 0<=k<nx:px[i,k]=(1,0,0,1)
im=bpy.data.images.new('seabed',nx,ny);im.pixels.foreach_set(px.astype(np.float32).ravel());im.filepath_raw=str(dest/'seabed-review.png');im.file_format='PNG';im.save()
meta=export(ob,dest/'coast-strip.glb')
meta.update(status='authored_requires_native_install',mode='shore-grid-v3',probe_map_sha256=G['map_sha256'],opt=T,lines=len(lines),dropped_dry_lines=dropped,bed_cells=bed,inland_blocks=inland,quay_points=len(quay),line_lengths_m=sorted([round(length(l)) for l in lines],reverse=True)[:20])
if wall:meta['seawall']=export(wall,dest/'seawall.glb')
(dest/'build.json').write_text(json.dumps(meta,indent=1),'utf8');print('SHORE',meta['triangles'],'tris lines',meta['lines'],'quay',len(quay),'wall',meta.get('seawall',{}).get('triangles'),flush=True)
]====]
local path=out..'/shore-strip-payload.json'
local f=assert(io.open(path,'wb'));f:write(j.encode({out=out,code=code,opt=o}));f:close()
local expr="import json;TASK=json.load(open('"..path.."',encoding='utf8'));exec(TASK['code'])"
assert(os.execute('""C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" --background --factory-startup --python-expr "'..expr..'" > "'..out..'/shore-strip-build.log" 2>&1"'))
print('Shore strip authored; see world-edge-v1/shore-strip-build.log')
