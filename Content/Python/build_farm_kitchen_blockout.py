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
bc.box('WBM_PathEntrance', 920, -1105, 2, 240, 210, 4, G)
# no farm dirt path — the reference image has none, flows are dashed arrows only

# ---------------------------------------------------------------- building shell (h300 t20)
bc.wall_x('WBM_Wall', 200, 300, -1000, 0, 300)          # south, west of window 1
bc.wall_x('WBM_Wall', 300, 480, -1000, 0, 90)           # window 1 sill
bc.wall_x('WBM_Wall', 300, 480, -1000, 230, 300)        # window 1 header
bc.wall_x('WBM_Wall', 480, 560, -1000, 0, 300)          # pier
bc.wall_x('WBM_Wall', 560, 740, -1000, 0, 90)           # window 2 sill
bc.wall_x('WBM_Wall', 560, 740, -1000, 230, 300)        # window 2 header
bc.wall_x('WBM_Wall', 740, 800, -1000, 0, 300)          # pier before entrance
bc.wall_x('WBM_Wall', 1040, 1600, -1000, 0, 300)        # south, east of entrance
bc.box('WBM_DoorHeader', 920, -1000, 270, 240, 20, 60)  # over entrance 240w x 240h
# north/west/east shell sides are open-air: PCG fence BP_Fence (see fence section below)
# kitchen divider at Y=100 with pass window (opening X 1250..1450, z 90..210)
bc.wall_x('WBM_Wall', 900, 1250, 100, 0, 300)
bc.wall_x('WBM_Wall', 1450, 1600, 100, 0, 300)
bc.wall_x('WBM_Wall', 1250, 1450, 100, 0, 90)           # sill (counter top at 90)
bc.wall_x('WBM_Wall', 1250, 1450, 100, 210, 300)        # header

# ---------------------------------------------------------------- restrooms (SW inside)
bc.wall_y('WBM_Wall', -1000, -760, 470, 0, 300)         # east divider
bc.wall_y('WBM_Wall', -1000, -760, 340, 0, 300)         # stall divider
bc.wall_x('WBM_Wall', 250, 380, -760, 0, 300)           # front wall, left of door gap
bc.wall_x('WBM_Wall', 450, 470, -760, 0, 300)           # front wall, right of door gap

# ---------------------------------------------------------------- kitchen equipment (kitchen X 900..1600, Y 100..1000)
# north-edge row per reference image, west -> east: fridges | sink | prep | stoves+pot | crates
# box/cyl z is CENTRE; floor kitchen slab top z=6, gear bottom z=4 (sunk 2cm)
bc.box('WBM_Sink', 1076, 940, 49, 100, 70, 90, G)
bc.box('WBM_KitchenTable', 1165, 940, 49, 200, 70, 90)
bc.box('WBM_Stove', 1254, 945, 49, 100, 80, 90)
bc.box('WBM_Stove', 1334, 945, 49, 100, 80, 90)
# whitebox-kept kitchen furniture is panda-scaled (PANDA=0.73, see replace_whitebox_props);
# swapped pieces keep human whitebox dims here — the replace script rescales the real meshes
bc.cyl('WBM_StovePot', 1254, 945, 84.5, 29, 22)
bc.box('WBM_Fridge', 938, 940, 104, 80, 80, 200)
bc.box('WBM_Fridge', 1005, 940, 104, 80, 80, 200)
bc.box('WBM_KitchenIsland', 1200, 500, 33, 146, 66, 66)
bc.box('WBM_KitchenShelf', 935, 400, 58, 22, 117, 117, G)
bc.box('WBM_PassCounter', 1350, 145, 33, 161, 44, 66)   # kitchen side of pass window

# ---------------------------------------------------------------- cashier between dining and kitchen + queue
bc.box('WBM_CashierCounter', 1350, 60, 50, 280, 60, 100)  # dining side, under pass window
bc.box('WBM_CashierRegister', 1440, 60, 112, 24, 24, 24, G)
# queue line stands by the player start at the entrance (replace script consolidates the
# posts into one SM_QueueBarrier there); no menu board — the HangingSign covers signage
for qx in (1150, 1270, 1390):
    bc.cyl('WBM_QueuePost', qx, -200, 50, 10, 100)
