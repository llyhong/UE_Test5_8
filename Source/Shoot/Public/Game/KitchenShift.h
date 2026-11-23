#pragma once

#include "CoreMinimal.h"
#include "KitchenShift.generated.h"

UENUM(BlueprintType)
enum class EKitchenShift : uint8
{
	None, Rush, TwoStations, PlateTurnover
};

USTRUCT(BlueprintType)
struct FKitchenShiftProgress
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Kitchen Shift")
	int32 Day = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Kitchen Shift")
	EKitchenShift Type = EKitchenShift::None;

	UPROPERTY()
	TArray<int32> DishIDs;

	UPROPERTY()
	TArray<FString> DishNames;

	UPROPERTY()
	bool bPremiumGoal = false;

	UPROPERTY(BlueprintReadOnly, Category = "Kitchen Shift")
	int32 Served = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Kitchen Shift")
	int32 OnTime = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Kitchen Shift")
	int32 BestStreak = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Kitchen Shift")
	int32 Premium = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Kitchen Shift")
	int32 PlatesWashed = 0;

	UPROPERTY()
	TMap<int32, int32> DishesServed;

	bool IsActive() const { return Type != EKitchenShift::None; }
	int32 GetStars() const;
	FText Format(bool bSettlement, bool bBeforeOpening = false) const;

private:
	struct FGoal
	{
		FText Label;
		int32 Value;
		int32 Target;
	};
	TArray<FGoal> GetGoals() const;
};
