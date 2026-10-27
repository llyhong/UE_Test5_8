# -*- coding: utf-8 -*-
"""Farm + restaurant whitebox blockout builder. Idempotent: wipes WBM_* then rebuilds.

Coordinates (cm): ground X -1700..1700, Y -1200..1200.
Building X 200..1600, Y -1000..1000. Farm west of X 200. +X east, +Y north.
"""
import unreal
import blockout_common as bc

W = bc.MAT_WHITE
G = bc.MAT_GRAY
CUST = bc.MAT_CUST
SERV = bc.MAT_SERV

deleted = bc.cleanup_wbm()

# PCG SpawnActor leftovers (BP_Tomato / BP_CornStalk / ... instances) are plain level actors,
# not owned by the field actor — sweep them by generated class name.
eas0 = bc._eas()
for a in [x for x in eas0.get_all_level_actors()
          if x.get_class().get_name().startswith('BP_') and not x.get_actor_label().startswith('WBM_')]:
    eas0.destroy_actor(a)

# ---------------------------------------------------------------- ground
bc.box('WBM_GroundInf', 0, 0, -5, 200000, 200000, 10, G)  # single 2km ground, top at z=0, no edge step
bc.box('WBM_FloorInterior', 900, 0, 2, 1400, 2000, 4, W)
bc.box('WBM_FloorKitchen', 1250, 550, 4, 700, 900, 4, G)
bc.box('WBM_PathEntrance', 900, -1100, 2, 200, 200, 4, G)
# no farm dirt path — the reference image has none, flows are dashed arrows only

# ---------------------------------------------------------------- building shell (h300 t20)
# every run sits on the 100cm grid (see grid_layout.py: cell (i,j) = x 100i..100i+100)
bc.wall_x('WBM_Wall', 200, 300, -1000, 0, 300)          # south, west of window 1
bc.wall_x('WBM_Wall', 300, 500, -1000, 0, 90)           # window 1 sill (2 cells)
bc.wall_x('WBM_Wall', 300, 500, -1000, 230, 300)        # window 1 header
bc.wall_x('WBM_Wall', 500, 600, -1000, 0, 300)          # pier
bc.wall_x('WBM_Wall', 600, 800, -1000, 0, 90)           # window 2 sill (2 cells)
bc.wall_x('WBM_Wall', 600, 800, -1000, 230, 300)        # window 2 header
bc.wall_x('WBM_Wall', 1000, 1600, -1000, 0, 300)        # south, east of entrance
bc.box('WBM_DoorHeader', 900, -1000, 270, 200, 20, 60)  # over entrance 200w x 240h
# north/west/east shell sides are open-air: PCG fence BP_Fence (see fence section below)
# kitchen divider at Y=100 with pass window (opening X 1200..1400, z 90..210)
bc.wall_x('WBM_Wall', 900, 1200, 100, 0, 300)
bc.wall_x('WBM_Wall', 1400, 1600, 100, 0, 300)
bc.wall_x('WBM_Wall', 1200, 1400, 100, 0, 90)           # sill (counter top at 90)
bc.wall_x('WBM_Wall', 1200, 1400, 100, 210, 300)        # header

# ---------------------------------------------------------------- restrooms (SW inside)
bc.wall_y('WBM_Wall', -1000, -760, 470, 0, 300)         # east divider
bc.wall_y('WBM_Wall', -1000, -760, 340, 0, 300)         # stall divider
bc.wall_x('WBM_Wall', 250, 380, -760, 0, 300)           # front wall, left of door gap
bc.wall_x('WBM_Wall', 450, 470, -760, 0, 300)           # front wall, right of door gap

