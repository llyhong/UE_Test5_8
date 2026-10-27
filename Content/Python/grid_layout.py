# -*- coding: utf-8 -*-
"""Enforce the 1 m build grid on L_FarmKitchen_Blockout.

Grid contract
-------------
CELL = 100 cm, origin at world (0, 0). Cell (i, j) covers
  x in [100*i, 100*i + 100), y in [100*j, 100*j + 100)
A placeable with footprint w*h occupies cells i..i+w-1, j..j+h-1 and its
bounding-box centre is placed at the footprint centre (100*i + 50*w,
100*j + 50*h). Every placeable is tagged:

  GridPlaceable       - occupies cells, included in the occupancy check
  GridCell=i,j        - anchor cell (min corner)
  GridSize=wxh        - footprint in cells
  GridStacked         - shares its cells with the object underneath
                        (pot on stove, register on counter, water in basin,
                        crop on a field cell) - not an occupancy conflict

Walls/floors are edge/ground features and stay untagged; field ground tiles
are tagged GridGround. Crops are GridPlaceable crops on their own cell.

Idempotent. Runs last in the pipeline:
  build_farm_kitchen_blockout -> replace_whitebox_props -> replace_farm_props
  -> farm_world_setup -> style_dining_kitchen -> grid_layout

Run by hand for reference snapping:
  cell_center(i, j, w, h), snap_xy(x, y, w, h), footprint_for(extent)
"""
import math
import struct
import zlib

import unreal

CELL = 100.0
EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()


# ---------------------------------------------------------------- helpers
def cell_center(i, j, w=1, h=1):
    return (CELL * i + CELL * w * 0.5, CELL * j + CELL * h * 0.5)


def snap_xy(x, y, w=1, h=1):
    i = int(round((x - CELL * w * 0.5) / CELL))
    j = int(round((y - CELL * h * 0.5) / CELL))
    return i, j, cell_center(i, j, w, h)


def footprint_for(ext):
    return (max(1, int(math.ceil(ext.x * 2.0 / CELL - 1e-4))),
            max(1, int(math.ceil(ext.y * 2.0 / CELL - 1e-4))))


def find_label(label):
    for a in EAS.get_all_level_actors():
        if a.get_actor_label() == label:
            return a
    return None


def base_of(actor):
    lb = actor.get_actor_label()
    return lb[4:].rsplit('_', 1)[0] if lb.startswith('WBM_') else None


def actors_of(base):
    return [a for a in EAS.get_all_level_actors() if base_of(a) == base]


def set_tags(actor, tags):
    actor.set_editor_property('tags', [unreal.Name(t) for t in tags])


def move_actor(actor, x, y):
    loc = actor.get_actor_location()
    actor.set_actor_location(unreal.Vector(x, y, loc.z), False, True)


def union_bounds(acts):
    minx = miny = 1e9
    maxx = maxy = -1e9
    for a in acts:
        o, e = a.get_actor_bounds(False)
        minx, maxx = min(minx, o.x - e.x), max(maxx, o.x + e.x)
        miny, maxy = min(miny, o.y - e.y), max(maxy, o.y + e.y)
    return minx, miny, maxx, maxy


def place_label(label, x, y):
    a = find_label(label)
    if a is None:
        print('MISSING', label)
        return None
    move_actor(a, x, y)
    return a


def shift_base(base, target):
    """Shift every actor of `base` so its union centre lands on target (idempotent)."""
    acts = actors_of(base)
    if not acts:
        print('MISSING', base)
        return []
    minx, miny, maxx, maxy = union_bounds(acts)
    dx, dy = target[0] - (minx + maxx) * 0.5, target[1] - (miny + maxy) * 0.5
    for a in acts:
        loc = a.get_actor_location()
        move_actor(a, loc.x + dx, loc.y + dy)
    return acts


def clusters_of(base, eps=45.0):
    out = []
    for a in actors_of(base):
        loc = a.get_actor_location()
        for cl in out:
            q = cl[0].get_actor_location()
            if math.hypot(loc.x - q.x, loc.y - q.y) <= eps:
                cl.append(a)
                break
        else:
            out.append([a])
    return out


