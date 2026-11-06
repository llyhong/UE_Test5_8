#include "Environment/FireflyAreaVolume.h"
#include "Components/BrushComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Game/BaseGameInstance.h"
#include "Game/BusinessSimulation.h"
#include "Game/BusinessTime.h"
#include "Materials/MaterialInterface.h"
#include "ShootCharacter.h"
#include "UObject/ConstructorHelpers.h"

AFireflyAreaVolume::AFireflyAreaVolume()
{
	PrimaryActorTick.bCanEverTick = true;
	SetActorHiddenInGame(false);
	GetBrushComponent()->SetHiddenInGame(true);
	GetBrushComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	GetBrushComponent()->SetCollisionResponseToAllChannels(ECR_Ignore);
	GetBrushComponent()->SetGenerateOverlapEvents(false);
	bColored = true;
	BrushColor = FColor(190, 230, 70);

	Fireflies = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Fireflies"));
	Fireflies->SetupAttachment(RootComponent);
	Fireflies->SetMobility(EComponentMobility::Movable);
	Fireflies->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Fireflies->SetCastShadow(false);
	Fireflies->SetVisibility(false);
	Fireflies->NumCustomDataFloats = 1;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Glow(TEXT("/Game/VFX/Fireflies/M_FireflyGlow"));
	Fireflies->SetStaticMesh(Sphere.Object);
	Fireflies->SetMaterial(0, Glow.Object);
}

void AFireflyAreaVolume::BeginPlay()
{
	Super::BeginPlay();
	Fireflies->ClearInstances();
	Swarm.SetNum(FMath::Clamp(FireflyCount, 1, 64));
	for (FFirefly& Fly : Swarm)
	{
		Fly.Position = RandomPointInVolume();
		Fly.Target = RandomPointInVolume();
		Fly.Phase = FMath::FRandRange(0.f, 2.f * PI);
		Fly.BlinkPeriod = FMath::FRandRange(2.2f, 4.6f);
		Fly.TargetTime = FMath::FRandRange(2.f, 5.f);
		Fireflies->AddInstance(FTransform(FQuat::Identity, Fly.Position, FVector(GlowRadius / 50.f)), true);
	}
	RefreshNightState();
}

void AFireflyAreaVolume::RefreshNightState()
{
	if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance()))
	{
		TimeSystem = GI->businessSimulation ? GI->businessSimulation->TimeSystem : nullptr;
	}
	const UTimeSystem* Time = TimeSystem.Get();
	bNightActive = bEnabled && Time &&
		(Time->GetCurrentHour() >= Time->SunsetHour || Time->GetCurrentHour() < Time->SunriseHour);
	if (!bNightActive)
	{
		ScatterTimeRemaining = 0.f;
		bScattering = false;
	}
}

FVector AFireflyAreaVolume::RandomPointInVolume() const
{
	const FBox Bounds = GetBrushComponent()->Bounds.GetBox();
	for (int32 Attempt = 0; Attempt < 32; ++Attempt)
	{
		const FVector Point = FMath::RandPointInBox(Bounds);
		if (EncompassesPoint(Point))
		{
			return Point;
		}
	}
	return GetActorLocation();
}

