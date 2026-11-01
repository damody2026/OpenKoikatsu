-- One clean water surface for the city, replacing the flat-campus G??_Water planes. Those meshes were meant to be flat
-- at z 1100 (prepare_street_city_flat_campus.lua sets every water vertex there), but the installed SM_Flat_* meshes
-- reference a corner vertex left at z 0: their "surface" is fans of translucent slopes from 0 to 1100, darker where the
-- slopes stay above the seabed and pale where they sink under it (the world-fixed stair across the river mouth,
-- water_owner_probe 2026-11-01).
-- Rule: water = the flat-campus water raster (world-ground-v1/world-field.npz water_map, 1 m, values 1-3) plus every
-- shore-grid cell under a water surface or with its ground below 1100 (world-edge-v1/shore-grid.json), grown 2 m so the
-- edge tucks under the banks (land stands at 1198). One quad per 1 m cell at z 1100, merged into flat faces.
-- Output world-edge-v1/water-plane/water-plane.glb + build.json (probes, bounds) for the native job
-- world_water_plane_install (actor WorldEdge_Water, M_ocean, G??_Water hidden).
-- Usage: lua Tools/build_street_city_water_plane.lua [--grow 2]
package.path='Tools/lua/?.lua;'..package.path
local j=require('ok.json');local base='C:/portable/OpenKoikatsu/output/street-social-city-production'
local o={grow=2}
for i=1,#arg-1 do local k=arg[i]:match('^%-%-(%w+)$');if k and o[k] then o[k]=tonumber(arg[i+1]) end end
local code=[====[
import bpy,bmesh,json,hashlib,math
import numpy as np
from pathlib import Path
bpy.ops.wm.read_factory_settings(use_empty=True)
B=Path(TASK['base']);T=TASK['opt'];O=B/'world-edge-v1'
W=np.load(B/'world-ground-v1/world-field.npz')['water_map']
# raster row j = 1100 - x/100 (north flipped), column i = y/100 (prepare_street_city_flat_campus.lua); cell (x,y) in metres
N=W.shape[0];X=(W>0)[::-1,:].copy()   # X[x m, y m]
G=json.loads((O/'shore-grid.json').read_text('utf8'));xs=np.array(G['x_cm'])/100;st=G['step_cm']/100
for r in G['rows']:
 y=int(round(r['y']/100-.5))
 if not 0<=y<N:continue
 z=[int(v) for v in r['z'].split()]
 for k,(zz,w) in enumerate(zip(z,r['wet'])):
  x=int(round(xs[k]))
  if 0<=x<N and (w=='1' or 0<=zz<1100):X[x,y]=True
# grow under the banks
g=int(T['grow'])
for _ in range(g):
 Y=X.copy();Y[1:,:]|=X[:-1,:];Y[:-1,:]|=X[1:,:];Y[:,1:]|=X[:,:-1];Y[:,:-1]|=X[:,1:];X=Y
# flat: merge cells into rectangles (runs along y per x row, stacked while the next row repeats the same run); the
# T-junctions this leaves lie in one plane at one height
cells=int(X.sum());rects=[];open_={}
for x in range(N+1):
 runs=set()
 if x<N:
  row=X[x];y=0
  while y<N:
   if row[y]:
    e=y
    while e+1<N and row[e+1]:e+=1
    runs.add((y,e+1));y=e+1
   else:y+=1
 for key in list(open_):
  if key not in runs:rects.append((open_.pop(key),x)+key)
 for key in runs:
  if key not in open_:open_[key]=x
bm=bmesh.new()
for x0,x1,y0,y1 in rects:
 bm.faces.new([bm.verts.new((a,b,11.0)) for a,b in ((x0,y0),(x1,y0),(x1,y1),(x0,y1))])
bmesh.ops.remove_doubles(bm,verts=bm.verts,dist=1e-4)
for f in bm.faces:
 if f.normal.z<0:f.normal_flip()
me=bpy.data.meshes.new('WaterPlane');bm.to_mesh(me);bm.free()
ob=bpy.data.objects.new('WaterPlane',me);bpy.context.collection.objects.link(ob)
m=bpy.data.materials.new('S_Water');m.use_nodes=True;me.materials.append(m)
uv=me.uv_layers.new(name='UVMap')
for lp in me.loops:q=me.vertices[lp.vertex_index].co;uv.data[lp.index].uv=(q.x/4,q.y/4)
me.calc_loop_triangles()
dest=O/'water-plane';dest.mkdir(parents=True,exist_ok=True);glb=dest/'water-plane.glb'
bpy.ops.object.select_all(action='SELECT');bpy.ops.export_scene.gltf(filepath=str(glb),export_format='GLB',use_selection=True,export_apply=True,export_yup=True)
xyz=[q.co for q in me.vertices];n=len(xyz)
# asymmetric probe vertices for the yaw x mirror search; the plane is flat, so pick extreme corners in x+y and x-y
pr=[min(xyz,key=lambda c:c.x+c.y),max(xyz,key=lambda c:c.x+c.y),min(xyz,key=lambda c:c.x-2*c.y),max(xyz,key=lambda c:3*c.x-c.y)]
meta=dict(status='authored_requires_native_install',path=str(glb),sha256=hashlib.sha256(glb.read_bytes()).hexdigest(),opt=T,water_cells=cells,triangles=len(me.loop_triangles),
 bounds_m=[[min(c[k] for c in xyz) for k in range(3)],[max(c[k] for c in xyz) for k in range(3)]],probes_m=[[round(c.x,3),round(c.y,3),round(c.z,3)] for c in pr],shore_grid_map_sha256=G['map_sha256'])
(dest/'build.json').write_text(json.dumps(meta,indent=1),'utf8');print('WATER',cells,'cells',meta['triangles'],'tris',flush=True)
]====]
local path=base..'/world-edge-v1/water-plane-payload.json'
local f=assert(io.open(path,'wb'));f:write(j.encode({base=base,code=code,opt=o}));f:close()
local expr="import json;TASK=json.load(open('"..path.."',encoding='utf8'));exec(TASK['code'])"
assert(os.execute('""C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" --background --factory-startup --python-expr "'..expr..'" > "'..base..'/world-edge-v1/water-plane-build.log" 2>&1"'))
print('Water plane authored; see world-edge-v1/water-plane-build.log')
