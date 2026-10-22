// Fill out your copyright notice in the Description page of Project Settings.

#include "NPC/GuestCharacter.h"
#include "NPC/ChairSeat.h"
#include "NPC/GuestManager.h"
#include "ShootCharacter.h"
#include "Components/SphereComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/WidgetComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UI/GuestStatusWidget.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/EnumProperty.h"
#include "NPC/GuestAIController.h"
#include "TimerManager.h"
#include "Game/BaseGameInstance.h"
#include "Game/BaseLevelBP.h"
#include "Game/OrderSystem.h"

AGuestCharacter::AGuestCharacter()
{
	PrimaryActorTick.bCanEverTick = false;

	// 由 AI 控制器自动接管
	AIControllerClass = AGuestAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	// 交互范围：玩家靠近后可按 E 接单（与上菜台/招揽台的交互球用法一致）
	InteractionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionSphere"));
	InteractionSphere->SetupAttachment(RootComponent);
	InteractionSphere->SetSphereRadius(220.f);
	InteractionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionSphere->SetCollisionResponseToAllChannels(ECR_Overlap);
	InteractionSphere->SetGenerateOverlapEvents(true);

	// 客人与客人之间的避让：
	// 1) 开启 RVO 避让，AI 走向同一方向（如同时进场/同时离场）时会互相绕行，而不是顶在一起。
	// 2) 设定避让分组/考虑半径，默认值即可正常工作。
	// 3) 关键：把胶囊对 Pawn（即其他客人）的碰撞响应改为 Overlap，取消相互之间的“硬阻挡”。
	//    否则即使有 RVO，坐着的客人挡在过道时，移动客人会被物理顶住、导航又只算直线，导致走不到目的地。
	//    仍保留对世界的 Block（墙/桌），不会穿墙；RVO 会尽量绕行，仅在拥挤时允许彼此穿插。
	if (UCharacterMovementComponent* Mov = GetCharacterMovement())
	{
		Mov->bUseRVOAvoidance = true;
		Mov->AvoidanceWeight = 1.0f;
		Mov->AvoidanceConsiderationRadius = 250.f;
		Mov->SetAvoidanceGroup(1);
		Mov->SetGroupsToAvoid(1);
		Mov->SetGroupsToIgnore(0);
	}

	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		// 使用自定义“Guest”对象通道区分客人：
		// - 把胶囊对象类型改为 Guest；
		// - 客人之间（Guest 通道）设为 Overlap，互不硬阻挡，避免被其他客人顶住走不到目的地；
		// - 对 Pawn（玩家）仍保持默认 Block，因此客人仍能挡住玩家；
		// - 对世界（墙/桌）仍 Block，不会穿墙。RVO 仍会尽量绕行，拥挤时才允许彼此穿插。
		Capsule->SetCollisionObjectType(ECC_GameTraceChannel3);
		Capsule->SetCollisionResponseToChannel(ECC_GameTraceChannel3, ECR_Overlap);
	}

	// 头顶状态气泡：屏幕空间、始终朝向相机，挂在头顶上方，不参与碰撞
	StatusWidgetComp = CreateDefaultSubobject<UWidgetComponent>(TEXT("StatusWidgetComp"));
	StatusWidgetComp->SetupAttachment(RootComponent);
	StatusWidgetComp->SetRelativeLocation(FVector(0.f, 0.f, 120.f));
	StatusWidgetComp->SetWidgetSpace(EWidgetSpace::Screen);
	StatusWidgetComp->SetDrawAtDesiredSize(true);
	StatusWidgetComp->SetWidgetClass(UGuestStatusWidget::StaticClass());
	StatusWidgetComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	StatusWidgetComp->SetGenerateOverlapEvents(false);

	// 头顶状态气泡的台词 / 颜色 / 贴图改由 GuestStatusWidget 持有的配置表（StatusConfigTable）
	// 按状态随机抽取，C++ 不再硬编码，便于策划在编辑器里维护。
}

