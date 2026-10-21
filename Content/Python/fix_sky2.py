import unreal

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
d = {a.get_actor_label(): a for a in eas.get_all_level_actors()}
ss = d['BLD_SkySphere']
ss.set_actor_rotation(unreal.Rotator(0.0, 69.0, 169.0), False)
print('ss rot now:', ss.get_actor_rotation())

mat = ss.get_components_by_class(unreal.StaticMeshComponent)[0].get_material(0)
mat.set_vector_parameter_value('Light direction', unreal.LinearColor(0.351, -0.068, -0.934, 0.0))
mat.set_scalar_parameter_value('Sun height', 0.93)
print('light direction set (z=-0.934, light travels downward)')

skc = d['BLD_SkyLight'].get_components_by_class(unreal.SkyLightComponent)[0]
skc.set_editor_property('source_type', unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
skc.set_editor_property('source_type', unreal.SkyLightSourceType.SLS_CAPTURED_SCENE)
print('skylight recaptured')
