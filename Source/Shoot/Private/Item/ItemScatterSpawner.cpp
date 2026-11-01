// Fill out your copyright notice in the Description page of Project Settings.

#include "Item/ItemScatterSpawner.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Pawn.h"
#include "Components/StaticMeshComponent.h"
#include "Item/ItemActor.h"

AItemScatterSpawner::AItemScatterSpawner()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	// 生成器本身不需要碰撞，也不要被玩家交互到
	SetActorEnableCollision(false);
}

void AItemScatterSpawner::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (!bDrawDebugArea || !GetWorld())
	{
		return;
	}

	// 只在调试开关打开时画区域；编辑器与运行时都能看到
	FlushPersistentDebugLines(GetWorld());
	const FVector Center = GetActorLocation();
	const FColor Color(0, 200, 120, 255);

	if (AreaShape == EScatterAreaShape::Circle)
	{
		DrawDebugCircle(GetWorld(), Center, SpawnRadius, 64, Color, true, -1.f, 0, 2.f,
			FVector(1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f), false);
		if (InnerRadius > KINDA_SMALL_NUMBER)
		{
			DrawDebugCircle(GetWorld(), Center, InnerRadius, 64, FColor::Yellow, true, -1.f, 0, 2.f,
				FVector(1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f), false);
		}
	}
	else
	{
		DrawDebugBox(GetWorld(), Center, BoxExtent, Color, true, -1.f, 0, 2.f);
	}
}

void AItemScatterSpawner::BeginPlay()
{
	Super::BeginPlay();

	EnsureRandomReady();

	if (bSpawnOnBeginPlay)
	{
		SpawnAll();
	}
}

void AItemScatterSpawner::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 回收到本生成器撒出的实例，避免切关卡/重开后残留
	for (FScatterSlot& Slot : Slots)
	{
		if (IsValid(Slot.Actor))
		{
			Slot.Actor->OnDestroyed.RemoveAll(this);
		}
	}
	Slots.Reset();

	Super::EndPlay(EndPlayReason);
}

void AItemScatterSpawner::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bRandReady)
	{
		EnsureRandomReady();
	}

	UpdateRespawnTimers(DeltaSeconds);

	if (bAutoRespawn)
	{
		CheckAccum += DeltaSeconds;
		if (CheckAccum >= PickCheckInterval)
		{
			CheckAccum = 0.f;
			CheckPickedUpItems();
		}
	}
}

void AItemScatterSpawner::EnsureRandomReady()
{
	if (bRandReady)
	{
		return;
	}
	Rand.Initialize(RandomSeed != 0 ? RandomSeed : FMath::Rand());
	bRandReady = true;
}

// ==================== 生成 ====================

void AItemScatterSpawner::SpawnAll()
{
	EnsureRandomReady();
	ClearAllSpawned();

	if (!Entries.Num())
	{
		UE_LOG(LogTemp, Warning, TEXT("[ItemScatterSpawner] %s 没有配置 Entries，不会生成任何食材"), *GetName());
		return;
	}

	RebuildWorldReserved();

	// 先按 SpawnCount 建好坑位，再逐个尝试落位；落不下的坑位会在之后的重生流程里继续尝试
	Slots.Reserve(SpawnCount);
	for (int32 i = 0; i < SpawnCount; ++i)
	{
		FScatterSlot NewSlot;
		NewSlot.EntryIndex = PickEntryIndex();
		Slots.Add(NewSlot);

		if (!SpawnIntoSlot(Slots.Num() - 1, false))
		{
			// 这次没找到位置：留空并给一个短重试倒计时
			FScatterSlot& Slot = Slots.Last();
			Slot.Actor = nullptr;
			Slot.EntryIndex = INDEX_NONE;
			Slot.RespawnCountdown = 3.f;
		}
	}
}

AItemActor* AItemScatterSpawner::SpawnOne(int32 EntryIndex)
{
	EnsureRandomReady();
	RebuildWorldReserved();

	if (!Entries.IsValidIndex(EntryIndex))
	{
		EntryIndex = PickEntryIndex();
	}
	if (!Entries.IsValidIndex(EntryIndex) || !Entries[EntryIndex].ItemClass)
	{
		return nullptr;
	}

	FScatterSlot NewSlot;
	NewSlot.EntryIndex = EntryIndex;
	Slots.Add(NewSlot);

	AItemActor* Spawned = SpawnIntoSlot(Slots.Num() - 1, false);
	if (!Spawned)
	{
		Slots.RemoveAt(Slots.Num() - 1);
	}
	return Spawned;
}

