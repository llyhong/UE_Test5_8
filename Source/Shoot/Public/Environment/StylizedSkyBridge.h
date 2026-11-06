#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Weather/WeatherSystem.h"
#include "StylizedSkyBridge.generated.h"

class UDirectionalLightComponent;
class USkyLightComponent;
class UExponentialHeightFogComponent;
class UMaterialParameterCollection;
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

private:
	bool InitializeVisuals();
	void UpdateVisuals(const UTimeSystem& Time);

	UPROPERTY(Transient)
	TObjectPtr<UDirectionalLightComponent> Sun;

	UPROPERTY(Transient)
	TObjectPtr<USkyLightComponent> SkyLight;

	UPROPERTY(Transient)
	TObjectPtr<UExponentialHeightFogComponent> Fog;

	UPROPERTY(Transient)
	TObjectPtr<UFunction> SetTimeFunction;

	FWeatherBlendState WeatherBlend;
	double VisualSkyTime = 0.0;
	bool bInitialized = false;
	bool bVisualsReady = false;
};
