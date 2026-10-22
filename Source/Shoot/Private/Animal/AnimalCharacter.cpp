// Fill out your copyright notice for Project Settings.


#include "Animal/AnimalCharacter.h"
#include "Animal/AnimalAnimation.h"
#include "ShootCharacter.h"
#include "Item/ItemActor.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "AIController.h"
#include "NavigationSystem.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/EnumProperty.h"
#include "DrawDebugHelpers.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISense_Hearing.h"
#include "GameFramework/PlayerState.h"
#include "Components/WidgetComponent.h"
#include "Blueprint/UserWidget.h"


AAnimalCharacter::AAnimalCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	GetCapsuleComponent()->SetGenerateOverlapEvents(true);
	// 自动生成并 Possess AIController，使 MoveToLocation 可用
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	bUseControllerRotationYaw = false;
	if (UCharacterMovementComponent* Mov = GetCharacterMovement())
	{
		Mov->bOrientRotationToMovement = true;

		// 开启 AI 互相避让，防止动物间相交重叠(基于 RVO)
		// 注意：UE5 的 RVO 避让半径由胶囊体自动决定(GetRVOAvoidanceRadius)，无单独 Radius 属性
		Mov->bUseRVOAvoidance = bEnableAvoidance;
		Mov->SetAvoidanceEnabled(bEnableAvoidance);
		Mov->AvoidanceConsiderationRadius = AvoidanceConsiderationRadius;
		Mov->AvoidanceWeight = AvoidanceWeight;
		// 使用组 1，默认所有动物同组互相避让
		Mov->SetAvoidanceGroup(1);
		Mov->SetGroupsToAvoid(1);
		Mov->SetGroupsToIgnore(0);
	}

	// 创建感知组件
	PerceptionComp = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("PerceptionComp"));

	// 警觉/逃跑标记：问号(思考中) 与 感叹号(逃跑中)
	QuestionMarkComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("QuestionMark"));
	if (QuestionMarkComp)
	{
		QuestionMarkComp->SetupAttachment(GetMesh() ? (USceneComponent*)GetMesh() : (USceneComponent*)GetRootComponent());
		QuestionMarkComp->SetRelativeLocation(FVector(0.f, 0.f, 120.f)); // 头顶偏移，蓝图可改
		QuestionMarkComp->SetVisibility(false);
		QuestionMarkComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	ExclamationMarkComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ExclamationMark"));
	if (ExclamationMarkComp)
	{
		ExclamationMarkComp->SetupAttachment(GetMesh() ? (USceneComponent*)GetMesh() : (USceneComponent*)GetRootComponent());
		ExclamationMarkComp->SetRelativeLocation(FVector(0.f, 0.f, 120.f));
		ExclamationMarkComp->SetVisibility(false);
		ExclamationMarkComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	// 默认动画类(蓝图可覆盖)
	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		MeshComp->SetAnimInstanceClass(UAnimalAnimation::StaticClass());
	}

	// 头顶合并 Widget：同时显示需求图标与气泡文本。
	// 初始隐藏；需求状态非 None 时显示(承载需求图标)，ShowBubble 触发时也显示(气泡)。
	// 分屏注意：必须用 World 空间！Screen 空间只会渲染到 Player0 视口，第二个本地玩家看不到。
	// World 空间会把 Widget 当成场景几何(贴图平面)，两个本地玩家的视口都会渲染到。
	BubbleWidgetComp = CreateDefaultSubobject<UWidgetComponent>(TEXT("BubbleWidget"));
	if (BubbleWidgetComp)
	{
		BubbleWidgetComp->SetupAttachment(GetMesh() ? (USceneComponent*)GetMesh() : (USceneComponent*)GetRootComponent());
		BubbleWidgetComp->SetRelativeLocation(FVector(0.f, 0.f, 160.f)); // 头顶上方
		// World 空间：作为场景几何，两个分屏视口都能看到
		BubbleWidgetComp->SetWidgetSpace(EWidgetSpace::World);
		// 法线朝 +Z(朝上)，俯视的两个相机都能看到并正常阅读；文本平面水平铺在头顶
		BubbleWidgetComp->SetRelativeRotation(FRotator(-90.f, 0.f, 0.f));
		// 像素->世界缩放，避免平面过大(DrawSize 300x120 * 0.15 ≈ 45x18 世界单位)
		BubbleWidgetComp->SetWorldScale3D(FVector(0.15f));
		BubbleWidgetComp->SetDrawSize(FVector2D(300.f, 120.f));
		BubbleWidgetComp->SetVisibility(false); // 初始隐藏
		BubbleWidgetComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	// 攻击命中检测盒：比胶囊更贴合动物体型，专门作为武器伤害检测的命中体
	DamageBox = CreateDefaultSubobject<UBoxComponent>(TEXT("DamageBox"));
	if (DamageBox)
	{
		// 挂在胶囊(根组件)上，跟随动物移动
		DamageBox->SetupAttachment(GetCapsuleComponent());
		DamageBox->SetRelativeLocation(FVector::ZeroVector);
		DamageBox->SetBoxExtent(DamageBoxExtent);
		// 仅查询(不产生物理碰撞)，避免把动物自身顶飞/卡住
		DamageBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		DamageBox->SetCollisionObjectType(ECC_WorldDynamic);
		// 默认对所有通道 Overlap；再单独关闭与自身胶囊(Pawn)的碰撞，
		// 并让武器的 BoxTrace(Visibility) 能命中本盒
		DamageBox->SetCollisionResponseToAllChannels(ECR_Overlap);
		DamageBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);        // 不与自身胶囊碰撞
		DamageBox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);   // 供武器 BoxTrace(Visibility) 命中
		DamageBox->SetGenerateOverlapEvents(true);
	}
}

