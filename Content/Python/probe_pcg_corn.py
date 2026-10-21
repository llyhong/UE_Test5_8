import unreal

g = unreal.load_object(None, '/Game/_BLOCKOUT/PCG_FenceGraph.PCG_FenceGraph')
nodes = g.get_editor_property('nodes')
for n in nodes:
    s = n.call_method('GetSettings', (), {})
    cn = s.get_class().get_name()
    if 'Spawner' in cn:
        sel = s.get_editor_property('mesh_selector_parameters')
        entries = sel.get_editor_property('mesh_entries')
        print('entries:', len(entries))
        e0 = entries[0]
        print('entry repr:', e0)
        print('entry props:', [m for m in dir(e0) if not m.startswith('_')])
        print('node edge api:', [m for m in dir(n) if 'edge' in m.lower()])
print('graph edge api:', [m for m in dir(g) if 'edge' in m.lower() or 'link' in m.lower()])