AItemActor* AItemScatterSpawner::SpawnIntoSlot(int32 SlotIndex, bool bUseOriginalPoint)
{
	if (!Slots.IsValidIndex(SlotIndex) || !GetWorld())
	{
		return nullptr;
	}

	FScatterSlot& Slot = Slots[SlotIndex];

	// 数量上限：已满就直接放弃
	if (MaxAliveTotal > 0 && GetAliveCount() >= MaxAliveTotal)
	{
		return nullptr;
	}

	// 决定这一坑用哪种食材（原坑那类已满则换一类）
	int32 EntryIndex = Slot.EntryIndex;
	if (!Entries.IsValidIndex(EntryIndex))
	{
		EntryIndex = PickEntryIndex();
	}
	else if (Entries[EntryIndex].MaxAlive > 0 && CountAliveOfEntry(EntryIndex) >= Entries[EntryIndex].MaxAlive)
	{
		EntryIndex = PickEntryIndex();
	}
	if (!Entries.IsValidIndex(EntryIndex) || !Entries[EntryIndex].ItemClass)
	{
		return nullptr;
	}

	FVector Location = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	bool bPlaced = false;

	// 1) 优先回原坑（要求该位置现在还是空的）
	if (bUseOriginalPoint && !Slot.OriginLocation.IsNearlyZero())
	{
		if (!bSnapToGround && IsLocationFree(Slot.OriginLocation))
		{
			Location = Slot.OriginLocation;
			Rotation = Slot.OriginRotation;
			bPlaced = true;
		}
		else if (bSnapToGround)
		{
			FVector GroundLoc;
			FRotator GroundRot;
			if (ProjectToGroundAndRotate(Slot.OriginLocation, GroundLoc, GroundRot) && IsLocationFree(GroundLoc))
			{
				Location = GroundLoc;
				Rotation = GroundRot;
				bPlaced = true;
			}
		}
	}

	// 2) 回不去就重新采样，直到找到一个既不重叠、又能贴到地面的点
	for (int32 Attempt = 0; !bPlaced && Attempt < MaxPlaceAttempts; ++Attempt)
	{
		FVector CandidateLoc;
		FRotator CandidateRot;
		if (!ProjectToGroundAndRotate(SamplePointInArea(), CandidateLoc, CandidateRot))
		{
			continue;
		}
		if (!IsLocationFree(CandidateLoc))
		{
			continue;
		}
		Location = CandidateLoc;
		Rotation = CandidateRot;
		bPlaced = true;
	}

	if (!bPlaced)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[ItemScatterSpawner] %s 在 %d 次尝试内没找到可放置的位置（可调小 MinSpacing 或调大 SpawnRadius）"),
			*GetName(), MaxPlaceAttempts);
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AItemActor* Spawned = GetWorld()->SpawnActor<AItemActor>(
		Entries[EntryIndex].ItemClass, Location, Rotation, Params);
	if (!Spawned)
	{
		return nullptr;
	}

	// 记录坑位
	Slot.Actor = Spawned;
	Slot.EntryIndex = EntryIndex;
	Slot.OriginLocation = Location;
	Slot.OriginRotation = Rotation;
	Slot.RespawnCountdown = -1.f;

	// 被销毁（存进背包 / 手扔消失）时立刻算作空坑，不必等轮询
	Spawned->OnDestroyed.AddUniqueDynamic(this, &AItemScatterSpawner::HandleSpawnedActorDestroyed);

	OnItemSpawned.Broadcast(Spawned);
	return Spawned;
}

void AItemScatterSpawner::ClearAllSpawned()
{
	for (FScatterSlot& Slot : Slots)
	{
		if (IsValid(Slot.Actor))
		{
			Slot.Actor->OnDestroyed.RemoveAll(this);
			Slot.Actor->Destroy();
		}
	}
	Slots.Reset();
	WorldReservedPoints.Reset();
	CheckAccum = 0.f;
}

// ==================== 采样 / 避让 ====================

FVector AItemScatterSpawner::SamplePointInArea()
{
	const FVector Center = GetActorLocation();

	if (AreaShape == EScatterAreaShape::Circle)
	{
		const float Outer = FMath::Max(SpawnRadius, 0.f);
		const float Inner = FMath::Clamp(InnerRadius, 0.f, Outer);
		// sqrt 采样保证面积上均匀，不会全挤在圆心
		const float R = FMath::Sqrt(Rand.FRandRange(Inner * Inner, Outer * Outer));
		const float Angle = Rand.FRandRange(0.f, 2.f * PI);
		return Center + FVector(FMath::Cos(Angle) * R, FMath::Sin(Angle) * R, 0.f);
	}

	const FVector Extent(FMath::Abs(BoxExtent.X), FMath::Abs(BoxExtent.Y), 0.f);
	return Center + FVector(
		Rand.FRandRange(-Extent.X, Extent.X),
		Rand.FRandRange(-Extent.Y, Extent.Y),
		Extent.Z);
}

