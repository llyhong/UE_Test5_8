// Fill out your copyright notice in the Description page of Project Settings.

#include "Item/ItemScatterSpawner.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Pawn.h"
#include "Components/StaticMeshComponent.h"
#include "Game/BaseGameInstance.h"
#include "Game/BusinessTime.h"
#include "Item/ItemActor.h"
#include "LandscapeHeightfieldCollisionComponent.h"
#include "LandscapeComponent.h"
#include "LandscapeInfo.h"
#include "LandscapeLayerInfoObject.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

namespace
{
	const TCHAR* SeasonToText(ESeason Season)
	{
		switch (Season)
		{
		case ESeason::Spring: return TEXT("春");
		case ESeason::Summer: return TEXT("夏");
		case ESeason::Autumn: return TEXT("秋");
		case ESeason::Winter: return TEXT("冬");
		default: return TEXT("?");
		}
	}
}

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
	EnsureTimeSystemBound();

	if (bSpawnOnBeginPlay)
	{
		SpawnAll();
	}
}

void AItemScatterSpawner::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (BoundTimeSystem)
	{
		BoundTimeSystem->OnDayChanged.RemoveAll(this);
		BoundTimeSystem->OnSeasonChanged.RemoveAll(this);
		BoundTimeSystem = nullptr;
	}

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

	// 关卡可能用 BusinessSimulation 蓝图子类重建时钟：每次 Tick 做一次幂等重绑检查（不重复加委托）
	EnsureTimeSystemBound();

	// 自动重生关闭时无需每帧轮询；每日刷新与换季由时间系统事件驱动
	if (!bAutoRespawn)
	{
		return;
	}

	if (!bRandReady)
	{
		EnsureRandomReady();
	}

	UpdateRespawnTimers(DeltaSeconds);

	CheckAccum += DeltaSeconds;
	if (CheckAccum >= PickCheckInterval)
	{
		CheckAccum = 0.f;
		CheckPickedUpItems();
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

// ==================== 季节 / 每日刷新 ====================

void AItemScatterSpawner::EnsureTimeSystemBound()
{
	UWorld* World = GetWorld();
	UBaseGameInstance* GI = World ? Cast<UBaseGameInstance>(World->GetGameInstance()) : nullptr;
	UTimeSystem* Current = (GI && GI->businessSimulation) ? GI->businessSimulation->TimeSystem.Get() : nullptr;

	if (Current == BoundTimeSystem.Get())
	{
		return;
	}

	if (BoundTimeSystem)
	{
		BoundTimeSystem->OnDayChanged.RemoveAll(this);
		BoundTimeSystem->OnSeasonChanged.RemoveAll(this);
		BoundTimeSystem = nullptr;
	}

	if (Current)
	{
		BoundTimeSystem = Current;
		CurrentSeason = Current->GetSeason();
		Current->OnDayChanged.AddDynamic(this, &AItemScatterSpawner::HandleDayChanged);
		Current->OnSeasonChanged.AddDynamic(this, &AItemScatterSpawner::HandleSeasonChanged);
	}
}

ESeason AItemScatterSpawner::GetCurrentSeason() const
{
	if (BoundTimeSystem)
	{
		return BoundTimeSystem->GetSeason();
	}

	if (const UWorld* World = GetWorld())
	{
		if (const UBaseGameInstance* GI = Cast<UBaseGameInstance>(World->GetGameInstance()))
		{
			if (GI->businessSimulation && GI->businessSimulation->TimeSystem)
			{
				return GI->businessSimulation->TimeSystem->GetSeason();
			}
		}
	}

	return CurrentSeason;
}

void AItemScatterSpawner::HandleDayChanged(int32 InYear, int32 InMonth, int32 InDay)
{
	if (!bRefreshDaily)
	{
		return;
	}

	EnsureRandomReady();
	RollDailyEntryAvailability();

	// SpawnCount 可能被运行时调大：先补够坑位数
	while (Slots.Num() < SpawnCount)
	{
		FScatterSlot NewSlot;
		Slots.Add(NewSlot);
	}

	int32 AliveCount = GetAliveCount();
	if (AliveCount >= SpawnCount)
	{
		return;
	}

	RebuildWorldReserved();

	int32 Refilled = 0;
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		if (AliveCount >= SpawnCount)
		{
			break;
		}

		FScatterSlot& Slot = Slots[i];
		// 已被占用，或在等自动重生，就不重复刷
		if (IsValid(Slot.Actor) || Slot.RespawnCountdown > 0.f)
		{
			continue;
		}
		if (Rand.FRand() > DailyRefreshChance)
		{
			continue;
		}

		const int32 EntryIndex = PickEntryIndex(true);
		if (EntryIndex == INDEX_NONE)
		{
			// 今天摇中的项都已达上限 / 都不在季节内
			continue;
		}

		Slot.EntryIndex = EntryIndex;
		if (SpawnIntoSlot(i, false))
		{
			++AliveCount;
			++Refilled;
		}
	}

	UE_LOG(LogTemp, Verbose, TEXT("[ItemScatterSpawner] 分区 %s 第 %d 天（%d-%02d-%02d，%s）补刷 %d 个"),
		*ZoneLabel.ToString(), InDay, InYear, InMonth, InDay, SeasonToText(GetCurrentSeason()), Refilled);
}

