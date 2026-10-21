import unreal

mel = unreal.MaterialEditingLibrary

def make_gray(path):
    mat = unreal.load_object(None, path)
    const = mel.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -400, -80)
    const.set_editor_property('constant', unreal.LinearColor(0.6, 0.6, 0.6, 1.0))
    mel.connect_material_property(const, '', unreal.MaterialProperty.MP_BASE_COLOR)
    mel.recompile_material(mat)
    ok = unreal.EditorAssetLibrary.save_loaded_asset(mat)
    print(path, 'gray, saved =', ok, ', exprs =', mel.get_num_material_expressions(mat))

make_gray('/Game/_BLOCKOUT/M_Whitebox.M_Whitebox')
make_gray('/Game/_BLOCKOUT/M_Whitebox_Gray.M_Whitebox_Gray')

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
d = {a.get_actor_label(): a for a in eas.get_all_level_actors()}

ss = d['BLD_SkySphere']
ss.set_actor_rotation(unreal.Rotator(0.0, -69.0, -11.0), False)
print('ss rot now', ss.get_actor_rotation())
ss.call_method('RefreshMaterial', (), {})

skc = d['BLD_SkyLight'].get_components_by_class(unreal.SkyLightComponent)[0]
skc.set_editor_property('source_type', unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
skc.set_editor_property('source_type', unreal.SkyLightSourceType.SLS_CAPTURED_SCENE)
print('skylight recapture forced')
