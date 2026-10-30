// Fill out your copyright notice in the Description page of Project Settings.

#include "Game/OrderSystem.h"
#include "Game/BaseLevelBP.h"

#include "BaseGameInstance.h"
#include "NPC/GuestCharacter.h"
#include "Game/FoodSystem.h"
#include "Data/MenuData.h"

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
			const float MaxWait = orderHandle->GetOrderMaxWaitTime();
			if (MaxWait > 0.f && orderHandle->waitTime >= MaxWait)
			{
				ABaseLevelBP::AddScore(this, -orderHandle->GetOrderPrice());
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
					// 定时生成的订单同样从"按菜品表构建的订单池"里随机取（不再按 AppearOrderID 索引 Orders）
					if (FOrderData* data = GetRandomOrder())
					{
						UOrderHandle* orderHandle = NewObject<UOrderHandle>(this);
						orderHandle->orderId = OrderID;
						OrderID++;
						orderHandle->OrderData = *data;
						orderHandle->waitTime = 0;
						orderHandle->RefreshOrderStats();
						waitList.Add(orderHandle);
						OnStartOrder.Broadcast(orderHandle, orderHandle->OrderData);
						CurOrderIndex++;
					}
				}
			}
		}
	}
	Super::Tick(DeltaTime);

}
void AOrderSystem::InitCfg(ULevelOrderCfg* cfg)
{
	orderCfg = cfg;
	waitList.Empty(orderCfg ? orderCfg->Orders.Num() : 0);
	CurOrderIndex = 0;
	// 关卡（重新）开始：配置订单的顺序游标从第一单重新开始
	CurConfigOrderIndex = 0;
	OrderID = 0;

	// 订单池改为按菜品表（MenuDataTable / FMenuData）构建
	RebuildOrderPool();
}

void AOrderSystem::RebuildOrderPool()
{
	OrderPool.Reset();
	OrderWeights.Reset();

	UBaseGameInstance* GI = GetWorld() ? Cast<UBaseGameInstance>(GetWorld()->GetGameInstance()) : nullptr;
	UFoodSystem* FS = GI ? GI->foodSystem : nullptr;
	if (!FS)
	{
		UE_LOG(LogTemp, Error, TEXT("[OrderSystem] 构建订单池失败：找不到 UFoodSystem（GameInstance->foodSystem 为空）"));
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 8.f, FColor::Red,
				TEXT("[订单] 构建订单池失败：GameInstance->foodSystem 为空（请检查 MenuDataTable 配置）"));
		}
		return;
	}

	// 价格 / 等待时间不再写进订单（FOrderData 只留"点了什么"），这里只做数据体检
	int32 BadPrice = 0;
	int32 BadWait = 0;

	// 订单里只存 FoodIDs（菜品表 FoodID），其余信息（价格/等待/菜名/机器）全部由菜品表派生
	auto BuildOrder = [FS, &BadPrice, &BadWait](const TArray<int32>& FoodIDs) -> FOrderData
	{
		FOrderData Data;
		Data.FoodIDs = FoodIDs;

		int32 TotalPrice = 0;
		float MaxWait = 0.f;
		for (const int32 FoodID : FoodIDs)
		{
			const FMenuData Menu = FS->GetMenuData(FoodID);
			if (Menu.FoodID < 0)
			{
				continue;
			}
			TotalPrice += Menu.Price;
			MaxWait = FMath::Max(MaxWait, Menu.MaxWaitTime);
		}

		if (TotalPrice <= 0)
		{
			BadPrice++;
		}
		if (MaxWait <= 0.f)
		{
			BadWait++;
		}
		return Data;
	};

	// 权重：自定义套餐取各菜品 Probabilitys 的平均值
	auto WeightOf = [FS](const TArray<int32>& FoodIDs) -> float
	{
		float Sum = 0.f;
		int32 Count = 0;
		for (const int32 FoodID : FoodIDs)
		{
			const FMenuData Menu = FS->GetMenuData(FoodID);
			if (Menu.FoodID < 0)
			{
				continue;
			}
			Sum += FMath::Max(Menu.Probabilitys, 0.f);
			Count++;
		}
		return Count > 0 ? Sum / static_cast<float>(Count) : 0.f;
	};

	// 1) 关卡里配置的自定义套餐（一单多道菜）：只取 FoodIDs，其余全部由菜品表派生
	if (orderCfg)
	{
		for (const FOrderData& Custom : orderCfg->Orders)
		{
			if (Custom.FoodIDs.Num() == 0)
			{
				continue;
			}
			OrderPool.Add(BuildOrder(Custom.FoodIDs));
			OrderWeights.Add(WeightOf(Custom.FoodIDs));
		}
	}

	// 2) 菜品表里每道 Probabilitys>0 的菜各成一单
	for (const int32 FoodID : FS->GetFoodIDsByMachine(0))
	{
		const FMenuData Menu = FS->GetMenuData(FoodID);
		if (Menu.FoodID < 0 || Menu.Probabilitys <= 0.f)
		{
			continue;
		}

		TArray<int32> FoodIDs;
		FoodIDs.Add(FoodID);
		OrderPool.Add(BuildOrder(FoodIDs));
		OrderWeights.Add(FMath::Max(Menu.Probabilitys, 0.f));
	}

	// 菜单表可能刚变过：让在制订单按新表重算价格 / 等待时间
	for (UOrderHandle* Handle : waitList)
	{
		if (Handle)
		{
			Handle->RefreshOrderStats();
		}
	}

	if (OrderPool.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("[OrderSystem] 订单池为空：菜品表里没有 Probabilitys>0 的菜，也没有自定义套餐（ULevelOrderCfg::Orders）"));
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 8.f, FColor::Red,
				TEXT("[订单] 订单池为空！请在 MenuDataTable 给菜品配 Probabilitys>0"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("[OrderSystem] 订单池已按菜品表构建：%d 条（含自定义套餐 %d 条）"),
			OrderPool.Num(), orderCfg ? orderCfg->Orders.Num() : 0);
		if (BadPrice > 0 || BadWait > 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[OrderSystem] 订单池里有 %d 条价格<=0、%d 条等待时间<=0（请在 MenuDataTable 给菜品配 Price / MaxWaitTime）"),
				BadPrice, BadWait);
		}
	}
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
	// 池为空时兜底重建一次（例如菜单表比订单系统晚初始化）
	if (OrderPool.Num() == 0)
	{
		RebuildOrderPool();
		if (OrderPool.Num() == 0)
		{
			return nullptr;
		}
	}

	// 优先按菜品表里的 Probabilitys 加权随机；全为 0（或负）时退化为均匀随机
	float TotalWeight = 0.f;
	for (const float W : OrderWeights)
	{
		TotalWeight += FMath::Max(W, 0.f);
	}

	if (TotalWeight <= 0.f)
	{
		return &OrderPool[FMath::RandRange(0, OrderPool.Num() - 1)];
	}

	float R = FMath::FRandRange(0.f, TotalWeight);
	for (int32 i = 0; i < OrderPool.Num(); ++i)
	{
		R -= FMath::Max(OrderWeights.IsValidIndex(i) ? OrderWeights[i] : 0.f, 0.f);
		if (R <= 0.f)
		{
			return &OrderPool[i];
		}
	}
	return &OrderPool.Last();
}