void AAnimalCharacter::BeginPlay()
{
	Super::BeginPlay();
	SpawnOrigin = GetActorLocation();
	if (HomeLocation.IsZero())
	{
		HomeLocation = SpawnOrigin;
	}
	CurrentMoveSpeed = MoveSpeed; // 初始为走路速度
	SetupPerception();
	if (bAutoSpawn)
	{
		StartSpawning();
	}

	// 广播一次当前需求状态，便于合并 Widget 绑定后对齐初始贴图/显隐
	OnNeedStateChanged.Broadcast(CurrentNeedState);

	// 注入合并头顶 Widget 的 Animal 引用(Widget 类已在蓝图组件上指定)
	if (BubbleWidgetComp)
	{
		if (UAnimalBubbleWidget* BubbleWdg = Cast<UAnimalBubbleWidget>(BubbleWidgetComp->GetUserWidgetObject()))
		{
			BubbleWdg->OwningAnimal = this;
		}
	}
}

void AAnimalCharacter::ShowBubble(const FText& Text, float Duration)
{
	// 文本为空或尚未创建气泡 Widget 则忽略
	if (Text.IsEmpty() || !BubbleWidgetComp)
	{
		return;
	}
	const float ShowTime = (Duration > 0.f) ? Duration : BubbleDuration;

	// 实时获取 Widget 实例并弹出气泡(不再缓存)
	if (UAnimalBubbleWidget* BubbleWdg = Cast<UAnimalBubbleWidget>(BubbleWidgetComp->GetUserWidgetObject()))
	{
		// 确保组件可见(气泡可能在需求为 None 时触发，如逃跑/警觉台词)
		BubbleWidgetComp->SetVisibility(true);
		BubbleWdg->ShowBubbleText(Text, ShowTime);
	}
}

void AAnimalCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bDie)
	{
		return;
	}

	UpdateAIDistanceCull();
	if (!bAIActive)
	{
		// 距离过远停用 AI：也输出一次日志便于观察
		if (bEnableDebugLog && GetWorld() && FMath::FloorToInt(GetWorld()->GetTimeSeconds()) != LastLogSecond)
		{
			LastLogSecond = FMath::FloorToInt(GetWorld()->GetTimeSeconds());
			UE_LOG(LogTemp, Log, TEXT("[Animal:%s] AI 停用(玩家距离过远)"), *GetName());
		}
		return;
	}

	// 昼夜
	const bool bIsNight = false; // TODO: 接入昼夜循环系统(从 GameState/Subsystem 读取)
	UpdateDayNight(bIsNight);

	UpdateNeeds(DeltaTime);

	// 进食动画计时
	if (EatRemaining > 0.f)
	{
		EatRemaining -= DeltaTime;
		if (EatRemaining <= 0.f)
		{
			EatRemaining = 0.f;
			bIsEating = false;
		}
	}

	UpdateBehavior(DeltaTime);

	// 限频(每秒)输出状态日志
	if (bEnableDebugLog && GetWorld())
	{
		const int32 CurSec = FMath::FloorToInt(GetWorld()->GetTimeSeconds());
		if (CurSec != LastLogSecond)
		{
			LastLogSecond = CurSec;
			const FString StateLog = FString::Printf(
				TEXT("[Animal:%s] AI:Active 休息:%s 逃跑:%s 行为:%s 饥饿:%.0f 口渴:%.0f 觅食/水:%s 驯服:%s 角色:%s 交战:%s"),
				*GetName(),
				bResting ? TEXT("是") : TEXT("否"),
				bFleeing ? TEXT("是") : TEXT("否"),
				*UEnum::GetValueAsString(CurrentBehavior),
				Hunger, Thirst,
				bSeekingNeed ? TEXT("是") : TEXT("否"),
				*UEnum::GetValueAsString(TameState),
				*UEnum::GetValueAsString(AnimalRole),
				*UEnum::GetValueAsString(EngageBehavior));
			UE_LOG(LogTemp, Log, TEXT("%s"), *StateLog);
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 1.f, FColor::Cyan, StateLog);
			}
		}
	}
}

void AAnimalCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
}

void AAnimalCharacter::GetHit_Implementation(const FVector& ImpactPoint, AActor* Hitter)
{
	if (bDie)
	{
		return;
	}
	// 命中处理已统一在 AWeapon::DealDamageToAnimal 中结算血量(读取血量 / 触发死亡或扣血)。
	// 其它来源(非武器)直接触发死亡，保持兼容。
	Die();
}

