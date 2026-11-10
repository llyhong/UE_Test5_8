import unreal

editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = editor.get_editor_world()
assert world.get_path_name() == '/Game/_BLOCKOUT/Kitchen_New.Kitchen_New'
assert editor.get_game_world() is None
assert hasattr(unreal.OrderSystem, 'can_accept_new_order'), 'Rebuild ShootEditor before applying pacing.'

managers = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.GuestManager)
assert len(managers) == 1
manager = managers[0]
config = unreal.load_asset('/Game/_Demo/Data/Order/Level_01_Order')
assert config is not None

manager_values = {
    'SpawnInterval': 16.0,
    'FirstRushDelay': 12.0,
    'RushInterval': 70.0,
    'RushDuration': 22.0,
    'RushSpawnInterval': 3.5,
    'SpawnRetryDelay': 3.0,
    'MaxGuests': 6,
    'MaxGuestsHardCap': 6,
    'OrderAcceptMode': unreal.GuestOrderAcceptMode.NEED_PLAYER_ACCEPT,
}
config_values = {
    'MaxWaitOrderNum': 4,
    'OrderWaitTimeMultiplier': 0.8,
    'LateServeGraceSeconds': 8.0,
    'LateServePriceMultiplier': 0.8,
}

packages = [manager.get_package(), config.get_package()]
dirty = unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages() + unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()
assert not any(package in dirty for package in packages), 'Target packages have unsaved changes.'
for obj, values in [(manager, manager_values), (config, config_values)]:
    for name in values:
        obj.get_editor_property(name)

with unreal.ScopedEditorTransaction('Configure business pacing'):
    manager.modify()
    config.modify()
    for obj, values in [(manager, manager_values), (config, config_values)]:
        for name, value in values.items():
            obj.set_editor_property(name, value)

assert unreal.EditorLoadingAndSavingUtils.save_packages(packages, False)
for obj, values in [(manager, manager_values), (config, config_values)]:
    for name, value in values.items():
        actual = obj.get_editor_property(name)
        assert abs(actual - value) < 0.0001 if isinstance(value, float) else actual == value

result = {
    'manager': {name: str(manager.get_editor_property(name)) for name in manager_values},
    'patience': {name: config.get_editor_property(name) for name in config_values},
    'saved_packages': [package.get_name() for package in packages],
}
print(result)
