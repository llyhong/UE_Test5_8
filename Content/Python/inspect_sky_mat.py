import unreal

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
d = {a.get_actor_label(): a for a in eas.get_all_level_actors()}
ss = d['BLD_SkySphere']
mid = ss.get_components_by_class(unreal.StaticMeshComponent)[0].get_material(0)
parent = mid.get_editor_property('parent')
ppath = parent.get_path_name()
print('parent:', ppath)

mel = unreal.MaterialEditingLibrary
node = mel.get_material_property_input_node(parent, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
print('emissive node:', node.get_class().get_name() if node else None)

exprs = []
for o in unreal.ObjectIterator():
    try:
        pn = o.get_path_name()
    except Exception:
        continue
    if pn.startswith(ppath + '.'):
        exprs.append(o)
print('subobjects:', len(exprs))
for e in exprs:
    cn = e.get_class().get_name()
    extra = ''
    for prop in ('parameter_name', 'desc', 'constant', 'texture_object', 'input_name'):
        try:
            v = e.get_editor_property(prop)
            extra += ' | %s=%s' % (prop, v)
        except Exception:
            pass
    print(cn, extra)
