"""生成天气系统资产：/Game/Weather/DT_WeatherPresets + /Game/Weather/BP_WeatherSystem。

用法（编辑器 Python Console 或 MCP 执行）：
    import build_weather_assets
    build_weather_assets.run()

当前版本使用 /Game/SoStylized 资产：NS_RainWithSplash、NS_LightningStrike、SC_LightningStrike、MS_Rain 等。
需要先编译并重启编辑器（FWeatherPreset / AWeatherSystem 是新增 C++ 类型）。
脚本可重复执行：已存在的表会按本脚本内容覆盖，蓝图存在则只重设天气表引用。
"""

import json

import unreal

ASSET_DIR = "/Game/Weather"
DT_NAME = "DT_WeatherPresets"
BP_NAME = "BP_WeatherSystem"

# 使用 SoStylized 资产
RAIN_EFFECT = "/Game/SoStylized/Effects/Systems/NS_RainWithSplash.NS_RainWithSplash"
RAIN_SOUND_GENTLE = "/Game/SoStylized/Sounds/Weather/S_Rain_Gentle.S_Rain_Gentle"
RAIN_SOUND_MEDIUM = "/Game/SoStylized/Sounds/Weather/S_Rain_Medium.S_Rain_Medium"
RAIN_SOUND_META = "/Game/SoStylized/Sounds/Weather/MS_Rain.MS_Rain"
THUNDER_SOUND = "/Game/SoStylized/Sounds/Weather/SC_LightningStrike.SC_LightningStrike"
WIND_SOUND = "/Game/SoStylized/Sounds/Weather/S_Ambience_Wind.S_Ambience_Wind"


def _color(r, g, b, a=1.0):
    return {"R": r, "G": g, "B": b, "A": a}


def _row(name, display, desc, weights, sun_scale, sun_tint, sky_scale, sky_color,
         fog_density, fog_color, wind_strength, wind_speed,
         precip="None", precip_rate=1.0, precip_effect="", ambient="", ambient_volume=0.0,
         lightning=False, transition=15.0):
    return {
        "Name": name,
        "DisplayName": display,
        "Description": desc,
        "Icon": "",
        "SpringWeight": weights[0],
        "SummerWeight": weights[1],
        "AutumnWeight": weights[2],
        "WinterWeight": weights[3],
        "SunIntensityScale": sun_scale,
        "SunColorTint": sun_tint,
        "SkyLightIntensityScale": sky_scale,
        "SkyLightColor": sky_color,
        "FogDensity": fog_density,
        "FogHeightFalloff": 0.18,
        "FogMaxOpacity": 1.0,
        "FogColor": fog_color,
        "bCloudsVisible": True,
        "CloudMaterial": "",
        "CloudCoverageParameter": "",
        "CloudCoverage": {"Clear": 0.0, "PartlyCloudy": 0.25, "Cloudy": 0.6,
                          "Overcast": 1.0, "Foggy": 0.65, "Windy": 0.4,
                          "RainLight": 0.75, "Rain": 0.9, "Thunderstorm": 1.0}[name],
        "CloudDensityParameter": "",
        "CloudDensity": 0.0,
        "WindStrength": wind_strength,
        "WindSpeed": wind_speed,
        "PrecipitationType": precip,
        "PrecipitationEffect": precip_effect,
        "PrecipitationRateParameter": "",
        "PrecipitationRate": precip_rate,
        "AmbientSound": ambient,
        "AmbientVolume": ambient_volume,
        "bLightning": lightning,
        "LightningIntervalMin": 5.0,
        "LightningIntervalMax": 14.0,
        "ThunderSound": THUNDER_SOUND if lightning else "",
        "TransitionSeconds": transition,
    }


