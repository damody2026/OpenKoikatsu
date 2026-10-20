"""Builds the StreetSocial place models in Blender and writes them as .okmesh for the game.

Run:  blender --background --factory-startup --python Tools/street_models/build_street_models.py -- <out_dir> [--preview] [ids...]

Each model is built around its own origin with the street-facing front toward +X (Blender metres, Z up).
The game places it with a yaw (OKStreetSocialPlaces.h). Output per model:
  <id>.okmesh       sections (one colour each), collision boxes and the door spot, in UE local cm
  <id>.png          a preview render (with --preview)

.okmesh (little endian): "OKPM", u32 version=1, u32 sections,
  per section: f32 r,g,b (sRGB), u32 street_material (fallback / forced), u32 flags (1 = use the street
  material itself, 2 = roof: hidden while the player is inside, bits 8-15 = floor level of a multi-storey
  building: levels above the player's floor hide), u32 vertices, u32 indices, f32[3] positions, f32[3] normals, f32[2] uv, u32 indices;
  u32 boxes, per box f32 centre[3], size[3]; f32 door[3];
  v2: u32 rooms, per room f32 centre[3], size[3]; u32 anchors, per anchor u32 len, utf8 name, f32 pos[3], yaw.
UE = (x, -y, z) * 100; the mirror itself makes Blender's counter-clockwise fronts what UE draws as front.
"""
import bmesh
import bpy
import math
import os
import struct
import sys
from mathutils import Matrix, Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import interiors  # noqa: E402
import interiors_more  # noqa: E402
import interiors_resort  # noqa: E402
import interiors_public  # noqa: E402
import dept_store  # noqa: E402
import multi_buildings  # noqa: E402
import vending  # noqa: E402
import lottery  # noqa: E402
import campus  # noqa: E402

# ---------------------------------------------------------------------------------------------------- palette
# name: (sRGB colour, fallback street material, use street material directly)
# Street materials: Checker0 Plaza1 Terracotta2 Cream3 Stripe4 Glass5 Leaf6 Bark7 Wood8 Lamp9 Hedge10 Stone11
PALETTE = {
    'cream': ((0.93, 0.88, 0.78), 3, False), 'white': ((0.95, 0.95, 0.93), 3, False),
    'plaster': ((0.86, 0.83, 0.78), 3, False), 'terracotta': ((0.74, 0.38, 0.25), 2, False),
    'brick': ((0.63, 0.31, 0.23), 2, False), 'roof_dark': ((0.24, 0.26, 0.31), 11, False),
    'roof_red': ((0.62, 0.21, 0.18), 2, False), 'roof_blue': ((0.26, 0.40, 0.58), 11, False),
    'roof_green': ((0.27, 0.45, 0.36), 10, False), 'wood': ((0.58, 0.40, 0.23), 8, False),
    'wood_dark': ((0.32, 0.21, 0.13), 7, False), 'wood_light': ((0.78, 0.62, 0.42), 8, False),
    'stone': ((0.62, 0.61, 0.58), 11, False), 'stone_dark': ((0.38, 0.38, 0.37), 11, False),
    'concrete': ((0.74, 0.74, 0.72), 11, False), 'pink': ((0.96, 0.72, 0.80), 4, False),
    'rose': ((0.86, 0.42, 0.55), 2, False), 'sky': ((0.58, 0.77, 0.92), 5, False),
    'navy': ((0.16, 0.24, 0.42), 11, False), 'mint': ((0.62, 0.86, 0.76), 6, False),
    'yellow': ((0.98, 0.82, 0.30), 9, False), 'orange': ((0.96, 0.55, 0.20), 2, False),
    'red': ((0.84, 0.16, 0.15), 2, False), 'green': ((0.38, 0.68, 0.22), 6, False),
    'purple': ((0.52, 0.32, 0.62), 2, False), 'metal': ((0.70, 0.72, 0.75), 11, False),
    'black': ((0.10, 0.10, 0.12), 11, False), 'grass': ((0.36, 0.60, 0.26), 6, False),
    'leaf': ((0.27, 0.52, 0.22), 6, False), 'leaf_light': ((0.45, 0.68, 0.28), 6, False),
    'trunk': ((0.40, 0.28, 0.18), 7, False), 'track': ((0.73, 0.35, 0.26), 2, False),
    'sand': ((0.86, 0.79, 0.62), 1, False), 'tile': ((0.86, 0.92, 0.94), 3, False),
    'water': ((0.30, 0.66, 0.84), 5, False), 'onsen': ((0.52, 0.76, 0.78), 5, False),
    'glass': ((0.42, 0.58, 0.68), 5, False), 'glass_dark': ((0.22, 0.32, 0.40), 5, False),
    'awning_a': ((0.86, 0.22, 0.20), 4, False), 'awning_b': ((0.96, 0.94, 0.90), 3, False),
    'cloth_blue': ((0.20, 0.35, 0.62), 11, False), 'cloth_indigo': ((0.17, 0.20, 0.38), 11, False),
    'lamp': ((1.0, 0.86, 0.55), 9, True), 'sign': ((0.98, 0.97, 0.94), 3, False),
    'bamboo': ((0.56, 0.62, 0.30), 6, False), 'rock': ((0.45, 0.44, 0.42), 11, False),
}


def srgb_to_linear(c):
    return tuple(x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4 for x in c)


def material(name):
    m = bpy.data.materials.get(name)
    if m is None:
        m = bpy.data.materials.new(name)
        lin = srgb_to_linear(PALETTE[name][0])
        m.diffuse_color = (*lin, 1.0)
        m.use_nodes = False
    return m