bc.box('WBM_QueueBar', 1210, -200, 92, 130, 6, 6)
bc.box('WBM_QueueBar', 1330, -200, 92, 130, 6, 6)
# greet counter outside the front door, west side of the entrance path (whitebox for now)
bc.box('WBM_GreetCounter', 740, -1120, 40, 88, 44, 80)
bc.text('WBM_Text', 920, -1185, 5, 'ENTRANCE', 34, (0.15, 0.15, 0.15))

# ---------------------------------------------------------------- dining: 6 tables 3 cols x 2 rows
for ty, row in ((-600, 1), (-200, 2)):
    for tx in (300, 560, 820):
        bc.box('WBM_TableTop', tx, ty, 72, 130, 85, 6)
        bc.box('WBM_TableLeg', tx, ty, 35, 14, 14, 70)
        # two seats per table: one chair each side, centred on the table axis
        bc.box('WBM_ChairSeat', tx, ty - 78, 22, 42, 42, 45)
        bc.box('WBM_ChairBack', tx, ty - 96, 70, 42, 8, 50)
        bc.box('WBM_ChairSeat', tx, ty + 78, 22, 42, 42, 45)
        bc.box('WBM_ChairBack', tx, ty + 96, 70, 42, 8, 50)

# ---------------------------------------------------------------- farm: open world (PCG fence + fields)
# One BP_Fence owns four spline templates (cattle pen rect, pig hexagon, patio U; the old
# chicken-yard spline Spline2 stays empty — coops are scattered now) plus a
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
# cattle pen on the SW edge, pig hexagon on the south edge, four coops scattered wide,
# well + rectangular reservoir west-centre, wooded hill pushed to the NW world edge.
# SM_FieldTile/SM_FieldRidge ridged ground and the real plant meshes come from
# replace_farm_props.py + farm_world_setup.py, field fences were removed. No dirt paths
# — flows are dashed arrows only. The whole strip east of x~-150 is left open as a buffer
# between farm and the dining patio (user: farm must not hug the customer/table side).

# cattle pen (x -1650..-1050 / y -1050..-650): 2 cows, feed trough, ring feeder
for cx, cy in ((-1470, -825), (-1230, -945)):
    bc.cyl_x('WBM_Cow', cx, cy, 85, 70, 170)
    bc.sph('WBM_CowHead', cx + 95, cy, 100, 42)
bc.box('WBM_Trough', -1350, -700, 17, 320, 60, 34, G)
bc.cyl('WBM_RingFeeder', -1150, -855, 8, 170, 16, G)
bc.cyl('WBM_RingFeeder', -1150, -855, 9, 120, 18, W)

# pig hexagon (x -560..-140 / y -889..-611)
for cx, cy in ((-390, -770), (-240, -680)):
    bc.cyl_x('WBM_Pig', cx, cy, 55, 52, 110)
    bc.sph('WBM_PigHead', cx + 65, cy, 62, 34)
bc.box('WBM_Trough', -320, -860, 15, 220, 50, 30, G)
bc.cyl('WBM_RingFeeder', -290, -690, 7, 120, 14, G)
bc.cyl('WBM_RingFeeder', -290, -690, 8, 80, 16, W)

# four hut coops scattered wide (no chicken run), birds pecking around each;
# none east of x=-300 — the whole east strip is a buffer between farm and dining patio
for ccx, ccy in ((-620, 320), (-880, 450), (-320, -180), (-940, -760)):
    bc.box('WBM_Coop', ccx, ccy, 45, 140, 110, 90)
    bc.cone_up('WBM_CoopRoof', ccx, ccy, 115, 150, 50)
