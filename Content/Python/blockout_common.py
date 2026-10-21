# -*- coding: utf-8 -*-
"""Whitebox blockout helpers: primitive spawning, fences, floor arrows, legend text."""
import unreal

MAT_WHITE = '/Game/_BLOCKOUT/M_Whitebox'
MAT_GRAY = '/Game/_BLOCKOUT/M_Whitebox_Gray'
MAT_CUST = '/Game/_BLOCKOUT/M_Arrow_Customer'
MAT_SERV = '/Game/_BLOCKOUT/M_Arrow_Service'

CUBE = '/Engine/BasicShapes/Cube'
SPHERE = '/Engine/BasicShapes/Sphere'
CYL = '/Engine/BasicShapes/Cylinder'
CONE = '/Engine/BasicShapes/Cone'

_mesh_cache = {}
_mat_cache = {}
_EAS = None
_N = [0]


def _mesh(path):
    if path not in _mesh_cache:
        _mesh_cache[path] = unreal.load_object(None, path + '.' + path.rsplit('/', 1)[1])
    return _mesh_cache[path]


def _mat(path):
    if path not in _mat_cache:
        _mat_cache[path] = unreal.load_object(None, path + '.' + path.rsplit('/', 1)[1])
    return _mat_cache[path]


def _eas():
    global _EAS
    if _EAS is None:
        _EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    return _EAS


def _label(name):
    _N[0] += 1
    return '%s_%03d' % (name, _N[0])


def _spawn(label, mesh, loc, rot, scale, mat):
    a = _eas().spawn_actor_from_class(unreal.StaticMeshActor, loc, rot)
    a.set_actor_label(label)
    comp = a.get_editor_property('static_mesh_component')
    comp.set_static_mesh(mesh)
    a.set_actor_scale3d(scale)
    if mat:
        comp.set_material(0, _mat(mat))
    return a


def box(label, cx, cy, cz, sx, sy, sz, mat=MAT_WHITE):
    return _spawn(_label(label), _mesh(CUBE),
                  unreal.Vector(cx, cy, cz), unreal.Rotator(0, 0, 0),
                  unreal.Vector(sx / 100.0, sy / 100.0, sz / 100.0), mat)


def cyl(label, cx, cy, cz, dia, h, mat=MAT_WHITE):
    return _spawn(_label(label), _mesh(CYL),
                  unreal.Vector(cx, cy, cz), unreal.Rotator(0, 0, 0),
                  unreal.Vector(dia / 100.0, dia / 100.0, h / 100.0), mat)


def cyl_x(label, cx, cy, cz, dia, length, mat=MAT_WHITE):
    # cylinder axis +Z, pitch 90 lays it along +X
    return _spawn(_label(label), _mesh(CYL),
                  unreal.Vector(cx, cy, cz), unreal.Rotator(90, 0, 0),
                  unreal.Vector(dia / 100.0, dia / 100.0, length / 100.0), mat)


def sph(label, cx, cy, cz, dia, mat=MAT_WHITE, zsq=1.0):
    return _spawn(_label(label), _mesh(SPHERE),
                  unreal.Vector(cx, cy, cz), unreal.Rotator(0, 0, 0),
                  unreal.Vector(dia / 100.0, dia / 100.0, dia * zsq / 100.0), mat)


def cone_up(label, cx, cy, cz, dia, h, mat=MAT_WHITE):
    return _spawn(_label(label), _mesh(CONE),
                  unreal.Vector(cx, cy, cz), unreal.Rotator(0, 0, 0),
                  unreal.Vector(dia / 100.0, dia / 100.0, h / 100.0), mat)


def cone_dir(label, x, y, z, dia, h, yaw, mat=MAT_WHITE):
    # pitch 90 lays the cone axis horizontal: +Z(tip) -> +X, yaw then rotates about world +Z
    return _spawn(_label(label), _mesh(CONE),
                  unreal.Vector(x, y, z), unreal.Rotator(90, yaw, 0),
                  unreal.Vector(dia / 100.0, dia / 100.0, h / 100.0), mat)


def text(label, x, y, z, content, size=40.0, color=(0.08, 0.08, 0.08)):
    a = _eas().spawn_actor_from_class(unreal.TextRenderActor,
                                      unreal.Vector(x, y, z),
                                      unreal.Rotator(90, -90, 0))
    a.set_actor_label(_label(label))
    trc = a.get_editor_property('text_render')
    trc.set_text(unreal.Text(content))
    trc.set_world_size(size)
    try:
        trc.set_text_render_color(unreal.LinearColor(color[0], color[1], color[2], 1.0))
    except Exception:
        pass
    try:
        trc.set_editor_property('horizontal_alignment', unreal.TextRenderHorizontalAlignment.CENTER)
        trc.set_editor_property('vertical_alignment', unreal.TextRenderVerticalAlignment.CENTER)
    except Exception:
        pass
    return a


def wall_x(label, x1, x2, y, z1, z2, mat=MAT_WHITE, t=20.0):
    return box(label, (x1 + x2) * 0.5, y, (z1 + z2) * 0.5, x2 - x1, t, z2 - z1, mat)


def wall_y(label, y1, y2, x, z1, z2, mat=MAT_WHITE, t=20.0):
    return box(label, x, (y1 + y2) * 0.5, (z1 + z2) * 0.5, t, y2 - y1, z2 - z1, mat)


