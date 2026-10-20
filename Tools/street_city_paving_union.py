"""Deterministic convex subtraction for coplanar paving; retain metric heights."""
import math
def area(poly):
 if len(poly)<3:return 0.
 x,y=poly[0][:2]
 return abs(sum((a[0]-x)*(b[1]-y)-(b[0]-x)*(a[1]-y) for a,b in zip(poly,poly[1:]+poly[:1])))/2
def halfplane(poly,a,b,inside):
 if not poly:return []
 dx,dy=b[0]-a[0],b[1]-a[1]
 def signed(p):return dx*(p[1]-a[1])-dy*(p[0]-a[0])
 result=[]
 for p,q in zip(poly,poly[1:]+poly[:1]):
  dp,dq=signed(p),signed(q);ip=dp>=-1e-9 if inside else dp<=1e-9;iq=dq>=-1e-9 if inside else dq<=1e-9
  if ip:result.append(p)
  if ip!=iq and abs(dp-dq)>1e-12:
   t=dp/(dp-dq);result.append([p[k]+t*(q[k]-p[k]) for k in range(3)])
 return result
def subtract(poly,clip):
 if area(poly)<1e-7:return []
 clip=list(clip)
 if sum(a[0]*b[1]-b[0]*a[1] for a,b in zip(clip,clip[1:]+clip[:1]))<0:clip.reverse()
 remaining=poly;result=[]
 for a,b in zip(clip,clip[1:]+clip[:1]):
  outside=halfplane(remaining,a,b,False)
  if area(outside)>1e-7:result.append(outside)
  remaining=halfplane(remaining,a,b,True)
  if area(remaining)<1e-7:break
 return result
class PavingUnion:
 def __init__(self):self.claims=[];self.bins={};self.exact=set();self.removed_area_m2=0.
 def keys(self,bounds,z=None):
  x0,y0,x1,y1=bounds
  return [(x,y) for x in range(math.floor(x0/16),math.floor(x1/16)+1) for y in range(math.floor(y0/16),math.floor(y1/16)+1)]
 def take(self,poly):
  if area(poly)<1e-7:return []
  signature=tuple(sorted(tuple(round(float(v),6) for v in p) for p in poly))
  if signature in self.exact:self.removed_area_m2+=area(poly);return []
  self.exact.add(signature)
  # Identify the whole plane, including graded roads. Truly non-planar
  # polygons are retained; identical duplicates were already removed above.
  p0=poly[0];plane=None
  for p1,p2 in zip(poly[1:-1],poly[2:]):
   dx1,dy1,dz1=[p1[k]-p0[k] for k in range(3)];dx2,dy2,dz2=[p2[k]-p0[k] for k in range(3)];det=dx1*dy2-dy1*dx2
   if abs(det)>1e-10:
    a=(dz1*dy2-dy1*dz2)/det;b=(dx1*dz2-dz1*dx2)/det;c=p0[2]-a*p0[0]-b*p0[1];plane=(a,b,c);break
  if plane is None:return []
  if max(abs(p[2]-plane[0]*p[0]-plane[1]*p[1]-plane[2]) for p in poly)>1e-6:
   # A graded quad may contain a flat triangle. Do not bypass the
   # overlap check for the whole quad merely because one corner varies.
   return [piece for i in range(1,len(poly)-1) for piece in self.take([poly[0],poly[i],poly[i+1]])]
  plane_key=(round(plane[0],8),round(plane[1],8),round(plane[2],6));bounds=[min(p[0] for p in poly),min(p[1] for p in poly),max(p[0] for p in poly),max(p[1] for p in poly)];keys=self.keys(bounds,plane_key);candidates=sorted({i for key in keys for i in self.bins.get(key,[])});pieces=[poly]
  for i in candidates:
   bb,clip,other_plane=self.claims[i]
   if min(bounds[2],bb[2])-max(bounds[0],bb[0])<=1e-7 or min(bounds[3],bb[3])-max(bounds[1],bb[1])<=1e-7:continue
   # Compare height over the actual overlap instead of rounding global
   # plane coefficients. Small coefficient differences at large world
   # coordinates otherwise put visually coplanar roads in different bins.
   overlap=poly;cc=list(clip)
   if sum(a[0]*b[1]-b[0]*a[1] for a,b in zip(cc,cc[1:]+cc[:1]))<0:cc.reverse()
   for a,b in zip(cc,cc[1:]+cc[:1]):overlap=halfplane(overlap,a,b,True)
   if area(overlap)<1e-7:continue
   if max(abs((plane[0]-other_plane[0])*p[0]+(plane[1]-other_plane[1])*p[1]+plane[2]-other_plane[2]) for p in overlap)>.002:continue
   pieces=[p for q in pieces for p in subtract(q,clip)]
   if not pieces:break
  self.removed_area_m2+=max(0,area(poly)-sum(area(p) for p in pieces));index=len(self.claims);self.claims.append((bounds,poly,plane))
  for key in keys:self.bins.setdefault(key,[]).append(index)
  return pieces
