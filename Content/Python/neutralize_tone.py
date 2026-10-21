import unreal

mel = unreal.MaterialEditingLibrary
mat = unreal.load_object(None, '/Game/_BLOCKOUT/M_Whitebox.M_Whitebox')
const = mel.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -350, -50)
const.set_editor_property('constant', unreal.LinearColor(0.6, 0.6, 0.6, 1.0))
mel.connect_material_property(const, '', unreal.MaterialProperty.MP_BASE_COLOR)
mel.recompile_material(mat)
print('mat saved', unreal.EditorAssetLibrary.save_loaded_asset(mat))

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
d = {a.get_actor_label(): a for a in eas.get_all_level_actors()}
skc = d['BLD_SkyLight'].get_components_by_class(unreal.SkyLightComponent)[0]
skc.set_editor_property('intensity', 1.0)
if hasattr(skc, 'recapture'):
    skc.recapture()
else:
    skc.call_method('Recapture', (), {})
print('skylight intensity ->', skc.get_editor_property('intensity'))
