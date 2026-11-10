// Fill out your copyright notice in the Description page of Project Settings.

#include "Weather/WeatherSystem.h"

#include "Environment/StylizedSkyBridge.h"
#include "Components/AudioComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "NiagaraComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Components/WindDirectionalSourceComponent.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "Engine/WindDirectionalSource.h"
#include "EngineUtils.h"
#include "Game/BaseGameInstance.h"
#include "Game/BusinessTime.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"
#include "NiagaraSystem.h"
#include "Sound/SoundBase.h"
#include "UObject/Class.h"

namespace
{
	bool HasScalarParameter(const UMaterialParameterCollection* Collection, FName ParameterName)
	{
		if (!Collection || ParameterName.IsNone())
		{
			return false;
		}
		for (const FCollectionScalarParameter& Param : Collection->ScalarParameters)
		{
			if (Param.ParameterName == ParameterName)
			{
				return true;
			}
		}
		return false;
	}
}

FWeatherBlendState FWeatherBlendState::FromPreset(const FWeatherPreset& Preset)
{
	FWeatherBlendState State;
	State.SunIntensityScale = Preset.SunIntensityScale;
	State.SunColorTint = Preset.SunColorTint;
	State.SkyLightIntensityScale = Preset.SkyLightIntensityScale;
	State.SkyLightColor = Preset.SkyLightColor;
	State.FogDensity = Preset.FogDensity;
	State.FogHeightFalloff = Preset.FogHeightFalloff;
	State.FogMaxOpacity = Preset.FogMaxOpacity;
	State.FogColor = Preset.FogColor;
	State.CloudCoverage = Preset.CloudCoverage;
	State.CloudDensity = Preset.CloudDensity;
	State.WindStrength = Preset.WindStrength;
	State.WindSpeed = Preset.WindSpeed;
	State.PrecipitationRate = Preset.PrecipitationType == EPrecipitationType::None ? 0.f : Preset.PrecipitationRate;
	State.RainStrength = Preset.PrecipitationType == EPrecipitationType::Rain
		? FMath::Clamp(Preset.PrecipitationRate, 0.f, 1.f) : 0.f;
	State.AmbientVolume = Preset.AmbientVolume;
	return State;
}

FWeatherBlendState FWeatherBlendState::LerpTo(const FWeatherBlendState& To, float Alpha) const
{
	FWeatherBlendState Result;
	Result.SunIntensityScale = FMath::Lerp(SunIntensityScale, To.SunIntensityScale, Alpha);
	Result.SunColorTint = FLinearColor::LerpUsingHSV(SunColorTint, To.SunColorTint, Alpha);
	Result.SkyLightIntensityScale = FMath::Lerp(SkyLightIntensityScale, To.SkyLightIntensityScale, Alpha);
	Result.SkyLightColor = FLinearColor::LerpUsingHSV(SkyLightColor, To.SkyLightColor, Alpha);
	Result.FogDensity = FMath::Lerp(FogDensity, To.FogDensity, Alpha);
	Result.FogHeightFalloff = FMath::Lerp(FogHeightFalloff, To.FogHeightFalloff, Alpha);
	Result.FogMaxOpacity = FMath::Lerp(FogMaxOpacity, To.FogMaxOpacity, Alpha);
	Result.FogColor = FLinearColor::LerpUsingHSV(FogColor, To.FogColor, Alpha);
	Result.CloudCoverage = FMath::Lerp(CloudCoverage, To.CloudCoverage, Alpha);
	Result.CloudDensity = FMath::Lerp(CloudDensity, To.CloudDensity, Alpha);
	Result.WindStrength = FMath::Lerp(WindStrength, To.WindStrength, Alpha);
	Result.WindSpeed = FMath::Lerp(WindSpeed, To.WindSpeed, Alpha);
	Result.PrecipitationRate = FMath::Lerp(PrecipitationRate, To.PrecipitationRate, Alpha);
	Result.RainStrength = FMath::Lerp(RainStrength, To.RainStrength, Alpha);
	Result.AmbientVolume = FMath::Lerp(AmbientVolume, To.AmbientVolume, Alpha);
	return Result;
}

