# -*- coding: utf-8 -*-
"""Open-world farm asset setup (run once):
1) re-lay BP_Fence farm spline templates (cow district / pig pen / chicken yard; patio U untouched)
2) create BP_CornStalk / BP_Cabbage / BP_Carrot plant blueprints
3) BP_Corn default PlantClass -> BP_CornStalk_C
"""
import unreal
eal = unreal.EditorAssetLibrary
bel = unreal.BlueprintEditorLibrary
sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)

# ---------- 1) BP_Fence spline templates (world coords, instance spawns at origin) ----------
# Spline/Spline1/Spline2: cow pen, pig pen, chicken yard (south row).
# Spline4/5/6: field fences matching the three BP_Corn field rects (added if missing).
# Spline3 (patio U) is intentionally not listed — left untouched.
SHAPES = {
    'Spline_GEN_VARIABLE':   ([(-1600.0, -550.0), (-1000.0, -550.0), (-1000.0, -100.0), (-1600.0, -100.0)], True),
    'Spline1_GEN_VARIABLE':  ([(-880.0, -620.0), (-480.0, -620.0), (-480.0, -300.0), (-880.0, -300.0)], True),
    'Spline2_GEN_VARIABLE':  ([(-360.0, -620.0), (40.0, -620.0), (40.0, -320.0), (-360.0, -320.0)], True),
    'Spline4_GEN_VARIABLE':  ([(-770.0, 510.0), (-430.0, 510.0), (-430.0, 890.0), (-770.0, 890.0)], True),
    'Spline5_GEN_VARIABLE':  ([(-230.0, 450.0), (150.0, 450.0), (150.0, 790.0), (-230.0, 790.0)], True),
    'Spline6_GEN_VARIABLE':  ([(-1250.0, 430.0), (-910.0, 430.0), (-910.0, 810.0), (-1250.0, 810.0)], True),
}
bp_fence = unreal.load_object(None, '/Game/_BLOCKOUT/BP_Fence.BP_Fence')
existing = set()
for o in unreal.ObjectIterator():
    try:
        pn = o.get_path_name()
    except Exception:
        continue
    if pn.startswith('/Game/_BLOCKOUT/BP_Fence.') and o.get_class().get_name() == 'SplineComponent':
        existing.add(o.get_name())
hs = sds.k2_gather_subobject_data_for_blueprint(bp_fence)
for want in ('Spline4_GEN_VARIABLE', 'Spline5_GEN_VARIABLE', 'Spline6_GEN_VARIABLE'):
    if want not in existing:
        p = unreal.AddNewSubobjectParams()
        p.set_editor_property('parent_handle', hs[0])
        p.set_editor_property('new_class', unreal.SplineComponent)
        p.set_editor_property('blueprint_context', bp_fence)
        sds.add_new_subobject(p)
        print('added spline template for', want)
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
eal.save_loaded_asset(bp_fence)

# ---------- 2) plant blueprints ----------
cy = unreal.load_object(None, '/Engine/BasicShapes/Cylinder.Cylinder')
sp = unreal.load_object(None, '/Engine/BasicShapes/Sphere.Sphere')
co = unreal.load_object(None, '/Engine/BasicShapes/Cone.Cone')
cu = unreal.load_object(None, '/Engine/BasicShapes/Cube.Cube')
white = unreal.load_object(None, '/Game/_BLOCKOUT/M_Whitebox.M_Whitebox')

PLANTS = {
    'BP_CornStalk': [
        # (mesh, scale, loc, rot(pitch,yaw,roll))
        (cy, (0.12, 0.12, 2.00), (0, 0, 100), (0, 0, 0)),    # stem
        (co, (0.40, 0.40, 0.70), (0, 0, 225), (0, 0, 0)),    # tassel
        (cu, (0.70, 0.06, 0.04), (28, 0, 115), (-20, 0, 0)),  # leaf +X
        (cu, (0.70, 0.06, 0.04), (0, 28, 95), (-20, 90, 0)),  # leaf +Y
        (cu, (0.55, 0.06, 0.04), (-24, 0, 130), (-20, 180, 0)),  # leaf -X
    ],
    'BP_Cabbage': [
        (sp, (0.90, 0.90, 0.55), (0, 0, 27), (0, 0, 0)),     # outer head
        (sp, (0.55, 0.55, 0.40), (0, 0, 42), (0, 0, 0)),     # inner head
        (sp, (0.35, 0.15, 0.08), (38, 0, 8), (0, 0, 0)),     # ground leaf +X
        (sp, (0.35, 0.15, 0.08), (0, 38, 8), (0, 90, 0)),    # ground leaf +Y
    ],
    'BP_Carrot': [
        (co, (0.28, 0.28, 0.35), (0, 0, 8), (0, 0, 0)),      # shoulder (tip up)
        (sp, (0.16, 0.16, 0.30), (0, 0, 38), (0, 0, 0)),     # foliage
        (sp, (0.12, 0.12, 0.24), (8, 3, 32), (10, 0, 15)),
        (sp, (0.12, 0.12, 0.24), (-7, -2, 33), (-10, 0, -15)),
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
        eal.save_loaded_asset(bp)
        print(name, 'created with', len(comps), 'comps')
    for i, (mesh, scale, loc, rot) in enumerate(comps):
        obj = unreal.find_object(None, '/Game/_BLOCKOUT/%s:%s_C:StaticMesh%s_GEN_VARIABLE' % (name, name, '' if i == 0 else str(i)))
        if not obj:
            print(name, 'MISSING template StaticMesh%s' % ('' if i == 0 else str(i)))
            continue
        obj.set_editor_property('static_mesh', mesh)
        obj.set_editor_property('relative_location', unreal.Vector(loc[0], loc[1], loc[2]))
        obj.set_editor_property('relative_scale3d', unreal.Vector(scale[0], scale[1], scale[2]))
        obj.set_editor_property('relative_rotation', unreal.Rotator(rot[0], rot[1], rot[2]))
        obj.set_material(0, white)
    bel.compile_blueprint(bp)
    eal.save_loaded_asset(bp)
    print(name, 'configured')

# ---------- 3) BP_Corn default PlantClass ----------
bp_corn = unreal.load_object(None, '/Game/_BLOCKOUT/BP_Corn.BP_Corn')
stalk_cls = bel.generated_class(unreal.load_object(None, '/Game/_BLOCKOUT/BP_CornStalk.BP_CornStalk'))
cdo = unreal.load_object(None, '/Game/_BLOCKOUT/BP_Corn.Default__BP_Corn_C')
cdo.set_editor_property('PlantClass', stalk_cls)
bel.compile_blueprint(bp_corn)
eal.save_loaded_asset(bp_corn)
print('BP_Corn default plant:', cdo.get_editor_property('PlantClass'))

print('level saved:', unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True))
