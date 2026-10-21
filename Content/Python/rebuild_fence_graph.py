import unreal

eal = unreal.EditorAssetLibrary
SM_FENCE = unreal.load_object(None, '/Game/_BLOCKOUT/SM_Fence.SM_Fence')
CONE = unreal.load_object(None, '/Engine/BasicShapes/Cone.Cone')

# ---------- 1) rebuild PCG_FenceGraph with fresh nodes/settings ----------
g = unreal.load_object(None, '/Game/_BLOCKOUT/PCG_FenceGraph.PCG_FenceGraph')
mode = None
kill = []
for n in g.get_editor_property('nodes'):
    s = n.call_method('GetSettings', (), {})
    cn = s.get_class().get_name()
    if cn == 'PCGDataFromActorSettings':
        try:
            mode = s.get_editor_property('mode')
        except Exception:
            mode = None
    if cn != 'PCGGraphInputOutputSettings':
        kill.append(n)
for n in kill:
    g.remove_node(n)
print('old nodes removed:', len(kill), '| mode captured:', mode)

actor_s = unreal.PCGDataFromActorSettings()
if mode is not None:
    try:
        actor_s.set_editor_property('mode', mode)
    except Exception as e:
        print('mode set ERR', e)
n_actor = g.add_node_instance(actor_s)

samp_s = unreal.PCGSplineSamplerSettings()
pp = samp_s.get_editor_property('sampler_params')
pp.set_editor_property('dimension', unreal.PCGSplineSamplingDimension.ON_SPLINE)
pp.set_editor_property('mode', unreal.PCGSplineSamplingMode.DISTANCE)
pp.set_editor_property('distance_increment', 250.0)
pp.set_editor_property('fit_to_curve', True)
pp.set_editor_property('treat_spline_as_polyline', True)
samp_s.set_editor_property('sampler_params', pp)
n_sampler = g.add_node_instance(samp_s)

sp_s = unreal.PCGStaticMeshSpawnerSettings()
sel = sp_s.get_editor_property('mesh_selector_parameters')
entry = unreal.PCGMeshSelectorWeightedEntry()
desc = entry.get_editor_property('descriptor')
desc.set_editor_property('static_mesh', SM_FENCE)
entry.set_editor_property('descriptor', desc)
entry.set_editor_property('weight', 1.0)
sel.set_editor_property('mesh_entries', [entry])
n_spawner = g.add_node_instance(sp_s)

n_actor.add_edge_to('Out', n_sampler, 'Spline')
n_sampler.add_edge_to('Out', n_spawner, 'In')
n_spawner.add_edge_to('Out', g.get_output_node(), 'Out')
eal.save_loaded_asset(g)
print('fence graph rebuilt, edges:', len(g.get_all_edges()))

# ---------- 2) re-apply corn params in place ----------
cg = unreal.load_object(None, '/Game/_BLOCKOUT/PCG_CornGraph.PCG_CornGraph')
for n in cg.get_editor_property('nodes'):
    s = n.call_method('GetSettings', (), {})
    cn = s.get_class().get_name()
    if cn == 'PCGSplineSamplerSettings':
        pp = s.get_editor_property('sampler_params')
        pp.set_editor_property('dimension', unreal.PCGSplineSamplingDimension.ON_INTERIOR)
        pp.set_editor_property('interior_sample_spacing', 80.0)
        pp.set_editor_property('interior_border_sample_spacing', 80.0)
        pp.set_editor_property('distance_increment', 80.0)
        pp.set_editor_property('fit_to_curve', False)
        s.set_editor_property('sampler_params', pp)
    if cn == 'PCGStaticMeshSpawnerSettings':
        sel = s.get_editor_property('mesh_selector_parameters')
        entry = sel.get_editor_property('mesh_entries')[0]
        desc = entry.get_editor_property('descriptor')
        desc.set_editor_property('static_mesh', CONE)
        entry.set_editor_property('descriptor', desc)
        entries = sel.get_editor_property('mesh_entries')
        entries[0] = entry
        sel.set_editor_property('mesh_entries', entries)
eal.save_loaded_asset(cg)
print('corn graph re-applied')

# ---------- 3) verify decoupling ----------
for path, tag in (('/Game/_BLOCKOUT/PCG_FenceGraph.PCG_FenceGraph', 'FENCE'),
                  ('/Game/_BLOCKOUT/PCG_CornGraph.PCG_CornGraph', 'CORN')):
    gg = unreal.load_object(None, path)
    for n in gg.get_editor_property('nodes'):
        s = n.call_method('GetSettings', (), {})
        cn = s.get_class().get_name()
        if cn == 'PCGSplineSamplerSettings':
            pp = s.get_editor_property('sampler_params')
            print(tag, 'dim', pp.get_editor_property('dimension'), 'spacing', pp.get_editor_property('distance_increment'))
        if cn == 'PCGStaticMeshSpawnerSettings':
            m = s.get_editor_property('mesh_selector_parameters').get_editor_property('mesh_entries')[0].get_editor_property('descriptor').get_editor_property('static_mesh')
            print(tag, 'mesh:', m.get_path_name() if m else None)
