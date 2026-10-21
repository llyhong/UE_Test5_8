# -*- coding: utf-8 -*-
"""Replace matching whitebox proxies with real props from /Game/_LOCAL/Props_Whitebox.

Rule: WBM_<Base>_<NNN> -> SM_<Base> when that asset exists. Scale reset to 1 and the
prop's bbox bottom aligned to the old whitebox bottom. No match -> untouched.
Re-run after any builder rebuild (the builder always respawns whiteboxes).
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
# asset meshes face mesh +Y at yaw 0 (user-confirmed: fridge ok at yaw 90, stove/sink at 180);
# yaw = value that turns that front where it should point: fridge on east wall -> west (90),
# sink/stove on north wall -> south into the kitchen (180)
YAW = {'Fridge': 90.0, 'Stove': 180.0, 'Sink': 180.0}
replaced, unmatched_assets = [], set(meshes)
for act in eas.get_all_level_actors():
    lb = act.get_actor_label()
    if not lb.startswith('WBM_'):
        continue
    base = lb[4:].rsplit('_', 1)[0]
    mesh = meshes.get(base)
    if not mesh:
        continue
    unmatched_assets.discard(base)
    origin, extent = act.get_actor_bounds(False)
    old_bottom = origin.z - extent.z
    comp = act.get_editor_property('static_mesh_component')
    if not comp.set_static_mesh(mesh):
        print('FAIL set mesh on', lb)
        continue
    act.set_actor_scale3d(unreal.Vector(1.0, 1.0, 1.0))
    act.set_actor_rotation(unreal.Rotator(0.0, 0.0, YAW.get(base, 0.0)), False)
    loc = act.get_actor_location()
    bb_min_z = mesh.get_bounding_box().min.z
    act.set_actor_location(unreal.Vector(loc.x, loc.y, old_bottom - bb_min_z), False, True)
    replaced.append((lb, base))
    print('replaced', lb, '->', 'SM_' + base)

print('---')
print('replaced %d actors' % len(replaced))
print('props with no same-name whitebox (kept whitebox):', sorted(unmatched_assets))

unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
print('level saved')