float AAnimalCharacter::ApplyDamage(float DamageAmount)
{
	if (bDie)
	{
		return Health;
	}
	Health = FMath::Max(0.f, Health - FMath::Max(0.f, DamageAmount));
	if (bEnableDebugLog)
	{
		UE_LOG(LogTemp, Log, TEXT("[%s] 受到 %.1f 伤害，剩余血量 %.1f / %.1f"),
			*GetName(), DamageAmount, Health, MaxHealth);
	}
	return Health;
}

void AAnimalCharacter::TakeWeaponDamage(float DamageAmount)
{
	if (bDie)
	{
		return;
	}
	const float CurrentHealth = Health;
	const float Remaining = CurrentHealth - FMath::Max(0.f, DamageAmount);

	if (bEnableDebugLog)
	{
		UE_LOG(LogTemp, Log, TEXT("[%s] 受到武器攻击：当前血量 %.1f，本次伤害 %.1f，剩余 %.1f"),
			*GetName(), CurrentHealth, DamageAmount, Remaining);
	}

	// 命中处理(读血 / 判死 / 扣血)都在动物自身完成
	if (Remaining <= 0.f)
	{
		Die(); // 血量耗尽：触发死亡(掉落 + 销毁)
	}
	else
	{
		ApplyDamage(DamageAmount); // 更新血量(扣血，不致死)
	}
}

void AAnimalCharacter::Die()
{
	if (bDie)
	{
		return;
	}
	bDie = true;
	
	// 播放死亡特效：表现“动物 -> 掉落物”的变化过程(鹿化为鹿肉)
	if (DeathEffect && GetWorld())
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(),
			DeathEffect,
			GetActorLocation(),         // 在动物位置播放
			GetActorRotation(),
			FVector::OneVector,
			true,                        // bAutoDestroy
			true,                        // bAutoActivate
			ENCPoolMethod::None);
	}
	
	// 被击杀时生成掉落物(蓝图在 DropActorClass 直接选择，如鸡腿/鹿肉)
	if (DropActorClass && GetWorld())
	{
		// 略微抬高到地面以上，避免卡在脚下
		const FVector DropLoc = GetActorLocation() + FVector(0.f, 0.f, 30.f);
		FRotator DropRot = GetActorRotation();
		FActorSpawnParameters Params;
		Params.Owner = this;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		GetWorld()->SpawnActor<AActor>(DropActorClass, DropLoc, DropRot, Params);
		if (bEnableDebugLog)
		{
			UE_LOG(LogTemp, Log, TEXT("[%s] 血量耗尽，被击杀，掉落 Actor"), *GetName());
		}
	}

	Destroy();
}

// ============================================================
//  感知距离剔除（性能）
// ============================================================
void AAnimalCharacter::UpdateAIDistanceCull()
{
	if (AIDisableDistance <= 0.f)
	{
		bAIActive = true;
		return;
	}
	AActor* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Player)
	{
		bAIActive = true;
		return;
	}
	const float Dist = FVector::DistSquared(GetActorLocation(), Player->GetActorLocation());
	bAIActive = Dist <= AIDisableDistance * AIDisableDistance;
}

// ============================================================
//  昼夜
// ============================================================
void AAnimalCharacter::UpdateDayNight(bool bIsNight)
{
	// 夜行动物: 夜间活跃, 白天休息; 昼行动物: 反之
	const bool bShouldRest = bIsNocturnal ? !bIsNight : bIsNight;
	if (bShouldRest && !bResting)
	{
		bResting = true;
		if (AAIController* AIC = GetAnimalController())
		{
			AIC->StopMovement(); // 休息时停止移动
		}
	}
	else if (!bShouldRest && bResting)
	{
		bResting = false;
	}
}

// ============================================================
//  需求系统
// ============================================================
void AAnimalCharacter::UpdateNeeds(float DeltaTime)
{
	Hunger = FMath::Min(100.f, Hunger + HungerRate * DeltaTime);
	Thirst = FMath::Min(100.f, Thirst + ThirstRate * DeltaTime);
	bSeekingNeed = (Hunger >= NeedThreshold) || (Thirst >= NeedThreshold);

	// 计算需求显示状态(供 NeedWidget 切换贴图)
	const bool bHungry = Hunger >= NeedThreshold;
	const bool bThirsty = Thirst >= NeedThreshold;
	EAnimalNeedState NewState = EAnimalNeedState::None;
	if (bHungry && bThirsty)
	{
		NewState = EAnimalNeedState::Both;
	}
	else if (bHungry)
	{
		NewState = EAnimalNeedState::Food;
	}
	else if (bThirsty)
	{
		NewState = EAnimalNeedState::Water;
	}

	// 仅在状态变化时更新并广播(供 Widget 实时切换贴图/显示气泡，无需每帧 Tick)
	if (NewState != CurrentNeedState)
	{
		CurrentNeedState = NewState;
		// 需求状态驱动的组件可见性：无需求隐藏，其余显示(承载需求图标)
		if (BubbleWidgetComp)
		{
			BubbleWidgetComp->SetVisibility(NewState != EAnimalNeedState::None);
		}
		// 仅广播状态变化，气泡台词交由 Widget 绑定 OnNeedStateChanged 后自行处理
		if (bEnableDebugLog)
		{
			UE_LOG(LogTemp, Log, TEXT("[%s] 需求状态变为 %d，已广播 OnNeedStateChanged 交由 Widget 处理"),
				*GetName(), (uint8)NewState);
		}
		OnNeedStateChanged.Broadcast(CurrentNeedState);
	}
	else
	{
		CurrentNeedState = NewState;
	}

	// 需求图标显隐交由合并 Widget 内部绑定 OnNeedStateChanged 处理，此处不再直接控制组件
}

