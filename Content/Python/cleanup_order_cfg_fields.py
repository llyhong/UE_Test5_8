"""清理订单配置资产（ULevelOrderCfg）里 FOrderData 已删除的旧字段。

背景：FOrderData 现在只剩 FoodIDs（见 Source/Shoot/Public/Game/DataClass.h），
SetName / Price / MaxWaitTime / Probabilitys / IamgeID / MachineImageID 都已从 C++ 结构体删除。
但历史资产（Content/_Demo/Data/Order/Level_0X_Order）里仍残留这些属性的序列化数据：
加载时会刷 “Failed to find property …” 之类的警告，旧值也一直占着磁盘体积。
本脚本把这类资产按当前结构体重新保存一次，旧属性随之被丢弃。

用法（二选一）：
  1) 编辑器内：Output Log 下方 Cmd 输入框执行
         py "C:/lly_UE_Proj/Ori5_8/Content/Python/cleanup_order_cfg_fields.py"
  2) 命令行（编辑器未打开时）：
         UnrealEditor-Cmd.exe C:/lly_UE_Proj/Ori5_8/Shoot.uproject -run=pythonscript \
             -script="C:/lly_UE_Proj/Ori5_8/Content/Python/cleanup_order_cfg_fields.py" \
             -unattended -nosplash -nop4 -nullrhi -stdout

脚本只读 Orders 里现存的数据并原样保存，不会修改任何菜品配置；运行完会在日志里
打印每个资产每一单的 FoodIDs，可用于核对旧字段清理后数据是否还在。
"""

import unreal

ORDER_DIR = "/Game/_Demo/Data/Order"
LEGACY_FIELDS = "SetName, Price, MaxWaitTime, Probabilitys, IamgeID, MachineImageID"


def _log(message):
    unreal.log("[CleanupOrderCfg] " + message)


def _find_cfg_assets():
    """返回 ORDER_DIR 下所有 ULevelOrderCfg 资产的包路径。"""
    paths = []
    for asset_path in unreal.EditorAssetLibrary.list_assets(ORDER_DIR, recursive=True, include_folder=False):
        asset = unreal.EditorAssetLibrary.load_asset(asset_path)
        if isinstance(asset, unreal.LevelOrderCfg):
            paths.append(asset_path)
    return paths


def _dump_orders(asset_path, asset):
    orders = asset.get_editor_property("Orders")
    _log("%s：%d 条订单" % (asset_path, len(orders)))
    for index, order in enumerate(orders):
        food_ids = order.get_editor_property("FoodIDs")
        _log("    第 %d 单 FoodIDs = %s" % (index + 1, list(food_ids)))


def main():
    cfg_paths = _find_cfg_assets()
    if not cfg_paths:
        unreal.log_warning("[CleanupOrderCfg] %s 下没有找到 ULevelOrderCfg 资产" % ORDER_DIR)
        return

    # 先读一遍：清理前的真实配置（旧属性在加载时已被丢弃，这里看到的就是保留下来的数据）
    for asset_path in cfg_paths:
        asset = unreal.EditorAssetLibrary.load_asset(asset_path)
        if asset:
            _dump_orders(asset_path, asset)

    # 重新保存：写盘时只序列化当前 FOrderData 的字段，旧属性随之从资产里消失
    failed = []
    for asset_path in cfg_paths:
        if unreal.EditorAssetLibrary.save_asset(asset_path, only_if_is_dirty=False):
            _log("已重新保存 %s（旧字段 %s 已清除）" % (asset_path, LEGACY_FIELDS))
        else:
            failed.append(asset_path)
            unreal.log_error("[CleanupOrderCfg] 保存失败：%s" % asset_path)

    if failed:
        unreal.log_error("[CleanupOrderCfg] 共 %d 个资产保存失败：%s" % (len(failed), ", ".join(failed)))
    else:
        _log("完成：共重置 %d 个订单配置资产" % len(cfg_paths))


main()