bool AOrderSystem::GetNextConfigOrder(FOrderData& OutOrder)
{
	if (!orderCfg || orderCfg->Orders.Num() == 0)
	{
		return false;
	}

	const int32 NumOrders = orderCfg->Orders.Num();

	// 按 Orders 数组顺序依次发单：一轮最多尝试 NumOrders 次，避免全是空配置时死循环
	for (int32 Tried = 0; Tried < NumOrders; ++Tried)
	{
		if (CurConfigOrderIndex >= NumOrders)
		{
			if (!bLoopConfigOrders)
			{
				// 配置里的订单已发完且不循环：不再出单（客人会因点不到单而离场）
				return false;
			}
			CurConfigOrderIndex = 0;
		}

		const FOrderData& Config = orderCfg->Orders[CurConfigOrderIndex];
		const int32 ServedIndex = CurConfigOrderIndex;
		++CurConfigOrderIndex;

		// 没填菜品的配置项跳过，继续取下一项
		if (Config.FoodIDs.Num() == 0)
		{
			continue;
		}

		OutOrder = Config;
		UE_LOG(LogTemp, Log, TEXT("[OrderSystem] 按关卡配置出单：第 %d/%d 单"), ServedIndex + 1, NumOrders);
		return true;
	}

	UE_LOG(LogTemp, Warning, TEXT("[OrderSystem] 关卡订单配置（ULevelOrderCfg::Orders）里没有填菜品的订单，请检查 LevelOrderConfigs 指向的资产"));
	return false;
}

