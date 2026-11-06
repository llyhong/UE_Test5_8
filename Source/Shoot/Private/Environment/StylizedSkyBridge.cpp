#include "Environment/StylizedSkyBridge.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/TimelineComponent.h"
#include "Engine/World.h"
#include "Game/BaseGameInstance.h"
#include "Game/BusinessTime.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace
{
	double ReadNumber(const UObject* Object, FName Name)
	{
		const FNumericProperty* Property = FindFProperty<FNumericProperty>(Object->GetClass(), Name);
		return Property->GetFloatingPointPropertyValue(Property->ContainerPtrToValuePtr<void>(Object));
	}

	void WriteNumber(UObject* Object, FName Name, double Value)
	{
		FNumericProperty* Property = FindFProperty<FNumericProperty>(Object->GetClass(), Name);
		Property->SetFloatingPointPropertyValue(Property->ContainerPtrToValuePtr<void>(Object), Value);
	}
}

UStylizedSkyBridge::UStylizedSkyBridge()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	static ConstructorHelpers::FObjectFinder<UMaterialParameterCollection> Parameters(
		TEXT("/Game/SoStylized/Environment/MPC_GlobalEnvironment.MPC_GlobalEnvironment"));
	EnvironmentParameters = Parameters.Object;
}

bool UStylizedSkyBridge::InitializeVisuals()
{
	AActor* Owner = GetOwner();
	Sun = Owner->FindComponentByClass<UDirectionalLightComponent>();
	SkyLight = Owner->FindComponentByClass<USkyLightComponent>();
	Fog = Owner->FindComponentByClass<UExponentialHeightFogComponent>();
	SetTimeFunction = Owner->FindFunction(TEXT("Set Time"));
	const TCHAR* RequiredProperties[] = {
		TEXT("Day Length"), TEXT("Night Length"), TEXT("Current Time of Day"), TEXT("Overcast Amount"),
		TEXT("Rain Strength"), TEXT("Rain Wetness"), TEXT("Rain Puddles"), TEXT("Rain Max Puddle Amount")
	};
	for (const TCHAR* Name : RequiredProperties)
	{
		const FNumericProperty* Property = FindFProperty<FNumericProperty>(Owner->GetClass(), Name);
		if (!Property || !Property->IsFloatingPoint())
		{
			UE_LOG(LogTemp, Error, TEXT("[StylizedSky] %s 缺少数值属性 %s"), *Owner->GetName(), Name);
			return false;
		}
	}
	const FNumericProperty* TimeParameter = SetTimeFunction
		? FindFProperty<FNumericProperty>(SetTimeFunction, TEXT("New Time")) : nullptr;
	if (!Sun || !SkyLight || !Fog || !TimeParameter || !TimeParameter->IsFloatingPoint())
	{
		UE_LOG(LogTemp, Error, TEXT("[StylizedSky] %s 缺少天空组件或 Set Time(New Time) 接口"), *Owner->GetName());
		return false;
	}

	VisualSkyTime = ReadNumber(Owner, TEXT("Current Time of Day"));
	// 停止原蓝图的自动状态机，但 Set Time 仍可主动采样昼夜 Timeline 的视觉曲线。
	Owner->SetActorTickEnabled(false);
	TInlineComponentArray<UTimelineComponent*> Timelines(Owner);
	for (UTimelineComponent* Timeline : Timelines)
	{
		Timeline->Stop();
	}
	return true;
}

void UStylizedSkyBridge::SetWeatherBlend(const FWeatherBlendState& InBlend)
{
	WeatherBlend = InBlend;
}

void UStylizedSkyBridge::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bInitialized)
	{
		bVisualsReady = InitializeVisuals();
		bInitialized = true;
	}
	if (bVisualsReady)
	{
		if (const UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetWorld()->GetGameInstance()))
		{
			if (GI->businessSimulation && GI->businessSimulation->TimeSystem)
			{
				UpdateVisuals(*GI->businessSimulation->TimeSystem);
			}
		}
	}
}

