// Fill out your copyright notice in the Description page of Project Settings.

#include "NPC/GuestManager.h"
#include "NPC/GuestCharacter.h"
#include "NPC/ChairSeat.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "DrawDebugHelpers.h"
#include "Game/BaseGameInstance.h"
#include "Game/EconomySystem.h"
#include "Game/BusinessTime.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"

AGuestManager::AGuestManager()
{
	// 需要每帧绘制队列调试球，所以开启 Tick（默认不自动启用，BeginPlay 里按开关决定）
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
}

void AGuestManager::BeginPlay()
{
	Super::BeginPlay();

	// 只有开启任一调试绘制时才启用 Tick，避免无谓开销
	SetActorTickEnabled(bDrawQueueDebug || bDrawSpawnLeaveDebug);

	// —— 配置自检：快速暴露"看不到客人"的常见原因 ——
	if (bShowDebugInfo)
	{
		if (!GuestClass)
		{
			UE_LOG(LogTemp, Warning, TEXT("[GuestManager] GuestClass 未配置，无法生成客人！请在管理器蓝图中指定客人蓝图类"));
			GEngine->AddOnScreenDebugMessage(-1, 8.0f, FColor::Red, TEXT("[GuestManager] GuestClass 未配置，无法生成客人！"));
		}
		else if (GuestClass->HasAnyClassFlags(CLASS_Abstract))
		{
			UE_LOG(LogTemp, Warning, TEXT("[GuestManager] GuestClass(%s) 是抽象类，无法生成，请指定其蓝图子类"), *GuestClass->GetName());
		}

		int32 SeatCount = 0;
		TArray<AActor*> Seats;
		UGameplayStatics::GetAllActorsOfClass(this, AChairSeat::StaticClass(), Seats);
		SeatCount = Seats.Num();
		if (SeatCount == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[GuestManager] 场景中没有 AChairSeat 座位，客人找不到座位会立刻离场（看起来像'没有客人'）"));
			GEngine->AddOnScreenDebugMessage(-1, 8.0f, FColor::Red, TEXT("[GuestManager] 场景中没有座位(AChairSeat)，客人会立刻离场"));
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("[GuestManager] 场景座位数 = %d"), SeatCount);
		}
	}

	// 绑定每个座位的“空碟回收释放”委托：空碟被玩家收走时座位才真正释放，
	// 由本管理器安排下一位客人入座（方案B 用餐闭环）。
	{
		TArray<AActor*> AllSeats;
		UGameplayStatics::GetAllActorsOfClass(this, AChairSeat::StaticClass(), AllSeats);
		for (AActor* A : AllSeats)
		{
			if (AChairSeat* Seat = Cast<AChairSeat>(A))
			{
				Seat->OnSeatFreedDelegate.AddDynamic(this, &AGuestManager::OnSeatFreed);
			}
		}
	}

	if (bAutoSpawn && SpawnInterval > 0.0f)
	{
		GetWorld()->GetTimerManager().SetTimer(AutoSpawnTimerHandle,
			this, &AGuestManager::TickSpawn, SpawnInterval, true, SpawnInterval);
	}

	// 座位占用情况自动打印（SeatStatusLogInterval>0 时按间隔循环打印）
	if (SeatStatusLogInterval > 0.0f)
	{
		GetWorld()->GetTimerManager().SetTimer(SeatStatusLogHandle,
			this, &AGuestManager::PrintSeatStatus, SeatStatusLogInterval, true, SeatStatusLogInterval);
	}
}

void AGuestManager::TickSpawn()
{
	SpawnGuest();
}

void AGuestManager::PrintSeatStatus()
{
	TArray<AActor*> SeatActors;
	UGameplayStatics::GetAllActorsOfClass(this, AChairSeat::StaticClass(), SeatActors);
	const int32 Total = SeatActors.Num();
	int32 Occupied = 0;
	int32 Free = 0;
	for (AActor* A : SeatActors)
	{
		if (AChairSeat* Seat = Cast<AChairSeat>(A))
		{
			if (Seat->bIsOccupied)
			{
				Occupied++;
				const FString Who = Seat->Occupant ? Seat->Occupant->GetName() : TEXT("???");
				UE_LOG(LogTemp, Log, TEXT("[SeatStatus] 座位 %s 占用中，占用者=%s"), *Seat->GetName(), *Who);
				if (bShowDebugInfo && GEngine)
				{
					GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red,
						FString::Printf(TEXT("[座位] %s 占用 (%s)"), *Seat->GetName(), *Who));
				}
			}
			else
			{
				Free++;
				UE_LOG(LogTemp, Log, TEXT("[SeatStatus] 座位 %s 空闲"), *Seat->GetName());
			}
		}
	}
	UE_LOG(LogTemp, Warning, TEXT("[SeatStatus] 共 %d 个座位：占用 %d，空闲 %d"), Total, Occupied, Free);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 6.f, FColor::Yellow,
			FString::Printf(TEXT("[座位总览] 共%d 占用%d 空闲%d"), Total, Occupied, Free));
	}
}

void AGuestManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bDrawQueueDebug)
	{
		DrawQueueDebug();
	}
	if (bDrawSpawnLeaveDebug)
	{
		DrawSpawnLeaveDebug();
	}
}

void AGuestManager::DrawQueueDebug()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FVector Center = GetQueueWanderCenter();
	const FColor AreaColor = FColor(0, 140, 255);

	// 等候徘徊范围：地面上的蓝色圆环（客人会在这个圈内游走）
	const int32 Segments = 48;
	for (int32 i = 0; i < Segments; ++i)
	{
		const float A0 = (2.0f * PI) * static_cast<float>(i) / Segments;
		const float A1 = (2.0f * PI) * static_cast<float>(i + 1) / Segments;
		const FVector P0 = Center + FVector(FMath::Cos(A0), FMath::Sin(A0), 0.0f) * QueueWanderRadius;
		const FVector P1 = Center + FVector(FMath::Cos(A1), FMath::Sin(A1), 0.0f) * QueueWanderRadius;
		DrawDebugLine(World, P0, P1, AreaColor, false, -1.0f, 0, 2.5f);
	}

	// 范围中心
	DrawDebugPoint(World, Center, 14.0f, AreaColor, false, -1.0f);

	// 每个等待客人当前位置：绿色小球 + 与中心的连线 + 序号（0 号最先入座）
	for (int32 i = 0; i < WaitingQueue.Num(); ++i)
	{
		AGuestCharacter* G = WaitingQueue[i];
		if (!IsValid(G))
		{
			continue;
		}

		const FVector Loc = G->GetActorLocation();
		DrawDebugLine(World, Center, Loc, FColor::Green, false, -1.0f, 0, 1.0f);
		DrawDebugSphere(World, Loc, QueueDebugSphereRadius, 12, FColor::Green, false, -1.0f, 0, 3.0f);
		DrawDebugString(World, Loc + FVector(0.0f, 0.0f, QueueDebugSphereRadius + 10.0f),
			FString::Printf(TEXT("#%d"), i), nullptr, FColor::Green, -1.0f, true);
	}
}

void AGuestManager::DrawSpawnLeaveDebug() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FVector Center = GetActorLocation();
	const FVector Spawn = SpawnLocation.IsNearlyZero() ? Center : SpawnLocation;

	// 生成点：绿色球 + 文字标签
	DrawDebugSphere(World, Spawn, 50.f, 12, FColor::Green, false, -1.f, 0, 4.f);
	DrawDebugString(World, Spawn + FVector(0.f, 0.f, 90.f), TEXT("Spawn"), nullptr, FColor::Green, -1.f, true);

	// 离场点：红色球 + 标签；若为默认零向量（意味着客人原地直接销毁、不走离场点）则用黄色警告
	if (LeaveLocation.IsNearlyZero())
	{
		DrawDebugSphere(World, Center, 50.f, 12, FColor::Yellow, false, -1.f, 0, 4.f);
		DrawDebugString(World, Center + FVector(0.f, 0.f, 90.f),
			TEXT("Leave=Zero(客人原地销毁!)"), nullptr, FColor::Yellow, -1.f, true);
	}
	else
	{
		DrawDebugSphere(World, LeaveLocation, 50.f, 12, FColor::Red, false, -1.f, 0, 4.f);
		DrawDebugString(World, LeaveLocation + FVector(0.f, 0.f, 90.f), TEXT("Leave"), nullptr, FColor::Red, -1.f, true);

		// Spawn -> Leave 连线，直观看客人入场/离场路径
		DrawDebugLine(World, Spawn, LeaveLocation, FColor::Cyan, false, -1.f, 0, 2.f);
	}
}