UOrderHandle* AOrderSystem::RequestOrderForGuest(AGuestCharacter* Guest)
{
	if (!IsStarted || !Guest)
	{
		return nullptr;
	}

	// 点餐来源：优先按关卡订单配置（Level_01_Order 等）的顺序依次取；
	// 关闭顺序出单或配置里没有订单时，才退回按菜品表 Probabilitys 的随机订单池
	FOrderData OrderData;
	if (bUseConfigOrderSequence && orderCfg && orderCfg->Orders.Num() > 0)
	{
		if (!GetNextConfigOrder(OrderData))
		{
			// 配置订单已按顺序发完（bLoopConfigOrders=false）：不再随机塞单，保持关卡节奏可控
			UE_LOG(LogTemp, Warning, TEXT("[OrderSystem] 关卡订单已发完（不循环），客人 %s 点不到单"), *Guest->GetName());
			return nullptr;
		}
	}
	else
	{
		const FOrderData* RandomData = GetRandomOrder();
		if (!RandomData)
		{
			return nullptr;
		}
		OrderData = *RandomData;
	}

	// 客人点餐：生成订单并绑定该客人（订单生命周期 = 客人的等餐周期）
	UOrderHandle* orderHandle = NewObject<UOrderHandle>(this);
	orderHandle->orderId = OrderID;
	OrderID++;
	orderHandle->OrderData = OrderData;
	orderHandle->waitTime = 0.f;
	orderHandle->Guest = Guest;
	orderHandle->RefreshOrderStats();

	waitList.Add(orderHandle);
	OnStartOrder.Broadcast(orderHandle, orderHandle->OrderData);

	// 屏幕提示由客人侧（AGuestCharacter::PlaceOrder）统一打印，这里只留日志避免重复刷屏
	UE_LOG(LogTemp, Log, TEXT("[OrderSystem] 客人 %s 点餐：%s（等待 %.0fs）"),
		*Guest->GetName(), *orderHandle->GetOrderDisplayName(), orderHandle->GetOrderMaxWaitTime());

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
	// 先取出绑定客人，避免移除句柄后访问
	AGuestCharacter* OrderedGuest = BestHandle->Guest;

	ABaseLevelBP::AddScore(this, BestHandle->GetOrderPrice());
	OnFinishOrder.Broadcast(BestHandle, BestHandle->OrderData);
	waitList.Remove(BestHandle);
	GEngine->AddOnScreenDebugMessage(-1, 2, FColor::Red, TEXT("完成订单(优先最短剩余时间)"));

	// 通知点餐客人开始进食（订单与客人绑定）
	LastServedGuest = OrderedGuest;
	if (OrderedGuest)
	{
		OrderedGuest->OnOrderServed();
	}
	return true;
}

bool AOrderSystem::PushFoodsForGuest(const TArray<int>& FoodIDs, AGuestCharacter* TargetGuest)
{
	if (!IsStarted)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Red, TEXT("上菜失败，游戏已经结束"));
		return false;
	}
	if (!TargetGuest)
	{
		return false;
	}

	const int Num = FoodIDs.Num();

	// 1. 在绑定到该客人的订单里，找出菜品集合完全匹配的候选
	TArray<UOrderHandle*> Candidates;
	for (UOrderHandle* handle : waitList)
	{
		if (!handle || handle->Guest != TargetGuest)
		{
			continue;
		}
		const FOrderData& order = handle->OrderData;
		if (order.FoodIDs.Num() != Num)
		{
			continue;
		}
		// 统计 Foods 中每个 ID 的出现次数，与订单比对
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

	// 2. 在该客人的候选订单中选剩余时间比例最少（最紧急）的优先完成
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
	AGuestCharacter* OrderedGuest = BestHandle->Guest;

	ABaseLevelBP::AddScore(this, BestHandle->GetOrderPrice());
	OnFinishOrder.Broadcast(BestHandle, BestHandle->OrderData);
	waitList.Remove(BestHandle);
	GEngine->AddOnScreenDebugMessage(-1, 2, FColor::Red, TEXT("完成订单(针对性上菜)"));

	// 通知点餐客人开始进食（订单与客人绑定）
	LastServedGuest = OrderedGuest;
	if (OrderedGuest)
	{
		OrderedGuest->OnOrderServed();
	}
	return true;
}

float UOrderHandle::GetPercent()
{
	// 按值保存后无需判空；等待时间为 0 时避免除零（返回 1 表示“刚下单”）
	const float MaxWait = GetOrderMaxWaitTime();
	if (MaxWait > 0.f)
	{
		return 1.f - waitTime / MaxWait;
	}
	return 1.f;
}

/** 句柄的 Outer 是 AOrderSystem（Actor），可由此拿到 World -> GameInstance -> FoodSystem */
static UFoodSystem* GetFoodSystemForHandle(const UOrderHandle* Handle)
{
	UWorld* World = Handle ? Handle->GetWorld() : nullptr;
	if (!World)
	{
		// 兜底：UObject::GetWorld 未解析出世界时，直接沿 Outer 找到订单系统所在世界
		const AOrderSystem* OS = Handle ? Cast<AOrderSystem>(Handle->GetOuter()) : nullptr;
		World = OS ? OS->GetWorld() : nullptr;
	}

	UBaseGameInstance* GI = World ? Cast<UBaseGameInstance>(World->GetGameInstance()) : nullptr;
	return GI ? GI->foodSystem : nullptr;
}