AWeatherSystem::AWeatherSystem()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	// 降水粒子：跟随玩家（世界坐标由 Tick 更新），默认不激活
	PrecipitationComponent = CreateDefaultSubobject<UNiagaraComponent>(TEXT("Precipitation"));
	PrecipitationComponent->SetupAttachment(SceneRoot);
	PrecipitationComponent->SetAutoActivate(false);
	PrecipitationComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// 环境音 / 雷声：2D 循环
	AmbientAudioComponent = CreateDefaultSubobject<UAudioComponent>(TEXT("AmbientAudio"));
	AmbientAudioComponent->SetupAttachment(SceneRoot);
	AmbientAudioComponent->SetAutoActivate(false);

	ThunderAudioComponent = CreateDefaultSubobject<UAudioComponent>(TEXT("ThunderAudio"));
	ThunderAudioComponent->SetupAttachment(SceneRoot);
	ThunderAudioComponent->SetAutoActivate(false);

	// 雷电闪光：高强度点光源，平时关闭
	LightningLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("LightningLight"));
	LightningLight->SetupAttachment(SceneRoot);
	LightningLight->Mobility = EComponentMobility::Movable;
	LightningLight->SetIntensity(0.f);
	LightningLight->SetAttenuationRadius(80000.f);
	LightningLight->SetCastShadows(false);
	LightningLight->SetVisibility(false);
}

void AWeatherSystem::BeginPlay()
{
	Super::BeginPlay();

	Rand.Initialize(RandomSeed != 0 ? RandomSeed : FMath::Rand());

	EnsureReady();

	if (!WeatherTable)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Weather] 未指定 WeatherTable（天气预设表），天气系统不会切换天气"));
		return;
	}

	FName StartID = StartWeatherID;
	if (StartID.IsNone() || !FindPreset(StartID))
	{
		FWeatherPreset PickedPreset;
		if (bAutoSelectOnBeginPlay && PickWeightedPresetForSeason(StartID, PickedPreset))
		{
			SetWeatherByID(StartID, true);
		}
	}
	else
	{
		SetWeatherByID(StartID, true);
	}
}

void AWeatherSystem::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (BoundTimeSystem)
	{
		BoundTimeSystem->OnDayChanged.RemoveDynamic(this, &AWeatherSystem::HandleDayChanged);
		if (BoundTimeSystem->StylizedSkyActor == StylizedSkyActor)
		{
			BoundTimeSystem->StylizedSkyActor = nullptr;
		}
		BoundTimeSystem = nullptr;
	}

	if (PrecipitationComponent)
	{
		PrecipitationComponent->Deactivate();
	}
	if (AmbientAudioComponent)
	{
		AmbientAudioComponent->Stop();
	}
	if (ThunderAudioComponent)
	{
		ThunderAudioComponent->Stop();
	}

	Super::EndPlay(EndPlayReason);
}

void AWeatherSystem::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	EnsureReady();

	if (bTransitioning)
	{
		TickTransition(DeltaSeconds);
	}

	if (StylizedSkyActor)
	{
		if (UStylizedSkyBridge* Bridge = StylizedSkyActor->FindComponentByClass<UStylizedSkyBridge>())
		{
			Bridge->SetWeatherBlend(BlendCurrent);
		}
	}

	// 太阳修正每帧写入时间系统：UpdateSun 不依赖天气系统的 Tick 顺序，且时间系统晚于天气系统创建时也能补齐
	if (UTimeSystem* Time = GetTimeSystem())
	{
		Time->SetWeatherSunModifiers(BlendCurrent.SunIntensityScale, BlendCurrent.SunColorTint);
	}

	UpdatePrecipitation();
	UpdateLightning(DeltaSeconds);
}

void AWeatherSystem::EnsureReady()
{
	RefreshTimeSystem();

	if (!bSceneInitialized)
	{
		InitSceneBindings();
		bSceneInitialized = true;
	}
}