# ---------------------------------------------------------------- kitchen equipment (kitchen X 900..1600, Y 100..1000)
# north-edge row on grid row j=9 (y 900..1000), west -> east: fridges | prep | stoves+pot | sink
# box/cyl z is CENTRE; floor kitchen slab top z=6, gear bottom z=4 (sunk 2cm)
bc.box('WBM_Sink', 1450, 950, 49, 100, 70, 90, G)
bc.box('WBM_KitchenTable', 1150, 950, 49, 200, 70, 90)
bc.box('WBM_Stove', 1210, 950, 49, 100, 80, 90)
bc.box('WBM_Stove', 1290, 950, 49, 100, 80, 90)
# whitebox-kept kitchen furniture is panda-scaled (PANDA=0.73, see replace_whitebox_props);
# swapped pieces keep human whitebox dims here — the replace script rescales the real meshes
bc.cyl('WBM_StovePot', 1350, 950, 84.5, 29, 22)
bc.box('WBM_Fridge', 950, 950, 104, 80, 80, 200)
bc.box('WBM_Fridge', 1050, 950, 104, 80, 80, 200)
bc.box('WBM_KitchenIsland', 1200, 450, 33, 146, 66, 66)
bc.box('WBM_KitchenShelf', 950, 500, 58, 22, 117, 117, G)
bc.box('WBM_PassCounter', 1300, 150, 33, 161, 44, 66)   # kitchen side of pass window

# ---------------------------------------------------------------- cashier between dining and kitchen + queue
bc.box('WBM_CashierCounter', 1300, 50, 50, 280, 60, 100)   # dining side, under pass window
bc.box('WBM_CashierRegister', 1350, 50, 112, 24, 24, 24, G)
# queue line stands by the player start at the entrance (replace script consolidates the
# posts into one SM_QueueBarrier there); no menu board — the HangingSign covers signage
for qx in (1150, 1270, 1390):
    bc.cyl('WBM_QueuePost', qx, -200, 50, 10, 100)
bc.box('WBM_QueueBar', 1210, -200, 92, 130, 6, 6)
bc.box('WBM_QueueBar', 1330, -200, 92, 130, 6, 6)
# greet counter outside the front door, west edge of the entrance path (whitebox for now)
bc.box('WBM_GreetCounter', 750, -1050, 40, 88, 44, 80)
bc.text('WBM_Text', 900, -1150, 5, 'ENTRANCE', 34, (0.15, 0.15, 0.15))

# ---------------------------------------------------------------- dining: 6 tables 3 cols x 2 rows (grid cells)
for ty, row in ((-650, 1), (-250, 2)):
    for tx in (350, 550, 750):
        bc.box('WBM_TableTop', tx, ty, 72, 130, 85, 6)
        bc.box('WBM_TableLeg', tx, ty, 35, 14, 14, 70)
        # two seats per table: one chair each side, centred on the table axis
        bc.box('WBM_ChairSeat', tx, ty - 100, 22, 42, 42, 45)
        bc.box('WBM_ChairBack', tx, ty - 118, 70, 42, 8, 50)
        bc.box('WBM_ChairSeat', tx, ty + 100, 22, 42, 42, 45)
        bc.box('WBM_ChairBack', tx, ty + 118, 70, 42, 8, 50)

# ---------------------------------------------------------------- farm: open world (PCG fence + fields)
# One BP_Fence owns the spline templates (cattle pen 6x4 rect, pig pen 5x4 rect, patio U; the
# old chicken-yard spline Spline2 stays empty — coops are scattered now) plus a
# PCG component that samples every spline of the actor at 250cm and spawns SM_Fence.
# Reshape any pen by editing BP_Fence's spline templates (see farm_world_setup.py), recompile.
# FenceMesh (SoftObject StaticMesh var, default SM_Fence, instance-editable) drives which
# mesh PCG spawns: GetActorProperty -> CopyAttributes -> spawner picks it by attribute.
# Patio U: west x=200 / north y=1000 / east x=1600 (dining-area shell, open to the south).
_cls = unreal.BlueprintEditorLibrary.generated_class(unreal.load_object(None, '/Game/_BLOCKOUT/BP_Fence.BP_Fence'))
_fence = bc._eas().spawn_actor_from_class(_cls, unreal.Vector(0.0, 0.0, 0.0), unreal.Rotator(0.0, 0.0, 0.0))
_fence.set_actor_label('WBM_Fences')
_fence.set_folder_path('PCG')

