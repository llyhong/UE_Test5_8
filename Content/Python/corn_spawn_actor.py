import unreal
eal = unreal.EditorAssetLibrary
bel = unreal.BlueprintEditorLibrary

# ---------- 1) BP_Corn: PlantClass variable (soft class ref, default BP_Tomato_C) ----------
bp = unreal.load_object(None, '/Game/_BLOCKOUT/BP_Corn.BP_Corn')
pt = unreal.EdGraphPinType()
pt.import_text('(PinCategory="softclass",PinSubCategoryObject=/Script/Engine.Actor")')
if 'PlantClass' not in list(bel.list_member_variable_names(bp)):
    ok = bel.add_member_variable(bp, 'PlantClass', pt)
    print('add var:', ok)
bel.set_blueprint_variable_instance_editable(bp, 'PlantClass', True)
bel.compile_blueprint(bp)
tomato_cls = unreal.BlueprintEditorLibrary.generated_class(unreal.load_object(None, '/Game/_BLOCKOUT/BP_Tomato.BP_Tomato'))
cdo = unreal.load_object(None, '/Game/_BLOCKOUT/BP_Corn.Default__BP_Corn_C')
cdo.set_editor_property('PlantClass', tomato_cls)
print('CDO default:', cdo.get_editor_property('PlantClass'))
eal.save_loaded_asset(bp)

# ---------- 2) rework PCG_CornGraph: sampler -> copy attrs -> SpawnActor ----------
g = unreal.load_object(None, '/Game/_BLOCKOUT/PCG_CornGraph.PCG_CornGraph')
n_actor = None; n_sampler = None; n_transform = None; n_spawner = None
for n in list(g.get_editor_property('nodes')):
    s = n.call_method('GetSettings', (), {})
    cn = s.get_class().get_name()
    if cn == 'PCGDataFromActorSettings': n_actor = n
    if cn == 'PCGSplineSamplerSettings': n_sampler = n
    if cn == 'PCGTransformPointsSettings': n_transform = n
    if cn == 'PCGStaticMeshSpawnerSettings': n_spawner = n
print('found:', bool(n_actor), bool(n_sampler), bool(n_transform), bool(n_spawner))

# remove transform and mesh spawner
if n_transform: g.remove_node(n_transform)
if n_spawner: g.remove_node(n_spawner)

# plant class property node
gp = unreal.PCGGetActorPropertySettings()
gp.set_editor_property('property_name', 'PlantClass')
osel = gp.get_editor_property('output_attribute_name')
osel.import_text('PlantClass')
gp.set_editor_property('output_attribute_name', osel)
n_prop = g.add_node_instance(gp)

# copy attributes node (same config as fence)
cs = unreal.PCGCopyAttributesSettings()
cs.set_editor_property('copy_all_attributes', True)
cs.set_editor_property('operation', unreal.PCGCopyAttributesOperation.COPY_EACH_SOURCE_ON_EVERY_TARGET)
ot = cs.get_editor_property('output_target')
ot.import_text('Target')
cs.set_editor_property('output_target', ot)
n_copy = g.add_node_instance(cs)

# spawn actor node driven by attribute
sa = unreal.PCGSpawnActorSettings()
sa.set_editor_property('spawn_by_attribute', True)
sa.set_editor_property('spawn_attribute', 'PlantClass')
n_spawn = g.add_node_instance(sa)

# wire: actor -> sampler (existing), sampler -> copy.Target, prop -> copy.Source, copy -> spawn
n_sampler.add_edge_to('Out', n_copy, 'Target')
n_prop.add_edge_to('Out', n_copy, 'Source')
n_copy.add_edge_to('Out', n_spawn, 'In')
eal.save_loaded_asset(g)
print('corn graph rewired, edges:', len(g.get_all_edges()))
for n in g.get_editor_property('nodes'):
    s = n.call_method('GetSettings', (), {})
    print(' node:', s.get_class().get_name())
