# -*- coding: utf-8 -*-
"""Swap farm whitebox composites for the colored FarmProps meshes.

Main part carries the full asset mesh, sub-parts are destroyed (the builder recreates
everything on rebuild — re-run after). Does NOT touch the WBX_Farm_* preview actors.
Asset meshes face mesh +Y at yaw 0; cows/pigs faced +X in whitebox (head offset), so yaw
270 turns the mesh front +X. Cows/pigs float in whitebox (bodiless cyl), so they get
bottom=0 instead of the old bbox bottom.
"""
import unreal
import math

eal = unreal.EditorAssetLibrary
ROOTS = ('/Game/_LOCAL/FarmProps', '/Game/_LOCAL/Crops')

meshes = {}
for ROOT in ROOTS:
    for path in eal.list_assets(ROOT):
        obj = unreal.load_object(None, path)
        if obj and obj.get_class().get_name() == 'StaticMesh':
            meshes[obj.get_name()[3:]] = obj  # 'Cow', 'Pig', 'ChickenCoop', ...
print('farm props:', sorted(meshes))

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

# base -> (asset, yaw); None bottom = keep whitebox bbox bottom
REPLACE = {
    'Cow':       ('Cow',         270.0),
    'Pig':       ('Pig',         270.0),
    'Coop':      ('ChickenCoop', 180.0),  # entrance south into the yard
    'Well':      ('Well',          0.0),
    'Reservoir': ('WaterTank',     0.0),
    'Trough':    ('FeedTrough',    0.0),  # long axis X like the whiteboxes
}
# TreeCrown too: TreeTrunk becomes a complete SM_WildTree, the whitebox canopy is redundant
DROP = ('CowHead', 'PigHead', 'CoopRoof', 'WellPost', 'WellBar', 'WellRoof', 'ReservoirWater', 'TreeCrown')
FLOOR = ('Cow', 'Pig', 'Trough')  # real mesh stands on the ground


def swap(act, mesh, yaw):
    origin, extent = act.get_actor_bounds(False)
    bottom = 0.0 if act.get_actor_label()[4:].rsplit('_', 1)[0] in FLOOR else origin.z - extent.z
    comp = act.get_editor_property('static_mesh_component')
    cur = comp.get_editor_property('static_mesh')
    if cur is None or cur.get_path_name() != mesh.get_path_name():
        if not comp.set_static_mesh(mesh):
            print('FAIL set mesh on', act.get_actor_label())
            return False
    act.set_actor_scale3d(unreal.Vector(1.0, 1.0, 1.0))
    act.set_actor_rotation(unreal.Rotator(0.0, 0.0, yaw), False)
    loc = act.get_actor_location()
    act.set_actor_location(unreal.Vector(loc.x, loc.y, bottom - mesh.get_bounding_box().min.z), False, True)
    return True


buckets = {}
for act in eas.get_all_level_actors():
    lb = act.get_actor_label()
    if lb.startswith('WBM_'):
        buckets.setdefault(lb[4:].rsplit('_', 1)[0], []).append(act)

replaced, dropped, unmatched = [], [], set(meshes)

for base, acts in sorted(buckets.items()):
    if base in DROP:
        for act in acts:
            eas.destroy_actor(act)
            dropped.append(act.get_actor_label())
        continue
    if base not in REPLACE:
        continue
    asset, yaw = REPLACE[base]
    mesh = meshes.get(asset)
    if not mesh:
        continue
    unmatched.discard(asset)
    for act in acts:
        if swap(act, mesh, yaw):
            replaced.append(act.get_actor_label())
            print('replaced', act.get_actor_label(), '-> SM_' + asset, 'yaw', yaw)

# trees: every trunk becomes a full SM_WildTree, varied yaw to break silhouettes
tree_mesh = meshes.get('WildTree')
if tree_mesh:
    unmatched.discard('WildTree')
    for i, act in enumerate(buckets.get('TreeTrunk', [])):
        yaw = float((i * 47) % 360)
        if swap(act, tree_mesh, yaw):
            replaced.append(act.get_actor_label())
            print('replaced', act.get_actor_label(), '-> SM_WildTree yaw', yaw)