void AGuestCharacter::BeginPlay()
{
	Super::BeginPlay();
	SetGuestState(EGuestState::WalkingIn);

	// 确保胶囊体对 Pawn（其他客人/玩家）通道设为阻挡，避免蓝图默认值把碰撞设成
	// Overlap 导致客人互相穿模、不互相避让。物理阻挡 + RVO 绕行双保险。
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	}

	// 交互球事件：把靠近的玩家注册为"可接单"目标
	if (InteractionSphere)
	{
		InteractionSphere->OnComponentBeginOverlap.AddDynamic(this, &AGuestCharacter::OnInteractionOverlap);
		InteractionSphere->OnComponentEndOverlap.AddDynamic(this, &AGuestCharacter::OnInteractionEndOverlap);
	}
}

void AGuestCharacter::ReleaseSeatIfOccupied()
{
	if (SeatActor)
	{
		// 仅当本客人确实仍占用该座位时才释放，避免误释放他人座位
		if (SeatActor->Occupant == this)
		{
			SeatActor->Release();
		}
		SeatActor = nullptr;
	}
}

void AGuestCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 无论客人因何种原因被销毁（正常离场 / 关卡卸载 / 管理器清空 / 超时销毁等），
	// 都在此兜底释放占用的座位，避免座位残留“占用”状态导致后续客人无法入座。
	ReleaseSeatIfOccupied();

	// 清理本对象相关定时器，避免销毁后回调访问悬垂 this
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(QueueTimerHandle);
		World->GetTimerManager().ClearTimer(GuestTimerHandle);
		World->GetTimerManager().ClearTimer(LeaveTimeoutTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}

void AGuestCharacter::SetGuestState(EGuestState NewState)
{
	const EGuestState OldState = GuestState;
	GuestState = NewState;
	OnGuestStateChanged(NewState);
	UpdateStatusWidget(NewState);
}

void AGuestCharacter::UpdateStatusWidget(EGuestState NewState)
{
	// 仅广播状态 + 是否已吃饱（离场区分“满意离开”与“等不及走了”）；
	// 台词 / 颜色 / 贴图由 GuestStatusWidget 从配置表按状态随机抽取并显示。

	// 主动把自身赋给头顶气泡的 OwnerCharacter（WidgetComponent 创建的世界空间控件
	// 无法从控件内部反向拿到所属 Actor，故由这里补设；每次状态变化都设，覆盖控件晚创建的情况）。
	if (UGuestStatusWidget* GSW = Cast<UGuestStatusWidget>(StatusWidgetComp->GetUserWidgetObject()))
	{
		const bool bWasNull = (GSW->OwnerCharacter == nullptr);
		GSW->OwnerCharacter = this;
		// 仅在第一次赋值时通知蓝图去绑定委托，避免重复绑出多个处理函数。
		if (bWasNull)
		{
			GSW->OnOwnerCharacterSet();
		}
	}

	const bool bSatisfied = (NewState == EGuestState::Leaving && bHasEaten);
	OnGuestStatusUpdated.Broadcast(NewState, bSatisfied);
}

AChairSeat* AGuestCharacter::FindRandomFreeSeat() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	TArray<AActor*> AllSeats;
	UGameplayStatics::GetAllActorsOfClass(World, AChairSeat::StaticClass(), AllSeats);

	TArray<AChairSeat*> FreeSeats;
	for (AActor* Actor : AllSeats)
	{
		if (AChairSeat* Seat = Cast<AChairSeat>(Actor))
		{
			if (!Seat->bIsOccupied)
			{
				FreeSeats.Add(Seat);
			}
		}
	}

	if (FreeSeats.Num() == 0)
	{
		return nullptr;
	}

	// 从空闲座位中随机挑选一个
	return FreeSeats[FMath::RandRange(0, FreeSeats.Num() - 1)];
}

