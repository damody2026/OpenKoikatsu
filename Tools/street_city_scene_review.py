"""Fit the actual projected world bounds, including render aspect ratio."""
from mathutils import Vector

def frame_overview(camera,points,aspect,margin=1.08):
 rotation=camera.rotation_euler.to_matrix();inverse=rotation.transposed();projected=[inverse@(p-camera.location) for p in points]
 x0=min(p.x for p in projected);x1=max(p.x for p in projected);y0=min(p.y for p in projected);y1=max(p.y for p in projected)
 shift=rotation@Vector(((x0+x1)/2,(y0+y1)/2,0));camera.location+=shift
 camera.data.sensor_fit='HORIZONTAL';camera.data.ortho_scale=max(x1-x0,(y1-y0)*aspect)*margin
 projected=[inverse@(p-camera.location) for p in points];half_x=camera.data.ortho_scale/2;half_y=half_x/aspect
 assert all(abs(p.x)<half_x and abs(p.y)<half_y for p in projected),'Overview clips actual world geometry'
 return shift,camera.data.ortho_scale