void AWeatherSystem::RefreshTimeSystem()
{
	UTimeSystem* Current = GetTimeSystem();
	if (Current == BoundTimeSystem.Get())
	{
		return;
	}

	if (BoundTimeSystem)
	{
		BoundTimeSystem->OnDayChanged.RemoveDynamic(this, &AWeatherSystem::HandleDayChanged);
		if (BoundTimeSystem->StylizedSkyActor == StylizedSkyActor)
		{
			BoundTimeSystem->StylizedSkyActor = nullptr;
		}
	}

	BoundTimeSystem = Current;
	if (BoundTimeSystem)
	{
		BoundTimeSystem->StylizedSkyActor = StylizedSkyActor;
		BoundTimeSystem->OnDayChanged.AddDynamic(this, &AWeatherSystem::HandleDayChanged);
	}
}

UTimeSystem* AWeatherSystem::GetTimeSystem() const
{
	if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance()))
	{
		if (GI->businessSimulation)
		{
			return GI->businessSimulation->TimeSystem;
		}
	}
	return nullptr;
}

ESeason AWeatherSystem::GetCurrentSeason() const
{
	if (const UTimeSystem* Time = GetTimeSystem())
	{
		return Time->GetSeason();
	}
	return ESeason::Spring;
}

void AWeatherSystem::InitSceneBindings()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 未手动绑定时按类型查找场景中的第一个对应 Actor
	if (!SkyLightActor)
	{
		for (TActorIterator<ASkyLight> It(World); It; ++It)
		{
			SkyLightActor = *It;
			break;
		}
	}
	if (!HeightFogActor)
	{
		for (TActorIterator<AExponentialHeightFog> It(World); It; ++It)
		{
			HeightFogActor = *It;
			break;
		}
	}
	if (!VolumetricCloudActor)
	{
		for (TActorIterator<AVolumetricCloud> It(World); It; ++It)
		{
			VolumetricCloudActor = *It;
			break;
		}
	}
	if (!WindSourceActor)
	{
		for (TActorIterator<AWindDirectionalSource> It(World); It; ++It)
		{
			WindSourceActor = *It;
			break;
		}
	}

	if (SkyLightActor)
	{
		SkyLightComp = SkyLightActor->GetLightComponent();
		if (SkyLightComp)
		{
			// 以场景作者设置的天光强度为基准，预设里的倍率在此基础上缩放
			BaseSkyLightIntensity = FMath::Max(0.01f, SkyLightComp->Intensity);
		}
	}
	if (HeightFogActor)
	{
		FogComp = HeightFogActor->GetComponent();
	}
	if (VolumetricCloudActor)
	{
		CloudComp = VolumetricCloudActor->FindComponentByClass<UVolumetricCloudComponent>();
	}
	if (WindSourceActor)
	{
		WindComp = WindSourceActor->GetComponent();
	}

	if (StylizedSkyActor)
	{
		SkyLightComp = nullptr;
		FogComp = nullptr;
		CloudComp = nullptr;
		if (!StylizedSkyActor->FindComponentByClass<UStylizedSkyBridge>())
		{
			UE_LOG(LogTemp, Error, TEXT("[Weather] StylizedSkyActor 必须包含 StylizedSkyBridge 组件"));
		}
	}

	if (bShowDebugInfo)
	{
		UE_LOG(LogTemp, Log, TEXT("[Weather] 场景绑定：SkyLight=%s Fog=%s Cloud=%s Wind=%s MPC=%s"),
			SkyLightComp ? TEXT("OK") : TEXT("None"),
			FogComp ? TEXT("OK") : TEXT("None"),
			CloudComp ? TEXT("OK") : TEXT("None"),
			WindComp ? TEXT("OK") : TEXT("None"),
			CloudParameterCollection ? TEXT("OK") : TEXT("None"));
	}
}

