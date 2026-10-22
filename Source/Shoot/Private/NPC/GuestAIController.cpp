// Fill out your copyright notice in the Description page of Project Settings.

#include "NPC/GuestAIController.h"
#include "NPC/GuestCharacter.h"
#include "TimerManager.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"

AGuestAIController::AGuestAIController()
{
	// 自动开始 AI 逻辑
	bStartAILogicOnPossess = true;
	bAttachToPawn = true;
}

void AGuestAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	

	// Possess 后延迟一帧再让客人开始走向座位。
	// 原因：OnPossess 时 NavMesh / PathFollowingComponent 可能尚未就绪，
	// 同步调用 MoveToActor 会因 BuildPathfindingQuery 拿不到有效 NavData 而 Failed；
	// 蓝图 AI Move To 走异步 AITask，天然在 tick 之后执行，所以能成功。
	if (AGuestCharacter* Guest = Cast<AGuestCharacter>(InPawn))
	{
		// CreateWeakLambda：控制器被销毁后回调不执行，避免悬垂 this/Guest 访问
		FTimerDelegate NextTickDelegate = FTimerDelegate::CreateWeakLambda(this, [this, Guest]()
		{
			if (IsValid(Guest))
			{
				Guest->StartWalkingToSeat();
			}
		});
		GetWorld()->GetTimerManager().SetTimerForNextTick(NextTickDelegate);
	}
}

void AGuestAIController::MoveToSeatLocation(const FVector& Target, AGuestCharacter* Guest)
{
	if (!Guest) return;

	// 若正在排队徘徊，先停止，避免两套移动指令互相打断
	StopWander();

	// 通过基类 AAIController 发起寻路
	MoveToLocation(Target, 5.0f, true, true, false, true, nullptr, true);

	// 用 Timer 轮询是否到达
	// 使用 CreateWeakLambda：控制器（this）被销毁后，回调不会执行，从根本上避免悬垂 this（0xff...）访问。
	FTimerDelegate NextTickDelegate = FTimerDelegate::CreateWeakLambda(this, [this, Guest, Target]()
	{
		// 双保险：控制器或 World 已失效（如移动途中客人/控制器被销毁）时直接退出
		if (!IsValid(this) || !GetWorld())
		{
			return;
		}
		FTimerDelegate TickDelegate = FTimerDelegate::CreateWeakLambda(this, [this, Guest, Target]()
		{
			// 双保险：控制器已失效时直接退出（Don't dereference a dead controller's World）
			if (!IsValid(this) || !GetWorld())
			{
				return;
			}
			if (!IsValid(Guest))
			{
				GetWorld()->GetTimerManager().ClearTimer(ArriveCheckTimerHandle);
				return;
			}
			const float Dist = FVector::Dist(Guest->GetActorLocation(), Target);
			// 轮询打印当前距离（青色，0.25s 短显，避免刷屏）
			if (bShowDebugInfo && GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 0.25f, FColor::Cyan,
					FString::Printf(TEXT("[AI] %s 轮询离场点距离: %.1f / 80"),
						*GetName(), Dist));
			}
			if (Dist < 80.0f)
			{
				// 到达目标（绿色）
				if (bShowDebugInfo && GEngine)
				{
					GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Green,
						FString::Printf(TEXT("[AI] %s 已到达离场点"), *GetName()));
				}
				// 直接 C++ 调用到达回调（不再依赖反射 FindFunction）
				Guest->OnReachLeavePoint();
				StopMovement();
				if (GetWorld())
				{
					GetWorld()->GetTimerManager().ClearTimer(ArriveCheckTimerHandle);
				}
			}
		});

		GetWorld()->GetTimerManager().SetTimer(ArriveCheckTimerHandle, TickDelegate, 0.2f, true);
	});
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().SetTimerForNextTick(NextTickDelegate);
	}
}

void AGuestAIController::MoveToSeatActor(AActor* Target, AGuestCharacter* Guest)
{
	if (!Target || !Guest) return;

	// 若正在排队徘徊，先停止，避免两套移动指令互相打断
	StopWander();

	// 新一轮寻路：清掉可能残留的重试定时器并重置计数
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SeatRetryTimerHandle);
	}
	SeatMoveRetryCount = 0;

	AttemptMoveToSeat(Target, Guest);
}

