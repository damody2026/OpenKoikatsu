"""Build exact continuous chunks from verified Tripo surfaces and geographic SVG."""
import bpy,sys,json,math,argparse
import numpy as np
from pathlib import Path
from mathutils import Vector
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_remesh_gate import verify,sha
from street_city_paving_union import PavingUnion
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'output/street-social-city-production/world-ground-v1';plan=json.loads((OUT/'ground-plan.json').read_text('utf-8'))
with np.load(OUT/'world-field.npz') as archive:field={key:archive[key] for key in archive.files}
N=110;STEP=2.5;SIZE=1100;KINDS=plan['ground_kinds'];world=json.loads((ROOT/'output/street-social-city-production/shop-street-v1/world-assembly.json').read_text('utf-8'))
p=argparse.ArgumentParser();p.add_argument('--ids',nargs='+');args=p.parse_args(sys.argv[sys.argv.index('--')+1:]);bpy.ops.wm.read_factory_settings(use_empty=True)
def at(array,x,y):
 x=max(0,min(SIZE,float(x)));y=max(0,min(SIZE,float(y)));i,j=int(x),int(y);u,v=x-i,y-j;ii,jj=min(SIZE,i+1),min(SIZE,j+1);return float((1-u)*(1-v)*array[j,i]+u*(1-v)*array[j,ii]+(1-u)*v*array[jj,i]+u*v*array[jj,ii])
surfaces={};mats=[]
for kind in KINDS:
 folder=OUT/'assets'/kind;s=json.loads((folder/'surface.json').read_text('utf-8'));assert s['source']['sha256']==verify(folder/'remesh')['sha256'];surfaces[kind]=s;m=bpy.data.materials.new('Tripo-ground-'+kind);m.use_nodes=True;bs=m.node_tree.nodes.get('Principled BSDF')
 for channel,socket in [('basecolor','Base Color'),('roughness','Roughness'),('normal','Normal')]:
  row=s['maps'][channel];assert sha(row['path'])==row['sha256'];im=bpy.data.images.load(row['path'],check_existing=True);im.colorspace_settings.name='sRGB' if channel=='basecolor' else 'Non-Color';im.pack();tex=m.node_tree.nodes.new('ShaderNodeTexImage');tex.image=im;tex.extension='REPEAT'
  if channel=='normal':normal=m.node_tree.nodes.new('ShaderNodeNormalMap');m.node_tree.links.new(tex.outputs['Color'],normal.inputs['Color']);m.node_tree.links.new(normal.outputs['Normal'],bs.inputs[socket])
  else:m.node_tree.links.new(tex.outputs['Color'],bs.inputs[socket])
 mats.append(m)
asphalt=bpy.data.materials.new('Road-asphalt-metric');asphalt.use_nodes=True;bs=asphalt.node_tree.nodes.get('Principled BSDF');bs.inputs['Base Color'].default_value=(.065,.073,.079,1);bs.inputs['Roughness'].default_value=.92;noise=asphalt.node_tree.nodes.new('ShaderNodeTexNoise');noise.inputs['Scale'].default_value=180;bump=asphalt.node_tree.nodes.new('ShaderNodeBump');bump.inputs['Strength'].default_value=.13;bump.inputs['Distance'].default_value=.002;asphalt.node_tree.links.new(noise.outputs['Fac'],bump.inputs['Height']);asphalt.node_tree.links.new(bump.outputs['Normal'],bs.inputs['Normal']);mats.append(asphalt)
watermat=bpy.data.materials.new('River-and-sea-water');watermat.use_nodes=True;bs=watermat.node_tree.nodes.get('Principled BSDF');bs.inputs['Base Color'].default_value=(.045,.23,.29,1);bs.inputs['Roughness'].default_value=.12;bs.inputs['Metallic'].default_value=.05
gx,gy=np.meshgrid(np.arange(441)*STEP,np.arange(441)*STEP);H=field['shared_height'].astype(np.float64).copy();kind_grid=field['kind_map'][np.rint(SIZE-gy).astype(int),np.rint(gx).astype(int)];water_grid=field['water_map'][np.rint(SIZE-gy).astype(int),np.rint(gx).astype(int)]
for index,kind in enumerate(KINDS):
 data=np.load(OUT/'assets'/kind/'height-field.npy');u=(gx%16)/16;v=(gy%16)/16;ix=np.minimum(63,(u*64).astype(int));iy=np.minimum(63,(v*64).astype(int));fx=u*64-ix;fy=v*64-iy;detail=(1-fx)*(1-fy)*data[iy,ix]+fx*(1-fy)*data[iy,ix+1]+(1-fx)*fy*data[iy+1,ix]+fx*fy*data[iy+1,ix+1];limit={'urban':0,'campus':0,'grass':.03,'forest':.3,'riverbank':.05,'sand':.015}[kind];detail=np.clip(detail,-limit,limit)*np.sin(np.pi*u)**2*np.sin(np.pi*v)**2;mask=(kind_grid==index)&(water_grid==0);H[mask]+=detail[mask]