const FWeatherPreset* AWeatherSystem::FindPreset(FName RowName) const
{
	if (!WeatherTable || RowName.IsNone())
	{
		return nullptr;
	}
	return WeatherTable->FindRow<FWeatherPreset>(RowName, TEXT("WeatherSystem"), false);
}

bool AWeatherSystem::GetCurrentWeatherInfo(FWeatherPreset& OutPreset) const
{
	const FWeatherPreset* Preset = FindPreset(CurrentWeatherID);
	if (!Preset)
	{
		return false;
	}
	OutPreset = *Preset;
	return true;
}

bool AWeatherSystem::SetWeatherByID(FName InWeatherID, bool bInstant)
{
	const FWeatherPreset* Preset = FindPreset(InWeatherID);
	if (!Preset)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Weather] 天气 ID '%s' 不在数据表中"), *InWeatherID.ToString());
		return false;
	}

	if (InWeatherID == CurrentWeatherID && !bTransitioning)
	{
		return true;
	}

	const FName OldID = CurrentWeatherID;
	const FWeatherPreset NewPreset = *Preset;
	const float Duration = bInstant
		? 0.f
		: (NewPreset.TransitionSeconds > 0.f ? NewPreset.TransitionSeconds : DefaultTransitionSeconds);

	StartTransitionTo(NewPreset, InWeatherID, Duration);

	if (bShowDebugInfo && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Cyan,
			FString::Printf(TEXT("[Weather] %s -> %s"), *OldID.ToString(), *InWeatherID.ToString()));
	}

	OnWeatherChanged.Broadcast(OldID, InWeatherID);
	return true;
}

bool AWeatherSystem::SelectDailyWeather()
{
	FName PickedID;
	FWeatherPreset PickedPreset;
	if (!PickWeightedPresetForSeason(PickedID, PickedPreset))
	{
		return false;
	}
	return SetWeatherByID(PickedID, false);
}

void AWeatherSystem::HandleDayChanged(int32 InYear, int32 InMonth, int32 InDay)
{
	if (bAutoSelectDaily)
	{
		SelectDailyWeather();
	}
}

bool AWeatherSystem::PickWeightedPresetForSeason(FName& OutID, FWeatherPreset& OutPreset)
{
	if (!WeatherTable)
	{
		return false;
	}

	const ESeason Season = GetCurrentSeason();
	const TArray<FName> RowNames = WeatherTable->GetRowNames();

	struct FCandidate
	{
		FName ID;
		float Weight = 0.f;
		const FWeatherPreset* Preset = nullptr;
	};

	TArray<FCandidate> Candidates;
	float TotalWeight = 0.f;
	for (const FName& Row : RowNames)
	{
		const FWeatherPreset* Preset = WeatherTable->FindRow<FWeatherPreset>(Row, TEXT("WeatherSelect"), false);
		if (!Preset)
		{
			continue;
		}
		const float Weight = FMath::Max(0.f, Preset->GetSeasonWeight(Season));
		if (Weight > 0.f)
		{
			Candidates.Add({ Row, Weight, Preset });
			TotalWeight += Weight;
		}
	}

	// 该季节没有配权重时退化为等概率，避免“整天没天气”
	if (Candidates.Num() == 0)
	{
		for (const FName& Row : RowNames)
		{
			if (const FWeatherPreset* Preset = WeatherTable->FindRow<FWeatherPreset>(Row, TEXT("WeatherSelect"), false))
			{
				Candidates.Add({ Row, 1.f, Preset });
				TotalWeight += 1.f;
			}
		}
	}
	if (Candidates.Num() == 0)
	{
		return false;
	}

	// 有多种可选时排除当前天气，避免连续两天一模一样
	if (Candidates.Num() > 1)
	{
		float TotalExcludingCurrent = 0.f;
		for (const FCandidate& C : Candidates)
		{
			if (C.ID != CurrentWeatherID)
			{
				TotalExcludingCurrent += C.Weight;
			}
		}
		if (TotalExcludingCurrent > 0.f)
		{
			Candidates.RemoveAll([this](const FCandidate& C) { return C.ID == CurrentWeatherID; });
			TotalWeight = TotalExcludingCurrent;
		}
	}

	float Roll = Rand.FRandRange(0.f, TotalWeight);
	int32 PickedIndex = Candidates.Num() - 1;
	for (int32 i = 0; i < Candidates.Num(); ++i)
	{
		Roll -= Candidates[i].Weight;
		if (Roll <= 0.f)
		{
			PickedIndex = i;
			break;
		}
	}

	OutID = Candidates[PickedIndex].ID;
	OutPreset = *Candidates[PickedIndex].Preset;
	return true;
}

