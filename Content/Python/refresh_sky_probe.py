import unreal

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
d = {a.get_actor_label(): a for a in eas.get_all_level_actors()}
ss = d['BLD_SkySphere']
mat = ss.get_components_by_class(unreal.StaticMeshComponent)[0].get_material(0)

def dump(tag):
    [print(tag, p) for p in mat.get_editor_property('vector_parameter_values')]
    [print(tag, p) for p in mat.get_editor_property('scalar_parameter_values')]

dump('BEFORE')
ss.call_method('RefreshMaterial', (), {})
dump('AFTER ')
