#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Weather/WeatherSystem.h"
#include "StylizedSkyBridge.generated.h"

class UDirectionalLightComponent;
class USkyLightComponent;
class UExponentialHeightFogComponent;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UMaterialParameterCollection;
class UStaticMesh;
class UTimeSystem;

UCLASS(ClassGroup = (Shoot), meta = (BlueprintSpawnableComponent))
class SHOOT_API UStylizedSkyBridge : public UActorComponent
{
	GENERATED_BODY()

public:
	UStylizedSkyBridge();
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	void SetWeatherBlend(const FWeatherBlendState& InBlend);

protected:
	UPROPERTY(EditAnywhere, Category = "Shoot|StylizedSky")
	TObjectPtr<UMaterialParameterCollection> EnvironmentParameters;

	/** 营业 / 收尾结束后天气光照恢复的时间常数（秒）；0 为立即恢复，雨效不使用此混合。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|StylizedSky|Lighting", meta = (ClampMin = "0"))
	float LightingRecoverySeconds = 12.f;

	// ------------------- 程序化雨幕 -------------------
	// UDS 的 NS_RainWithSplash 在本工程 5.8 下不产生可见粒子（CPU Sim + 烘焙参数，激活后渲染为空），
	// 雨天改用 InstancedStaticMesh 雨丝：由天气 RainStrength 驱动，保证任何视角都能看到雨。

	/** 雨丝使用的圆柱网格（引擎基础网格，高 100cm、直径 100cm） */
	UPROPERTY(EditAnywhere, Category = "Shoot|StylizedSky|Rain")
	TObjectPtr<UStaticMesh> RainStreakMesh;

	/** 雨丝材质（无光照半透明，需含 Opacity 参数） */
	UPROPERTY(EditAnywhere, Category = "Shoot|StylizedSky|Rain")
	TObjectPtr<UMaterialInterface> RainStreakMaterial;

	/** 同时存在的雨丝数量 */
	UPROPERTY(EditAnywhere, Category = "Shoot|StylizedSky|Rain", meta = (ClampMin = "0", ClampMax = "20000"))
	int32 RainStreakCount = 6000;

	/** 雨丝分布半径（cm） */
	UPROPERTY(EditAnywhere, Category = "Shoot|StylizedSky|Rain", meta = (ClampMin = "100.0"))
	float RainRadius = 1200.f;

	/** 雨丝分布高度（cm） */
	UPROPERTY(EditAnywhere, Category = "Shoot|StylizedSky|Rain", meta = (ClampMin = "200.0"))
	float RainHeight = 1800.f;

	/** 雨丝下落速度（cm/s） */
	UPROPERTY(EditAnywhere, Category = "Shoot|StylizedSky|Rain", meta = (ClampMin = "0.0"))
	float RainFallSpeed = 2600.f;

	/** 雨丝水平风速（cm/s），决定雨丝倾斜方向 */
	UPROPERTY(EditAnywhere, Category = "Shoot|StylizedSky|Rain")
	float RainWindSpeed = 350.f;

	/** 单根雨丝长度（cm） */
	UPROPERTY(EditAnywhere, Category = "Shoot|StylizedSky|Rain", meta = (ClampMin = "1.0"))
	float RainStreakLength = 42.f;

	/** 单根雨丝直径（cm） */
	UPROPERTY(EditAnywhere, Category = "Shoot|StylizedSky|Rain", meta = (ClampMin = "0.1"))
	float RainStreakThickness = 1.2f;

	/** 雨幕中心相对玩家位置的高度偏移（cm） */
	UPROPERTY(EditAnywhere, Category = "Shoot|StylizedSky|Rain")
	float RainCenterHeight = 700.f;

	/** 满强降雨时雨丝的材质 Opacity */
	UPROPERTY(EditAnywhere, Category = "Shoot|StylizedSky|Rain", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RainOpacity = 0.42f;

	/** 距相机多近开始缩小雨丝（cm）：避免贴脸的雨丝变成巨大白条 */
	UPROPERTY(EditAnywhere, Category = "Shoot|StylizedSky|Rain", meta = (ClampMin = "1.0"))
	float RainNearFadeDistance = 900.f;

	/** 是否剔除落在 RainExclusionVolume 内的雨丝：用于就餐区、煮饭区等不应见雨的室内区域 */
	UPROPERTY(EditAnywhere, Category = "Shoot|StylizedSky|Rain")
	bool bRespectRainExclusionVolumes = true;

private:
	bool InitializeVisuals();
	void UpdateVisuals(const UTimeSystem& Time, float DeltaTime);
	void UpdateLightingWeatherBlend(const UTimeSystem& Time, float DeltaTime);
	void EnsureRainStreaks();
	void UpdateRainStreaks(float DeltaTime, float Rain);

	/** 收集场景内所有启用的 RainExclusionVolume 包围盒（世界空间），供雨丝逐实例剔除 */
	void RefreshRainExclusionBoxes();

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> RainStreaks;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> RainStreakMID;

	/** 每根雨丝相对于雨幕中心的偏移（局部空间，Z 范围 [0, RainHeight]） */
	TArray<FVector> RainOffsets;
	TArray<float> RainFallSpeeds;
	TArray<FTransform> RainTransforms;
	bool bRainStreaksSeeded = false;

	/** 排除雨效的区域（世界空间 XY 包围盒）及刷新计时：区域可在关卡里增删，无需改代码 */
	TArray<FBox> RainExclusionBoxes;
	float RainExclusionRefreshTimer = 0.f;
	static constexpr float RainExclusionRefreshInterval = 0.5f;

	UPROPERTY(Transient)
	TObjectPtr<UDirectionalLightComponent> Sun;

	UPROPERTY(Transient)
	TObjectPtr<USkyLightComponent> SkyLight;

	UPROPERTY(Transient)
	TObjectPtr<UExponentialHeightFogComponent> Fog;

	UPROPERTY(Transient)
	TObjectPtr<UFunction> SetTimeFunction;

	// 真实天气继续驱动雨丝 / 湿度 / 积水；光照只使用独立保存的混合。
	FWeatherBlendState WeatherBlend;
	FWeatherBlendState LightingWeatherBlend;
	bool bLightingWeatherBlendInitialized = false;
	double VisualSkyTime = 0.0;
	bool bInitialized = false;
	bool bVisualsReady = false;
};