// ============================================================
//  行为状态机
// ============================================================
bool AAnimalCharacter::CheckFleeFromPlayer()
{
	// 取玩家(AShootCharacter)pawn
	AShootCharacter* Player = Cast<AShootCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	if (!Player)
	{
		return false;
	}

	const FVector ToPlayer = Player->GetActorLocation() - GetActorLocation();
	const float Dist = ToPlayer.Size2D();

	// 是否已满足"发现玩家"的视觉条件(正前方视锥 + 视觉距离内)
	bool bSeesPlayer = false;
	if (Dist <= SightRange && Dist >= KINDA_SMALL_NUMBER)
	{
		const FVector Forward = GetActorForwardVector();
		const float Dot = Forward.GetSafeNormal2D() | ToPlayer.GetSafeNormal2D();
		const float HalfFovCos = FMath::Cos(FMath::DegreesToRadians(SightAngleDegrees));
		bSeesPlayer = (Dot >= HalfFovCos);
	}

	// ---- 阶段 1：尚未警觉也未逃跑，刚发现玩家 -> 进入警觉(显示问号) ----
	if (!bAlerting && !bFleeing)
	{
		if (bSeesPlayer)
		{
			EnterAlert();
			return false; // 本帧仅进入警觉思考，不逃跑
		}
		return false;
	}

	// ---- 阶段 2：警觉思考中(问号显示) ----
	if (bAlerting && !bFleeing)
	{
		// 思考期间若玩家离开视锥，取消警觉(隐藏问号，恢复正常)
		if (!bSeesPlayer)
		{
			HideMarks();
			bAlerting = false;
			AlertRemaining = 0.f;
			return false;
		}
		// 推进思考计时
		AlertRemaining -= GetWorld()->GetDeltaSeconds();
		if (AlertRemaining > 0.f)
		{
			return false; // 仍在思考，原地不动，问号保持
		}
		// 思考结束 -> 真正开始逃跑(隐藏问号，显示感叹号)
		StartFlee();
	}

	// ---- 阶段 3：逃跑中(感叹号显示) ----
	if (bFleeing)
	{
		const float HideDist = (FleeHideDistance > 0.f) ? FleeHideDistance : SightRange;
		if (Dist > HideDist)
		{
			// 已跑远：隐藏感叹号并退出逃跑
			HideMarks();
			bFleeing = false;
			CurrentMoveSpeed = MoveSpeed; // 复位速度，恢复正常行走
			return false;
		}

		// 触发 SpawnActor：每只动物只生成一次(除非冷却时间已过允许再生成)
		const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		if (!bHasFleeSpawnedEver && NextFleeSpawnTime < 0.0)
		{
			bHasFleeSpawnedEver = true;
			DoSpawn();
			NextFleeSpawnTime = (FleeSpawnCooldown > 0.f) ? (Now + FleeSpawnCooldown) : -1.0;
			if (bEnableDebugLog)
			{
				UE_LOG(LogTemp, Log, TEXT("[%s] 发现玩家，开始逃跑并触发一次 SpawnActor"), *GetName());
			}
		}
		else if (bHasFleeSpawnedEver && FleeSpawnCooldown > 0.f && Now >= NextFleeSpawnTime)
		{
			DoSpawn();
			NextFleeSpawnTime = Now + FleeSpawnCooldown;
			if (bEnableDebugLog)
			{
				UE_LOG(LogTemp, Log, TEXT("[%s] 逃跑冷却结束，再次触发一次 SpawnActor"), *GetName());
			}
		}

		// 逃跑方向：远离玩家(投影到 NavMesh，避免目标点越界导致 AI 抖动/原地旋转)
		const FVector AwayDir = (GetActorLocation() - Player->GetActorLocation()).GetSafeNormal2D();
		const FVector FleeGoalRaw = GetActorLocation() + AwayDir * WanderRadius;
		FVector FleeGoal = FleeGoalRaw;
		if (UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(GetWorld()))
		{
			FNavLocation OutLoc;
			if (NavSys->GetRandomPointInNavigableRadius(FleeGoalRaw, WanderRadius * 0.25f, OutLoc))
			{
				FleeGoal = OutLoc.Location;
			}
		}
		// 逃跑加速：从 CurrentMoveSpeed(起点=MoveSpeed 走路)平滑插值到 FleeSpeed(跑步)
		const float DeltaTime = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.f;
		CurrentMoveSpeed = FMath::FInterpTo(CurrentMoveSpeed, FleeSpeed, DeltaTime, FleeAccelRate / FMath::Max(1.f, FleeSpeed - MoveSpeed));
		MoveToLocation(FleeGoal, CurrentMoveSpeed);
		return true;
	}

	return false;
}