_DIR = {(1, 0): 0, (0, 1): 90, (-1, 0): 180, (0, -1): 270}


def floor_arrow(x1, y1, x2, y2, mat, dashed=False, width=14.0, z=2.5):
    """Axis-aligned floor arrow from (x1,y1) to (x2,y2). Dashed mode chops into dashes."""
    dx = (x2 > x1) - (x2 < x1)
    dy = (y2 > y1) - (y2 < y1)
    if (dx != 0) == (dy != 0):
        raise ValueError('arrow must be axis-aligned')
    yaw = _DIR[(dx, dy)]
    L = abs(x2 - x1) + abs(y2 - y1)
    if dashed:
        dash, gap = 46.0, 34.0
        n = max(1, int((L - dash) // (dash + gap)) + 1)
        step = dash + gap
        for i in range(n):
            d0 = i * step
            seg = min(dash, L - d0)
            if seg <= 0:
                break
            mx = x1 + dx * (d0 + seg * 0.5)
            my = y1 + dy * (d0 + seg * 0.5)
            if dx != 0:
                box('WBM_Dash', mx, my, z, seg, width, 5.0, mat)
            else:
                box('WBM_Dash', mx, my, z, width, seg, 5.0, mat)
    else:
        mx = (x1 + x2) * 0.5
        my = (y1 + y2) * 0.5
        if dx != 0:
            box('WBM_Arrow', mx, my, z, max(L - width * 1.4, 1.0), width, 5.0, mat)
        else:
            box('WBM_Arrow', mx, my, z, width, max(L - width * 1.4, 1.0), 5.0, mat)
    hx = x2 - dx * 14.0
    hy = y2 - dy * 14.0
    cone_dir('WBM_ArrowHead', hx, hy, z + 9.0, width * 2.4, 30.0, yaw, mat)


def poly_arrow(points, mat, dashed=False, width=14.0, z=2.5):
    for i in range(len(points) - 1):
        a = points[i]
        b = points[i + 1]
        floor_arrow(a[0], a[1], b[0], b[1], mat, dashed, width, z)


def fence_rect(x1, y1, x2, y2, gate_side=None, gate_range=None, label='WBM_Fence',
               mat=MAT_WHITE, step=250.0):
    """Fenced rectangle. Posts every ~250, 2 rails. gate_side in S/N/W/E, gate_range=(a,b) along that side."""
    sides = [
        ('S', lambda t: (x1 + t, y1), lambda t: box(label + 'Post', x1 + t, y1, 55.0, 14.0, 14.0, 110.0, mat),
         lambda a, b: box(label + 'Rail', (x1 + a + x1 + b) * 0.5, y1, 56.0, b - a, 6.0, 6.0, mat),
         lambda a, b: box(label + 'Rail', (x1 + a + x1 + b) * 0.5, y1, 92.0, b - a, 6.0, 6.0, mat)),
        ('N', lambda t: (x1 + t, y2), lambda t: box(label + 'Post', x1 + t, y2, 55.0, 14.0, 14.0, 110.0, mat),
         lambda a, b: box(label + 'Rail', (x1 + a + x1 + b) * 0.5, y2, 56.0, b - a, 6.0, 6.0, mat),
         lambda a, b: box(label + 'Rail', (x1 + a + x1 + b) * 0.5, y2, 92.0, b - a, 6.0, 6.0, mat)),
        ('W', lambda t: (x1, y1 + t), lambda t: box(label + 'Post', x1, y1 + t, 55.0, 14.0, 14.0, 110.0, mat),
         lambda a, b: box(label + 'Rail', x1, (y1 + a + y1 + b) * 0.5, 56.0, 6.0, b - a, 6.0, mat),
         lambda a, b: box(label + 'Rail', x1, (y1 + a + y1 + b) * 0.5, 92.0, 6.0, b - a, 6.0, mat)),
        ('E', lambda t: (x2, y1 + t), lambda t: box(label + 'Post', x2, y1 + t, 55.0, 14.0, 14.0, 110.0, mat),
         lambda a, b: box(label + 'Rail', x2, (y1 + a + y1 + b) * 0.5, 56.0, 6.0, b - a, 6.0, mat),
         lambda a, b: box(label + 'Rail', x2, (y1 + a + y1 + b) * 0.5, 92.0, 6.0, b - a, 6.0, mat)),
    ]
    for name, _pt, post, rail_lo, rail_hi in sides:
        L = (x2 - x1) if name in ('S', 'N') else (y2 - y1)
        n = max(2, int(round(L / step)) + 1)
        for i in range(n):
            t = L * i / (n - 1.0)
            if gate_side == name and gate_range and gate_range[0] < t < gate_range[1]:
                continue
            post(t)
        segs = [(0.0, L)]
        if gate_side == name and gate_range:
            a, b = gate_range
            segs = [(0.0, a), (b, L)]
        for a, b in segs:
            if b - a > 10.0:
                rail_lo(a, b)
                rail_hi(a, b)


def cleanup_wbm():
    eas = _eas()
    doomed = [a for a in eas.get_all_level_actors()
              if a.get_actor_label().startswith('WBM_')]
    for a in doomed:
        eas.destroy_actor(a)
    return len(doomed)
