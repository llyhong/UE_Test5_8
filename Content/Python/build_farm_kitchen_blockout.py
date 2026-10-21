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

# ---------------------------------------------------------------- ground
bc.box('WBM_GroundInf', 0, 0, -5, 200000, 200000, 10, G)  # single 2km ground, top at z=0, no edge step
bc.box('WBM_FloorInterior', 900, 0, 2, 1400, 2000, 4, W)
bc.box('WBM_FloorKitchen', 1250, 550, 4, 700, 900, 4, G)
bc.box('WBM_PathEntrance', 920, -1105, 2, 240, 210, 4, G)
bc.box('WBM_PathFarm', -1025, -425, 2, 150, 1550, 4, G)

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
bc.box('WBM_Sink', 990, 940, 45, 100, 70, 90, G)
bc.box('WBM_KitchenTable', 1160, 940, 45, 200, 70, 90)
bc.box('WBM_Stove', 1330, 945, 45, 100, 80, 90)
bc.box('WBM_Stove', 1440, 945, 45, 100, 80, 90)
bc.cyl('WBM_StovePot', 1330, 945, 105, 40, 30)
bc.box('WBM_Fridge', 1550, 780, 100, 80, 80, 200)
bc.box('WBM_Fridge', 1550, 650, 100, 80, 80, 200)
bc.box('WBM_KitchenIsland', 1200, 500, 45, 200, 90, 90)
bc.box('WBM_KitchenShelf', 935, 400, 80, 30, 160, 160, G)
bc.box('WBM_PassCounter', 1350, 145, 45, 220, 60, 90)   # kitchen side of pass window, top at 90

# ---------------------------------------------------------------- cashier between dining and kitchen + queue
bc.box('WBM_CashierCounter', 1350, 60, 50, 280, 60, 100)  # dining side, under pass window
bc.box('WBM_CashierRegister', 1440, 60, 112, 24, 24, 24, G)
bc.box('WBM_MenuBoard', 1300, 85, 180, 300, 10, 70, G)
for qx in (1150, 1270, 1390):
    bc.cyl('WBM_QueuePost', qx, -200, 50, 10, 100)
bc.box('WBM_QueueBar', 1210, -200, 92, 130, 6, 6)
bc.box('WBM_QueueBar', 1330, -200, 92, 130, 6, 6)
bc.text('WBM_Text', 920, -1185, 5, 'ENTRANCE', 34, (0.15, 0.15, 0.15))

# ---------------------------------------------------------------- dining: 6 tables 3 cols x 2 rows
for ty, row in ((-600, 1), (-200, 2)):
    for tx in (300, 560, 820):
        bc.box('WBM_TableTop', tx, ty, 72, 130, 85, 6)
        bc.box('WBM_TableLeg', tx, ty, 35, 14, 14, 70)
        for ox in (-35, 35):
            bc.box('WBM_ChairSeat', tx + ox, ty - 78, 22, 42, 42, 45)
            bc.box('WBM_ChairBack', tx + ox, ty - 96, 70, 42, 8, 50)
            bc.box('WBM_ChairSeat', tx + ox, ty + 78, 22, 42, 42, 45)
            bc.box('WBM_ChairBack', tx + ox, ty + 96, 70, 42, 8, 50)

# ---------------------------------------------------------------- farm: PCG fence
# One BP_Fence owns four spline templates (cow pen, pig pen, chicken yard, patio U) plus a
# PCG component that samples every spline of the actor at 250cm and spawns SM_Fence.
# Reshape any fence by editing BP_Fence's splines, then recompile and it updates in place.
# Patio U: west x=200 / north y=1000 / east x=1600 (dining-area shell, open to the south).
_cls = unreal.BlueprintEditorLibrary.generated_class(unreal.load_object(None, '/Game/_BLOCKOUT/BP_Fence.BP_Fence'))
_fence = bc._eas().spawn_actor_from_class(_cls, unreal.Vector(0.0, 0.0, 0.0), unreal.Rotator(0.0, 0.0, 0.0))
_fence.set_actor_label('WBM_Fences')
for cx, cy in ((-1450, -250), (-1280, 100), (-1400, 220)):
    bc.cyl_x('WBM_Cow', cx, cy, 85, 70, 170)
    bc.sph('WBM_CowHead', cx + 95, cy, 100, 42)
for cx, cy in ((-850, -250), (-700, 60), (-800, 220)):
    bc.cyl_x('WBM_Pig', cx, cy, 55, 52, 110)
    bc.sph('WBM_PigHead', cx + 65, cy, 62, 34)

