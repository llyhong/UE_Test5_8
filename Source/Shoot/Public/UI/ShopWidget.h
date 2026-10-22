// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Game/EconomySystem.h"
#include "ShopWidget.generated.h"

class AShootCharacter;

/**
 * 商店界面基类：在编辑器里用 WBP 继承它即可实现购买 UI。
 * 打开时由 ShootCharacter 设置 OwnerCharacter 并 BindEconomy。
 * 蓝图用 GetShopItems() 生成商品条目，对每个商品调用 BuyShopItem(ItemID) 完成购买。
 */
UCLASS()
class SHOOT_API UShopWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 商店界面的所属角色(ShootCharacter 打开时赋值)
	UPROPERTY(BlueprintReadOnly, Category = "Shop")
	TObjectPtr<AShootCharacter> OwnerCharacter;

	// 绑定经济系统（创建后由角色调用）
	UFUNCTION(BlueprintCallable, Category = "Shop")
	void BindEconomy(UEconomySystem* Eco);

	// 返回配置的商品列表（蓝图用于生成商品条目）
	UFUNCTION(BlueprintCallable, Category = "Shop")
	TArray<FEconomyShopItem> GetShopItems() const;

	// 购买指定商品（按 ItemID 从配置表取价格）。成功返回 true
	UFUNCTION(BlueprintCallable, Category = "Shop")
	bool BuyShopItem(int32 ItemID);

	// 已拥有某道具的数量（展示已购次数）
	UFUNCTION(BlueprintCallable, Category = "Shop")
	int32 GetOwnedItemCount(int32 ItemID) const;

	// 当前金钱（按钮置灰/提示用）
	UFUNCTION(BlueprintCallable, Category = "Shop")
	int32 GetMoney() const;

	// 蓝图实现：刷新商品列表/金钱显示
	UFUNCTION(BlueprintImplementableEvent, Category = "Shop")
	void RefreshShop();

	// 关闭商店界面（蓝图按钮可调用）
	UFUNCTION(BlueprintCallable, Category = "Shop")
	void CloseShop();

protected:
	UFUNCTION()
	void HandleMoneyChanged(int32 NewValue, int32 Delta, EMoneySource Source);

	UPROPERTY()
	TObjectPtr<UEconomySystem> Economy;
};