# ---------------------------------------------------------------------------------------------------- model
class Model:
    """Collects mesh objects, collision boxes (local metres) and the door spot for one place."""

    def __init__(self, ident):
        self.id = ident
        self.objects = []
        self.boxes = []
        self.roof = False      # objects made while set are hidden in game when the player is inside
        self.level = 0         # floor of a multi-storey building (1 = ground); levels above the player's hide
        self.interiors = []    # walkable rooms (centre, size) in local metres
        self.anchors = {}      # activity spots: name -> ((x, y), yaw degrees, CCW from +X)
        self.door_at = Vector((0, 0, 0))
        for o in list(bpy.data.objects):
            bpy.data.objects.remove(o, do_unlink=True)

    # -- primitives -------------------------------------------------------------------------------------
    def _object(self, bm, mat, bevel=0.0):
        me = bpy.data.meshes.new('m')
        bm.to_mesh(me)
        bm.free()
        me.materials.append(material(mat))
        ob = bpy.data.objects.new('o', me)
        bpy.context.scene.collection.objects.link(ob)
        if self.roof:
            ob['roof'] = 1
        if self.level:
            ob['level'] = self.level
        if bevel > 0:
            mod = ob.modifiers.new('bevel', 'BEVEL')
            mod.width = bevel
            mod.segments = 1
            mod.limit_method = 'ANGLE'
        self.objects.append(ob)
        return ob

    def box(self, mat, center, size, bevel=0.0, rot_z=0.0, solid=False):
        bm = bmesh.new()
        bmesh.ops.create_cube(bm, size=1.0)
        bmesh.ops.scale(bm, vec=Vector(size), verts=bm.verts)
        if rot_z:
            bmesh.ops.rotate(bm, cent=Vector((0, 0, 0)), matrix=Matrix.Rotation(rot_z, 3, 'Z'), verts=bm.verts)
        bmesh.ops.translate(bm, vec=Vector(center), verts=bm.verts)
        if solid:
            self.boxes.append((Vector(center), Vector(size)))
        return self._object(bm, mat, bevel)

    def cyl(self, mat, center, radius, height, segments=16, radius2=None, axis='Z'):
        bm = bmesh.new()
        bmesh.ops.create_cone(bm, cap_ends=True, segments=segments, radius1=radius,
                              radius2=radius if radius2 is None else radius2, depth=height)
        if axis == 'X':
            bmesh.ops.rotate(bm, cent=Vector((0, 0, 0)), matrix=Matrix.Rotation(math.radians(90), 3, 'Y'), verts=bm.verts)
        bmesh.ops.translate(bm, vec=Vector(center), verts=bm.verts)
        ob = self._object(bm, mat)
        for p in ob.data.polygons:
            p.use_smooth = len(p.vertices) == 4
        return ob

    def sphere(self, mat, center, radius, scale=(1, 1, 1), segments=12):
        bm = bmesh.new()
        bmesh.ops.create_uvsphere(bm, u_segments=segments, v_segments=max(6, segments // 2), radius=radius)
        bmesh.ops.scale(bm, vec=Vector(scale), verts=bm.verts)
        bmesh.ops.translate(bm, vec=Vector(center), verts=bm.verts)
        ob = self._object(bm, mat)
        for p in ob.data.polygons:
            p.use_smooth = True
        return ob

    def prism(self, mat, points, z0, z1):
        """Extruded polygon (counter-clockwise points in XY)."""
        bm = bmesh.new()
        bottom = [bm.verts.new((x, y, z0)) for x, y in points]
        top = [bm.verts.new((x, y, z1)) for x, y in points]
        bm.faces.new(list(reversed(bottom)))
        bm.faces.new(top)
        n = len(points)
        for i in range(n):
            j = (i + 1) % n
            bm.faces.new((bottom[i], bottom[j], top[j], top[i]))
        return self._object(bm, mat)

    def gable_roof(self, mat, center, length, width, rise, along_x=False, overhang=0.35, thick=0.12):
        """Pitched roof with the ridge along Y (or X), eaves at center.z."""
        cx, cy, cz = center
        L = length + overhang * 2
        W = width / 2 + overhang
        bm = bmesh.new()
        def v(a, b, z):  # a along ridge, b across
            return bm.verts.new((cx + b, cy + a, z) if not along_x else (cx + a, cy + b, z))
        for sign in (-1, 1):
            p0 = v(-L / 2, sign * W, cz); p1 = v(L / 2, sign * W, cz)
            p2 = v(L / 2, 0, cz + rise); p3 = v(-L / 2, 0, cz + rise)
            q0 = v(-L / 2, sign * W, cz - thick); q1 = v(L / 2, sign * W, cz - thick)
            q2 = v(L / 2, 0, cz + rise - thick); q3 = v(-L / 2, 0, cz + rise - thick)
            f = [bm.faces.new((p0, p1, p2, p3)), bm.faces.new((q3, q2, q1, q0)),
                 bm.faces.new((p0, q0, q1, p1)), bm.faces.new((p1, q1, q2, p2)), bm.faces.new((p3, q3, q0, p0))]
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        ob = self._object(bm, mat)
        return ob

    def gable_end(self, mat, center, width, rise, along_x=False, depth=0.05):
        """Triangular wall filling a gable (at the given end)."""
        cx, cy, cz = center
        w = width / 2
        if not along_x:
            pts = [(-w, 0), (w, 0), (0, rise)]
            bm = bmesh.new()
            vs = [bm.verts.new((cx + a, cy - depth, cz + b)) for a, b in pts] + [bm.verts.new((cx + a, cy + depth, cz + b)) for a, b in pts]
        else:
            pts = [(-w, 0), (w, 0), (0, rise)]
            bm = bmesh.new()
            vs = [bm.verts.new((cx - depth, cy + a, cz + b)) for a, b in pts] + [bm.verts.new((cx + depth, cy + a, cz + b)) for a, b in pts]
        bm.faces.new(vs[:3]); bm.faces.new(vs[3:])
        for i in range(3):
            j = (i + 1) % 3
            bm.faces.new((vs[i], vs[j], vs[3 + j], vs[3 + i]))
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        return self._object(bm, mat)

    def hip_roof(self, mat, center, sx, sy, rise, overhang=0.4):
        cx, cy, cz = center
        hx, hy = sx / 2 + overhang, sy / 2 + overhang
        ridge = max(0.0, (hx - hy)) if hx > hy else max(0.0, (hy - hx))
        bm = bmesh.new()
        c = [bm.verts.new((cx + x, cy + y, cz)) for x, y in ((-hx, -hy), (hx, -hy), (hx, hy), (-hx, hy))]
        if hx >= hy:
            r = [bm.verts.new((cx - ridge, cy, cz + rise)), bm.verts.new((cx + ridge, cy, cz + rise))]
            faces = [(c[0], c[1], r[1], r[0]), (c[1], c[2], r[1]), (c[2], c[3], r[0], r[1]), (c[3], c[0], r[0])]
        else:
            r = [bm.verts.new((cx, cy - ridge, cz + rise)), bm.verts.new((cx, cy + ridge, cz + rise))]
            faces = [(c[0], c[1], r[0]), (c[1], c[2], r[1], r[0]), (c[2], c[3], r[1]), (c[3], c[0], r[0], r[1])]
        for f in faces:
            bm.faces.new(f)
        bm.faces.new(list(reversed(c)))
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        return self._object(bm, mat)

    def text(self, body, mat, center, height, depth=0.04, facing='+X'):
        """Extruded sign lettering on a wall facing +X (default) or +Y, centred at `center`."""
        curve = bpy.data.curves.new('t', 'FONT')
        curve.body = body
        curve.font = FONT
        curve.size = height
        curve.extrude = depth
        curve.align_x = 'CENTER'
        curve.align_y = 'CENTER'
        ob = bpy.data.objects.new('t', curve)
        bpy.context.scene.collection.objects.link(ob)
        rot = Matrix.Rotation(math.radians(90), 4, 'X')  # upright, facing -Y
        rot = Matrix.Rotation(math.radians(90 if facing == '+X' else 180), 4, 'Z') @ rot
        ob.matrix_world = Matrix.Translation(Vector(center)) @ rot
        deps = bpy.context.evaluated_depsgraph_get()
        me = bpy.data.meshes.new_from_object(ob.evaluated_get(deps))
        bpy.data.objects.remove(ob, do_unlink=True)
        # Fonts differ in em size (the variable Noto font is about a third of curve.size): scale the glyphs so
        # the letters are `height` tall, keeping the extrusion depth.
        if me.vertices:
            ys = [v.co.y for v in me.vertices]
            span = max(ys) - min(ys)
            if span > 1e-4:
                k = height / span
                me.transform(Matrix.Diagonal((k, k, 1.0, 1.0)))
        me.transform(Matrix.Translation(Vector(center)) @ rot)
        me.materials.clear()
        me.materials.append(material(mat))
        out = bpy.data.objects.new('txt', me)
        bpy.context.scene.collection.objects.link(out)
        if self.roof:
            out['roof'] = 1
        if self.level:
            out['level'] = self.level
        self.objects.append(out)
        return out

    # -- composites -------------------------------------------------------------------------------------
    def window(self, wall_x, y, z, w, h, frame='white', glass='glass', sill=True, mullion=True, facing=1):
        """Window on a wall whose outer face is at x = wall_x (facing +X if facing=1, -X if -1)."""
        d = 0.06 * facing
        self.box(glass, (wall_x + d * 0.3, y, z), (0.04, w, h))
        t = 0.07
        for zz in (z - h / 2, z + h / 2):
            self.box(frame, (wall_x + d, y, zz), (0.08, w + t * 2, t))
        for yy in (y - w / 2, y + w / 2):
            self.box(frame, (wall_x + d, yy, z), (0.08, t, h))
        if mullion:
            self.box(frame, (wall_x + d, y, z), (0.06, 0.04, h))
            self.box(frame, (wall_x + d, y, z + h * 0.15), (0.06, w, 0.04))
        if sill:
            self.box(frame, (wall_x + d * 2, y, z - h / 2 - 0.05), (0.18, w + 0.2, 0.06))

    def side_window(self, x, wall_y, z, w, h, frame='white', glass='glass', facing=1):
        """Window on a wall whose outer face is at y = wall_y (facing +Y if 1)."""
        d = 0.06 * facing
        self.box(glass, (x, wall_y + d * 0.3, z), (w, 0.04, h))
        t = 0.07
        for zz in (z - h / 2, z + h / 2):
            self.box(frame, (x, wall_y + d, zz), (w + t * 2, 0.08, t))
        for xx in (x - w / 2, x + w / 2):
            self.box(frame, (xx, wall_y + d, z), (t, 0.08, h))
        self.box(frame, (x, wall_y + d * 2, z - h / 2 - 0.05), (w + 0.2, 0.18, 0.06))

    def door(self, wall_x, y, w=1.6, h=2.3, frame='white', leaf='wood', glass=True):
        self.box(leaf, (wall_x + 0.02, y, h / 2), (0.06, w, h))
        if glass:
            for yy in (y - w / 4, y + w / 4):
                self.box('glass', (wall_x + 0.06, yy, h * 0.58), (0.03, w * 0.32, h * 0.62))
        self.box(frame, (wall_x + 0.06, y, h + 0.06), (0.12, w + 0.3, 0.12))
        for yy in (y - w / 2 - 0.08, y + w / 2 + 0.08):
            self.box(frame, (wall_x + 0.06, yy, h / 2), (0.12, 0.12, h))
        self.box('concrete', (wall_x + 0.45, y, 0.06), (0.9, w + 1.0, 0.12))  # step

    def awning(self, wall_x, y, z, width, depth=1.3, drop=0.45, stripes=8, colors=('awning_a', 'awning_b')):
        """Striped sloped awning sticking out of a +X wall."""
        sw = width / stripes
        ang = math.atan2(drop, depth)
        L = math.hypot(depth, drop)
        for i in range(stripes):
            yy = y - width / 2 + sw * (i + 0.5)
            ob = self.box(colors[i % 2], (0, 0, 0), (L, sw, 0.04))
            ob.matrix_world = Matrix.Translation((wall_x + depth / 2, yy, z - drop / 2)) @ Matrix.Rotation(-ang, 4, 'Y') @ Matrix.Rotation(0, 4, 'Z')
            # front valance
            self.box(colors[i % 2], (wall_x + depth + 0.01, yy, z - drop - 0.12), (0.03, sw, 0.25))
        for yy in (y - width / 2, y + width / 2):
            self.box('metal', (wall_x + depth / 2, yy, z - drop / 2), (depth, 0.03, 0.03))

    def tree(self, x, y, h=4.5, r=1.4, leaf='leaf'):
        self.cyl('trunk', (x, y, h * 0.3), 0.16, h * 0.6, 8, 0.11)
        self.sphere(leaf, (x, y, h * 0.68), r, (1, 1, 0.85), 10)
        self.sphere('leaf_light', (x + r * 0.35, y - r * 0.25, h * 0.82), r * 0.65, (1, 1, 0.9), 8)

    def palm(self, x, y, h=5.5):
        self.cyl('trunk', (x, y, h / 2), 0.17, h, 8, 0.12)
        for i in range(7):
            a = i / 7 * math.tau
            ob = self.box('leaf', (0, 0, 0), (2.2, 0.45, 0.04))
            ob.matrix_world = Matrix.Translation((x + math.cos(a) * 0.9, y + math.sin(a) * 0.9, h - 0.25)) @ Matrix.Rotation(a, 4, 'Z') @ Matrix.Rotation(math.radians(22), 4, 'Y')

    def lantern(self, x, y, z, r=0.18):
        self.sphere('lamp', (x, y, z), r, (1, 1, 1.25), 10)
        self.box('wood_dark', (x, y, z + r * 1.35), (r * 1.2, r * 1.2, 0.05))
        self.box('wood_dark', (x, y, z - r * 1.35), (r * 1.2, r * 1.2, 0.05))

    def street_lamp(self, x, y, h=3.6):
        self.cyl('black', (x, y, h / 2), 0.06, h, 8)
        self.box('black', (x, y, h), (0.32, 0.32, 0.05))
        self.sphere('lamp', (x, y, h + 0.2), 0.2, (1, 1, 1.2), 10)

    def bench(self, x, y, rot=0.0):
        for dx in (-0.6, 0.6):
            ob = self.box('black', (0, 0, 0), (0.06, 0.45, 0.42))
            ob.matrix_world = Matrix.Translation((x, y, 0.21)) @ Matrix.Rotation(rot, 4, 'Z') @ Matrix.Translation((0, dx, 0)) @ Matrix.Rotation(math.radians(90), 4, 'Z')
        seat = self.box('wood', (0, 0, 0), (0.45, 1.5, 0.05))
        seat.matrix_world = Matrix.Translation((x, y, 0.44)) @ Matrix.Rotation(rot, 4, 'Z')
        back = self.box('wood', (0, 0, 0), (0.05, 1.5, 0.4))
        back.matrix_world = Matrix.Translation((x, y, 0.7)) @ Matrix.Rotation(rot, 4, 'Z') @ Matrix.Translation((-0.22, 0, 0))

    def table_umbrella(self, x, y, color='awning_a'):
        self.cyl('white', (x, y, 0.72), 0.45, 0.04, 16)
        self.cyl('metal', (x, y, 0.36), 0.04, 0.72, 8)
        self.cyl('metal', (x, y, 1.3), 0.025, 1.2, 6)
        self.cyl(color, (x, y, 2.0), 1.3, 0.35, 12, 0.05)
        for a in range(0, 360, 90):
            r = math.radians(a)
            self.box('wood', (x + math.cos(r) * 0.75, y + math.sin(r) * 0.75, 0.24), (0.38, 0.38, 0.04))

    def railing(self, x0, y0, x1, y1, z, h=1.0, mat='metal', posts=6):
        mx, my = (x0 + x1) / 2, (y0 + y1) / 2
        L = math.hypot(x1 - x0, y1 - y0)
        ang = math.atan2(y1 - y0, x1 - x0)
        top = self.box(mat, (0, 0, 0), (L, 0.05, 0.05))
        top.matrix_world = Matrix.Translation((mx, my, z + h)) @ Matrix.Rotation(ang, 4, 'Z')
        mid = self.box(mat, (0, 0, 0), (L, 0.03, 0.03))
        mid.matrix_world = Matrix.Translation((mx, my, z + h * 0.5)) @ Matrix.Rotation(ang, 4, 'Z')
        for i in range(posts + 1):
            t = i / posts
            self.box(mat, (x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, z + h / 2), (0.04, 0.04, h))

    def floor_band(self, cx, cy, sx, sy, z, mat='white', h=0.18, out=0.06):
        self.box(mat, (cx, cy, z), (sx + out * 2, sy + out * 2, h))


# ---------------------------------------------------------------------------------------------------- buildings
def walls(m, depth, width, height, mat, base='stone_dark', solid=True):
    """Main block centred on the origin: x in [-depth/2, depth/2], front face at +depth/2."""
    m.box(mat, (0, 0, height / 2), (depth, width, height), solid=solid)
    m.box(base, (0, 0, 0.25), (depth + 0.08, width + 0.08, 0.5))
    m.floor_band(0, 0, depth, width, height - 0.1, 'white', 0.22)
    return depth / 2


def facade_windows(m, front, width, floors, floor_h, start_z, w=1.3, h=1.4, skip_center=0.0, frame='white', glass='glass', columns=None):
    cols = columns or max(2, int(width / 2.4))
    for f in range(floors):
        z = start_z + f * floor_h
        for i in range(cols):
            y = -width / 2 + width * (i + 0.5) / cols
            if f == 0 and abs(y) < skip_center:
                continue
            m.window(front, y, z, w, h, frame, glass)


def side_windows(m, depth, width, floors, floor_h, start_z, frame='white', glass='glass'):
    cols = max(1, int(depth / 2.6))
    for side in (-1, 1):
        for f in range(floors):
            for i in range(cols):
                x = -depth / 2 + depth * (i + 0.5) / cols
                m.side_window(x, side * width / 2, start_z + f * floor_h, 1.1, 1.2, frame, glass, side)


def sign(m, front, text, z, width, height=0.75, board='sign', letters='navy', facing='+X'):
    m.box(board, (front + 0.08, 0, z), (0.12, width, height), bevel=0.02)
    m.box('wood_dark', (front + 0.03, 0, z), (0.06, width + 0.12, height + 0.12))
    m.text(text, letters, (front + 0.16, 0, z), height * 0.78)


def finish(m, front, door_out=2.6):
    m.door_spot = None
    m.door_at = Vector((front + door_out, 0, 0))


def build_restaurant():
    m = Model('restaurant')
    depth, width, height = 9.0, 8.0, 4.4
    front = walls(m, depth, width, height, 'cream', 'brick')
    m.hip_roof('roof_red', (0, 0, height), depth, width, 2.0, 0.5)
    m.box('brick', (front + 0.05, 0, 1.6), (0.1, width, 0.3))
    for y in (-2.6, 2.6):
        m.window(front, y, 1.9, 2.0, 1.8, 'wood_dark')
    m.door(front, 0, 1.8, 2.4, 'wood_dark', 'wood')
    m.awning(front, 0, 3.3, 7.6, 1.6, 0.5, 10, ('roof_green', 'awning_b'))
    sign(m, front, '餐廳', 3.85, 3.2, 0.7, 'cream', 'roof_red')
    side_windows(m, depth, width, 1, 0, 2.0, 'wood_dark')
    # terrace: low fence, tables with umbrellas, planters, lanterns
    m.box('wood_light', (front + 2.6, 0, 0.04), (3.8, width, 0.08))
    for y in (-2.9, 2.9):
        m.table_umbrella(front + 2.6, y, 'roof_green' if y < 0 else 'awning_a')
    for y in (-4.4, 4.4):
        m.box('wood', (front + 2.6, y, 0.35), (3.6, 0.5, 0.7))
        for k in range(4):
            m.sphere('leaf', (front + 1.2 + k * 0.95, y, 0.85), 0.32, (1, 1, 0.8), 8)
    for y in (-1.4, 1.4):
        m.lantern(front + 0.35, y, 2.8)
    m.chimney = m.box('brick', (-2.5, 2.2, height + 1.6), (0.7, 0.7, 2.0))
    finish(m, front, 4.9)
    return m


def build_snack():
    m = Model('snack')
    depth, width, height = 9.0, 8.0, 3.8
    front = walls(m, depth, width, height, 'pink', 'rose')
    m.box('white', (0, 0, height + 0.3), (depth + 0.3, width + 0.3, 0.6))
    # scalloped cornice
    for i in range(14):
        m.sphere('rose', (front + 0.1, -width / 2 + (i + 0.5) * width / 14, height + 0.05), 0.24, (0.4, 1, 0.8), 8)
    m.window(front, -2.4, 1.6, 2.6, 1.9, 'white')
    m.window(front, 2.4, 1.6, 2.6, 1.9, 'white')
    m.door(front, 0, 1.4, 2.3, 'white', 'rose')
    m.awning(front, 0, 3.0, 7.8, 1.4, 0.45, 12, ('rose', 'awning_b'))
    sign(m, front, '點心店', 3.45, 3.6, 0.62, 'white', 'rose')
    # giant cupcake on the roof
    m.cyl('wood_light', (0, 0, height + 1.0), 0.9, 0.9, 16, 1.1)
    m.sphere('pink', (0, 0, height + 1.7), 1.05, (1, 1, 0.7), 14)
    m.sphere('red', (0, 0, height + 2.45), 0.25, (1, 1, 1), 10)
    side_windows(m, depth, width, 1, 0, 1.8, 'white')
    for y in (-2.6, 2.6):
        m.bench(front + 2.2, y, math.radians(90))
    finish(m, front)
    return m


def build_bookstore():
    m = Model('bookstore')
    depth, width, height = 9.0, 9.0, 4.0
    front = walls(m, depth, width, height, 'wood', 'stone_dark')
    m.gable_roof('roof_dark', (0, 0, height), depth, width, 2.4, along_x=True, overhang=0.5)
    for s in (-1, 1):
        m.gable_end('wood_light', (0, s * 0, height), width, 2.4, along_x=True)
    m.gable_end('wood_light', (front, 0, height), width, 2.4, along_x=True)
    m.gable_end('wood_light', (-front, 0, height), width, 2.4, along_x=True)
    m.window(front, -2.6, 1.7, 2.8, 2.0, 'wood_dark', 'glass')
    m.window(front, 2.6, 1.7, 2.8, 2.0, 'wood_dark', 'glass')
    # book stacks in the shop windows
    colors = ['red', 'navy', 'yellow', 'green', 'purple', 'orange']
    for side in (-2.6, 2.6):
        for i in range(9):
            hh = 0.25 + (i * 37 % 5) * 0.05
            m.box(colors[(i + int(side)) % len(colors)], (front - 0.25, side - 1.2 + i * 0.3, 0.75 + hh / 2), (0.3, 0.22, hh))
    m.door(front, 0, 1.4, 2.4, 'wood_dark', 'wood_dark')
    m.window(front, 0, height + 0.9, 1.4, 1.0, 'wood_dark')
    sign(m, front, '書局', 3.35, 2.6, 0.62, 'cream', 'wood_dark')
    side_windows(m, depth, width, 1, 0, 1.9, 'wood_dark')
    m.box('wood_dark', (front + 1.2, -3.6, 0.6), (0.6, 1.4, 1.2))  # book cart
    for i in range(5):
        m.box(colors[i], (front + 1.2, -4.1 + i * 0.25, 1.32), (0.5, 0.18, 0.25))
    m.street_lamp(front + 2.0, 3.8)
    finish(m, front)
    return m


def build_lodge():
    m = Model('lodge')
    depth, width, height = 9.0, 11.0, 6.4
    front = walls(m, depth, width, height, 'plaster', 'stone')
    m.hip_roof('roof_dark', (0, 0, height), depth, width, 2.2, 0.7)
    m.floor_band(0, 0, depth, width, 3.2, 'wood_dark', 0.2)
    facade_windows(m, front, width, 2, 3.2, 1.7, 1.4, 1.5, skip_center=2.0, frame='wood_dark')
    side_windows(m, depth, width, 2, 3.2, 1.7, 'wood_dark')
    m.door(front, 0, 2.2, 2.5, 'wood_dark', 'wood_dark')
    # entrance canopy on posts
    m.box('wood_dark', (front + 1.4, 0, 3.0), (2.8, 4.2, 0.18))
    m.gable_roof('roof_dark', (front + 1.4, 0, 3.08), 2.8, 4.2, 0.7, along_x=True, overhang=0.2)
    for y in (-1.9, 1.9):
        m.cyl('wood_dark', (front + 2.6, y, 1.5), 0.13, 3.0, 10)
    sign(m, front, '會館', 4.6, 2.8, 0.75, 'wood_dark', 'yellow')
    for y in (-4.5, 4.5):
        m.lantern(front + 0.3, y, 2.4, 0.24)
        m.box('stone', (front + 1.5, y, 0.5), (1.0, 1.0, 1.0))
        m.sphere('leaf', (front + 1.5, y, 1.3), 0.6, (1, 1, 0.8), 10)
    finish(m, front)
    return m


def build_school():
    m = Model('school')
    depth, width, height = 14.0, 16.0, 9.6
    front = walls(m, depth, width, height, 'cream', 'stone_dark')
    m.box('roof_red', (0, 0, height + 0.25), (depth + 0.4, width + 0.4, 0.5))
    for f in (3.2, 6.4):
        m.floor_band(0, 0, depth, width, f, 'white', 0.16)
    facade_windows(m, front, width, 3, 3.2, 1.8, 1.6, 1.6, skip_center=2.2, columns=6)
    side_windows(m, depth, width, 3, 3.2, 1.8)
    m.door(front, 0, 2.8, 2.6, 'white', 'glass_dark')
    # clock tower over the entrance
    m.box('cream', (front - 1.0, 0, height + 1.8), (2.4, 3.2, 3.6))
    m.hip_roof('roof_red', (front - 1.0, 0, height + 3.6), 2.4, 3.2, 1.6, 0.3)
    m.cyl('white', (front + 0.22, 0, height + 2.0), 0.95, 0.08, 24, axis='X')
    m.cyl('black', (front + 0.28, 0, height + 2.0), 0.08, 0.06, 8, axis='X')
    m.box('black', (front + 0.3, 0, height + 2.3), (0.04, 0.06, 0.6))
    m.box('black', (front + 0.3, 0.2, height + 2.0), (0.04, 0.45, 0.06))
    sign(m, front, '學校', 7.8, 3.4, 0.8, 'white', 'navy')
    # gate posts, fence and flagpole in front
    for y in (-3.2, 3.2):
        m.box('brick', (front + 4.5, y, 1.0), (0.7, 0.7, 2.0))
        m.box('stone', (front + 4.5, y, 2.05), (0.85, 0.85, 0.12))
    m.railing(front + 4.5, -7.8, front + 4.5, -3.6, 0, 1.4, 'black', 8)
    m.railing(front + 4.5, 3.6, front + 4.5, 7.8, 0, 1.4, 'black', 8)
    m.cyl('metal', (front + 3.0, -5.5, 4.0), 0.06, 8.0, 8)
    m.box('red', (front + 3.0, -6.2, 7.4), (0.02, 1.3, 0.85))
    for y in (-6.5, 6.5):
        m.tree(front + 2.0, y, 5.0, 1.5)
    finish(m, front, 2.6)
    return m


def build_library():
    m = Model('library')
    depth, width, height = 12.0, 10.0, 6.5
    front = walls(m, depth, width, height, 'stone', 'stone_dark')
    m.box('white', (0, 0, height + 0.25), (depth + 0.3, width + 0.3, 0.5))
    # dome
    m.cyl('stone', (-1.0, 0, height + 1.0), 2.4, 1.6, 24)
    m.sphere('roof_green', (-1.0, 0, height + 1.8), 2.4, (1, 1, 0.75), 24)
    m.cyl('white', (-1.0, 0, height + 3.7), 0.15, 0.6, 8)
    # portico: steps, columns, pediment
    for i in range(3):
        m.box('stone', (front + 1.9 - i * 0.3, 0, 0.1 + i * 0.2), (3.8 - i * 0.6, 7.6 - i * 0.4, 0.2), solid=False)
    for y in (-2.7, -0.9, 0.9, 2.7):
        m.cyl('white', (front + 2.2, y, 2.95), 0.26, 4.7, 16)
        m.box('white', (front + 2.2, y, 5.35), (0.65, 0.65, 0.15))
    m.box('white', (front + 1.5, 0, 5.6), (3.2, 7.2, 0.4))
    m.gable_roof('roof_green', (front + 1.5, 0, 5.8), 3.2, 7.2, 1.3, along_x=True, overhang=0.15)
    m.gable_end('white', (front + 3.05, 0, 5.8), 7.2, 1.3, along_x=True)
    m.door(front, 0, 2.0, 3.0, 'white', 'wood_dark')
    for y in (-3.6, 3.6):
        m.window(front, y, 3.2, 1.2, 3.2, 'white', 'glass_dark', sill=False)
    side_windows(m, depth, width, 2, 3.0, 2.0, 'white', 'glass_dark')
    m.text('圖書館', 'navy', (front + 3.15, 0, 6.3), 0.5)
    for y in (-4.4, 4.4):
        m.street_lamp(front + 3.6, y)
    finish(m, front, 4.6)
    return m


def build_playground():
    m = Model('playground')
    L, W = 17.0, 13.0
    m.box('grass', (0, 0, 0.01), (L, W, 0.02))
    # running track border
    m.box('track', (0, -W / 2 - 0.35, 0.015), (L + 1.4, 0.7, 0.03))
    m.box('track', (0, W / 2 + 0.35, 0.015), (L + 1.4, 0.7, 0.03))
    m.box('track', (-L / 2 - 0.35, 0, 0.015), (0.7, W, 0.03))
    m.box('track', (L / 2 + 0.35, 0, 0.015), (0.7, W, 0.03))
    for y in (-W / 2 - 0.12, -W / 2 - 0.58, W / 2 + 0.12, W / 2 + 0.58):
        m.box('white', (0, y, 0.035), (L + 1.4, 0.04, 0.01))
    m.box('white', (0, 0, 0.03), (0.08, W, 0.01))
    m.cyl('white', (0, 0, 0.03), 1.8, 0.01, 32)
    m.cyl('grass', (0, 0, 0.035), 1.72, 0.012, 32)
    for x in (-L / 2 + 1.0, L / 2 - 1.0):
        for y in (-1.2, 1.2):
            m.cyl('white', (x, y, 1.0), 0.06, 2.0, 8)
        m.box('white', (x, 0, 2.0), (0.1, 2.5, 0.1))
        m.box('metal', (x + (0.5 if x < 0 else -0.5), 0, 1.0), (1.0, 2.4, 0.02))
    # bleachers along the far side
    for i in range(3):
        m.box('concrete', (0, W / 2 + 1.6 + i * 0.6, 0.25 + i * 0.4), (10.0, 0.6, 0.5 + i * 0.8))
        m.box('sky' if i % 2 else 'cloth_blue', (0, W / 2 + 1.45 + i * 0.6, 0.52 + i * 0.8), (10.0, 0.35, 0.06))
    for x in (-7.5, 7.5):
        m.tree(x, W / 2 + 2.2, 5.0, 1.6)
        m.street_lamp(x, -W / 2 - 1.4, 5.0)
    m.door_at = Vector((-3.5, 0, 0))
    m.anchors['field'] = ((-1.0, 0.8), 0)
    return m


def build_drinkshop():
    m = Model('drinkshop')
    depth, width, height = 8.0, 9.0, 3.8
    front = walls(m, depth, width, height, 'mint', 'white')
    m.box('white', (0, 0, height + 0.2), (depth + 0.3, width + 0.3, 0.4))
    m.window(front, -2.4, 1.6, 2.8, 1.8, 'white')
    # service counter with menu board
    m.box('wood_light', (front + 0.4, 2.2, 0.55), (0.8, 3.2, 1.1))
    m.box('white', (front + 0.4, 2.2, 1.15), (0.9, 3.3, 0.06))
    m.box('black', (front + 0.06, 2.2, 2.3), (0.06, 2.6, 1.0))
    for i in range(4):
        m.box('white', (front + 0.1, 1.2 + i * 0.65, 2.45), (0.02, 0.45, 0.06))
        m.box('yellow', (front + 0.1, 1.2 + i * 0.65, 2.2), (0.02, 0.3, 0.06))
    m.door(front, -0.2, 1.2, 2.3, 'white', 'glass_dark')
    m.awning(front, 0, 3.05, 8.6, 1.3, 0.4, 10, ('mint', 'awning_b'))
    sign(m, front, '飲料店', 3.45, 3.4, 0.6, 'white', 'green')
    # giant bubble-tea cup on the roof
    m.cyl('sky', (0, 0, height + 1.3), 0.95, 2.0, 20, 1.15)
    m.cyl('sand', (0, 0, height + 0.85), 0.99, 0.9, 20, 1.05)
    for a in range(10):
        r = math.radians(a * 36)
        m.sphere('black', (math.cos(r) * 0.6, math.sin(r) * 0.6, height + 0.55), 0.14, (1, 1, 1), 8)
    m.cyl('white', (0, 0, height + 2.35), 1.2, 0.12, 20)
    m.cyl('rose', (0.3, 0.2, height + 3.0), 0.09, 1.6, 8)
    side_windows(m, depth, width, 1, 0, 1.8)
    for y in (-3.4, -1.6):
        m.cyl('metal', (front + 1.6, y, 0.35), 0.05, 0.7, 8)
        m.cyl('mint', (front + 1.6, y, 0.72), 0.25, 0.06, 14)
    m.cyl('white', (front + 1.6, -2.5, 0.75), 0.45, 0.04, 16)
    m.cyl('metal', (front + 1.6, -2.5, 0.37), 0.04, 0.74, 8)
    finish(m, front)
    return m


def build_fruitshop():
    m = Model('fruitshop')
    depth, width, height = 8.0, 9.0, 3.6
    front = walls(m, depth, width, height, 'cream', 'wood_dark')
    m.gable_roof('roof_green', (0, 0, height), depth, width, 1.6, along_x=False, overhang=0.4)
    m.gable_end('cream', (0, width / 2, height), depth, 1.6, along_x=False)
    m.gable_end('cream', (0, -width / 2, height), depth, 1.6, along_x=False)
    # open front: dark interior recess
    m.box('wood_dark', (front + 0.01, 0, 1.4), (0.04, width - 1.2, 2.6))
    m.awning(front, 0, 3.0, 8.8, 1.8, 0.5, 9, ('green', 'awning_b'))
    sign(m, front, '水果店', 3.35, 3.0, 0.55, 'yellow', 'red')
    # tiered fruit stands with crates of fruit
    fruits = [('red', 0.13), ('orange', 0.14), ('yellow', 0.12), ('green', 0.13), ('purple', 0.08), ('red', 0.1)]
    for row in range(2):
        x = front + 0.8 + row * 0.75
        z = 0.95 - row * 0.35
        m.box('wood', (x, 0, z / 2), (0.7, width - 1.6, z))
        for c in range(6):
            y = -(width - 2.0) / 2 + c * (width - 2.0) / 5
            m.box('wood_light', (x, y, z + 0.1), (0.6, 1.1, 0.2))
            col, r = fruits[(c + row) % len(fruits)]
            for i in range(3):
                for j in range(2):
                    m.sphere(col, (x - 0.15 + j * 0.3, y - 0.35 + i * 0.35, z + 0.22 + r), r, (1, 1, 1), 8)
    # watermelons on the ground
    for i in range(3):
        m.sphere('green', (front + 2.6, -3.6 + i * 0.6, 0.25), 0.27, (1.25, 1, 1), 10)
    m.lantern(front + 0.2, -4.2, 2.6)
    m.lantern(front + 0.2, 4.2, 2.6)
    m.door_at = Vector((front + 3.6, 0, 0))
    return m


def build_petshop():
    m = Model('petshop')
    depth, width, height = 8.0, 9.0, 4.0
    front = walls(m, depth, width, height, 'sky', 'white')
    m.box('white', (0, 0, height + 0.2), (depth + 0.3, width + 0.3, 0.4))
    # big display windows with pets inside
    for y in (-2.4, 2.4):
        m.window(front, y, 1.6, 2.8, 2.0, 'white', 'glass', mullion=False)
    def dog(x, y, col):
        m.sphere(col, (x, y, 0.75), 0.25, (1.3, 0.8, 0.8), 10)
        m.sphere(col, (x + 0.3, y, 0.98), 0.17, (1, 1, 1), 10)
        for e in (-0.09, 0.09):
            m.sphere('wood_dark', (x + 0.3, y + e, 1.14), 0.06, (0.6, 1, 1.4), 6)
    dog(front - 0.4, -2.9, 'wood_light')
    dog(front - 0.4, -1.8, 'white')
    m.sphere('orange', (front - 0.4, 2.0, 0.72), 0.22, (1.2, 0.9, 0.9), 10)  # cat
    m.sphere('orange', (front - 0.25, 2.2, 0.9), 0.15, (1, 1, 1), 10)
    m.door(front, 0, 1.3, 2.3, 'white', 'navy')
    m.awning(front, 0, 3.1, 8.6, 1.2, 0.4, 10, ('cloth_blue', 'awning_b'))
    sign(m, front, '寵物店', 3.55, 3.2, 0.6, 'white', 'cloth_blue')
    # paw print sign on the roof
    m.box('white', (0.5, 0, height + 1.4), (0.2, 2.2, 2.0), bevel=0.05)
    m.sphere('wood_dark', (0.62, 0, height + 1.1), 0.45, (0.3, 1.1, 0.9), 12)
    for dy, dz in ((-0.55, 0.45), (-0.2, 0.7), (0.2, 0.7), (0.55, 0.45)):
        m.sphere('wood_dark', (0.62, dy, height + 1.3 + dz), 0.16, (0.3, 1, 1.2), 10)
    # dog house outside
    m.box('red', (front + 1.6, 3.3, 0.45), (0.9, 0.9, 0.9))
    m.gable_roof('wood_dark', (front + 1.6, 3.3, 0.9), 0.9, 0.9, 0.45, along_x=False, overhang=0.12, thick=0.05)
    m.box('black', (front + 2.06, 3.3, 0.35), (0.02, 0.4, 0.5))
    side_windows(m, depth, width, 1, 0, 1.8)
    finish(m, front)
    return m


def dorm(ident, title, accent, roof):
    m = Model(ident)
    depth, width, height, floors = 9.0, 15.0, 12.6, 4
    front = walls(m, depth, width, height, 'white', 'stone_dark')
    m.box(roof, (0, 0, height + 0.3), (depth + 0.4, width + 0.4, 0.6))
    for f in range(1, floors):
        m.floor_band(0, 0, depth, width, f * 3.1, accent, 0.18)
    # balconies on the upper floors
    for f in range(1, floors):
        z = f * 3.1
        for i in range(4):
            y = -width / 2 + width * (i + 0.5) / 4
            m.box('concrete', (front + 0.6, y, z + 0.05), (1.2, 2.9, 0.15))
            m.railing(front + 1.18, y - 1.45, front + 1.18, y + 1.45, z + 0.12, 1.0, accent, 6)
            m.window(front, y, z + 1.3, 1.6, 2.0, 'white', 'glass', sill=False)
            m.box(accent if (i + f) % 2 else 'awning_b', (front + 0.3, y + 1.0, z + 0.9), (0.04, 0.6, 1.2))  # curtain/laundry
    facade_windows(m, front, width, 1, 3.1, 1.6, 1.5, 1.4, skip_center=2.0, columns=4)
    side_windows(m, depth, width, floors, 3.1, 1.6)
    m.door(front, 0, 2.2, 2.5, 'white', 'glass_dark')
    m.box(accent, (front + 1.0, 0, 2.85), (2.0, 3.6, 0.2))
    for y in (-1.6, 1.6):
        m.cyl('white', (front + 1.9, y, 1.4), 0.1, 2.8, 10)
    sign(m, front, title, 3.4, 3.8, 0.6, 'white', accent if accent != 'white' else 'navy')
    # bicycle rack
    for i in range(5):
        y = 3.5 + i * 0.7
        m.cyl('black', (front + 1.6, y, 0.33), 0.32, 0.04, 14, axis='X')
        m.box('metal', (front + 1.6, y, 0.55), (0.8, 0.03, 0.03))
    m.tree(front + 2.0, -6.0, 4.5, 1.3)
    finish(m, front)
    return m


def build_hotel():
    m = Model('hotel')
    # main tower (back) and a low lobby wing toward the street; pool deck on the front-right
    depth, width = 10.0, 13.0
    tower_h = 19.0
    m.box('white', (-1.0, 0, tower_h / 2), (depth - 2.0, width, tower_h), solid=True)
    m.box('stone_dark', (-1.0, 0, 0.25), (depth - 1.9, width + 0.1, 0.5))
    tfront = -1.0 + (depth - 2.0) / 2
    for f in range(1, 6):
        z = f * 3.2
        m.floor_band(-1.0, 0, depth - 2.0, width, z, 'navy', 0.14)
        for i in range(6):
            y = -width / 2 + width * (i + 0.5) / 6
            m.box('glass_dark', (tfront + 0.03, y, z + 1.55), (0.05, 1.9, 2.4))
            m.box('white', (tfront + 0.55, y, z + 0.05), (1.1, 2.0, 0.1))
            m.railing(tfront + 1.08, y - 1.0, tfront + 1.08, y + 1.0, z + 0.1, 0.95, 'glass', 4)
    side_windows(m, depth - 2.0, width, 6, 3.2, 1.8, 'white', 'glass_dark')
    m.box('navy', (-1.0, 0, tower_h + 0.4), (depth - 1.6, width + 0.4, 0.8))
    m.text('SPA HOTEL', 'yellow', (tfront + 0.12, 0, tower_h - 1.2), 1.1)
    # lobby wing
    lw_front = depth / 2 + 1.0
    m.box('glass_dark', (lw_front - 1.5, -2.0, 2.2), (3.0, 7.0, 4.4), solid=True)
    m.box('white', (lw_front - 1.5, -2.0, 4.55), (3.4, 7.4, 0.3))
    m.door(lw_front, -2.0, 2.6, 2.8, 'white', 'glass_dark')
    # porte-cochere
    m.box('white', (lw_front + 2.0, -2.0, 3.6), (4.0, 5.0, 0.35))
    for y in (-4.2, 0.2):
        m.cyl('white', (lw_front + 3.7, y, 1.8), 0.2, 3.6, 12)
    m.text('溫泉SPA飯店', 'navy', (lw_front + 4.02, -2.0, 3.6), 0.28)
    # pool deck (front right), sunloungers, umbrellas, palms
    px, py = lw_front + 1.0, 4.4
    m.box('tile', (px, py, 0.05), (7.0, 4.6, 0.1))
    m.box('water', (px, py, 0.08), (5.6, 3.0, 0.06))
    m.box('white', (px, py - 1.55, 0.13), (5.8, 0.12, 0.08))
    m.box('white', (px, py + 1.55, 0.13), (5.8, 0.12, 0.08))
    m.box('white', (px - 2.85, py, 0.13), (0.12, 3.2, 0.08))
    m.box('white', (px + 2.85, py, 0.13), (0.12, 3.2, 0.08))
    for i in range(3):
        x = px - 2.0 + i * 2.0
        lounger = m.box('white', (x, py + 2.05, 0.3), (1.6, 0.55, 0.08))
        m.box('cloth_blue', (x - 0.55, py + 2.05, 0.45), (0.5, 0.5, 0.06))
        m.table_umbrella(x + 1.0, py - 2.0, 'cloth_blue') if i < 2 else None
    for x in (px - 3.6, px + 3.6):
        m.palm(x, py + 2.3)
    m.door_at = Vector((lw_front + 2.6, -2.0, 0))
    m.anchors['pool'] = ((px, py + 0.8), 0)
    return m


def build_minshuku():
    m = Model('minshuku')
    depth, width, height = 9.0, 10.0, 3.2
    front = walls(m, depth, width, height, 'plaster', 'stone_dark')
    # dark timber frame on the plaster
    for y in (-width / 2, -width / 6, width / 6, width / 2):
        m.box('wood_dark', (front + 0.04, y, height / 2), (0.08, 0.18, height))
    m.box('wood_dark', (front + 0.04, 0, 2.7), (0.08, width, 0.16))
    # second floor, smaller
    m.box('plaster', (-0.8, 0, height + 1.4), (depth - 2.6, width - 1.0, 2.8), solid=False)
    m.box('wood_dark', (-0.8 + (depth - 2.6) / 2 + 0.04, 0, height + 1.4), (0.08, width - 1.0, 0.14))
    m.gable_roof('roof_dark', (0, 0, height), depth, width, 0.8, along_x=False, overhang=0.7, thick=0.1)
    m.gable_roof('roof_dark', (-0.8, 0, height + 2.8), depth - 2.6, width - 1.0, 1.6, along_x=False, overhang=0.6)
    for s in (-1, 1):
        m.gable_end('plaster', (-0.8, s * (width - 1.0) / 2, height + 2.8), depth - 2.6, 1.6, along_x=False)
    for y in (-3.0, 3.0):
        m.window(front, y, 1.6, 2.2, 1.3, 'wood_dark', 'awning_b', mullion=True)
    for y in (-2.6, 0, 2.6):
        m.window(-0.8 + (depth - 2.6) / 2, y, height + 1.5, 1.4, 1.0, 'wood_dark', 'awning_b')
    # engawa veranda and sliding door with noren curtain
    m.box('wood_light', (front + 0.6, 0, 0.35), (1.2, width - 0.4, 0.1))
    for y in (-width / 2 + 0.3, width / 2 - 0.3):
        m.box('wood_dark', (front + 1.1, y, 0.17), (0.12, 0.12, 0.34))
    m.box('wood', (front + 0.03, 0, 1.2), (0.05, 1.8, 2.2))
    for i in range(3):
        m.box('cloth_indigo', (front + 0.16, -0.55 + i * 0.55, 1.9), (0.02, 0.5, 0.75))
    sign(m, front, '民宿', 2.95, 1.8, 0.42, 'wood_light', 'black')
    for y in (-1.4, 1.4):
        m.lantern(front + 0.5, y, 2.3, 0.2)
    # stone path and garden
    for i in range(4):
        m.cyl('stone', (front + 1.6 + i * 0.7, 0.2 * (-1) ** i, 0.03), 0.3, 0.06, 10)
    for y in (-3.8, 3.8):
        m.sphere('leaf', (front + 1.6, y, 0.5), 0.7, (1.2, 1, 0.7), 10)
        m.box('rock', (front + 2.4, y * 0.85, 0.25), (0.6, 0.5, 0.5), bevel=0.1)
    m.tree(front + 1.8, -4.6, 4.0, 1.1, 'leaf_light')
    finish(m, front, 3.6)
    return m


def build_onsen():
    m = Model('onsen')
    # open-air rock bath with a bamboo fence, a changing hut at the back, lanterns
    W, D = 14.0, 15.0  # width (y) x depth (x)
    m.box('stone', (0, 0, 0.03), (D, W, 0.06))
    # irregular pool: ring of rocks around water
    pr = 3.6
    # Water to the chest of someone sitting on the pool floor (the bath activity seats the pair inside).
    m.cyl('onsen', (0.6, 0, 0.28), pr, 0.56, 28)
    for i in range(22):
        a = i / 22 * math.tau
        r = pr + 0.25 + (i * 13 % 5) * 0.06
        s = 0.42 + (i * 7 % 4) * 0.08
        m.sphere('rock', (0.6 + math.cos(a) * r, math.sin(a) * r, 0.35), s, (1.3, 1.0, 0.9), 8)
    # bamboo fence on three sides, entrance gap on the street (+X) side
    def fence(x0, y0, x1, y1):
        L = math.hypot(x1 - x0, y1 - y0)
        n = int(L / 0.14)
        for i in range(n):
            t = (i + 0.5) / n
            m.cyl('bamboo', (x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, 1.1), 0.065, 2.2, 6)
        ang = math.atan2(y1 - y0, x1 - x0)
        for z in (0.6, 1.7):
            rail = m.box('wood_dark', (0, 0, 0), (L, 0.06, 0.08))
            rail.matrix_world = Matrix.Translation(((x0 + x1) / 2, (y0 + y1) / 2, z)) @ Matrix.Rotation(ang, 4, 'Z')
    hx, hy = D / 2, W / 2
    fence(-hx, -hy, hx, -hy); fence(-hx, hy, hx, hy)
    fence(hx, -hy, hx, -1.4); fence(hx, 1.4, hx, hy)
    for (cx, cy, sx, sy) in ((0, -hy, D, 0.3), (0, hy, D, 0.3), (hx, -(hy + 1.4) / 2, 0.3, hy - 1.4), (hx, (hy + 1.4) / 2, 0.3, hy - 1.4)):
        m.boxes.append((Vector((cx, cy, 1.1)), Vector((sx, sy, 2.2))))
    # entrance gate with noren
    for y in (-1.4, 1.4):
        m.box('wood_dark', (hx, y, 1.4), (0.22, 0.22, 2.8))
    m.box('wood_dark', (hx, 0, 2.85), (0.3, 3.4, 0.22))
    m.gable_roof('roof_dark', (hx, 0, 2.95), 3.6, 1.0, 0.45, along_x=False, overhang=0.15, thick=0.06)
    for i in range(3):
        m.box('red', (hx + 0.05, -0.7 + i * 0.7, 2.2), (0.02, 0.65, 0.9))
    m.text('湯', 'white', (hx + 0.08, 0.0, 2.3), 0.5)
    m.box('wood_light', (hx + 0.25, 0, 3.4), (0.1, 2.6, 0.6))
    m.text('露天溫泉', 'black', (hx + 0.32, 0, 3.4), 0.4)
    # changing hut at the back
    m.box('wood', (-hx + 2.0, 0, 1.4), (3.4, 6.0, 2.8), solid=True)
    m.gable_roof('roof_dark', (-hx + 2.0, 0, 2.8), 3.4, 6.0, 1.2, along_x=False, overhang=0.4)
    for s in (-1, 1):
        m.gable_end('wood', (-hx + 2.0, s * 3.0, 2.8), 3.4, 1.2, along_x=False)
    m.box('cloth_indigo', (-hx + 3.72, -1.2, 1.2), (0.03, 1.0, 1.6))
    m.box('red', (-hx + 3.72, 1.2, 1.2), (0.03, 1.0, 1.6))
    # stone lanterns, wooden buckets, pines
    for (x, y) in ((hx - 1.2, -hy + 1.2), (hx - 1.2, hy - 1.2), (-1.5, -hy + 1.0)):
        m.box('stone', (x, y, 0.5), (0.35, 0.35, 1.0))
        m.box('lamp', (x, y, 1.15), (0.45, 0.45, 0.35))
        m.hip_roof('stone_dark', (x, y, 1.35), 0.5, 0.5, 0.3, 0.12)
    for i in range(3):
        m.cyl('wood_light', (hx - 2.2, -3.5 + i * 0.5, 0.18), 0.2, 0.36, 12)
    for (x, y) in ((-hx + 1.0, -hy + 1.0), (-hx + 1.0, hy - 1.0), (hx - 0.8, -hy + 3.2)):
        m.tree(x, y, 5.0, 1.2, 'leaf')
    m.door_at = Vector((hx + 2.4, 0, 0))
    m.anchors['pool'] = ((0.6, 0.77), 0)  # seated in the water, facing the gate
    return m


BUILDERS = {
    'restaurant': lambda: interiors.walkin_restaurant(Model), 'snack': lambda: interiors.walkin_snack(Model),
    'bookstore': lambda: interiors.walkin_bookstore(Model), 'lodge': lambda: multi_buildings.lodge(Model),
    'school': lambda: multi_buildings.school(Model), 'library': lambda: interiors.walkin_library(Model), 'playground': build_playground,
    'drinkshop': lambda: interiors.walkin_drinkshop(Model), 'fruitshop': lambda: interiors_resort.walkin_fruitshop(Model), 'petshop': lambda: interiors.walkin_petshop(Model),
    'cafe': lambda: interiors.walkin_cafe(Model), 'conveni': lambda: interiors.walkin_conveni(Model),
    'lecture': lambda: interiors.walkin_lecture(Model), 'cafeteria': lambda: interiors.walkin_cafeteria(Model),
    'department': lambda: dept_store.department_store(Model),
    'aquarium': lambda: interiors_public.aquarium(Model),
    'restroom_park': lambda: interiors_public.restroom(Model, 'restroom_park', '公廁'),
    'restroom_campus': lambda: interiors_public.restroom(Model, 'restroom_campus', '公廁'),
    'restroom_north': lambda: interiors_public.restroom(Model, 'restroom_north', '公廁'),
    'restroom_resort': lambda: interiors_public.restroom(Model, 'restroom_resort', '公廁'),
    'park': lambda: interiors.build_park(Model), 'shrine': lambda: interiors.build_shrine(Model),
    'girls_dorm': lambda: multi_buildings.dorm(Model, 'girls_dorm', '校外女生宿舍', 'pink', 'rose'),
    'boys_dorm': lambda: multi_buildings.dorm(Model, 'boys_dorm', '校外男生宿舍', 'sky', 'navy'),
    'lottery': lambda: lottery.walkin_lottery(Model),
    'hotel': lambda: multi_buildings.hotel(Model), 'minshuku': lambda: interiors_more.walkin_minshuku(Model), 'onsen': build_onsen,
}

BUILDERS.update(campus.builders(Model))

# Vending-machine banks: place -> (search origin in local metres, or None for the door; machine front direction).
VENDING = {
    'school': (None, 0), 'library': (None, 0), 'department': (None, 0), 'aquarium': (None, 0),
    'girls_dorm': (None, 0), 'boys_dorm': (None, 0), 'hotel': (None, 0), 'lodge': (None, 0),
    'cafeteria': (None, 0), 'park': (None, 0), 'playground': ((5.0, -8.0), 90),
}


# ---------------------------------------------------------------------------------------------------- export
def export(m, path):
    deps = bpy.context.evaluated_depsgraph_get()
    sections = {}
    for ob in m.objects:
        ev = ob.evaluated_get(deps)
        me = bpy.data.meshes.new_from_object(ev)
        me.transform(ob.matrix_world)
        bm = bmesh.new()
        bm.from_mesh(me)
        bmesh.ops.triangulate(bm, faces=bm.faces)
        bm.normal_update()
        name = me.materials[0].name if me.materials else 'white'
        sec = sections.setdefault((name, bool(ob.get('roof')), int(ob.get('level', 0))), ([], [], [], []))
        P, N, U, I = sec
        for f in bm.faces:
            fn = f.normal
            base = len(P) // 3
            ax = max(range(3), key=lambda k: abs(fn[k]))
            for loop in f.loops:
                co = loop.vert.co
                n = loop.vert.normal if f.smooth else fn
                P.extend((co.x * 100, -co.y * 100, co.z * 100))
                N.extend((n.x, -n.y, n.z))
                a, b = [(1, 2), (0, 2), (0, 1)][ax]
                U.extend((co[a], -co[b]))
            I.extend((base, base + 1, base + 2))  # the Y mirror into UE already turns the winding round
        bm.free()
        bpy.data.meshes.remove(me)
    out = bytearray(b'OKPM') + struct.pack('<II', 2, len(sections))
    tris = 0
    for (name, roof, level), (P, N, U, I) in sections.items():
        rgb, street, direct = PALETTE[name]
        out += struct.pack('<3f3I', *rgb, street, (1 if direct else 0) | (2 if roof else 0) | (level << 8), len(P) // 3)
        out += struct.pack('<I', len(I))
        out += struct.pack(f'<{len(P)}f', *P) + struct.pack(f'<{len(N)}f', *N) + struct.pack(f'<{len(U)}f', *U)
        out += struct.pack(f'<{len(I)}I', *I)
        tris += len(I) // 3
    out += struct.pack('<I', len(m.boxes))
    for c, s in m.boxes:
        out += struct.pack('<6f', c.x * 100, -c.y * 100, c.z * 100, s.x * 100, s.y * 100, s.z * 100)
    out += struct.pack('<3f', m.door_at.x * 100, -m.door_at.y * 100, 0.0)
    # v2: walkable rooms and activity anchors (UE local cm; yaw in UE degrees, so the Y mirror flips its sign)
    out += struct.pack('<I', len(m.interiors))
    for c, sz in m.interiors:
        out += struct.pack('<6f', c[0] * 100, -c[1] * 100, c[2] * 100, sz[0] * 100, sz[1] * 100, sz[2] * 100)
    out += struct.pack('<I', len(m.anchors))
    for name, (pos, yaw) in m.anchors.items():
        ax, ay = pos[0], pos[1]
        az = pos[2] if len(pos) > 2 else 0.0
        raw = name.encode('utf-8')
        out += struct.pack('<I', len(raw)) + raw + struct.pack('<4f', ax * 100, -ay * 100, az * 100, -yaw)
    with open(path, 'wb') as fh:
        fh.write(out)
    return tris, len(sections)


def preview(m, path):
    scene = bpy.context.scene
    scene.render.engine = 'BLENDER_WORKBENCH'
    scene.display.shading.light = 'STUDIO'
    scene.display.shading.color_type = 'MATERIAL'
    scene.display.shading.show_shadows = True
    scene.display.shading.show_cavity = True
    scene.render.resolution_x, scene.render.resolution_y = 1100, 760
    scene.render.film_transparent = False
    world = bpy.data.worlds.get('W') or bpy.data.worlds.new('W')
    world.color = (0.62, 0.74, 0.86)
    scene.world = world
    lo = Vector((1e9, 1e9, 1e9)); hi = -lo
    for ob in m.objects:
        for c in ob.bound_box:
            w = ob.matrix_world @ Vector(c)
            lo = Vector(map(min, lo, w)); hi = Vector(map(max, hi, w))
    center = (lo + hi) / 2
    radius = (hi - lo).length / 2
    ground = m.box('grass', (center.x, center.y, -0.02), (radius * 6, radius * 6, 0.02))
    cam_data = bpy.data.cameras.new('c'); cam_data.lens = 35
    cam = bpy.data.objects.new('c', cam_data); scene.collection.objects.link(cam)
    direction = Vector((1.0, 0.75, 0.55)).normalized()
    cam.location = center + direction * radius * 2.6
    cam.rotation_euler = (center - cam.location).to_track_quat('-Z', 'Y').to_euler()
    scene.camera = cam
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    roofed = [o for o in m.objects if o.get('roof')]
    if roofed or m.interiors:
        # Interior check: roof hidden, looking down from above the front.
        for o in roofed:
            o.hide_render = True
        cam.location = center + Vector((radius * 0.9, radius * 0.35, radius * 1.6))
        cam.rotation_euler = (center - cam.location).to_track_quat('-Z', 'Y').to_euler()
        scene.render.filepath = path.replace('.png', '_inside.png')
        bpy.ops.render.render(write_still=True)
        for o in roofed:
            o.hide_render = False
    m.objects.remove(ground)


def load_font():
    for f in ('NotoSansTC-VF.ttf', 'msjhbd.ttc', 'kaiu.ttf', 'NotoSansCJKsc-Bold.otf'):
        p = os.path.join(os.environ.get('WINDIR', 'C:/Windows'), 'Fonts', f)
        if os.path.exists(p):
            try:
                return bpy.data.fonts.load(p)
            except Exception as e:  # noqa: BLE001
                print('font failed', p, e)
    return None


if __name__ == '__main__':
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    out_dir = argv[0] if argv else os.path.join(os.path.dirname(__file__), 'out')
    want_preview = '--preview' in argv
    ids = [a for a in argv[1:] if not a.startswith('--')] or list(BUILDERS)
    os.makedirs(out_dir, exist_ok=True)
    FONT = load_font()
    for ident in ids:
        model = BUILDERS[ident]()
        if ident in VENDING:
            origin, front = VENDING[ident]
            vending.bank(model, origin if origin else (model.door_at.x, model.door_at.y), front, seed=len(ident))
        tris, secs = export(model, os.path.join(out_dir, ident + '.okmesh'))
        print(f'OKMODEL {ident}: {tris} triangles, {secs} colours, {len(model.boxes)} collision boxes, door={tuple(round(v, 2) for v in model.door_at)}')
        if want_preview:
            preview(model, os.path.join(out_dir, ident + '.png'))