void AGuestCharacter::StartWalkingToSeat()
{
	// 若未指定座位，则随机找一个未被占用的座位
	if (!SeatActor)
	{
		SeatActor = FindRandomFreeSeat();
	}

	// 仍然没有可用座位（场景无座位或全被占用）→ 进入排队等待
	if (!SeatActor)
	{
		if (bShowDebugInfo && GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 4.0f, FColor::Cyan,
				FString::Printf(TEXT("[Guest] %s 找不到空闲座位，进入排队等待"), *GetName()));
		}
		JoinQueue();
		return;
	}

	// 先标记占用，避免多个客人同时选中同一座位
	if (!SeatActor->Occupy(this))
	{
		// 占用失败（竞态中被占用），重新随机找一个
		SeatActor = FindRandomFreeSeat();
		if (!SeatActor || !SeatActor->Occupy(this))
		{
			JoinQueue();
			return;
		}
	}

	SetGuestState(EGuestState::WalkingIn);

	// 入座超时兜底：若长时间走不到座位（被桌子/碰撞挡住、导航投影达不到），
	// 放弃并离场、释放座位，避免永久卡在 WalkingIn 导致座位被占用却无人就座。
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(GuestTimerHandle,
			FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				if (GuestState == EGuestState::WalkingIn)
				{
					// 始终打印（不依赖 bShowDebugInfo）：这是座位被“幽灵占用”的关键诊断信息
					UE_LOG(LogTemp, Warning,
						TEXT("[GuestCharacter] %s 走向座位超时(%.1fs)仍无法入座（被桌子/碰撞阻挡或导航不可达），放弃并离场、释放座位 %s"),
						*GetName(), SeatWalkTimeout,
						SeatActor ? *SeatActor->GetName() : TEXT("无"));
					GiveUpAndLeave();
				}
			}),
			SeatWalkTimeout, false);
	}

	if (AGuestAIController* AIC = Cast<AGuestAIController>(GetController()))
	{
		AIC->MoveToSeatActor(SeatActor, this);
	}
}

void AGuestCharacter::JoinQueue()
{
	// 没有管理器（异常情况）：退化为直接离场
	if (!Manager)
	{
		if (bShowDebugInfo)
		{
			UE_LOG(LogTemp, Warning, TEXT("[GuestCharacter] %s 需要排队但没有 Manager，直接离场"), *GetName());
		}
		StartLeaving();
		return;
	}

	// 切到"等待"状态，供动画蓝图播放等待动画
	SetGuestState(EGuestState::Waiting);

	// 停住当前移动，随后由管理器下令在等候区内徘徊
	if (AGuestAIController* AIC = Cast<AGuestAIController>(GetController()))
	{
		AIC->StopMovement();
	}

	// 入队（管理器内部会让客人开始在等候范围内徘徊）
	QueueIndex = Manager->EnqueueGuest(this);

	if (bShowDebugInfo && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Cyan,
			FString::Printf(TEXT("[Guest] %s 排队等候（序号 %d，超时 %.0fs）"), *GetName(), QueueIndex, QueueWaitTimeout));
	}

	// 排队超时：离场并扣除声誉
	GetWorld()->GetTimerManager().ClearTimer(QueueTimerHandle);
	FTimerDelegate TimeoutDelegate;
	TimeoutDelegate.BindLambda([this]()
	{
		if (GuestState == EGuestState::Waiting)
		{
			OnQueueTimeout();
		}
	});
	GetWorld()->GetTimerManager().SetTimer(QueueTimerHandle, TimeoutDelegate, QueueWaitTimeout, false);
}

void AGuestCharacter::StartWaitingWander(const FVector& Center, float Radius, float PauseMin, float PauseMax)
{
	if (AGuestAIController* AIC = Cast<AGuestAIController>(GetController()))
	{
		AIC->BeginWander(Center, Radius, PauseMin, PauseMax);
	}
}

void AGuestCharacter::PromoteFromQueue()
{
	GetWorld()->GetTimerManager().ClearTimer(QueueTimerHandle);
	QueueIndex = -1;

	// 停止徘徊，准备前往座位（MoveToSeatActor 内部也会再停一次，这里提前停更直观）
	if (AGuestAIController* AIC = Cast<AGuestAIController>(GetController()))
	{
		AIC->StopWander();
	}

	if (bShowDebugInfo && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Green,
			FString::Printf(TEXT("[Guest] %s 等到空位，前往入座"), *GetName()));
	}

	// 有空位了，重新寻找座位并前往（找不到会再次入队）
	StartWalkingToSeat();
}

void AGuestCharacter::OnQueueTimeout()
{
	if (bShowDebugInfo)
	{
		UE_LOG(LogTemp, Warning, TEXT("[GuestCharacter] %s 排队等待超时，离场并扣除声誉"), *GetName());

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Red,
				FString::Printf(TEXT("[Guest] %s 等待超时，离场（扣声誉）"), *GetName()));
		}
	}

	// 由管理器移出队列并扣除声誉
	if (Manager)
	{
		Manager->HandleQueueTimeout(this);
	}
	QueueIndex = -1;

	StartLeaving();
}

