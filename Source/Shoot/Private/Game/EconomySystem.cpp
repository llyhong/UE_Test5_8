// Fill out your copyright notice in the Description page of Project Settings.

#include "Game/EconomySystem.h"
#include "Game/BaseGameInstance.h"
#include "MySaveGame.h"

void UEconomySystem::Init()
{
	ResolvedConfig = DefaultConfig;
	if (ConfigTable)
	{
		if (const FEconomyConfigRow* Row = ConfigTable->FindRow<FEconomyConfigRow>(ConfigRowName, TEXT("EconomySystem::Init")))
		{
			ResolvedConfig = *Row;
		}
	}
	Load();
}

void UEconomySystem::Load()
{
	if (const UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetOuter()))
	{
		if (GI->curSaveGameData)
		{
			Money = GI->curSaveGameData->Money;
			Reputation = GI->curSaveGameData->Reputation;
			OwnedProps = GI->curSaveGameData->PropMap;
		}
	}
}

void UEconomySystem::Save()
{
	if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetOuter()))
	{
		if (GI->curSaveGameData)
		{
			GI->curSaveGameData->Money = Money;
			GI->curSaveGameData->Reputation = Reputation;
			GI->curSaveGameData->PropMap = OwnedProps;
			GI->SaveGameData();
		}
	}
}

void UEconomySystem::OnOrderCompleted(int32 OrderPrice)
{
	AddReputation(ResolvedConfig.ReputationGainOnComplete);

	int32 Gain = ResolvedConfig.MoneyPerOrder + FMath::RoundToInt(OrderPrice * ResolvedConfig.MoneyPriceMultiplier);
	Gain = FMath::Max(Gain, ResolvedConfig.MinMoneyOnComplete);
	AddMoney(Gain, EMoneySource::OrderComplete);
}

void UEconomySystem::OnOrderFailed()
{
	AddReputation(-ResolvedConfig.ReputationLossOnFail);
}

void UEconomySystem::AddReputation(int32 Delta)
{
	Reputation = FMath::Max(0, Reputation + Delta);
	OnReputationChanged.Broadcast(Reputation, Delta);
	Save();
}

void UEconomySystem::AddMoney(int32 Delta, EMoneySource Source)
{
	Money = FMath::Max(0, Money + Delta);
	OnMoneyChanged.Broadcast(Money, Delta, Source);
	UE_LOG(LogTemp, Warning, TEXT("AddMoney: %d"), Money);
	Save();
}

bool UEconomySystem::SpendMoney(int32 Amount, EMoneySource Source)
{
	if (Amount <= 0)
	{
		return true;
	}
	if (Money < Amount)
	{
		return false;
	}
	Money -= Amount;
	OnMoneyChanged.Broadcast(Money, -Amount, Source);
	Save();
	return true;
}

bool UEconomySystem::BuyItem(int32 ItemID, int32 Cost)
{
	if (!SpendMoney(Cost, EMoneySource::ShopPurchase))
	{
		return false;
	}
	OwnedProps.FindOrAdd(ItemID)++;
	Save();
	return true;
}

int32 UEconomySystem::GetOwnedItemCount(int32 ItemID) const
{
	const int32* p = OwnedProps.Find(ItemID);
	return p ? *p : 0;
}

TArray<FEconomyShopItem> UEconomySystem::GetShopItems() const
{
	TArray<FEconomyShopItem> Result;
	if (ShopItemsTable)
	{
		static const FString Ctx(TEXT("EconomySystem::GetShopItems"));
		for (const TPair<FName, uint8*>& Row : ShopItemsTable->GetRowMap())
		{
			if (const FEconomyShopItem* Item = reinterpret_cast<FEconomyShopItem*>(Row.Value))
			{
				Result.Add(*Item);
			}
		}
	}
	else
	{
		Result = DefaultShopItems;
	}
	return Result;
}
