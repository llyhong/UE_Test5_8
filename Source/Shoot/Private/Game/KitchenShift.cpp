#include "Game/KitchenShift.h"
#include "Game/OrderSystem.h"
#include "Game/BaseGameInstance.h"
#include "Game/BusinessSimulation.h"
#include "Game/BusinessTime.h"
#include "Data/MenuData.h"
#include "Kismet/GameplayStatics.h"
#include "Game/FoodSystem.h"

TArray<FKitchenShiftProgress::FGoal> FKitchenShiftProgress::GetGoals() const
{
	if (!IsActive()) return {};

	TArray<FGoal> Goals = {{NSLOCTEXT("KitchenShift", "Orders", "完成订单"), Served,
		Type == EKitchenShift::TwoStations ? 7 : 8}};
	switch (Type)
	{
	case EKitchenShift::Rush:
		Goals.Add({NSLOCTEXT("KitchenShift", "Streak", "最高准时连单"), BestStreak, 4});
		Goals.Add(bPremiumGoal
			? FGoal{NSLOCTEXT("KitchenShift", "Premium", "端出佳品"), Premium, 2}
			: FGoal{NSLOCTEXT("KitchenShift", "OnTime", "准时上菜"), OnTime, 6});
		break;
	case EKitchenShift::TwoStations:
		Goals.Add({NSLOCTEXT("KitchenShift", "OnTime", "准时上菜"), OnTime, 5});
		if (DishIDs.Num() >= 2 && DishNames.Num() >= 2)
		{
			const int32 PairsServed = FMath::Min(DishesServed.FindRef(DishIDs[0]), DishesServed.FindRef(DishIDs[1]));
			Goals.Add({FText::Format(NSLOCTEXT("KitchenShift", "TwoDishGoal", "{0}、{1}各上菜"),
				FText::FromString(DishNames[0]), FText::FromString(DishNames[1])), PairsServed, 2});
		}
		else
		{
			Goals.Add({NSLOCTEXT("KitchenShift", "Streak", "最高准时连单"), BestStreak, 3});
		}
		break;
	case EKitchenShift::PlateTurnover:
		Goals.Add({NSLOCTEXT("KitchenShift", "OnTime", "准时上菜"), OnTime, 6});
		Goals.Add({NSLOCTEXT("KitchenShift", "Wash", "送回洗碗机的盘子"), PlatesWashed, 5});
		break;
	default:
		break;
	}
	return Goals;
}

int32 FKitchenShiftProgress::GetStars() const
{
	int32 Stars = 0;
	for (const FGoal& Goal : GetGoals())
	{
		if (Goal.Value >= Goal.Target) ++Stars;
	}
	return Stars;
}

FText FKitchenShiftProgress::Format(bool bSettlement, bool bBeforeOpening) const
{
	if (!IsActive()) return FText::GetEmpty();

	FText Title;
	FText Change;
	switch (Type)
	{
	case EKitchenShift::Rush:
		Title = NSLOCTEXT("KitchenShift", "Rush", "高峰连单");
		Change = NSLOCTEXT("KitchenShift", "RushChange", "高峰提早，客人来得更密");
		break;
	case EKitchenShift::TwoStations:
		Title = NSLOCTEXT("KitchenShift", "TwoStations", "双灶协作");
		Change = DishIDs.Num() >= 2
			? NSLOCTEXT("KitchenShift", "TwoStationsChange", "两道招牌菜交替来单")
			: NSLOCTEXT("KitchenShift", "OneDishChange", "单菜营业，挑战连续准时");
		break;
	case EKitchenShift::PlateTurnover:
		Title = NSLOCTEXT("KitchenShift", "PlateTurnover", "餐盘周转");
		Change = NSLOCTEXT("KitchenShift", "PlateTurnoverChange", "来客间隔更均匀，留空收盘送洗");
		break;
	default:
		break;
	}

	const TArray<FGoal> Goals = GetGoals();
	const int32 Stars = GetStars();
	const FText Rating = FText::FromString(FString::ChrN(Stars, TEXT('★')) + FString::ChrN(3 - Stars, TEXT('☆')));
	FString Text = FText::Format(NSLOCTEXT("KitchenShift", "Title", "第 {0} 班 · {1}   {2}"), Day - 3, Title, Rating).ToString();
	if (bBeforeOpening) Text += TEXT("\n") + Change.ToString();
	for (int32 Index = 0; Index < Goals.Num(); ++Index)
	{
		const FGoal& Goal = Goals[Index];
		const int32 Remaining = FMath::Max(0, Goal.Target - Goal.Value);
		Text += bSettlement && Index > 0 ? TEXT("   ·   ") : TEXT("\n");
		Text += bSettlement
			? FText::Format(Remaining == 0 ? NSLOCTEXT("KitchenShift", "Met", "✓ {0} {1}/{2}")
				: NSLOCTEXT("KitchenShift", "Missing", "{0} {1}/{2}（还差 {3}）"),
				Goal.Label, Goal.Value, Goal.Target, Remaining).ToString()
			: FText::Format(NSLOCTEXT("KitchenShift", "Progress", "{0} {1}/{2}"), Goal.Label, Goal.Value, Goal.Target).ToString();
	}
	return FText::FromString(Text);
}