void AAnimalCharacter::EnterAlert()
{
	bAlerting = true;
	AlertRemaining = FMath::FRandRange(AlertThinkTimeMin, AlertThinkTimeMax); // 随机 1~3s 思考
	if (QuestionMarkComp) { QuestionMarkComp->SetVisibility(true); }
	if (ExclamationMarkComp) { ExclamationMarkComp->SetVisibility(false); }
	// 气泡：发现玩家时的台词
	if (TextCollection && !TextCollection->AlertLine.IsEmpty())
	{
		ShowBubble(TextCollection->AlertLine);
	}
	if (bEnableDebugLog)
	{
		UE_LOG(LogTemp, Log, TEXT("[%s] 发现玩家，进入警觉思考 %.1f~%.1f 秒(显示问号)"), *GetName(), AlertThinkTimeMin, AlertThinkTimeMax);
	}
}

void AAnimalCharacter::StartFlee()
{
	bAlerting = false;
	bFleeing = true;
	EngageBehavior = EAnimalEngageBehavior::Flee; // 确保交战行为为逃跑
	CurrentMoveSpeed = MoveSpeed; // 从走路速度开始，后续每帧平滑加速到 FleeSpeed
	if (QuestionMarkComp) { QuestionMarkComp->SetVisibility(false); }
	if (ExclamationMarkComp) { ExclamationMarkComp->SetVisibility(true); }
	// 气泡：开始逃跑时的台词
	if (TextCollection && !TextCollection->FleeLine.IsEmpty())
	{
		ShowBubble(TextCollection->FleeLine);
	}
	if (bEnableDebugLog)
	{
		UE_LOG(LogTemp, Log, TEXT("[%s] 思考结束，开始逃跑(显示感叹号)"), *GetName());
	}
}

void AAnimalCharacter::HideMarks()
{
	if (QuestionMarkComp) { QuestionMarkComp->SetVisibility(false); }
	if (ExclamationMarkComp) { ExclamationMarkComp->SetVisibility(false); }
}

void AAnimalCharacter::UpdateBehavior(float DeltaTime)
{
	// 优先检测正前方玩家 -> 警觉/逃跑(覆盖其它行为)
	if (CheckFleeFromPlayer())
	{
		CurrentBehavior = EAnimalActiveBehavior::Idle; // 逃跑由 CheckFleeFromPlayer 内驱动，状态机仅标记
		return;
	}

	// 防御性：既不在逃跑也不在警觉时，确保标记已隐藏(状态自管理，仅兜底)
	if (!bFleeing && !bAlerting)
	{
		HideMarks();
	}

	if (bResting)
	{
		CurrentBehavior = EAnimalActiveBehavior::Idle;
		bHasDestination = false; // 休息时清空目标
		WaitRemaining = 0.f;
		return;
	}

	// 需求优先：中断当前活跃行为去觅食/找水
	if (bSeekingNeed)
	{
		CurrentBehavior = EAnimalActiveBehavior::SeekNeed;
		// TODO: 在关卡中查找最近的食物/水源 Actor 并移动; 此处退化为前往 Homebase
		if (!bHasDestination || FVector::DistSquared(GetActorLocation(), CurrentDestination) > ArriveThreshold * ArriveThreshold)
		{
			CurrentDestination = HomeLocation;
			bHasDestination = true;
			MoveToLocation(CurrentDestination, MoveSpeed);
		}
		return;
	}

	CurrentBehavior = ActiveBehavior;
	ExecuteActiveBehavior(DeltaTime);
}

