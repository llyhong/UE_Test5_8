import unreal

eal = unreal.EditorAssetLibrary
bel = unreal.BlueprintEditorLibrary
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

# cleanup old
old = [a for a in eas.get_all_level_actors() if a.get_actor_label() == 'WBM_Corn_Field']
[eas.destroy_actor(a) for a in old]
if eal.does_asset_exist('/Game/_BLOCKOUT/BP_Corn'):
    eal.delete_asset('/Game/_BLOCKOUT/BP_Corn')
    print('old BP_Corn deleted')

eal.duplicate_asset('/Game/_BLOCKOUT/BP_Fence_Chicken', '/Game/_BLOCKOUT/BP_Corn')
bp = unreal.load_object(None, '/Game/_BLOCKOUT/BP_Corn.BP_Corn')
print('BP_Corn fresh duplicated')

sp = unreal.find_object(None, '/Game/_BLOCKOUT/BP_Corn:BP_Corn_C:Spline_GEN_VARIABLE')
sp.clear_spline_points(False)
[sp.add_spline_point(unreal.Vector(x, y, 0.0), unreal.SplineCoordinateSpace.WORLD)
 for (x, y) in ((-440.0, 450.0), (-100.0, 450.0), (-100.0, 830.0), (-440.0, 830.0))]
[sp.set_spline_point_type(i, unreal.SplinePointType.LINEAR) for i in range(4)]
sp.set_closed_loop(True)
sp.update_spline()
print('field outline set:', [(round(sp.get_world_location_at_spline_point(i).x),
                              round(sp.get_world_location_at_spline_point(i).y)) for i in range(4)])

cg = unreal.load_object(None, '/Game/_BLOCKOUT/PCG_CornGraph.PCG_CornGraph')
for n in cg.get_editor_property('nodes'):
    s = n.call_method('GetSettings', (), {})
    if s.get_class().get_name() == 'PCGSplineSamplerSettings':
        pp = s.get_editor_property('sampler_params')
        pp.set_editor_property('dimension', unreal.PCGSplineSamplingDimension.ON_INTERIOR)
        pp.set_editor_property('interior_sample_spacing', 85.0)
        pp.set_editor_property('interior_border_sample_spacing', 85.0)
        pp.set_editor_property('distance_increment', 85.0)
        pp.set_editor_property('fit_to_curve', False)
        s.set_editor_property('sampler_params', pp)
        print('sampler: interior grid @85')
eal.save_loaded_asset(cg)

pcg_t = unreal.find_object(None, '/Game/_BLOCKOUT/BP_Corn:BP_Corn_C:PCG_GEN_VARIABLE')
pcg_t.call_method('SetGraph', (cg,), {})
bel.compile_blueprint(bp)
print('bp saved:', eal.save_loaded_asset(bp))

cls = bel.generated_class(bp)
f = eas.spawn_actor_from_class(cls, unreal.Vector(0.0, 0.0, 0.0), unreal.Rotator(0.0, 0.0, 0.0))
f.set_actor_label('WBM_Corn_Field')
f.set_folder_path('PCG')
print('spawned WBM_Corn_Field')
print('level saved:', unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True))
