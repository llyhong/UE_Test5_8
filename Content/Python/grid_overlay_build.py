# -*- coding: utf-8 -*-
"""Rebuild the 100cm build-grid texture (2x2 cells per tile) and refresh the overlay.

Lines are 8px of the 256px half-tile => ~3cm on the ground, fully opaque; the fill keeps a
faint checker so the surface stays readable without hiding the art underneath.
Idempotent: rewrites the PNG, reimports T_GridCell, re-saves M_BuildGrid, re-places WBM_BuildGrid.
"""
import struct
import zlib

import unreal

CELL = 100.0
SIZE = 512
HALF = SIZE // 2          # one cell = 256 px = 100 cm
LINE = 8                  # 3.1 cm line
SRC = r'C:\lly_UE_Proj\Ori5_8\Content\_LOCAL\_SrcTex\T_GridCell.png'


def write_png_rgba(path, w, h, px):
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw.extend(px[y * w * 4:(y + 1) * w * 4])

    def chunk(typ, data):
        return (struct.pack('>I', len(data)) + typ + data
                + struct.pack('>I', zlib.crc32(typ + data) & 0xffffffff))

    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n')
        f.write(chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)))
        f.write(chunk(b'IDAT', zlib.compress(bytes(raw), 9)))
        f.write(chunk(b'IEND', b''))


px = bytearray(SIZE * SIZE * 4)
for y in range(SIZE):
    for x in range(SIZE):
        lx, ly = x % HALF, y % HALF
        d = min(lx, ly)                      # distance to the cell edge (top/left)
        checker = ((x // HALF) + (y // HALF)) % 2
        if d < LINE:
            col = (255, 255, 255, 245)       # main line
        elif d < LINE + 2:
            col = (255, 255, 255, 110)       # AA fringe
        else:
            # faint checker so the plane reads as a surface, not a tint
            col = (255, 255, 255, 26) if checker == 0 else (30, 30, 30, 16)
        o = (y * SIZE + x) * 4
        px[o:o + 4] = bytes(col)
write_png_rgba(SRC, SIZE, SIZE, px)
print('texture written', SRC)

eal = unreal.EditorAssetLibrary
at = unreal.AssetToolsHelpers.get_asset_tools()

task = unreal.AssetImportTask()
task.set_editor_property('filename', SRC)
task.set_editor_property('destination_path', '/Game/_BLOCKOUT')
task.set_editor_property('destination_name', 'T_GridCell')
task.set_editor_property('automated', True)
task.set_editor_property('save', True)
task.set_editor_property('replace_existing', True)
at.import_asset_tasks([task])

tex = unreal.load_object(None, '/Game/_BLOCKOUT/T_GridCell.T_GridCell')
tex.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_EDITOR_ICON)
tex.set_editor_property('srgb', True)
tex.set_editor_property('never_stream', True)
eal.save_loaded_asset(tex)
print('texture reimported', tex.get_editor_property('compression_settings'))

# recompile the translucent unlit material so the new texture is used
mat = unreal.load_object(None, '/Game/_BLOCKOUT/M_BuildGrid.M_BuildGrid')
unreal.MaterialEditingLibrary.recompile_material(mat)
eal.save_loaded_asset(mat)
print('material recompiled')

# overlay plane: sit just above the kitchen slab (top z=6) and dining floor (top z=4)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for a in eas.get_all_level_actors():
    if a.get_actor_label() == 'WBM_BuildGrid':
        a.set_actor_location(unreal.Vector(0.0, 0.0, 6.8), False, True)
        comp = a.get_editor_property('static_mesh_component')
        comp.set_editor_property('hidden_in_scene_capture', True)
        a.set_actor_scale3d(unreal.Vector(34.0, 24.0, 1.0))
        print('overlay placed', a.get_actor_location(), a.get_actor_scale3d())

unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
print('saved')
