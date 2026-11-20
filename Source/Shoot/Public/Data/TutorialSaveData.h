#pragma once

#include "CoreMinimal.h"
#include "ShootCharacter.h"
#include "TutorialSaveData.generated.h"

// Stable level actor names are used instead of PIE-specific object paths.
USTRUCT()
struct FTutorialContainerSave
{
	GENERATED_BODY()
	UPROPERTY() FName Key;
	UPROPERTY() TArray<FInventorySlot> Items;
};

USTRUCT()
struct FTutorialItemSave
{
	GENERATED_BODY()
	UPROPERTY() FInventorySlot Item;
	UPROPERTY() FTransform Transform;
	UPROPERTY() int32 MenuID = -1;
	UPROPERTY() int32 Holder = INDEX_NONE;
};

USTRUCT()
struct FTutorialResearchSave
{
	GENERATED_BODY()
	UPROPERTY() FName Key;
	UPROPERTY() TArray<int32> Items;
	UPROPERTY() int32 Recipe = INDEX_NONE;
	UPROPERTY() float Remaining = 0.f;
	UPROPERTY() float Total = 0.f;
	UPROPERTY() bool bRunning = false;
};

USTRUCT()
struct FTutorialCookerSave
{
	GENERATED_BODY()
	UPROPERTY() FName Key;
	UPROPERTY() TMap<int32, int32> Materials;
	UPROPERTY() int32 MenuID = -1;
	UPROPERTY() float Time = 0.f;
	UPROPERTY() float OverTime = 0.f;
	UPROPERTY() bool bCooking = false;
	UPROPERTY() bool bFinished = false;
	UPROPERTY() bool bFire = false;
};

USTRUCT()
struct FTutorialDaySave
{
	GENERATED_BODY()
	UPROPERTY() TMap<int32, int32> Steps;
	UPROPERTY() int32 Served = 0;
	UPROPERTY() bool bOpened = false;
};

USTRUCT()
struct FTutorialWorldSave
{
	GENERATED_BODY()
	UPROPERTY() int32 Day = 0;
	UPROPERTY() FString Map;
	UPROPERTY() TArray<FTutorialContainerSave> Warehouses;
	UPROPERTY() TArray<FTutorialContainerSave> Baskets;
	UPROPERTY() TArray<FTutorialContainerSave> Backpacks;
	UPROPERTY() TArray<FTutorialItemSave> Items;
	UPROPERTY() TArray<FTutorialResearchSave> Research;
	UPROPERTY() TArray<FTutorialCookerSave> Cookers;
};
