// Fill out your copyright notice in the Description page of Project Settings.

#include "Game/OrderSystem.h"
#include "Game/BaseLevelBP.h"

#include "BaseGameInstance.h"
#include "NPC/GuestCharacter.h"

// Sets default values
AOrderSystem::AOrderSystem()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
}

// Called when the game starts or when spawned
void AOrderSystem::BeginPlay()
{
	Super::BeginPlay();
	
}

void AOrderSystem::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 清空所有未完成订单：waitList 是 UPROPERTY，句柄会随本 Actor 一起被 GC 回收，无需 AddToRoot。
	waitList.Empty();
	// 解除所有蓝图委托绑定，避免退出时回调到已销毁的蓝图对象
	OnStartOrder.Clear();
	OnFinishOrder.Clear();
	OnFailOrder.Clear();
	Super::EndPlay(EndPlayReason);
}

// Called every frame
void AOrderSystem::Tick(float DeltaTime)
{
	if (IsStarted)
	{
		CurTime += DeltaTime;

		// —— 订单超时检查（无论订单来自定时生成还是客人点餐）——
		// 倒序遍历，便于超时后直接从列表移除
		for (int i = waitList.Num() - 1; i >= 0; i--)
		{
			UOrderHandle* orderHandle = waitList[i];
			if (!orderHandle)
			{
				continue;
			}

			orderHandle->waitTime += DeltaTime;
			if (orderHandle->waitTime >= orderHandle->OrderData.MaxWaitTime)
			{
				ABaseLevelBP::AddScore(this, -orderHandle->OrderData.Price);
				OnFailOrder.Broadcast(orderHandle, orderHandle->OrderData);
				GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("订单超时"));

				// 记录绑定客人后移除订单，并通知其离场（订单生命周期与客人绑定）
				AGuestCharacter* OrderedGuest = orderHandle->Guest;
				waitList.RemoveAt(i);

				if (OrderedGuest)
				{
					OrderedGuest->OnOrderFailed();
				}
			}
		}

		// —— 定时自动生成订单（默认关闭）——
		// 关闭后订单只在客人坐下点餐时产生（见 RequestOrderForGuest）。
		if (bAutoGenerateByTime && orderCfg && orderCfg->AppearTimes.Num() > 0)
		{
			int num = orderCfg->AppearTimes.Num();
			if (CurOrderIndex >= num)
			{
				CurOrderIndex = 0;
				CurTime = 0;
				GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("最后的订单了"));
				//最后订单		
			}
			else
			{
				auto nextOrder = orderCfg->AppearTimes[CurOrderIndex];
				if (CurTime >= nextOrder.AppearTime && waitList.Num() < orderCfg->MaxWaitOrderNum)
				{
					FOrderData* data = &orderCfg->Orders[nextOrder.AppearOrderID];
					UOrderHandle* orderHandle = NewObject<UOrderHandle>(this);
					orderHandle->orderId = OrderID;
					OrderID++;
					orderHandle->OrderData = *data;
					orderHandle->waitTime = 0;
					waitList.Add(orderHandle);
					OnStartOrder.Broadcast(orderHandle, orderHandle->OrderData);
					CurOrderIndex++;
				}
			}
		}
	}
	Super::Tick(DeltaTime);

}
void AOrderSystem::InitCfg(ULevelOrderCfg* cfg)
{
	orderCfg = cfg;
	waitList.Empty(orderCfg->Orders.Num());
	CurOrderIndex = 0;
	OrderID = 0;
}
void AOrderSystem::StartSystem()
{
	IsStarted = true;
}
void AOrderSystem::StopSystem()
{
	IsStarted = false;
	// for (int i = 0; i < waitList.Num(); i++)
	// {
	// 	auto handle = waitList[i];
	// 	handle->RemoveFromRoot();
	// }
	//waitList.Empty();
}
void AOrderSystem::ClearAllOrders()
{
	// 静默清空：不广播 OnFailOrder，避免切换关卡时误扣分数/声誉。
	// 句柄为本 Actor 创建的 UObject，随 waitList 清空后被 GC 回收。
	waitList.Empty();
}

FOrderData* AOrderSystem::GetRandomOrder()
{
	if (!orderCfg || orderCfg->Orders.Num() == 0)
	{
		return nullptr;
	}

	// 优先按 Probabilitys 加权随机；全为 0（或负）时退化为均匀随机
	float TotalWeight = 0.f;
	for (const FOrderData& O : orderCfg->Orders)
	{
		TotalWeight += FMath::Max(O.Probabilitys, 0.f);
	}

	if (TotalWeight <= 0.f)
	{
		return &orderCfg->Orders[FMath::RandRange(0, orderCfg->Orders.Num() - 1)];
	}

	float R = FMath::FRandRange(0.f, TotalWeight);
	for (FOrderData& O : orderCfg->Orders)
	{
		R -= FMath::Max(O.Probabilitys, 0.f);
		if (R <= 0.f)
		{
			return &O;
		}
	}
	return &orderCfg->Orders.Last();
}

