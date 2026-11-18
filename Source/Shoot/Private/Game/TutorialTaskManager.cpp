// Fill out your copyright notice in the Description page of Project Settings.

#include "Game/TutorialTaskManager.h"

#include "Data/TutorialTaskData.h"
#include "Data/ItemActorInfo.h"
#include "Game/BaseGameInstance.h"
#include "Game/BaseLevelBP.h"
#include "Game/BusinessSimulation.h"
#include "Game/BusinessTime.h"
#include "Dining/DiningHygieneManager.h"
#include "Dining/DiningMessActor.h"
#include "Item/ItemActor.h"
#include "Item/Plant.h"
#include "Item/OutdoorBasket.h"
#include "Machine/ResearchStove.h"
#include "Machine/StorageWarehouse.h"
#include "NPC/GuestManager.h"
#include "NPC/GuestCharacter.h"
#include "NPC/ChairSeat.h"
#include "ShootCharacter.h"
#include "UI/TutorialGuideWidget.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Engine/DataTable.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	/** 从背包槽位取物品 ID（行名是数字时以行名为准） */
	int32 SlotItemID(const FInventorySlot& Slot)
	{
		const int32 RowID = FItemActorInfo::GetItemIDFromRowName(Slot.ItemInfoRow.RowName);
		if (RowID != INDEX_NONE)
		{
			return RowID;
		}
		if (const AItemActor* CDO = Slot.ItemClass ? Slot.ItemClass->GetDefaultObject<AItemActor>() : nullptr)
		{
			return CDO->GetItemID();
		}
		return -1;
	}

	FVector GetLocalPlayerReferenceLocation(const UWorld* World)
	{
		if (World)
		{
			for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
			{
				APlayerController* PC = It->Get();
				if (PC && PC->IsLocalController() && PC->GetPawn())
				{
					return PC->GetPawn()->GetActorLocation();
				}
			}
		}
		return FVector::ZeroVector;
	}
}

ATutorialTaskManager::ATutorialTaskManager()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ATutorialTaskManager::BeginPlay()
{
	Super::BeginPlay();

	if (!TaskTable)
	{
		// 用 LoadObject 而不是构造函数里的 FObjectFinder：配置表可能在编辑器启动后才创建
		TaskTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/_Demo/Data/Tutorial/DT_TutorialTasks"));
	}

	InitDelay = 0.4f;
	PollAccum = PollInterval;
}

void ATutorialTaskManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bEnable || !TaskTable)
	{
		return;
	}

	// 等所有 Actor 的 BeginPlay 跑完再接管卫生管理器（否则初始撒点会盖掉“只刷 2 个垃圾”）
	if (InitDelay > 0.f)
	{
		InitDelay -= DeltaSeconds;
		if (InitDelay > 0.f)
		{
			return;
		}
		AppliedDay = INDEX_NONE;
	}

	PollAccum += DeltaSeconds;
	if (PollAccum < PollInterval)
	{
		return;
	}
	PollAccum = 0.f;

	const int32 Today = GetCurrentDay();
	if (Today != AppliedDay)
	{
		ApplyDay(Today);
	}

	UpdateProgress();
	UpdateFocusTarget();
	RefreshWidgets();
}

void ATutorialTaskManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ActiveTasks.Reset();
	Super::EndPlay(EndPlayReason);
}

UTimeSystem* ATutorialTaskManager::GetTimeSystem() const
{
	if (const UWorld* World = GetWorld())
	{
		if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(World->GetGameInstance()))
		{
			if (GI->businessSimulation)
			{
				return GI->businessSimulation->TimeSystem;
			}
		}
	}
	return nullptr;
}

int32 ATutorialTaskManager::GetCurrentDay() const
{
	const UTimeSystem* TS = GetTimeSystem();
	return TS ? FMath::Max(1, TS->Day) : 1;
}

ADiningHygieneManager* ATutorialTaskManager::FindHygieneManager() const
{
	return Cast<ADiningHygieneManager>(UGameplayStatics::GetActorOfClass(this, ADiningHygieneManager::StaticClass()));
}

