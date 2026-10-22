// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/ShopWidget.h"
#include "Game/EconomySystem.h"
#include "ShootCharacter.h"

void UShopWidget::BindEconomy(UEconomySystem* Eco)
{
	Economy = Eco;
	if (Eco)
	{
		Eco->OnMoneyChanged.AddDynamic(this, &UShopWidget::HandleMoneyChanged);
	}
}

TArray<FEconomyShopItem> UShopWidget::GetShopItems() const
{
	return Economy ? Economy->GetShopItems() : TArray<FEconomyShopItem>();
}

bool UShopWidget::BuyShopItem(int32 ItemID)
{
	if (!Economy)
	{
		return false;
	}
	// 从配置列表里找到该商品的价格
	TArray<FEconomyShopItem> Items = Economy->GetShopItems();
	for (const FEconomyShopItem& Item : Items)
	{
		if (Item.ItemID == ItemID)
		{
			const bool bOk = Economy->BuyItem(ItemID, Item.Cost);
			if (bOk)
			{
				RefreshShop();
			}
			return bOk;
		}
	}
	return false;
}

int32 UShopWidget::GetOwnedItemCount(int32 ItemID) const
{
	return Economy ? Economy->GetOwnedItemCount(ItemID) : 0;
}

int32 UShopWidget::GetMoney() const
{
	return Economy ? Economy->GetMoney() : 0;
}

void UShopWidget::HandleMoneyChanged(int32 NewValue, int32 Delta, EMoneySource Source)
{
	RefreshShop();
}

void UShopWidget::CloseShop()
{
	RemoveFromParent();
}
