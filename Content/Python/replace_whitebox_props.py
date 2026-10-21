# -*- coding: utf-8 -*-
"""Replace whitebox proxies with real props from /Game/_LOCAL/Props_Whitebox.

Same-name rule for Fridge/Stove/Sink; the other assets map semantically (listed in
REPLACE). Composite whiteboxes: one part carries the full asset mesh, the rest are
destroyed (DROP) — the builder recreates them on rebuild, so re-run this after any
rebuild. Asset meshes face mesh +Y at yaw 0 (user-confirmed).
"""
import unreal

eal = unreal.EditorAssetLibrary
ROOT = '/Game/_LOCAL/Props_Whitebox'

meshes = {}
for path in eal.list_assets(ROOT):
    obj = unreal.load_object(None, path)
    if obj and obj.get_class().get_name() == 'StaticMesh':
        name = path.rsplit('/', 1)[-1].split('.')[0]
        meshes[name[3:] if name.startswith('SM_') else name] = obj
print('props available:', sorted(meshes))

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

# base -> (asset, yaw): fridge on east wall doors west (90); north-wall kitchen gear and
# customer-facing cashier gear face south (180); dining tables neutral (0)
REPLACE = {
    'Fridge':          ('Fridge',        90.0),
    'Stove':           ('Stove',        180.0),
    'Sink':            ('Sink',         180.0),
    'KitchenTable':    ('PrepTable',    180.0),
    'CashierCounter':  ('BarCounter',   180.0),
    'CashierRegister': ('CashRegister', 180.0),
    'TableTop':        ('DiningTable',   0.0),
    'MenuBoard':       ('HangingSign',  180.0),
}
# sub-parts superseded by a complete prop mesh (QueuePost handled separately: one becomes
# the SM_QueueBarrier, the rest are dropped)
DROP = ('TableLeg', 'ChairBack', 'QueueBar')
TABLE_ROWS = (-600.0, -200.0)
# bottom overrides: MenuBoard stands on the cashier counter top; DiningTable is a complete
# grounded table — must NOT inherit the old floating TableTop-only bottom (z=69)
BOTTOM_OVERRIDE = {'MenuBoard': 100.0, 'TableTop': 0.0}


def swap(act, mesh, yaw, bottom=None):
    origin, extent = act.get_actor_bounds(False)
    if bottom is None:
        bottom = origin.z - extent.z
    comp = act.get_editor_property('static_mesh_component')
    cur = comp.get_editor_property('static_mesh')
    if cur is None or cur.get_path_name() != mesh.get_path_name():
        # set_static_mesh returns False when the mesh is already assigned, so only call
        # it on an actual change; the transform below is always re-applied
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

replaced, dropped, unmatched_assets = [], [], set(meshes)

for base, acts in sorted(buckets.items()):
    if base in DROP:
        for act in acts:
            eas.destroy_actor(act)
            dropped.append(act.get_actor_label())
        continue
    if base == 'ChairSeat':
        mesh = meshes.get('Chair')
        if not mesh:
            continue
        unmatched_assets.discard('Chair')
        for act in acts:
            y = act.get_actor_location().y
            ty = min(TABLE_ROWS, key=lambda r: abs(y - r))
            yaw = 0.0 if y < ty else 180.0  # sitter faces the table
            if swap(act, mesh, yaw):
                replaced.append(act.get_actor_label())
                print('replaced', act.get_actor_label(), '-> SM_Chair yaw', yaw)
        continue
    if base not in REPLACE:
        continue
    asset, yaw = REPLACE[base]
    mesh = meshes.get(asset)
    if not mesh:
        continue
    unmatched_assets.discard(asset)
    for act in acts:
        if swap(act, mesh, yaw, BOTTOM_OVERRIDE.get(base)):
            replaced.append(act.get_actor_label())
            print('replaced', act.get_actor_label(), '-> SM_' + asset, 'yaw', yaw)

# one SM_QueueBarrier centered on the queue line: reuse the middle post when present,
# otherwise spawn a fresh actor (a previous run may have destroyed all posts)
barrier = meshes.get('QueueBarrier')
if barrier:
    unmatched_assets.discard('QueueBarrier')
    posts = buckets.get('QueuePost', [])
    if posts:
        target = min(posts, key=lambda a: abs(a.get_actor_location().x - 1270.0))
        others = [a for a in posts if a is not target]
    else:
        target = eas.spawn_actor_from_object(barrier, unreal.Vector(1270.0, -200.0, 0.0), unreal.Rotator(0.0, 0.0, 0.0))
        target.set_actor_label('WBM_QueueBarrier')
        others = []
    for act in others:
        eas.destroy_actor(act)
        dropped.append(act.get_actor_label())
    if swap(target, barrier, 0.0, 0.0):
        replaced.append(target.get_actor_label())
        print('replaced', target.get_actor_label(), '-> SM_QueueBarrier')

print('---')
print('replaced %d actors, dropped %d whitebox parts' % (len(replaced), len(dropped)))
print('props not placed:', sorted(unmatched_assets) or 'none — all 10 in the level')

unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
print('level saved')
