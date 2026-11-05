# -*- coding: utf-8 -*-
import unreal

P = '/Game/_LOCAL/KitchenAssets/Meshes/SM_Knife_Pivot.SM_Knife_Pivot'
m = unreal.load_object(None, P)
print('mesh', m)
if m:
    bb = m.get_bounding_box()
    print(' bbox min', bb.min, 'max', bb.max,
          'size', bb.max - bb.min)
    print(' pivot offset', -bb.min)
    print(' materials', [m.get_material(i).get_name() if m.get_material(i) else None
                         for i in range(m.get_num_sections(0))])

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
users = []
for a in eas.get_all_level_actors():
    try:
        c = a.get_editor_property('static_mesh_component')
    except Exception:
        continue
    if c is None:
        continue
    sm = c.get_editor_property('static_mesh')
    if sm and sm.get_path_name() == P:
        users.append(a.get_actor_label())
print('actors using knife:', users or 'none')

# asset registry: which packages reference this mesh
ar = unreal.AssetRegistryHelpers.get_asset_registry()
refs = ar.get_referencers(unreal.Name('/Game/_LOCAL/KitchenAssets/Meshes/SM_Knife_Pivot'))
print('referencers:', [str(r) for r in refs])

print('--- sequencer python api ---')
names = [n for n in dir(unreal) if 'MovieScene' in n or 'LevelSequence' in n]
print(names)
