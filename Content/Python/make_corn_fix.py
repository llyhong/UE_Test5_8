import unreal

eal = unreal.EditorAssetLibrary
bel = unreal.BlueprintEditorLibrary
cg = unreal.load_object(None, '/Game/_BLOCKOUT/PCG_CornGraph.PCG_CornGraph')

for e in cg.get_all_edges():
    no, ni = e.get_output_node(), e.get_input_node()
    co = no.call_method('GetSettings', (), {}).get_class().get_name()
    ci = ni.call_method('GetSettings', (), {}).get_class().get_name()
    print('edge:', co, e.get_output_pin_label(), '->', ci, e.get_input_pin_label())
    if {co, ci} == {'PCGSplineSamplerSettings', 'PCGStaticMeshSpawnerSettings'}:
        cg.remove_edge(no, e.get_output_pin_label(), ni, e.get_input_pin_label())
        print('REMOVED')
print('edges after:', len(cg.get_all_edges()))
eal.save_loaded_asset(cg)

# ---- spline templates
bp = unreal.load_object(None, '/Game/_BLOCKOUT/BP_Corn.BP_Corn')
spls = []
for o in unreal.ObjectIterator():
    try:
        pn = o.get_path_name()
    except Exception:
        continue
    if pn.startswith('/Game/_BLOCKOUT/BP_Corn.') and o.get_class().get_name() == 'SplineComponent':
        spls.append(o)
print('spline templates:', len(spls), [(s.get_name(), s.get_number_of_spline_points()) for s in spls])

xs = (-355.0, -270.0, -185.0, -100.0)
done = 0
for sp in spls:
    if sp.get_number_of_spline_points() == 2:
        continue
    sp.clear_spline_points(False)
    sp.add_spline_point(unreal.Vector(xs[done % 4], 450.0, 0.0), unreal.SplineCoordinateSpace.WORLD)
    sp.add_spline_point(unreal.Vector(xs[done % 4], 830.0, 0.0), unreal.SplineCoordinateSpace.WORLD)
    sp.set_spline_point_type(0, unreal.SplinePointType.LINEAR)
    sp.set_spline_point_type(1, unreal.SplinePointType.LINEAR)
    sp.set_closed_loop(False)
    sp.update_spline()
    done += 1
print('shaped extra rows:', done)
bel.compile_blueprint(bp)
print('bp saved:', eal.save_loaded_asset(bp))

# ---- respawn field + save level
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
have = [a for a in eas.get_all_level_actors() if a.get_actor_label() == 'WBM_Corn_Field']
if not have:
    cls = bel.generated_class(bp)
    f = eas.spawn_actor_from_class(cls, unreal.Vector(0.0, 0.0, 0.0), unreal.Rotator(0.0, 0.0, 0.0))
    f.set_actor_label('WBM_Corn_Field')
    f.set_folder_path('PCG')
    print('spawned WBM_Corn_Field')
leftover = [a for a in eas.get_all_level_actors()
            if a.get_actor_label().startswith('WBM_Corn') and a.get_actor_label() != 'WBM_Corn_Field']
print('old corn cones remaining:', len(leftover))
print('level saved:', unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True))
