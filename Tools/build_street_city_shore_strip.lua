-- Beach strip v2, along the real waterline in 2D (replaces the per-row strip of Tools/build_street_city_coast_strip.lua,
-- which only knew "first land from the east" per 1 m row and so cut the beach square where the shore turns into the
-- river mouth, A03 2026-10-31). Input world-edge-v1/shore-grid.json (native job world_shore_grid_probe: 1 m raster of
-- the ground top under the water surfaces). Rule:
--   land = ground top >= 1150 cm; blur the land mask (gaussian, sigma `--sigma` m) and take the iso-line at `--level`
--   (below 0.5 = seaward of the 2.5 m stair corners), chain it into polylines (marching squares), drop pieces shorter
--   than 30 m, smooth (Chaikin) and resample every metre;
--   along each polyline lay the old cross profile on the line normal: flat 1 cm over the 1200 land from `--inland` m
--   inland out to the line, then down to 1060 over `--slope` m into the water (z 1100), then on to `--bed` cm over
--   `--shelf` m more, so the shallow water reads as one sandy shelf instead of the 1000/1095 seabed squares.
-- Water bed cells also get a sand sheet 15 cm over the bed (they kept the grass of the land they were pressed from).
-- Output world-edge-v1/coast-strip/coast-strip.glb + build.json in the coast strip contract (probes, bounds), so the
-- native job world_coast_strip_install places it as WorldEdge_CoastStrip (replacing v1).
-- Usage: lua Tools/build_street_city_shore_strip.lua [--dir <world-edge-v1 dir>] [--sigma 2.5] [--level 0.2] [--inland 6] [--slope 6] [--shelf 14] [--bed 1030]
package.path='Tools/lua/?.lua;'..package.path
local j=require('ok.json');local out='C:/portable/OpenKoikatsu/output/street-social-city-production/world-edge-v1'
local o={sigma=2.5,level=0.2,inland=6,slope=6,shelf=14,bed=1030}
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
bm=bmesh.new();cols_all=0
for l,N in kept:
 cols=[]
 for p,(nx_,ny_) in zip(l,N):
  # in a channel the far bank is close: squeeze the slope + shelf into half the gap so the two banks' shelves never
  # overlap (coplanar shelves would flicker under the water)
  far=next((s for s in range(2,int(SL+SH)*2+4) if Fat(p[0]+nx_*s,p[1]+ny_*s)>=L),None)
  k=1.0 if far is None else min(1.0,max(.2,(far/2-.5)/(SL+SH)))
  cols.append([bm.verts.new((p[0]+nx_*(d if d<=0 else d*k),p[1]+ny_*(d if d<=0 else d*k),z)) for d,z in prof])
 for a,b in zip(cols,cols[1:]):
  for k in range(len(prof)-1):bm.faces.new((a[k],a[k+1],b[k+1],b[k]))
 cols_all+=len(cols)
assert cols_all,'no shore line'
# sand bed: the water bed cells keep the material of the ground they were pressed down from (grass squares read as dark
# blue stairs under the shallow water off the river mouth, A03 2026-10-31), so every water bed cell (ground < 1060, water)
# gets one sand sheet 15 cm over the bed, merged into runs along x per 1 m row; the shelf (down to --bed 1030) stays on
# top. A run only merges cells of the same ground height: taking the run's highest cell lifted whole rows of open sea
# to a near-shore stair (1040 -> sheet 1055 next to rows at 1015), and the two water depths read as light and dark
# rectangles off A03 (seabed-layers probe 2026-11-01).
bed=0
for i in range(ny):
 k=0
 while k<nx:
  if 0<=Z[i,k]<1060 and WET[i,k]:
   e=k
   while e+1<nx and Z[i,e+1]==Z[i,k] and WET[i,e+1]:e+=1
   x0,x1=xs[0]+(k-.5)*st,xs[0]+(e+.5)*st;y0,y1=ys[0]+(i-.5)*st,ys[0]+(i+.5)*st;zb=float(Z[i,k])/100+.15
   v=[bm.verts.new((x0,y0,zb)),bm.verts.new((x1,y0,zb)),bm.verts.new((x1,y1,zb)),bm.verts.new((x0,y1,zb))];bm.faces.new(v);bed+=1;k=e+1
  else:k+=1
bmesh.ops.recalc_face_normals(bm,faces=bm.faces)
for f in bm.faces:
 if f.normal.z<0:f.normal_flip()
me=bpy.data.meshes.new('CoastStrip');bm.to_mesh(me);bm.free()
for p in me.polygons:p.use_smooth=True
ob=bpy.data.objects.new('CoastStrip',me);bpy.context.collection.objects.link(ob)
m=bpy.data.materials.new('S_CoastSand');m.use_nodes=True;me.materials.append(m)
uv=me.uv_layers.new(name='UVMap')
for lp in me.loops:v=me.vertices[lp.vertex_index].co;uv.data[lp.index].uv=(v.x/4,v.y/4)
me.calc_loop_triangles();dest=O/'coast-strip';dest.mkdir(parents=True,exist_ok=True);glb=dest/'coast-strip.glb'
bpy.ops.object.select_all(action='SELECT');bpy.ops.export_scene.gltf(filepath=str(glb),export_format='GLB',use_selection=True,export_apply=True,export_yup=True)
xyz=[v.co for v in me.vertices]
probes=[[round(c.x,3),round(c.y,3),round(c.z,3)] for c in (xyz[0],xyz[len(xyz)//2],xyz[-1],xyz[len(prof)*7+2])]
meta=dict(status='authored_requires_native_install',mode='shore-grid-v2',path=str(glb),sha256=hashlib.sha256(glb.read_bytes()).hexdigest(),probe_map_sha256=G['map_sha256'],opt=T,
 triangles=len(me.loop_triangles),lines=len(lines),dropped_dry_lines=dropped,bed_quads=bed,line_lengths_m=sorted([round(length(l)) for l in lines],reverse=True)[:20],bounds_m=[[min(c[k] for c in xyz) for k in range(3)],[max(c[k] for c in xyz) for k in range(3)]],probes_m=probes)
(dest/'build.json').write_text(json.dumps(meta,indent=1),'utf8');print('SHORE',meta['triangles'],'tris lines',meta['lines'],meta['line_lengths_m'][:8],flush=True)
]====]
local path=out..'/shore-strip-payload.json'
local f=assert(io.open(path,'wb'));f:write(j.encode({out=out,code=code,opt=o}));f:close()
local expr="import json;TASK=json.load(open('"..path.."',encoding='utf8'));exec(TASK['code'])"
assert(os.execute('""C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" --background --factory-startup --python-expr "'..expr..'" > "'..out..'/shore-strip-build.log" 2>&1"'))
print('Shore strip authored; see world-edge-v1/shore-strip-build.log')
