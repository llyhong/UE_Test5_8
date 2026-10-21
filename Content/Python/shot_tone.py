import math, os
import unreal

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
cam = unreal.Vector(300.0, -1150.0, 900.0)
tgt = unreal.Vector(-900.0, -100.0, 60.0)
d = tgt - cam
hor = math.sqrt(d.x * d.x + d.y * d.y)
yaw = math.degrees(math.atan2(d.y, d.x))
pitch = math.degrees(math.atan2(d.z, hor))
key = les.get_active_viewport_config_key()
# swizzle: pass (roll, pitch, yaw) -> stored (pitch, yaw, roll)
les.set_level_viewport_camera_info(cam, unreal.Rotator(0.0, pitch, yaw), key)
print('cam set', cam, 'pitch', pitch, 'yaw', yaw)

w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.SystemLibrary.execute_console_command(w, 'HighResShot 2560x1440', None)
print('shot fired')

sd = 'C:/lly_UE_Proj/Ori5_8/Saved/Screenshots/WindowsEditor'
now = os.path.getmtime(sd) if os.path.isdir(sd) else 0
print('marker', now)
