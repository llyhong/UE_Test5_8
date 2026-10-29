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

	/** 菜品表：键为 FoodID（优先取行名，行名不是数字时取行内的 FoodID 字段） */
	TMap<int32,FMenuData*> MenuTables;
public:
	void InitMenuData(UDataTable* MenuDataTable);
	UFUNCTION(BlueprintCallable)
	FMenuData GetMenuData(int32 FoodID);
	/** 按 FoodID 取菜品图标（菜品表的 Icon）；取不到时返回 null */
	UFUNCTION(BlueprintCallable, BlueprintPure)
	UTexture2D* GetFoodIcon(int32 FoodID);
	/** 按机器 ID 取可制作的菜品 ID 列表：菜品的 MachineID 为 0 视为通用；
	 *  MachineID<=0 时返回全部菜品 */
	UFUNCTION(BlueprintCallable, BlueprintPure)
	TArray<int32> GetFoodIDsByMachine(int32 MachineID) const;
	UFUNCTION(BlueprintCallable)
	ABaseFoodActor* CreateFoodActor(int32 FoodID);
};
