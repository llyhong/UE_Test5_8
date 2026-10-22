// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BaseFoodActor.h"
#include "Data/MenuData.h"
#include "UObject/NoExportTypes.h"
#include "FoodSystem.generated.h"

/**
 * 
 */
UCLASS()
class SHOOT_API UFoodSystem : public UObject
{
	GENERATED_BODY()

	TMap<int32,FMenuData*> MenuTables;
public:
	void InitMenuData(UDataTable* MenuDataTable);
	UFUNCTION(BlueprintCallable)
	FMenuData GetMenuData(int32 Index);
	UFUNCTION(BlueprintCallable)
	ABaseFoodActor* CreateFoodActor(int32 FoodID);
};