def make_crate(label, x, y, z, mat):
    bc.box(label, x, y, z + 1, 60, 40, 2, mat)
    bc.box(label, x, y - 19, z + 13, 60, 2, 24, mat)
    bc.box(label, x, y + 19, z + 13, 60, 2, 24, mat)
    bc.box(label, x - 29, y, z + 13, 2, 36, 24, mat)
    bc.box(label, x + 29, y, z + 13, 2, 36, 24, mat)
    for px, py in ((-28, -18), (28, -18), (-28, 18), (28, 18)):
        bc.box(label, x + px, y + py, z + 14, 4, 4, 28, mat)
    for i in range(3):
        bc.sph(label + 'Fill', x - 12 + i * 12, y, z + 26, 9, mat)

# v3 spread for a big-world breathing feel: three open field plots scattered diagonally
# across the north (cabbage NW-top, corn centre-N, carrot W) with wide gaps between them,
# cattle pen on the SW edge, pig pen on the south edge, four coops scattered wide,
# well + rectangular reservoir west-centre, wooded hill pushed to the NW world edge.
# Field soil/crops are per-cell GRD_* actors made by grid_layout.py (100cm build grid);
# field fences were removed. No dirt paths — flows are dashed arrows only. The whole strip
# east of x~-150 is left open as a buffer between farm and the dining patio (user: farm
# must not hug the customer/table side).

# cattle pen rect (x -1700..-1100 / y -1100..-700, 6x4 cells): 2 cows, feed trough, ring feeder
for cx, cy in ((-1500, -900), (-1250, -750)):
    bc.cyl_x('WBM_Cow', cx, cy, 85, 70, 170)
    bc.sph('WBM_CowHead', cx + 95, cy, 100, 42)
bc.box('WBM_Trough', -1400, -750, 17, 320, 60, 34, G)
bc.cyl('WBM_RingFeeder', -1200, -900, 8, 170, 16, G)
bc.cyl('WBM_RingFeeder', -1200, -900, 9, 120, 18, W)

# pig pen rect (x -600..-100 / y -900..-500, 5x4 cells)
for cx, cy in ((-520, -680), (-400, -560)):
    bc.cyl_x('WBM_Pig', cx, cy, 55, 52, 110)
    bc.sph('WBM_PigHead', cx + 65, cy, 62, 34)
bc.box('WBM_Trough', -400, -850, 15, 220, 50, 30, G)
bc.cyl('WBM_RingFeeder', -200, -700, 7, 120, 14, G)
bc.cyl('WBM_RingFeeder', -200, -700, 8, 80, 16, W)

# four hut coops scattered wide (no chicken run), birds pecking around each;
# none east of x=-300 — the whole east strip is a buffer between farm and dining patio
for ccx, ccy in ((-600, 300), (-800, 400), (-300, -200), (-900, -800)):
    bc.box('WBM_Coop', ccx, ccy, 45, 140, 110, 90)
    bc.cone_up('WBM_CoopRoof', ccx, ccy, 115, 150, 50)
for kx, ky in ((-645, 240), (-555, 240), (-845, 340), (-755, 340),
               (-345, -260), (-255, -260), (-945, -860), (-855, -860)):
    bc.sph('WBM_Chicken', kx, ky, 13, 26, zsq=0.85)
    bc.sph('WBM_ChickenHead', kx + 11, ky, 22, 13)

# round basin (3x3) south of the pens
bc.cyl('WBM_Basin', -1450, -450, 8, 280, 16, G)
bc.cyl('WBM_BasinWater', -1450, -450, 9, 220, 18, W)

