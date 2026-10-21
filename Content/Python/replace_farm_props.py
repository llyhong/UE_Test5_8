# -*- coding: utf-8 -*-
"""Swap farm whitebox composites for the colored FarmProps meshes.

Main part carries the full asset mesh, sub-parts are destroyed (the builder recreates
everything on rebuild — re-run after). Does NOT touch the WBX_Farm_* preview actors.
Asset meshes face mesh +Y at yaw 0; cows/pigs faced +X in whitebox (head offset), so yaw
270 turns the mesh front +X. Cows/pigs float in whitebox (bodiless cyl), so they get
bottom=0 instead of the old bbox bottom.
"""
import unreal

eal = unreal.EditorAssetLibrary
ROOT = '/Game/_LOCAL/FarmProps'

meshes = {}
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
}
DROP = ('CowHead', 'PigHead', 'CoopRoof', 'WellPost', 'WellBar', 'WellRoof', 'ReservoirWater')
FLOOR = ('Cow', 'Pig')  # whitebox body floats; real mesh stands on the ground


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

print('---')
print('replaced %d actors, dropped %d whitebox parts' % (len(replaced), len(dropped)))
print('farm props not placed:', sorted(unmatched) or 'none')