def shift_clusters(base, targets):
    """Move each cluster of `base` to its nearest target (targets are consumed)."""
    left = [tuple(t) for t in targets]
    for cl in clusters_of(base):
        c = union_bounds(cl)
        cx, cy = (c[0] + c[2]) * 0.5, (c[1] + c[3]) * 0.5
        if not left:
            print('EXTRA', base, cl[0].get_actor_label())
            continue
        t = min(left, key=lambda v: math.hypot(cx - v[0], cy - v[1]))
        left.remove(t)
        for a in cl:
            loc = a.get_actor_location()
            move_actor(a, loc.x + t[0] - cx, loc.y + t[1] - cy)


# ---------------------------------------------------------------- shell
def wall(label, x1, x2, y, z1, z2, t=20.0):
    a = find_label(label)
    if a is None:
        print('MISSING wall', label)
        return
    a.set_actor_location(unreal.Vector((x1 + x2) * 0.5, y, (z1 + z2) * 0.5), False, True)
    a.set_actor_scale3d(unreal.Vector((x2 - x1) / 100.0, t / 100.0, (z2 - z1) / 100.0))


# south wall: 2+2+2 windows / 2-cell door / 6-cell east run; divider pass window 2 cells
wall('WBM_Wall_1602', 200, 300, -1000, 0, 300)
wall('WBM_Wall_1603', 300, 500, -1000, 0, 90)
wall('WBM_Wall_1604', 300, 500, -1000, 230, 300)
wall('WBM_Wall_1605', 500, 600, -1000, 0, 300)
wall('WBM_Wall_1606', 600, 800, -1000, 0, 90)
wall('WBM_Wall_1607', 600, 800, -1000, 230, 300)
pier = find_label('WBM_Wall_1608')
if pier:
    EAS.destroy_actor(pier)
wall('WBM_Wall_1609', 1000, 1600, -1000, 0, 300)
wall('WBM_DoorHeader_1610', 800, 1000, -1000, 240, 300)
wall('WBM_Wall_1611', 900, 1200, 100, 0, 300)
wall('WBM_Wall_1612', 1400, 1600, 100, 0, 300)
wall('WBM_Wall_1613', 1200, 1400, 100, 0, 90)
wall('WBM_Wall_1614', 1200, 1400, 100, 210, 300)

path = find_label('WBM_PathEntrance_1600')
if path:
    path.set_actor_location(unreal.Vector(900.0, -1100.0, 2.0), False, True)
    path.set_actor_scale3d(unreal.Vector(2.0, 2.0, 0.04))

start = find_label('WBM_PlayerStart')
if start:
    start.set_actor_location(unreal.Vector(900.0, -1100.0, 100.0), False, True)

text = find_label('WBM_Text_1637')
if text:
    text.set_actor_location(unreal.Vector(900.0, -1150.0, 5.0), False, True)


# ---------------------------------------------------------------- dining
# absolute targets by label: tables on odd cells, one chair per cell on the
# table axis 1 cell away (north/south)
place_label('WBM_TableTop_1638', 350.0, -650.0)
place_label('WBM_TableTop_1648', 550.0, -650.0)
place_label('WBM_TableTop_1658', 750.0, -650.0)
place_label('WBM_TableTop_1668', 350.0, -250.0)
place_label('WBM_TableTop_1678', 550.0, -250.0)
place_label('WBM_TableTop_1688', 750.0, -250.0)
for lb, x, y in (('WBM_ChairSeat_1640', 350.0, -750.0), ('WBM_ChairSeat_1642', 350.0, -550.0),
                 ('WBM_ChairSeat_1650', 550.0, -750.0), ('WBM_ChairSeat_1652', 550.0, -550.0),
                 ('WBM_ChairSeat_1660', 750.0, -750.0), ('WBM_ChairSeat_1662', 750.0, -550.0),
                 ('WBM_ChairSeat_1670', 350.0, -350.0), ('WBM_ChairSeat_1672', 350.0, -150.0),
                 ('WBM_ChairSeat_1680', 550.0, -350.0), ('WBM_ChairSeat_1682', 550.0, -150.0),
                 ('WBM_ChairSeat_1690', 750.0, -350.0), ('WBM_ChairSeat_1692', 750.0, -150.0)):
    place_label(lb, x, y)


