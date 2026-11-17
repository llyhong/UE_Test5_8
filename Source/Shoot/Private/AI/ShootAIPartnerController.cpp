// Fill out your copyright notice in the Description page of Project Settings.

#include "AI/ShootAIPartnerController.h"

#include "ShootCharacter.h"
#include "Item/Plant.h"
#include "Item/ItemActor.h"
#include "Machine/CookingMachine.h"
#include "NPC/GuestCharacter.h"
#include "NPC/GuestManager.h"
#include "Game/OrderSystem.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogAIPartner, Log, All);

AShootCharacter* AShootAIPartnerController::GetPartnerChar() const
{
	return Cast<AShootCharacter>(GetPawn());
}

void AShootAIPartnerController::SetPartnerMode(EAIPartnerMode NewMode)
{
	if (Mode != NewMode)
	{
		Mode = NewMode;
		bHasPendingDestination = false;
		// 切模式时把溜达状态一并清掉，避免带着旧目标点先跑去别处
		bHasIdleWanderTarget = false;
		IdleWanderPauseLeft = 0.f;
		StopMovement();
		UE_LOG(LogAIPartner, Log, TEXT("[AIPartner] 切换模式：%d"), static_cast<int32>(NewMode));
	}
}

APawn* AShootAIPartnerController::ResolveFollowTarget() const
{
	if (FollowTarget)
	{
		return FollowTarget.Get();
	}
	const UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (const APlayerController* PC = It->Get())
		{
			if (APawn* P = PC->GetPawn(); P && P != GetPawn())
			{
				return P;
			}
		}
	}
	return nullptr;
}

AGuestManager* AShootAIPartnerController::FindGuestManager() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AGuestManager> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

bool AShootAIPartnerController::MoveWithinRange(const FVector& TargetLocation, float Range)
{
	const APawn* PartnerPawn = GetPawn();
	if (!PartnerPawn)
	{
		return false;
	}
	const float Dist = FVector::Dist2D(PartnerPawn->GetActorLocation(), TargetLocation);
	if (Dist <= Range)
	{
		return true;
	}
	// 目标偏移超过 1m 才重发寻路；寻路已停（走到路径尽头/目标不在导航网上）也重发，但按冷却节流
	const bool bStalled = GetMoveStatus() != EPathFollowingStatus::Moving;
	if ((!bHasPendingDestination || FVector::Dist2D(PendingDestination, TargetLocation) > 100.f) || (bStalled && MoveRetryTimer <= 0.f))
	{
		MoveToLocation(TargetLocation, Range * 0.5f);
		PendingDestination = TargetLocation;
		bHasPendingDestination = true;
		MoveRetryTimer = 1.5f;
	}
	return false;
}

void AShootAIPartnerController::DoPick()
{
	if (AShootCharacter* Char = GetPartnerChar())
	{
		Char->AIPartnerDoInteract();
	}
}

void AShootAIPartnerController::TickIdleWander(const FVector& Center)
{
	AShootCharacter* Char = GetPartnerChar();
	if (!Char)
	{
		return;
	}
	if (!bIdleWander || Center.IsNearlyZero())
	{
		// 未开启 / 没配驻守点：保留旧的原地待命行为，零向量直接不动
		if (!Center.IsNearlyZero())
		{
			if (MoveWithinRange(Center, 100.f))
			{
				bHasPendingDestination = false;
			}
		}
		return;
	}

	if (IdleWanderPauseLeft > 0.f)
	{
		return; // 原地歇一会儿再走，避免来回抽动
	}

	if (!bHasIdleWanderTarget)
	{
		// 离驻守点太远（刚生成 / 采完从野外回来）：先直接回点，避免随机点又把路绕长
		if (FVector::Dist2D(Char->GetActorLocation(), Center) > IdleWanderRadius * 2.5f)
		{
			IdleWanderTarget = Center;
			bHasIdleWanderTarget = true;
		}
		else
		{
			FNavLocation WanderPoint;
			UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(GetWorld());
			if (NavSys && NavSys->GetRandomReachablePointInRadius(Center, IdleWanderRadius, WanderPoint))
			{
				IdleWanderTarget = WanderPoint.Location;
				bHasIdleWanderTarget = true;
			}
			// huanglily：驻守点可能落在导航网外（锚点摆得偏），此时以搭档自身位置为圆心再取一次，
			// 让它至少在身子周围动起来，而不是彻底站死。
			else if (NavSys && NavSys->GetRandomReachablePointInRadius(Char->GetActorLocation(), IdleWanderRadius, WanderPoint))
			{
				IdleWanderTarget = WanderPoint.Location;
				bHasIdleWanderTarget = true;
			}
			else
			{
				// 随机点取不到（导航未覆盖驻守点）：退回站定，不影响正常任务
				return;
			}
		}
	}

	if (MoveWithinRange(IdleWanderTarget, 45.f))
	{
		bHasIdleWanderTarget = false;
		bHasPendingDestination = false;
		const float MinPause = FMath::Max(0.f, FMath::Min(IdleWanderPauseMin, IdleWanderPauseMax));
		const float MaxPause = FMath::Max(MinPause, IdleWanderPauseMax);
		IdleWanderPauseLeft = FMath::FRandRange(MinPause, MaxPause);
	}
}

void AShootAIPartnerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	PickCooldown = FMath::Max(0.f, PickCooldown - DeltaSeconds);
	MoveRetryTimer = FMath::Max(0.f, MoveRetryTimer - DeltaSeconds);
	IdleWanderPauseLeft = FMath::Max(0.f, IdleWanderPauseLeft - DeltaSeconds);
	// 拉黑到期后清除失败计数，下次窗口可以重新尝试那株
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	if (HarvestFailCount >= 3 && Now >= HarvestBlacklistUntil)
	{
		HarvestFailCount = 0;
		LastHarvestTarget = nullptr;
	}

	switch (Mode)
	{
	case EAIPartnerMode::Follow:
		TickFollow(DeltaSeconds);
		break;
	case EAIPartnerMode::Forager:
		TickForager();
		break;
	case EAIPartnerMode::Helper:
		TickHelper();
		break;
	}
}

