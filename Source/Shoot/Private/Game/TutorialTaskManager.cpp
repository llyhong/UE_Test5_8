// Fill out your copyright notice in the Description page of Project Settings.

#include "Game/TutorialTaskManager.h"

#include "Data/TutorialTaskData.h"
#include "Data/ItemActorInfo.h"
#include "Game/BaseGameInstance.h"
#include "Game/BaseLevelBP.h"
#include "Game/OrderSystem.h"
#include "Game/BusinessSimulation.h"
#include "Game/BusinessTime.h"
#include "Dining/DiningHygieneManager.h"
#include "Dining/DiningMessActor.h"
#include "Item/ItemActor.h"
#include "Item/FoodSpawner.h"
#include "Game/FoodSystem.h"
#include "Item/Plant.h"
#include "Item/OutdoorBasket.h"
#include "Item/HarvestBasketArea.h"
#include "Machine/ResearchStove.h"
#include "Machine/StorageWarehouse.h"
#include "Machine/CookingMachine.h"
#include "Machine/GateCounter.h"
#include "Machine/Dishwasher.h"
#include "MySaveGame.h"
#include "NPC/GuestCharacter.h"
#include "NPC/ChairSeat.h"
#include "ShootCharacter.h"
#include "UI/TutorialGuideWidget.h"
#include "UI/HUDWidget.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Engine/DataTable.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// Day one used to store the final order target under key 8.
	constexpr int32 FirstDayFinalOrdersKey = 8;

	int32 GetFirstDaySaveKey(const FTutorialTaskRow& Row)
	{
		return Row.StepOrder == 9 ? FirstDayFinalOrdersKey : Row.StepOrder;
	}

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
	static ConstructorHelpers::FClassFinder<AItemActor> PlateFinder(TEXT("/Game/_Demo/Blueprints/Pickable/Plate/BP_Plate"));
	PracticePlateClass = PlateFinder.Class;
}

void ATutorialTaskManager::BeginPlay()
{
	Super::BeginPlay();
	// Merge old map/Blueprint overrides that still serialize only day one.
	for (int32 Day : {1, 2, 3, 4}) NoMessesDays.AddUnique(Day);

	if (!TaskTable)
	{
		// 用 LoadObject 而不是构造函数里的 FObjectFinder：配置表可能在编辑器启动后才创建
		TaskTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/_Demo/Data/Tutorial/DT_TutorialTasks"));
	}

	InitDelay = 0.4f;
	PollAccum = PollInterval;

	// 蓝图 Class Defaults 勾选 Auto Start Day 3 时，第一帧 Tick 自动跳到第 3 天
	// （放在 Tick 而不是 BeginPlay：保证其它 Actor 的 BeginPlay 已跑完，时钟/存档已就绪）
	bDebugJumpPending = bAutoStartDay3;
}

void ATutorialTaskManager::DebugStartFromDay3()
{
	// 1) 只跳过前两天，教程系统本身继续运行（不能调用 SkipTutorial：那会 bEnable=false，
	//    导致 Tick 不再 ApplyDay，第 3 天任务永远加载不出来）。
	bEnable = true;
	if (bHoldingTutorialClock)
	{
		// 释放第 1/2 天引导可能持有的暂停，让第 3 天时钟能正常推进
		if (UTimeSystem* TS = GetTimeSystem())
		{
			TS->SetPaused(TS->IsEndOfDayReached());
		}
		bHoldingTutorialClock = false;
	}
	// 强制下一帧 Tick 按新的第 3 天重新 ApplyDay，加载第 3 天任务卡
	ResetRuntimeState();
	AppliedDay = INDEX_NONE;

	UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance());
	UMySaveGame* Save = GI ? GI->curSaveGameData.Get() : nullptr;

	// 2) 存档标记为教程已跳过，避免读档后引导又回来
	if (Save)
	{
		Save->bFirstServiceTutorialComplete = true;
		Save->FirstDayOrdersServed = FMath::Max(Save->FirstDayOrdersServed, 2);
		for (int32 Day = 1; Day <= 2; ++Day)
		{
			FTutorialDaySave& DaySave = Save->TutorialDays.FindOrAdd(Day);
			DaySave.bOpened = true;
			DaySave.Served = FMath::Max(DaySave.Served, 2);
		}
	}

	// 3) 经营时钟拨到第 3 天：Init 清掉打烊/暂停等瞬时状态，不触发前两天日结算
	if (GI && GI->businessSimulation)
	{
		if (UTimeSystem* TS = GI->businessSimulation->TimeSystem.Get())
		{
			TS->SetPaused(false);
			TS->Init();
			TS->Day = FMath::Clamp(3, 1, FMath::Max(1, TS->DaysPerMonth));
			TS->DayProgress = 0.f;
			TS->SetGoalDrivenBusiness(false);
			if (Save)
			{
				Save->BizYear = TS->Year;
				Save->BizMonth = TS->Month;
				Save->BizDay = TS->Day;
			}
		}
	}

	// 4) 赛季天数同步到第 3 天，避免日评级/检查点还停在第 1 天
	if (GI && GI->seasonSystem)
	{
		GI->seasonSystem->SeasonDay = 3;
		GI->seasonSystem->CheckpointDay = 3;
		GI->seasonSystem->CheckpointDayOfMonth = 3;
		if (Save)
		{
			Save->SeasonDay = 3;
			Save->CheckpointDay = 3;
			Save->CheckpointDayOfMonth = 3;
		}
	}

	if (GI)
	{
		if (GI->businessSimulation) GI->businessSimulation->Save();
		else GI->SaveGameData();
	}
	UE_LOG(LogTemp, Log, TEXT("[Debug] TutorialTaskManager: 已跳过新手教程并从第 3 天开始"));
}

void ATutorialTaskManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 蓝图勾选的调试跳天：在所有 Actor BeginPlay 后只执行一次
	if (bDebugJumpPending)
	{
		bDebugJumpPending = false;
		DebugStartFromDay3();
		return;
	}

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
	const UBaseGameInstance* CurrentGI = Cast<UBaseGameInstance>(GetGameInstance());
	UMySaveGame* CurrentSave = CurrentGI ? CurrentGI->curSaveGameData.Get() : nullptr;
	if (Today != AppliedDay || AppliedSave.Get() != CurrentSave)
	{
		ApplyDay(Today);
	}

	UpdateProgress();
	if (UTimeSystem* TS = GetTimeSystem())
	{
		if (AppliedDay == 1 && TS->IsBusinessOpen())
		{
			ReplenishFirstDayEggs();
		}
		if (IsDayTwoServiceLesson() && TS->IsBusinessOpen() && !ActiveTasks.ContainsByPredicate(
			[](const FActiveTask& Task) { return !Task.bDone; }))
		{
			TS->FinishBusinessDayEarly();
		}
		if (IsFirstServiceTutorialActive())
		{
			TS->SetPaused(true);
			bHoldingTutorialClock = true;
		}
		else if (bHoldingTutorialClock)
		{
			TS->SetPaused(TS->IsEndOfDayReached());
			bHoldingTutorialClock = false;
			if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance()))
			{
				// 释放暂停不等于完成教学（读档或重开也会释放）；只有首个上菜任务完成才保存。
				bool bFirstServeDone = false;
				for (const FActiveTask& Task : ActiveTasks)
					if (Task.Row.TaskType == ETutorialTaskType::ServeGuests)
					{
						bFirstServeDone = AppliedDay == 1 && Task.bDone;
						break;
					}
				if (GI->curSaveGameData && bFirstServeDone)
				{
					GI->curSaveGameData->bFirstServiceTutorialComplete = true;
					GI->SaveGameData();
				}
			}
		}
	}

	UpdateFocusTarget();
	RefreshWidgets();
	SnapshotAccum += PollInterval;
	if (bSavePending || (AppliedDay >= 2 && AppliedDay <= 3 && SnapshotAccum >= 2.f))
	{
		SnapshotAccum = 0.f;
		bSavePending = false;
		if (AppliedDay >= 1 && AppliedDay <= 3)
			if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance())) GI->SaveGameData();
	}

}

void ATutorialTaskManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bHoldingTutorialClock)
		if (UTimeSystem* TS = GetTimeSystem()) TS->SetPaused(false);
	ActiveTasks.Reset();
	Super::EndPlay(EndPlayReason);
}

void ATutorialTaskManager::SkipTutorial()
{
	// 关闭总开关后 Tick 不再装任务/刷新箭头；任务卡自身会在下一帧因 bEnable=false 折叠。
	bEnable = false;
	if (bHoldingTutorialClock)
	{
		if (UTimeSystem* TS = GetTimeSystem())
		{
			TS->SetPaused(TS->IsEndOfDayReached());
		}
		bHoldingTutorialClock = false;
	}
	ResetRuntimeState();
	AppliedDay = INDEX_NONE;
}

bool ATutorialTaskManager::IsFirstServiceTutorialActive() const
{
	if (!bEnable || AppliedDay != 1) return false;
	if (IsFirstDayPlateLessonActive()) return true;
	// 首单之后的备餐和两单练习不再阻止后续客人进场。
	for (const FActiveTask& Task : ActiveTasks)
		if (Task.Row.TaskType == ETutorialTaskType::ServeGuests) return !Task.bDone;
	return false;
}

bool ATutorialTaskManager::IsFirstDayPlateLessonActive() const
{
	if (!bEnable || AppliedDay != 1)
	{
		return false;
	}
	for (const FActiveTask& Task : ActiveTasks)
	{
		if (!Task.bDone)
		{
			return Task.Row.TaskType == ETutorialTaskType::ReturnPlate;
		}
	}
	return false;
}