AGuestCharacter* AGuestManager::SpawnGuest()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	// 仅在营业时生成客人（白天备货 / 打烊后不再来客）；同时懒绑定营业委托
	if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(World->GetGameInstance()))
	{
		if (GI->businessSimulation && GI->businessSimulation->TimeSystem)
		{
			UTimeSystem* TS = GI->businessSimulation->TimeSystem;
			if (!bBoundBusiness)
			{
				TS->OnBusinessOpenChanged.AddDynamic(this, &AGuestManager::OnBusinessOpenChanged);
				bBoundBusiness = true;
			}
			if (!TS->IsBusinessOpen())
			{
				return nullptr; // 未营业：静默跳过，不刷屏
			}
		}
	}

	// 每个失败分支都给出明确原因，避免"静默不生成"
	if (!GuestClass)
	{
		if (bShowDebugInfo)
		{
			UE_LOG(LogTemp, Warning, TEXT("[GuestManager] SpawnGuest 失败：GuestClass 未配置"));
			GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Red, TEXT("[Manager] SpawnGuest 失败：GuestClass 未配置"));
		}
		return nullptr;
	}

	if (GuestClass->HasAnyClassFlags(CLASS_Abstract))
	{
		if (bShowDebugInfo)
		{
			UE_LOG(LogTemp, Warning, TEXT("[GuestManager] SpawnGuest 失败：GuestClass(%s) 是抽象类，请指定蓝图子类"), *GuestClass->GetName());
			GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Red, TEXT("[Manager] SpawnGuest 失败：GuestClass 是抽象类"));
		}
		return nullptr;
	}

	// 受到最大在场数量限制（实际容量随声誉/新餐桌道具动态变化）
	const int32 MaxAllowed = GetEffectiveMaxGuests();
	if (ActiveGuests.Num() >= MaxAllowed)
	{
		if (bShowDebugInfo)
		{
			UE_LOG(LogTemp, Log, TEXT("[GuestManager] SpawnGuest 跳过：已达容量上限 %d/%d"), ActiveGuests.Num(), MaxAllowed);
			GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Orange,
				FString::Printf(TEXT("[Manager] 客人已满 %d/%d"), ActiveGuests.Num(), MaxAllowed));
		}
		return nullptr;
	}

	const FVector Loc = SpawnLocation.IsNearlyZero() ? GetActorLocation() : SpawnLocation;
	const FRotator Rot = GetActorRotation();

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AGuestCharacter* Guest = World->SpawnActor<AGuestCharacter>(GuestClass, Loc, Rot, SpawnParams);
	if (!Guest)
	{
		if (bShowDebugInfo)
		{
			UE_LOG(LogTemp, Warning, TEXT("[GuestManager] SpawnGuest 失败：SpawnActor 返回空（类不可生成 / 世界未就绪）位置=%s"), *Loc.ToString());
			GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Red, TEXT("[Manager] SpawnActor 返回空，客人未生成"));
		}
		return nullptr;
	}

	// 配置离场点，并把管理器回指给客人（用于销毁时反注册）
	Guest->LeaveLocation = LeaveLocation;
	Guest->SetManager(this);

	// 接单方式下发：管理器开关统一覆盖每个生成出的客人
	Guest->OrderAcceptMode = OrderAcceptMode;

	// 按概率决定是否为特殊客人（默认 Normal，故仅命中时改写）。
	// 特殊客人完成订单会获得额外声誉（AGuestCharacter::SpecialReputationBonus）。
	if (FMath::FRand() < SpecialGuestChance)
	{
		Guest->GuestType = EGuestType::Special;
	}

	ActiveGuests.Add(Guest);

	if (bShowDebugInfo && GEngine)
	{
		// 生成点可视化：绿色球表示客人实际生成位置
		DrawDebugSphere(World, Loc, 40.0f, 12, FColor::Green, false, 3.0f);

		// 检查生成点是否在 NavMesh 上（不在导航范围内的客人将无法寻路移动）
		bool bOnNavMesh = false;
		if (UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(World))
		{
			FNavLocation NavLoc;
			bOnNavMesh = NavSys->ProjectPointToNavigation(Loc, NavLoc, FVector(200.f, 200.f, 400.f));
		}

		GEngine->AddOnScreenDebugMessage(-1, 4.0f, FColor::Green,
			FString::Printf(TEXT("[Manager] 生成客人 %s @ %s NavMesh=%s，当前在场 %d/%d"),
				*Guest->GetName(), *Loc.ToString(), bOnNavMesh ? TEXT("是") : TEXT("否(将无法寻路)"),
				ActiveGuests.Num(), MaxAllowed));

		if (!bOnNavMesh)
		{
			UE_LOG(LogTemp, Warning, TEXT("[GuestManager] 生成点 %s 不在 NavMesh 上，客人无法寻路移动（检查 NavMeshBoundsVolume 是否覆盖此区域）"), *Loc.ToString());
		}

		// 客人是否可见（蓝图未挂网格时只有胶囊体，容易"看不见"）
		USkeletalMeshComponent* GuestMesh = Guest->GetMesh();
		const bool bHasMesh = GuestMesh && GuestMesh->GetSkeletalMeshAsset();
		if (!bHasMesh)
		{
			UE_LOG(LogTemp, Warning, TEXT("[GuestManager] 客人 %s 没有骨骼网格体（蓝图里未设置 Mesh），场景中几乎看不见"), *Guest->GetName());
			GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Yellow, TEXT("[Manager] 客人蓝图没有 Mesh，所以看不清"));
		}
	}

	return Guest;
}

