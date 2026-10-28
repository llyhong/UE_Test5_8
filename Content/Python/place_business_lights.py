# -*- coding: utf-8 -*-
"""
在编辑器里批量放置“营业灯饰(BusinessLight)”与“大门交互台(GateCounter)”。
运行前请先编译 C++（让 Shoot 模块加载，否则 load_class 会返回 None）。

放置规则（贴合营业/用餐区，模拟“开门才亮灯”）
  - 每个 ChairSeat（座位）上方 200cm 放一盏暖黄吊灯（小范围）
  - 每个 ServingCounter（上菜台/吧台）上方 260cm 放一盏主灯（较大范围）
  - 每个 AttractCounter（招揽台）上方 260cm 放一盏灯
  - 在 PlayerStart（玩家出生点=餐厅入口）放置一个 GateCounter（大门，按E开门营业）

重复运行会先清理本脚本先前放置的 BusinessLight / GateCounter，避免产生副本。
"""
import unreal

# 优先使用项目内的蓝图子类（BP_BusinessLight / BP_GateCounter，位于 Content/_Demo/Blueprints/Guest），
# 它们在 C++ 基类 ABusinessLight / AGateCounter 之上加了可见模型等；加载失败再回退到 C++ 基类。
BUSINESS_LIGHT_CLS = unreal.load_class(None, "/Game/_Demo/Blueprints/Guest/BP_BusinessLight.BP_BusinessLight_C")
if BUSINESS_LIGHT_CLS is None:
    BUSINESS_LIGHT_CLS = unreal.load_class(None, "/Script/Shoot.BusinessLight")

GATE_COUNTER_CLS = unreal.load_class(None, "/Game/_Demo/Blueprints/Guest/BP_GateCounter.BP_GateCounter_C")
if GATE_COUNTER_CLS is None:
    GATE_COUNTER_CLS = unreal.load_class(None, "/Script/Shoot.GateCounter")

SEAT_CLS = "ChairSeat"
SERVING_CLS = "ServingCounter"
ATTRACT_CLS = "AttractCounter"
PLAYER_START_CLS = "PlayerStart"


def get_all_actors():
    return unreal.EditorLevelLibrary.get_all_level_actors()


def clear_previous():
    removed = 0
    for a in get_all_actors():
        name = a.get_class().get_name()
        # 同时兼容蓝图子类（BP_BusinessLight_C / BP_GateCounter_C）与原 C++ 基类（BusinessLight / GateCounter）
        if name.startswith("BP_BusinessLight") or name.startswith("BP_GateCounter") \
                or name in ("BusinessLight", "GateCounter"):
            unreal.EditorLevelLibrary.destroy_actor(a)
            removed += 1
    return removed


def place_lights_above(ref_class_name, height, radius, intensity):
    count = 0
    for a in get_all_actors():
        if a.get_class().get_name() == ref_class_name:
            loc = a.get_actor_location()
            loc.z += height
            light = unreal.EditorLevelLibrary.spawn_actor_from_class(BUSINESS_LIGHT_CLS, loc)
            if light:
                light.set_editor_property("LitRadius", radius)
                light.set_editor_property("LitIntensity", intensity)
                light.set_editor_property("LitColor", unreal.LinearColor(1.0, 0.78, 0.42, 1.0))
                count += 1
    return count


def place_gate_at_player_start():
    for a in get_all_actors():
        if a.get_class().get_name() == PLAYER_START_CLS:
            loc = a.get_actor_location()
            gate = unreal.EditorLevelLibrary.spawn_actor_from_class(GATE_COUNTER_CLS, loc)
            if gate:
                return 1
    return 0


def main():
    if BUSINESS_LIGHT_CLS is None or GATE_COUNTER_CLS is None:
        unreal.log_error(
            "[放置灯饰] 无法加载类，请先编译 Shoot 模块，并确认蓝图存在："
            "/Game/_Demo/Blueprints/Guest/BP_BusinessLight、/Game/_Demo/Blueprints/Guest/BP_GateCounter"
            "（或 C++ 基类 /Script/Shoot.BusinessLight、/Script/Shoot.GateCounter）"
        )
        return

    removed = clear_previous()
    seat_n = place_lights_above(SEAT_CLS, 200.0, 450.0, 1200.0)
    serving_n = place_lights_above(SERVING_CLS, 260.0, 600.0, 1800.0)
    attract_n = place_lights_above(ATTRACT_CLS, 260.0, 600.0, 1800.0)
    gate_n = place_gate_at_player_start()
    total = seat_n + serving_n + attract_n + gate_n

    unreal.log_warning(
        "[放置灯饰] 已清理旧实例 %d 个；新放置：座位灯 %d、吧台灯 %d、招揽灯 %d、大门 %d"
        % (removed, seat_n, serving_n, attract_n, gate_n)
    )

    if total > 0:
        unreal.EditorLevelLibrary.save_current_level()
        unreal.log_warning("[放置灯饰] 已保存关卡")
    else:
        unreal.log_warning("[放置灯饰] 未放置任何 Actor：请确认场景中存在 ChairSeat/ServingCounter/AttractCounter/PlayerStart，且模块已编译")


main()
