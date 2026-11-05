// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Engine/Texture2D.h"
#include "Game/BusinessTime.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraSystem.h"
#include "Sound/SoundBase.h"
#include "WeatherTypes.generated.h"

/** 降水类型（决定默认粒子效果与雨声） */
UENUM(BlueprintType)
enum class EPrecipitationType : uint8
{
	None UMETA(DisplayName = "无"),
	Rain UMETA(DisplayName = "雨"),
	Snow UMETA(DisplayName = "雪"),
	Dust UMETA(DisplayName = "沙尘"),
};

/**
 * 天气预设（一行一种天气）。
 * 设计为纯数据配置：光照 / 雾 / 云 / 风 / 降水 / 音效 / 雷电全部走表，
 * 由 AWeatherSystem 读取并平滑过渡到场景组件，便于后续增删改天气。
 */
USTRUCT(BlueprintType)
struct FWeatherPreset : public FTableRowBase
{
	GENERATED_BODY()

	// ========== UI ==========
	/** 天气名称（UI 显示） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|UI")
	FText DisplayName;

	/** 天气描述（UI / 提示） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|UI")
	FText Description;

	/** 天气图标 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|UI")
	TSoftObjectPtr<UTexture2D> Icon;

	// ========== 季节权重（按当前季节加权随机抽取；填 0 表示该季节不会出现） ==========
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Season", meta = (ClampMin = "0"))
	float SpringWeight = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Season", meta = (ClampMin = "0"))
	float SummerWeight = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Season", meta = (ClampMin = "0"))
	float AutumnWeight = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Season", meta = (ClampMin = "0"))
	float WinterWeight = 1.f;

	// ========== 太阳（叠加在时间系统的日变化之上） ==========
	/** 太阳强度倍率（1=晴天正常，阴雨天 <1） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Sun", meta = (ClampMin = "0"))
	float SunIntensityScale = 1.f;

	/** 太阳颜色染色（与时间系统的日变化颜色相乘） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Sun")
	FLinearColor SunColorTint = FLinearColor::White;

	// ========== 天光 ==========
	/** 天光强度倍率（相对 AWeatherSystem.BaseSkyLightIntensity） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|SkyLight", meta = (ClampMin = "0"))
	float SkyLightIntensityScale = 1.f;

	/** 天光颜色 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|SkyLight")
	FLinearColor SkyLightColor = FLinearColor(0.85f, 0.9f, 1.f);

	// ========== 指数高度雾 ==========
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Fog", meta = (ClampMin = "0"))
	float FogDensity = 0.02f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Fog", meta = (ClampMin = "0"))
	float FogHeightFalloff = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Fog", meta = (ClampMin = "0", ClampMax = "1"))
	float FogMaxOpacity = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Fog")
	FLinearColor FogColor = FLinearColor(0.55f, 0.65f, 0.8f);

	// ========== 体积云 ==========
	/** 是否显示云层（阴雨 / 晴天可用不同云材质） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Clouds")
	bool bCloudsVisible = true;

	/** 云材质（留空则不改动场景里已设置的材质） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Clouds")
	TSoftObjectPtr<UMaterialInterface> CloudMaterial;

	/** 云覆盖度参数名（材质 / MPC 中的标量参数；留空则不驱动） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Clouds")
	FName CloudCoverageParameter;

	/** 云覆盖度 0~1 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Clouds", meta = (ClampMin = "0", ClampMax = "1"))
	float CloudCoverage = 0.3f;

	/** 云密度参数名（材质 / MPC 中的标量参数；留空则不驱动） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Clouds")
	FName CloudDensityParameter;

	/** 云密度（材质单位，通常 0~1 或更大） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Clouds", meta = (ClampMin = "0"))
	float CloudDensity = 0.5f;

	// ========== 风 ==========
	/** 风力强度（WindDirectionalSource.Strength） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Wind", meta = (ClampMin = "0"))
	float WindStrength = 0.2f;

	/** 风速（WindDirectionalSource.Speed） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Wind", meta = (ClampMin = "0"))
	float WindSpeed = 0.6f;

	// ========== 降水 ==========
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Precipitation")
	EPrecipitationType PrecipitationType = EPrecipitationType::None;

	/** 降水粒子（Niagara；留空时按类型自动使用 UDS 的 Rain / Snow / Dust） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Precipitation")
	TSoftObjectPtr<UNiagaraSystem> PrecipitationEffect;

	/** 降水强度写入的 Niagara 用户参数名（留空则只控制粒子组件开关） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Precipitation")
	FName PrecipitationRateParameter = FName(TEXT("SpawnRate"));

	/** 降水强度倍率写入上述参数（无该参数时被引擎忽略） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Precipitation", meta = (ClampMin = "0"))
	float PrecipitationRate = 1.f;

	/** 环境音（雨声 / 风声；循环播放） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Precipitation")
	TSoftObjectPtr<USoundBase> AmbientSound;

	/** 环境音音量 0~1 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Precipitation", meta = (ClampMin = "0", ClampMax = "1"))
	float AmbientVolume = 0.f;

	// ========== 雷电 ==========
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Lightning")
	bool bLightning = false;

	/** 两次闪电的最小 / 最大间隔（秒，区间内随机） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Lightning", meta = (ClampMin = "0.5"))
	float LightningIntervalMin = 6.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Lightning", meta = (ClampMin = "0.5"))
	float LightningIntervalMax = 18.f;

	/** 雷声（闪电后播放） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Lightning")
	TSoftObjectPtr<USoundBase> ThunderSound;

	// ========== 过渡 ==========
	/** 切到该天气的过渡时长（秒；<=0 使用 AWeatherSystem.DefaultTransitionSeconds） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Transition", meta = (ClampMin = "0"))
	float TransitionSeconds = 0.f;

	/** 按季节取权重（0 = 该季节不出现） */
	float GetSeasonWeight(ESeason InSeason) const
	{
		switch (InSeason)
		{
		case ESeason::Spring: return SpringWeight;
		case ESeason::Summer: return SummerWeight;
		case ESeason::Autumn: return AutumnWeight;
		case ESeason::Winter: return WinterWeight;
		default: return 0.f;
		}
	}

	/** 该预设是否允许在当前季节出现（权重 > 0） */
	bool IsAllowedInSeason(ESeason InSeason) const { return GetSeasonWeight(InSeason) > 0.f; }
};