int32 AGuestManager::GetEffectiveMaxGuests() const
{
	int32 Base = MaxGuests;
	if (const UWorld* W = GetWorld())
	{
		if (const UBaseGameInstance* GI = Cast<UBaseGameInstance>(W->GetGameInstance()))
		{
			if (GI->economySystem)
			{
				Base += GI->economySystem->GetReputation() / GuestsPerReputation;
				Base += GI->economySystem->GetOwnedItemCount(EconomyShopItemIDs::ExtraTable) * ExtraGuestsPerTable;
			}
			// 员工服务品质 → 额外招客容量（开罗式资源循环：招人/培养 → 吸引更多顾客）
			if (GI->businessSimulation && GI->businessSimulation->StaffSystem)
			{
				Base += GI->businessSimulation->StaffSystem->GetCustomerAttractionBonus();
			}
		}
	}
	return FMath::Clamp(Base, 0, MaxGuestsHardCap);
}

void AGuestManager::SpawnGuestsBurst(int32 Count)
{
	for (int32 i = 0; i < Count; ++i)
	{
		if (!SpawnGuest())
		{
			// 达到 MaxGuests 或生成失败则停止
			break;
		}
	}
}

void AGuestManager::UnregisterGuest(AGuestCharacter* Guest)
{
	if (Guest)
	{
		ActiveGuests.Remove(Guest);
		RemoveFromQueue(Guest);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Yellow,
				FString::Printf(TEXT("[Manager] 客人 %s 离场，当前在场 %d/%d"),
					*Guest->GetName(), ActiveGuests.Num(), GetEffectiveMaxGuests()));
		}
	}

	// 打烊收尾中：每走一位客人就检测一次，全部处理完才真正打烊
	if (bClosingPending)
	{
		CheckClosingFinished();
	}
}

bool AGuestManager::IsGuestNeedKeepOnClose(const AGuestCharacter* Guest) const
{
	if (!IsValid(Guest))
	{
		return false;
	}
	// 已点菜（等餐）/ 正在进食 的客人需要保留：等玩家上完菜，或等待超时离场
	return Guest->GuestState == EGuestState::WaitingFood
		|| Guest->GuestState == EGuestState::Eating
		|| Guest->CurrentOrder != nullptr;
}

int32 AGuestManager::GetPendingClosingGuestCount() const
{
	int32 Count = 0;
	for (const TObjectPtr<AGuestCharacter>& G : ActiveGuests)
	{
		if (IsGuestNeedKeepOnClose(G.Get()))
		{
			Count++;
		}
	}
	return Count;
}

void AGuestManager::ClearGuestsExcept(const TArray<AGuestCharacter*>& KeepList)
{
	// 收集需要清走的：在场列表 + 排队列表，跳过需要保留的
	TArray<AGuestCharacter*> ToDestroy;
	for (AGuestCharacter* G : ActiveGuests)
	{
		if (IsValid(G) && !KeepList.Contains(G) && !ToDestroy.Contains(G))
		{
			ToDestroy.Add(G);
		}
	}
	for (AGuestCharacter* Q : WaitingQueue)
	{
		if (IsValid(Q) && !KeepList.Contains(Q) && !ToDestroy.Contains(Q))
		{
			ToDestroy.Add(Q);
		}
	}

	for (AGuestCharacter* G : ToDestroy)
	{
		if (IsValid(G))
		{
			// 立刻释放座位再销毁，避免同帧座位仍被判定为占用
			G->ReleaseSeatIfOccupied();
			G->Destroy();
		}
	}

	// 从列表里移除被清走的（保留的仍留在列表中继续等餐/进食）
	ActiveGuests.RemoveAll([&ToDestroy](const TObjectPtr<AGuestCharacter>& G)
	{
		return !IsValid(G.Get()) || ToDestroy.Contains(G.Get());
	});
	WaitingQueue.RemoveAll([&ToDestroy](const TObjectPtr<AGuestCharacter>& G)
	{
		return !IsValid(G.Get()) || ToDestroy.Contains(G.Get());
	});
}

