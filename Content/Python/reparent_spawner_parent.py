# -*- coding: utf-8 -*-
"""将 BP_SpawnerParent 的父类由 AItemActor 重定为 AFoodSpawner。
运行前提：工程已编译包含 AFoodSpawner（C++ 已加入，Live Coding / 全量编译后）。
运行方式（二选一）：
  1) 编辑器内：Output Log 切换到 Python，粘贴执行本脚本；或
  2) Commandlet：
     UnrealEditor.exe "E:\Unreal_Proj\UE_Test5_8\Shoot.uproject" -run=pythonscript -script="E:\Unreal_Proj\UE_Test5_8\Content\Python\reparent_spawner_parent.py" -unattended
"""
import unreal

BP_PATH = "/Game/_Demo/Blueprints/Pickable/Spawn/Parent/BP_SpawnerParent"
NEW_PARENT_PATH = "/Script/Shoot.FoodSpawner"   # 模块名 Sho->Shoot，类名 FoodSpawner

eal = unreal.EditorAssetLibrary

bp = unreal.load_asset(BP_PATH)
if bp is None:
    unreal.log_error("[Reparent] 找不到蓝图: " + BP_PATH)
else:
    new_parent = unreal.load_class(None, NEW_PARENT_PATH)
    if new_parent is None:
        unreal.log_error("[Reparent] 找不到父类 " + NEW_PARENT_PATH + "，请确认工程已编译 AFoodSpawner")
    else:
        old_parent = bp.get_editor_property("parent_class")
        unreal.log("[Reparent] 当前父类: {}".format(old_parent.get_path_name() if old_parent else "None"))

        # 重定父类
        unreal.BlueprintEditorLibrary.reparent_blueprint(bp, new_parent)

        # 重新编译以应用新父类（并校验蓝图是否仍合法）
        compile_status = unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        unreal.log("[Reparent] 编译结果: {}".format(compile_status))

        # 保存资产
        if eal.save_asset(BP_PATH):
            unreal.log("[Reparent] 已保存: " + BP_PATH)
            unreal.log("[Reparent] 成功将父类改为 {}".format(new_parent.get_path_name()))
        else:
            unreal.log_error("[Reparent] 保存失败: " + BP_PATH)
