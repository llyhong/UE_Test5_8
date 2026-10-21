import unreal
eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
bel = unreal.BlueprintEditorLibrary

# ---------- 1) three colored materials for food crates ----------
for name, rgb in (('M_Crate_Red', (0.78, 0.12, 0.10)),
                  ('M_Crate_Orange', (0.95, 0.42, 0.05)),
                  ('M_Crate_Green', (0.20, 0.62, 0.18))):
    path = '/Game/_BLOCKOUT/' + name
    mat = eal.load_asset(path)
    if not mat:
        factory = unreal.MaterialFactoryNew()
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, '/Game/_BLOCKOUT', unreal.Material, factory)
        print('created', name)
    const = mel.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -400, -80)
    const.set_editor_property('constant', unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
    mel.connect_material_property(const, '', unreal.MaterialProperty.MP_BASE_COLOR)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    print(name, 'ready')

# ---------- 2) BP_Tomato: stem + 3 fruits as SCS template components ----------
bp = unreal.load_object(None, '/Game/_BLOCKOUT/BP_Tomato.BP_Tomato')
sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
hs = sds.k2_gather_subobject_data_for_blueprint(bp)
cy = unreal.load_object(None, '/Engine/BasicShapes/Cylinder.Cylinder')
sp = unreal.load_object(None, '/Engine/BasicShapes/Sphere.Sphere')
white = unreal.load_object(None, '/Game/_BLOCKOUT/M_Whitebox.M_Whitebox')

def add_comp(name_suffix, mesh, scale, loc):
    p = unreal.AddNewSubobjectParams()
    p.set_editor_property('parent_handle', hs[0])
    p.set_editor_property('new_class', unreal.StaticMeshComponent)
    p.set_editor_property('blueprint_context', bp)
    handle = sds.add_new_subobject(p)
    return handle

h1 = add_comp('stem', cy, None, None)
h2 = add_comp('f1', sp, None, None)
h3 = add_comp('f2', sp, None, None)
h4 = add_comp('f3', sp, None, None)
bel.compile_blueprint(bp)
eal.save_loaded_asset(bp)
print('comps added')

# configure template objects by path
tpl = [
    ('StaticMesh_GEN_VARIABLE', cy, (0.05, 0.05, 0.55), (0, 0, 27)),
    ('StaticMesh1_GEN_VARIABLE', sp, (0.24, 0.24, 0.24), (0, 0, 45)),
    ('StaticMesh2_GEN_VARIABLE', sp, (0.18, 0.18, 0.18), (16, -8, 30)),
    ('StaticMesh3_GEN_VARIABLE', sp, (0.16, 0.16, 0.16), (-13, 11, 32)),
]
for base, mesh, scale, loc in tpl:
    obj = unreal.find_object(None, '/Game/_BLOCKOUT/BP_Tomato:BP_Tomato_C:%s' % base)
    if not obj:
        print('MISSING template', base)
        continue
    obj.set_editor_property('static_mesh', mesh)
    obj.set_editor_property('relative_location', unreal.Vector(loc[0], loc[1], loc[2]))
    obj.set_editor_property('relative_scale3d', unreal.Vector(scale[0], scale[1], scale[2]))
    obj.set_material(0, white)
    print('set', base)

bel.compile_blueprint(bp)
eal.save_loaded_asset(bp)
print('BP_Tomato done')
