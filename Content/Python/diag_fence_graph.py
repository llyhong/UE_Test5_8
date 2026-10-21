import unreal

def graph_info(path, name):
    g = unreal.load_object(None, path)
    print('===', name, '===')
    for n in g.get_editor_property('nodes'):
        s = n.call_method('GetSettings', (), {})
        cn = s.get_class().get_name()
        if cn == 'PCGSplineSamplerSettings':
            pp = s.get_editor_property('sampler_params')
            print(' sampler dim', pp.get_editor_property('dimension'),
                  'spacing', pp.get_editor_property('distance_increment'),
                  'interior_spacing', pp.get_editor_property('interior_sample_spacing'))
        if cn == 'PCGStaticMeshSpawnerSettings':
            sel = s.get_editor_property('mesh_selector_parameters')
            e0 = sel.get_editor_property('mesh_entries')[0]
            m = e0.get_editor_property('descriptor').get_editor_property('static_mesh')
            print(' spawner mesh:', m.get_path_name() if m else None)

graph_info('/Game/_BLOCKOUT/PCG_FenceGraph.PCG_FenceGraph', 'PCG_FenceGraph')
graph_info('/Game/_BLOCKOUT/PCG_CornGraph.PCG_CornGraph', 'PCG_CornGraph')

pcg_t = unreal.find_object(None, '/Game/_BLOCKOUT/BP_Fence:BP_Fence_C:PCG_GEN_VARIABLE')
print('BP_Fence pcg template:', pcg_t.get_name() if pcg_t else None)
if pcg_t:
    for meth in ('GetGraph',):
        try:
            g = pcg_t.call_method(meth, (), {})
            print(' template graph:', g.get_path_name() if g else None)
        except Exception as e:
            print(' ', meth, 'ERR', e)
