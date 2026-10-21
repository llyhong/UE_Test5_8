import unreal

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
d = {a.get_actor_label(): a for a in eas.get_all_level_actors()}
ss = d['BLD_SkySphere']
ss.set_actor_hidden_in_game(False)
print('skysphere visible again, rot:', ss.get_actor_rotation())

parent = unreal.load_object(None, '/Engine/EngineSky/M_Sky_Panning_Clouds2.M_Sky_Panning_Clouds2')
for prop in ('vector_parameter_defaults', 'scalar_parameter_defaults', 'vector_parameter_names', 'scalar_parameter_names'):
    try:
        vals = parent.get_editor_property(prop)
        print(prop, ':')
        for v in vals:
            print('  ', v)
    except Exception as e:
        print(prop, 'ERR', e)

mid = ss.get_components_by_class(unreal.StaticMeshComponent)[0].get_material(0)
print('mid sun height:', [p.get_editor_property('parameter_value') for p in mid.get_editor_property('scalar_parameter_values') if p.get_editor_property('parameter_info').get_editor_property('name') == 'Sun height'])