void AAnimalCharacter::ExecuteActiveBehavior(float DeltaTime)
{
	// 是否正在原地停留(到点的"思考/休息")
	if (WaitRemaining > 0.f)
	{
		WaitRemaining -= DeltaTime;
		if (AAIController* AIC = GetAnimalController()) { AIC->StopMovement(); }
		if (bEnableDebugLog)
		{
			UE_LOG(LogTemp, VeryVerbose, TEXT("[%s] 到达后停留中: %.1fs"), *GetName(), WaitRemaining);
		}
		// 停留结束 -> 清空目标，下一帧去选新点
		if (WaitRemaining <= 0.f)
		{
			bHasDestination = false;
		}
		return;
	}

	// 是否已到达当前目标：距离阈值 或 AIController 已停止移动(到达)
	bool bArrived = false;
	if (bHasDestination)
	{
		const bool bClose = FVector::DistSquared(GetActorLocation(), CurrentDestination) <= ArriveThreshold * ArriveThreshold;
		const bool bMoveIdle = [&]()
		{
			if (AAIController* AIC = GetAnimalController())
			{
				return AIC->GetMoveStatus() == EPathFollowingStatus::Idle;
			}
			return false;
		}();
		bArrived = bClose || bMoveIdle;
	}

	// 刚到达且配置了停留时间 -> 进入停留状态，原地不动(停留时长在 Min~Max 间随机)
	if (bArrived && WaitAtPointTimeMax > WaitAtPointTimeMin)
	{
		WaitRemaining = FMath::FRandRange(WaitAtPointTimeMin, WaitAtPointTimeMax);
		if (AAIController* AIC = GetAnimalController()) { AIC->StopMovement(); }
		if (bEnableDebugLog)
		{
			UE_LOG(LogTemp, Log, TEXT("[%s] 到达目标点，进入停留 %.1f~%.1f 秒"), *GetName(), WaitAtPointTimeMin, WaitAtPointTimeMax);
		}
		return;
	}

	switch (ActiveBehavior)
	{
	case EAnimalActiveBehavior::Idle:
		if (AAIController* AIC = GetAnimalController()) { AIC->StopMovement(); }
		bHasDestination = false;
		break;

	case EAnimalActiveBehavior::RoamRandom:
		if (!bHasDestination || bArrived)
		{
			CurrentDestination = GetRandomPointInNavMesh(WanderRadius);
			bHasDestination = true;
			MoveToLocation(CurrentDestination, MoveSpeed);
		}
		break;

	case EAnimalActiveBehavior::RoamInRange:
		if (!bHasDestination || bArrived)
		{
			CurrentDestination = SpawnOrigin + FVector(FMath::FRandRange(-WanderRadius, WanderRadius), FMath::FRandRange(-WanderRadius, WanderRadius), 0.f);
			// 抬到地面高度(避免 Z 偏差导致永远到不了)
			CurrentDestination.Z = GetActorLocation().Z;
			bHasDestination = true;
			MoveToLocation(CurrentDestination, MoveSpeed);
		}
		break;

	case EAnimalActiveBehavior::WaypointRandom:
		if (Waypoints.Num() > 0 && (!bHasDestination || bArrived))
		{
			CurrentDestination = Waypoints[FMath::RandRange(0, Waypoints.Num() - 1)];
			bHasDestination = true;
			MoveToLocation(CurrentDestination, MoveSpeed);
		}
		break;

	case EAnimalActiveBehavior::WaypointPath:
		if (Waypoints.Num() > 0 && (!bHasDestination || bArrived))
		{
			CurrentDestination = Waypoints[WaypointIndex % Waypoints.Num()];
			WaypointIndex = (WaypointIndex + 1) % Waypoints.Num();
			bHasDestination = true;
			MoveToLocation(CurrentDestination, MoveSpeed);
		}
		break;

	case EAnimalActiveBehavior::GoToHome:
		if (!bHasDestination || bArrived)
		{
			CurrentDestination = HomeLocation;
			bHasDestination = true;
			MoveToLocation(CurrentDestination, MoveSpeed);
		}
		break;

	case EAnimalActiveBehavior::GoToPOI:
	case EAnimalActiveBehavior::SeekNeed:
		// TODO: 查找 POI / 食物水源后移动(暂保持当前目标)
		break;

	default:
		break;
	}
}

// ============================================================
//  驯服
// ============================================================
void AAnimalCharacter::StartTaming()
{
	if (TameState == EAnimalTameState::Wild)
	{
		TameState = EAnimalTameState::Taming;
	}
}

void AAnimalCharacter::Release()
{
	TameState = EAnimalTameState::Wild;
	CompanionName = NAME_None;
}

// ============================================================
//  Blueprint Native Events
// ============================================================
void AAnimalCharacter::SetupPerception()
{
	if (!PerceptionComp)
	{
		return;
	}

	// 视觉配置
	UAISenseConfig_Sight* SightCfg = NewObject<UAISenseConfig_Sight>(this);
	SightCfg->DetectionByAffiliation.bDetectEnemies = true;
	SightCfg->DetectionByAffiliation.bDetectNeutrals = true;
	SightCfg->DetectionByAffiliation.bDetectFriendlies = true;
	SightCfg->SightRadius = SightRange;
	SightCfg->LoseSightRadius = SightRange * 1.2f;
	SightCfg->PeripheralVisionAngleDegrees = SightAngleDegrees;
	SightCfg->SetMaxAge(5.f);
	PerceptionComp->ConfigureSense(*SightCfg);

	// 听觉配置
	UAISenseConfig_Hearing* HearCfg = NewObject<UAISenseConfig_Hearing>(this);
	HearCfg->HearingRange = HearingRange;
	HearCfg->DetectionByAffiliation.bDetectEnemies = true;
	HearCfg->DetectionByAffiliation.bDetectNeutrals = true;
	HearCfg->DetectionByAffiliation.bDetectFriendlies = true;
	PerceptionComp->ConfigureSense(*HearCfg);

	PerceptionComp->SetDominantSense(UAISense_Sight::StaticClass());

	// 绑定感知回调
	PerceptionComp->OnTargetPerceptionUpdated.AddDynamic(this, &AAnimalCharacter::OnPerceptionUpdated);
}

