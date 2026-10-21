import unreal

eal = unreal.EditorAssetLibrary
SM_FENCE = unreal.load_object(None, '/Game/_BLOCKOUT/SM_Fence.SM_Fence')

g = unreal.load_object(None, '/Game/_BLOCKOUT/PCG_FenceGraph.PCG_FenceGraph')
for n in g.get_editor_property('nodes'):
    s = n.call_method('GetSettings', (), {})
    cn = s.get_class().get_name()
    if cn == 'PCGSplineSamplerSettings':
        pp = s.get_editor_property('sampler_params')
        pp.set_editor_property('dimension', unreal.PCGSplineSamplingDimension.ON_SPLINE)
        pp.set_editor_property('distance_increment', 250.0)
        pp.set_editor_property('fit_to_curve', True)
        pp.set_editor_property('treat_spline_as_polyline', True)
        s.set_editor_property('sampler_params', pp)
        print('fence sampler restored: ON_SPLINE @250')
    if cn == 'PCGStaticMeshSpawnerSettings':
        sel = s.get_editor_property('mesh_selector_parameters')
        entry = sel.get_editor_property('mesh_entries')[0]
        desc = entry.get_editor_property('descriptor')
        desc.set_editor_property('static_mesh', SM_FENCE)
        entry.set_editor_property('descriptor', desc)
        entries = sel.get_editor_property('mesh_entries')
        entries[0] = entry
        sel.set_editor_property('mesh_entries', entries)
        print('fence spawner restored: SM_Fence')
eal.save_loaded_asset(g)

# re-check both graphs for shared-state coupling
for path, tag in (('/Game/_BLOCKOUT/PCG_FenceGraph.PCG_FenceGraph', 'FENCE'),
                  ('/Game/_BLOCKOUT/PCG_CornGraph.PCG_CornGraph', 'CORN')):
    gg = unreal.load_object(None, path)
    for n in gg.get_editor_property('nodes'):
        s = n.call_method('GetSettings', (), {})
        cn = s.get_class().get_name()
        if cn == 'PCGSplineSamplerSettings':
            pp = s.get_editor_property('sampler_params')
            print(tag, 'dim', pp.get_editor_property('dimension'),
                  'spacing', pp.get_editor_property('distance_increment'),
                  'interior', pp.get_editor_property('interior_sample_spacing'))
        if cn == 'PCGStaticMeshSpawnerSettings':
            sel = s.get_editor_property('mesh_selector_parameters')
            m = sel.get_editor_property('mesh_entries')[0].get_editor_property('descriptor').get_editor_property('static_mesh')
            print(tag, 'mesh:', m.get_path_name() if m else None)
