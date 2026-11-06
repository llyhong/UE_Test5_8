// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Weather/WeatherTypes.h"
#include "WeatherSystem.generated.h"

class UDataTable;
class UMaterialInterface;
class UMaterialParameterCollection;
class UMaterialInstanceDynamic;
class UNiagaraComponent;
class UNiagaraSystem;
class UAudioComponent;
class USoundBase;
class UPointLightComponent;
class USceneComponent;
class UDirectionalLightComponent;
class USkyLightComponent;
class UExponentialHeightFogComponent;
class UVolumetricCloudComponent;
class UWindDirectionalSourceComponent;
class ADirectionalLight;
class ASkyLight;
class AExponentialHeightFog;
class AVolumetricCloud;
class AWindDirectionalSource;
class UTimeSystem;

/** 天气混合用的标量集合（内部结构，逐帧在旧/新天气之间插值） */
struct FWeatherBlendState
{
	float SunIntensityScale = 1.f;
	FLinearColor SunColorTint = FLinearColor::White;
	float SkyLightIntensityScale = 1.f;
	FLinearColor SkyLightColor = FLinearColor(0.85f, 0.9f, 1.f);
	float FogDensity = 0.02f;
	float FogHeightFalloff = 0.2f;
	float FogMaxOpacity = 1.f;
	FLinearColor FogColor = FLinearColor(0.55f, 0.65f, 0.8f);
	float CloudCoverage = 0.3f;
	float CloudDensity = 0.5f;
	float WindStrength = 0.2f;
	float WindSpeed = 0.6f;
	float PrecipitationRate = 1.f;
	float RainStrength = 0.f;
	float AmbientVolume = 0.f;

	static FWeatherBlendState FromPreset(const FWeatherPreset& Preset);
	FWeatherBlendState LerpTo(const FWeatherBlendState& To, float Alpha) const;
};

/**
 * 天气系统（C++ 版 UDS 控制层）：
 * 读 DataTable 里的天气预设，按当前季节加权每日切换，并平滑驱动场景中的
 * 太阳（叠加到 UTimeSystem 的日变化）/ 天光 / 指数高度雾 / 体积云 / 风 /
 * Niagara 降水 / 环境音 / 雷暴。
 * 天空球的材质与粒子可直接复用 UltraDynamicSky 的资产，本类只做数据驱动。
 */