void AWeatherSystem::StartTransitionTo(const FWeatherPreset& Preset, FName PresetID, float InTransitionSeconds)
{
	BlendFrom = BlendCurrent;
	BlendTo = FWeatherBlendState::FromPreset(Preset);
	CurrentPreset = Preset;
	CurrentWeatherID = PresetID;
	TransitionElapsed = 0.f;
	TransitionDuration = FMath::Max(0.f, InTransitionSeconds);
	bTransitioning = TransitionDuration > 0.f;
	ApplyStaticFeatures(Preset);
	if (!bTransitioning)
	{
		BlendCurrent = BlendTo;
		ApplyBlend(BlendCurrent);
	}
}

void AWeatherSystem::TickTransition(float DeltaSeconds)
{
	TransitionElapsed += DeltaSeconds;
	const float Alpha = TransitionDuration > 0.f
		? FMath::Clamp(TransitionElapsed / TransitionDuration, 0.f, 1.f)
		: 1.f;

	BlendCurrent = BlendFrom.LerpTo(BlendTo, FMath::SmoothStep(0.f, 1.f, Alpha));
	ApplyBlend(BlendCurrent);

	if (Alpha >= 1.f)
	{
		bTransitioning = false;
		BlendCurrent = BlendTo;
	}
}

void AWeatherSystem::ApplyBlend(const FWeatherBlendState& Blend)
{
	if (SkyLightComp)
	{
		SkyLightComp->SetIntensity(BaseSkyLightIntensity * Blend.SkyLightIntensityScale);
		SkyLightComp->SetLightColor(Blend.SkyLightColor);
	}

	if (FogComp)
	{
		FogComp->SetFogDensity(Blend.FogDensity);
		FogComp->SetFogHeightFalloff(Blend.FogHeightFalloff);
		FogComp->SetFogMaxOpacity(Blend.FogMaxOpacity);
		FogComp->SetFogInscatteringColor(Blend.FogColor);
	}

	if (CloudComp)
	{
		if (CloudMaterialInstance)
		{
			if (!ActiveCloudCoverageParameter.IsNone())
			{
				CloudMaterialInstance->SetScalarParameterValue(ActiveCloudCoverageParameter, Blend.CloudCoverage);
			}
			if (!ActiveCloudDensityParameter.IsNone())
			{
				CloudMaterialInstance->SetScalarParameterValue(ActiveCloudDensityParameter, Blend.CloudDensity);
			}
		}
		// UDS 的体积云参数走 MPC（材质里读 Collection 而不是直接参数）；
		// 参数名先校验，避免对不存在的参数反复刷引擎警告
		if (CloudParameterCollection)
		{
			if (HasScalarParameter(CloudParameterCollection, ActiveCloudCoverageParameter))
			{
				UKismetMaterialLibrary::SetScalarParameterValue(this, CloudParameterCollection, ActiveCloudCoverageParameter, Blend.CloudCoverage);
			}
			if (HasScalarParameter(CloudParameterCollection, ActiveCloudDensityParameter))
			{
				UKismetMaterialLibrary::SetScalarParameterValue(this, CloudParameterCollection, ActiveCloudDensityParameter, Blend.CloudDensity);
			}
		}
	}

	if (WindComp)
	{
		WindComp->SetStrength(Blend.WindStrength);
		WindComp->SetSpeed(Blend.WindSpeed);
	}

	if (PrecipitationComponent && ActivePrecipitationType != EPrecipitationType::None
		&& !ActivePrecipitationRateParameter.IsNone())
	{
		PrecipitationComponent->SetFloatParameter(ActivePrecipitationRateParameter, Blend.PrecipitationRate);
	}

	if (AmbientAudioComponent && AmbientAudioComponent->IsPlaying())
	{
		AmbientAudioComponent->SetVolumeMultiplier(Blend.AmbientVolume);
	}
}