# ---------------------------------------------------------------- kitchen
# back row along j=9: 900..1000 fridges, 1100 sink, 1200 prep, 1300/1400 stoves.
# Positions are cell centres, so re-runs are stable (no cascading base lookup).
for lb, x, y in (('WBM_Fridge_1624', 950.0, 950.0), ('WBM_Fridge_1625', 1050.0, 950.0),
                 ('WBM_Sink_1619', 1150.0, 950.0), ('WBM_KitchenTable_1620', 1250.0, 950.0),
                 ('WBM_Stove_1621', 1350.0, 950.0), ('WBM_Stove_1622', 1450.0, 950.0),
                 ('WBM_StovePot_1623', 1350.0, 950.0),
                 ('WBM_KitchenIsland_1626', 1200.0, 450.0), ('WBM_KitchenShelf_1627', 950.0, 500.0),
                 ('WBM_PassCounter_1628', 1300.0, 150.0), ('WBM_CashierCounter_1629', 1300.0, 50.0),
                 ('WBM_CashierRegister_1630', 1350.0, 50.0),
                 ('WBM_QueueBarrier', 1050.0, -1100.0), ('WBM_HangingSign_1631', 1150.0, -1050.0),
                 ('WBM_GreetCounter_1995', 750.0, -1050.0)):
    place_label(lb, x, y)

# colored produce crates: frame composite + loose fruit spheres move together
for base, target in (('Crate_Green', (1550.0, 550.0)), ('Crate_GreenFill', (1550.0, 550.0)),
                     ('Crate_Orange', (1550.0, 650.0)), ('Crate_OrangeFill', (1550.0, 650.0)),
                     ('Crate_Red', (1550.0, 750.0)), ('Crate_RedFill', (1550.0, 750.0))):
    shift_base(base, target)


# ---------------------------------------------------------------- farm props
for lb, x, y in (('WBM_Coop_1712', -600.0, 300.0), ('WBM_Coop_1714', -800.0, 400.0),
                 ('WBM_Coop_A', -300.0, -200.0), ('WBM_Coop_B', -900.0, -800.0),
                 ('WBM_Trough_1702', -1400.0, -750.0), ('WBM_Trough_1709', -400.0, -850.0),
                 ('WBM_Basin_1724', -1450.0, -450.0), ('WBM_BasinWater_1725', -1450.0, -450.0),
                 ('WBM_Well_1726', -200.0, 100.0), ('WBM_Reservoir_1731', -500.0, 100.0),
                 ('WBM_Pig_1705', -500.0, -650.0), ('WBM_Pig_1707', -400.0, -550.0),
                 ('WBM_CowRigged_1', -1600.0, -1000.0), ('WBM_CowRigged_2', -1400.0, -1000.0),
                 ('WBM_Chicken_1716', -650.0, 150.0), ('WBM_Chicken_1718', -550.0, 150.0),
                 ('WBM_ChickenHead_1717', -639.0, 150.0), ('WBM_ChickenHead_1719', -539.0, 150.0),
                 ('WBM_Chicken_1720', -850.0, 250.0), ('WBM_Chicken_1722', -750.0, 250.0),
                 ('WBM_ChickenHead_1721', -839.0, 250.0), ('WBM_ChickenHead_1723', -739.0, 250.0),
                 ('WBM_Chicken_A', -350.0, -350.0), ('WBM_Chicken_B', -250.0, -350.0),
                 ('WBM_ChickenHead_A', -339.0, -350.0), ('WBM_ChickenHead_B', -239.0, -350.0),
                 ('WBM_Chicken_C', -950.0, -650.0), ('WBM_Chicken_D', -850.0, -650.0),
                 ('WBM_ChickenHead_C', -939.0, -650.0), ('WBM_ChickenHead_D', -839.0, -650.0)):
    place_label(lb, x, y)

shift_clusters('RingFeeder', ((-1200.0, -900.0), (-200.0, -700.0)))


# ---------------------------------------------------------------- fields on grid
PLOT_CELLS = 5
PLOTS = (  # crop type, min corner in cm (grid boundary), plant BP
    ('Corn', -650.0, 400.0, '/Game/_BLOCKOUT/BP_CornStalk.BP_CornStalk'),
    ('Cabbage', -1400.0, 600.0, '/Game/_BLOCKOUT/BP_Cabbage.BP_Cabbage'),
    ('Carrot', -1650.0, -150.0, '/Game/_BLOCKOUT/BP_Carrot.BP_Carrot'),
)
TILE_TOP = 17.5
TILE = unreal.load_object(None, '/Game/_LOCAL/Crops/SM_FieldTile.SM_FieldTile')