for kx, ky in ((-665, 260), (-575, 260), (-925, 390), (-835, 390),
               (-365, -240), (-275, -240), (-985, -820), (-895, -820)):
    bc.sph('WBM_Chicken', kx, ky, 13, 26, zsq=0.85)
    bc.sph('WBM_ChickenHead', kx + 11, ky, 22, 13)

# round basin south of the pens
bc.cyl('WBM_Basin', -620, -1000, 8, 280, 16, G)
bc.cyl('WBM_BasinWater', -620, -1000, 9, 220, 18, W)

# well (with little roof) + rectangular reservoir, west-centre pair
bc.cyl('WBM_Well', -200, 100, 30, 80, 60, G)
bc.box('WBM_WellPost', -230, 100, 60, 10, 10, 120)
bc.box('WBM_WellPost', -170, 100, 60, 10, 10, 120)
bc.box('WBM_WellBar', -200, 100, 125, 80, 16, 16)
bc.cone_up('WBM_WellRoof', -200, 100, 150, 120, 50)
bc.box('WBM_Reservoir', -520, 100, 20, 280, 140, 40, G)
bc.box('WBM_ReservoirWater', -520, 100, 19, 232, 92, 38, W)

# three fields: BP_Corn instances with per-instance PlantClass. The rect (x -440..-100 /
# y 450..830) is baked in the BP spline template around the instance origin; yaw 90 maps
# rel (px,py) -> world offset (-py,px). Corn center (-310,650), cabbage center (-1150,850),
# carrot plot center (-1400,100). Interior grid ~80cm.
bel = unreal.BlueprintEditorLibrary
_stalk = bel.generated_class(unreal.load_object(None, '/Game/_BLOCKOUT/BP_CornStalk.BP_CornStalk'))
_cabbage = bel.generated_class(unreal.load_object(None, '/Game/_BLOCKOUT/BP_Cabbage.BP_Cabbage'))
_carrot = bel.generated_class(unreal.load_object(None, '/Game/_BLOCKOUT/BP_Carrot.BP_Carrot'))
_corn_cls = bel.generated_class(unreal.load_object(None, '/Game/_BLOCKOUT/BP_Corn.BP_Corn'))
_corn = bc._eas().spawn_actor_from_class(_corn_cls, unreal.Vector(-40.0, 10.0, 0.0), unreal.Rotator(0.0, 0.0, 0.0))
_corn.set_actor_label('WBM_Field_Corn')
_corn.set_editor_property('PlantClass', _stalk)
_corn = bc._eas().spawn_actor_from_class(_corn_cls, unreal.Vector(-510.0, 1120.0, 0.0), unreal.Rotator(0.0, 0.0, 90.0))
_corn.set_actor_label('WBM_Field_Cabbage')
_corn.set_editor_property('PlantClass', _cabbage)
_corn = bc._eas().spawn_actor_from_class(_corn_cls, unreal.Vector(-1130.0, -540.0, 0.0), unreal.Rotator(0.0, 0.0, 0.0))
_corn.set_actor_label('WBM_Field_Carrot')
_corn.set_editor_property('PlantClass', _carrot)

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

# colored food crates in a row at the east end of the kitchen row (reference layout);
# z = crate bottom, kitchen slab top z=6 with 2cm sink
make_crate('WBM_Crate_Green', 1409, 940, 4, '/Game/_BLOCKOUT/M_Crate_Green')
make_crate('WBM_Crate_Orange', 1479, 940, 4, '/Game/_BLOCKOUT/M_Crate_Orange')
make_crate('WBM_Crate_Red', 1549, 940, 4, '/Game/_BLOCKOUT/M_Crate_Red')

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
ps = bc._eas().spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(920.0, -1150.0, 100.0), unreal.Rotator(0.0, 0.0, 90.0))
ps.set_actor_label('WBM_PlayerStart')

unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, False)

eas = bc._eas()
result = {
    'deleted': deleted,
    'spawned': bc._N[0],
    'total_actors': len(eas.get_all_level_actors()),
}
