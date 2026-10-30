// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BaseFoodActor.h"
#include "Data/MenuData.h"
#include "Data/ItemActorInfo.h"
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

	/** 物品表（DT_FoodID / DT_ItemInfo，RowStruct = FItemActorInfo）：键为 ItemID（优先取行名）。
	 *  食材图标 / 描述都从这里取，菜品表只配成品图标 MenuIcon。 */
	TMap<int32,FItemActorInfo*> ItemTables;

	/** 已经建过索引的那张表（用于表被换掉后自动重建索引） */
	TObjectPtr<UDataTable> IndexedItemInfoTable = nullptr;
public:
	/** 物品信息表（DT_ItemInfo，RowStruct = FItemActorInfo）：食材图标/描述按 ItemID 从这里取。
	 *  直接改这个属性后，下次取图标会自动重建索引（也可显式调 InitItemInfoTable） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	TObjectPtr<UDataTable> ItemInfoTable = nullptr;

	void InitMenuData(UDataTable* MenuDataTable);

	/** 初始化物品表（DT_FoodID / DT_ItemInfo）；传 null 时清空索引（食材图标会取不到） */
	UFUNCTION(BlueprintCallable)
	void InitItemInfoTable(UDataTable* ItemDataTable);

	UFUNCTION(BlueprintCallable)
	FMenuData GetMenuData(int32 FoodID);

	/** 按 ItemID 取物品/食材图标（DT_FoodID / DT_ItemInfo 的 Icon）；取不到时返回 null */
	UFUNCTION(BlueprintCallable, BlueprintPure)
	UTexture2D* GetItemIcon(int32 ItemID);

	/** 按 ItemID 取食材图标（FItemActorInfo::Icon，与 GetItemIcon 同一字段）；取不到时返回 null */
	UFUNCTION(BlueprintCallable, BlueprintPure)
	UTexture2D* GetItemFoodIcon(int32 ItemID);

	/** 按 FoodID 取菜品成品图标（菜品表 MenuIcon）；没配 MenuIcon 时退回第一个食材的图标 */
	UFUNCTION(BlueprintCallable, BlueprintPure)
	UTexture2D* GetFoodIcon(int32 FoodID);

	/** 按 FoodID + 食材 ItemID 取该食材的图标（FItemActorInfo::Icon）；取不到时返回 null */
	UFUNCTION(BlueprintCallable, BlueprintPure)
	UTexture2D* GetFoodItemIcon(int32 FoodID, int32 ItemID);

	/** 该菜品用到的食材 ItemID 列表（MenuItems 的键，与 GetFoodItemIcon 的 ItemID 对应） */
	UFUNCTION(BlueprintCallable, BlueprintPure)
	TArray<int32> GetFoodItemIDs(int32 FoodID) const;
	/** 按机器 ID 取可制作的菜品 ID 列表：菜品的 MachineID 为 0 视为通用；
	 *  MachineID<=0 时返回全部菜品 */
	UFUNCTION(BlueprintCallable, BlueprintPure)
	TArray<int32> GetFoodIDsByMachine(int32 MachineID) const;
	UFUNCTION(BlueprintCallable)
	ABaseFoodActor* CreateFoodActor(int32 FoodID);

private:
	/** 按 ItemID 取物品行（ItemInfoTable 被换掉会自动重建索引）；取不到返回 null */
	FItemActorInfo* FindItemRow(int32 ItemID);
};