old_field = []
for a in EAS.get_all_level_actors():
    lb = a.get_actor_label()
    cn = a.get_class().get_name()
    if (lb.startswith('WBM_FieldGround') or lb.startswith('WBM_Field_')
            or lb.startswith('GRD_') or cn in ('BP_CornStalk_C', 'BP_Cabbage_C', 'BP_Carrot_C')):
        old_field.append(a)
for a in old_field:
    EAS.destroy_actor(a)
print('dropped old field actors:', len(old_field))

bel = unreal.BlueprintEditorLibrary
for crop, x0, y0, plant in PLOTS:
    plant_cls = bel.generated_class(unreal.load_object(None, plant))
    for cx in range(PLOT_CELLS):
        for cy in range(PLOT_CELLS):
            i = int(round(x0 / CELL)) + cx
            j = int(round(y0 / CELL)) + cy
            wx, wy = cell_center(i, j)
            t = EAS.spawn_actor_from_object(TILE, unreal.Vector(wx, wy, 0.0),
                                            unreal.Rotator(0.0, 0.0, 0.0))
            t.set_actor_label('GRD_Soil_%s_%d_%d' % (crop, i, j))
            t.set_actor_scale3d(unreal.Vector(CELL / 120.0, CELL / 90.0, 1.0))
            t.set_folder_path('Fields')
            set_tags(t, ['GridGround', 'GridCell=%d,%d' % (i, j), 'GridSize=1x1'])
            p = EAS.spawn_actor_from_class(plant_cls, unreal.Vector(wx, wy, TILE_TOP - 1.0),
                                           unreal.Rotator(0.0, 0.0, 0.0))
            p.set_actor_label('GRD_Crop_%s_%d_%d' % (crop, i, j))
            p.set_folder_path('Fields')
            set_tags(p, ['GridPlaceable', 'GridCrop=' + crop, 'GridStacked',
                         'GridCell=%d,%d' % (i, j), 'GridSize=1x1'])


# ---------------------------------------------------------------- tag placeables
TAG_RULES = {
    # base: (footprint w, h, stacked)
    'TableTop': (1, 1, False), 'ChairSeat': (1, 1, False),
    'Sink': (1, 1, False), 'KitchenTable': (1, 1, False),
    'Stove': (1, 1, False), 'StovePot': (1, 1, True),
    'Fridge': (1, 1, False),
    'KitchenIsland': (2, 1, False), 'KitchenShelf': (1, 2, False),
    'PassCounter': (2, 1, False), 'CashierCounter': (2, 1, False),
    'CashierRegister': (1, 1, True),
    'Crate_Green': (1, 1, False), 'Crate_Orange': (1, 1, False), 'Crate_Red': (1, 1, False),
    'GreetCounter': (1, 1, False), 'QueueBarrier': (1, 2, False),
    'HangingSign': (1, 1, False),
    'Trough': (2, 1, False), 'RingFeeder': (2, 2, False),
    'Coop': (2, 2, False), 'Basin': (3, 3, False), 'BasinWater': (3, 3, True),
    'Well': (2, 2, False), 'Reservoir': (2, 2, False),
    'Pig': (2, 1, False), 'CowRigged': (2, 2, False),
}
entries = []  # (label, i, j, w, h, stacked)
cell_of = {}  # label -> (i, j) for follow-up parts
for base, (w, h, stacked) in TAG_RULES.items():
    # group by anchor cell: parts of one composite snap to the same cell, while
    # two neighbours one cell apart keep separate anchors (never merged)
    groups = {}
    for a in actors_of(base):
        loc = a.get_actor_location()
        groups.setdefault(snap_xy(loc.x, loc.y, w, h)[:2], []).append(a)
    for (i, j), cl in sorted(groups.items()):
        minx, miny, maxx, maxy = union_bounds(cl)
        cx, cy = (minx + maxx) * 0.5, (miny + maxy) * 0.5
        tx, ty = cell_center(i, j, w, h)
        if abs(tx - cx) > 0.5 or abs(ty - cy) > 0.5:
            for a in cl:
                loc = a.get_actor_location()
                move_actor(a, loc.x + tx - cx, loc.y + ty - cy)
        spanx, spany = maxx - minx, maxy - miny
        if spanx > CELL * w + 1.0 or spany > CELL * h + 1.0:
            print('FOOTPRINT TOO SMALL', base, cl[0].get_actor_label(),
                  '%.1fx%.1f' % (spanx, spany), 'in %dx%d' % (w, h))
        tags = ['GridPlaceable', 'GridCell=%d,%d' % (i, j), 'GridSize=%dx%d' % (w, h)]
        if stacked:
            tags.append('GridStacked')
        set_tags(cl[0], tags)
        for a in cl[1:]:
            set_tags(a, ['GridPart', 'GridCell=%d,%d' % (i, j)])
        entries.append((cl[0].get_actor_label(), i, j, w, h, stacked))
        cell_of.setdefault(base, {})[cl[0].get_actor_label()] = (i, j)

