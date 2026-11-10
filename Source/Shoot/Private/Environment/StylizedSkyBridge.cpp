#include "Environment/StylizedSkyBridge.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/TimelineComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Environment/RainExclusionVolume.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Game/BaseGameInstance.h"
#include "Game/BusinessTime.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Math/RandomStream.h"
#include "Math/RotationMatrix.h"
#include "NiagaraComponent.h"
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
	static ConstructorHelpers::FObjectFinder<UStaticMesh> StreakMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	RainStreakMesh = StreakMesh.Object;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> StreakMaterial(
		TEXT("/Game/_LOCAL/VFX/M_RainStreak.M_RainStreak"));
	RainStreakMaterial = StreakMaterial.Object;
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
	SunBaseIntensity = Sun->Intensity;
	SunBaseColor = FLinearColor(Sun->LightColor);
	SkyLightBaseIntensity = SkyLight->Intensity;
	SkyLightBaseColor = FLinearColor(SkyLight->LightColor);
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
				UpdateVisuals(*GI->businessSimulation->TimeSystem, DeltaTime);
			}
		}
	}
}

void UStylizedSkyBridge::UpdateVisuals(const UTimeSystem& Time, float DeltaTime)
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

	// UDS 的 NS_RainWithSplash 在本工程 5.8 下激活后不产生可见粒子（多种强度 / 重置 / 缩放均验证无效），
	// 这里关闭它的渲染，雨效交给下方跟随玩家的程序化雨丝，避免出现"有雨声有湿度但看不到雨"。
	TInlineComponentArray<UNiagaraComponent*> Effects(Owner);
	for (UNiagaraComponent* Effect : Effects)
	{
		if (Effect->GetFName() != TEXT("FX_Rain"))
		{
			continue;
		}
		Effect->SetVisibility(false);
		if (Effect->IsActive())
		{
			Effect->Deactivate();
		}
	}

	UpdateRainStreaks(DeltaTime, Rain);

	// 始终以初始化时的基础光照为基准：直接乘当前值会逐帧叠加衰减，几十秒后太阳/天光归零、场景全黑。
	Sun->SetIntensity(SunBaseIntensity * WeatherBlend.SunIntensityScale);
	Sun->SetLightColor(SunBaseColor * WeatherBlend.SunColorTint);
	SkyLight->SetIntensity(SkyLightBaseIntensity * WeatherBlend.SkyLightIntensityScale);
	SkyLight->SetLightColor(SkyLightBaseColor * WeatherBlend.SkyLightColor);
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

void UStylizedSkyBridge::EnsureRainStreaks()
{
	if (RainStreaks)
	{
		return;
	}
	AActor* Owner = GetOwner();
	// 材质/网格可能在本类 CDO 构造之后才创建（热更新、首次生成资产），运行时再兜底加载一次。
	if (!RainStreakMesh)
	{
		RainStreakMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	}
	if (!RainStreakMaterial)
	{
		RainStreakMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/_LOCAL/VFX/M_RainStreak.M_RainStreak"));
	}
	if (!Owner || !RainStreakMesh || !RainStreakMaterial)
	{
		UE_LOG(LogTemp, Warning, TEXT("[StylizedSky] 缺少 RainStreakMesh / RainStreakMaterial，无法生成程序化雨丝"));
		return;
	}

	RainStreaks = NewObject<UInstancedStaticMeshComponent>(Owner, TEXT("FX_ProceduralRain"));
	RainStreaks->SetMobility(EComponentMobility::Movable);
	RainStreaks->SetStaticMesh(RainStreakMesh);
	RainStreaks->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RainStreaks->SetGenerateOverlapEvents(false);
	RainStreaks->SetCastShadow(false);
	RainStreaks->bCastDynamicShadow = false;
	RainStreaks->bCastStaticShadow = false;
	RainStreaks->bCastVolumetricTranslucentShadow = false;
	RainStreaks->bAffectDynamicIndirectLighting = false;
	RainStreaks->SetReceivesDecals(false);
	RainStreaks->SetCanEverAffectNavigation(false);
	// 不挂到天空 Actor 下：雨幕原点每个 Tick 直接设为玩家位置，避免受天空 Actor 变换影响。
	RainStreaks->RegisterComponent();
	RainStreaks->SetVisibility(false);

	RainStreakMID = UMaterialInstanceDynamic::Create(RainStreakMaterial, this);
	if (RainStreakMID)
	{
		RainStreaks->SetMaterial(0, RainStreakMID);
	}

	bRainStreaksSeeded = false;
}