# chicken coops + chickens
for ccx in (-380, -170):
    bc.box('WBM_Coop', ccx, -330, 45, 140, 110, 90)
    bc.cone_up('WBM_CoopRoof', ccx, -330, 115, 150, 50)
for kx, ky in ((-300, -150), (-255, -110), (-345, -110), (-205, -165)):
    bc.sph('WBM_Chicken', kx, ky, 13, 26, zsq=0.85)
    bc.sph('WBM_ChickenHead', kx + 11, ky, 22, 13)

# well
bc.cyl('WBM_Well', -250, 200, 30, 80, 60, G)
bc.box('WBM_WellPost', -280, 200, 60, 10, 10, 120)
bc.box('WBM_WellPost', -220, 200, 60, 10, 10, 120)
bc.box('WBM_WellBar', -250, 200, 125, 80, 16, 16)

# corn field: PCG BP_Corn — one closed rect spline (x -440..-100, y 450..830) baked in the
# BP's spline template; PCG_CornGraph fills the interior on an ~80cm grid and spawns Engine
# Cone scaled (0.26,0.26,1.0) lifted z+50. Resize the field by editing BP_Corn's spline.
_cls = unreal.BlueprintEditorLibrary.generated_class(unreal.load_object(None, '/Game/_BLOCKOUT/BP_Corn.BP_Corn'))
_corn = bc._eas().spawn_actor_from_class(_cls, unreal.Vector(0.0, 0.0, 0.0), unreal.Rotator(0.0, 0.0, 0.0))
_corn.set_actor_label('WBM_Corn_Field')

# crop beds 3x3
for bx in (-1450, -1100, -750):
    for by in (620, 780, 940):
        bc.box('WBM_CropBed', bx, by, 15, 280, 110, 30, G)

# trees (pine = cone, round = sphere)
trees = [(-1550, -850, 1), (-1300, -950, 0), (-1000, -900, 1), (-700, -980, 0),
         (-350, -920, 1), (-1550, 700, 0), (-450, 1000, 1), (60, 1050, 1),
         (-100, -800, 0), (60, -500, 1)]
for tx, ty, pine in trees:
    bc.cyl('WBM_TreeTrunk', tx, ty, 45, 20, 90, G)
    if pine:
        bc.cone_up('WBM_TreeCrown', tx, ty, 170, 110, 160)
    else:
        bc.sph('WBM_TreeCrown', tx, ty, 150, 120)

for rx, ry, rd in ((-600, -700, 60), (-1550, 150, 50), (-620, 1050, 70), (-160, -1050, 44), (-1180, -780, 52)):
    bc.sph('WBM_Rock', rx, ry, rd * 0.22, rd, G, zsq=0.6)

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

# ---------------------------------------------------------------- legend (bottom-left)
bc.box('WBM_LegendPlate', -1275, -1085, 2, 660, 160, 4, G)
bc.text('WBM_Text', -1560, -1030, 5, 'LEGEND', 34, (0.1, 0.1, 0.1))
bc.floor_arrow(-1580, -1085, -1480, -1085, CUST, z=7)
bc.text('WBM_Text', -1350, -1085, 5, 'Customer Flow', 32, (0.1, 0.1, 0.1))
bc.floor_arrow(-1580, -1140, -1480, -1140, SERV, dashed=True, width=12, z=13)
bc.text('WBM_Text', -1350, -1140, 5, 'Service Flow', 32, (0.1, 0.1, 0.1))

# ---------------------------------------------------------------- zone labels
bc.text('WBM_Zone', -850, 100, 6, 'FARM', 90, (0.45, 0.45, 0.45))
bc.text('WBM_Zone', 430, -880, 6, 'DINING', 70, (0.45, 0.45, 0.45))
bc.text('WBM_Zone', 1350, 800, 8, 'KITCHEN', 50, (0.45, 0.45, 0.45))

# ---------------------------------------------------------------- PIE: third-person entry
gm_cls = unreal.load_object(None, '/Game/ThirdPerson/Blueprints/BP_ThirdPersonGameMode.BP_ThirdPersonGameMode_C')
for a in bc._eas().get_all_level_actors():
    if isinstance(a, unreal.WorldSettings):
        a.set_editor_property('default_game_mode', gm_cls)
ps = bc._eas().spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(920.0, -1150.0, 100.0), unreal.Rotator(0.0, 90.0, 0.0))
ps.set_actor_label('WBM_PlayerStart')

unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, False)

eas = bc._eas()
result = {
    'deleted': deleted,
    'spawned': bc._N[0],
    'total_actors': len(eas.get_all_level_actors()),
}
