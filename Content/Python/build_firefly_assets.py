import unreal as ue

path = '/Game/VFX/Fireflies/M_FireflyGlow'
assert not ue.EditorAssetLibrary.does_asset_exist(path), path
material = ue.AssetToolsHelpers.get_asset_tools().create_asset(
    'M_FireflyGlow', '/Game/VFX/Fireflies', ue.Material, ue.MaterialFactoryNew())
assert material
material.set_editor_property('blend_mode', ue.BlendMode.BLEND_ADDITIVE)
material.set_editor_property('shading_model', ue.MaterialShadingModel.MSM_UNLIT)
material.set_editor_property('used_with_instanced_static_meshes', True)
mel = ue.MaterialEditingLibrary

color = mel.create_material_expression(material, ue.MaterialExpressionConstant3Vector, -650, -150)
color.set_editor_property('constant', ue.LinearColor(0.65, 1.0, 0.12, 1.0))
brightness = mel.create_material_expression(material, ue.MaterialExpressionConstant, -650, -20)
brightness.set_editor_property('r', 12.0)
blink = mel.create_material_expression(material, ue.MaterialExpressionPerInstanceCustomData, -650, 120)
blink.set_editor_property('data_index', 0)
blink.set_editor_property('const_default_value', 1.0)
interpolator = mel.create_material_expression(material, ue.MaterialExpressionVertexInterpolator, -420, 120)
assert mel.connect_material_expressions(blink, '', interpolator, 'VS')
fresnel = mel.create_material_expression(material, ue.MaterialExpressionFresnel, -650, 300)
fresnel.set_editor_property('exponent', 1.0)
fresnel.set_editor_property('base_reflect_fraction', 0.0)
inverse = mel.create_material_expression(material, ue.MaterialExpressionOneMinus, -420, 300)
assert mel.connect_material_expressions(fresnel, '', inverse, 'None')
softness = mel.create_material_expression(material, ue.MaterialExpressionPower, -200, 300)
softness.set_editor_property('const_exponent', 3.0)
assert mel.connect_material_expressions(inverse, '', softness, 'Base')

lit_color = mel.create_material_expression(material, ue.MaterialExpressionMultiply, -420, -150)
assert mel.connect_material_expressions(color, '', lit_color, 'A')
assert mel.connect_material_expressions(brightness, '', lit_color, 'B')
blinking_color = mel.create_material_expression(material, ue.MaterialExpressionMultiply, -200, -150)
assert mel.connect_material_expressions(lit_color, '', blinking_color, 'A')
assert mel.connect_material_expressions(interpolator, '', blinking_color, 'B')
assert mel.connect_material_property(blinking_color, '', ue.MaterialProperty.MP_EMISSIVE_COLOR)
assert mel.connect_material_property(softness, '', ue.MaterialProperty.MP_OPACITY)
mel.recompile_material(material)
assert ue.EditorAssetLibrary.save_asset(path, False)
print('FIREFLY_MATERIAL_BUILT', path)
