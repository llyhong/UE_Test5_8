# -*- coding: utf-8 -*-
import unreal

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
at = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary

for a in eas.get_all_level_actors():
    if a.get_actor_label() == 'WBM_BuildGrid':
        comp = a.get_editor_property('static_mesh_component')
        print('loc', a.get_actor_location(), 'scale', a.get_actor_scale3d())
        print('actor hidden(ed)', a.is_hidden_ed())
        print('comp visible', comp.is_visible(), 'hidden_in_game', comp.get_editor_property('hidden_in_game'))
        print('mesh', comp.get_editor_property('static_mesh'))
        print('mats', [comp.get_material(i) for i in range(comp.get_num_materials())])
        print('tags', [str(t) for t in a.tags])
        print('folder', a.get_folder_path())

m = unreal.load_object(None, '/Game/_BLOCKOUT/M_BuildGrid.M_BuildGrid')
print('material', m)
for e in mel.get_material_expressions(m):
    print(' expr', e.get_class().get_name(), e.get_name(),
          getattr(e, 'texture', None) if e.get_class().get_name() == 'MaterialExpressionTextureSample' else '')
print('blend', m.get_editor_property('blend_mode'), 'shading', m.get_editor_property('shading_model'))