bool AItemScatterSpawner::ProjectToGroundAndRotate(const FVector& InPoint, FVector& OutLocation, FRotator& OutRotation)
{
	OutLocation = InPoint;

	FVector Normal = FVector::UpVector;

	if (bSnapToGround)
	{
		UWorld* World = GetWorld();
		if (!World)
		{
			return false;
		}

		// 用物体中心做起点，避免生成点本身就陷在地里
		const FVector Start = InPoint + FVector(0.f, 0.f, TraceUp);
		const FVector End = InPoint - FVector(0.f, 0.f, TraceDown);

		FHitResult Hit;
		FCollisionQueryParams QueryParams(FName(TEXT("ItemScatterGround")), false, this);
		if (!World->LineTraceSingleByChannel(Hit, Start, End, GroundChannel, QueryParams))
		{
			// 打不到地面（悬空/区域超出地面）：丢弃这个点
			return false;
		}

		OutLocation = Hit.ImpactPoint;
		Normal = Hit.ImpactNormal;
	}

	const float Yaw = Rand.FRandRange(YawRange.X, YawRange.Y);

	if (bSnapToGround && bAlignToGroundNormal && !Normal.Equals(FVector::UpVector, 0.01f))
	{
		// Z 轴对齐地面法线，再绕自身 Z 随机转
		const FQuat AlignQuat = FQuat::FindBetweenNormals(FVector::UpVector, Normal);
		const FQuat YawQuat(FVector::UpVector, FMath::DegreesToRadians(Yaw));
		OutRotation = (AlignQuat * YawQuat).Rotator();
	}
	else
	{
		OutRotation = FRotator(0.f, Yaw, 0.f);
	}

	// 抬高量沿法线方向，斜面/平地上都合理
	OutLocation += Normal * GroundOffset;

	return true;
}

bool AItemScatterSpawner::IsLocationFree(const FVector& InLocation) const
{
	const float MinDistSq = FMath::Square(FMath::Max(MinSpacing, 1.f));

	// 与已生成的实例比（水平距离，高度不同不算重叠）
	for (const FScatterSlot& Slot : Slots)
	{
		if (!IsValid(Slot.Actor))
		{
			continue;
		}
		if (FVector::DistSquared2D(Slot.Actor->GetActorLocation(), InLocation) < MinDistSq)
		{
			return false;
		}
	}

	// 与关卡里已有的物品比（手动摆放的、别的生成器撒的）
	for (const FVector& Point : WorldReservedPoints)
	{
		if (FVector::DistSquared2D(Point, InLocation) < MinDistSq)
		{
			return false;
		}
	}

	return true;
}

void AItemScatterSpawner::RebuildWorldReserved()
{
	WorldReservedPoints.Reset();

	if (!bAvoidWorldItems)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<AItemActor> It(World); It; ++It)
	{
		AItemActor* Other = *It;
		if (!IsValid(Other) || IsSpawnedByMe(Other))
		{
			continue;
		}
		// 已被玩家拿在手上的不算占位
		if (IsHeldByPlayer(Other))
		{
			continue;
		}
		WorldReservedPoints.Add(Other->GetActorLocation());
	}
}

bool AItemScatterSpawner::IsSpawnedByMe(const AItemActor* Item) const
{
	if (!Item)
	{
		return false;
	}
	for (const FScatterSlot& Slot : Slots)
	{
		if (Slot.Actor == Item)
		{
			return true;
		}
	}
	return false;
}

// ==================== 选食材 ====================