const ABaseLevelBP* ATutorialTaskManager::GetLevelBP() const
{
	if (const UWorld* World = GetWorld())
	{
		if (const UBaseGameInstance* GI = Cast<UBaseGameInstance>(World->GetGameInstance()))
		{
			return GI->curLevelBP;
		}
	}
	return nullptr;
}

void ATutorialTaskManager::ResetRuntimeState()
{
	ActiveTasks.Reset();
	PrevHeldItemID.Reset();
	FocusHint = FText::GetEmpty();
	FocusLocation = FVector::ZeroVector;
	bHasFocusTarget = false;
	ServeBaseline = 0;
	TrashBaseline = 0;
	MoneyBaseline = 0;
}

void ATutorialTaskManager::ApplyDay(int32 Day)
{
	AppliedDay = Day;
	ResetRuntimeState();

	if (!TaskTable)
	{
		return;
	}

	TArray<FTutorialTaskRow*> Rows;
	TaskTable->GetAllRows<FTutorialTaskRow>(TEXT("TutorialTaskManager"), Rows);
	for (FTutorialTaskRow* Row : Rows)
	{
		if (Row && Row->Day == Day)
		{
			FActiveTask Task;
			Task.Row = *Row;
			ActiveTasks.Add(Task);
		}
	}

	ActiveTasks.Sort([](const FActiveTask& A, const FActiveTask& B)
	{
		return A.Row.StepOrder < B.Row.StepOrder;
	});

	// 接待客人的基线：服务计数可能跨天累计，按当天起点做差
	ServeBaseline = GetLevelBP() ? GetLevelBP()->GuestsServedThisLevel : 0;
	MoneyBaseline = GetLevelBP() ? GetLevelBP()->MoneyEarnedThisLevel : 0;

	// 当天有“清理垃圾”任务：接管就餐区卫生，只刷配置数量并暂停自动撒点
	int32 RequiredTrash = 0;
	for (const FActiveTask& Task : ActiveTasks)
	{
		if (Task.Row.TaskType == ETutorialTaskType::CleanTrash)
		{
			RequiredTrash += FMath::Max(1, Task.Row.TargetCount);
		}
	}

	if (ADiningHygieneManager* Hygiene = FindHygieneManager())
	{
		if (RequiredTrash > 0)
		{
			Hygiene->SetAutoSpawnEnabled(false);
			Hygiene->ClearAllMesses();
			bHygieneAutoSpawnOverridden = true;
			for (int32 Index = 0; Index < RequiredTrash; ++Index)
			{
				Hygiene->SpawnMessInArea(EDiningMessType::Trash);
			}
			TrashBaseline = Hygiene->GetTotalCleanedCount();
		}
		else if (NoMessesDays.Contains(Day))
		{
			// 配置的“无垃圾日”（第 1 天）：暂停自动撒点并清空现有散落物
			Hygiene->SetAutoSpawnEnabled(false);
			Hygiene->ClearAllMesses();
			bHygieneAutoSpawnOverridden = true;
		}
		else if (bHygieneAutoSpawnOverridden)
		{
			Hygiene->SetAutoSpawnEnabled(true);
			bHygieneAutoSpawnOverridden = false;
		}
	}

	// 篮子基线：任务开始前篮子里已有的食材不算进度
	for (FActiveTask& Task : ActiveTasks)
	{
		Task.LastBasketTotal = CountItemInBaskets(Task.Row.TargetItemID);
	}
}

int32 ATutorialTaskManager::CountItemAcrossPlayers(int32 ItemID) const
{
	if (ItemID < 0)
	{
		return 0;
	}

	int32 Total = 0;
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0;
	}

	for (TActorIterator<AShootCharacter> It(const_cast<UWorld*>(World)); It; ++It)
	{
		AShootCharacter* Char = *It;
		if (!IsValid(Char))
		{
			continue;
		}
		for (const FInventorySlot& Slot : Char->GetInventoryItems())
		{
			if (SlotItemID(Slot) == ItemID)
			{
				Total += Slot.Quantity;
			}
		}
		if (const AItemActor* Held = Char->GetPickingItem())
		{
			if (!Cast<AOutdoorBasket>(Held) && Held->GetItemID() == ItemID)
			{
				Total += 1;
			}
		}
	}
	return Total;
}