void AGuestCharacter::SitDown()
{
	if (!SeatActor)
	{
		return;
	}
	// 已成功入座：取消“走向座位超时”兜底（避免它随后误触发离场）
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(GuestTimerHandle);
	}
	// 占用座位并吸附到坐下位置
	SeatActor->Occupy(this);
	SetActorTransform(SeatActor->GetSitTransform());

	if (AGuestAIController* AIC = Cast<AGuestAIController>(GetController()))
	{
		AIC->StopMovement();
	}

	// 设置动画状态标记，由 ShootAnimInstance 驱动坐下动画（不再使用 PlayAnimMontage）
	bIsSitting = true;
	bIsEating = false;
	bIsLeaving = false;

	SetGuestState(EGuestState::WaitingFood);

	// 接单方式：
	// - 自动接单：坐下即向订单系统点单（订单出现时机 = 客人点餐）
	// - 需要玩家接单：坐下后等待玩家按 E 接单（见 AcceptOrderByPlayer），未接单则超时离场
	if (OrderAcceptMode == EGuestOrderAcceptMode::AutoAccept)
	{
		// 若没有订单系统（未初始化 / 未启动），退化为“自己等一会儿就走”，避免客人卡死在座位上
		const bool bOrdered = PlaceOrder();
		if (bShowDebugInfo && GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.0f, bOrdered ? FColor::Green : FColor::Red,
				FString::Printf(TEXT("[Guest] %s 坐下 → 自动接单：%s"), *GetName(),
					bOrdered ? TEXT("已下单") : TEXT("下单失败（见下方橙色原因）")));
		}

		if (!bOrdered)
		{
			StartWaitFoodTimeout();
		}
	}
	else
	{
		// 等待玩家接单；长时间无人接单则兜底离场
		StartWaitFoodTimeout();
	}
}

void AGuestCharacter::StartWaitFoodTimeout()
{
	// 兜底：无订单系统时，等待上菜超时则离开
	GetWorld()->GetTimerManager().SetTimerForNextTick([this]()
	{
		FTimerDelegate TimeoutDelegate;
		TimeoutDelegate.BindLambda([this]()
		{
			if (GuestState == EGuestState::WaitingFood)
			{
				StartLeaving();
			}
		});
		GetWorld()->GetTimerManager().SetTimer(GuestTimerHandle, TimeoutDelegate, WaitFoodTimeout, false);
	});
}

bool AGuestCharacter::PlaceOrder()
{
	// 失败原因统一打印（屏幕 + Output Log），便于定位点单失败。
	// 注意：这里不受 bShowDebugInfo 控制——失败属于错误信息，默认就要能被看到。
	auto LogFail = [this](const TCHAR* Reason)
	{
		const FString Msg = FString::Printf(TEXT("[Guest] 客人 %s PlaceOrder 失败：%s"), *GetName(), Reason);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 4.0f, FColor::Orange, Msg);
		}
		UE_LOG(LogTemp, Warning, TEXT("%s"), *Msg);
	};

	UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance());
	if (!GI || !GI->curLevelBP)
	{
		LogFail(TEXT("GameInstance 或 curLevelBP 未就绪"));
		return false;
	}

	AOrderSystem* OS = GI->curLevelBP->orderSystem;
	if (!OS)
	{
		LogFail(TEXT("订单系统未创建（关卡蓝图未调用 InitOrderSystem）"));
		return false;
	}

	if (!OS->IsStarted)
	{
		LogFail(TEXT("订单系统未启动（IsStarted=false，需调用 StartSystem）"));
		return false;
	}

	CurrentOrder = OS->RequestOrderForGuest(this);
	if (!CurrentOrder)
	{
		LogFail(TEXT("取不到订单（orderCfg 为空或 Orders 无配置）"));
		return false;
	}

	// 调试信息：客人点单成功（订单号 / 套餐 / 菜品 / 价格 / 等待时长 / 接单方式）
	if (bShowDebugInfo)
	{
		const FOrderData OrderData = CurrentOrder->GetOrderData();
		const TCHAR* ModeText = (OrderAcceptMode == EGuestOrderAcceptMode::AutoAccept)
			? TEXT("自动接单") : TEXT("玩家接单");

		FString FoodList;
		for (int32 i = 0; i < OrderData.FoodIDs.Num(); i++)
		{
			if (i > 0)
			{
				FoodList += TEXT(",");
			}
			FoodList += FString::FromInt(OrderData.FoodIDs[i]);
		}

		const FString Msg = FString::Printf(
			TEXT("[Guest] 客人 %s 点单 → 订单#%d 【%s】菜品[%s] 价格%d 等待%.0fs（%s）"),
			*GetName(), CurrentOrder->orderId, *OrderData.SetName, *FoodList,
			OrderData.Price, OrderData.MaxWaitTime, ModeText);

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 4.0f, FColor::Yellow, Msg);
		}
		UE_LOG(LogTemp, Log, TEXT("%s"), *Msg);
	}

	return true;
}