# well (2x2, with little roof) + rectangular reservoir (2x2), west-centre pair
bc.cyl('WBM_Well', -200, 100, 30, 80, 60, G)
bc.box('WBM_WellPost', -230, 100, 60, 10, 10, 120)
bc.box('WBM_WellPost', -170, 100, 60, 10, 10, 120)
bc.box('WBM_WellBar', -200, 100, 125, 80, 16, 16)
bc.cone_up('WBM_WellRoof', -200, 100, 150, 120, 50)
bc.box('WBM_Reservoir', -500, 100, 20, 280, 140, 40, G)
bc.box('WBM_ReservoirWater', -500, 100, 19, 232, 92, 38, W)

# three crop plots are built per-cell by grid_layout.py:
#   corn    cells x -7..-2 / y 4..8   cabbage cells x -14..-10 / y 6..10
#   carrot  cells x -17..-13 / y -2..2
# it spawns GRD_Soil_* (SM_FieldTile) + GRD_Crop_* on every 100cm cell, tagged for build mode.

# wooded hill pushed to the NW world edge + scattered trees (none hugging the patio fence)
bc.sph('WBM_Hill', -1780, 1100, 30, 360, G, zsq=0.32)
trees = [(-1950, 1000, 1), (-1800, 1230, 1), (-1650, 1050, 0), (-1880, 1300, 1),
         (-1600, 1230, 0), (-1720, 910, 1), (-700, 250, 0), (-180, 1010, 1), (1150, -1100, 0)]
for tx, ty, pine in trees:
    bc.cyl('WBM_TreeTrunk', tx, ty, 45, 20, 90, G)
    # no crown here — TreeTrunk is swapped for a complete SM_WildTree by the replace script

for rx, ry, rd in ((-1690, -550, 55), (-800, -850, 40), (-450, 320, 40), (-1600, -1150, 44),
                   (-120, -850, 36), (-1450, -1150, 50)):
    bc.sph('WBM_Rock', rx, ry, rd * 0.22, rd, G, zsq=0.6)

# scattered flower dots for breathing room (kept west of the farm/dining buffer strip)
for fx, fy in ((-1500, -700), (-1500, -450), (-800, -250), (-350, -800), (-600, -500),
               (-550, 30), (-550, -40), (-750, 350), (-1250, 1080), (-200, 900),
               (-120, 300), (-170, 300), (-1200, 600), (-1050, -950), (-900, -980)):
    bc.sph('WBM_Flower', fx, fy, 4, 16, W, zsq=0.5)

# colored food crates on the east open edge of the kitchen (cells (15,5)/(15,6)/(15,7));
# z = crate bottom, kitchen slab top z=6 with 2cm sink
make_crate('WBM_Crate_Green', 1550, 550, 4, '/Game/_BLOCKOUT/M_Crate_Green')
make_crate('WBM_Crate_Orange', 1550, 650, 4, '/Game/_BLOCKOUT/M_Crate_Orange')
make_crate('WBM_Crate_Red', 1550, 750, 4, '/Game/_BLOCKOUT/M_Crate_Red')

# ---------------------------------------------------------------- customer flow (solid cyan)
bc.poly_arrow([(920, -1150), (920, -950)], CUST, z=2.5)
bc.poly_arrow([(920, -950), (430, -950), (430, -150)], CUST, z=2.5)
bc.poly_arrow([(920, -950), (690, -950), (690, -150)], CUST, z=2.5)
bc.poly_arrow([(920, -950), (1250, -950), (1250, -230)], CUST, z=2.5)
bc.poly_arrow([(1250, -180), (1250, -30)], CUST, z=2.5)
for ry in (-600, -200):
    bc.floor_arrow(430, ry, 370, ry, CUST, z=2.5)
    bc.floor_arrow(430, ry, 500, ry, CUST, z=2.5)
    bc.floor_arrow(690, ry, 620, ry, CUST, z=2.5)
    bc.floor_arrow(690, ry, 760, ry, CUST, z=2.5)