int32 ATutorialTaskManager::CountItemInBaskets(int32 ItemID) const
{
	if (ItemID < 0)
	{
		return 0;
	}

	int32 Total = 0;
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0;
	}
	for (TActorIterator<AOutdoorBasket> It(const_cast<UWorld*>(World)); It; ++It)
	{
		if (!IsValid(*It))
		{
			continue;
		}
		for (const FInventorySlot& Slot : It->BasketItems)
		{
			if (SlotItemID(Slot) == ItemID)
			{
				Total += Slot.Quantity;
			}
		}
	}
	return Total;
}

int32 ATutorialTaskManager::CountItemInStorages(int32 ItemID, FName ActorTag) const
{
	int32 Total = 0;
	const UWorld* World = GetWorld();
	if (!World || ItemID < 0)
	{
		return 0;
	}
	for (TActorIterator<AStorageWarehouse> It(const_cast<UWorld*>(World)); It; ++It)
	{
		AStorageWarehouse* Warehouse = *It;
		if (!IsValid(Warehouse) || (!ActorTag.IsNone() && !Warehouse->ActorHasTag(ActorTag)))
		{
			continue;
		}
		for (const FInventorySlot& Slot : Warehouse->StoredItems)
		{
			if (SlotItemID(Slot) == ItemID)
			{
				Total += Slot.Quantity;
			}
		}
	}
	return Total;
}

int32 ATutorialTaskManager::CountItemInResearch(int32 ItemID, FName ActorTag) const
{
	int32 Total = 0;
	const UWorld* World = GetWorld();
	if (!World || ItemID < 0)
	{
		return 0;
	}
	for (TActorIterator<AResearchStove> It(const_cast<UWorld*>(World)); It; ++It)
	{
		AResearchStove* Stove = *It;
		if (!IsValid(Stove) || (!ActorTag.IsNone() && !Stove->ActorHasTag(ActorTag)))
		{
			continue;
		}
		for (int32 StoredID : Stove->StoredItemIDs)
		{
			if (StoredID == ItemID)
			{
				++Total;
			}
		}
	}
	return Total;
}

void ATutorialTaskManager::UpdateProgress()
{
	if (ActiveTasks.Num() == 0)
	{
		return;
	}

	// 每次轮询只统计一次“刚拿到手上”的物品，供“空手采摘”任务使用
	TMap<int32, int32> NewHandPicks;
	const UWorld* World = GetWorld();
	if (World)
	{
		for (TActorIterator<AShootCharacter> It(const_cast<UWorld*>(World)); It; ++It)
		{
			AShootCharacter* Char = *It;
			if (!IsValid(Char))
			{
				continue;
			}
			const AItemActor* Held = Char->GetPickingItem();
			const int32 HeldID = (Held && !Cast<AOutdoorBasket>(Held)) ? Held->GetItemID() : -1;
			const int32 PrevID = PrevHeldItemID.FindRef(TWeakObjectPtr<AShootCharacter>(Char));
			if (HeldID >= 0 && HeldID != PrevID && Char->GetEquippedBasket() == nullptr)
			{
				NewHandPicks.FindOrAdd(HeldID)++;
			}
			PrevHeldItemID.Add(TWeakObjectPtr<AShootCharacter>(Char), HeldID);
		}
	}

	ADiningHygieneManager* Hygiene = FindHygieneManager();
	const ABaseLevelBP* LevelBP = GetLevelBP();

	for (FActiveTask& Task : ActiveTasks)
	{
		if (Task.bDone)
		{
			continue;
		}

		switch (Task.Row.TaskType)
		{
		case ETutorialTaskType::CleanTrash:
			if (Hygiene)
			{
				Task.Progress = FMath::Max(0, Hygiene->GetTotalCleanedCount() - TrashBaseline);
			}
			break;
		case ETutorialTaskType::ServeGuests:
			if (LevelBP)
			{
				Task.Progress = FMath::Max(0, LevelBP->GuestsServedThisLevel - ServeBaseline);
			}
			break;
		case ETutorialTaskType::EarnMoney:
			if (LevelBP)
			{
				Task.Progress = FMath::Max(0, LevelBP->MoneyEarnedThisLevel - MoneyBaseline);
			}
			break;
		case ETutorialTaskType::PickupItem:
			Task.MaxSeen = FMath::Max(Task.MaxSeen, CountItemAcrossPlayers(Task.Row.TargetItemID));
			Task.Progress = Task.MaxSeen;
			break;
		case ETutorialTaskType::HarvestQuest:
			if (Task.Row.bRequireBasket)
			{
				const int32 Total = CountItemInBaskets(Task.Row.TargetItemID);
				if (Total > Task.LastBasketTotal)
				{
					Task.Progress += Total - Task.LastBasketTotal;
				}
				Task.LastBasketTotal = Total;
			}
			else
			{
				Task.Progress += NewHandPicks.FindRef(Task.Row.TargetItemID);
			}
			break;
		case ETutorialTaskType::ResearchItem:
			Task.MaxSeen = FMath::Max(Task.MaxSeen, CountItemInResearch(Task.Row.TargetItemID, Task.Row.TargetActorTag));
			Task.Progress = Task.MaxSeen;
			break;
		case ETutorialTaskType::StoreToWarehouse:
			Task.MaxSeen = FMath::Max(Task.MaxSeen, CountItemInStorages(Task.Row.TargetItemID, Task.Row.TargetActorTag));
			Task.Progress = Task.MaxSeen;
			break;
		default:
			break;
		}

		Task.Progress = FMath::Clamp(Task.Progress, 0, FMath::Max(1, Task.Row.TargetCount));
		if (Task.Progress >= FMath::Max(1, Task.Row.TargetCount))
		{
			Task.bDone = true;
		}
	}
}