# PCG fence: point the BP_Fence instance's FenceMesh at the farm fence (soft object var)
fence = meshes.get('Fence')
if fence:
    unmatched.discard('Fence')
    for act in eas.get_all_level_actors():
        if act.get_actor_label() == 'WBM_Fences':
            act.set_editor_property('FenceMesh', fence)
            print('WBM_Fences FenceMesh ->', fence.get_path_name())
            break

# field ground: FieldTile grid fills each field rect, FieldRidge rows follow the plant
# serpentine. Ridges sink SINK into the tiles (avoids coplanar z-fighting); plants stand on
# the ridge top via PCG_CornGraph absolute offset z = tile_h + ridge_h - SINK — keep in sync.
TILE, RIDGE = meshes.get('FieldTile'), meshes.get('FieldRidge')
SINK = 3.0
if TILE and RIDGE:
    tb, rb = TILE.get_bounding_box(), RIDGE.get_bounding_box()
    tile_len, tile_wid = tb.max.x - tb.min.x, tb.max.y - tb.min.y  # long axis X
    tile_h = tb.max.z - tb.min.z
    ridge_len, ridge_h = rb.max.y - rb.min.y, rb.max.z - rb.min.z  # long axis Y
    olds = [a for a in eas.get_all_level_actors() if a.get_actor_label().startswith('WBM_FieldGround')]
    for a in olds:
        eas.destroy_actor(a)
    if olds:
        print('dropped %d old field-ground actors' % len(olds))

    def spawn_ground(mesh, label, x, y, z, yaw, sx, sy):
        a = eas.spawn_actor_from_object(mesh, unreal.Vector(x, y, z), unreal.Rotator(0.0, 0.0, yaw))
        a.set_actor_label(label)
        a.set_folder_path('Fields')
        a.set_actor_scale3d(unreal.Vector(sx, sy, 1.0))

    ground = 0
    for act in eas.get_all_level_actors():
        lb = act.get_actor_label()
        if not lb.startswith('WBM_Field_'):
            continue
        field = lb[4:].split('_', 1)[1]  # Corn / Cabbage / Carrot
        sps = act.get_components_by_class(unreal.SplineComponent)
        if not sps:
            continue
        sp = sps[0]
        n = sp.get_number_of_spline_points()
        pts = [sp.get_location_at_spline_point(i, unreal.SplineCoordinateSpace.WORLD) for i in range(n)]
        xs = [p.x for p in pts]
        ys = [p.y for p in pts]
        x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)

        nx = max(1, int(math.ceil((x1 - x0) / tile_len)))
        ny = max(1, int(math.ceil((y1 - y0) / tile_wid)))
        sx = (x1 - x0) / (nx * tile_len)
        sy = (y1 - y0) / (ny * tile_wid)
        for i in range(nx):
            for j in range(ny):
                spawn_ground(TILE, 'WBM_FieldGround_%s_T%d_%d' % (field, i, j),
                             x0 + (i + 0.5) * (x1 - x0) / nx, y0 + (j + 0.5) * (y1 - y0) / ny,
                             0.0, 0.0, sx, sy)

        OVERLAP = 2.0
        rid = 0
        for i in range(n - 1):
            a, b = pts[i], pts[i + 1]
            dx, dy = b.x - a.x, b.y - a.y
            L = math.hypot(dx, dy)
            if L < 1.0:
                continue
            ux, uy = dx / L, dy / L
            nseg = max(1, int(round(L / ridge_len)))
            step = L / nseg
            s_long = (step + OVERLAP) / ridge_len
            yaw = math.degrees(math.atan2(-ux, uy))
            for k in range(nseg):
                spawn_ground(RIDGE, 'WBM_FieldGround_%s_R%d' % (field, rid),
                             a.x + ux * step * (k + 0.5), a.y + uy * step * (k + 0.5),
                             tile_h - SINK, yaw, s_long, 1.0)
                rid += 1
        ground += nx * ny + rid
        print('%s ground: %d tiles + %d ridges, plant z=%.0f' % (field, nx * ny, rid, tile_h + ridge_h - SINK))
    print('field ground actors:', ground)

print('---')
print('replaced %d actors, dropped %d whitebox parts' % (len(replaced), len(dropped)))
print('farm props not placed:', sorted(unmatched) or 'none')