# crate fruit spheres ride along as composite parts of their crate
for base in ('Crate_Green', 'Crate_Orange', 'Crate_Red'):
    frame = base + '_'
    cell = None
    for lb, c in cell_of.get(base, {}).items():
        if lb.startswith('WBM_' + frame):
            cell = c
            break
    if cell is None:
        continue
    for a in actors_of(base + 'Fill'):
        set_tags(a, ['GridPart', 'GridCell=%d,%d' % cell])

# chickens are livestock decoration inside a coop footprint: cell-tagged, not placeable
for a in actors_of('Chicken'):
    loc = a.get_actor_location()
    i, j, _c = snap_xy(loc.x, loc.y)
    set_tags(a, ['GridDecor', 'GridCell=%d,%d' % (i, j)])
for a in actors_of('ChickenHead'):
    loc = a.get_actor_location()
    best = None
    for c in actors_of('Chicken'):
        q = c.get_actor_location()
        if best is None or math.hypot(loc.x - q.x, loc.y - q.y) < best[1]:
            best = (c, math.hypot(loc.x - q.x, loc.y - q.y))
    if best:
        for t in best[0].tags:
            t = str(t)
            if t.startswith('GridCell='):
                set_tags(a, ['GridPart', t])
                break

# crops are cell-sized placeables as well
for a in EAS.get_all_level_actors():
    if a.get_actor_label().startswith('GRD_Crop_'):
        tags = [str(t) for t in a.tags]
        for t in tags:
            if t.startswith('GridCell='):
                ij = t.split('=', 1)[1].split(',')
                entries.append((a.get_actor_label(), int(ij[0]), int(ij[1]), 1, 1, True))


# ---------------------------------------------------------------- overlay
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