void AItemScatterSpawner::HandleSeasonChanged(ESeason OldSeason, ESeason NewSeason)
{
	CurrentSeason = NewSeason;

	UE_LOG(LogTemp, Log, TEXT("[ItemScatterSpawner] 分区 %s 换季：%s → %s"),
		*ZoneLabel.ToString(), SeasonToText(OldSeason), SeasonToText(NewSeason));

	if (!bSeasonAware || !bClearOnSeasonChange)
	{
		return;
	}

	// 过期不候：上一季的野外采集物全部消失（玩家手里拿着的保留），再按新季节重新撒
	const bool bHadSlots = Slots.Num() > 0;
	ClearAllSpawned();
	if (bHadSlots || bSpawnOnBeginPlay)
	{
		SpawnAll();
	}
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
	RollDailyEntryAvailability();

	// 先按 SpawnCount 建好坑位，再逐个尝试落位；落不下的坑位会在之后的重生流程里继续尝试
	Slots.Reserve(SpawnCount);
	for (int32 i = 0; i < SpawnCount; ++i)
	{
		FScatterSlot NewSlot;

		// 开局按"当天稀有度"选材；若所有稀有项都没摇中，兜底再挑一次，避免野外空无一物
		int32 EntryIndex = PickEntryIndex(true);
		if (EntryIndex == INDEX_NONE)
		{
			EntryIndex = PickEntryIndex(false);
		}
		NewSlot.EntryIndex = EntryIndex;
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

	// 决定这一坑用哪种食材（原坑那类不可用——已满 / 不在当前季节——则换一类）
	int32 EntryIndex = Slot.EntryIndex;
	if (!IsEntryAvailable(EntryIndex, false))
	{
		EntryIndex = PickEntryIndex(false);
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
		UE_LOG(LogTemp, Verbose, TEXT("[ItemScatterSpawner] %s 在 %d 次尝试内没找到可放置的位置（可调小 MinSpacing / MinLandscapeLayerWeight、调大 SpawnRadius，或放宽地形层名单）"),
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
			// 玩家手上拿着的（换季时）不回收，已离开野外；不做 Destroy，避免把玩家物品一起删掉
			if (!IsHeldByPlayer(Slot.Actor))
			{
				Slot.Actor->Destroy();
			}
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
		// 物理材质过滤需要命中结果带 PhysMaterial；默认 LineTrace 不填充，必须显式开启
		QueryParams.bReturnPhysicalMaterial = bFilterByPhysicalSurface;

		if (bOnlyOnLandscape)
		{
			// 只接受地形（Landscape）命中：沿打线路径从上到下遍历全部命中，
			// 跳过屋顶 / 楼板 / 道具等非地形表面，找到第一个 Landscape 碰撞组件为止；
			// 整条线都碰不到地形（悬空、区域完全在建筑底下）则丢弃该点。
			TArray<FHitResult> Hits;
			World->LineTraceMultiByChannel(Hits, Start, End, GroundChannel, QueryParams);

			bool bFoundLandscape = false;
			for (const FHitResult& H : Hits)
			{
				if (H.GetComponent() && H.GetComponent()->IsA<ULandscapeHeightfieldCollisionComponent>())
				{
					Hit = H;
					bFoundLandscape = true;
					break;
				}
			}
			if (!bFoundLandscape)
			{
				return false;
			}
		}
		else if (!World->LineTraceSingleByChannel(Hit, Start, End, GroundChannel, QueryParams))
		{
			// 打不到地面（悬空/区域超出地面）：丢弃这个点
			return false;
		}

		OutLocation = Hit.ImpactPoint;
		Normal = Hit.ImpactNormal;

		// 地形层 / 物理材质筛选：不合格的点直接丢弃，由 SpawnIntoSlot 重新采样
		if (!IsGroundAllowedByFilter(Hit, Hit.ImpactPoint))
		{
			return false;
		}
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

bool AItemScatterSpawner::IsGroundAllowedByFilter(const FHitResult& Hit, const FVector& CandidateLocation)
{
	if (!bFilterByLandscapeLayer && !bFilterByPhysicalSurface)
	{
		return true;
	}

	bool bSawGroundInfo = false;

	// ---------- 通道 1：物理材质 / SurfaceType（全平台可用，打包版推荐）----------
	if (bFilterByPhysicalSurface)
	{
		const bool bHasLists = AllowedSurfaceTypes.Num() > 0 || AllowedPhysicalMaterials.Num() > 0;
		if (bHasLists)
		{
			const UPhysicalMaterial* PhysMat = Hit.PhysMaterial.Get();
			if (!PhysMat)
			{
				if (!bAllowUnknownGround)
				{
					return false;
				}
			}
			else
			{
				bSawGroundInfo = true;

				bool bMatched = false;
				for (const TEnumAsByte<EPhysicalSurface>& Surface : AllowedSurfaceTypes)
				{
					const EPhysicalSurface Value = Surface;
					if (PhysMat->SurfaceType == Value)
					{
						bMatched = true;
						break;
					}
				}
				if (!bMatched)
				{
					for (const TObjectPtr<UPhysicalMaterial>& Mat : AllowedPhysicalMaterials)
					{
						if (Mat.Get() == PhysMat)
						{
							bMatched = true;
							break;
						}
					}
				}
				if (!bMatched)
				{
					return false;
				}
			}
		}
	}

	// ---------- 通道 2：Landscape 图层名（编辑器 / PIE）----------
	if (bFilterByLandscapeLayer)
	{
		const bool bHasLists = AllowedLandscapeLayers.Num() > 0 || BlockedLandscapeLayers.Num() > 0;
		if (bHasLists)
		{
#if WITH_EDITOR
			ULandscapeHeightfieldCollisionComponent* CollisionComp =
				Cast<ULandscapeHeightfieldCollisionComponent>(Hit.GetComponent());
			ULandscapeComponent* RenderComp = CollisionComp ? CollisionComp->GetRenderComponent() : nullptr;
			ULandscapeInfo* Info = RenderComp ? RenderComp->GetLandscapeInfo() : nullptr;

			if (!RenderComp || !Info)
			{
				if (!bAllowUnknownGround)
				{
					return false;
				}
			}
			else
			{
				bSawGroundInfo = true;

				// 同一渲染组件内跨采样点复用权重纹理数据，避免每次重读整张 weightmap
				if (CachedLayerComponent.Get() != RenderComp)
				{
					CachedLayerComponent = RenderComp;
					LayerWeightCache.Reset();
				}

				auto WeightOf = [&](FName LayerName) -> float
				{
					ULandscapeLayerInfoObject* Layer = Info->GetLayerInfoByName(LayerName);
					if (!Layer)
					{
						return -1.f;
					}
					TArray<uint8>& Cache = LayerWeightCache.FindOrAdd(Layer);
					return RenderComp->GetLayerWeightAtLocation(CandidateLocation, Layer, &Cache);
				};

				// 黑名单优先：踩到任何被禁止的主导层就丢弃
				for (const FName& LayerName : BlockedLandscapeLayers)
				{
					if (WeightOf(LayerName) >= MinLandscapeLayerWeight)
					{
						return false;
					}
				}
				// 白名单：至少一个允许的层权重达标
				if (AllowedLandscapeLayers.Num() > 0)
				{
					bool bAnyAllowed = false;
					for (const FName& LayerName : AllowedLandscapeLayers)
					{
						if (WeightOf(LayerName) >= MinLandscapeLayerWeight)
						{
							bAnyAllowed = true;
							break;
						}
					}
					if (!bAnyAllowed)
					{
						return false;
					}
				}
			}
#else
			// 打包版没有 GetLayerInfoByName / GetLayerWeightAtLocation：按"未知"处理，
			// 且不引用 editor-only 符号，保证 Game target 能链接。要打包请改用物理材质通道。
			if (!bAllowUnknownGround)
			{
				return false;
			}
#endif
		}
	}

	return bSawGroundInfo || bAllowUnknownGround;
}

float AItemScatterSpawner::GetLandscapeLayerWeight(FName LayerName, const FVector& WorldLocation) const
{
#if WITH_EDITOR
	if (!GetWorld() || LayerName.IsNone())
	{
		return -1.f;
	}

	// 竖直打线找到该点下方的地形渲染组件，再读它的权重图
	const FVector Start = WorldLocation + FVector(0.f, 0.f, TraceUp);
	const FVector End = WorldLocation - FVector(0.f, 0.f, TraceDown);
	FCollisionQueryParams QueryParams(FName(TEXT("ItemScatterLayerProbe")), false, this);
	TArray<FHitResult> Hits;
	GetWorld()->LineTraceMultiByChannel(Hits, Start, End, GroundChannel, QueryParams);

	for (const FHitResult& H : Hits)
	{
		ULandscapeHeightfieldCollisionComponent* CollisionComp =
			Cast<ULandscapeHeightfieldCollisionComponent>(H.GetComponent());
		ULandscapeComponent* RenderComp = CollisionComp ? CollisionComp->GetRenderComponent() : nullptr;
		ULandscapeInfo* Info = RenderComp ? RenderComp->GetLandscapeInfo() : nullptr;
		ULandscapeLayerInfoObject* Layer = Info ? Info->GetLayerInfoByName(LayerName) : nullptr;
		if (RenderComp && Layer)
		{
			TArray<uint8> Cache;
			return RenderComp->GetLayerWeightAtLocation(WorldLocation, Layer, &Cache);
		}
	}
	return -1.f;
#else
	return -1.f;
#endif
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

bool AItemScatterSpawner::IsEntryAvailable(int32 EntryIndex, bool bUseDailyFilter) const
{
	if (!Entries.IsValidIndex(EntryIndex))
	{
		return false;
	}

	const FScatterSpawnEntry& Entry = Entries[EntryIndex];
	if (!Entry.ItemClass)
	{
		return false;
	}

	// 季节过滤：Seasons 留空 = 四季通用
	if (bSeasonAware && Entry.Seasons.Num() > 0 && !Entry.Seasons.Contains(GetCurrentSeason()))
	{
		return false;
	}

	// 每类数量上限
	if (Entry.MaxAlive > 0 && CountAliveOfEntry(EntryIndex) >= Entry.MaxAlive)
	{
		return false;
	}

	// 当天稀有度：DailySpawnChance 没摇中的项当天不参与刷新
	if (bUseDailyFilter && DailyEntryAvailable.IsValidIndex(EntryIndex) && !DailyEntryAvailable[EntryIndex])
	{
		return false;
	}

	return true;
}

void AItemScatterSpawner::RollDailyEntryAvailability()
{
	EnsureRandomReady();

	DailyEntryAvailable.Reset(Entries.Num());
	for (const FScatterSpawnEntry& Entry : Entries)
	{
		const float Chance = FMath::Clamp(Entry.DailySpawnChance, 0.f, 1.f);
		DailyEntryAvailable.Add(Chance >= 1.f || Rand.FRand() <= Chance);
	}
}

int32 AItemScatterSpawner::PickEntryIndex(bool bUseDailyFilter)
{
	float TotalWeight = 0.f;
	for (int32 i = 0; i < Entries.Num(); ++i)
	{
		if (IsEntryAvailable(i, bUseDailyFilter))
		{
			TotalWeight += FMath::Max(Entries[i].Weight, 0.01f);
		}
	}

	if (TotalWeight <= 0.f)
	{
		return INDEX_NONE;
	}

	float Pick = Rand.FRandRange(0.f, TotalWeight);
	int32 LastAvailable = INDEX_NONE;
	for (int32 i = 0; i < Entries.Num(); ++i)
	{
		if (!IsEntryAvailable(i, bUseDailyFilter))
		{
			continue;
		}
		LastAvailable = i;
		Pick -= FMath::Max(Entries[i].Weight, 0.01f);
		if (Pick <= 0.f)
		{
			return i;
		}
	}

	// 数值误差兜底：返回最后一个可用项（而不是像旧版那样返回任意有类的项）
	return LastAvailable;
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