AActor* ATutorialTaskManager::FindTargetActor(const FActiveTask& Task) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	const FVector Ref = GetLocalPlayerReferenceLocation(World);
	const FName Tag = Task.Row.TargetActorTag;

	auto Better = [&Ref](AActor* Best, AActor* Candidate) -> AActor*
	{
		if (!Candidate)
		{
			return Best;
		}
		if (!Best)
		{
			return Candidate;
		}
		return FVector::DistSquared(Candidate->GetActorLocation(), Ref) < FVector::DistSquared(Best->GetActorLocation(), Ref)
			? Candidate : Best;
	};

	AActor* Best = nullptr;
	switch (Task.Row.TaskType)
	{
	case ETutorialTaskType::CleanTrash:
	{
		if (ADiningHygieneManager* Hygiene = FindHygieneManager())
		{
			for (ADiningMessActor* Mess : Hygiene->ActiveMesses)
			{
				Best = Better(Best, Mess);
			}
		}
		break;
	}
	case ETutorialTaskType::ServeGuests:
	{
		// 箭头指向“有客人坐着的餐桌”（椅子），而不是跟着走动的客人一路跑
		for (TActorIterator<AChairSeat> It(World); It; ++It)
		{
			AChairSeat* Seat = *It;
			if (!IsValid(Seat) || !IsValid(Seat->Occupant))
			{
				continue;
			}
			Best = Better(Best, Seat);
		}
		if (!Best)
		{
			if (AGuestManager* Mgr = Cast<AGuestManager>(UGameplayStatics::GetActorOfClass(this, AGuestManager::StaticClass())))
			{
				Best = Mgr;
			}
		}
		break;
	}
	case ETutorialTaskType::EarnMoney:
	{
		// 赚钱没有固定目标物：指向最近有客人的餐桌（继续接待客人才能赚钱）
		for (TActorIterator<AChairSeat> It(World); It; ++It)
		{
			AChairSeat* Seat = *It;
			if (!IsValid(Seat) || !IsValid(Seat->Occupant))
			{
				continue;
			}
			Best = Better(Best, Seat);
		}
		break;
	}
	case ETutorialTaskType::PickupItem:
	{
		for (TActorIterator<AItemActor> It(World); It; ++It)
		{
			AItemActor* Item = *It;
			if (!IsValid(Item) || Item->GetAttachParentActor())
			{
				continue;
			}
			// 鸡蛋等食材可能没有散落实例，而是由“生成箱”产出：箱子的产出类也算目标
			int32 ItemID = Item->GetItemID();
			if (ItemID != Task.Row.TargetItemID && Item->SpawnItemActorClass)
			{
				const AItemActor* SpawnCDO = Item->SpawnItemActorClass->GetDefaultObject<AItemActor>();
				ItemID = SpawnCDO ? SpawnCDO->GetItemID() : ItemID;
			}
			if (ItemID != Task.Row.TargetItemID)
			{
				continue;
			}
			Best = Better(Best, Item);
		}
		break;
	}
	case ETutorialTaskType::HarvestQuest:
	{
		// 先找现在就能采的，没熟就退回到任意同种作物（箭头至少把人带到正确的田里）
		for (int32 Pass = 0; Pass < 2 && !Best; ++Pass)
		{
			for (TActorIterator<APlant> It(World); It; ++It)
			{
				APlant* Plant = *It;
				if (!IsValid(Plant) || Plant->GetFruitItemID() != Task.Row.TargetItemID)
				{
					continue;
				}
				if (!Tag.IsNone() && !Plant->ActorHasTag(Tag))
				{
					continue;
				}
				if (Pass == 0 && !Plant->CanHarvest())
				{
					continue;
				}
				Best = Better(Best, Plant);
			}
		}
		break;
	}
	case ETutorialTaskType::ResearchItem:
	{
		for (TActorIterator<AResearchStove> It(World); It; ++It)
		{
			if (Tag.IsNone() || It->ActorHasTag(Tag))
			{
				Best = Better(Best, *It);
			}
		}
		break;
	}
	case ETutorialTaskType::StoreToWarehouse:
	{
		for (TActorIterator<AStorageWarehouse> It(World); It; ++It)
		{
			if (Tag.IsNone() || It->ActorHasTag(Tag))
			{
				Best = Better(Best, *It);
			}
		}
		break;
	}
	default:
		break;
	}
	return Best;
}

