"""Normalize animations retargeted onto rabit_rigged_Skeleton.

rabit_rigged was imported with the FBX armature root bone (RabitRig) carrying
scale=100 while every child bone keeps meter-scale translations. IK Retargeter
writes poses with root scale=1 and centimeter translations, so the skinned
rabbit collapses to 1/100 size and looks empty in Persona.

This rewrites each exported AnimSequence into the skeleton's native convention:
    RabitRig   -> scale (100, 100, 100), identity translation
    all others -> translation / 100, rotation and scale unchanged

Usage (editor Python):
    import fix_rabit_retarget_scale as f
    f.fix_folder("/Game/_BLOCKOUT")
    f.fix_folder("/Game/Characters", recursive=True)
"""

import unreal

ROOT_BONE = "RabitRig"
SCALE_FACTOR = 100.0
SKELETON_HINT = "rabit"


def _bone_names(model):
    return [str(name) for name in model.get_bone_track_names()]


def needs_fix(anim):
    pose = unreal.AnimationLibrary.get_bone_pose_for_time(anim, ROOT_BONE, 0.0, False)
    return pose.scale3d.x < SCALE_FACTOR * 0.5


def fix_animation(anim):
    model = anim.get_editor_property("data_model_interface")
    num_keys = int(model.get_number_of_keys())
    if num_keys == 0:
        return False

    times = [float(unreal.AnimationLibrary.get_time_at_frame(anim, i)) for i in range(num_keys)]
    controller = anim.get_editor_property("controller")
    controller.open_bracket(unreal.Text("Normalize rabit root scale"), False)
    try:
        for bone in _bone_names(model):
            is_root = bone.lower() == ROOT_BONE.lower()
            positions, rotations, scales = [], [], []
            for time in times:
                pose = unreal.AnimationLibrary.get_bone_pose_for_time(anim, bone, time, False)
                rotations.append(pose.rotation)
                if is_root:
                    positions.append(unreal.Vector(0.0, 0.0, 0.0))
                    scales.append(unreal.Vector(SCALE_FACTOR, SCALE_FACTOR, SCALE_FACTOR))
                else:
                    positions.append(unreal.Vector(pose.translation.x / SCALE_FACTOR,
                                                   pose.translation.y / SCALE_FACTOR,
                                                   pose.translation.z / SCALE_FACTOR))
                    scales.append(pose.scale3d)
            controller.set_bone_track_keys(bone, positions, rotations, scales, False)
    finally:
        controller.close_bracket()
    return True


def fix_folder(folder="/Game/_BLOCKOUT", recursive=True):
    fixed = []
    for path in unreal.EditorAssetLibrary.list_assets(folder, recursive=recursive):
        try:
            anim = unreal.load_asset(path)
        except Exception:
            continue
        if not isinstance(anim, unreal.AnimSequence):
            continue
        skeleton = anim.get_editor_property("skeleton")
        if not skeleton or SKELETON_HINT not in skeleton.get_name().lower():
            continue
        if not needs_fix(anim):
            continue
        if fix_animation(anim):
            unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
            fixed.append(path)
    return fixed