void AGuestAIController::BeginWander(const FVector& Center, float Radius, float PauseMin, float PauseMax)
{
	WanderCenter = Center;
	WanderRadius = FMath::Max(Radius, 50.0f);
	WanderPauseMin = FMath::Max(PauseMin, 0.0f);
	WanderPauseMax = FMath::Max(PauseMax, WanderPauseMin);
	bWandering = true;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(WanderCheckTimerHandle);
		World->GetTimerManager().ClearTimer(WanderPauseTimerHandle);
	}

	WanderToNextPoint();
}

void AGuestAIController::StopWander()
{
	bWandering = false;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(WanderCheckTimerHandle);
		World->GetTimerManager().ClearTimer(WanderPauseTimerHandle);
	}

	// 徘徊期间可能正停着不走，这里统一停一次（幂等）
	StopMovement();
}

void AGuestAIController::WanderToNextPoint()
{
	if (!bWandering)
	{
		return;
	}

	UWorld* World = GetWorld();
	APawn* MyPawn = GetPawn();
	if (!World || !MyPawn)
	{
		StopWander();
		return;
	}

	// 在徘徊范围内随机取一个点（内圈留一点余量，避免总贴着边缘）
	const float Angle = FMath::FRandRange(0.0f, 2.0f * PI);
	const float Dist = FMath::FRandRange(WanderRadius * 0.3f, WanderRadius);
	FVector Candidate = WanderCenter + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * Dist;

	// 投影到 NavMesh：保证目标点落在可行走面上（否则寻路会失败）
	if (UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(World))
	{
		FNavLocation NavLoc;
		if (NavSys->ProjectPointToNavigation(Candidate, NavLoc, FVector(WanderRadius, WanderRadius, 400.0f)))
		{
			Candidate = NavLoc.Location;
		}
	}
	WanderTarget = Candidate;

	const EPathFollowingRequestResult::Type MoveResult =
		MoveToLocation(WanderTarget, WanderAcceptRadius, true, true, true, false, nullptr, true);

	// 这个点去不了（导航上不可达）：略作停顿后换一个点再试
	// 注意：全部用 CreateWeakLambda 绑定 —— 客人可能在徘徊期间被销毁（排队超时等），
	// 弱引用能保证控制器失效后回调不再执行，避免访问野指针。
	if (MoveResult == EPathFollowingRequestResult::Failed)
	{
		FTimerDelegate RetryDelegate = FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			WanderToNextPoint();
		});
		World->GetTimerManager().SetTimer(WanderPauseTimerHandle, RetryDelegate,
			FMath::FRandRange(WanderPauseMin, WanderPauseMax), false);
		return;
	}

	// 轮询是否到达当前徘徊点：到达（或路径结束）后驻足一段时间，再去下一个点
	World->GetTimerManager().ClearTimer(WanderCheckTimerHandle);
	FTimerDelegate CheckDelegate = FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		if (!bWandering)
		{
			return;
		}

		APawn* P = GetPawn();
		if (!P)
		{
			StopWander();
			return;
		}

		const float Dist2D = FVector::Dist2D(P->GetActorLocation(), WanderTarget);
		const bool bArrived = Dist2D < WanderAcceptRadius;
		// 路径已结束（走完或被挡）也视为本次到达，进入驻足
		const bool bPathDone = (GetMoveStatus() != EPathFollowingStatus::Moving);

		if (bArrived || bPathDone)
		{
			UWorld* W = GetWorld();
			if (!W)
			{
				return;
			}
			W->GetTimerManager().ClearTimer(WanderCheckTimerHandle);
			StopMovement();

			// 驻足（走走停停里的"停"），时长在 [PauseMin, PauseMax] 内随机
			FTimerDelegate PauseDelegate = FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				WanderToNextPoint();
			});
			W->GetTimerManager().SetTimer(WanderPauseTimerHandle, PauseDelegate,
				FMath::FRandRange(WanderPauseMin, WanderPauseMax), false);
		}
	});
	World->GetTimerManager().SetTimer(WanderCheckTimerHandle, CheckDelegate, 0.2f, true);
}

