import unreal

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
d = {a.get_actor_label(): a for a in eas.get_all_level_actors()}

ss = d['BLD_SkySphere']
print('ss rot:', ss.get_actor_rotation())
comps = ss.get_components_by_class(unreal.StaticMeshComponent)
print('ss comps:', [c.get_name() for c in comps])
mat = comps[0].get_material(0) if comps else None
print('ss mat:', mat.get_path_name() if mat else None)
if mat is not None:
    mel = unreal.MaterialEditingLibrary
    names = mel.get_vector_parameter_names(mat)
    print('vec params:', names)
    for n in ('SunDirection', 'SunColor', 'HorizonColor', 'ZenithColor'):
        if n in names:
            print(' ', n, mel.get_material_instance_vector_parameter_value(mat, n))
    print('scalar:', mel.get_scalar_parameter_names(mat))

skc = d['BLD_SkyLight'].get_components_by_class(unreal.SkyLightComponent)[0]
print('skylight src:', skc.get_editor_property('source_type'),
      'intensity:', skc.get_editor_property('intensity'))

dls = [a for a in eas.get_all_level_actors() if a.get_class().get_name() == 'DirectionalLight']
for a in dls:
    c = a.get_components_by_class(unreal.DirectionalLightComponent)[0]
    print('dl:', a.get_actor_label(), 'rot', a.get_actor_rotation(),
          'intensity', c.get_editor_property('intensity'),
          'color', c.get_editor_property('light_color'))