bool AGuestCharacter::CanAcceptOrderByPlayer() const
{
	return OrderAcceptMode == EGuestOrderAcceptMode::NeedPlayerAccept
		&& GuestState == EGuestState::WaitingFood
		&& CurrentOrder == nullptr;
}

bool AGuestCharacter::AcceptOrderByPlayer()
{
	if (!CanAcceptOrderByPlayer())
	{
		return false;
	}

	// 玩家接单：此时才向订单系统下单（订单出现时机 = 接单）
	if (!PlaceOrder())
	{
		// 没有订单系统：保持兜底超时，避免客人卡死在座位上
		return false;
	}

	// 接单成功：取消兜底超时，改由订单系统的等待超时驱动（见 OnOrderFailed）
	GetWorld()->GetTimerManager().ClearTimer(GuestTimerHandle);

	// 点单成功的调试信息已由 PlaceOrder 统一打印（含订单号与“玩家接单”标记）
	return true;
}

void AGuestCharacter::OnInteractionOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	// 只有“需要玩家接单”模式才需要玩家靠近交互
	if (OrderAcceptMode != EGuestOrderAcceptMode::NeedPlayerAccept)
	{
		return;
	}

	if (AShootCharacter* Player = Cast<AShootCharacter>(OtherActor))
	{
		Player->SetOverlappingGuest(this);
	}
}

void AGuestCharacter::OnInteractionEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (AShootCharacter* Player = Cast<AShootCharacter>(OtherActor))
	{
		// 仅当仍指向自己时才清空，避免多个客人同时重叠时互相串扰
		if (Player->GetOverlappingGuest() == this)
		{
			Player->SetOverlappingGuest(nullptr);
		}
	}
}

void AGuestCharacter::OnOrderServed()
{
	// 订单被完成（上菜成功）：清掉订单引用并开始进食
	CurrentOrder = nullptr;

	// 特殊客人：在 EconomySystem 完成订单的基础声誉之上，额外提升声誉
	if (GuestType == EGuestType::Special && SpecialReputationBonus > 0)
	{
		if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance()))
		{
			if (GI->economySystem)
			{
				GI->economySystem->AddReputation(SpecialReputationBonus);
				if (bShowDebugInfo && GEngine)
				{
					GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Magenta,
						FString::Printf(TEXT("[Guest] 特殊客人 %s 额外声誉 +%d"), *GetName(), SpecialReputationBonus));
				}
			}
		}

		// 通知蓝图做金色特效 / 音效 / 飘字等
		OnSpecialGuestServed();
	}

	StartEating();
}

void AGuestCharacter::OnOrderFailed()
{
	// 订单超时失败：清掉订单引用并离场
	CurrentOrder = nullptr;

	if (bShowDebugInfo && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 4.0f, FColor::Red,
			FString::Printf(TEXT("[Guest] %s 订单超时，离场"), *GetName()));
	}

	// 仅在仍处于等餐状态时才离场（已开始进食/已在离场则不打断）
	if (GuestState == EGuestState::WaitingFood)
	{
		StartLeaving();
	}
}