bool AGuestAIController::AttemptMoveToSeat(AActor* Target, AGuestCharacter* Guest)
{
	if (!Target || !Guest || !Guest->IsValidLowLevel()) return false;

	// 座位 Actor 的位置可能本身不落在 NavMesh 上（例如被椅子/桌子的碰撞"挖空"）。
	// 因此用 MoveToLocation 并把 bProjectDestinationToNavigation 设为 true：
	// 引擎会先把终点投影到最近的可导航点再寻路，到达后由轮询触发 SitDown 吸附到座位。
	const EPathFollowingRequestResult::Type MoveResult =
		MoveToLocation(Target->GetActorLocation(), 5.0f, true, true, true, false, nullptr, true);

	if (bShowDebugInfo && GEngine)
	{
		const TCHAR* ResultStr =
			MoveResult == EPathFollowingRequestResult::RequestSuccessful ? TEXT("成功(已接受)") :
			MoveResult == EPathFollowingRequestResult::AlreadyAtGoal ? TEXT("已到达目标") :
			MoveResult == EPathFollowingRequestResult::Failed ? TEXT("失败(无法寻路)") : TEXT("未知");
		GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Yellow,
			FString::Printf(TEXT("[AI] %s 寻路到 %s: %s"),
				*GetName(), *Target->GetName(), ResultStr));
	}

	// —— 成功发起（或已在目标处）：开始轮询是否到达 ——
	if (MoveResult == EPathFollowingRequestResult::RequestSuccessful
		|| MoveResult == EPathFollowingRequestResult::AlreadyAtGoal)
	{
		GetWorld()->GetTimerManager().ClearTimer(ArriveCheckTimerHandle);

		FTimerDelegate TickDelegate = FTimerDelegate::CreateWeakLambda(this, [this, Guest, Target]()
		{
			// 双保险：控制器或 World 已失效（如入座途中客人/控制器被销毁）时直接退出，避免悬垂指针崩溃
			if (!IsValid(this) || !GetWorld())
			{
				return;
			}
			if (!IsValid(Guest) || !IsValid(Target))
			{
				GetWorld()->GetTimerManager().ClearTimer(ArriveCheckTimerHandle);
				return;
			}
			const float Dist = FVector::Dist(Guest->GetActorLocation(), Target->GetActorLocation());
			// 轮询打印当前距离（青色，0.25s 短显，避免刷屏）
			if (bShowDebugInfo && GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 0.25f, FColor::Cyan,
					FString::Printf(TEXT("[AI] %s 轮询座位 %s 距离: %.1f / 到达%.0f 卡住%.0f"),
						*GetName(), *Target->GetName(), Dist, SeatArriveRadius, SeatStuckRadius));
			}

			// 到达判定：距离进入 SeatArriveRadius；或被桌子/碰撞挡住已停止移动（不再 Moving）且离座位
			// 小于 SeatStuckRadius 时，也视为到达并吸附坐下，避免客人停在桌边却永远坐不上去。
			const bool bReached   = Dist < SeatArriveRadius;
			const bool bStuckNear = (GetMoveStatus() != EPathFollowingStatus::Moving) && Dist < SeatStuckRadius;
			if (bReached || bStuckNear)
			{
				if (bStuckNear && !bReached && bShowDebugInfo && GEngine)
				{
					GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Orange,
						FString::Printf(TEXT("[AI] %s 停在桌边(距%.0f<%.0f)，吸附坐下 %s"),
							*GetName(), Dist, SeatStuckRadius, *Target->GetName()));
				}
				// 到达目标（绿色）
				if (bShowDebugInfo && GEngine)
				{
					GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Green,
						FString::Printf(TEXT("[AI] %s 已到达座位 %s"), *GetName(), *Target->GetName()));
				}
				// 直接 C++ 调用到达回调（不再依赖反射 FindFunction）
				Guest->SitDown();
				StopMovement();
				if (GetWorld())
				{
					GetWorld()->GetTimerManager().ClearTimer(ArriveCheckTimerHandle);
				}
			}
		});

		GetWorld()->GetTimerManager().SetTimer(ArriveCheckTimerHandle, TickDelegate, 0.2f, true);
		return true;
	}

	// —— Failed：先打印"为什么"（起点/终点是否落在 NavMesh 上）——
	if (bShowDebugInfo)
	{
		UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(GetWorld());
		APawn* MyPawn = GetPawn();
		const FVector StartLoc = MyPawn ? MyPawn->GetActorLocation() : FVector::ZeroVector;
		const FVector GoalLoc = Target->GetActorLocation();

		// 用小范围投影："点本身"是否真的落在 NavMesh 上（范围过大容易把附近网格误报成"在网格上"）
		const FVector SmallExtent(50.f, 50.f, 100.f);
		FNavLocation StartNavLoc;
		FNavLocation GoalNavLoc;
		const bool bStartOnMesh = NavSys && NavSys->ProjectPointToNavigation(StartLoc, StartNavLoc, SmallExtent);
		const bool bGoalOnMesh = NavSys && NavSys->ProjectPointToNavigation(GoalLoc, GoalNavLoc, SmallExtent);

		// 偏移距离：为 0 表示点就落在网格上；大于 0 说明点其实悬空/偏出，网格是"附近"才有
		const float StartOffset = bStartOnMesh ? FVector::Dist(StartLoc, StartNavLoc.Location) : -1.f;
		const float GoalOffset = bGoalOnMesh ? FVector::Dist(GoalLoc, GoalNavLoc.Location) : -1.f;

		const FString Reason =
			!NavSys ? TEXT("世界无 NavigationSystem（关卡缺 NavMeshBoundsVolume 或导航未生成）")
			: !bStartOnMesh ? TEXT("客人起点不在 NavMesh 上（生成点悬空/偏出导航范围）")
			: !bGoalOnMesh ? TEXT("座位终点不在 NavMesh 上（座位偏离导航网格）")
			: TEXT("起终点都在 NavMesh 上但不连通：通常是导航被切成互不相连的岛屿（多个 NavMeshBoundsVolume 未重叠/中间有缺口）、通道窄于 Agent 半径、或导航仍未构建完");

		UE_LOG(LogTemp, Warning, TEXT("[GuestAIController] %s 寻路失败：%s | 起点=%s 在Mesh=%d 偏移=%.1f | 终点=%s 在Mesh=%d 偏移=%.1f"),
			*GetName(), *Reason, *StartLoc.ToString(), bStartOnMesh ? 1 : 0, StartOffset,
			*GoalLoc.ToString(), bGoalOnMesh ? 1 : 0, GoalOffset);

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 6.0f, FColor::Red,
				FString::Printf(TEXT("[AI] 寻路失败原因: %s"), *Reason));
		}
	}

	// —— 还没到重试上限：延迟一小段时间再试（应对"还没落地 / 导航未就绪"）——
	if (SeatMoveRetryCount < MaxSeatMoveRetries)
	{
		++SeatMoveRetryCount;
		const int32 AttemptNo = SeatMoveRetryCount;

		TWeakObjectPtr<AActor> WeakTarget(Target);
		TWeakObjectPtr<AGuestCharacter> WeakGuest(Guest);

		FTimerDelegate RetryDelegate;
		RetryDelegate.BindLambda([this, WeakTarget, WeakGuest, AttemptNo]()
		{
			if (!WeakTarget.IsValid() || !WeakGuest.IsValid()) return;
			if (bShowDebugInfo && GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Orange,
					FString::Printf(TEXT("[AI] %s 座位寻路重试第 %d/%d 次"), *GetName(), AttemptNo, MaxSeatMoveRetries));
			}
			AttemptMoveToSeat(WeakTarget.Get(), WeakGuest.Get());
		});
		GetWorld()->GetTimerManager().SetTimer(SeatRetryTimerHandle, RetryDelegate, SeatMoveRetryInterval, false);
		return false;
	}

	// —— 重试耗尽：放弃本次寻路，直接让客人离场，避免卡在原地 ——
	if (bShowDebugInfo)
	{
		UE_LOG(LogTemp, Warning, TEXT("[GuestAIController] %s 座位寻路重试 %d 次仍失败，客人离场"), *GetName(), MaxSeatMoveRetries);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 6.0f, FColor::Red,
				FString::Printf(TEXT("[AI] %s 座位寻路重试 %d 次仍失败，客人离场"), *GetName(), MaxSeatMoveRetries));
		}
	}

	StopMovement();
	GetWorld()->GetTimerManager().ClearTimer(ArriveCheckTimerHandle);
	GetWorld()->GetTimerManager().ClearTimer(SeatRetryTimerHandle);

	if (Guest && Guest->IsValidLowLevel())
	{
		Guest->GiveUpAndLeave();
	}
	return false;
}