void AWeatherSystem::ApplyStaticFeatures(const FWeatherPreset& Preset)
{
	// 云：可见性 / 材质（仅切换预设时做，不参与逐帧插值）
	if (CloudComp)
	{
		CloudComp->SetVisibility(Preset.bCloudsVisible);

		UMaterialInterface* BaseMaterial = Preset.CloudMaterial.IsNull() ? nullptr : Preset.CloudMaterial.LoadSynchronous();
		if (BaseMaterial && BaseMaterial != ActiveCloudBaseMaterial)
		{
			ActiveCloudBaseMaterial = BaseMaterial;
			CloudMaterialInstance = UMaterialInstanceDynamic::Create(BaseMaterial, this);
			CloudComp->SetMaterial(CloudMaterialInstance);
		}
	}
	ActiveCloudCoverageParameter = Preset.CloudCoverageParameter;
	ActiveCloudDensityParameter = Preset.CloudDensityParameter;
	if (bShowDebugInfo && CloudParameterCollection)
	{
		if (!HasScalarParameter(CloudParameterCollection, ActiveCloudCoverageParameter))
		{
			UE_LOG(LogTemp, Warning, TEXT("[Weather] 云参数 '%s' 不在 MPC %s 中，覆盖度不会被驱动"),
				*ActiveCloudCoverageParameter.ToString(), *CloudParameterCollection->GetName());
		}
		if (!HasScalarParameter(CloudParameterCollection, ActiveCloudDensityParameter))
		{
			UE_LOG(LogTemp, Warning, TEXT("[Weather] 云参数 '%s' 不在 MPC %s 中，密度不会被驱动"),
				*ActiveCloudDensityParameter.ToString(), *CloudParameterCollection->GetName());
		}
	}

	// 降水
	ActivePrecipitationType = Preset.PrecipitationType;
	ActivePrecipitationRateParameter = Preset.PrecipitationRateParameter;
	if (PrecipitationComponent)
	{
		bool bSkyHandlesRain = false;
		if (Preset.PrecipitationType == EPrecipitationType::Rain && StylizedSkyActor
			&& StylizedSkyActor->FindComponentByClass<UStylizedSkyBridge>())
		{
			TInlineComponentArray<UNiagaraComponent*> SkyEffects(StylizedSkyActor);
			for (const UNiagaraComponent* Effect : SkyEffects)
			{
				if (Effect->GetFName() == TEXT("FX_Rain") && Effect->GetAsset())
				{
					bSkyHandlesRain = true;
					break;
				}
			}
		}
		if (Preset.PrecipitationType == EPrecipitationType::None || bSkyHandlesRain)
		{
			ActivePrecipitationEffect = nullptr;
			PrecipitationComponent->Deactivate();
		}
		else
		{
			UNiagaraSystem* Effect = Preset.PrecipitationEffect.IsNull() ? nullptr : Preset.PrecipitationEffect.LoadSynchronous();
			ActivePrecipitationEffect = Effect;
			if (Effect)
			{
				PrecipitationComponent->SetAsset(Effect);
				PrecipitationComponent->Activate(true);
			}
			else
			{
				if (bShowDebugInfo)
				{
					UE_LOG(LogTemp, Warning, TEXT("[Weather] 降水类型为 %s，但预设未指定 NiagaraEffect，无降水粒子"),
						*StaticEnum<EPrecipitationType>()->GetDisplayNameTextByValue((int64)Preset.PrecipitationType).ToString());
				}
				PrecipitationComponent->Deactivate();
			}
		}
	}

	// 环境音
	USoundBase* Ambient = Preset.AmbientSound.IsNull() ? nullptr : Preset.AmbientSound.LoadSynchronous();
	if (Ambient != ActiveAmbientSound.Get())
	{
		ActiveAmbientSound = Ambient;
		if (AmbientAudioComponent)
		{
			if (Ambient)
			{
				AmbientAudioComponent->SetSound(Ambient);
				// 先按上一种天气的音量起播，再由 ApplyBlend 平滑推向新天气音量，避免爆音
				AmbientAudioComponent->SetVolumeMultiplier(BlendFrom.AmbientVolume);
				AmbientAudioComponent->Play();
			}
			else
			{
				AmbientAudioComponent->Stop();
			}
		}
	}
	ActiveThunderSound = Preset.ThunderSound.IsNull() ? nullptr : Preset.ThunderSound.LoadSynchronous();

	// 雷电
	if (Preset.bLightning)
	{
		const float IntervalMin = FMath::Max(0.5f, FMath::Min(Preset.LightningIntervalMin, Preset.LightningIntervalMax));
		const float IntervalMax = FMath::Max(IntervalMin, FMath::Max(Preset.LightningIntervalMin, Preset.LightningIntervalMax));
		LightningCountdown = Rand.FRandRange(IntervalMin, IntervalMax);
	}
	else
	{
		LightningCountdown = 0.f;
		FlashRemaining = 0.f;
		if (LightningLight)
		{
			LightningLight->SetIntensity(0.f);
			LightningLight->SetVisibility(false);
		}
	}
}

