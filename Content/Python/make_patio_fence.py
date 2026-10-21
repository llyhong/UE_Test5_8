import unreal

eal = unreal.EditorAssetLibrary
bel = unreal.BlueprintEditorLibrary

src = '/Game/_BLOCKOUT/BP_Fence_Chicken'
dst = '/Game/_BLOCKOUT/BP_Fence_Patio'
if eal.does_asset_exist(dst):
    bp = unreal.load_object(None, dst + '.BP_Fence_Patio')
    print('existing bp loaded')
else:
    dup = eal.duplicate_asset(src, dst)
    bp = unreal.load_object(None, dst + '.BP_Fence_Patio')
    print('dup ok:', dup is not None)

sp = unreal.find_object(None, dst + ':BP_Fence_Patio_C:Spline_GEN_VARIABLE')
print('template:', sp.get_name() if sp else None)

sp.clear_spline_points(False)
[sp.add_spline_point(unreal.Vector(x, y, 0.0), unreal.SplineCoordinateSpace.WORLD)
 for (x, y) in ((200.0, -1000.0), (200.0, 1000.0), (1600.0, 1000.0), (1600.0, -1000.0))]
[sp.set_spline_point_type(i, unreal.SplinePointType.LINEAR) for i in range(4)]
sp.set_closed_loop(False)
sp.update_spline()
print('pts:', [tuple(round(v, 1) for v in (sp.get_world_location_at_spline_point(i).x,
                                            sp.get_world_location_at_spline_point(i).y)) for i in range(4)])

bel.compile_blueprint(bp)
print('bp saved:', eal.save_loaded_asset(bp))

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
have = [a for a in eas.get_all_level_actors() if a.get_actor_label() == 'WBM_Fence_Patio']
if not have:
    cls = bel.generated_class(bp)
    patio = eas.spawn_actor_from_class(cls, unreal.Vector(0.0, 0.0, 0.0), unreal.Rotator(0.0, 0.0, 0.0))
    patio.set_actor_label('WBM_Fence_Patio')
    patio.set_folder_path('PCG')
print('patio in level:', bool(have) or 'spawned now')

walls = [a for a in eas.get_all_level_actors()
         if a.get_actor_label() in ('WBM_Wall_015', 'WBM_Wall_016', 'WBM_Wall_017')]
print('deleting:', [a.get_actor_label() for a in walls])
[eas.destroy_actor(a) for a in walls]

print('level saved:', unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True))