# Surface overlays own the walkable datum. Earth/grass sits below the paving,
# rather than protruding through it or raising a doorway by an arbitrary offset.
road_grid=field['road_map'][np.rint(SIZE-gy).astype(int),np.rint(gx).astype(int)]>0;bed=(road_grid)&(water_grid==0);H[bed]=np.minimum(H[bed],field['shared_height'][bed]-.04)
# An exact narrow path can fall entirely between the 2.5m raster samples.
# Depress every grid triangle touching its metric segment, including the
# neighbouring vertices, so earth never lies coplanar with paving.
for road in plan['roads']:
 for aa,bb in zip(road['points_map_m'],road['points_map_m'][1:]):
  ax,ay=aa[0],SIZE-aa[1];bx,by=bb[0],SIZE-bb[1];dx,dy=bx-ax,by-ay;length2=dx*dx+dy*dy
  if length2<1e-9:continue
  t=np.clip(((gx-ax)*dx+(gy-ay)*dy)/length2,0,1);mask=((gx-ax-t*dx)**2+(gy-ay-t*dy)**2<=(road['width_m']/2+STEP*math.sqrt(2))**2)&(water_grid==0);H[mask]=np.minimum(H[mask],field['shared_height'][mask]-.04)
portals=[]
from street_city_campus_contract import IDS as CAMPUS_IDS,context as campus_context
foundations=[]
for bid in CAMPUS_IDS:
 cc=campus_context(bid);campus=cc['building'];cx,cy,z=campus['world_position_m'];w,d=campus['footprint_m'];mask=(abs(gx-cx)<w/2-.01)&(abs(gy-cy)<d/2-.01);plate=next(cc['assets'][i['asset_id']] for i in cc['instances'] if i['floor']==1 and cc['assets'][i['asset_id']]['role']=='floor_plate');openings=plate.get('required_openings',[])
 for hole in [q for q in openings if q['kind']=='open_courtyard']:
  hx,hy=hole['center_m'];hw,hd=hole['size_m'];mask&=~((abs(gx-cx-hx)<hw/2)&(abs(gy-cy-hy)<hd/2))
 depth=max([.35]+[-q['bottom_z_m']+.6 for q in openings if q['kind']=='pool_basin']);H[mask]=np.minimum(H[mask],z-depth);foundations.append(dict(id=bid,footprint_m=[w,d],world_position_m=[cx,cy,z],foundation_depth_m=depth,openings=openings,entrances=campus['entrances']))
 for entry in campus['entrances']:
  px,py,_=entry['position_m'];px+=cx;py+=cy;outward={'S':(0,-1),'N':(0,1),'W':(-1,0),'E':(1,0)}[entry['facade_side']];center=np.array([px,py])+np.array(outward)*1.8005;portals.append(dict(building=bid+'-'+entry['id'],center_world_m=[*center,z],door_world_m=[px,py,z],outward=list(outward),width_m=3.6,depth_m=3.4))
(OUT/'campus-foundations.json').write_text(json.dumps(dict(buildings=foundations),ensure_ascii=False,indent=2),'utf8')
for b in world['buildings']:
 cx,cy,z=b['position_m'];w,d=b['footprint_m'];mask=(abs(gx-cx)<w/2-.01)&(abs(gy-cy)<d/2-.01);H[mask]=np.minimum(H[mask],z-.35)
 r=json.loads((Path(b['source']).parent/'assembly-report.json').read_text('utf-8'));door=next(d for d in r['doors'] if d['floor']==1 and 'entry-front' in d['id']);local=door.get('position_m',door.get('center_m'));angle=math.radians(b['rotation_z_deg']);c,s=math.cos(angle),math.sin(angle);px=c*local[0]-s*local[1]+cx;py=s*local[0]+c*local[1]+cy;outward=np.array([s,-c]);center=np.array([px,py])+outward*1.8005;portals.append(dict(building=b['building'],center_world_m=[*center,z],door_world_m=[px,py,z],outward=outward.tolist(),width_m=3.6,depth_m=3.4))
# Keep fine grass relief below every exact entrance slab too. Depressing only
# the building footprint lets neighbouring grid triangles poke through paving.
for portal in portals:
 x,y,z=portal['center_world_m'];mask=(abs(gx-x)<=portal['width_m']/2+STEP)&(abs(gy-y)<=portal['depth_m']/2+STEP);H[mask]=np.minimum(H[mask],z-.14)

