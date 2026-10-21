import unreal

eal = unreal.EditorAssetLibrary
bel = unreal.BlueprintEditorLibrary
cg = unreal.load_object(None, '/Game/_BLOCKOUT/PCG_CornGraph.PCG_CornGraph')

print('=== graph topology now ===')
all_edges = cg.get_all_edges()
print('edge count:', len(all_edges))
for e in all_edges:
    print(' ', e.get_output_node().call_method('GetSettings', (), {}).get_class().get_name(),
          e.get_output_pin_label(), '->',
          e.get_input_node().call_method('GetSettings', (), {}).get_class().get_name(),
          e.get_input_pin_label())

bp = unreal.load_object(None, '/Game/_BLOCKOUT/BP_Corn.BP_Corn')
spls = []
for o in unreal.ObjectIterator():
    try:
        pn = o.get_path_name()
    except Exception:
        continue
    if pn.startswith('/Game/_BLOCKOUT/BP_Corn.') and o.get_class().get_name() == 'SplineComponent':
        spls.append(o)
print('=== spline template points ===')
for s in spls:
    pts = [s.get_world_location_at_spline_point(i) for i in range(s.get_number_of_spline_points())]
    print(' ', s.get_name(), [(round(p.x), round(p.y)) for p in pts])
