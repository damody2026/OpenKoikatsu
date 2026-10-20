"""Shading only: preserve certified geometry and UVs, split hard product edges."""
import math
def apply(mesh):
 for face in mesh.polygons:face.use_smooth=True
 mesh.set_sharp_from_angle(angle=math.radians(45))
 mesh.update()
