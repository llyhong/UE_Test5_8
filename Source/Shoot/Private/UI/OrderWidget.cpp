// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/OrderWidget.h"
#include "Game/BaseGameInstance.h"
#include "Game/BaseLevelBP.h"
#include "Game/EconomySystem.h"

void UOrderWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 先试一次；订单系统可能由关卡蓝图稍后创建，未就绪时交给 Tick 轮询
	TryBindPending();
}

void UOrderWidget::NativeDestruct()
{
	if (Economy)
	{
		Economy->OnReputationChanged.RemoveDynamic(this, &UOrderWidget::HandleReputationChanged);
		Economy = nullptr;
	}
	UnbindOrderSystem();
	Super::NativeDestruct();
}

void UOrderWidget::BindEconomy(UEconomySystem* Eco)
{
	Economy = Eco;
	if (Eco)
	{
		Eco->OnReputationChanged.AddDynamic(this, &UOrderWidget::HandleReputationChanged);
		// 立即用当前值刷新一次
		HandleReputationChanged(Eco->GetReputation(), 0);
	}
}

int32 UOrderWidget::GetReputation() const
{
	return Economy ? Economy->GetReputation() : 0;
}

int32 UOrderWidget::GetMoney() const
{
	return Economy ? Economy->GetMoney() : 0;
}

void UOrderWidget::HandleReputationChanged(int32 NewValue, int32 Delta)
{
	OnReputationUpdated(NewValue);
}

void UOrderWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// 订单系统尚未就绪：按间隔轮询，直到关卡蓝图创建出订单系统
	if (!OrderSystem)
	{
		BindRetryAccum += InDeltaTime;
		if (BindRetryAccum >= BindRetryInterval)
		{
			BindRetryAccum = 0.f;
			TryBindPending();
		}
		return;
	}

	// 有订单时通知蓝图刷新进度（倒计时 / 进度条）
	if (WaitingOrders.Num() > 0)
	{
		RefreshOrderProgress();
	}
}

void UOrderWidget::BindOrderSystem(AOrderSystem* InOrderSystem)
{
	if (OrderSystem == InOrderSystem)
	{
		return;
	}

	UnbindOrderSystem();

	OrderSystem = InOrderSystem;
	if (OrderSystem)
	{
		OrderSystem->OnStartOrder.AddDynamic(this, &UOrderWidget::HandleOrderStarted);
		OrderSystem->OnFinishOrder.AddDynamic(this, &UOrderWidget::HandleOrderFinished);
		OrderSystem->OnFailOrder.AddDynamic(this, &UOrderWidget::HandleOrderFailed);
	}

	RebuildWaitingOrders();
}

void UOrderWidget::UnbindOrderSystem()
{
	if (OrderSystem)
	{
		OrderSystem->OnStartOrder.RemoveDynamic(this, &UOrderWidget::HandleOrderStarted);
		OrderSystem->OnFinishOrder.RemoveDynamic(this, &UOrderWidget::HandleOrderFinished);
		OrderSystem->OnFailOrder.RemoveDynamic(this, &UOrderWidget::HandleOrderFailed);
	}

	OrderSystem = nullptr;
	WaitingOrders.Reset();
	RefreshOrderList(WaitingOrders);
}

void UOrderWidget::ClearAllOrdersUI()
{
	// 打烊清空：直接清空本地订单缓存，并触发增量“全部清空”事件，
	// 蓝图据此一次性销毁所有订单子控件（不走整表重建）。
	WaitingOrders.Reset();
	OnOrdersCleared();
}

void UOrderWidget::TryBindPending()
{
	if (OrderSystem)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	UBaseGameInstance* GI = Cast<UBaseGameInstance>(World->GetGameInstance());
	if (!GI || !GI->curLevelBP)
	{
		return;
	}

	if (AOrderSystem* OS = GI->curLevelBP->orderSystem)
	{
		BindOrderSystem(OS);
	}
}

void UOrderWidget::RebuildWaitingOrders()
{
	WaitingOrders.Reset();

	if (OrderSystem)
	{
		WaitingOrders.Reserve(OrderSystem->waitList.Num());
		for (UOrderHandle* Handle : OrderSystem->waitList)
		{
			if (Handle)
			{
				WaitingOrders.Add(Handle);
			}
		}
	}

	RefreshOrderList(WaitingOrders);
}

void UOrderWidget::HandleOrderStarted(const UOrderHandle* Handle, const FOrderData& Order)
{
	if (Handle)
	{
		WaitingOrders.AddUnique(const_cast<UOrderHandle*>(Handle));
	}

	// 增量：仅通知新增，不再整表重建
	OnOrderAdded(Handle, Order);
}

void UOrderWidget::HandleOrderFinished(const UOrderHandle* Handle, const FOrderData& Order)
{
	WaitingOrders.RemoveAll([Handle](const UOrderHandle* Item)
	{
		return Item == Handle;
	});

	// 增量：仅通知移除，不再整表重建
	OnOrderFinished(Handle, Order);
}

void UOrderWidget::HandleOrderFailed(const UOrderHandle* Handle, const FOrderData& Order)
{
	WaitingOrders.RemoveAll([Handle](const UOrderHandle* Item)
	{
		return Item == Handle;
	});

	// 增量：仅通知移除，不再整表重建
	OnOrderFailed(Handle, Order);
}