# ---------------------------------------------------------------- service flow (dashed orange, layered above)
bc.poly_arrow([(1350, -20), (310, -20)], SERV, dashed=True, width=12, z=8.5)
bc.poly_arrow([(690, -20), (690, -350), (310, -350)], SERV, dashed=True, width=12, z=8.5)
for tx in (310, 560, 820):
    bc.floor_arrow(tx, -20, tx, -130, SERV, dashed=True, width=12, z=8.5)
    bc.floor_arrow(tx, -350, tx, -460, SERV, dashed=True, width=12, z=8.5)
# farm harvest runs converging on the restaurant entrance (floor arrows are axis-aligned only)
# v3 spread routing: main trunk x=-60, south collector ring y=-1150
bc.poly_arrow([(-60, 480), (-60, -1150), (920, -1150)], SERV, dashed=True, width=12, z=8.5)
bc.poly_arrow([(-1150, 680), (-1150, -100), (-60, -100)], SERV, dashed=True, width=12, z=8.5)
bc.poly_arrow([(-200, 10), (-200, -100), (-60, -100)], SERV, dashed=True, width=12, z=8.5)
bc.poly_arrow([(-1400, -140), (-830, -140), (-830, -1150)], SERV, dashed=True, width=12, z=8.5)
bc.poly_arrow([(-1350, -1060), (-1350, -1150), (-60, -1150)], SERV, dashed=True, width=12, z=8.5)
bc.poly_arrow([(-350, -910), (-350, -1150), (-60, -1150)], SERV, dashed=True, width=12, z=8.5)
bc.poly_arrow([(-940, -850), (-940, -1150), (-60, -1150)], SERV, dashed=True, width=12, z=8.5)

# ---------------------------------------------------------------- legend (bottom-left)
bc.box('WBM_LegendPlate', -1275, -1085, 2, 660, 160, 4, G)
bc.text('WBM_Text', -1560, -1030, 5, 'LEGEND', 34, (0.1, 0.1, 0.1))
bc.floor_arrow(-1580, -1085, -1480, -1085, CUST, z=7)
bc.text('WBM_Text', -1350, -1085, 5, 'Customer Flow', 32, (0.1, 0.1, 0.1))
bc.floor_arrow(-1580, -1140, -1480, -1140, SERV, dashed=True, width=12, z=13)
bc.text('WBM_Text', -1350, -1140, 5, 'Service Flow', 32, (0.1, 0.1, 0.1))

# ---------------------------------------------------------------- zone labels
bc.text('WBM_Zone', -850, -950, 6, 'FARM', 90, (0.45, 0.45, 0.45))
bc.text('WBM_Zone', -1350, -850, 6, 'PASTURE', 46, (0.45, 0.45, 0.45))
bc.text('WBM_Zone', -800, 1100, 6, 'CROPS', 46, (0.45, 0.45, 0.45))
bc.text('WBM_Zone', 430, -880, 6, 'DINING', 70, (0.45, 0.45, 0.45))
bc.text('WBM_Zone', 1350, 800, 8, 'KITCHEN', 50, (0.45, 0.45, 0.45))

# ---------------------------------------------------------------- PIE: third-person entry
gm_cls = unreal.load_object(None, '/Game/ThirdPerson/Blueprints/BP_ThirdPersonGameMode.BP_ThirdPersonGameMode_C')
for a in bc._eas().get_all_level_actors():
    if isinstance(a, unreal.WorldSettings):
        a.set_editor_property('default_game_mode', gm_cls)
ps = bc._eas().spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(900.0, -1100.0, 100.0), unreal.Rotator(0.0, 0.0, 90.0))
ps.set_actor_label('WBM_PlayerStart')

unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, False)

eas = bc._eas()
result = {
    'deleted': deleted,
    'spawned': bc._N[0],
    'total_actors': len(eas.get_all_level_actors()),
}
