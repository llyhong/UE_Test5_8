# -*- coding: utf-8 -*-
"""Open-world farm asset setup (run once):
1) re-lay BP_Fence farm spline templates (cattle pen / pig hex; patio U untouched,
   chicken run removed — coops are scattered now)
2) create BP_CornStalk / BP_Cabbage / BP_Carrot plant blueprints
3) BP_Corn default PlantClass -> BP_CornStalk_C
"""
import unreal
eal = unreal.EditorAssetLibrary
bel = unreal.BlueprintEditorLibrary
sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)

# ---------- 1) BP_Fence spline templates (world coords, instance spawns at origin) ----------
# Spline/Spline1: cattle pen rect (SW edge), pig pen hexagon (S edge).
# Spline2 (old chicken yard) is emptied — four coops stand scattered, no run.
# Spline3 (patio U) is intentionally not listed — left untouched.
# Former field fences Spline4/5/6 were removed (fields are open ridged plots now); their
# empty spline templates may linger in BP_Fence — harmless, no points means no spawns.
# Pens snap to the 1 m build grid (100 cm cells, origin 0,0): cattle 6x4 cells,
# pig 5x4 cells. Fence posts land on cell boundaries like every placeable edge.
SHAPES = {
    'Spline_GEN_VARIABLE':   ([(-1700.0, -1100.0), (-1100.0, -1100.0), (-1100.0, -700.0), (-1700.0, -700.0)], True),
    'Spline1_GEN_VARIABLE':  ([(-600.0, -900.0), (-100.0, -900.0), (-100.0, -500.0), (-600.0, -500.0)], True),
    'Spline2_GEN_VARIABLE':  ([], False),
}
bp_fence = unreal.load_object(None, '/Game/_BLOCKOUT/BP_Fence.BP_Fence')
bel.compile_blueprint(bp_fence)
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
        continue
    pts, closed = SHAPES[nm]
    o.clear_spline_points(False)
    [o.add_spline_point(unreal.Vector(x, y, 0.0), unreal.SplineCoordinateSpace.WORLD) for (x, y) in pts]
    [o.set_spline_point_type(i, unreal.SplinePointType.LINEAR) for i in range(len(pts))]
    o.set_closed_loop(closed)
    o.update_spline()
    found += 1
print('farm splines re-laid:', found)
bel.compile_blueprint(bp_fence)
eal.save_asset('/Game/_BLOCKOUT/BP_Fence', False)

# ---------- 2) plant blueprints ----------
# Real Crops meshes; mesh min.z=0 + rel location zero keeps plants grounded on their origin.
# BP_Cabbage: no cabbage asset yet — tomato plants stand in; extra whitebox templates are
# blanked so reruns keep the swap.
PLANTS = {
    'BP_CornStalk': [
        ('/Game/_LOCAL/Crops/SM_CornPlant.SM_CornPlant', (1.0, 1.0, 1.0), (0, 0, 0), (0, 0, 0)),
    ],
    'BP_Cabbage': [
        ('/Game/_LOCAL/Crops/SM_TomatoPlant.SM_TomatoPlant', (1.0, 1.0, 1.0), (0, 0, 0), (0, 0, 0)),
        (None, None, None, None), (None, None, None, None), (None, None, None, None),
    ],
    'BP_Carrot': [
        ('/Game/_LOCAL/Crops/SM_CarrotPlant.SM_CarrotPlant', (1.8, 1.8, 1.8), (0, 0, 0), (0, 0, 0)),
    ],
}

for name, comps in PLANTS.items():
    bp = unreal.load_object(None, '/Game/_BLOCKOUT/%s.%s' % (name, name))
    if not bp:
        factory = unreal.BlueprintFactory()
        factory.set_editor_property('parent_class', unreal.Actor)
        bp = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, '/Game/_BLOCKOUT', unreal.Blueprint, factory)
        hs = sds.k2_gather_subobject_data_for_blueprint(bp)
        for i in range(len(comps)):
            p = unreal.AddNewSubobjectParams()
            p.set_editor_property('parent_handle', hs[0])
            p.set_editor_property('new_class', unreal.StaticMeshComponent)
            p.set_editor_property('blueprint_context', bp)
            sds.add_new_subobject(p)
        bel.compile_blueprint(bp)
        eal.save_asset('/Game/_BLOCKOUT/' + name, False)
        print(name, 'created with', len(comps), 'comps')
    for i, (mesh_path, scale, loc, rot) in enumerate(comps):
        obj = unreal.find_object(None, '/Game/_BLOCKOUT/%s:%s_C:StaticMesh%s_GEN_VARIABLE' % (name, name, '' if i == 0 else str(i)))
        if not obj:
            print(name, 'MISSING template StaticMesh%s' % ('' if i == 0 else str(i)))
            continue
        obj.set_editor_property('static_mesh', unreal.load_object(None, mesh_path) if mesh_path else None)
        if mesh_path:
            obj.set_editor_property('relative_location', unreal.Vector(loc[0], loc[1], loc[2]))
            obj.set_editor_property('relative_scale3d', unreal.Vector(scale[0], scale[1], scale[2]))
            obj.set_editor_property('relative_rotation', unreal.Rotator(rot[0], rot[1], rot[2]))
            try:
                obj.set_editor_property('override_materials', [])
            except Exception:
                pass
    bel.compile_blueprint(bp)
    eal.save_asset('/Game/_BLOCKOUT/' + name, False)
    print(name, 'configured')

# ---------- 3) BP_Corn default PlantClass ----------
bp_corn = unreal.load_object(None, '/Game/_BLOCKOUT/BP_Corn.BP_Corn')
stalk_cls = bel.generated_class(unreal.load_object(None, '/Game/_BLOCKOUT/BP_CornStalk.BP_CornStalk'))
cdo = unreal.load_object(None, '/Game/_BLOCKOUT/BP_Corn.Default__BP_Corn_C')
cdo.set_editor_property('PlantClass', stalk_cls)
bel.compile_blueprint(bp_corn)
eal.save_asset('/Game/_BLOCKOUT/BP_Corn', False)
print('BP_Corn default plant:', cdo.get_editor_property('PlantClass'))

print('level saved:', unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True))