def clip(poly,x0,y0,x1,y1):
 for axis,bound,positive in [(0,x0,True),(0,x1,False),(1,y0,True),(1,y1,False)]:
  result=[]
  if not poly:return []
  for a,b in zip(poly,poly[1:]+poly[:1]):
   ina=a[axis]>=bound-1e-8 if positive else a[axis]<=bound+1e-8;inb=b[axis]>=bound-1e-8 if positive else b[axis]<=bound+1e-8
   if ina:result.append(a)
   if ina!=inb:
    t=(bound-a[axis])/(b[axis]-a[axis]);q=[a[k]+t*(b[k]-a[k]) for k in range(3)];q[axis]=bound;result.append(q)
  poly=result
 return poly
def make(name,verts,faces,materials,indices,layer,origin):
 verts=[tuple(round(float(v),5) for v in p) for p in verts];mesh=bpy.data.meshes.new(name);mesh.from_pydata(verts,[],faces);mesh.update();obj=bpy.data.objects.new(name,mesh);bpy.context.scene.collection.objects.link(obj)
 for m in materials:mesh.materials.append(m)
 uv=mesh.uv_layers.new(name='World-metric-16m')
 for p,idx in zip(mesh.polygons,indices):
  p.material_index=int(idx)
  for loop in p.loop_indices:
   co=mesh.vertices[mesh.loops[loop].vertex_index].co;uv.data[loop].uv=((co.x+origin[0])/16,(co.y+origin[1])/16) if abs(p.normal.z)>.5 else ((co.x+co.y+origin[0]+origin[1])/16,co.z/16)
 obj['ground_layer']=layer;obj['chunk_id']=name[:3];obj['source_route']='verified Tripo surface fields / exact geographic contract';return obj
def solid_polygon(vertices,faces,mids,polygon,origin,mid,thickness=0):
 if len(polygon)<3:return
 # Drop duplicate corners introduced when clipping an existing boundary.
 clean=[]
 for q in polygon:
  if not clean or sum((q[k]-clean[-1][k])**2 for k in range(3))>1e-12:clean.append(q)
 if len(clean)>2 and sum((clean[0][k]-clean[-1][k])**2 for k in range(3))<1e-12:clean.pop()
 if len(clean)<3:return
 area=sum(a[0]*b[1]-b[0]*a[1] for a,b in zip(clean,clean[1:]+clean[:1]));
 if abs(area)<1e-7:return
 if area<0:clean.reverse()
 start=len(vertices);n=len(clean);vertices.extend([(q[0]-origin[0],q[1]-origin[1],q[2]) for q in clean]);faces.append(tuple(start+i for i in range(n)));mids.append(mid)
 if thickness==0:return
 vertices.extend([(q[0]-origin[0],q[1]-origin[1],q[2]-thickness) for q in clean]);faces.append(tuple(start+n+i for i in reversed(range(n))));mids.append(mid)
 for i in range(n):j=(i+1)%n;faces.append((start+i,start+n+i,start+n+j,start+j));mids.append(mid)