int32 AItemScatterSpawner::PickEntryIndex()
{
	float TotalWeight = 0.f;
	for (int32 i = 0; i < Entries.Num(); ++i)
	{
		const FScatterSpawnEntry& Entry = Entries[i];
		if (!Entry.ItemClass)
		{
			continue;
		}
		if (Entry.MaxAlive > 0 && CountAliveOfEntry(i) >= Entry.MaxAlive)
		{
			continue;
		}
		TotalWeight += FMath::Max(Entry.Weight, 0.01f);
	}

	if (TotalWeight <= 0.f)
	{
		return INDEX_NONE;
	}

	float Pick = Rand.FRandRange(0.f, TotalWeight);
	for (int32 i = 0; i < Entries.Num(); ++i)
	{
		const FScatterSpawnEntry& Entry = Entries[i];
		if (!Entry.ItemClass)
		{
			continue;
		}
		if (Entry.MaxAlive > 0 && CountAliveOfEntry(i) >= Entry.MaxAlive)
		{
			continue;
		}
		Pick -= FMath::Max(Entry.Weight, 0.01f);
		if (Pick <= 0.f)
		{
			return i;
		}
	}

	// 数值误差兜底：返回最后一个可用项
	for (int32 i = Entries.Num() - 1; i >= 0; --i)
	{
		if (Entries[i].ItemClass)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

int32 AItemScatterSpawner::CountAliveOfEntry(int32 EntryIndex) const
{
	int32 Count = 0;
	for (const FScatterSlot& Slot : Slots)
	{
		if (Slot.EntryIndex == EntryIndex && IsValid(Slot.Actor))
		{
			++Count;
		}
	}
	return Count;
}

// ==================== 采摘检测与重生 ====================

bool AItemScatterSpawner::IsHeldByPlayer(const AItemActor* Item) const
{
	if (!Item)
	{
		return false;
	}

	// 手上拿着时的状态
	if (Item->GetItemState() == EItemState::EIS_Equipped)
	{
		return true;
	}

	// 兜底：Mesh 被 Attach 到某个 Pawn 上也算被拿走（状态没来得及切）
	if (const UStaticMeshComponent* Mesh = Item->ItemMesh)
	{
		if (AActor* Parent = Mesh->GetAttachParentActor())
		{
			return Parent->IsA<APawn>();
		}
	}

	return false;
}

void AItemScatterSpawner::CheckPickedUpItems()
{
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		FScatterSlot& Slot = Slots[i];
		if (!IsValid(Slot.Actor) || Slot.RespawnCountdown > 0.f)
		{
			continue;
		}
		if (IsHeldByPlayer(Slot.Actor))
		{
			VacateSlot(i, Slot.Actor);
		}
	}
}

void AItemScatterSpawner::HandleSpawnedActorDestroyed(AActor* DestroyedActor)
{
	AItemActor* Item = Cast<AItemActor>(DestroyedActor);
	if (!Item)
	{
		return;
	}
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		if (Slots[i].Actor == Item)
		{
			VacateSlot(i, Item);
			return;
		}
	}
}

void AItemScatterSpawner::VacateSlot(int32 SlotIndex, AItemActor* Item)
{
	if (!Slots.IsValidIndex(SlotIndex))
	{
		return;
	}

	FScatterSlot& Slot = Slots[SlotIndex];
	if (Slot.RespawnCountdown > 0.f)
	{
		// 已经在等重生，别重复排
		return;
	}

	Slot.Actor = nullptr;

	if (bAutoRespawn)
	{
		const float MinDelay = FMath::Max(RespawnDelayMin, 0.f);
		const float MaxDelay = FMath::Max(RespawnDelayMax, MinDelay);
		Slot.RespawnCountdown = FMath::Max(Rand.FRandRange(MinDelay, MaxDelay), 0.1f);
	}
	else
	{
		Slot.RespawnCountdown = -1.f;
	}

	OnItemRemoved.Broadcast(Item);
}

void AItemScatterSpawner::UpdateRespawnTimers(float DeltaSeconds)
{
	if (!bAutoRespawn)
	{
		return;
	}

	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		FScatterSlot& Slot = Slots[i];
		if (Slot.RespawnCountdown <= 0.f)
		{
			continue;
		}

		Slot.RespawnCountdown -= DeltaSeconds;
		if (Slot.RespawnCountdown > 0.f)
		{
			continue;
		}

		Slot.RespawnCountdown = -1.f;

		// 总数到顶就往后挪一点再试，不丢坑位
		if (MaxAliveTotal > 0 && GetAliveCount() >= MaxAliveTotal)
		{
			Slot.RespawnCountdown = 2.f;
			continue;
		}

		if (!SpawnIntoSlot(i, bRespawnAtOriginalPoint))
		{
			// 这次落不下（位置被占/打不到地面）：隔几秒再试
			Slot.RespawnCountdown = 5.f;
		}
	}
}

void AItemScatterSpawner::RespawnSlotNow(int32 SlotIndex)
{
	if (!Slots.IsValidIndex(SlotIndex))
	{
		return;
	}
	Slots[SlotIndex].RespawnCountdown = 0.01f;
}

// ==================== 查询 ====================

int32 AItemScatterSpawner::GetAliveCount() const
{
	int32 Count = 0;
	for (const FScatterSlot& Slot : Slots)
	{
		if (IsValid(Slot.Actor))
		{
			++Count;
		}
	}
	return Count;
}

int32 AItemScatterSpawner::GetPendingRespawnCount() const
{
	int32 Count = 0;
	for (const FScatterSlot& Slot : Slots)
	{
		if (Slot.RespawnCountdown > 0.f)
		{
			++Count;
		}
	}
	return Count;
}
