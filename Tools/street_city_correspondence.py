"""Certified surface-distance bound when indexed triangle connectivity is identical."""
import math
def surface_displacement_bound(source_vertices,source_faces,result_vertices,result_faces):
 if len(source_vertices)!=len(result_vertices) or not source_vertices:return None
 if len(source_faces)!=len(result_faces) or any(tuple(a)!=tuple(b) for a,b in zip(source_faces,result_faces)):return None
 # For any barycentric point on a corresponding triangle, displacement is a
 # convex combination of its three vertex displacements. The largest vertex
 # displacement therefore bounds the entire surface in both directions.
 return max(math.dist(a,b) for a,b in zip(source_vertices,result_vertices))
