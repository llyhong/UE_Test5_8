import unreal
eal = unreal.EditorAssetLibrary
g = unreal.load_object(None, '/Game/_BLOCKOUT/PCG_FenceGraph.PCG_FenceGraph')
n_actor = None; n_sampler = None; n_spawner = None
for n in g.get_editor_property('nodes'):
    s = n.call_method('GetSettings', (), {})
    cn = s.get_class().get_name()
    if cn == 'PCGDataFromActorSettings': n_actor = n
    if cn == 'PCGSplineSamplerSettings': n_sampler = n
    if cn == 'PCGStaticMeshSpawnerSettings': n_spawner = n
print('found actor/sampler/spawner:', bool(n_actor), bool(n_sampler), bool(n_spawner))

prop_s = unreal.PCGDataFromActorSettings()
prop_s.set_editor_property('mode', unreal.PCGGetDataFromActorMode.GET_DATA_FROM_PROPERTY)
prop_s.set_editor_property('property_name', 'FenceMesh')
n_prop = g.add_node_instance(prop_s)

copy_s = unreal.PCGCopyAttributesSettings()
copy_s.set_editor_property('copy_all_attributes', True)
n_copy = g.add_node_instance(copy_s)

sps = n_spawner.call_method('GetSettings', (), {})
sps.set_editor_property('mesh_selector_type', unreal.PCGMeshSelectorByAttribute.static_class())
sel = sps.get_editor_property('mesh_selector_parameters')
sel.set_editor_property('attribute_name', 'FenceMesh')
print('selector:', sel.get_class().get_name(), 'attr:', sel.get_editor_property('attribute_name'))

g.remove_edge(n_sampler, 'Out', n_spawner, 'In')
print('old direct edge removed')

attempts = [('Target', 'Source'), ('Input', 'Source'), ('Target', 'Input'),
            ('In', 'Out'), ('Input', 'Input'), ('Target', 'Source pin')]
done = False; err_last = None
for tpin, spin in attempts:
    try:
        n_sampler.add_edge_to('Out', n_copy, tpin)
        n_prop.add_edge_to('Out', n_copy, spin)
        done = True
        print('copy pins:', tpin, spin)
        break
    except Exception as e:
        err_last = str(e)[:300]
if not done:
    print('EDGE FAIL:', err_last)
else:
    n_copy.add_edge_to('Out', n_spawner, 'In')
    eal.save_loaded_asset(g)
    print('saved, total edges:', len(g.get_all_edges()))
