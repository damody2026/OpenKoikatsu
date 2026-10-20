"""Fit a complete, visible logo on one actual public wall, never on an atlas guess."""
import math

def point_inside(y,z,triangles):
 for tri in triangles:
  if y<min(v[0] for v in tri)-.002 or y>max(v[0] for v in tri)+.002 or z<min(v[1] for v in tri)-.002 or z>max(v[1] for v in tri)+.002:continue
  signs=[(y-b[0])*(a[1]-b[1])-(a[0]-b[0])*(z-b[1]) for a,b in zip(tri,tri[1:]+tri[:1])]
  if not (min(signs)<-1e-5 and max(signs)>1e-5):return True
 return False

def samples(box,spacing=.16):
 y0,z0,y1,z1=box;ny=max(2,math.ceil((y1-y0)/spacing));nz=max(2,math.ceil((z1-z0)/spacing))
 return [(y0+.015+(y1-y0-.03)*i/ny,z0+.015+(z1-z0-.03)*j/nz) for i in range(ny+1) for j in range(nz+1)]

def triangle_data(obj,indices):
 obj.data.calc_loop_triangles();result=[]
 for tri in obj.data.loop_triangles:
  if tri.polygon_index in indices:
   vs=[obj.matrix_world@obj.data.vertices[k].co for k in tri.vertices];result.append([(float(v.y),float(v.z)) for v in vs])
 return result

def select(scene,building,p):
 import bpy
 from mathutils import Vector
 # The text-free symbol and localized name share a fixed 2:1 canvas. Avoid
 # Pillow in Blender's isolated Python; image processing runs in host Python.
 scene.frame_set(1);bpy.context.view_layer.update();dg=bpy.context.evaluated_depsgraph_get()
 floors=building.get('small_shop_design',{}).get('floors',building.get('floors',[]));floor=next(f for f in floors if f['floor']==1);fz=floor['z_m'];w,d=building['footprint_m']
 cells=floor.get('cells',[]);public=[c['bounds_m'] for c in cells if c['kind']=='activity'];cores=[c['bounds_m'] for c in cells if c['kind']!='activity']
 if not cells:
  public=[[-w/2+.22,-d/2+.22,w/2-.22,d/2-.22]]
  for core in floor.get('vertical_circulation',[]):
   x,y,_=core['position_m'];cw,cd,_=core['size_m'];cores.append([x-cw/2,y-cd/2,x+cw/2,y+cd/2])
 aspect=2.0;preferred=[.15,.5,.85,.35][int(p['building'][1:])%4];choices=[]
 for obj in scene.objects:
  if obj.type!='MESH' or obj.get('floor')!=1 or obj.get('part_kind') not in ('wall','wall_corner'):continue
  groups={};normat=obj.matrix_world.to_3x3().inverted().transposed()
  for poly in obj.data.polygons:
   normal=(normat@poly.normal).normalized();center=obj.matrix_world@poly.center
   if abs(normal.x)<.98 or (abs(center.x)>w/2-.23 and normal.x*center.x>0):continue
   key=(round(center.x,3),1 if normal.x>0 else -1);groups.setdefault(key,[]).append(poly)
  for (plane,sign),polys in groups.items():
   ids={q.index for q in polys};tris=triangle_data(obj,ids);points=[v for tri in tris for v in tri]
   if not points:continue
   ymin=min(v[0] for v in points);ymax=max(v[0] for v in points);zmin=max(fz+.45,min(v[1] for v in points));zmax=min(fz+floor['height_m']-.4,max(v[1] for v in points))
   for cell in public:
    if not cell[0]-.22<=plane+sign*.08<=cell[2]+.22:continue
    low=max(ymin,cell[1])+.18;high=min(ymax,cell[3])-.18
    if high-low<1.2 or zmax-zmin<.6:continue
    choices.append((-(high-low)*(zmax-zmin),obj.name,plane,sign,ids,tris,cell,low,high,zmin,zmax))
 choices.sort(key=lambda q:q[0])
 for _,name,plane,sign,ids,tris,cell,low,high,zmin,zmax in choices:
  for desired in [4.2,3.4,2.7,2.0,1.5]:
   lw=min(desired,high-low,(zmax-zmin)*aspect,1.9*aspect);lh=lw/aspect
   if lw<1.15 or lh<.4:continue
   for fraction in [preferred,.5,.1,.9,0,1]:
    y0=low+(high-low-lw)*fraction
    for center_z in [fz+1.8,fz+2.1,fz+1.5]:
     z0=max(zmin,min(zmax-lh,center_z-lh/2));box=[y0,z0,y0+lw,z0+lh]
     if any(a<=plane+sign*.25<=c and not(box[2]<b or box[0]>e) for a,b,c,e in cores):continue
     if not all(point_inside(y,z,tris) for y,z in samples(box)):continue
     target=Vector((plane,(box[0]+box[2])/2,(box[1]+box[3])/2));probe=[(target.y,target.z),(box[0]+.05,box[1]+.05),(box[2]-.05,box[3]-.05),(box[0]+.05,box[3]-.05),(box[2]-.05,box[1]+.05)]
     for dy in [-2.0,2.0,0]:
      cx=max(cell[0]+.55,min(cell[2]-.55,plane+sign*4));cy=max(cell[1]+.55,min(cell[3]-.55,target.y+dy));camera=Vector((cx,cy,fz+1.75));visible=True
      if any(a<cx<c and b<cy<e for a,b,c,e in cores):continue
      for yy,zz in probe:
       dest=Vector((plane-sign*.004,yy,zz));delta=dest-camera
       hit=scene.ray_cast(dg,camera,delta.normalized(),distance=delta.length+.02)
       if not hit[0] or hit[4].original.name!=name or hit[3] not in ids:visible=False;break
      if visible:
       return dict(object=name,polygons=sorted(ids),plane_x_m=plane,normal_sign=sign,logo_bounds_yz_m=box,floor_z_m=fz,camera_m=list(camera),target_m=list(target),public_bounds_m=cell,surface_coverage=1.0,visibility_rays=5)
 raise AssertionError((p['building'],'No complete visible public wall logo patch',len(choices)))

