import unreal

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
d = {a.get_actor_label(): a for a in eas.get_all_level_actors()}
ss = d['BLD_SkySphere']
mat = ss.get_components_by_class(unreal.StaticMeshComponent)[0].get_material(0)

# sun toward yaw -11deg, elevation 69deg: (cosP*cosY, cosP*sinY, sinElev)
mat.set_vector_parameter_value('Light direction', unreal.LinearColor(0.351, -0.068, 0.934, 0.0))
mat.set_scalar_parameter_value('Sun height', 0.93)
mat.set_vector_parameter_value('Horizon color', unreal.LinearColor(0.82, 0.84, 0.87, 1.0))
mat.set_vector_parameter_value('Zenith color', unreal.LinearColor(0.33, 0.38, 0.45, 1.0))
mat.set_vector_parameter_value('Cloud color', unreal.LinearColor(0.80, 0.82, 0.85, 1.0))
mat.set_vector_parameter_value('Sun color', unreal.LinearColor(1.0, 0.98, 0.95, 1.0))
print('sun height now:', mat.get_scalar_parameter_value('Sun height') if hasattr(mat, 'get_scalar_parameter_value') else 'n/a')
for p in mat.get_editor_property('vector_parameter_values'):
    print('V', p.get_editor_property('parameter_info'), p.get_editor_property('parameter_value'))
for p in mat.get_editor_property('scalar_parameter_values'):
    print('S', p.get_editor_property('parameter_info'), p.get_editor_property('parameter_value'))

skc = d['BLD_SkyLight'].get_components_by_class(unreal.SkyLightComponent)[0]
skc.set_editor_property('source_type', unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
skc.set_editor_property('source_type', unreal.SkyLightSourceType.SLS_CAPTURED_SCENE)
print('skylight recaptured')