UOrderHandle* AOrderSystem::RequestOrderForGuest(AGuestCharacter* Guest)
{
	if (!IsStarted || !Guest)
	{
		return nullptr;
	}

	FOrderData* data = GetRandomOrder();
	if (!data)
	{
		return nullptr;
	}

	// 客人点餐：生成订单并绑定该客人（订单生命周期 = 客人的等餐周期）
	UOrderHandle* orderHandle = NewObject<UOrderHandle>(this);
	orderHandle->orderId = OrderID;
	OrderID++;
	orderHandle->OrderData = *data;
	orderHandle->waitTime = 0.f;
	orderHandle->Guest = Guest;

	waitList.Add(orderHandle);
	OnStartOrder.Broadcast(orderHandle, orderHandle->OrderData);

	// 屏幕提示由客人侧（AGuestCharacter::PlaceOrder）统一打印，这里只留日志避免重复刷屏
	UE_LOG(LogTemp, Log, TEXT("[OrderSystem] 客人 %s 点餐：%s（等待 %.0fs）"),
		*Guest->GetName(), *data->SetName, data->MaxWaitTime);

	return orderHandle;
}

bool AOrderSystem::CancelOrderForGuest(AGuestCharacter* Guest)
{
	if (!Guest)
	{
		return false;
	}

	for (int i = 0; i < waitList.Num(); i++)
	{
		UOrderHandle* handle = waitList[i];
		if (handle && handle->Guest == Guest)
		{
			waitList.RemoveAt(i);
			return true;
		}
	}
	return false;
}

bool AOrderSystem::PushFoods(const TArray<int>& FoodIDs)
{
	if (!IsStarted)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Red	 , FString::Printf(TEXT("上菜失败，游戏已经结束")));
		return false;
	}
	const int Num = FoodIDs.Num();

	// 1. 收集所有完全匹配的候选订单（菜品集合与数量一致）
	TArray<UOrderHandle*> Candidates;
	for (UOrderHandle* handle : waitList)
	{
		const FOrderData& order = handle->OrderData;
		if (order.FoodIDs.Num() != Num)
		{
			continue;
		}
		// 统计 Foods 中每个 ID 的出现次数，与订单比对（避免菜品重复导致的误判）
		TMap<int, int> NeedCount;
		for (int id : order.FoodIDs)
		{
			NeedCount.FindOrAdd(id)++;
		}
		bool bMatch = true;
		for (int id : FoodIDs)
		{
			int* p = NeedCount.Find(id);
			if (!p || *p <= 0)
			{
				bMatch = false;
				break;
			}
			(*p)--;
		}
		if (bMatch)
		{
			Candidates.Add(handle);
		}
	}
	if (Candidates.Num() == 0)
	{
		return false;
	}

	// 2. 在候选中选剩余时间比例最少（GetPercent 最小，最紧急）的订单优先完成
	UOrderHandle* BestHandle = Candidates[0];
	float BestPercent = BestHandle->GetPercent();
	for (UOrderHandle* handle : Candidates)
	{
		const float Percent = handle->GetPercent();
		if (Percent < BestPercent)
		{
			BestPercent = Percent;
			BestHandle = handle;
		}
	}

	// 3. 完成该订单
	const FOrderData& order = BestHandle->OrderData;
	// 先取出绑定客人，避免移除句柄后访问
	AGuestCharacter* OrderedGuest = BestHandle->Guest;

	ABaseLevelBP::AddScore(this, order.Price);
	OnFinishOrder.Broadcast(BestHandle, BestHandle->OrderData);
	waitList.Remove(BestHandle);
	GEngine->AddOnScreenDebugMessage(-1, 2, FColor::Red, TEXT("完成订单(优先最短剩余时间)"));

	// 通知点餐客人开始进食（订单与客人绑定）
	if (OrderedGuest)
	{
		OrderedGuest->OnOrderServed();
	}
	return true;
}

float UOrderHandle::GetPercent()
{
	// 按值保存后无需判空；MaxWaitTime 为 0 时避免除零（返回 1 表示“刚下单”）
	if (OrderData.MaxWaitTime > 0.f)
	{
		return 1.f - waitTime / OrderData.MaxWaitTime;
	}
	return 1.f;
}

void UOrderHandle::Reset()
{
	waitTime = 0;
}

FOrderData UOrderHandle::GetOrderData() const
{
	return OrderData;
}
