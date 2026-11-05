// Fill out your copyright notice in the Description page of Project Settings.

#include "Event/PestActor.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "NPC/GuestCharacter.h"
#include "ShootCharacter.h"

APestActor::APestActor()
{
	PrimaryActorTick.bCanEverTick = true;

	// 外观网格作根组件：不参与碰撞（不打挤玩家 / 家具），只由交互球负责范围检测
	MeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComp"));
	SetRootComponent(MeshComp);
	MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MeshComp->SetGenerateOverlapEvents(false);

	// 交互球：只与 Pawn 重叠，用于朝玩家注册“按 E 抓取”
	InteractionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionSphere"));
	InteractionSphere->SetupAttachment(RootComponent);
	InteractionSphere->InitSphereRadius(InteractionRadius);
	InteractionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	InteractionSphere->SetGenerateOverlapEvents(true);
}

void APestActor::BeginPlay()
{
	Super::BeginPlay();

	InteractionSphere->SetSphereRadius(InteractionRadius);
	InteractionSphere->OnComponentBeginOverlap.AddDynamic(this, &APestActor::OnInteractionOverlap);
	InteractionSphere->OnComponentEndOverlap.AddDynamic(this, &APestActor::OnInteractionEndOverlap);

	Rand.Initialize(FMath::Rand());
	HomeLocation = GetActorLocation();
	PickNewWanderTarget();
	ScareCountdown = ScareInterval;

	CachedPlayer = UGameplayStatics::GetPlayerPawn(this, 0);
}

void APestActor::Configure(float InMoveSpeed, float InFleeRadius, float InWanderRadius, bool bInScareGuests, float InScareRadius)
{
	MoveSpeed = FMath::Max(0.f, InMoveSpeed);
	FleeRadius = FMath::Max(0.f, InFleeRadius);
	WanderRadius = FMath::Max(0.f, InWanderRadius);
	bScareGuests = bInScareGuests;
	ScareRadius = FMath::Max(0.f, InScareRadius);

	if (IsValid(InteractionSphere))
	{
		InteractionSphere->SetSphereRadius(InteractionRadius);
	}

	// 参数变化后重新选点，避免沿用旧范围外的目标
	HomeLocation = GetActorLocation();
	PickNewWanderTarget();
}

void APestActor::PickNewWanderTarget()
{
	const float Angle = Rand.FRandRange(0.f, 2.f * PI);
	const float Dist = WanderRadius * FMath::Sqrt(Rand.FRand());
	MoveTarget = HomeLocation + FVector(FMath::Cos(Angle) * Dist, FMath::Sin(Angle) * Dist, 0.f);
	RepathCountdown = Rand.FRandRange(RepathIntervalMin, RepathIntervalMax);
}

void APestActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!IsValid(CachedPlayer))
	{
		CachedPlayer = UGameplayStatics::GetPlayerPawn(this, 0);
	}

	FVector Location = GetActorLocation();
	FVector Direction = FVector::ZeroVector;
	float Speed = MoveSpeed;

	RepathCountdown -= DeltaSeconds;
	if (RepathCountdown <= 0.f)
	{
		PickNewWanderTarget();
	}

	// 玩家逼近：朝反方向逃跑（目标点限制在活动范围内，跑出去就折返回出生点）
	if (IsValid(CachedPlayer))
	{
		const FVector PlayerLoc = CachedPlayer->GetActorLocation();
		const FVector Away = (Location - PlayerLoc).GetSafeNormal2D();
		const float PlayerDist = FVector::Dist2D(Location, PlayerLoc);
		if (PlayerDist < FleeRadius)
		{
			Direction = Away;
			Speed = FleeSpeed;
		}
	}

	if (Direction.IsNearlyZero())
	{
		Direction = (MoveTarget - Location).GetSafeNormal2D();
	}

	// 活动范围约束：超出 WanderRadius 时不往外走，直接往出生点方向折返
	if (WanderRadius > 0.f && FVector::Dist2D(Location, HomeLocation) > WanderRadius)
	{
		Direction = (HomeLocation - Location).GetSafeNormal2D();
	}

	if (!Direction.IsNearlyZero() && Speed > 0.f)
	{
		const FVector NextLocation = Location + Direction * Speed * DeltaSeconds;

		// 简单避障：水平方向打线，撞到墙 / 家具就放弃这一步并换个目标（避免穿模）
		bool bBlocked = false;
		if (GetWorld())
		{
			FCollisionQueryParams Params(FName(TEXT("PestMoveTrace")), false, this);
			const FVector TraceStart = Location + FVector(0.f, 0.f, 30.f);
			const FVector TraceEnd = NextLocation + FVector(0.f, 0.f, 30.f);
			FHitResult Hit;
			bBlocked = GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_WorldStatic, Params);
		}

		if (bBlocked)
		{
			PickNewWanderTarget();
		}
		else
		{
			Location = NextLocation;
		}
	}

	SnapToGround(Location);
	SetActorLocation(Location);

	if (!Direction.IsNearlyZero())
	{
		const FRotator Facing(0.f, Direction.Rotation().Yaw, 0.f);
		SetActorRotation(Facing);
	}

	UpdateGuestScaring(DeltaSeconds);
}