void AWeatherSystem::UpdatePrecipitation()
{
	if (!PrecipitationComponent || ActivePrecipitationType == EPrecipitationType::None || !ActivePrecipitationEffect)
	{
		return;
	}
	if (!PrecipitationComponent->IsActive())
	{
		return;
	}

	if (const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		PrecipitationComponent->SetWorldLocation(Player->GetActorLocation() + FVector(0.f, 0.f, PrecipitationFollowHeight));
	}
}

void AWeatherSystem::UpdateLightning(float DeltaSeconds)
{
	if (!LightningLight)
	{
		return;
	}

	if (FlashRemaining > 0.f)
	{
		FlashRemaining = FMath::Max(0.f, FlashRemaining - DeltaSeconds);
		const float Alpha = LightningFlashDuration > 0.f ? FlashRemaining / LightningFlashDuration : 0.f;
		LightningLight->SetIntensity(LightningFlashIntensity * Alpha * Alpha);
		if (FlashRemaining <= 0.f)
		{
			LightningLight->SetIntensity(0.f);
			LightningLight->SetVisibility(false);
		}
		return;
	}

	if (!CurrentPreset.bLightning)
	{
		return;
	}

	LightningCountdown -= DeltaSeconds;
	if (LightningCountdown > 0.f)
	{
		return;
	}

	const float IntervalMin = FMath::Max(0.5f, FMath::Min(CurrentPreset.LightningIntervalMin, CurrentPreset.LightningIntervalMax));
	const float IntervalMax = FMath::Max(IntervalMin, FMath::Max(CurrentPreset.LightningIntervalMin, CurrentPreset.LightningIntervalMax));
	LightningCountdown = Rand.FRandRange(IntervalMin, IntervalMax);

	FlashRemaining = FMath::Max(0.05f, LightningFlashDuration);

	FVector BaseLocation = GetActorLocation();
	if (const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		BaseLocation = Player->GetActorLocation();
	}
	LightningLight->SetWorldLocation(BaseLocation + FVector(
		Rand.FRandRange(-4000.f, 4000.f),
		Rand.FRandRange(-4000.f, 4000.f),
		LightningLightHeight));
	LightningLight->SetVisibility(true);
	LightningLight->SetIntensity(LightningFlashIntensity);

	if (ActiveThunderSound && ThunderAudioComponent)
	{
		ThunderAudioComponent->SetSound(ActiveThunderSound);
		ThunderAudioComponent->Play();
	}
}