ROWS = [
    # 晴
    _row("Clear", "晴", "阳光充足，适合营业。", (4.0, 4.0, 3.5, 2.0),
         1.0, _color(1.0, 1.0, 1.0), 1.0, _color(0.85, 0.9, 1.0),
         0.012, _color(0.55, 0.68, 0.85), 0.1, 0.3,
         transition=12.0),
    # 多云
    _row("PartlyCloudy", "多云", "云量适中，光线柔和。", (2.5, 2.5, 2.5, 1.5),
         0.85, _color(1.0, 0.97, 0.92), 0.9, _color(0.8, 0.85, 0.95),
         0.015, _color(0.55, 0.67, 0.83), 0.2, 0.45,
         transition=15.0),
    # 阴
    _row("Cloudy", "阴", "天空阴沉，风略大。", (1.5, 1.2, 1.8, 1.5),
         0.6, _color(0.85, 0.88, 0.95), 0.75, _color(0.75, 0.8, 0.9),
         0.02, _color(0.5, 0.6, 0.72), 0.35, 0.6,
         transition=15.0),
    # 阴天密云
    _row("Overcast", "阴云密布", "厚重云层压顶，天色很暗。", (0.8, 0.6, 1.0, 0.8),
         0.35, _color(0.75, 0.8, 0.9), 0.55, _color(0.65, 0.72, 0.85),
         0.03, _color(0.45, 0.55, 0.68), 0.45, 0.7,
         transition=18.0),
    # 雾
    _row("Foggy", "雾", "能见度低，远处白茫茫一片。", (1.0, 0.3, 1.5, 0.6),
         0.5, _color(0.9, 0.92, 0.95), 0.7, _color(0.8, 0.85, 0.9),
         0.09, _color(0.62, 0.7, 0.8), 0.15, 0.3,
         transition=18.0),
    # 大风
    _row("Windy", "大风", "狂风呼啸，注意招牌。", (0.8, 1.0, 1.2, 1.0),
         0.75, _color(1.0, 0.95, 0.9), 0.8, _color(0.8, 0.85, 0.9),
         0.02, _color(0.55, 0.65, 0.78), 1.2, 1.0,
         ambient=WIND_SOUND, ambient_volume=0.5, transition=15.0),
    # 小雨
    _row("RainLight", "小雨", "细雨绵绵，路面湿滑。", (1.5, 1.0, 1.5, 0.2),
         0.55, _color(0.8, 0.85, 0.92), 0.65, _color(0.7, 0.76, 0.86),
         0.03, _color(0.5, 0.6, 0.72), 0.4, 0.6,
         precip="Rain", precip_rate=0.5, precip_effect=RAIN_EFFECT,
         ambient=RAIN_SOUND_GENTLE, ambient_volume=0.35, transition=15.0),
    # 中雨
    _row("Rain", "雨", "持续降雨，记得照顾客人。", (1.0, 0.8, 1.2, 0.0),
         0.4, _color(0.75, 0.8, 0.88), 0.55, _color(0.65, 0.72, 0.84),
         0.04, _color(0.45, 0.55, 0.68), 0.6, 0.8,
         precip="Rain", precip_rate=1.0, precip_effect=RAIN_EFFECT,
         ambient=RAIN_SOUND_MEDIUM, ambient_volume=0.5, transition=15.0),
    # 雷雨
    _row("Thunderstorm", "雷雨", "电闪雷鸣，暴雨倾盆。", (0.6, 1.2, 0.4, 0.0),
         0.25, _color(0.7, 0.75, 0.9), 0.45, _color(0.6, 0.68, 0.82),
         0.05, _color(0.38, 0.45, 0.58), 1.2, 1.0,
         precip="Rain", precip_rate=1.5, precip_effect=RAIN_EFFECT,
         ambient=RAIN_SOUND_META, ambient_volume=0.6, lightning=True, transition=20.0),
]


def _ensure_dir():
    if not unreal.EditorAssetLibrary.does_directory_exist(ASSET_DIR):
        unreal.EditorAssetLibrary.make_directory(ASSET_DIR)


def _get_or_create_datatable():
    dt_path = "{0}/{1}".format(ASSET_DIR, DT_NAME)
    dt = unreal.EditorAssetLibrary.load_asset(dt_path)
    if dt:
        return dt

    factory = unreal.DataTableFactory()
    try:
        factory.set_editor_property("struct", unreal.WeatherPreset)
    except Exception:
        factory.set_editor_property("struct", unreal.WeatherPreset.static_struct())
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    return asset_tools.create_asset(DT_NAME, ASSET_DIR, unreal.DataTable, factory)


def _get_or_create_blueprint():
    bp_path = "{0}/{1}".format(ASSET_DIR, BP_NAME)
    bp = unreal.EditorAssetLibrary.load_asset(bp_path)
    if bp:
        return bp

    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", unreal.WeatherSystem)
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    return asset_tools.create_asset(BP_NAME, ASSET_DIR, unreal.Blueprint, factory)


def run():
    _ensure_dir()

    dt = _get_or_create_datatable()
    if not dt:
        raise RuntimeError("创建天气数据表失败")

    json_text = json.dumps(ROWS, ensure_ascii=False, indent=1)
    ok = unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(dt, json_text)
    if not ok:
        raise RuntimeError("天气预设 JSON 导入失败，请检查字段名/类型是否与 FWeatherPreset 一致")
    unreal.EditorAssetLibrary.save_loaded_asset(dt, False)

    bp = _get_or_create_blueprint()
    if bp:
        cdo = unreal.get_default_object(bp.generated_class())
        cdo.set_editor_property("weather_table", dt)
        # 修改蓝图默认值后必须编译，否则 CDO 不会写入 uasset
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        unreal.EditorAssetLibrary.save_loaded_asset(bp, False)

    unreal.log("[Weather] 已生成 {0}/{1}（{2} 条天气）与 {3}/{4}".format(
        ASSET_DIR, DT_NAME, len(ROWS), ASSET_DIR, BP_NAME))
    unreal.log("[Weather] 把 BP_WeatherSystem 拖入关卡；场景有 BP_StylizedSky 时，天空/云/雾由它负责，")
    unreal.log("[Weather] AWeatherSystem 负责驱动降水粒子、环境音与雷暴闪光。")
    return dt, bp