void AGuestManager::CheckClosingFinished()
{
	if (!bClosingPending)
	{
		return;
	}
	if (GetPendingClosingGuestCount() > 0)
	{
		// 还有已点菜的客人没处理完（等玩家上菜 / 等待超时），继续等
		return;
	}
	FinishClosing();
}

void AGuestManager::FinishClosing()
{
	if (!bClosingPending)
	{
		return;
	}
	bClosingPending = false;

	// 兜底超时定时器清掉
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ClosingTimeoutHandle);
	}

	// 真正清场：此时不应再有需要保留的客人
	ClearAllGuests();

	UE_LOG(LogTemp, Log, TEXT("[GuestManager] 打烊收尾完成：已点菜客人全部处理完，真正打烊结束这一天"));
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Cyan, TEXT("[Manager] 客人已全部接待完，今日打烊"));
	}

	OnClosingFinished.Broadcast();
}

void AGuestManager::ClearAllGuests()
{
	// 收集所有需要销毁的客人：在场列表 + 排队等待列表（排队等待的客人可能尚未入座，
	// 但同样需要离开场景；用 Contains 去重，避免重复 Destroy）。
	TArray<AGuestCharacter*> ToDestroy = ActiveGuests;
	for (AGuestCharacter* Queued : WaitingQueue)
	{
		if (IsValid(Queued) && !ToDestroy.Contains(Queued))
		{
			ToDestroy.Add(Queued);
		}
	}

	for (AGuestCharacter* Guest : ToDestroy)
	{
		if (IsValid(Guest))
		{
			// 立刻释放座位，再销毁：避免 Destroy 延迟到帧末才触发 EndPlay 释放，
			// 导致同帧重新生成客人时该座位仍被判定为占用而找不到空位。
			Guest->ReleaseSeatIfOccupied();
			Guest->Destroy();
		}
	}
	ActiveGuests.Empty();
	WaitingQueue.Empty();
}

void AGuestManager::PauseSpawning()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AutoSpawnTimerHandle);
	}
}

void AGuestManager::ResumeSpawning()
{
	if (!bAutoSpawn || SpawnInterval <= 0.0f)
	{
		return;
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(AutoSpawnTimerHandle,
			this, &AGuestManager::TickSpawn, SpawnInterval, true, SpawnInterval);
	}
}

FVector AGuestManager::GetQueueWanderCenter() const
{
	// 等候徘徊区就以管理器位置为中心（旋转/移动管理器即可调整等候区域）
	return GetActorLocation();
}

int32 AGuestManager::EnqueueGuest(AGuestCharacter* Guest)
{
	if (!Guest)
	{
		return -1;
	}

	// 已在队列中则先移除，重新排到队尾
	WaitingQueue.Remove(Guest);
	WaitingQueue.Add(Guest);

	const int32 Index = WaitingQueue.Num() - 1;
	Guest->QueueIndex = Index;

	// 不再等距排队：让客人在管理器附近的范围内随机徘徊（走走停停）。
	// 用寻路而非传送，动作更自然；目标点会在徘徊逻辑里投影到 NavMesh。
	Guest->StartWaitingWander(GetQueueWanderCenter(), QueueWanderRadius,
		QueueWanderPauseMin, QueueWanderPauseMax);

	if (bShowDebugInfo)
	{
		UE_LOG(LogTemp, Log, TEXT("[GuestManager] 客人 %s 入队，序号 %d，队列长度 %d，开始在半径 %.0f 内徘徊"),
			*Guest->GetName(), Index, WaitingQueue.Num(), QueueWanderRadius);
	}
	return Index;
}

void AGuestManager::RemoveFromQueue(AGuestCharacter* Guest)
{
	if (Guest && WaitingQueue.Remove(Guest) > 0)
	{
		RefreshQueueSlots();
	}
}

