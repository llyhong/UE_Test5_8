import unreal

eal = unreal.EditorAssetLibrary
bel = unreal.BlueprintEditorLibrary
cg = unreal.load_object(None, '/Game/_BLOCKOUT/PCG_CornGraph.PCG_CornGraph')

def cls_of(n):
    return n.call_method('GetSettings', (), {}).get_class().get_name()

for e in list(cg.get_all_edges()):
    no, ni = e.get_output_node(), e.get_input_node()
    co, ci = cls_of(no), cls_of(ni)
    if {co, ci} == {'PCGSplineSamplerSettings', 'PCGStaticMeshSpawnerSettings'}:
        cg.remove_edge(ni, e.get_input_pin_label(), no, e.get_output_pin_label())
        print('REMOVED (flow: sampler.Out -> spawner.In)')
print('edges after:', len(cg.get_all_edges()))
eal.save_loaded_asset(cg)

bp = unreal.load_object(None, '/Game/_BLOCKOUT/BP_Corn.BP_Corn')
xs = {'Spline1_GEN_VARIABLE': -355.0, 'Spline2_GEN_VARIABLE': -270.0,
      'Spline3_GEN_VARIABLE': -185.0, 'Spline4_GEN_VARIABLE': -100.0}
for o in unreal.ObjectIterator():
    try:
        pn = o.get_path_name()
    except Exception:
        continue
    if not pn.startswith('/Game/_BLOCKOUT/BP_Corn.') or o.get_class().get_name() != 'SplineComponent':
        continue
    nm = o.get_name()
    if nm in xs:
        o.clear_spline_points(False)
        o.add_spline_point(unreal.Vector(xs[nm], 450.0, 0.0), unreal.SplineCoordinateSpace.WORLD)
        o.add_spline_point(unreal.Vector(xs[nm], 830.0, 0.0), unreal.SplineCoordinateSpace.WORLD)
        o.set_spline_point_type(0, unreal.SplinePointType.LINEAR)
        o.set_spline_point_type(1, unreal.SplinePointType.LINEAR)
        o.set_closed_loop(False)
        o.update_spline()
        print('shaped', nm, '-> x =', xs[nm])
bel.compile_blueprint(bp)
print('bp saved:', eal.save_loaded_asset(bp))

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
old = [a for a in eas.get_all_level_actors() if a.get_actor_label() == 'WBM_Corn_Field']
[eas.destroy_actor(a) for a in old]
cls = bel.generated_class(bp)
f = eas.spawn_actor_from_class(cls, unreal.Vector(0.0, 0.0, 0.0), unreal.Rotator(0.0, 0.0, 0.0))
f.set_actor_label('WBM_Corn_Field')
f.set_folder_path('PCG')
print('respawned WBM_Corn_Field')
print('level saved:', unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True))