void ATutorialTaskManager::NotifyAction(UWorld* World, ETutorialTaskType Type, int32 ItemID, AActor* Target, int32 Quantity)
{
	if (!World || Quantity <= 0) return;
	for (TActorIterator<ATutorialTaskManager> It(World); It; ++It)
	{
		if (!It->bEnable || It->bRestoring) continue;
		if (It->AppliedDay != It->GetCurrentDay()) It->ApplyDay(It->GetCurrentDay());
		const UTimeSystem* TS = It->GetTimeSystem();
		if (It->AppliedDay == 1 && Type == ETutorialTaskType::PickupItem && ItemID == FirstDayEggItemID && Target)
		{
			Target->Tags.AddUnique(CollectedFirstDayEggTag);
		}
		if (It->AppliedDay >= 2 && It->AppliedDay <= 3 && Type == ETutorialTaskType::ServeGuests)
			if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(It->GetGameInstance()))
				if (GI->curSaveGameData) GI->curSaveGameData->TutorialDays.FindOrAdd(It->AppliedDay).Served += Quantity;
		for (FActiveTask& Task : It->ActiveTasks)
		{
			if (Task.bDone) continue;
			if (Type == ETutorialTaskType::ReturnPlate && Task.Row.TaskType != Type) break;
			// Preparatory actions may be performed in either order. One event belongs to
			// one matching objective; it never completes a chain of future objectives.
			const bool bPreparation = Type == ETutorialTaskType::PickupItem || Type == ETutorialTaskType::HarvestQuest || Type == ETutorialTaskType::StoreToWarehouse
				|| Type == ETutorialTaskType::ResearchItem;
			if (!bPreparation && Type != ETutorialTaskType::ServeGuests && (!TS || !TS->IsBusinessOpen())) continue;
			if (Task.Row.TaskType != Type || (Task.Row.TargetItemID >= 0 && Task.Row.TargetItemID != ItemID)) continue;
			if (Task.Row.TargetMachineID > 0 && (!Cast<ACookingMachine>(Target)
				|| Cast<ACookingMachine>(Target)->MachineID != Task.Row.TargetMachineID)) continue;
			if (!Task.Row.TargetActorTag.IsNone() && (!Target || !Target->ActorHasTag(Task.Row.TargetActorTag))) continue;
			if (Type == ETutorialTaskType::HarvestQuest && Task.Row.bRequireBasket && !Cast<AOutdoorBasket>(Target)) continue;
			Task.Progress = FMath::Min(Task.Row.TargetCount, Task.Progress + Quantity);
			Task.bDone = Task.Progress >= Task.Row.TargetCount;
			It->SaveCompletedTask(Task);
			// Using an ingredient successfully proves that this same ingredient was
			// taken in hand, even if the player acted between two polling frames.
			if (Type == ETutorialTaskType::ResearchItem || Type == ETutorialTaskType::AddToCooker)
				for (FActiveTask& Earlier : It->ActiveTasks)
				{
					if (&Earlier == &Task) break;
					if (Earlier.Row.TaskType == ETutorialTaskType::PickupItem
						&& Earlier.Row.TargetItemID == ItemID && !Earlier.bDone)
					{
						Earlier.Progress = Earlier.Row.TargetCount; Earlier.bDone = true; It->SaveCompletedTask(Earlier);
					}
				}
			break;
		}
		It->bSavePending = true;
	}
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

bool ATutorialTaskManager::NeedsOpeningGuidance() const
{
	const UTimeSystem* TS = GetTimeSystem();
	if (!bEnable || ActiveTasks.IsEmpty() || !TS || TS->IsBusinessOpen() || TS->IsEndOfDayReached())
		return false;
	if (AppliedDay == 1) return IsFirstServiceTutorialActive();
	if (AppliedDay == 3) return true;
	if (AppliedDay == 2)
	{
		for (const FActiveTask& Task : ActiveTasks)
			if (!Task.bDone) return Task.Row.TaskType == ETutorialTaskType::OpenBusiness || Task.Row.TaskType == ETutorialTaskType::ServeGuests;
		// 先完成篮子采集和入库，轮到接待时才引导开门。
	}
	return false;
}