void AAnimalCharacter::OnPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	if (!Actor || !Stimulus.WasSuccessfullySensed())
	{
		return; // 只处理"新感知到"的事件
	}
	if (Actor == this)
	{
		return;
	}

	// 判断威胁 / 猎物（按标签）
	auto HasAnyTag = [&](const TArray<FName>& TagsToCheck) -> bool
	{
		if (TagsToCheck.Num() == 0)
		{
			return false;
		}
		for (const FName& T : TagsToCheck)
		{
			if (Actor->ActorHasTag(T))
			{
				return true;
			}
		}
		return false;
	};

	const bool bIsThreat = HasAnyTag(ThreatTags);
	const bool bIsPrey = HasAnyTag(PreyTags);

	// 猎物：捕食者触发攻击；威胁：按 EngageBehavior 反应
	if (AnimalRole == EAnimalRole::Predator && bIsPrey)
	{
		// 捕食者发现猎物 -> 当作威胁目标去攻击
		OnPerceiveThreat(Actor);
	}
	else if (bIsThreat)
	{
		// 发现威胁 -> 按交战行为(逃跑/攻击/保卫)
		OnPerceiveThreat(Actor);
	}
}

void AAnimalCharacter::OnPerceiveThreat_Implementation(AActor* Threat)
{
	// 默认 C++ 行为：根据 EngageBehavior 反应
	switch (EngageBehavior)
	{
	case EAnimalEngageBehavior::Flee:
		if (Threat)
		{
			MoveToLocation(GetActorLocation() + (GetActorLocation() - Threat->GetActorLocation()).GetSafeNormal() * WanderRadius, FleeSpeed);
		}
		break;
	case EAnimalEngageBehavior::Attack:
		if (Threat)
		{
			MoveToLocation(Threat->GetActorLocation(), FleeSpeed);
		}
		break;
	case EAnimalEngageBehavior::Defend:
		if (Threat)
		{
			MoveToLocation(Threat->GetActorLocation(), MoveSpeed);
		}
		break;
	case EAnimalEngageBehavior::Ignore:
	default:
		break;
	}
}

void AAnimalCharacter::OnNeedSatisfied_Implementation(EAnimalNeed Need)
{
	if (Need == EAnimalNeed::Food) { Hunger = 0.f; }
	else { Thirst = 0.f; }

	// 触发进食动画
	bIsEating = true;
	EatRemaining = EatDuration;
}

// ============================================================
//  工具
// ============================================================
AAIController* AAnimalCharacter::GetAnimalController() const
{
	return Cast<AAIController>(GetController());
}

bool AAnimalCharacter::MoveToLocation(const FVector& Goal, float Speed)
{
	// 将速度约束到合法区间：行走 1~300，跑步/逃跑 301~600
	const float ClampedSpeed = FMath::Clamp(Speed, WalkSpeedMin, RunSpeedMax);
	if (UCharacterMovementComponent* Mov = GetCharacterMovement())
	{
		Mov->MaxWalkSpeed = ClampedSpeed;
	}
	if (AAIController* AIC = GetAnimalController())
	{
		const EPathFollowingRequestResult::Type Result = AIC->MoveToLocation(Goal, ArriveThreshold, true, true, false);
		return Result == EPathFollowingRequestResult::AlreadyAtGoal
			|| Result == EPathFollowingRequestResult::RequestSuccessful;
	}
	return false;
}

FVector AAnimalCharacter::GetRandomPointInNavMesh(float Radius) const
{
	FVector Result;
	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(GetWorld());
	if (NavSys)
	{
		FNavLocation OutLoc;
		if (NavSys->GetRandomPointInNavigableRadius(SpawnOrigin, Radius, OutLoc))
		{
			Result = OutLoc.Location;
		}
		else
		{
			// 导航获取失败回退
			Result = SpawnOrigin + FVector(FMath::FRandRange(-Radius, Radius), FMath::FRandRange(-Radius, Radius), 0.f);
		}
	}
	else
	{
		// 无导航系统回退
		Result = SpawnOrigin + FVector(FMath::FRandRange(-Radius, Radius), FMath::FRandRange(-Radius, Radius), 0.f);
	}

	// 调试预览：受 bEnableDebugLog 控制
	if (bEnableDebugLog && GetWorld())
	{
		// 在世界中画一个持续 2 秒的球体标记目标点
		DrawDebugSphere(GetWorld(), Result, 30.f, 12, FColor::Green, false, 2.f);
		// 从出生点到目标点连线
		DrawDebugLine(GetWorld(), SpawnOrigin, Result, FColor::Yellow, false, 2.f, 0, 2.f);
		const FString Msg = FString::Printf(TEXT("[%s] 随机点: X=%.0f Y=%.0f Z=%.0f"), *GetName(), Result.X, Result.Y, Result.Z);
		UE_LOG(LogTemp, Log, TEXT("%s"), *Msg);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Green, Msg);
		}
	}
	return Result;
}

