#include "Environment/FoliageOcclusionFaderSubsystem.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

namespace
{
	const TCHAR* DefaultFadeMeshPaths[] =
	{
		TEXT("/Game/Fantastic_Village_Pack/meshes/environment/trees/SM_tree_01.SM_tree_01"),
		TEXT("/Game/Fantastic_Village_Pack/meshes/environment/trees/SM_tree_bush_01.SM_tree_bush_01"),
	};

	float DistancePointToSegment(const FVector& Point, const FVector& A, const FVector& B)
	{
		const FVector Segment = B - A;
		const float LengthSq = Segment.SizeSquared();
		if (LengthSq <= KINDA_SMALL_NUMBER)
		{
			return FVector::Dist(Point, A);
		}

		const float T = FMath::Clamp(FVector::DotProduct(Point - A, Segment) / LengthSq, 0.f, 1.f);
		return FVector::Dist(Point, A + Segment * T);
	}
}

UFoliageOcclusionFaderSubsystem::UFoliageOcclusionFaderSubsystem()
{
	for (const TCHAR* Path : DefaultFadeMeshPaths)
	{
		FadeMeshPaths.AddUnique(FSoftObjectPath(Path));
	}
}

bool UFoliageOcclusionFaderSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UFoliageOcclusionFaderSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UFoliageOcclusionFaderSubsystem, STATGROUP_Tickables);
}

void UFoliageOcclusionFaderSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bEnabled)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}

	TimeSinceRebuild += DeltaTime;
	if (!bCacheBuilt || TimeSinceRebuild >= RescanInterval)
	{
		RebuildCache();
	}

	UpdateInstances(DeltaTime);
}

void UFoliageOcclusionFaderSubsystem::RebuildCache()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	TMap<FFoliageFadeInstanceKey, float> PreviousFades;
	PreviousFades.Reserve(TrackedInstances.Num());
	for (const FTrackedInstance& Old : TrackedInstances)
	{
		if (const UInstancedStaticMeshComponent* Component = Old.Component.Get())
		{
			PreviousFades.Add(FFoliageFadeInstanceKey{ Component, Old.InstanceIndex }, Old.CurrentFade);
		}
	}

	TrackedInstances.Reset();
	FadeMeshPathStrings.Reset();
	for (const FSoftObjectPath& Path : FadeMeshPaths)
	{
		FadeMeshPathStrings.Add(Path.ToString());
	}

	if (FadeMeshPathStrings.Num() == 0)
	{
		TimeSinceRebuild = 0.f;
		bCacheBuilt = true;
		return;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor)
		{
			continue;
		}

		TArray<UInstancedStaticMeshComponent*> Components;
		Actor->GetComponents<UInstancedStaticMeshComponent>(Components);
		for (UInstancedStaticMeshComponent* Component : Components)
		{
			if (!Component || Component->GetInstanceCount() <= 0)
			{
				continue;
			}

			const UStaticMesh* Mesh = Component->GetStaticMesh();
			if (!Mesh || !FadeMeshPathStrings.Contains(FSoftObjectPath(Mesh).ToString()))
			{
				continue;
			}

			bool bResizedCustomData = false;
			if (Component->NumCustomDataFloats <= CustomDataIndex)
			{
				Component->SetNumCustomDataFloats(CustomDataIndex + 1);
				bResizedCustomData = true;
			}

			const FBoxSphereBounds MeshBounds = Mesh->GetBounds();
			const int32 InstanceCount = Component->GetInstanceCount();
			TrackedInstances.Reserve(TrackedInstances.Num() + InstanceCount);

			for (int32 Index = 0; Index < InstanceCount; ++Index)
			{
				FTransform InstanceTransform;
				if (!Component->GetInstanceTransform(Index, InstanceTransform, true))
				{
					continue;
				}

				FTrackedInstance Instance;
				Instance.Component = Component;
				Instance.InstanceIndex = Index;
				Instance.Center = InstanceTransform.TransformPosition(MeshBounds.Origin);
				Instance.Radius = MeshBounds.SphereRadius * InstanceTransform.GetMaximumAxisScale();
				Instance.WrittenFade = -1.f;

				if (const float* Previous = PreviousFades.Find(FFoliageFadeInstanceKey{ Component, Index }))
				{
					Instance.CurrentFade = *Previous;
					Instance.WrittenFade = bResizedCustomData ? -1.f : *Previous;
				}

				TrackedInstances.Add(MoveTemp(Instance));
			}
		}
	}

	TimeSinceRebuild = 0.f;
	bCacheBuilt = true;
}

void UFoliageOcclusionFaderSubsystem::UpdateInstances(float DeltaTime)
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (!Pawn)
	{
		return;
	}

	FVector CameraLocation = Pawn->GetPawnViewLocation();
	if (PlayerController->PlayerCameraManager)
	{
		CameraLocation = PlayerController->PlayerCameraManager->GetCameraLocation();
	}

	FVector FocusLocation = Pawn->GetActorLocation();
	if (const ACharacter* Character = Cast<ACharacter>(Pawn))
	{
		if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
		{
			FocusLocation.Z += Capsule->GetScaledCapsuleHalfHeight() * 0.7f;
		}
	}
	else
	{
		FocusLocation.Z += 60.f;
	}

	const FVector ToFocus = FocusLocation - CameraLocation;
	const float SegmentLength = ToFocus.Size();
	if (SegmentLength <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const FVector ViewDirection = ToFocus / SegmentLength;
	const float MaxDistanceSq = FMath::Square(MaxPlayerDistance);

	for (FTrackedInstance& Instance : TrackedInstances)
	{
		UInstancedStaticMeshComponent* Component = Instance.Component.Get();
		if (!Component)
		{
			continue;
		}

		float TargetFade = 0.f;
		if (FVector::DistSquared(Instance.Center, FocusLocation) <= MaxDistanceSq)
		{
			const float NearCameraRadius = Instance.Radius + NearCameraPadding;
			if (FVector::DistSquared(Instance.Center, CameraLocation) <= FMath::Square(NearCameraRadius))
			{
				// 相机贴进树冠，直接淡出
				TargetFade = 1.f;
			}
			else
			{
				const float Along = FVector::DotProduct(Instance.Center - CameraLocation, ViewDirection);
				if (Along > 0.f && Along < SegmentLength)
				{
					const float Threshold = Instance.Radius * OcclusionRadiusScale + OcclusionPadding;
					if (DistancePointToSegment(Instance.Center, CameraLocation, FocusLocation) < Threshold)
					{
						TargetFade = 1.f;
					}
				}
			}
		}

		const float Speed = TargetFade > Instance.CurrentFade ? FadeOutSpeed : FadeInSpeed;
		Instance.CurrentFade = FMath::FInterpTo(Instance.CurrentFade, TargetFade, DeltaTime, Speed);
		if (FMath::IsNearlyEqual(Instance.CurrentFade, TargetFade, 0.01f))
		{
			Instance.CurrentFade = TargetFade;
		}

		if (FMath::Abs(Instance.CurrentFade - Instance.WrittenFade) > 0.005f)
		{
			Component->SetCustomDataValue(Instance.InstanceIndex, CustomDataIndex, Instance.CurrentFade, false);
			Instance.WrittenFade = Instance.CurrentFade;
		}
	}
}