void ATutorialTaskManager::UpdateFocusTarget()
{
	bHasFocusTarget = false;
	FocusHint = FText::GetEmpty();

	for (const FActiveTask& Task : ActiveTasks)
	{
		if (Task.bDone)
		{
			continue;
		}
		if (AActor* Target = FindTargetActor(Task))
		{
			FocusLocation = Target->GetActorLocation();
			bHasFocusTarget = true;
			FocusHint = Task.Row.HintText.IsEmpty() ? Task.Row.Description : Task.Row.HintText;
			return;
		}
		// 该任务暂时找不到目标（例如垃圾已清完但计数未到）：也把它的提示作为引导
		if (FocusHint.IsEmpty())
		{
			FocusHint = Task.Row.HintText.IsEmpty() ? Task.Row.Description : Task.Row.HintText;
		}
	}
}

TArray<FString> ATutorialTaskManager::GetChecklistLines() const
{
	TArray<FString> Lines;
	for (const FActiveTask& Task : ActiveTasks)
	{
		const int32 Target = FMath::Max(1, Task.Row.TargetCount);
		const FString Desc = Task.Row.Description.IsEmpty()
			? (Task.Row.HintText.IsEmpty() ? TEXT("任务") : Task.Row.HintText.ToString())
			: Task.Row.Description.ToString();
		Lines.Add(FString::Printf(TEXT("%s %s  %d/%d"),
			Task.bDone ? TEXT("[√]") : TEXT("[ ]"), *Desc, FMath::Min(Task.Progress, Target), Target));
	}

	// 按天追加的提示行（如第 3 天提醒客人耐心值恢复限制）
	if (const FText* Note = DayExtraNotes.Find(AppliedDay))
	{
		if (!Note->IsEmpty())
		{
			Lines.Add(FString::Printf(TEXT("※ %s"), *Note->ToString()));
		}
	}
	return Lines;
}

FText ATutorialTaskManager::GetDayTitle() const
{
	return FText::FromString(FString::Printf(TEXT("第 %d 天任务"), FMath::Max(1, AppliedDay)));
}

bool ATutorialTaskManager::GetFocusTargetLocation(FVector& OutLocation) const
{
	if (!bHasFocusTarget)
	{
		return false;
	}
	OutLocation = FocusLocation;
	return true;
}

void ATutorialTaskManager::RefreshWidgets() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (PC && PC->IsLocalController())
		{
			UTutorialGuideWidget::EnsureForPlayer(PC, const_cast<ATutorialTaskManager*>(this));
		}
	}
}