records=[]
for chunk in plan['chunks']:
 if args.ids and chunk['id'] not in args.ids:continue
 bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False);cid=chunk['id'];folder=OUT/'chunks'/cid;folder.mkdir(parents=True,exist_ok=True);ox,oy,_=chunk['world_origin_m'];sx,sy=int(ox/STEP),int(oy/STEP);vertices=[(ix*STEP,iy*STEP,float(H[sy+iy,sx+ix])) for iy in range(N+1) for ix in range(N+1)];faces=[];mids=[]
 for iy in range(N):
  for ix in range(N):
   k=iy*(N+1)+ix;faces.append((k,k+1,k+N+2,k+N+1));x=int(ox+(ix+.5)*STEP);y=int(SIZE-oy-(iy+.5)*STEP);mids.append(int(field['kind_map'][y,x]))
 perimeter=list(range(N+1))+[iy*(N+1)+N for iy in range(1,N+1)]+[N*(N+1)+ix for ix in range(N-1,-1,-1)]+[iy*(N+1) for iy in range(N-1,0,-1)];start=len(vertices)
 # Match the bottom grid to the top. Huge skinny fan triangles cause excessive
 # adaptive subdivision in field-based remeshers; uniform cells avoid that.
 vertices.extend([(ix*STEP,iy*STEP,-30) for iy in range(N+1) for ix in range(N+1)])
 for iy in range(N):
  for ix in range(N):
   k=start+iy*(N+1)+ix;faces.append((k,k+N+1,k+N+2,k+1));mids.append(3)
 for i,k in enumerate(perimeter):j=(i+1)%len(perimeter);faces.append((k,start+k,start+perimeter[j],perimeter[j]));mids.append(3)
 objects=[make(cid+'-ground',vertices,faces,mats,mids,'base',[ox,oy])];paving=PavingUnion();entries=[]
 # Entry slabs own their surface before route strips are added. Overlapping
 # slabs and paths must share one visible top surface, not coincident faces.
 for portal in portals:
  x,y,z=portal['center_world_m'];w=portal['width_m']/2;d=portal['depth_m']/2;poly=clip([[x-w,y-d,z],[x+w,y-d,z],[x+w,y+d,z],[x-w,y+d,z]],ox,oy,ox+275,oy+275)
  for piece in paving.take(poly):entries.append((portal['building'],piece))
 for road in plan['roads']:
  vertices=[];faces=[];mids=[]
  for a,b in zip(road['points_map_m'],road['points_map_m'][1:]):
   aa=np.array([a[0],SIZE-a[1]]);bb=np.array([b[0],SIZE-b[1]]);length=np.linalg.norm(bb-aa)
   if length<1e-7:continue
   normal=np.array([-(bb-aa)[1],(bb-aa)[0]])/length*road['width_m']/2
   for t0,t1 in zip(np.linspace(0,1,max(1,math.ceil(length/2))+1)[:-1],np.linspace(0,1,max(1,math.ceil(length/2))+1)[1:]):
    p0=aa+(bb-aa)*t0;p1=aa+(bb-aa)*t1;poly=[]
    for q in [p0+normal,p0-normal,p1-normal,p1+normal]:
     x,y=q;mapy=SIZE-y;water=field['water_map'][max(0,min(SIZE,round(mapy))),max(0,min(SIZE,round(x)))];z=12+8*max(0,min(1,(625-mapy)/100)) if water or road['kind']=='asphalt' else at(field['height_map'],x,mapy);poly.append([x,y,z+(.025 if road['kind']=='asphalt' else 0)])
    for piece in paving.take(clip(poly,ox,oy,ox+275,oy+275)):solid_polygon(vertices,faces,mids,piece,[ox,oy],6 if road['kind']=='asphalt' else KINDS.index(road['kind']))
  if vertices:objects.append(make(cid+'-'+road['id'],vertices,faces,mats,mids,'routes',[ox,oy]))
 for entry_id,piece in entries:
  vertices=[];faces=[];mids=[];solid_polygon(vertices,faces,mids,piece,[ox,oy],1,.12)
  if vertices:objects.append(make(cid+'-entry-'+entry_id,vertices,faces,mats,mids,'entry',[ox,oy]))
 bpy.ops.object.select_all(action='DESELECT')
 for o in objects:o.select_set(True)
 target=folder/'normalized-source.glb';bpy.ops.export_scene.gltf(filepath=str(target),export_format='GLB',use_selection=True,export_extras=True,export_animations=False);total=0
 for o in objects:o.data.calc_loop_triangles();total+=len(o.data.loop_triangles)
 assert total<=100000,(cid,total);row=dict(**chunk,source=str(target),source_sha256=sha(target),triangles=total,world_field_sha256=sha(OUT/'world-field.npz'),prototype_sha256={k:s['source']['sha256'] for k,s in surfaces.items()},topology='closed uniform-grid terrain solid; separately manifold open road surfaces and closed entry slabs');wv=[];wf=[]
 for iy in range(N):
  for ix in range(N):
   x=int(ox+(ix+.5)*STEP);y=int(SIZE-oy-(iy+.5)*STEP);water=field['water_map'][y,x]
   if not water:continue
   start=len(wv)
   for xx,yy in [(ix*STEP,iy*STEP),((ix+1)*STEP,iy*STEP),((ix+1)*STEP,(iy+1)*STEP),(ix*STEP,(iy+1)*STEP)]:wv.append((xx,yy,(0 if water==2 else 19 if water==3 else at(field['river_water_z'],ox+xx,SIZE-oy-yy))))
   wf.append(tuple(range(start,start+4)))
 if wv:
  bpy.ops.object.select_all(action='DESELECT');o=make(cid+'-water',wv,wf,[watermat],[0]*len(wf),'water',[ox,oy]);o.select_set(True);wp=folder/'water-source.glb';bpy.ops.export_scene.gltf(filepath=str(wp),export_format='GLB',use_selection=True,export_extras=True,export_animations=False);row['water_source']=str(wp);row['water_source_sha256']=sha(wp)
 (folder/'source.json').write_text(json.dumps(row,indent=2),'utf-8');records.append(row);print(cid,'source ready',total,flush=True)
(OUT/('chunk-sources-smoke.json' if args.ids else 'chunk-sources.json')).write_text(json.dumps(dict(status='smoke_sources_only' if args.ids else 'sources_require_remesh',chunks=records,portals=portals,campus_foundations_sha256=sha(OUT/'campus-foundations.json')),indent=2),'utf-8')
