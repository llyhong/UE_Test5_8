import unreal

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
acts = eas.get_all_level_actors()

rules = [
    ('Lighting', ('BLD_',)),
    ('PCG', ('WBM_Fence_',)),
    ('World', ('WBM_GroundInf_', 'WBM_PathEntrance_', 'WBM_PathFarm_', 'WBM_PlayerStart')),
    ('Architecture', ('WBM_Wall_', 'WBM_FloorInterior_', 'WBM_FloorKitchen_', 'WBM_DoorHeader_')),
    ('Restaurant', ('WBM_TableTop_', 'WBM_TableLeg_', 'WBM_ChairSeat_', 'WBM_ChairBack_',
                    'WBM_QueuePost_', 'WBM_QueueBar_', 'WBM_MenuBoard_', 'WBM_CashierCounter_',
                    'WBM_CashierRegister_', 'WBM_Text_044')),
    ('Kitchen', ('WBM_Sink_', 'WBM_Stove', 'WBM_Fridge_', 'WBM_KitchenIsland_',
                 'WBM_KitchenShelf_', 'WBM_KitchenTable_', 'WBM_PassCounter_')),
    ('Farm/Animals', ('WBM_Cow', 'WBM_Pig', 'WBM_Chicken', 'WBM_Coop')),
    ('Farm/Crops', ('WBM_Corn_', 'WBM_CropBed_')),
    ('Farm/Orchard', ('WBM_TreeTrunk_', 'WBM_TreeCrown_')),
    ('Farm/Decor', ('WBM_Well', 'WBM_Rock_')),
    ('Signs', ('WBM_Arrow', 'WBM_Dash_', 'WBM_Text_', 'WBM_LegendPlate_', 'WBM_Zone_')),
]

counts = {}
left = []
for a in acts:
    label = a.get_actor_label()
    if label == 'PCGWorldActor0':
        continue
    for folder, prefixes in rules:
        if any(label.startswith(p) for p in prefixes):
            a.set_folder_path(folder)
            counts[folder] = counts.get(folder, 0) + 1
            break
    else:
        left.append(label)

print('moved total')
for k in sorted(counts):
    print('  %-16s %d' % (k, counts[k]))
print('left unmatched:', left)
