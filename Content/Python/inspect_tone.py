import unreal

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
d = {a.get_actor_label(): a for a in eas.get_all_level_actors()}

mat = unreal.load_object(None, '/Game/_BLOCKOUT/M_Whitebox.M_Whitebox')
mel = unreal.MaterialEditingLibrary
print('mat expr count', mel.get_num_material_expressions(mat))

fog = d['BLD_HeightFog'].get_components_by_class(unreal.ExponentialHeightFogComponent)[0]
print('fog color', fog.get_editor_property('fog_inscattering_luminance'))
print('fog density', fog.get_editor_property('fog_density'), 'start', fog.get_editor_property('fog_height_falloff'))
print('dir inscatter', fog.get_editor_property('directional_inscattering_color'), 'exponent', fog.get_editor_property('directional_inscattering_exponent'))

sun = d['BLD_DirectionalLight']
ss = d['BLD_SkySphere']
print('sun rot', sun.get_actor_rotation())
print('ss rot', ss.get_actor_rotation())
print('ss vars', [m for m in dir(ss) if 'light' in m.lower() or 'sun' in m.lower()])
