// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/ChairTipsWidget.h"
#include "NPC/ChairSeat.h"
#include "Item/ItemActor.h"
#include "Game/OrderSystem.h"
#include "Game/BaseGameInstance.h"
#include "Game/FoodSystem.h"

void UChairTipsWidget::RefreshTips()
{
	// 事件驱动刷新时重置累积时间，避免紧接着又自动刷一次
	RefreshAccum = 0.f;
	OnTipsRefreshed();
}

void UChairTipsWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// 倒计时 / 订单剩余时间需要连续刷新时按间隔驱动；<=0 表示只由餐桌事件驱动
	if (AutoRefreshInterval <= 0.f || !OwnerSeat)
	{
		return;
	}

	RefreshAccum += InDeltaTime;
	if (RefreshAccum >= AutoRefreshInterval)
	{
		RefreshAccum = 0.f;
		OnTipsRefreshed();
	}
}

EChairTipsState UChairTipsWidget::GetSeatState() const
{
	return OwnerSeat ? OwnerSeat->GetSeatState() : EChairTipsState::Empty;
}

FText UChairTipsWidget::GetSeatStateText() const
{
	// 默认中文文案：蓝图可直接用，也可以按状态换自己的美术字
	switch (GetSeatState())
	{
	case EChairTipsState::Seated:
		return FText::FromString(TEXT("入座中…"));
	case EChairTipsState::WaitingOrder:
		return FText::FromString(TEXT("等待点单"));
	case EChairTipsState::WaitingFood:
		return FText::FromString(TEXT("等待上菜"));
	case EChairTipsState::Eating:
		return FText::FromString(TEXT("用餐中"));
	case EChairTipsState::DirtyPlate:
		return FText::FromString(TEXT("待收空碟"));
	case EChairTipsState::Empty:
	default:
		return FText::FromString(TEXT("空闲"));
	}
}

bool UChairTipsWidget::HasOrder() const
{
	return OwnerSeat && OwnerSeat->HasOrder();
}

UOrderHandle* UChairTipsWidget::GetOrderHandle() const
{
	return OwnerSeat ? OwnerSeat->BoundOrder : nullptr;
}

FOrderData UChairTipsWidget::GetOrderData() const
{
	const UOrderHandle* Handle = GetOrderHandle();
	return Handle ? Handle->GetOrderData() : FOrderData();
}

TArray<int32> UChairTipsWidget::GetFoodIDs() const
{
	if (!OwnerSeat)
	{
		return TArray<int32>();
	}

	// 座位上的副本优先（点单时就从订单拷过来了），异常情况下退回订单本身
	return OwnerSeat->BoundFoodIDs.Num() > 0 ? OwnerSeat->BoundFoodIDs : GetOrderData().FoodIDs;
}

TArray<FChairMenuEntry> UChairTipsWidget::GetMenuEntries() const
{
	TArray<FChairMenuEntry> Entries;
	for (int32 FoodID : GetFoodIDs())
	{
		FChairMenuEntry* Found = Entries.FindByPredicate(
			[FoodID](const FChairMenuEntry& Entry) { return Entry.FoodID == FoodID; });

		if (!Found)
		{
			Found = &Entries.AddDefaulted_GetRef();
			Found->FoodID = FoodID;
			Found->FoodName = GetFoodName(FoodID);
			Found->Icon = GetFoodIcon(FoodID);
		}
		Found->Count++;
	}

	// 拼好显示文本：单份直接显示菜名，多份追加 "xN"
	for (FChairMenuEntry& Entry : Entries)
	{
		Entry.DisplayText = (Entry.Count > 1)
			? FText::FromString(FString::Printf(TEXT("%s x%d"), *Entry.FoodName, Entry.Count))
			: FText::FromString(Entry.FoodName);
	}
	return Entries;
}

FString UChairTipsWidget::GetSetName() const
{
	if (OwnerSeat)
	{
		return OwnerSeat->BoundSetName;
	}
	return GetOrderData().SetName;
}

int32 UChairTipsWidget::GetOrderPrice() const
{
	return GetOrderData().Price;
}

float UChairTipsWidget::GetRemainPercent() const
{
	UOrderHandle* Handle = GetOrderHandle();
	return Handle ? FMath::Clamp(Handle->GetPercent(), 0.f, 1.f) : 0.f;
}

float UChairTipsWidget::GetRemainSeconds() const
{
	const UOrderHandle* Handle = GetOrderHandle();
	if (!Handle)
	{
		return 0.f;
	}
	return FMath::Max(0.f, Handle->OrderData.MaxWaitTime - Handle->waitTime);
}

int32 UChairTipsWidget::GetOrderImageID() const
{
	return GetOrderData().IamgeID;
}

TArray<int32> UChairTipsWidget::GetMachineImageIDs() const
{
	return GetOrderData().MachineImageID;
}

FString UChairTipsWidget::GetFoodName(int32 FoodID) const
{
	if (FoodID >= 0)
	{
		if (const UWorld* World = GetWorld())
		{
			if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(World->GetGameInstance()))
			{
				if (GI->foodSystem)
				{
					const FMenuData MenuData = GI->foodSystem->GetMenuData(FoodID);
					if (!MenuData.FoodName.IsEmpty())
					{
						return MenuData.FoodName;
					}
				}
			}
		}
	}
	return FString::Printf(TEXT("菜品%d"), FoodID);
}

UTexture2D* UChairTipsWidget::GetFoodIcon(int32 FoodID) const
{
	if (FoodID >= 0)
	{
		if (const UWorld* World = GetWorld())
		{
			if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(World->GetGameInstance()))
			{
				if (GI->foodSystem)
				{
					return GI->foodSystem->GetFoodIcon(FoodID);
				}
			}
		}
	}
	return nullptr;
}

AItemActor* UChairTipsWidget::GetPlateOnSeat() const
{
	return OwnerSeat ? OwnerSeat->PlateOnSeat : nullptr;
}
