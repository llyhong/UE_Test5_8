import unreal as ue

editor = ue.get_editor_subsystem(ue.EditorActorSubsystem)
world = ue.get_editor_subsystem(ue.UnrealEditorSubsystem).get_editor_world()
assert world.get_name() == 'Kitchen_New', world.get_name()
plants = [actor for actor in editor.get_all_level_actors() if isinstance(actor, ue.Plant)]
assert plants
positions = [actor.get_actor_location() for actor in plants]
min_x, max_x = min(p.x for p in positions), max(p.x for p in positions)
min_y, max_y = min(p.y for p in positions), max(p.y for p in positions)
ground_z = sum(p.z for p in positions) / len(positions)
center_y = (min_y + max_y) * 0.5
extent_y = (max_y - min_y) * 0.5 + 80.0

placed = []
for side, x in [('West', min_x - 200.0), ('East', max_x + 200.0)]:
    label = 'Fireflies_Farm_' + side
    actor = next((a for a in editor.get_all_level_actors() if a.get_actor_label() == label), None)
    if actor is None:
        actor = editor.spawn_actor_from_class(ue.FireflyAreaVolume, ue.Vector(x, center_y, ground_z + 130.0))
        assert actor
        actor.set_actor_label(label)
        actor.set_folder_path('Farm/Fireflies')
        actor.set_actor_scale3d(ue.Vector(1.4, extent_y / 100.0, 0.8))
        actor.set_editor_property('firefly_count', 16)
    placed.append({'label': label, 'location': str(actor.get_actor_location()), 'bounds': str(actor.get_actor_bounds(False))})

assert ue.EditorLoadingAndSavingUtils.save_current_level()
print('FIREFLY_AREAS_PLACED', placed)