void AGuestManager::RefreshQueueSlots()
{
	// 排队序号仅用于记录先后顺序（提升时按列表顺序取队首），不再摆放位置
	for (int32 i = 0; i < WaitingQueue.Num(); ++i)
	{
		AGuestCharacter* G = WaitingQueue[i];
		if (IsValid(G))
		{
			G->QueueIndex = i;
		}
	}
}

void AGuestManager::OnSeatFreed()
{
	while (WaitingQueue.Num() > 0)
	{
		AGuestCharacter* Next = WaitingQueue[0];
		WaitingQueue.RemoveAt(0);

		// 跳过已失效的条目
		if (!IsValid(Next))
		{
			continue;
		}

		RefreshQueueSlots();

		if (bShowDebugInfo)
		{
			UE_LOG(LogTemp, Log, TEXT("[GuestManager] 座位空出，提升队首客人 %s 入座，剩余等待 %d"),
				*Next->GetName(), WaitingQueue.Num());
		}

		Next->PromoteFromQueue();
		return;
	}
}

void AGuestManager::HandleQueueTimeout(AGuestCharacter* Guest)
{
	RemoveFromQueue(Guest);

	if (ReputationPenaltyOnWaitTimeout == 0)
	{
		return;
	}

	if (const UWorld* W = GetWorld())
	{
		if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(W->GetGameInstance()))
		{
			if (GI->economySystem)
			{
				GI->economySystem->AddReputation(-ReputationPenaltyOnWaitTimeout);

				UE_LOG(LogTemp, Log, TEXT("[GuestManager] 客人排队超时离场，声誉 -%d（当前 %d）"),
					ReputationPenaltyOnWaitTimeout, GI->economySystem->GetReputation());

				if (bShowDebugInfo && GEngine)
				{
					GEngine->AddOnScreenDebugMessage(-1, 4.0f, FColor::Red,
						FString::Printf(TEXT("[Manager] 客人等待超时，声誉 -%d（当前 %d）"),
							ReputationPenaltyOnWaitTimeout, GI->economySystem->GetReputation()));
				}
			}
		}
	}
}

void AGuestManager::OnBusinessOpenChanged(bool bOpen)
{
	if (bOpen)
	{
		// 开门营业：恢复自动生成客人（前提是 bAutoSpawn 开启）
		ResumeSpawning();
		// 新一天开始：清掉上一日残留的打烊收尾状态
		bClosingPending = false;
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(ClosingTimeoutHandle);
		}
		return;
	}

	// 打烊：先停止生成新客人（不再有新客进场）
	PauseSpawning();

	// 保留"已点菜"的客人：等玩家把菜上完，或客人等待超时离场，才真正打烊结束这一天
	if (bKeepOrderedGuestsOnClose)
	{
		TArray<AGuestCharacter*> KeepList;
		for (AGuestCharacter* G : ActiveGuests)
		{
			if (IsGuestNeedKeepOnClose(G))
			{
				KeepList.Add(G);
			}
		}
		// 排队中但已点单的（理论上不会，保险起见也保留）
		for (AGuestCharacter* Q : WaitingQueue)
		{
			if (IsGuestNeedKeepOnClose(Q) && !KeepList.Contains(Q))
			{
				KeepList.Add(Q);
			}
		}

		if (KeepList.Num() > 0)
		{
			bClosingPending = true;

			// 清走不需要保留的客人（未点单/排队/进场中），已点菜的留在座位上继续等餐
			ClearGuestsExcept(KeepList);

			UE_LOG(LogTemp, Log, TEXT("[GuestManager] 已打烊：保留 %d 位已点菜客人，等上完菜或等待超时后才真正结束这一天"),
				KeepList.Num());
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Yellow,
					FString::Printf(TEXT("[Manager] 打烊：还有 %d 位客人等餐，上完菜才能收工"), KeepList.Num()));
			}

			// 兜底：避免客人异常卡住导致永远打不了烊
			if (ClosingMaxWaitTime > 0.f)
			{
				if (UWorld* World = GetWorld())
				{
					World->GetTimerManager().SetTimer(ClosingTimeoutHandle,
						FTimerDelegate::CreateWeakLambda(this, [this]()
						{
							if (bClosingPending)
							{
								UE_LOG(LogTemp, Warning,
									TEXT("[GuestManager] 打烊收尾超时(%.0fs)，强制结束这一天"), ClosingMaxWaitTime);
								FinishClosing();
							}
						}),
						ClosingMaxWaitTime, false);
				}
			}
			return;
		}
	}

	// 没有需要保留的客人：直接真正清场完成打烊
	bClosingPending = true;
	FinishClosing();
}