int32 ATutorialTaskManager::GetCurrentDay() const
{
	const UTimeSystem* TS = GetTimeSystem();
	return TS ? FMath::Max(1, TS->GetBusinessDayNumber()) : 1;
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
	const bool bNewSave = AppliedSave.Get() != (Cast<UBaseGameInstance>(GetGameInstance()) ? Cast<UBaseGameInstance>(GetGameInstance())->curSaveGameData.Get() : nullptr);
	AppliedDay = Day;
	const UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance());
	AppliedSave = GI ? GI->curSaveGameData.Get() : nullptr;
	ResetRuntimeState();

	if (!TaskTable)
	{
		return;
	}

	TArray<FTutorialTaskRow*> Rows;
	const bool bAutoBasket = UGameplayStatics::GetActorOfClass(this, AHarvestBasketArea::StaticClass()) != nullptr;
	TaskTable->GetAllRows<FTutorialTaskRow>(TEXT("TutorialTaskManager"), Rows);
	for (FTutorialTaskRow* Row : Rows)
	{
		if (Row && Row->Day == Day)
		{
			if (Row->TaskType == ETutorialTaskType::CleanTrash
				&& (Day < 5 || NoMessesDays.Contains(Day) || !FindHygieneManager()))
			{
				continue;
			}
			if (Row->TaskType == ETutorialTaskType::ReturnPlate
				&& !UGameplayStatics::GetActorOfClass(this, ADishwasher::StaticClass()))
			{
				continue;
			}
			FActiveTask Task;
			Task.Row = *Row;
			if (bAutoBasket && Day == 2)
			{
				if (Row->TaskType == ETutorialTaskType::EquipBasket)
				{
					Task.Row.Description = NSLOCTEXT("Tutorial", "AutoBasketOnExit", "空手走出厨房，自动带上采集篮");
					Task.Row.HintText = Task.Row.Description;
				}
				else if (Row->TaskType == ETutorialTaskType::StoreToWarehouse)
				{
					Task.Row.Description = Row->TargetItemID == 2
						? NSLOCTEXT("Tutorial", "ReturnEggBasket", "带鸡蛋回厨房，自动存入冰箱")
						: NSLOCTEXT("Tutorial", "ReturnTomatoBasket", "带番茄回厨房，自动存入冰箱");
					Task.Row.HintText = Task.Row.Description;
				}
			}
			ActiveTasks.Add(Task);
		}
	}


	if (bNewSave) RestoreTutorialState();
	if (Day == 2)
	{
		for (int32 ID : {0, 2})
		{
			FActiveTask Supply;
			Supply.Row.Day = 2;
			Supply.Row.StepOrder = ID == 0 ? 106 : 107;
			Supply.Row.TaskType = ETutorialTaskType::StockSupplies;
			Supply.Row.TargetItemID = ID;
			Supply.Row.TargetCount = 2;
			Supply.Row.Description = FText::FromString(ID == 0 ? TEXT("确认冰箱有足够番茄") : TEXT("确认冰箱有足够鸡蛋"));
			ActiveTasks.Add(Supply);
		}
	}
	ActiveTasks.Sort([](const FActiveTask& A, const FActiveTask& B)
	{
		// Keep the existing day-two save keys for harvest and service.
		const auto SortKey = [](int32 Step) { return Step >= 101 && Step <= 107 ? 50 + Step - 100 : Step * 10; };
		return SortKey(A.Row.StepOrder) < SortKey(B.Row.StepOrder);
	});
	if (Day == 2)
		DayExtraNotes.Add(2, NSLOCTEXT("Tutorial", "Day2ResearchAndPots", "鸡蛋和番茄可以研究新菜。番茄炒鸡蛋用煎锅，番茄汤用汤锅。"));
	if (Day == 2)
	{
		// 已研究过的存档不应再要求向研究炉投入同一配方。
		const FActiveTask* Research = ActiveTasks.FindByPredicate([](const FActiveTask& Task)
			{ return Task.Row.TaskType == ETutorialTaskType::ResearchRecipe; });
		if (Research && GI && GI->IsRecipeUnlocked(Research->Row.TargetItemID))
			for (FActiveTask& Task : ActiveTasks)
				if (Task.Row.StepOrder >= 101 && Task.Row.StepOrder <= 105)
				{
					Task.bDone = true;
					Task.Progress = Task.Row.TargetCount;
				}
	}

	if (Day == 2 && GI && GI->curSaveGameData)
		if (const FTutorialDaySave* Saved = GI->curSaveGameData->TutorialDays.Find(Day))
			for (FActiveTask& Task : ActiveTasks)
			{
				Task.Progress = FMath::Clamp(FMath::Max(Task.Progress, Saved->Steps.FindRef(Task.Row.StepOrder)), 0, Task.Row.TargetCount);
				Task.bDone = Task.Progress >= Task.Row.TargetCount;
			}
	if (Day == 1)
	{
		// 订单数及旧布尔标记都不能证明操作教学做过，必须有逐步完成记录。
		// 未做完首单就读档时，场景中的食材/锅已重置，重新引导这一整份料理。
		const TArray<int32>* Completed = GI && GI->curSaveGameData
			? &GI->curSaveGameData->FirstDayTutorialCompletedSteps : nullptr;
		const FActiveTask* FirstServe = ActiveTasks.FindByPredicate([](const FActiveTask& Task)
			{ return Task.Row.TaskType == ETutorialTaskType::ServeGuests; });
		if (Completed && FirstServe && Completed->Contains(FirstServe->Row.StepOrder))
			for (FActiveTask& Task : ActiveTasks)
			{
				if (!Completed->Contains(GetFirstDaySaveKey(Task.Row))) break;
				Task.bDone = true;
				Task.Progress = Task.Row.TargetCount;
			}
	}

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
		TrashBaseline = Hygiene->GetTotalCleanedCount();
		if (RequiredTrash > 0)
		{
			Hygiene->SetAutoSpawnEnabled(false);
			Hygiene->ClearAllMesses();
			bHygieneAutoSpawnOverridden = true;
			for (int32 Index = 0; Index < RequiredTrash; ++Index)
			{
				Hygiene->SpawnMessInArea(EDiningMessType::Trash);
			}
		}
		else if (NoMessesDays.Contains(Day))
		{
		// 前三天及额外配置的无垃圾日：暂停自动撒点并清空现有散落物
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

int32 ATutorialTaskManager::CountItemInBaskets(int32 ItemID, const AOutdoorBasket* OnlyBasket) const
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
		if (!IsValid(*It) || (OnlyBasket && *It != OnlyBasket))
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

	const FActiveTask* FirstServe = ActiveTasks.FindByPredicate([](const FActiveTask& Task)
		{ return Task.Row.TaskType == ETutorialTaskType::ServeGuests; });
	for (FActiveTask& Task : ActiveTasks)
	{
		if (Task.bDone)
		{
			continue;
		}

		switch (Task.Row.TaskType)
		{
		case ETutorialTaskType::EquipBasket:
			for (TActorIterator<AShootCharacter> It(GetWorld()); It; ++It)
				if (It->GetEquippedBasket()) Task.Progress = Task.Row.TargetCount;
			break;
		case ETutorialTaskType::OpenBusiness:
			if (const UTimeSystem* TS = GetTimeSystem())
				if (TS->IsBusinessOpen()) Task.Progress = Task.Row.TargetCount;
			break;
		case ETutorialTaskType::ResearchRecipe:
			if (const UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance()))
				if (GI->IsRecipeUnlocked(Task.Row.TargetItemID)) Task.Progress = Task.Row.TargetCount;
			break;
		case ETutorialTaskType::CleanTrash:
			if (Hygiene)
			{
				Task.Progress = FMath::Max(0, Hygiene->GetTotalCleanedCount() - TrashBaseline);
			}
			break;
		case ETutorialTaskType::ServeGuests:
			if (LevelBP)
			{
				if (AppliedDay == 1 && LevelBP->orderSystem && LevelBP->orderSystem->GetFirstDayServiceTarget() > 0)
				{
					// 首单必须在轮到上菜这一步后真正交付；历史总单数不能完成这一步。
					if (&Task != FirstServe) Task.Progress = LevelBP->orderSystem->GetFirstDayOrdersServed();
				}
				else if (AppliedDay == 2 || AppliedDay == 3) Task.Progress = GetServedToday();
				else Task.Progress = FMath::Max(0, LevelBP->GuestsServedThisLevel - ServeBaseline);
			}
			break;
		case ETutorialTaskType::EarnMoney:
			if (LevelBP)
			{
				const UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance());
				Task.Progress = AppliedDay == 3 && GI && GI->businessSimulation
					? GI->businessSimulation->TodaySales : FMath::Max(0, LevelBP->MoneyEarnedThisLevel - MoneyBaseline);
			}
			break;
		case ETutorialTaskType::PickupItem:
			for (TActorIterator<AShootCharacter> It(GetWorld()); It; ++It)
				if (const AItemActor* Held = It->GetPickingItem())
					if (!Cast<AOutdoorBasket>(Held) && Held->GetItemID() == Task.Row.TargetItemID) Task.Progress = 1;
			break;
		case ETutorialTaskType::CollectDish:
			for (TActorIterator<AShootCharacter> It(GetWorld()); It; ++It)
			{
				const AItemActor* Held = It->GetPickingItem();
				if (Held && Held->GetMenuFoodID() >= 0
					&& (Task.Row.TargetItemID < 0 || Held->GetMenuFoodID() == Task.Row.TargetItemID))
					Task.Progress = Task.Row.TargetCount;
			}
			break;
		case ETutorialTaskType::HarvestQuest:
			if (!Task.Row.bRequireBasket) Task.Progress += NewHandPicks.FindRef(Task.Row.TargetItemID);
			break;
		case ETutorialTaskType::StoreToWarehouse:
			// Successful transfers/harvests report their actual quantities through NotifyAction.
			break;
		case ETutorialTaskType::ReturnPlate:
			EnsureFirstDayPracticePlate();
			break;
		case ETutorialTaskType::ResearchItem:
			Task.Progress = FMath::Max(Task.Progress, CountItemInResearch(Task.Row.TargetItemID, Task.Row.TargetActorTag));
			break;
		case ETutorialTaskType::StockSupplies:
			Task.Progress = FMath::Min(Task.Row.TargetCount, CountReadyStock(Task.Row.TargetItemID)
				+ CountItemAcrossPlayers(Task.Row.TargetItemID));
			break;
		default:
			break;
		}

		Task.Progress = FMath::Clamp(Task.Progress, 0, FMath::Max(1, Task.Row.TargetCount));
		if (Task.Progress >= FMath::Max(1, Task.Row.TargetCount))
		{
			Task.bDone = true;
			SaveCompletedTask(Task);
		}
		// 一次只学习当前一步，后续任务不提前偷计数。
		break;
	}
}

