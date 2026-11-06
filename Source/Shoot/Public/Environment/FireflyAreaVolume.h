#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Volume.h"
#include "FireflyAreaVolume.generated.h"

class AShootCharacter;
class UInstancedStaticMeshComponent;
class UTimeSystem;

UCLASS(Blueprintable)
class SHOOT_API AFireflyAreaVolume : public AVolume
{
	GENERATED_BODY()

public:
	AFireflyAreaVolume();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shoot|Fireflies")
	TObjectPtr<UInstancedStaticMeshComponent> Fireflies;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Fireflies")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Fireflies", meta = (ClampMin = "1", ClampMax = "64"))
	int32 FireflyCount = 16;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Fireflies", meta = (ClampMin = "1.0"))
	float WanderSpeed = 32.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Fireflies", meta = (ClampMin = "0.5"))
	float GlowRadius = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Fireflies", meta = (ClampMin = "0.1"))
	float FadeSpeed = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Fireflies|Interaction", meta = (ClampMin = "1.0"))
	float InteractionRadius = 180.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Fireflies|Interaction", meta = (ClampMin = "1.0"))
	float ScatterSpeed = 220.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Fireflies|Interaction", meta = (ClampMin = "0.1"))
	float ScatterDuration = 2.f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Shoot|Fireflies")
	bool bNightActive = false;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Shoot|Fireflies")
	bool bScattering = false;

	UFUNCTION(BlueprintPure, Category = "Shoot|Fireflies|Interaction")
	bool CanInteract(AShootCharacter* Player) const;

	UFUNCTION(BlueprintCallable, Category = "Shoot|Fireflies|Interaction")
	bool Interact(AShootCharacter* Player);

	float GetNearestFireflyDistance(const FVector& Location) const;
	FVector GetNearestFireflyLocation(const FVector& Location) const;

private:
	struct FFirefly
	{
		FVector Position;
		FVector Target;
		FVector Velocity = FVector::ZeroVector;
		float Phase = 0.f;
		float BlinkPeriod = 3.f;
		float TargetTime = 0.f;
	};

	TArray<FFirefly> Swarm;
	TWeakObjectPtr<UTimeSystem> TimeSystem;
	float VisibilityAlpha = 0.f;
	float ScatterTimeRemaining = 0.f;

	void RefreshNightState();
	FVector RandomPointInVolume() const;
};
