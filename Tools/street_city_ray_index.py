"""Exact object ray casts after conservative world-AABB spatial filtering."""
import math
from mathutils import Vector

class RayIndex:
 def __init__(self,scene,depsgraph,cell=4):
  self.cell=cell;self.grid={};self.items=[]
  for original in scene.objects:
   if original.type!='MESH':continue
   obj=original.evaluated_get(depsgraph);matrix=obj.matrix_world.copy();points=[matrix@Vector(c) for c in obj.bound_box];lo=[min(p[k] for p in points)-1e-5 for k in range(3)];hi=[max(p[k] for p in points)+1e-5 for k in range(3)];idx=len(self.items);self.items.append((obj,matrix.inverted(),lo,hi))
   for x in range(math.floor(lo[0]/cell),math.floor(hi[0]/cell)+1):
    for y in range(math.floor(lo[1]/cell),math.floor(hi[1]/cell)+1):
     for z in range(math.floor(lo[2]/cell),math.floor(hi[2]/cell)+1):self.grid.setdefault((x,y,z),[]).append(idx)
 def cast(self,origin,direction,distance):
  end=origin+direction*distance;indices=set();cell=self.cell
  for x in range(math.floor(min(origin.x,end.x)/cell),math.floor(max(origin.x,end.x)/cell)+1):
   for y in range(math.floor(min(origin.y,end.y)/cell),math.floor(max(origin.y,end.y)/cell)+1):
    for z in range(math.floor(min(origin.z,end.z)/cell),math.floor(max(origin.z,end.z)/cell)+1):indices.update(self.grid.get((x,y,z),()))
  for idx in indices:
   obj,inverse,lo,hi=self.items[idx];near=0;far=distance
   for k in range(3):
    if abs(direction[k])<1e-12:
     if origin[k]<lo[k] or origin[k]>hi[k]:far=-1;break
    else:
     aa=(lo[k]-origin[k])/direction[k];bb=(hi[k]-origin[k])/direction[k];near=max(near,min(aa,bb));far=min(far,max(aa,bb))
   if near>far:continue
   local_direction=inverse.to_3x3()@direction;scale=local_direction.length
   if scale<1e-12:continue
   hit,point,normal,face=obj.ray_cast(inverse@origin,local_direction/scale,distance=distance*scale)
   if hit:return obj
  return None
