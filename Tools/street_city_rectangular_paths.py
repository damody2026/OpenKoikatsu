"""Nonoverlapping metric board tiling for coplanar pedestrian path unions."""
def union(rows):
 if not rows:return []
 xs=sorted({v for x,y,z,w,d,h in rows for v in [x-w/2,x+w/2]});ys=sorted({v for x,y,z,w,d,h in rows for v in [y-d/2,y+d/2]});tiles=[]
 for xa,xb in zip(xs,xs[1:]):
  for ya,yb in zip(ys,ys[1:]):
   x,y=(xa+xb)/2,(ya+yb)/2;cover=[r for r in rows if abs(x-r[0])<r[3]/2+1e-9 and abs(y-r[1])<r[4]/2+1e-9]
   if cover:
    # The highest requested walk surface owns the intersection. Each cell is
    # emitted once as a copy of the already-remeshed rectangular board.
    r=max(cover,key=lambda q:q[2]+q[5]);tiles.append((x,y,r[2],xb-xa,yb-ya,r[5]))
 return tiles