SRC = r'C:\lly_UE_Proj\Ori5_8\Content\_LOCAL\_SrcTex\T_GridCell.png'
SIZE = 512   # 2x2 cells; one cell = 256 px = 100 cm
HALF = SIZE // 2
LINE = 8     # 3.1 cm wide, opaque lines
px = bytearray(SIZE * SIZE * 4)
for y in range(SIZE):
    for x in range(SIZE):
        lx, ly = x % HALF, y % HALF
        d = min(lx, ly)                    # distance to the cell edge
        checker = ((x // HALF) + (y // HALF)) % 2
        if d < LINE:
            col = (255, 255, 255, 245)
        elif d < LINE + 2:
            col = (255, 255, 255, 110)     # AA fringe
        else:
            col = (255, 255, 255, 26) if checker == 0 else (30, 30, 30, 16)
        o = (y * SIZE + x) * 4
        px[o:o + 4] = bytes(col)
write_png_rgba(SRC, SIZE, SIZE, px)

if not EAL.does_asset_exist('/Game/_BLOCKOUT/T_GridCell'):
    task = unreal.AssetImportTask()
    task.set_editor_property('filename', SRC)
    task.set_editor_property('destination_path', '/Game/_BLOCKOUT')
    task.set_editor_property('destination_name', 'T_GridCell')
    task.set_editor_property('automated', True)
    task.set_editor_property('save', True)
    task.set_editor_property('replace_existing', True)
    AT.import_asset_tasks([task])
tex = unreal.load_object(None, '/Game/_BLOCKOUT/T_GridCell.T_GridCell')
if tex:
    tex.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    tex.set_editor_property('srgb', True)
    EAL.save_loaded_asset(tex)

if not EAL.does_asset_exist('/Game/_BLOCKOUT/M_BuildGrid'):
    mel = unreal.MaterialEditingLibrary
    mat = AT.create_asset('M_BuildGrid', '/Game/_BLOCKOUT', unreal.Material,
                          unreal.MaterialFactoryNew())
    mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property('two_sided', True)
    tc = mel.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -700, 0)
    tc.set_editor_property('u_tiling', 17.0)   # 3400 cm / (2 cells * 100 cm)
    tc.set_editor_property('v_tiling', 12.0)   # 2400 cm / 200
    ts = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -420, 0)
    ts.set_editor_property('texture', tex)
    mel.connect_material_expressions(tc, '', ts, 'UVs')
    mel.connect_material_property(ts, 'RGB', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.connect_material_property(ts, 'A', unreal.MaterialProperty.MP_OPACITY)
    mel.recompile_material(mat)
    EAL.save_loaded_asset(mat)
mat = unreal.load_object(None, '/Game/_BLOCKOUT/M_BuildGrid.M_BuildGrid')

grid = find_label('WBM_BuildGrid')
HOVER = 6.8   # above the dining floor (top z=4) and the kitchen slab (top z=6)
if grid is None:
    grid = EAS.spawn_actor_from_class(unreal.StaticMeshActor,
                                      unreal.Vector(0.0, 0.0, HOVER),
                                      unreal.Rotator(0.0, 0.0, 0.0))
    grid.set_actor_label('WBM_BuildGrid')
    grid.set_folder_path('Grid')
    comp = grid.get_editor_property('static_mesh_component')
    comp.set_static_mesh(unreal.load_object(None, '/Engine/BasicShapes/Plane.Plane'))
    comp.set_material(0, mat)
grid.set_actor_location(unreal.Vector(0.0, 0.0, HOVER), False, True)
grid.set_actor_scale3d(unreal.Vector(34.0, 24.0, 1.0))
# keep review captures readable: the overlay is editor/build-visualisation only
grid.get_editor_property('static_mesh_component').set_editor_property('hidden_in_scene_capture', True)
set_tags(grid, ['GridOverlay', 'GridCellSize=100', 'GridOrigin=0,0'])


# ---------------------------------------------------------------- verify
occupied = {}
conflicts = []
crops = set()
ground = set()
for label, i, j, w, h, stacked in entries:
    if stacked:
        if label.startswith('GRD_Crop_'):
            crops.add((i, j))
        continue
    for ci in range(i, i + w):
        for cj in range(j, j + h):
            if (ci, cj) in occupied:
                conflicts.append((label, occupied[(ci, cj)], ci, cj))
            occupied[(ci, cj)] = label
for a in EAS.get_all_level_actors():
    if a.get_actor_label().startswith('GRD_Soil_'):
        for t in a.tags:
            t = str(t)
            if t.startswith('GridCell='):
                ij = t.split('=', 1)[1].split(',')
                ground.add((int(ij[0]), int(ij[1])))

print('grid entries: %d, occupied cells: %d, conflicts: %d'
      % (len(entries), len(occupied), len(conflicts)))
for c in conflicts[:20]:
    print('CONFLICT', c)

ROWS = ('x=-1700..1700 (1 cell = 100cm, ^ north / +y, > east / +x)')
for j in range(23, -25, -1):
    row = ''
    for i in range(-34, 34):
        if (i, j) in occupied:
            row += '#'
        elif (i, j) in crops:
            row += 'C'
        elif (i, j) in ground:
            row += 'f'
        else:
            row += '.'
    print('%3d %s' % (j, row))
print(ROWS)

with open(r'C:\lly_UE_Proj\Ori5_8\Saved\GridManifest.txt', 'w', encoding='utf-8') as f:
    f.write('label\tanchor_i\tanchor_j\tw\th\tstacked\n')
    for e in sorted(entries):
        f.write('%s\t%d\t%d\t%d\t%d\t%d\n' % e)
print('manifest -> Saved/GridManifest.txt')

unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
print('map saved')