void AFireflyAreaVolume::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	RefreshNightState();
	VisibilityAlpha = FMath::FInterpConstantTo(VisibilityAlpha, bNightActive ? 1.f : 0.f, DeltaSeconds, FadeSpeed);
	Fireflies->SetVisibility(VisibilityAlpha > KINDA_SMALL_NUMBER);
	if (VisibilityAlpha <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	ScatterTimeRemaining = FMath::Max(0.f, ScatterTimeRemaining - DeltaSeconds);
	bScattering = ScatterTimeRemaining > 0.f;
	const float ScatterAlpha = FMath::Clamp(ScatterTimeRemaining / ScatterDuration, 0.f, 1.f);
	const float Speed = FMath::Lerp(WanderSpeed, ScatterSpeed, ScatterAlpha);
	const float Time = GetWorld()->GetTimeSeconds();
	for (int32 Index = 0; Index < Swarm.Num(); ++Index)
	{
		FFirefly& Fly = Swarm[Index];
		Fly.TargetTime -= DeltaSeconds;
		if (Fly.TargetTime <= 0.f || FVector::DistSquared(Fly.Position, Fly.Target) < 100.f)
		{
			Fly.Target = RandomPointInVolume();
			Fly.TargetTime = FMath::FRandRange(2.f, 5.f);
		}
		const FVector DesiredVelocity = (Fly.Target - Fly.Position).GetSafeNormal() * Speed;
		Fly.Velocity = FMath::VInterpTo(Fly.Velocity, DesiredVelocity, DeltaSeconds, bScattering ? 8.f : 2.f);
		const FVector NextPosition = Fly.Position + Fly.Velocity * DeltaSeconds;
		if (EncompassesPoint(NextPosition))
		{
			Fly.Position = NextPosition;
		}
		else
		{
			Fly.Target = RandomPointInVolume();
			Fly.Velocity = FVector::ZeroVector;
		}
		const float Pulse = FMath::Pow(FMath::Max(0.f, FMath::Sin(Time * 2.f * PI / Fly.BlinkPeriod + Fly.Phase)), 3.f);
		Fireflies->UpdateInstanceTransform(Index, FTransform(FQuat::Identity, Fly.Position, FVector(GlowRadius / 50.f)), true, false, true);
		Fireflies->SetCustomDataValue(Index, 0, VisibilityAlpha * (0.06f + 0.94f * Pulse), false);
	}
	Fireflies->MarkRenderStateDirty();
}

float AFireflyAreaVolume::GetNearestFireflyDistance(const FVector& Location) const
{
	float DistanceSquared = FLT_MAX;
	for (const FFirefly& Fly : Swarm)
	{
		DistanceSquared = FMath::Min(DistanceSquared, static_cast<float>(FVector::DistSquared(Location, Fly.Position)));
	}
	return FMath::Sqrt(DistanceSquared);
}

bool AFireflyAreaVolume::CanInteract(AShootCharacter* Player) const
{
	return IsValid(Player) && bNightActive && !bScattering && VisibilityAlpha > 0.2f &&
		GetNearestFireflyDistance(Player->GetActorLocation()) <= InteractionRadius;
}

bool AFireflyAreaVolume::Interact(AShootCharacter* Player)
{
	if (!CanInteract(Player))
	{
		return false;
	}
	ScatterTimeRemaining = ScatterDuration;
	bScattering = true;
	for (FFirefly& Fly : Swarm)
	{
		const FVector Away = (Fly.Position - Player->GetActorLocation()).GetSafeNormal();
		FVector Target = RandomPointInVolume();
		double BestScore = FVector::DotProduct(Target - Fly.Position, Away);
		for (int32 Attempt = 0; Attempt < 12; ++Attempt)
		{
			const FVector Candidate = RandomPointInVolume();
			const double Score = FVector::DotProduct(Candidate - Fly.Position, Away);
			if (Score > BestScore)
			{
				Target = Candidate;
				BestScore = Score;
			}
		}
		Fly.Target = Target;
		Fly.TargetTime = ScatterDuration;
		Fly.Velocity = (Target - Fly.Position).GetSafeNormal() * ScatterSpeed;
	}
	return true;
}

FVector AFireflyAreaVolume::GetNearestFireflyLocation(const FVector& Location) const
{
	FVector Nearest = GetActorLocation();
	double DistanceSquared = DBL_MAX;
	for (const FFirefly& Fly : Swarm)
	{
		const double Distance = FVector::DistSquared(Location, Fly.Position);
		if (Distance < DistanceSquared)
		{
			DistanceSquared = Distance;
			Nearest = Fly.Position;
		}
	}
	return Nearest;
}