void UStylizedSkyBridge::UpdateRainStreaks(float DeltaTime, float Rain)
{
	if (Rain <= KINDA_SMALL_NUMBER)
	{
		if (RainStreaks && RainStreaks->IsVisible())
		{
			RainStreaks->SetVisibility(false);
		}
		return;
	}

	EnsureRainStreaks();
	if (!RainStreaks)
	{
		return;
	}

	if (!bRainStreaksSeeded || RainOffsets.Num() != RainStreakCount)
	{
		FRandomStream Random(1337);
		RainOffsets.SetNum(RainStreakCount);
		RainFallSpeeds.SetNum(RainStreakCount);
		RainTransforms.SetNum(RainStreakCount);
		for (int32 Index = 0; Index < RainStreakCount; ++Index)
		{
			const float Angle = Random.FRandRange(0.f, 2.f * UE_PI);
			const float Radius = FMath::Sqrt(Random.FRand()) * RainRadius;
			RainOffsets[Index] = FVector(
				FMath::Cos(Angle) * Radius,
				FMath::Sin(Angle) * Radius,
				Random.FRand() * RainHeight);
			RainFallSpeeds[Index] = RainFallSpeed * Random.FRandRange(0.85f, 1.15f);
			RainTransforms[Index] = FTransform::Identity;
		}
		RainStreaks->ClearInstances();
		RainStreaks->AddInstances(RainTransforms, false, false, false);
		RainStreaks->SetCastShadow(false);
		bRainStreaksSeeded = true;
	}

	// 雨幕整体跟随玩家：组件原点即雨幕中心；玩家走远后雨也不会留在原地。
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC)
	{
		return;
	}
	FVector CameraLocation;
	FRotator CameraRotation;
	PC->GetPlayerViewPoint(CameraLocation, CameraRotation);
	FVector Center = CameraLocation;
	if (const APawn* Pawn = PC->GetPawn())
	{
		Center = Pawn->GetActorLocation();
	}
	Center.Z += RainCenterHeight;
	RainStreaks->SetWorldLocation(Center);
	RainStreaks->SetWorldRotation(FRotator::ZeroRotator);

	const float ClampedDeltaTime = FMath::Min(DeltaTime, 0.1f);
	const FVector Velocity(RainWindSpeed, 0.f, -RainFallSpeed);
	const FQuat StreakRotation = FRotationMatrix::MakeFromZ(Velocity.GetSafeNormal()).ToQuat();
	const FVector StreakScale(RainStreakThickness / 100.f, RainStreakThickness / 100.f, RainStreakLength / 100.f);
	const float HalfHeight = RainHeight * 0.5f;

	// 排除区域允许在关卡里随时增删：按固定间隔重新收集，避免每帧遍历场景
	if (bRespectRainExclusionVolumes)
	{
		RainExclusionRefreshTimer -= ClampedDeltaTime;
		if (RainExclusionRefreshTimer <= 0.f)
		{
			RainExclusionRefreshTimer = RainExclusionRefreshInterval;
			RefreshRainExclusionBoxes();
		}
	}

	for (int32 Index = 0; Index < RainOffsets.Num(); ++Index)
	{
		FVector& Offset = RainOffsets[Index];
		Offset.Z -= RainFallSpeeds[Index] * ClampedDeltaTime;
		if (Offset.Z < 0.f)
		{
			// 落出底部后从顶部重新出现并重新分布水平位置，否则雨丝会因下落速度差逐渐聚成一圈。
			Offset.Z += RainHeight;
			const float Angle = FMath::FRandRange(0.f, 2.f * UE_PI);
			const float Radius = FMath::Sqrt(FMath::FRand()) * RainRadius;
			Offset.X = FMath::Cos(Angle) * Radius;
			Offset.Y = FMath::Sin(Angle) * Radius;
		}

		FVector LocalOffset = Offset;
		LocalOffset.Z -= HalfHeight;

		// 落在排除区域（就餐区 / 煮饭区等）内的雨丝直接缩到不可见。
		// 雨丝垂直下落，按 XY 判断即可；区域外的雨照常显示。
		bool bExcluded = false;
		if (RainExclusionBoxes.Num() > 0)
		{
			const FVector WorldOffset = Center + LocalOffset;
			for (const FBox& Box : RainExclusionBoxes)
			{
				if (Box.IsInsideXY(WorldOffset))
				{
					bExcluded = true;
					break;
				}
			}
		}
		if (bExcluded)
		{
			RainTransforms[Index] = FTransform(StreakRotation, LocalOffset, FVector(0.001f));
			continue;
		}

		// 近处雨丝按距离缩小，避免贴到相机的那几根变成巨大的白条。
		const float Distance = (Center + LocalOffset - CameraLocation).Size();
		const float DistanceScale = FMath::Clamp(FMath::Sqrt(Distance / RainNearFadeDistance), 0.2f, 1.f);
		RainTransforms[Index] = FTransform(StreakRotation, LocalOffset, StreakScale * DistanceScale);
	}

	RainStreaks->BatchUpdateInstancesTransforms(0, RainTransforms, false, true, false);

	if (RainStreakMID)
	{
		// 雨越大雨丝越亮：Transition/LightRain 时仍保留可见度，满强度时达到 RainOpacity。
		const float Strength = FMath::Clamp(Rain, 0.f, 1.f);
		RainStreakMID->SetScalarParameterValue(TEXT("Opacity"), RainOpacity * FMath::Lerp(0.6f, 1.f, Strength));
	}

	if (!RainStreaks->IsVisible())
	{
		RainStreaks->SetVisibility(true);
	}
}

void UStylizedSkyBridge::RefreshRainExclusionBoxes()
{
	RainExclusionBoxes.Reset();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 区域完全由关卡里摆放的 RainExclusionVolume 决定：加/删/改体积即可调整禁雨范围
	for (TActorIterator<ARainExclusionVolume> It(World); It; ++It)
	{
		const ARainExclusionVolume* Volume = *It;
		if (!Volume || !Volume->bExcludeRain)
		{
			continue;
		}

		const FBox Box = Volume->GetBoundsBox();
		if (Box.IsValid)
		{
			RainExclusionBoxes.Add(Box);
		}
	}
}
