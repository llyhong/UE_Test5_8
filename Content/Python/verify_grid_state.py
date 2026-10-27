# -*- coding: utf-8 -*-
import unreal

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
acts = eas.get_all_level_actors()
placeable = [a for a in acts if 'GridPlaceable' in [str(t) for t in a.tags]]
stacked = [a for a in acts if 'GridStacked' in [str(t) for t in a.tags]]
crops = [a for a in acts if a.get_actor_label().startswith('GRD_Crop_')]
soil = [a for a in acts if a.get_actor_label().startswith('GRD_Soil_')]
print('actors total', len(acts))
print('grid placeable', len(placeable), 'stacked', len(stacked))
print('crops', len(crops), 'soil', len(soil))
tmp = [a.get_actor_label() for a in acts if a.get_actor_label().startswith('TMP_')]
print('temp actors', tmp or 'none')
for a in acts:
    if a.get_actor_label() == 'WBM_BuildGrid':
        comp = a.get_editor_property('static_mesh_component')
        print('grid overlay hidden_in_capture',
              comp.get_editor_property('hidden_in_scene_capture'),
              'scale', a.get_actor_scale3d(), 'z', a.get_actor_location().z,
              'tags', [str(t) for t in a.tags])

# occupancy re-check straight from the scene tags
occ = {}
bad = []
for a in placeable:
    if 'GridStacked' in [str(t) for t in a.tags]:
        continue
    cell = size = None
    for t in [str(t) for t in a.tags]:
        if t.startswith('GridCell='):
            cell = tuple(int(v) for v in t.split('=', 1)[1].split(','))
        elif t.startswith('GridSize='):
            size = tuple(int(v) for v in t.split('=', 1)[1].split('x'))
    if cell is None or size is None:
        bad.append(a.get_actor_label())
        continue
    for i in range(cell[0], cell[0] + size[0]):
        for j in range(cell[1], cell[1] + size[1]):
            if (i, j) in occ:
                print('CONFLICT', a.get_actor_label(), occ[(i, j)], i, j)
            occ[(i, j)] = a.get_actor_label()
print('untagged placeables', bad or 'none')
print('occupied cells', len(occ))