void UOrderHandle::EnsureStatsCached() const
{
	if (bStatsCached)
	{
		return;
	}

	CachedPrice = 0;
	CachedMaxWaitTime = 0.f;
	CachedDisplayName.Reset();
	CachedMachineImageIDs.Reset();

	if (UFoodSystem* FS = GetFoodSystemForHandle(this))
	{
		for (const int32 FoodID : OrderData.FoodIDs)
		{
			const FMenuData Menu = FS->GetMenuData(FoodID);
			if (Menu.FoodID < 0)
			{
				continue;
			}
			CachedPrice += Menu.Price;
			CachedMaxWaitTime = FMath::Max(CachedMaxWaitTime, Menu.MaxWaitTime);

			// 需要的机器（MachineID>0 才记，去重）
			if (Menu.MachineID > 0)
			{
				CachedMachineImageIDs.AddUnique(Menu.MachineID);
			}

			// 显示名：多道菜用 "+" 连接（如“番茄炒蛋+米饭”）
			if (!Menu.FoodName.IsEmpty())
			{
				if (!CachedDisplayName.IsEmpty())
				{
					CachedDisplayName += TEXT("+");
				}
				CachedDisplayName += Menu.FoodName;
			}
		}
	}

	bStatsCached = true;
}

int32 UOrderHandle::GetOrderPrice() const
{
	EnsureStatsCached();
	return CachedPrice;
}

float UOrderHandle::GetOrderMaxWaitTime(int32 MenuID) const
{
	// 传了菜单 ID：只查这一道菜（单菜直查菜品表，不参与整单缓存）
	if (MenuID >= 0)
	{
		if (UFoodSystem* FS = GetFoodSystemForHandle(this))
		{
			const FMenuData Menu = FS->GetMenuData(MenuID);
			if (Menu.FoodID >= 0)
			{
				return Menu.MaxWaitTime;
			}
		}
		return 0.f;
	}

	// 未传：整单最长等待时间（各菜取最大）
	EnsureStatsCached();
	return CachedMaxWaitTime;
}

FString UOrderHandle::GetOrderDisplayName(int32 MenuID) const
{
	// 传了菜单 ID：只查这一道菜的菜名
	if (MenuID >= 0)
	{
		if (UFoodSystem* FS = GetFoodSystemForHandle(this))
		{
			const FMenuData Menu = FS->GetMenuData(MenuID);
			if (Menu.FoodID >= 0 && !Menu.FoodName.IsEmpty())
			{
				return Menu.FoodName;
			}
		}
		// 查不到菜名时退化成“菜品<ID>”，避免 UI 出现空白（与 UChairTipsWidget::GetFoodName 一致）
		return FString::Printf(TEXT("菜品%d"), MenuID);
	}

	// 未传：整单显示名（多道菜用 "+" 连接）
	EnsureStatsCached();
	return CachedDisplayName;
}

TArray<int32> UOrderHandle::GetMachineImageIDs() const
{
	EnsureStatsCached();
	return CachedMachineImageIDs;
}

TArray<UTexture2D*> UOrderHandle::GetOrderFoodIcons() const
{
	TArray<UTexture2D*> Icons;

	UFoodSystem* FS = GetFoodSystemForHandle(this);
	if (!FS)
	{
		return Icons;
	}

	// 菜品表 MenuItems 的 key = 食材 ItemID：到 DT_FoodID（FItemActorInfo::Icon）按 ItemID 取图
	TSet<int32> CollectedItems;
	for (const int32 FoodID : OrderData.FoodIDs)
	{
		const FMenuData Menu = FS->GetMenuData(FoodID);
		if (Menu.FoodID < 0)
		{
			continue;
		}

		for (const TPair<int32, int32>& Item : Menu.MenuItems)
		{
			// 同一食材在多道菜里重复出现时只出一张图
			if (CollectedItems.Contains(Item.Key))
			{
				continue;
			}

			if (UTexture2D* Icon = FS->GetItemFoodIcon(Item.Key))
			{
				CollectedItems.Add(Item.Key);
				Icons.Add(Icon);
			}
		}
	}

	return Icons;
}

void UOrderHandle::RefreshOrderStats()
{
	bStatsCached = false;
	EnsureStatsCached();
}

void UOrderHandle::Reset()
{
	waitTime = 0;
}

FOrderData UOrderHandle::GetOrderData() const
{
	return OrderData;
}
