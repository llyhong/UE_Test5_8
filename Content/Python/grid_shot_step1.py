# -*- coding: utf-8 -*-
"""Throwaway captures: show the grid overlay at eye level + top view."""
import unreal

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
eal = unreal.EditorAssetLibrary
at = unreal.AssetToolsHelpers.get_asset_tools()

for a in eas.get_all_level_actors():
    if a.get_actor_label().startswith('TMP_GridShot'):
        eas.destroy_actor(a)
    elif a.get_actor_label() == 'WBM_BuildGrid':
        a.get_editor_property('static_mesh_component').set_editor_property(
            'hidden_in_scene_capture', False)

SHOTS = {
    'RT_GridTop': ((0.0, 0.0, 4200.0), (0.0, -90.0, 90.0), 55.0),
    'RT_GridEye': ((450.0, -930.0, 165.0), (0.0, -6.0, 55.0), 80.0),
    'RT_GridFarm': ((150.0, -420.0, 170.0), (0.0, -8.0, 175.0), 80.0),
}

for name, (loc, rot, fov) in SHOTS.items():
    path = '/Game/_ReviewTemp/' + name
    if eal.does_asset_exist(path):
        rt = unreal.load_object(None, path + '.' + name)
    else:
        rt = at.create_asset(name, '/Game/_ReviewTemp',
                             unreal.TextureRenderTarget2D,
                             unreal.TextureRenderTargetFactoryNew())
    rt.set_editor_property('size_x', 1600)
    rt.set_editor_property('size_y', 900)
    rt.set_editor_property('render_target_format', unreal.TextureRenderTargetFormat.RTF_RGBA8)
    cap = eas.spawn_actor_from_class(unreal.SceneCapture2D,
                                     unreal.Vector(*loc), unreal.Rotator(*rot))
    cap.set_actor_label('TMP_GridShot_' + name)
    comp = cap.get_editor_property('capture_component2d')
    comp.set_editor_property('texture_target', rt)
    comp.set_editor_property('capture_every_frame', True)
    comp.set_editor_property('capture_source', unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
    comp.set_editor_property('fov_angle', fov)
    comp.capture_scene()
    print('capture', name)