def verify(scene,bid,report,check):
 import bpy
 from mathutils import Vector
 patch=report['wall_style'].get('feature_placement');check(bool(patch),bid+' physical logo placement recorded')
 if not patch:return
 scene.frame_set(1);bpy.context.view_layer.update();tris=[]
 for obj in scene.objects:
  if obj.type!='MESH':continue
  indices=set()
  for poly in obj.data.polygons:
   mat=obj.data.materials[poly.material_index] if poly.material_index<len(obj.data.materials) else None
   if mat and mat.get('shop_wall_style') and mat.get('wall_role')=='feature' and mat.get('style_building')==bid:indices.add(poly.index)
  if indices:tris+=triangle_data(obj,indices)
 box=patch['logo_bounds_yz_m'];points=samples(box);check(bool(tris) and all(point_inside(y,z,tris) for y,z in points),bid+' complete logo rectangle covered by actual imported wall',samples=len(points))
 dg=bpy.context.evaluated_depsgraph_get();camera=Vector(patch['camera_m']);plane=patch['plane_x_m'];sign=patch['normal_sign'];probe=[((box[0]+box[2])/2,(box[1]+box[3])/2),(box[0]+.05,box[1]+.05),(box[2]-.05,box[3]-.05),(box[0]+.05,box[3]-.05),(box[2]-.05,box[1]+.05)]
 for i,(y,z) in enumerate(probe):
  delta=Vector((plane-sign*.004,y,z))-camera;hit=scene.ray_cast(dg,camera,delta.normalized(),distance=delta.length+.02);ok=False
  if hit[0]:
   obj=hit[4].original;face=hit[3]
   if 0<=face<len(obj.data.polygons):
    mat=obj.data.materials[obj.data.polygons[face].material_index];ok=bool(mat and mat.get('wall_role')=='feature' and mat.get('style_building')==bid)
  check(ok,bid+f' imported logo unobstructed view {i}')
