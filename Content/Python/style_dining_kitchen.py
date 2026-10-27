# -*- coding: utf-8 -*-
"""Style the dining + kitchen zones to the cartoon concept:
wood plank floor, kitchen tile floor, cream walls with wood wainscot,
pastel furniture tints. Idempotent - safe to re-run after builder rebuild."""
import unreal

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
eal = unreal.EditorAssetLibrary

MAT = {}
for n in ('M_Wall_Cream', 'M_Board_Wood', 'M_Furni_Table', 'M_Furni_Chair', 'M_Furni_Counter',
          'M_Furni_Fridge', 'M_Furni_Stove', 'M_Furni_Pot', 'M_Furni_Sink', 'M_Furni_Register',
          'M_FloorWood', 'M_FloorTile'):
    MAT[n] = unreal.load_object(None, '/Game/_BLOCKOUT/%s.%s' % (n, n))

# label base name -> material
RULES = [
    ('WBM_FloorInterior',    'M_FloorWood'),
    ('WBM_FloorKitchen',     'M_FloorTile'),
    ('WBM_Wall',             'M_Wall_Cream'),
    ('WBM_DoorHeader',       'M_Wall_Cream'),
    ('WBM_TableTop',         'M_Furni_Table'),
    ('WBM_ChairSeat',        'M_Furni_Chair'),
    ('WBM_CashierCounter',   'M_Furni_Counter'),
    ('WBM_CashierRegister',  'M_Furni_Register'),
    ('WBM_Fridge',           'M_Furni_Fridge'),
    ('WBM_Sink',             'M_Furni_Sink'),
    ('WBM_KitchenTable',     'M_Furni_Table'),
    ('WBM_Stove',            'M_Furni_Stove'),
    ('WBM_StovePot',         'M_Furni_Pot'),
    ('WBM_KitchenIsland',    'M_Furni_Counter'),
    ('WBM_KitchenShelf',     'M_Board_Wood'),
    ('WBM_PassCounter',      'M_Furni_Counter'),
]

ZONE = (150.0, 1750.0, -1050.0, 1150.0)
applied = {}
for a in eas.get_all_level_actors():
    lb = a.get_actor_label()
    if not lb.startswith('WBM_'):
        continue
    try:
        loc = a.get_actor_location()
    except Exception:
        continue
    if not (ZONE[0] <= loc.x <= ZONE[1] and ZONE[2] <= loc.y <= ZONE[3]):
        continue
    base = lb[4:].rsplit('_', 1)[0]
    for pref, mn in RULES:
        if base == pref[4:]:
            comp = a.get_editor_property('static_mesh_component')
            comp.set_material(0, MAT[mn])
            applied[mn] = applied.get(mn, 0) + 1
            break
for mn in sorted(applied):
    print('%-18s x%d' % (mn, applied[mn]))

# ---------- wainscot strips along inner wall faces ----------
CUBE = unreal.load_object(None, '/Engine/BasicShapes/Cube.Cube')

def find_by_label(lb):
    for a in eas.get_all_level_actors():
        if a.get_actor_label() == lb:
            return a
    return None

def wall_thickness(lbl):
    a = find_by_label(lbl)
    if not a:
        return 30.0
    _o, ext = a.get_actor_bounds(False)
    return ext.y * 2.0

def strip(label, x, y, sx, sy, folder):
    a = find_by_label(label)
    if a is None:
        a = eas.spawn_actor_from_class(unreal.StaticMeshActor,
                                       unreal.Vector(x, y, 49.0), unreal.Rotator(0.0, 0.0, 0.0))
        a.set_actor_label(label)
    a.set_actor_location(unreal.Vector(x, y, 49.0), False, False)
    a.set_actor_scale3d(unreal.Vector(sx, sy, 0.9))
    comp = a.get_editor_property('static_mesh_component')
    comp.set_static_mesh(CUBE)
    comp.set_material(0, MAT['M_Board_Wood'])
    a.set_folder_path(folder)
    print(label, 'at (%.0f, %.0f)' % (x, y))

tw_s = wall_thickness('WBM_Wall_1602')
strip('WBM_Wainscot_South', 500.0, -1000.0 + tw_s / 2.0 + 4.0, 6.0, 0.08, 'Building/Walls')
tw_p = wall_thickness('WBM_Wall_1611')
strip('WBM_Wainscot_Partition', 1250.0, 100.0 - tw_p / 2.0 - 4.0, 7.0, 0.08, 'Building/Walls')

eal.save_asset('/Game/_BLOCKOUT/L_FarmKitchen_Blockout', False)
print('map saved')
