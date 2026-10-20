"""Geometry checks shared by candidate inspection and unit tests (metres)."""
import math


def valid_uv_triangle(uv, physical_area):
    if len(uv) != 3 or not all(math.isfinite(x) for q in uv for x in q):
        return False
    a, b, c = uv
    area = abs((b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]))*.5
    return math.isfinite(physical_area) and physical_area > 0 and area > 1e-20 and area/physical_area > 1e-8


def triangle_intersects_box(triangle, center, size, tolerance=1e-7):
    """Closed triangle/AABB SAT, including triangles that cross without a vertex inside."""
    if len(triangle) != 3 or len(center) != 3 or len(size) != 3:
        raise ValueError('Expected triangle and three-axis box')
    if not all(math.isfinite(x) for p in triangle for x in p):
        raise ValueError('Nonfinite triangle')
    if not all(math.isfinite(x) for x in (*center, *size)) or min(size) <= 0:
        raise ValueError('Invalid box')
    points = [[p[k] - center[k] for k in range(3)] for p in triangle]
    edges = [[points[(j + 1) % 3][k] - points[j][k] for k in range(3)] for j in range(3)]
    cross = lambda a, b: [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]
    basis = [[1, 0, 0], [0, 1, 0], [0, 0, 1]]
    axes = basis + [cross(edges[0], edges[1])] + [cross(e, b) for e in edges for b in basis]
    for axis in axes:
        norm = math.sqrt(sum(x*x for x in axis))
        if norm < 1e-12:
            continue
        axis = [x/norm for x in axis]
        projections = [sum(p[k]*axis[k] for k in range(3)) for p in points]
        radius = sum(size[k]*.5*abs(axis[k]) for k in range(3))
        if min(projections) > radius+tolerance or max(projections) < -radius-tolerance:
            return False
    return True
