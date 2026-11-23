#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HarvestBasketArea.generated.h"

class AOutdoorBasket;
class AShootCharacter;
class AStorageWarehouse;
class UBoxComponent;

/** Hands the shared harvest basket to players as they cross the kitchen boundary. */
UCLASS()
class SHOOT_API AHarvestBasketArea : public AActor
{
	GENERATED_BODY()

public:
	AHarvestBasketArea();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Harvest Basket")
	TObjectPtr<UBoxComponent> KitchenBounds;

	UPROPERTY(EditInstanceOnly, Category = "Harvest Basket")
	TObjectPtr<AOutdoorBasket> Basket;

	UPROPERTY(EditInstanceOnly, Category = "Harvest Basket")
	TObjectPtr<AStorageWarehouse> Warehouse;

	UPROPERTY(EditInstanceOnly, Category = "Harvest Basket", meta = (MakeEditWidget = true))
	FVector EntranceGuideOffset = FVector::ZeroVector;

	FVector GetEntranceGuideLocation() const { return GetActorTransform().TransformPosition(EntranceGuideOffset); }
	bool IsInsideKitchen(const FVector& Location) const { return IsInside(Location, 0.0); }
	bool IsBasketAvailableInKitchen() const;

private:
	struct FPlayerCrossing
	{
		bool bInside = false;
		bool bPending = false;
	};

	bool IsInside(const FVector& Location, double Margin) const;
	void UpdatePlayer(AShootCharacter& Player);

	TMap<TWeakObjectPtr<AShootCharacter>, FPlayerCrossing> Crossings;
	FTransform DockTransform;
};
