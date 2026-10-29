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

	// 菜单内容（菜品 / 桌况）真变了才让蓝图重建列表与图标
	CheckMenuContentChanged();

	// 全量刷新：状态 / 进度（蓝图建议不再在这里重建菜单）
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

		// 周期刷新只推进度条 / 倒计时；只有内容真的变了才顺带让蓝图重建菜单
		CheckMenuContentChanged();
		OnTipsTick();
	}
}

int32 UChairTipsWidget::GetMenuSignature() const
{
	// 桌况 + 菜品 ID 序列（含顺序与重复次数）决定菜单内容是否变化
	uint32 Hash = ::GetTypeHash(static_cast<uint8>(GetSeatState()));
	for (const int32 FoodID : GetFoodIDs())
	{
		Hash = HashCombine(Hash, ::GetTypeHash(FoodID));
	}
	return static_cast<int32>(Hash);
}

void UChairTipsWidget::InvalidateMenuCache()
{
	bMenuCacheValid = false;
}

void UChairTipsWidget::CheckMenuContentChanged()
{
	const int32 NewSignature = GetMenuSignature();
	if (bMenuCacheValid && CachedMenuSignature == NewSignature)
	{
		return;
	}

	RebuildMenuCache(NewSignature);
	OnMenuContentChanged();
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

void UChairTipsWidget::RebuildMenuCache(int32 NewSignature) const
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
			// 逐个食材的图标：ItemIDs 与 ItemIcons 一一对应（键与 MenuItems 的 ItemID 相同）
			Found->ItemIDs = GetFoodItemIDs(FoodID);
			for (int32 ItemID : Found->ItemIDs)
			{
				Found->ItemIcons.Add(GetFoodItemIcon(FoodID, ItemID));
			}
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

	CachedMenuEntries = MoveTemp(Entries);
	CachedMenuSignature = NewSignature;
	bMenuCacheValid = true;
}

TArray<FChairMenuEntry> UChairTipsWidget::GetMenuEntries() const
{
	// 签名没变就直接复用缓存：高频刷新时不会重复查菜品表 / 取图标
	const int32 Signature = GetMenuSignature();
	if (!bMenuCacheValid || CachedMenuSignature != Signature)
	{
		RebuildMenuCache(Signature);
	}
	return CachedMenuEntries;
}

FString UChairTipsWidget::GetSetName() const
{
	if (OwnerSeat)
	{
		return OwnerSeat->BoundSetName;
	}
	return GetOrderHandle() ? GetOrderHandle()->GetOrderDisplayName() : FString();
}

int32 UChairTipsWidget::GetOrderPrice() const
{
	return GetOrderHandle() ? GetOrderHandle()->GetOrderPrice() : 0;
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
	return FMath::Max(0.f, Handle->GetOrderMaxWaitTime() - Handle->waitTime);
}

UTexture2D* UChairTipsWidget::GetOrderIcon() const
{
	const TArray<int32> FoodIDs = GetFoodIDs();
	return FoodIDs.Num() > 0 ? GetFoodIcon(FoodIDs[0]) : nullptr;
}

TArray<int32> UChairTipsWidget::GetMachineImageIDs() const
{
	return GetOrderHandle() ? GetOrderHandle()->GetMachineImageIDs() : TArray<int32>();
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

UTexture2D* UChairTipsWidget::GetFoodItemIcon(int32 FoodID, int32 ItemID) const
{
	if (FoodID >= 0)
	{
		if (const UWorld* World = GetWorld())
		{
			if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(World->GetGameInstance()))
			{
				if (GI->foodSystem)
				{
					return GI->foodSystem->GetFoodItemIcon(FoodID, ItemID);
				}
			}
		}
	}
	return nullptr;
}

TArray<int32> UChairTipsWidget::GetFoodItemIDs(int32 FoodID) const
{
	if (FoodID >= 0)
	{
		if (const UWorld* World = GetWorld())
		{
			if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(World->GetGameInstance()))
			{
				if (GI->foodSystem)
				{
					return GI->foodSystem->GetFoodItemIDs(FoodID);
				}
			}
		}
	}
	return TArray<int32>();
}

AItemActor* UChairTipsWidget::GetPlateOnSeat() const
{
	return OwnerSeat ? OwnerSeat->PlateOnSeat : nullptr;
}
