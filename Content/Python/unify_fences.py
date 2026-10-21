import unreal

eal = unreal.EditorAssetLibrary
bel = unreal.BlueprintEditorLibrary
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

LABELS = ('WBM_Fence_CowPen', 'WBM_Fence_PigPen', 'WBM_Fence_ChickenYard', 'WBM_Fence_Patio')
old = [a for a in eas.get_all_level_actors() if a.get_actor_label() in LABELS]
print('removing level fence actors:', [a.get_actor_label() for a in old])
[eas.destroy_actor(a) for a in old]

if not eal.does_asset_exist('/Game/_BLOCKOUT/BP_Fence'):
    eal.duplicate_asset('/Game/_BLOCKOUT/BP_Fence_Chicken', '/Game/_BLOCKOUT/BP_Fence')
bp = unreal.load_object(None, '/Game/_BLOCKOUT/BP_Fence.BP_Fence')
print('BP_Fence ready')

for p in ('/Game/_BLOCKOUT/BP_Fence_Cow', '/Game/_BLOCKOUT/BP_Fence_Pig',
          '/Game/_BLOCKOUT/BP_Fence_Chicken', '/Game/_BLOCKOUT/BP_Fence_Patio'):
    if eal.does_asset_exist(p):
        eal.delete_asset(p)
print('old fence BPs deleted')

sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
hs = sds.k2_gather_subobject_data_for_blueprint(bp)
for i in range(3):
    p = unreal.AddNewSubobjectParams()
    p.set_editor_property('parent_handle', hs[0])
    p.set_editor_property('new_class', unreal.SplineComponent)
    p.set_editor_property('blueprint_context', bp)
    sds.add_new_subobject(p)
bel.compile_blueprint(bp)
print('3 splines added')

SHAPES = {
    'Spline_GEN_VARIABLE':   ([(-1600.0, -450.0), (-1100.0, -450.0), (-1100.0, 350.0), (-1600.0, 350.0)], True),
    'Spline1_GEN_VARIABLE':  ([(-950.0, -450.0), (-500.0, -450.0), (-500.0, 350.0), (-950.0, 350.0)], True),
    'Spline2_GEN_VARIABLE':  ([(-450.0, -450.0), (-80.0, -450.0), (-80.0, -50.0), (-450.0, -50.0)], True),
    'Spline3_GEN_VARIABLE':  ([(200.0, -1000.0), (200.0, 1000.0), (1600.0, 1000.0), (1600.0, -1000.0)], False),
}
found = 0
for o in unreal.ObjectIterator():
    try:
        pn = o.get_path_name()
    except Exception:
        continue
    if not pn.startswith('/Game/_BLOCKOUT/BP_Fence.') or o.get_class().get_name() != 'SplineComponent':
        continue
    nm = o.get_name()
    if nm not in SHAPES:
        print('UNEXPECTED spline template:', nm)
        continue
    pts, closed = SHAPES[nm]
    o.clear_spline_points(False)
    [o.add_spline_point(unreal.Vector(x, y, 0.0), unreal.SplineCoordinateSpace.WORLD) for (x, y) in pts]
    [o.set_spline_point_type(i, unreal.SplinePointType.LINEAR) for i in range(len(pts))]
    o.set_closed_loop(closed)
    o.update_spline()
    found += 1
print('shaped templates:', found)

pcg_t = unreal.find_object(None, '/Game/_BLOCKOUT/BP_Fence:BP_Fence_C:PCG_GEN_VARIABLE')
pcg_t.call_method('SetGraph', (unreal.load_object(None, '/Game/_BLOCKOUT/PCG_FenceGraph.PCG_FenceGraph'),), {})

bel.compile_blueprint(bp)
print('bp saved:', eal.save_loaded_asset(bp))

f = eas.spawn_actor_from_class(bel.generated_class(bp), unreal.Vector(0.0, 0.0, 0.0), unreal.Rotator(0.0, 0.0, 0.0))
f.set_actor_label('WBM_Fences')
f.set_folder_path('PCG')
print('spawned WBM_Fences')
print('level saved:', unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True))