namespace
{
	const UTimeSystem* GetShiftClock(const AOrderSystem& Orders)
	{
		const UBaseGameInstance* GI = Cast<UBaseGameInstance>(Orders.GetGameInstance());
		return GI && GI->businessSimulation ? GI->businessSimulation->TimeSystem.Get() : nullptr;
	}
}

EKitchenShift AOrderSystem::GetKitchenShiftType() const
{
	const int32 Day = GetCurrentBusinessDay();
	if (Day < 4 || UGameplayStatics::GetCurrentLevelName(this) != TEXT("Kitchen_New"))
		return EKitchenShift::None;

	switch ((Day - 4) % 3)
	{
	case 0: return EKitchenShift::Rush;
	case 1: return EKitchenShift::TwoStations;
	default: return EKitchenShift::PlateTurnover;
	}
}

void AOrderSystem::RefreshKitchenShift()
{
	const int32 Day = GetCurrentBusinessDay();
	if (KitchenShift.Day != Day)
	{
		KitchenShift = {};
		KitchenShift.Day = Day;
		KitchenShift.Type = GetKitchenShiftType();
		bKitchenShiftMenuLocked = false;
		KitchenShiftMenuCursor = 0;
	}

	const UTimeSystem* Clock = GetShiftClock(*this);
	if (KitchenShift.IsActive() && !bKitchenShiftMenuLocked && Clock && Clock->IsBusinessOpen())
	{
		// Opening is the last chance to pick goals based on the current menu and stock.
		RebuildOrderPool();
	}
}

void AOrderSystem::UpdateKitchenShiftMenu()
{
	if (!KitchenShift.IsActive() || bKitchenShiftMenuLocked) return;

	const UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance());
	if (!GI || !GI->foodSystem) return;

	KitchenShift.DishIDs.Reset();
	KitchenShift.DishNames.Reset();
	KitchenShift.bPremiumGoal = false;

	TArray<int32> Candidates;
	for (const FOrderData& Order : OrderPool)
	{
		if (Order.FoodIDs.Num() == 1) Candidates.AddUnique(Order.FoodIDs[0]);
	}
	// Use eggs and stewed tomato first when both are on today's menu.
	Candidates.Sort();
	int32 FirstMachine = INDEX_NONE;
	for (const int32 ID : Candidates)
	{
		const FMenuData Dish = GI->foodSystem->GetMenuData(ID);
		if (Dish.bRequiresAttention && KitchenShift.Day >= Dish.AttentionUnlockDay)
		{
			KitchenShift.bPremiumGoal = true;
		}
		if (KitchenShift.DishIDs.IsEmpty() || (KitchenShift.DishIDs.Num() == 1 && Dish.MachineID != FirstMachine))
		{
			if (KitchenShift.DishIDs.IsEmpty()) FirstMachine = Dish.MachineID;
			KitchenShift.DishIDs.Add(ID);
			KitchenShift.DishNames.Add(Dish.FoodName);
		}
	}
	if (KitchenShift.DishIDs.Num() == 1)
	{
		const int32* Alternative = Candidates.FindByPredicate([this](int32 ID) { return ID != KitchenShift.DishIDs[0]; });
		if (Alternative)
		{
			KitchenShift.DishIDs.Add(*Alternative);
			KitchenShift.DishNames.Add(GI->foodSystem->GetMenuData(*Alternative).FoodName);
		}
	}

	const UTimeSystem* Clock = GetShiftClock(*this);
	bKitchenShiftMenuLocked = Clock && Clock->IsBusinessOpen();
}

bool AOrderSystem::TryBuildKitchenShiftOrder(FOrderData& OutOrder)
{
	// Keep the early orders varied while respecting the menu. If a special
	// runs out, the normal order pool can keep service moving.
	if (KitchenShift.Type != EKitchenShift::TwoStations && KitchenShift.Type != EKitchenShift::Rush)
		return false;
	if (KitchenShift.DishIDs.Num() < 2 || KitchenShiftMenuCursor >= 8) return false;

	const int32 ID = KitchenShift.DishIDs[KitchenShiftMenuCursor++ % KitchenShift.DishIDs.Num()];
	const FOrderData* Available = OrderPool.FindByPredicate([ID](const FOrderData& Order)
	{
		return Order.FoodIDs.Num() == 1 && Order.FoodIDs[0] == ID;
	});
	if (!Available) return false;
	OutOrder = *Available;
	return true;
}

void AOrderSystem::RecordKitchenShiftOrder(const UOrderHandle& Order, bool bPremiumDish)
{
	RefreshKitchenShift();
	const UTimeSystem* Clock = GetShiftClock(*this);
	if (!KitchenShift.IsActive() || !Clock || Clock->IsEndOfDayReached()) return;

	++KitchenShift.Served;
	if (!Order.IsLate())
	{
		++KitchenShift.OnTime;
		KitchenShift.BestStreak = FMath::Max(KitchenShift.BestStreak, OnTimeStreak);
	}
	if (bPremiumDish) ++KitchenShift.Premium;
	for (int32 ID : Order.GetOrderData().FoodIDs) ++KitchenShift.DishesServed.FindOrAdd(ID);
}

void AOrderSystem::RecordKitchenShiftPlate()
{
	RefreshKitchenShift();
	const UTimeSystem* Clock = GetShiftClock(*this);
	if (KitchenShift.IsActive() && Clock && Clock->IsBusinessOpen())
	{
		++KitchenShift.PlatesWashed;
	}
}
