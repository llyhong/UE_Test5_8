// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InventoryWidget.generated.h"

class AShootCharacter;

/**
 * 背包界面基类：在编辑器里用 WBP 继承它即可。
 * 打开时由 ShootCharacter 设置 OwnerCharacter，并调用 RefreshInventory 刷新格子。
 */
UCLASS()
class SHOOT_API UInventoryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 背包界面的所属角色(ShootCharacter 打开时赋值)
	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	TObjectPtr<AShootCharacter> OwnerCharacter;

	// 刷新背包显示：由蓝图实现，从 OwnerCharacter->GetInventoryItems() 读取槽位填充格子
	UFUNCTION(BlueprintImplementableEvent, Category = "Inventory")
	void RefreshInventory();

	// 关闭背包界面(蓝图按钮可调用)
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void CloseInventory();
};