void APestActor::SnapToGround(FVector& InOutLocation) const
{
	if (!bSnapToGround || !GetWorld())
	{
		return;
	}

	const FVector Start = InOutLocation + FVector(0.f, 0.f, TraceUp);
	const FVector End = InOutLocation - FVector(0.f, 0.f, TraceDown);

	FCollisionQueryParams Params(FName(TEXT("PestGroundSnap")), false, this);
	FHitResult Hit;
	if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
	{
		InOutLocation.Z = Hit.ImpactPoint.Z + GroundOffset;
	}
}

void APestActor::UpdateGuestScaring(float DeltaSeconds)
{
	if (!bScareGuests)
	{
		return;
	}

	ScareCountdown -= DeltaSeconds;
	if (ScareCountdown > 0.f)
	{
		return;
	}
	ScareCountdown = ScareInterval;

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FVector MyLocation = GetActorLocation();
	const float RadiusSq = ScareRadius * ScareRadius;

	for (TActorIterator<AGuestCharacter> It(World); It; ++It)
	{
		AGuestCharacter* Guest = *It;
		if (!IsValid(Guest) || Guest->GuestState == EGuestState::Leaving)
		{
			continue;
		}
		if (FVector::DistSquared2D(MyLocation, Guest->GetActorLocation()) > RadiusSq)
		{
			continue;
		}

		if (bShowDebugInfo)
		{
			UE_LOG(LogTemp, Warning, TEXT("[PestActor] %s 吓跑了客人 %s（未完成订单将被取消）"),
				*GetName(), *Guest->GetName());
		}
		BP_OnScaredGuest(Guest);
		Guest->ScareAway();
	}
}

void APestActor::OnInteractionOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (AShootCharacter* Character = Cast<AShootCharacter>(OtherActor))
	{
		Character->SetOverlappingPest(this);
	}
}

void APestActor::OnInteractionEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (AShootCharacter* Character = Cast<AShootCharacter>(OtherActor))
	{
		if (Character->GetOverlappingPest() == this)
		{
			Character->SetOverlappingPest(nullptr);
		}
	}
}

bool APestActor::TryCatch(AShootCharacter* Player)
{
	if (bCaught || !IsValid(Player))
	{
		return false;
	}

	bCaught = true;

	if (Player->GetOverlappingPest() == this)
	{
		Player->SetOverlappingPest(nullptr);
	}

	if (bShowDebugInfo)
	{
		UE_LOG(LogTemp, Log, TEXT("[PestActor] %s 被玩家抓住"), *GetName());
	}

	BP_OnCaught();
	OnCaught.Broadcast(this);
	Destroy();
	return true;
}

void APestActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 兜底：Actor 被销毁（事件失败清场 / 关卡卸载）时清掉玩家身上的交互引用
	if (AShootCharacter* Player = Cast<AShootCharacter>(CachedPlayer))
	{
		if (Player->GetOverlappingPest() == this)
		{
			Player->SetOverlappingPest(nullptr);
		}
	}

	Super::EndPlay(EndPlayReason);
}