void AShootAIPartnerController::TickFollow(float DeltaSeconds)
{
	AShootCharacter* Char = GetPartnerChar();
	if (!Char)
	{
		return;
	}
	if (APawn* Target = ResolveFollowTarget())
	{
		const float Dist = FVector::Dist2D(Char->GetActorLocation(), Target->GetActorLocation());
		if (Dist > FollowDistance)
		{
			MoveWithinRange(Target->GetActorLocation(), FollowDistance * 0.8f);
		}
		else
		{
			bHasPendingDestination = false;
		}
	}

	// 脚边有掉落物就捡（只在有重叠目标时按 E，避免空按触发"放下手上物品"的兜底）
	if (PickCooldown <= 0.f && Char->GetOverlappingItemCount() > 0)
	{
		DoPick();
		PickCooldown = PickTryInterval;
	}
}

void AShootAIPartnerController::TickForager()
{
	AShootCharacter* Char = GetPartnerChar();
	if (!Char)
	{
		return;
	}
	const AGuestManager* GM = FindGuestManager();

	// 非备料窗口：回驻守点附近待命（不抢玩家的店内交互）
	if (!GM || !GM->bInPrepWindow)
	{
		TickIdleWander(IdlePostLocation);
		return;
	}

	// 备料窗口：找最近可采集的植株；连续失败 3 次的拉黑 30 秒
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	APlant* Best = nullptr;
	float BestDist = TNumericLimits<float>::Max();
	for (TActorIterator<APlant> It(GetWorld()); It; ++It)
	{
		APlant* Plant = *It;
		if (!Plant->UsesWildHarvest() || !Plant->CanHarvest())
		{
			continue;
		}
		if (LastHarvestTarget.Get() == Plant && HarvestFailCount >= 3)
		{
			continue;
		}
		const float Dist = FVector::Dist2D(Char->GetActorLocation(), Plant->GetActorLocation());
		if (Dist < BestDist)
		{
			BestDist = Dist;
			Best = Plant;
		}
	}

	if (!Best)
	{
		// 能采的都采完了：回驻守点附近溜达着等窗口结束
		TickIdleWander(IdlePostLocation);
		return;
	}

	if (MoveWithinRange(Best->GetActorLocation(), InteractRange))
	{
		if (Best->HarvestForCharacter(Char))
		{
			HarvestFailCount = 0;
			LastHarvestTarget = nullptr;
			UE_LOG(LogAIPartner, Log, TEXT("[AIPartner] 采集：%s"), *Best->GetName());
		}
		else if (LastHarvestTarget.Get() == Best)
		{
			++HarvestFailCount;
			if (HarvestFailCount >= 3)
			{
				HarvestBlacklistUntil = Now + 30.f;
				UE_LOG(LogAIPartner, Log, TEXT("[AIPartner] 采集点不可达，拉黑 30s：%s"), *Best->GetName());
			}
		}
		else
		{
			LastHarvestTarget = Best;
			HarvestFailCount = 1;
		}
	}
}

void AShootAIPartnerController::TickHelper()
{
	AShootCharacter* Char = GetPartnerChar();
	if (!Char)
	{
		return;
	}

	if (!Char->GetPickingItem())
	{
		// 空手：找最近煮好待取的锅
		ACookingMachine* Best = nullptr;
		float BestDist = TNumericLimits<float>::Max();
		for (TActorIterator<ACookingMachine> It(GetWorld()); It; ++It)
		{
			if (!It->IsFinish)
			{
				continue;
			}
			const float Dist = FVector::Dist2D(Char->GetActorLocation(), It->GetActorLocation());
			if (Dist < BestDist)
			{
				BestDist = Dist;
				Best = *It;
			}
		}
		if (Best)
		{
			if (MoveWithinRange(Best->GetActorLocation(), InteractRange))
			{
				DoPick(); // 取餐（Tier 2 出锅分支，内部校验锅是否完成）
			}
		}
		else if (!IdlePostLocation.IsNearlyZero())
		{
			TickIdleWander(IdlePostLocation);
		}
		return;
	}

	// 手上有菜：找点了这道菜、还没上菜的客人
	const AItemActor* Held = Char->GetPickingItem();
	const int32 FoodID = Held ? Held->GetMenuFoodID() : -1;
	AGuestCharacter* BestGuest = nullptr;
	float BestDist = TNumericLimits<float>::Max();
	const AGuestManager* GM = FindGuestManager();
	if (GM && FoodID >= 0)
	{
		for (const TObjectPtr<AGuestCharacter>& Guest : GM->ActiveGuests)
		{
			if (!Guest || !Guest->CurrentOrder || Guest->CurrentOrder->IsExpired())
			{
				continue;
			}
			if (!Guest->CurrentOrder->OrderData.FoodIDs.Contains(FoodID))
			{
				continue;
			}
			const float Dist = FVector::Dist2D(Char->GetActorLocation(), Guest->GetActorLocation());
			if (Dist < BestDist)
			{
				BestDist = Dist;
				BestGuest = Guest;
			}
		}
	}
	if (BestGuest)
	{
		if (MoveWithinRange(BestGuest->GetActorLocation(), InteractRange))
		{
			DoPick(); // 端给客人（Tier 1 交付分支，内部按菜品匹配该客人的订单）
		}
	}
	else if (!IdlePostLocation.IsNearlyZero())
	{
		TickIdleWander(IdlePostLocation);
	}
}
