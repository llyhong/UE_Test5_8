import unreal

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actors = eas.get_all_level_actors()

print('sky spheres:', [a.get_actor_label() for a in actors if a.get_class().get_name() == 'BP_Sky_Sphere_C'])
print('atmosphere/cloud/fog classes:',
      [a.get_class().get_name() for a in actors
       if 'Atmosphere' in a.get_class().get_name() or 'Cloud' in a.get_class().get_name()
       or 'Fog' in a.get_class().get_name()])

for a in actors:
    if a.get_class().get_name() == 'ExponentialHeightFog':
        c = a.get_components_by_class(unreal.ExponentialHeightFogComponent)[0]
        for p in ('fog_density', 'fog_inscattering_luminance', 'directional_inscattering_luminance',
                  'fog_height_falloff', 'start_distance'):
            try:
                print('fog', a.get_actor_label(), p, c.get_editor_property(p))
            except Exception as e:
                print('fog', a.get_actor_label(), p, 'ERR', e)

d = {x.get_actor_label(): x for x in actors}
ss = d['BLD_SkySphere']
mat = ss.get_components_by_class(unreal.StaticMeshComponent)[0].get_material(0)
print('mid recheck: sun height =', [p.get_editor_property('parameter_value') for p in mat.get_editor_property('scalar_parameter_values') if p.get_editor_property('parameter_info').get_editor_property('name') == 'Sun height'])
print('mid recheck: zenith =', [p.get_editor_property('parameter_value') for p in mat.get_editor_property('vector_parameter_values') if p.get_editor_property('parameter_info').get_editor_property('name') == 'Zenith color'])

ss.set_actor_hidden_in_game(True)
print('skysphere hidden, firing shot')
w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.SystemLibrary.execute_console_command(w, 'HighResShot 2560x1440', None)
