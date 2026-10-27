# -*- coding: utf-8 -*-
"""Dump grid-relevant actor state for post-run verification."""
import unreal

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
eal = unreal.EditorAssetLibrary
lines = []

for a in eas.get_all_level_actors():
    lb = a.get_actor_label()
    if lb.startswith(('WBM_', 'GRD_')):
        o, e = a.get_actor_bounds(False)
        try:
            tags = ','.join(str(t) for t in a.tags)
        except Exception:
            tags = '?'
        lines.append('%s\t%s\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t%s' % (
            lb, a.get_class().get_name(), o.x, o.y, e.x * 2, e.y * 2, o.z - e.z, o.z + e.z, tags))

lines.append('---')
for p in ('/Game/_BLOCKOUT/T_GridCell', '/Game/_BLOCKOUT/M_BuildGrid'):
    lines.append('%s exists=%s' % (p, eal.does_asset_exist(p)))
mat = unreal.load_object(None, '/Game/_BLOCKOUT/M_BuildGrid.M_BuildGrid')
if mat:
    lines.append('M_BuildGrid blend=%s shading=%s twosided=%s' % (
        mat.get_editor_property('blend_mode'),
        mat.get_editor_property('shading_model'),
        mat.get_editor_property('two_sided')))
tex = unreal.load_object(None, '/Game/_BLOCKOUT/T_GridCell.T_GridCell')
if tex:
    lines.append('T_GridCell %dx%d %s srgb=%s' % (
        tex.blueprint_get_size_x(), tex.blueprint_get_size_y(),
        tex.get_editor_property('compression_settings'), tex.get_editor_property('srgb')))

with open(r'C:\lly_UE_Proj\Ori5_8\Saved\GridProbe.txt', 'w', encoding='utf-8') as f:
    f.write('\n'.join(lines))
print('probe lines:', len(lines))