UCLASS(Blueprintable)
class SHOOT_API AWeatherSystem : public AActor
{
	GENERATED_BODY()

public:
	AWeatherSystem();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 天气切换广播（参数：旧天气 ID、新天气 ID） */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnWeatherChanged, FName, OldWeatherID, FName, NewWeatherID);

	UPROPERTY(BlueprintAssignable, Category = "Shoot|Weather")
	FOnWeatherChanged OnWeatherChanged;

	// ========== 配置 ==========
	/** 天气预设表（行结构 FWeatherPreset，RowName 即天气 ID） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Weather")
	TObjectPtr<UDataTable> WeatherTable;

	/** 起始天气 ID（留空则开局自动按季节随机） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Weather")
	FName StartWeatherID;

	/** BeginPlay 时若 StartWeatherID 为空，是否自动选一个天气 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Weather")
	bool bAutoSelectOnBeginPlay = true;

	/** 是否每天按季节权重自动切换天气 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Weather")
	bool bAutoSelectDaily = true;

	/** 预设未填 TransitionSeconds 时的默认过渡时长（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Weather", meta = (ClampMin = "0"))
	float DefaultTransitionSeconds = 12.f;

	/** 随机种子（0 = 每次运行随机） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Weather")
	int32 RandomSeed = 0;

	/** 云参数集合（默认尝试加载 UDS 的 UDS_VolumetricClouds_MPC；留空则只驱动云材质） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Weather|Clouds")
	TObjectPtr<UMaterialParameterCollection> CloudParameterCollection;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Weather")
	bool bShowDebugInfo = false;

	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Shoot|Weather|Scene")
	TObjectPtr<AActor> StylizedSkyActor;

	// ========== 场景绑定（留空时 BeginPlay 自动按类型查找） ==========
	UPROPERTY(EditAnywhere, Category = "Shoot|Weather|Scene")
	TObjectPtr<ASkyLight> SkyLightActor;

	UPROPERTY(EditAnywhere, Category = "Shoot|Weather|Scene")
	TObjectPtr<AExponentialHeightFog> HeightFogActor;

	UPROPERTY(EditAnywhere, Category = "Shoot|Weather|Scene")
	TObjectPtr<AVolumetricCloud> VolumetricCloudActor;

	UPROPERTY(EditAnywhere, Category = "Shoot|Weather|Scene")
	TObjectPtr<AWindDirectionalSource> WindSourceActor;

	/** 降水粒子跟随玩家时的高度偏移 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Weather|Precipitation")
	float PrecipitationFollowHeight = 400.f;

	/** 雷电闪光强度 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Weather|Lightning", meta = (ClampMin = "0"))
	float LightningFlashIntensity = 20000.f;

	/** 单次闪电衰减时长（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Weather|Lightning", meta = (ClampMin = "0.01"))
	float LightningFlashDuration = 0.25f;

	/** 闪电光源相对玩家上方的高度 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Weather|Lightning")
	float LightningLightHeight = 1500.f;

	// ========== 运行时查询（供 UI / 其他系统） ==========
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Weather")
	FName GetCurrentWeatherID() const { return CurrentWeatherID; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Weather")
	bool IsWeatherTransitioning() const { return bTransitioning; }

	/** 取当前天气预设（表里找不到返回 false） */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Weather")
	bool GetCurrentWeatherInfo(FWeatherPreset& OutPreset) const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Weather")
	FText GetCurrentWeatherName() const { return CurrentPreset.DisplayName; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Weather")
	FText GetCurrentWeatherDescription() const { return CurrentPreset.Description; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Weather")
	TSoftObjectPtr<UTexture2D> GetCurrentWeatherIcon() const { return CurrentPreset.Icon; }

	// ========== 控制 ==========
	/** 切到指定天气（bInstant=true 跳过过渡）；ID 不存在返回 false */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Weather")
	bool SetWeatherByID(FName InWeatherID, bool bInstant = false);

	/** 立即按当前季节权重抽一个天气并切换 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Weather")
	bool SelectDailyWeather();

protected:
	/** 懒初始化：时间系统绑定 / 场景组件查找 */
	void EnsureReady();
	void RefreshTimeSystem();
	void InitSceneBindings();

	void StartTransitionTo(const FWeatherPreset& Preset, FName PresetID, float InTransitionSeconds);
	void TickTransition(float DeltaSeconds);
	void ApplyBlend(const FWeatherBlendState& Blend);
	void ApplyStaticFeatures(const FWeatherPreset& Preset);
	void UpdatePrecipitation();
	void UpdateLightning(float DeltaSeconds);

	bool PickWeightedPresetForSeason(FName& OutID, FWeatherPreset& OutPreset);
	ESeason GetCurrentSeason() const;
	UTimeSystem* GetTimeSystem() const;
	const FWeatherPreset* FindPreset(FName RowName) const;

	UFUNCTION()
	void HandleDayChanged(int32 InYear, int32 InMonth, int32 InDay);

	UPROPERTY()
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY()
	TObjectPtr<UNiagaraComponent> PrecipitationComponent;

	UPROPERTY()
	TObjectPtr<UAudioComponent> AmbientAudioComponent;

	UPROPERTY()
	TObjectPtr<UAudioComponent> ThunderAudioComponent;

	UPROPERTY()
	TObjectPtr<UPointLightComponent> LightningLight;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> CloudMaterialInstance;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> ActiveCloudBaseMaterial;

	UPROPERTY()
	TObjectPtr<USoundBase> ActiveAmbientSound;

	UPROPERTY()
	TObjectPtr<USoundBase> ActiveThunderSound;

	UPROPERTY()
	TObjectPtr<UTimeSystem> BoundTimeSystem;

	UPROPERTY()
	TObjectPtr<USkyLightComponent> SkyLightComp;

	UPROPERTY()
	TObjectPtr<UExponentialHeightFogComponent> FogComp;

	UPROPERTY()
	TObjectPtr<UVolumetricCloudComponent> CloudComp;

	UPROPERTY()
	TObjectPtr<UWindDirectionalSourceComponent> WindComp;

	/** 天光基准强度（BeginPlay 时从场景 Skylight 读取，乘预设倍率） */
	float BaseSkyLightIntensity = 1.f;

	FName CurrentWeatherID;
	FWeatherPreset CurrentPreset;

	bool bTransitioning = false;
	float TransitionElapsed = 0.f;
	float TransitionDuration = 0.f;
	FWeatherBlendState BlendFrom;
	FWeatherBlendState BlendTo;
	FWeatherBlendState BlendCurrent;

	/** 当前生效的云参数名 / 降水参数名（来自进入过渡时的预设） */
	FName ActiveCloudCoverageParameter;
	FName ActiveCloudDensityParameter;
	FName ActivePrecipitationRateParameter;

	EPrecipitationType ActivePrecipitationType = EPrecipitationType::None;
	TObjectPtr<UNiagaraSystem> ActivePrecipitationEffect;

	bool bSceneInitialized = false;
	FRandomStream Rand;

	float LightningCountdown = 0.f;
	float FlashRemaining = 0.f;
};
