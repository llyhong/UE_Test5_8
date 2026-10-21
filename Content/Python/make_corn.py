import unreal

eal = unreal.EditorAssetLibrary
bel = unreal.BlueprintEditorLibrary
CONE = unreal.load_object(None, '/Engine/BasicShapes/Cone.Cone')

# ============ 1) PCG_CornGraph ============
gpath = '/Game/_BLOCKOUT/PCG_CornGraph'
if eal.does_asset_exist(gpath):
    cg = unreal.load_object(None, gpath + '.PCG_CornGraph')
else:
    eal.duplicate_asset('/Game/_BLOCKOUT/PCG_FenceGraph', gpath)
    cg = unreal.load_object(None, gpath + '.PCG_CornGraph')
    print('graph duplicated')

sampler_node = spawner_node = None
for n in cg.get_editor_property('nodes'):
    cn = n.call_method('GetSettings', (), {}).get_class().get_name()
    if cn == 'PCGSplineSamplerSettings':
        sampler_node = n
    elif cn == 'PCGStaticMeshSpawnerSettings':
        spawner_node = n
print('nodes found:', sampler_node is not None, spawner_node is not None)

sset = sampler_node.call_method('GetSettings', (), {})
pp = sset.get_editor_property('sampler_params')
pp.set_editor_property('distance_increment', 95.0)
sset.set_editor_property('sampler_params', pp)
print('increment:', sset.get_editor_property('sampler_params').get_editor_property('distance_increment'))

spset = spawner_node.call_method('GetSettings', (), {})
sel = spset.get_editor_property('mesh_selector_parameters')
entry = sel.get_editor_property('mesh_entries')[0]
desc = entry.get_editor_property('descriptor')
desc.set_editor_property('static_mesh', CONE)
entry.set_editor_property('descriptor', desc)
entries = sel.get_editor_property('mesh_entries')
entries[0] = entry
sel.set_editor_property('mesh_entries', entries)
chk = spawner_node.call_method('GetSettings', (), {}).get_editor_property('mesh_selector_parameters').get_editor_property('mesh_entries')[0]
print('spawner mesh:', chk.get_editor_property('descriptor').get_editor_property('static_mesh').get_path_name() if chk.get_editor_property('descriptor').get_editor_property('static_mesh') else None)

def node_of(pin):
    for attr in ('node', 'owner', 'Node', 'Owner'):
        try:
            return pin.get_editor_property(attr)
        except Exception:
            pass
    return None

for e in cg.get_all_edges():
    a = b = None
    for attr in ('from', 'from_', 'From', 'source'):
        try:
            a = e.get_editor_property(attr)
            break
        except Exception:
            pass
    for attr in ('to', 'To', 'destination'):
        try:
            b = e.get_editor_property(attr)
            break
        except Exception:
            pass
    na, nb = node_of(a), node_of(b)
    ca = na.call_method('GetSettings', (), {}).get_class().get_name() if na else '?'
    cb = nb.call_method('GetSettings', (), {}).get_class().get_name() if nb else '?'
    print('edge:', ca, '->', cb)
    if {ca, cb} == {'PCGSplineSamplerSettings', 'PCGStaticMeshSpawnerSettings'}:
        cg.remove_edge(e)
        print('old sampler->spawner edge removed')

tp = unreal.PCGTransformPointsSettings()
tp.set_editor_property('scale_min', unreal.Vector(0.26, 0.26, 1.0))
tp.set_editor_property('scale_max', unreal.Vector(0.26, 0.26, 1.0))
tp.set_editor_property('offset_min', unreal.Vector(0.0, 0.0, 50.0))
tp.set_editor_property('offset_max', unreal.Vector(0.0, 0.0, 50.0))
tp.set_editor_property('absolute_offset', True)
tp_node = cg.add_node_instance(tp)
sampler_node.add_edge_to('Out', tp_node, 'In')
tp_node.add_edge_to('Out', spawner_node, 'In')
print('edge count now:', len(cg.get_all_edges()))
eal.save_loaded_asset(cg)
print('corn graph saved')

# ============ 2) BP_Corn ============
dst_bp = '/Game/_BLOCKOUT/BP_Corn'
if eal.does_asset_exist(dst_bp):
    bp = unreal.load_object(None, dst_bp + '.BP_Corn')
else:
    eal.duplicate_asset('/Game/_BLOCKOUT/BP_Fence_Chicken', dst_bp)
    bp = unreal.load_object(None, dst_bp + '.BP_Corn')
    print('BP duplicated')

sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
hs = sds.k2_gather_subobject_data_for_blueprint(bp)
for i in range(4):
    p = unreal.AddNewSubobjectParams()
    p.set_editor_property('parent_handle', hs[0])
    p.set_editor_property('new_class', unreal.SplineComponent)
    p.set_editor_property('blueprint_context', bp)
    sds.add_new_subobject(p)
print('4 splines added')
bel.compile_blueprint(bp)

tmpl = []
for suffix in ('Spline_GEN_VARIABLE', 'Spline_GEN_VARIABLE_0', 'Spline_GEN_VARIABLE_1',
               'Spline_GEN_VARIABLE_2', 'Spline_GEN_VARIABLE_3'):
    o = unreal.find_object(None, dst_bp + ':BP_Corn_C:' + suffix)
    if o is not None:
        tmpl.append(o)
print('spline templates found:', len(tmpl), [t.get_name() for t in tmpl])

xs = (-440.0, -355.0, -270.0, -185.0, -100.0)
for x, sp in zip(xs, tmpl):
    sp.clear_spline_points(False)
    sp.add_spline_point(unreal.Vector(x, 450.0, 0.0), unreal.SplineCoordinateSpace.WORLD)
    sp.add_spline_point(unreal.Vector(x, 830.0, 0.0), unreal.SplineCoordinateSpace.WORLD)
    sp.set_spline_point_type(0, unreal.SplinePointType.LINEAR)
    sp.set_spline_point_type(1, unreal.SplinePointType.LINEAR)
    sp.set_closed_loop(False)
    sp.update_spline()
print('row splines shaped')

pcg_t = unreal.find_object(None, dst_bp + ':BP_Corn_C:PCG_GEN_VARIABLE')
pcg_t.call_method('SetGraph', (cg,), {})
print('pcg template graph set')

bel.compile_blueprint(bp)
print('bp saved:', eal.save_loaded_asset(bp))

# ============ 3) instance + cleanup ============
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
have = [a for a in eas.get_all_level_actors() if a.get_actor_label() == 'WBM_Corn_Field']
if not have:
    cls = bel.generated_class(bp)
    f = eas.spawn_actor_from_class(cls, unreal.Vector(0.0, 0.0, 0.0), unreal.Rotator(0.0, 0.0, 0.0))
    f.set_actor_label('WBM_Corn_Field')
    f.set_folder_path('PCG')
    print('spawned WBM_Corn_Field')

old = [a for a in eas.get_all_level_actors() if a.get_actor_label().startswith('WBM_Corn')]
print('deleting old corn actors:', len(old))
[eas.destroy_actor(a) for a in old]

print('level saved:', unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True))