// ============================================================
//  定时生成 Actor（原有功能保留）
// ============================================================
void AAnimalCharacter::StartSpawning()
{
	if (SpawnActorClasses.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("AAnimalCharacter::StartSpawning: SpawnActorClasses 为空，无法生成"));
		return;
	}
	if (SpawnInterval <= 0.f)
	{
		SpawnInterval = 0.1f;
	}
	if (GetWorld()->GetTimerManager().IsTimerActive(SpawnTimerHandle))
	{
		GetWorld()->GetTimerManager().ClearTimer(SpawnTimerHandle);
	}
	GetWorld()->GetTimerManager().SetTimer(SpawnTimerHandle, this, &AAnimalCharacter::SpawnTick, SpawnInterval, true, SpawnInterval);
}

void AAnimalCharacter::StopSpawning()
{
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(SpawnTimerHandle);
	}
}

AActor* AAnimalCharacter::SpawnOnce()
{
	if (GetWorld() && GetWorld()->GetTimeSeconds() < NextManualSpawnTime)
	{
		return nullptr;
	}
	AActor* Spawned = DoSpawn();
	if (Spawned && GetWorld())
	{
		NextManualSpawnTime = GetWorld()->GetTimeSeconds() + SpawnInterval;
	}
	return Spawned;
}

AActor* AAnimalCharacter::DoSpawn()
{
	// 处于饥饿/口渴(需求)状态时抑制生成行为(由 bDisableSpawnWhenNeeding 控制是否启用)
	if (bDisableSpawnWhenNeeding && bSeekingNeed)
	{
		if (bEnableDebugLog)
		{
			UE_LOG(LogTemp, Log, TEXT("[%s] 处于需求状态(饥饿/口渴)，抑制 Spawn 行为"), *GetName());
		}
		return nullptr;
	}

	if (SpawnActorClasses.Num() == 0)
	{
		return nullptr;
	}
	if (MaxSpawnCount >= 0 && SpawnedCount >= MaxSpawnCount)
	{
		return nullptr;
	}

	TSubclassOf<AActor> ClassToSpawn;
	if (bUseWeightedSpawn)
	{
		float TotalWeight = 0.f;
		for (const FSpawnClassEntry& Entry : SpawnActorClasses)
		{
			if (Entry.Class && Entry.Weight > 0.f)
			{
				TotalWeight += Entry.Weight;
			}
		}
		if (TotalWeight <= 0.f) { return nullptr; }
		float Roll = FMath::FRandRange(0.f, TotalWeight);
		for (const FSpawnClassEntry& Entry : SpawnActorClasses)
		{
			if (!Entry.Class || Entry.Weight <= 0.f) { continue; }
			Roll -= Entry.Weight;
			if (Roll <= 0.f) { ClassToSpawn = Entry.Class; break; }
		}
	}
	else if (bRandomClass)
	{
		TArray<TSubclassOf<AActor>> ValidClasses;
		for (const FSpawnClassEntry& Entry : SpawnActorClasses)
		{
			if (Entry.Class) { ValidClasses.Add(Entry.Class); }
		}
		if (ValidClasses.Num() == 0) { return nullptr; }
		ClassToSpawn = ValidClasses[FMath::RandRange(0, ValidClasses.Num() - 1)];
	}
	else
	{
		for (int32 k = 0; k < SpawnActorClasses.Num(); k++)
		{
			const FSpawnClassEntry& Entry = SpawnActorClasses[SpawnClassIndex % SpawnActorClasses.Num()];
			SpawnClassIndex++;
			if (Entry.Class) { ClassToSpawn = Entry.Class; break; }
		}
	}

	if (!ClassToSpawn) { return nullptr; }

	USkeletalMeshComponent* MeshComp = GetMesh();
	FVector SpawnLocation = GetActorLocation();
	FRotator SpawnRotation = GetActorRotation();
	if (MeshComp && !SpawnSocketName.IsNone() && MeshComp->DoesSocketExist(SpawnSocketName))
	{
		SpawnLocation = MeshComp->GetSocketLocation(SpawnSocketName);
		SpawnRotation = MeshComp->GetSocketRotation(SpawnSocketName);
	}
	else if (!SpawnSocketName.IsNone())
	{
		UE_LOG(LogTemp, Warning, TEXT("AAnimalCharacter::DoSpawn: Socket '%s' 不存在于 Mesh，回退到 Actor 位置"), *SpawnSocketName.ToString());
	}

	UWorld* World = GetWorld();
	if (!World) { return nullptr; }

	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	AActor* Spawned = World->SpawnActor<AActor>(ClassToSpawn, SpawnLocation, SpawnRotation, Params);
	if (Spawned) { SpawnedCount++; }
	return Spawned;
}

void AAnimalCharacter::SpawnTick()
{
	if (MaxSpawnCount >= 0 && SpawnedCount >= MaxSpawnCount)
	{
		StopSpawning();
		return;
	}
	DoSpawn();
}
