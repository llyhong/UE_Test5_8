// Fill out your copyright notice in the Description page of Project Settings.

#include "Item/FoodSpawner.h"
#include "Game/BaseGameInstance.h"
#include "Machine/StorageWarehouse.h"
#include "Data/ItemActorInfo.h"
#include "UI/SpawnerRestockTipWidget.h"
#include "Components/WidgetComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

namespace
{
	/**
	 * FNV-1a 32 位哈希：结果与平台、构建类型、进程运行次数完全无关，
	 * 因此可以拿它把「关卡名 + Actor 名」映射成一份稳定不变的存档 Key。
	 * （AActor::GetActorGuid 只在编辑器构建中存在，打包 exe 里拿不到，故不再使用。）
	 */
	uint32 StableHash32(const FString& Text, uint32 Seed)
	{
		uint32 Hash = 2166136261u ^ Seed;
		for (int32 Index = 0; Index < Text.Len(); ++Index)
		{
			const uint32 Code = static_cast<uint32>(Text[Index]);
			Hash = (Hash ^ (Code & 0xFFu)) * 16777619u;
			Hash = (Hash ^ ((Code >> 8) & 0xFFu)) * 16777619u;
		}

		// 收尾再混一次，避免不同 seed 生成的 32 位段之间相关性过强
		Hash ^= Hash >> 13;
		Hash *= 0x5BD1E995u;
		Hash ^= Hash >> 15;
		return Hash;
	}
}

AFoodSpawner::AFoodSpawner()
{
	// 补货提示：屏幕空间控件挂在箱子上方，不参与碰撞；默认隐藏，由 Tick 按库存与距离决定显隐
	RestockTipComp = CreateDefaultSubobject<UWidgetComponent>(TEXT("RestockTipComp"));
	RestockTipComp->SetupAttachment(RootComponent);
	RestockTipComp->SetRelativeLocation(FVector(0.f, 0.f, RestockTipHeight));
	RestockTipComp->SetWidgetSpace(EWidgetSpace::Screen);
	RestockTipComp->SetDrawAtDesiredSize(true);
	RestockTipComp->SetTwoSided(true);
	RestockTipComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RestockTipComp->SetGenerateOverlapEvents(false);
	RestockTipComp->SetWidgetClass(USpawnerRestockTipWidget::StaticClass());
	RestockTipComp->SetVisibility(false);
}

FString AFoodSpawner::BuildSaveKeyText() const
{
	// 手动指定优先（重命名过箱子 / 运行时生成的箱子用这个固定住存档）
	if (!SpawnerKeyOverride.IsNone())
	{
		return SpawnerKeyOverride.ToString();
	}

	FString MapName;
	if (const UWorld* World = GetWorld())
	{
		// PIE 下地图名带 UEDPIE_0_ 前缀，去掉后与打包 exe 里读到的名字完全一致
		MapName = UWorld::RemovePIEPrefix(World->GetMapName());
	}

	// 同一关卡内 Actor 名唯一，因此「关卡名 + Actor 名」足以稳定定位一个箱子
	return FString::Printf(TEXT("%s|%s"), *MapName, *GetFName().ToString());
}

FGuid AFoodSpawner::GetSpawnerSaveGuid()
{
	if (!bSpawnerGuidResolved)
	{
		const FString KeyText = BuildSaveKeyText();

		// 用四个不同种子的哈希拼成 GUID：同一次构建、编辑器与打包版之间都保持一致
		const uint32 PartA = StableHash32(KeyText, 0x00000000u);
		const uint32 PartB = StableHash32(KeyText, 0x9E3779B9u);
		const uint32 PartC = StableHash32(KeyText, 0x85EBCA6Bu);
		const uint32 PartD = StableHash32(KeyText, 0xC2B2AE35u);

		SpawnerGuid = FGuid(PartA, PartB, PartC, PartD);
		bSpawnerGuidResolved = true;
	}
	return SpawnerGuid;
}

bool AFoodSpawner::CanSpawn() const
{
	// 野外专属食材（DT_ItemInfo::bWildOnly）默认不从食材箱产出：
	// 直接读生成目标 CDO 上的 ItemInfoRow，不需要运行时查表，也不影响其他系统。
	if (!bAllowWildOnlyItems && SpawnItemActorClass)
	{
		if (const AItemActor* DefaultItem = SpawnItemActorClass->GetDefaultObject<AItemActor>())
		{
			if (const FItemActorInfo* Row = DefaultItem->ItemInfoRow.GetRow<FItemActorInfo>(TEXT("FoodSpawner::CanSpawn")))
			{
				if (Row->bWildOnly)
				{
					return false;
				}
			}
		}
	}

	// 仓库驱动模式：能否生成取决于仓库中该食材是否有库存（取一件扣一件）
	if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetWorld()->GetGameInstance()))
	{
		if (GI->bLinkSpawnerToWarehouse && SpawnItemActorClass)
		{
			return GI->GetWarehouseStock(SpawnItemActorClass) > 0;
		}
	}
	// 原逻辑：无限(-1)或剩余>0 均可生成；0 表示已空
	return RemainingCount != 0;
}

void AFoodSpawner::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 提示检查不需要每帧做：0.2 秒一次足够，且不影响物品本身的交互 / 腐烂逻辑
	RestockTipAccum += DeltaSeconds;
	if (RestockTipAccum >= 0.2f)
	{
		RestockTipAccum = 0.f;
		UpdateRestockTip();
	}
}

int32 AFoodSpawner::GetEffectiveStock() const
{
	// 与 SupplyWarningWidget 同一口径：仓库联动时以仓库库存为准，否则看本箱剩余数量
	if (const UWorld* World = GetWorld())
	{
		if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(World->GetGameInstance()))
		{
			if (GI->bLinkSpawnerToWarehouse && SpawnItemActorClass)
			{
				return GI->GetWarehouseStock(SpawnItemActorClass);
			}
		}
	}
	return RemainingCount;
}