bool ATutorialTaskManager::HasDirtyPlateInHand() const
{
	for (TActorIterator<AShootCharacter> It(GetWorld()); It; ++It)
	{
		const AItemActor* Held = It->GetPickingItem();
		if (IsValid(Held) && Held->bIsDirtyPlate)
		{
			return true;
		}
	}
	return false;
}

void ATutorialTaskManager::EnsureFirstDayPracticePlate()
{
	const UTimeSystem* Time = GetTimeSystem();
	if (!PracticePlateClass || !Time || !Time->IsBusinessOpen() || !IsFirstDayPlateLessonActive()
		|| HasDirtyPlateInHand())
	{
		return;
	}
	UWorld* World = GetWorld();
	for (TActorIterator<AGuestCharacter> It(World); It; ++It)
	{
		if (It->GuestState == EGuestState::Eating && IsValid(It->PlateOnTable))
		{
			return;
		}
	}
	AChairSeat* FreeSeat = nullptr;
	const FVector PlayerLocation = GetLocalPlayerReferenceLocation(World);
	for (TActorIterator<AChairSeat> It(World); It; ++It)
	{
		if (IsValid(It->DirtyPlate))
		{
			return;
		}
		if (!It->bIsOccupied && !It->HasPlateOnSeat()
			&& (!FreeSeat || FVector::DistSquared(It->GetActorLocation(), PlayerLocation)
				< FVector::DistSquared(FreeSeat->GetActorLocation(), PlayerLocation)))
		{
			FreeSeat = *It;
		}
	}
	// The first order is saved, but its guest and plate are not restored on re-entry.
	if (FreeSeat)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (AItemActor* Plate = World->SpawnActor<AItemActor>(PracticePlateClass, FreeSeat->GetPlateSocketTransform(), Params))
		{
			Plate->TryPrepareServingPlate();
			FreeSeat->SetDirtyPlate(Plate);
		}
	}
}