void UStylizedSkyBridge::UpdateVisuals(const UTimeSystem& Time)
{
	AActor* Owner = GetOwner();
	const double DayLength = ReadNumber(Owner, TEXT("Day Length"));
	const double NightLength = ReadNumber(Owner, TEXT("Night Length"));
	const double DayHours = Time.SunsetHour - Time.SunriseHour;
	if (DayLength <= 0.0 || NightLength <= 0.0 || DayHours <= 0.0 || DayHours >= 24.0)
	{
		return;
	}
	const double HourSinceSunrise = FMath::Fmod(Time.GetCurrentHour() - Time.SunriseHour + 24.0, 24.0);
	if (Time.bDriveSun)
	{
		VisualSkyTime = HourSinceSunrise < DayHours
			? HourSinceSunrise / DayHours * DayLength
			: DayLength + (HourSinceSunrise - DayHours) / (24.0 - DayHours) * NightLength;
	}
	const float Rain = WeatherBlend.RainStrength;
	WriteNumber(Owner, TEXT("Overcast Amount"), WeatherBlend.CloudCoverage);
	WriteNumber(Owner, TEXT("Rain Strength"), Rain);
	WriteNumber(Owner, TEXT("Rain Wetness"), Rain);
	WriteNumber(Owner, TEXT("Rain Puddles"), Rain * ReadNumber(Owner, TEXT("Rain Max Puddle Amount")));

	FStructOnScope Parameters(SetTimeFunction);
	FNumericProperty* TimeParameter = FindFProperty<FNumericProperty>(SetTimeFunction, TEXT("New Time"));
	TimeParameter->SetFloatingPointPropertyValue(TimeParameter->ContainerPtrToValuePtr<void>(Parameters.GetStructMemory()), VisualSkyTime);
	Owner->ProcessEvent(SetTimeFunction, Parameters.GetStructMemory());
	// Set Time 的 Day/Night Event 会重新启动 Timeline，采样后必须再次停止自动走时。
	TInlineComponentArray<UTimelineComponent*> Timelines(Owner);
	for (UTimelineComponent* Timeline : Timelines)
	{
		Timeline->Stop();
	}

	Sun->SetIntensity(Sun->Intensity * WeatherBlend.SunIntensityScale);
	Sun->SetLightColor(FLinearColor(Sun->LightColor) * WeatherBlend.SunColorTint);
	SkyLight->SetIntensity(SkyLight->Intensity * WeatherBlend.SkyLightIntensityScale);
	SkyLight->SetLightColor(FLinearColor(SkyLight->LightColor) * WeatherBlend.SkyLightColor);
	Fog->SetFogDensity(WeatherBlend.FogDensity);
	Fog->SetFogHeightFalloff(WeatherBlend.FogHeightFalloff);
	Fog->SetFogMaxOpacity(WeatherBlend.FogMaxOpacity);
	Fog->SetFogInscatteringColor(WeatherBlend.FogColor);

	if (EnvironmentParameters)
	{
		UKismetMaterialLibrary::SetScalarParameterValue(this, EnvironmentParameters, TEXT("Overcast"), WeatherBlend.CloudCoverage);
		UKismetMaterialLibrary::SetScalarParameterValue(this, EnvironmentParameters, TEXT("Rain Strength"), Rain);
		UKismetMaterialLibrary::SetScalarParameterValue(this, EnvironmentParameters, TEXT("Rain Wetness"), Rain);
		UKismetMaterialLibrary::SetScalarParameterValue(this, EnvironmentParameters, TEXT("Rain Puddles"), ReadNumber(Owner, TEXT("Rain Puddles")));
		UKismetMaterialLibrary::SetScalarParameterValue(this, EnvironmentParameters, TEXT("Weather Wind Multiplier"), WeatherBlend.WindStrength);
	}
}
