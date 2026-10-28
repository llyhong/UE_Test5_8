# 一键创建经营模拟蓝图链：
#   BP_TimeSystem         (继承 UTimeSystem，可在其 Class Defaults 调一天时长/营业时段 8~22/太阳参数)
#   BP_BusinessSimulation (继承 UBusinessSimulation，并把 TimeSystemClass 指向 BP_TimeSystem)
# 之后再在关卡 BaseLevelBP 的 Details -> Business -> Business Simulation Class 选 BP_BusinessSimulation。
#
# 运行：编辑器 Output Log 执行  py create_business_blueprints.py
# 前提：Shoot C++ 模块已编译（否则父类 /Script/Shoot.TimeSystem 等无法加载）。
#
# 注意：每次运行都会“重新指向” TimeSystemClass；若你已在 BP_TimeSystem 里调好的参数（如 bAutoOpenBusiness=false）
# 在 Class Defaults 中已保存，不会被本脚本覆盖（本脚本只设 TimeSystemClass 引用，不动其它默认值）。

import unreal

BP_DIR = "/Game/_Demo/Blueprints"
TS_PARENT = "/Script/Shoot.TimeSystem"
BIZ_PARENT = "/Script/Shoot.BusinessSimulation"


def ensure_blueprint(name, parent_path):
    pkg = f"{BP_DIR}/{name}"
    existing = unreal.load_asset(pkg)
    if existing is not None:
        unreal.log(f"[经营蓝图] {name} 已存在，跳过创建")
        return existing

    parent_class = unreal.load_class(None, parent_path)
    if parent_class is None:
        unreal.log_error(
            f"[经营蓝图] 无法加载父类 {parent_path}，请先编译 Shoot 模块"
        )
        return None

    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", parent_class)
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    bp = asset_tools.create_asset(name, BP_DIR, unreal.Blueprint, factory)
    if bp is None:
        unreal.log_error(f"[经营蓝图] 创建 {name} 失败")
        return None

    # 新建后必须编译，否则下方的 BP_TimeSystem_C 加载不到、TimeSystemClass 无法指向它
    unreal.KismetEditorUtilities.compile_blueprint(bp)
    unreal.log(f"[经营蓝图] 已创建并编译 {name}（父类={parent_path}）")
    return bp


def main():
    bp_ts = ensure_blueprint("BP_TimeSystem", TS_PARENT)
    bp_biz = ensure_blueprint("BP_BusinessSimulation", BIZ_PARENT)
    if bp_ts is None or bp_biz is None:
        return

    # 在 BP_BusinessSimulation 的 Class Defaults 中把 TimeSystemClass 指向 BP_TimeSystem_C
    ts_class = unreal.load_class(None, f"{BP_DIR}/BP_TimeSystem.BP_TimeSystem_C")
    if ts_class is None:
        unreal.log_error(
            "[经营蓝图] 无法加载 BP_TimeSystem_C，请确认 BP_TimeSystem 已编译（可重新运行本脚本一次）"
        )
        return

    gen = bp_biz.generated_class()
    cdo = unreal.get_default_object(gen)
    cdo.set_editor_property("TimeSystemClass", ts_class)
    unreal.KismetEditorUtilities.compile_blueprint(bp_biz)
    unreal.EditorAssetLibrary.save_asset(f"{BP_DIR}/BP_BusinessSimulation")
    unreal.log("[经营蓝图] 已将 BP_BusinessSimulation.TimeSystemClass -> BP_TimeSystem")


main()