void ATutorialTaskManager::SaveCompletedTask(const FActiveTask& Task)
{
	if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance()))
		if (GI->curSaveGameData)
		{
			if (AppliedDay == 1 && Task.bDone)
				GI->curSaveGameData->FirstDayTutorialCompletedSteps.AddUnique(GetFirstDaySaveKey(Task.Row));
			if (AppliedDay >= 2 && AppliedDay <= 3)
				GI->curSaveGameData->TutorialDays.FindOrAdd(AppliedDay).Steps.Add(Task.Row.StepOrder, Task.Progress);
			bSavePending = true;
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
	case ETutorialTaskType::ReturnPlate:
		if (HasDirtyPlateInHand())
		{
			for (TActorIterator<ADishwasher> It(World); It; ++It) Best = Better(Best, *It);
		}
		else
		{
			for (TActorIterator<AChairSeat> It(World); It; ++It)
				if (IsValid(It->DirtyPlate)) Best = Better(Best, It->DirtyPlate);
		}
		break;
	case ETutorialTaskType::EquipBasket:
		for (TActorIterator<AOutdoorBasket> It(World); It; ++It)
			if (!It->GetAttachParentActor()) Best = Better(Best, *It);
		break;
	case ETutorialTaskType::OpenBusiness:
		for (TActorIterator<AGateCounter> It(World); It; ++It) Best = Better(Best, *It);
		break;
	case ETutorialTaskType::AddToCooker:
	case ETutorialTaskType::CookDish:
	{
		int32 MachineID = Task.Row.TargetMachineID;
		if (MachineID <= 0 && Task.Row.TaskType == ETutorialTaskType::CookDish)
			if (const UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance()))
				if (GI->foodSystem) MachineID = GI->foodSystem->GetMenuData(Task.Row.TargetItemID).MachineID;
		if (MachineID <= 0) MachineID = 1001;
		for (TActorIterator<ACookingMachine> It(World); It; ++It)
		{
			if (!Tag.IsNone() && !It->ActorHasTag(Tag)) continue;
			if (It->MachineID == MachineID) Best = Better(Best, *It);
		}
		break;
	}
	case ETutorialTaskType::CollectDish:
	{
		for (TActorIterator<AItemActor> It(World); It; ++It)
			if (It->GetMenuFoodID() >= 0 && !It->GetAttachParentActor()
				&& (Task.Row.TargetItemID < 0 || It->GetMenuFoodID() == Task.Row.TargetItemID))
				Best = Better(Best, *It);
		break;
	}
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
	case ETutorialTaskType::AcceptOrder:
	{
		// 箭头指向“有客人坐着的餐桌”（椅子），而不是跟着走动的客人一路跑
		for (TActorIterator<AChairSeat> It(World); It; ++It)
		{
			AChairSeat* Seat = *It;
			if (!IsValid(Seat) || !IsValid(Seat->Occupant))
			{
				continue;
			}

			AGuestCharacter* Guest = Seat->Occupant;
			if (Task.Row.TaskType == ETutorialTaskType::AcceptOrder && !Guest->CanAcceptOrderByPlayer()) continue;
			if (Task.Row.TaskType == ETutorialTaskType::ServeGuests)
			{
				if (!Guest->CanPlayerServeDish() || !Guest->CurrentOrder) continue;
				const AShootCharacter* Player = Cast<AShootCharacter>(UGameplayStatics::GetPlayerPawn(World, 0));
				const AItemActor* Held = Player ? Player->GetPickingItem() : nullptr;
				if (Held && Held->GetMenuFoodID() >= 0 && !Guest->CurrentOrder->GetOrderData().FoodIDs.Contains(Held->GetMenuFoodID())) continue;
			}
			Best = Better(Best, Seat);
		}
		// No actionable guest yet: leave the arrow hidden instead of pointing at the spawner.
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
			if (!IsValid(Item) || Item->GetAttachParentActor() || (Cast<AFoodSpawner>(Item) && !Cast<AFoodSpawner>(Item)->CanSpawn()))
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
		// 鸡蛋是鸡舍地上的散落实例，不是植物。
		if (Task.Row.TargetItemID == 2)
			for (TActorIterator<AItemActor> It(World); It; ++It)
				if (It->GetItemID() == 2 && !It->GetAttachParentActor()
					&& (Tag.IsNone() || It->ActorHasTag(Tag))) Best = Better(Best, *It);
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
	case ETutorialTaskType::ResearchRecipe:
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
	if (NeedsOpeningGuidance())
	{
		FText Reason;
		const UTimeSystem* Time = GetTimeSystem();
		const bool bDayThreeOpening = Time && Time->IsFirstRegularKitchenService();
		if (bDayThreeOpening)
		{
			int32 MissingItem = INDEX_NONE;
			if (!GetDayThreeOpeningStatus(Reason, MissingItem))
			{
				FocusHint = Reason;
				if (MissingItem != INDEX_NONE)
				{
					ResolveIngredientRoute(MissingItem, true, 1);
				}
				else if (AActor* Stove = UGameplayStatics::GetActorOfClass(this, AResearchStove::StaticClass()))
				{
					FocusLocation = Stove->GetActorLocation();
					bHasFocusTarget = true;
				}
				return;
			}
		}
		else if (!CanOpenForTutorial(Reason))
		{
			FocusHint = Reason;
			for (int32 ID : {0, 2})
				if (CountAvailableSupply(ID) < GetSupplyRequirement(ID)) { ResolveIngredientRoute(ID, true); break; }
			return;
		}
		FocusHint = bDayThreeOpening ? Reason : AppliedDay == 1
			? NSLOCTEXT("Tutorial", "FirstDayOpeningHint", "跟随箭头走到入口大门，靠近后按 E 开始试营业。今天完成 3 单即可，不限时。")
			: NSLOCTEXT("Tutorial", "SecondDayOpeningHint", "备料完成！跟随箭头走到入口大门，靠近后按 E 开始营业，再接待今天的客人。");
		const FVector Ref = GetLocalPlayerReferenceLocation(GetWorld());
		AGateCounter* Gate = nullptr;
		for (TActorIterator<AGateCounter> It(GetWorld()); It; ++It)
			if (!Gate || FVector::DistSquared(It->GetActorLocation(), Ref) < FVector::DistSquared(Gate->GetActorLocation(), Ref))
				Gate = *It;
		if (Gate)
		{
			FocusLocation = Gate->GetActorLocation();
			bHasFocusTarget = true;
		}
		return;
	}

	for (const FActiveTask& Task : ActiveTasks)
	{
		if (Task.bDone)
		{
			continue;
		}

		FocusHint = Task.Row.HintText.IsEmpty() ? Task.Row.Description : Task.Row.HintText;
		if (Task.Row.TaskType == ETutorialTaskType::EquipBasket || Task.Row.TaskType == ETutorialTaskType::StoreToWarehouse)
		{
			const AHarvestBasketArea* Area = Cast<AHarvestBasketArea>(UGameplayStatics::GetActorOfClass(this, AHarvestBasketArea::StaticClass()));
			const AShootCharacter* Player = Cast<AShootCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
			if (Area && Player && ((Task.Row.TaskType == ETutorialTaskType::EquipBasket
					&& Area->IsInsideKitchen(Player->GetActorLocation()) && Area->IsBasketAvailableInKitchen())
				|| (Task.Row.TaskType == ETutorialTaskType::StoreToWarehouse
					&& Player->GetEquippedBasket() == Area->Basket && CountItemInBaskets(Task.Row.TargetItemID, Area->Basket) > 0)))
			{
				FocusLocation = Area->GetEntranceGuideLocation();
				bHasFocusTarget = true;
				return;
			}
		}
		if (Task.Row.TaskType == ETutorialTaskType::ReturnPlate)
		{
			if (AActor* Target = FindTargetActor(Task))
			{
				FocusHint = HasDirtyPlateInHand()
					? NSLOCTEXT("Tutorial", "ReturnFirstPlate", "拿着空碟走到洗碗机旁，按 E 放回。")
					: NSLOCTEXT("Tutorial", "CollectFirstPlate", "空手走到餐桌旁，按 E 拿起空碟。");
				FocusLocation = Target->GetActorLocation();
				bHasFocusTarget = true;
			}
			else
			{
				FocusHint = NSLOCTEXT("Tutorial", "WaitForFirstPlate", "等客人吃完，桌上会留下空碟。");
			}
			return;
		}
		if (Task.Row.TaskType == ETutorialTaskType::StoreToWarehouse && CountItemInBaskets(Task.Row.TargetItemID) == 0
			&& CountItemAcrossPlayers(Task.Row.TargetItemID) == 0)
		{
			ResolveIngredientRoute(Task.Row.TargetItemID, true);
			return;
		}
		if (Task.Row.TaskType == ETutorialTaskType::HarvestQuest && Task.Row.bRequireBasket)
		{
			const AShootCharacter* Player = Cast<AShootCharacter>(UGameplayStatics::GetPlayerPawn(GetWorld(), 0));
			if (!Player || !Player->GetEquippedBasket() || Player->GetEquippedBasket()->IsBasketFull())
			{
				ResolveIngredientRoute(Task.Row.TargetItemID, true);
				return;
			}
		}
		if (Task.Row.TaskType == ETutorialTaskType::StockSupplies)
		{
			const int32 Missing = FMath::Max(0, Task.Row.TargetCount - Task.Progress);
			ResolveIngredientRoute(Task.Row.TargetItemID, true, Missing);
			return;
		}
		if (Task.Row.TaskType == ETutorialTaskType::OpenBusiness)
		{
			FText Reason;
			if (!CanOpenForTutorial(Reason))
			{
				FocusHint = Reason;
				for (int32 ID : {0, 2})
					if (CountAvailableSupply(ID) < GetSupplyRequirement(ID)) { ResolveIngredientRoute(ID, true); break; }
				return;
			}
		}
		if (Task.Row.TaskType == ETutorialTaskType::PickupItem || Task.Row.TaskType == ETutorialTaskType::ResearchItem
			|| Task.Row.TaskType == ETutorialTaskType::AddToCooker)
		{
			const AShootCharacter* Player = Cast<AShootCharacter>(UGameplayStatics::GetPlayerPawn(GetWorld(), 0));
			const AItemActor* Held = Player ? Player->GetPickingItem() : nullptr;
			if (!Held || Held->GetItemID() != Task.Row.TargetItemID || Cast<AOutdoorBasket>(Held))
			{
				ResolveIngredientRoute(Task.Row.TargetItemID, false);
				return;
			}
		}
		if (!Task.Row.bShowNavigation)
		{
			FocusHint = Task.Row.HintText.IsEmpty() ? Task.Row.Description : Task.Row.HintText;
			return;
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
		return;
	}
}

TArray<FTutorialChecklistEntry> ATutorialTaskManager::GetChecklistEntries() const
{
	TArray<FTutorialChecklistEntry> Entries;
	if (NeedsOpeningGuidance() && AppliedDay != 3)
	{
		if (AppliedDay == 2)
			for (const FActiveTask& Task : ActiveTasks)
			{
				if (!Task.bDone) break;
				FTutorialChecklistEntry& Completed = Entries.AddDefaulted_GetRef();
				Completed.Text = FText::Format(NSLOCTEXT("Tutorial", "ObjectiveProgress", "{0}  {1}/{2}"),
					Task.Row.Description, FText::AsNumber(Task.Progress), FText::AsNumber(FMath::Max(1, Task.Row.TargetCount)));
				Completed.bCompleted = true;
			}
		FTutorialChecklistEntry& Entry = Entries.AddDefaulted_GetRef();
		Entry.Text = AppliedDay == 1
			? NSLOCTEXT("Tutorial", "OpeningObjective", "走到入口大门，按 E 开始试营业")
			: NSLOCTEXT("Tutorial", "SecondDayOpeningObjective", "走到入口大门，按 E 开始营业");
		return Entries;
	}
	for (const FActiveTask& Task : ActiveTasks)
	{
		FTutorialChecklistEntry& Entry = Entries.AddDefaulted_GetRef();
		Entry.Text = FormatObjectiveProgress(Task);
		Entry.bCompleted = Task.bDone;
		if (!Task.bDone) break;
	}

	if (const FText* Note = DayExtraNotes.Find(AppliedDay))
	{
		if (!Note->IsEmpty())
		{
			FTutorialChecklistEntry& Entry = Entries.AddDefaulted_GetRef();
			Entry.Text = *Note;
			Entry.bNote = true;
		}
	}
	return Entries;
}

FText ATutorialTaskManager::GetDayTitle() const
{
	if (AppliedDay == 3) return NeedsOpeningGuidance()
		? NSLOCTEXT("Tutorial", "Day3OpeningCard", "第 3 天 · 开班准备")
		: NSLOCTEXT("Tutorial", "Day3FirstRegularService", "第 3 天 · 正式营业");
	if (NeedsOpeningGuidance()) return FText::Format(
		NSLOCTEXT("Tutorial", "PreparingToOpenTitle", "第 {0} 天 · 准备开门"), FText::AsNumber(AppliedDay));
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

	// 每日任务面板整合在常驻 WBP_HUD 内：HUD 只有一份，分屏也不会按玩家复制。
	if (const ABaseLevelBP* Level = GetLevelBP())
	{
		if (UHUDWidget* HUD = Level->GetHUDWidget())
		{
			HUD->EnsureTutorialGuide(const_cast<ATutorialTaskManager*>(this));
			return;
		}
	}

	// HUD 尚未创建（例如自动化 UI 夹具）：只为 0 号本地玩家保留一份兜底实例。
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
	{
		if (PC->IsLocalController())
		{
			UTutorialGuideWidget::EnsureForPlayer(PC, const_cast<ATutorialTaskManager*>(this));
		}
	}
}