int32 AFoodSpawner::GetLowStockThreshold() const
{
	if (const UWorld* World = GetWorld())
	{
		if (const UBaseGameInstance* GI = Cast<UBaseGameInstance>(World->GetGameInstance()))
		{
			return FMath::Max(1, GI->SpawnerLowStockThreshold);
		}
	}
	return 3;
}

bool AFoodSpawner::IsLowStock() const
{
	const int32 Stock = GetEffectiveStock();
	// 无限箱（-1）永远够用，不提示
	return Stock >= 0 && Stock < GetLowStockThreshold();
}

bool AFoodSpawner::IsLocalPlayerNearRestockTip() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	if (RestockTipHideDistance <= 0.f)
	{
		return true;
	}

	const float DistSq = FMath::Square(RestockTipHideDistance);
	const FVector BoxLocation = GetActorLocation();
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		if (!PC || !PC->IsLocalController())
		{
			continue;
		}
		if (const APawn* Pawn = PC->GetPawn())
		{
			if (FVector::DistSquared(BoxLocation, Pawn->GetActorLocation()) <= DistSq)
			{
				return true;
			}
		}
	}
	return false;
}

FText AFoodSpawner::GetRestockTipTitle() const
{
	FString ItemName = TEXT("食材");
	if (SpawnItemActorClass)
	{
		if (const AItemActor* CDO = SpawnItemActorClass->GetDefaultObject<AItemActor>())
		{
			if (const FItemActorInfo* Row = CDO->ItemInfoRow.GetRow<FItemActorInfo>(TEXT("FoodSpawner::GetRestockTipTitle")))
			{
				if (!Row->ItemDes.IsEmpty())
				{
					ItemName = Row->ItemDes;
				}
			}
		}
	}

	const int32 Stock = FMath::Max(0, GetEffectiveStock());
	return FText::FromString(FString::Printf(TEXT("%s不足（%d/%d）"), *ItemName, Stock, GetLowStockThreshold()));
}

void AFoodSpawner::UpdateRestockTip()
{
	if (!RestockTipComp)
	{
		return;
	}

	const bool bShow = IsLowStock() && IsLocalPlayerNearRestockTip();
	if (bShow)
	{
		if (!RestockTipComp->GetUserWidgetObject())
		{
			RestockTipComp->InitWidget();
		}

		if (USpawnerRestockTipWidget* Tip = Cast<USpawnerRestockTipWidget>(RestockTipComp->GetUserWidgetObject()))
		{
			const FText Title = GetRestockTipTitle();
			const int32 Signature = static_cast<int32>(HashCombine(GetTypeHash(Title.ToString()), GetEffectiveStock()));
			if (Signature != LastRestockTipSignature)
			{
				LastRestockTipSignature = Signature;
				Tip->SetTip(Title, FText::FromString(TEXT("需要到野外补货")));
			}
		}
	}
	else
	{
		// 隐藏期间库存可能变化，下次显示时重写文案
		LastRestockTipSignature = INDEX_NONE;
	}

	if (RestockTipComp->IsVisible() != bShow)
	{
		RestockTipComp->SetVisibility(bShow);
	}
}

void AFoodSpawner::BeginPlay()
{
	Super::BeginPlay();

	if (RestockTipComp)
	{
		RestockTipComp->SetRelativeLocation(FVector(0.f, 0.f, RestockTipHeight));
	}

	// 仅有限模式参与存档；无限(-1)不需要持久化（仓库驱动模式由仓库库存决定，也不走此处）
	if (RemainingCount < 0)
	{
		return;
	}

	if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetWorld()->GetGameInstance()))
	{
		const FGuid SaveGuid = GetSpawnerSaveGuid();
		const int32 Saved = GI->GetSpawnerRemaining(SaveGuid);
		if (Saved >= 0)
		{
			// 有存档记录：用存档值覆盖（玩家之前摘取过）
			RemainingCount = Saved;
		}
		else
		{
			// 首次出现：把编辑器里设置的初始值写入存档内存（落盘统一在关卡退出时由 BaseLevelBP::EndPlay 完成）
			GI->SetSpawnerRemaining(SaveGuid, RemainingCount);
		}
	}
}

void AFoodSpawner::ConsumeOne()
{
	// 仓库驱动模式：从仓库库存取料（取一件仓库减一件），不再动自身 RemainingCount
	if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetWorld()->GetGameInstance()))
	{
		if (GI->bLinkSpawnerToWarehouse && SpawnItemActorClass)
		{
			GI->ConsumeWarehouseItem(SpawnItemActorClass);
			return;
		}
	}

	// 仅在有限模式下递减；无限(-1)保持不动
	if (RemainingCount > 0)
	{
		--RemainingCount;
		// 写回存档内存（落盘统一在关卡退出时由 BaseLevelBP::EndPlay 完成）
		if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetWorld()->GetGameInstance()))
		{
			GI->SetSpawnerRemaining(GetSpawnerSaveGuid(), RemainingCount);
		}
	}
}

void AFoodSpawner::AddStock(int32 Quantity)
{
	// 无限箱不受库存限制，补货没有意义
	if (Quantity <= 0 || RemainingCount < 0)
	{
		return;
	}

	RemainingCount += Quantity;
	if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetWorld()->GetGameInstance()))
	{
		GI->SetSpawnerRemaining(GetSpawnerSaveGuid(), RemainingCount);
	}
}

void AFoodSpawner::OnSpawnActorEvent()
{
	OnSpawnActor.Broadcast();
}