void AGuestCharacter::StartEating()
{
	if (GuestState != EGuestState::WaitingFood)
	{
		return;
	}

	SetGuestState(EGuestState::Eating);

	// 标记已用餐：离场台词据此显示“这顿饭真好吃”，而非“等太久了，不吃了”
	bHasEaten = true;

	// 设置动画状态标记，由 ShootAnimInstance 驱动进食动画
	bIsEating = true;
	bIsSitting = true;

	// 进食结束后离开
	GetWorld()->GetTimerManager().SetTimerForNextTick([this]()
	{
		FTimerDelegate EatDelegate;
		EatDelegate.BindLambda([this]()
		{
			StartLeaving();
		});
		GetWorld()->GetTimerManager().SetTimer(GuestTimerHandle, EatDelegate, EatDuration, false);
	});
}

void AGuestCharacter::GiveUpAndLeave()
{
	// AI 已确认无法寻路到座位：为避免客人卡在原地，直接走离场流程。
	// StartLeaving 内部会先释放已占用的座位；若配置了离场点则走出场后销毁，
	// 未配置离场点则立即销毁。
	if (bShowDebugInfo)
	{
		UE_LOG(LogTemp, Warning, TEXT("[GuestCharacter] %s 因无法寻路到座位，放弃并离场"), *GetName());
	}
	StartLeaving();
}

void AGuestCharacter::StartLeaving()
{
	// 排队 / 等餐 / 离场兜底计时器（若在等待中被触发离场）先清掉
	GetWorld()->GetTimerManager().ClearTimer(QueueTimerHandle);
	GetWorld()->GetTimerManager().ClearTimer(GuestTimerHandle);
	GetWorld()->GetTimerManager().ClearTimer(LeaveTimeoutTimerHandle);

	// 若还挂着未完成的订单，通知订单系统取消，避免遗留“幽灵订单”占用订单位
	if (CurrentOrder)
	{
		if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance()))
		{
			if (GI->curLevelBP && GI->curLevelBP->orderSystem)
			{
				GI->curLevelBP->orderSystem->CancelOrderForGuest(this);
			}
		}
		CurrentOrder = nullptr;
	}

	// 记录是否真的释放了座位：只有释放了才通知管理器去提升排队客人
	const bool bHadSeat = (SeatActor != nullptr);
	if (SeatActor)
	{
		SeatActor->Release();
		SeatActor = nullptr;
	}

	SetGuestState(EGuestState::Leaving);

	// 设置动画状态标记，由 ShootAnimInstance 驱动离开动画
	bIsLeaving = true;
	bIsSitting = false;
	bIsEating = false;

	// 座位空出：通知管理器让队首客人来占座
	if (bHadSeat && Manager)
	{
		Manager->OnSeatFreed();
	}

	if (LeaveLocation.IsNearlyZero())
	{
		// 未配置离场点，直接销毁
		if (bShowDebugInfo)
		{
			UE_LOG(LogTemp, Warning, TEXT("[GuestCharacter] %s 未配置离场点(LeaveLocation)，直接销毁（GuestManager 的 LeaveLocation 未设置）"), *GetName());
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Yellow,
					FString::Printf(TEXT("[Guest] %s 无离场点，直接销毁"), *GetName()));
			}
		}
		SetGuestState(EGuestState::Dead);
		Destroy();
		return;
	}

	if (AGuestAIController* AIC = Cast<AGuestAIController>(GetController()))
	{
		AIC->MoveToSeatLocation(LeaveLocation, this);
	}

	// 离场兜底：若离场点导航不可达导致永远走不到（轮询距离 < 80 不触发），
	// 超时后强制销毁，避免客人永久残留场景。正常到达会先清掉本定时器。
	GetWorld()->GetTimerManager().SetTimerForNextTick([this]()
	{
		FTimerDelegate LeaveDelegate;
		LeaveDelegate.BindLambda([this]()
		{
			if (GuestState != EGuestState::Dead)
			{
				OnReachLeavePoint();
			}
		});
		GetWorld()->GetTimerManager().SetTimer(LeaveTimeoutTimerHandle, LeaveDelegate, 10.0f, false);
	});
}

void AGuestCharacter::OnReachLeavePoint()
{
	// 取消离场兜底计时器（正常到达时本函数先被调用，避免重复销毁）
	GetWorld()->GetTimerManager().ClearTimer(LeaveTimeoutTimerHandle);

	// 销毁前先从管理器反注册，避免列表残留
	if (Manager)
	{
		Manager->UnregisterGuest(this);
	}
	SetGuestState(EGuestState::Dead);
	Destroy();
}
