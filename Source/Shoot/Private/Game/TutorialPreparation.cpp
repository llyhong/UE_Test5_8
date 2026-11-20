#include "Game/TutorialTaskManager.h"
#include "Game/BaseGameInstance.h"
#include "Game/BusinessTime.h"
#include "Game/OrderSystem.h"
#include "Game/FoodSystem.h"
#include "Item/ItemActor.h"
#include "Item/FoodSpawner.h"
#include "Item/OutdoorBasket.h"
#include "Item/Plant.h"
#include "Machine/StorageWarehouse.h"
#include "Machine/ResearchStove.h"
#include "Machine/CookingMachine.h"
#include "Machine/GateCounter.h"
#include "MySaveGame.h"
#include "ShootCharacter.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"

int32 ATutorialTaskManager::GetServedToday() const
{
	const UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance());
	const FTutorialDaySave* Day = GI && GI->curSaveGameData ? GI->curSaveGameData->TutorialDays.Find(AppliedDay) : nullptr;
	return Day ? Day->Served : 0;
}

int32 ATutorialTaskManager::GetPhase(const FActiveTask& Task) const
{
	if (Task.Row.TaskType == ETutorialTaskType::StockSupplies || Task.Row.TaskType == ETutorialTaskType::EquipBasket
		|| Task.Row.TaskType == ETutorialTaskType::HarvestQuest || Task.Row.TaskType == ETutorialTaskType::StoreToWarehouse) return 0;
	if (AppliedDay == 3 && Task.Row.StepOrder < 6) return 1;
	if ((AppliedDay == 2 && Task.Row.StepOrder >= 8 && Task.Row.StepOrder <= 11)
		|| (AppliedDay == 1 && Task.Row.TaskType != ETutorialTaskType::ServeGuests)) return 1;
	return 2;
}

int32 ATutorialTaskManager::GetSupplyRequirement(int32 ItemID) const
{
	// Ingredients for the remaining fixed orders, plus research not yet invested.
	const TArray<int32> Sequence = AppliedDay == 2 ? TArray<int32>{1002, 1001, 1002, 1001}
		: TArray<int32>{1003, 1001, 1002, 1003, 1001};
	const UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance());
	int32 Required = 0;
	if (!GI || !GI->foodSystem) return 0;
	for (int32 Index = FMath::Clamp(GetServedToday(), 0, Sequence.Num()); Index < Sequence.Num(); ++Index)
		Required += GI->foodSystem->GetMenuData(Sequence[Index]).MenuItems.FindRef(ItemID);
	if (AppliedDay == 3 && !GI->IsRecipeUnlocked(1003))
		Required += FMath::Max(0, 1 - CountItemInResearch(ItemID, NAME_None));
	return Required;
}

int32 ATutorialTaskManager::CountReadyStock(int32 ItemID) const
{
	const UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance());
	if (GI && GI->bLinkSpawnerToWarehouse) return CountItemInStorages(ItemID, NAME_None);
	// Independent boxes spend their own stock. Historical warehouse deposits
	// cannot prove those boxes still contain the same ingredients.
	int32 Total = 0;
	for (TActorIterator<AFoodSpawner> It(GetWorld()); It; ++It)
		if (It->SpawnItemActorClass && It->SpawnItemActorClass->GetDefaultObject<AItemActor>()->GetItemID() == ItemID)
		{
			const int32 Stock = It->GetEffectiveStock();
			if (Stock < 0) return MAX_int32 / 4;
			Total += FMath::Max(0, Stock);
		}
	return Total;
}

int32 ATutorialTaskManager::CountAvailableSupply(int32 ItemID) const
{
	int32 Total = CountReadyStock(ItemID) + CountItemAcrossPlayers(ItemID);
	const UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance());
	if (!GI || !GI->foodSystem) return Total;
	for (TActorIterator<ACookingMachine> It(GetWorld()); It; ++It)
		if (!It->OnFire) Total += It->FoodMaterial.FindRef(ItemID);
	// Cooked dishes already in the scene/hand also cover their ingredients.
	for (TActorIterator<AItemActor> It(GetWorld()); It; ++It)
		if (It->GetMenuFoodID() >= 0 && !It->bIsDirtyPlate && !It->GetAttachParentActor())
			Total += GI->foodSystem->GetMenuData(It->GetMenuFoodID()).MenuItems.FindRef(ItemID);
	for (TActorIterator<AShootCharacter> It(GetWorld()); It; ++It)
		if (const AItemActor* Held = It->GetPickingItem())
			if (Held->GetMenuFoodID() >= 0) Total += GI->foodSystem->GetMenuData(Held->GetMenuFoodID()).MenuItems.FindRef(ItemID);
	return Total;
}

bool ATutorialTaskManager::CanOpenForTutorial(FText& OutReason)
{
	OutReason = FText::GetEmpty();
	if (!bEnable) return true;
	if (AppliedDay != GetCurrentDay() || AppliedSave.Get() != (Cast<UBaseGameInstance>(GetGameInstance())
		? Cast<UBaseGameInstance>(GetGameInstance())->curSaveGameData.Get() : nullptr)) ApplyDay(GetCurrentDay());
	if (AppliedDay < 2 || AppliedDay > 3 || ActiveTasks.IsEmpty()) return true;
	UpdateProgress();
	const UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance());
	const FTutorialDaySave* Saved = GI && GI->curSaveGameData ? GI->curSaveGameData->TutorialDays.Find(AppliedDay) : nullptr;
	// Closing and reopening must not re-impose already completed preparation.
	if (!Saved || !Saved->bOpened)
		for (const FActiveTask& Task : ActiveTasks)
			if (Task.Row.TaskType == ETutorialTaskType::OpenBusiness) break;
			else if (!Task.bDone)
			{
				OutReason = FText::Format(NSLOCTEXT("Tutorial", "FinishPreparation", "先完成：{0}。准备完成后再到大门按 E 营业。"), Task.Row.Description);
				return false;
			}
	for (int32 ID : {0, 2})
	{
		const int32 Missing = GetSupplyRequirement(ID) - CountAvailableSupply(ID);
		if (Missing > 0)
		{
			OutReason = FText::FromString(FString::Printf(TEXT("剩余教学订单还缺 %d 个%s。先采集并存入仓库/冰箱，再回来开门。"), Missing, ID == 0 ? TEXT("番茄") : TEXT("鸡蛋")));
			return false;
		}
	}
	return true;
}

AActor* ATutorialTaskManager::FindIngredientSource(int32 ItemID, bool bHarvestOnly) const
{
	const APawn* Player = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	const FVector Ref = Player ? Player->GetActorLocation() : FVector::ZeroVector;
	AActor* Best = nullptr;
	auto Consider = [&](AActor* Actor)
	{
		if (!Best || FVector::DistSquared(Ref, Actor->GetActorLocation()) < FVector::DistSquared(Ref, Best->GetActorLocation())) Best = Actor;
	};
	if (!bHarvestOnly)
		for (TActorIterator<AFoodSpawner> It(GetWorld()); It; ++It)
			if (It->SpawnItemActorClass && It->SpawnItemActorClass->GetDefaultObject<AItemActor>()->GetItemID() == ItemID && It->CanSpawn()) Consider(*It);
	if (Best) return Best;
	for (TActorIterator<AItemActor> It(GetWorld()); It; ++It)
		if (!Cast<AFoodSpawner>(*It) && It->GetItemID() == ItemID && !It->GetAttachParentActor() && It->GetMenuFoodID() < 0) Consider(*It);
	for (TActorIterator<APlant> It(GetWorld()); It; ++It)
		if (It->GetFruitItemID() == ItemID && It->CanHarvest()) Consider(*It);
	return Best;
}

bool ATutorialTaskManager::ResolveIngredientRoute(int32 ItemID, bool bMustStore, int32 RemainingToStore)
{
	AShootCharacter* Player = Cast<AShootCharacter>(UGameplayStatics::GetPlayerPawn(GetWorld(), 0));
	if (!Player) return false;
	const TCHAR* Name = ItemID == 0 ? TEXT("番茄") : TEXT("鸡蛋");
	auto Point = [&](AActor* Actor)
	{
		if (Actor) { FocusLocation = Actor->GetActorLocation(); bHasFocusTarget = true; }
		return Actor != nullptr;
	};
	auto Warehouse = [&]() -> AActor*
	{
		AStorageWarehouse* Best = nullptr;
		for (TActorIterator<AStorageWarehouse> It(GetWorld()); It; ++It)
			if (!Best || Player->GetDistanceTo(*It) < Player->GetDistanceTo(Best)) Best = *It;
		return Best;
	};
	const AOutdoorBasket* Equipped = Player->GetEquippedBasket();
	const int32 InBaskets = CountItemInBaskets(ItemID);
	AActor* Source = nullptr;
	if (Equipped && !Equipped->IsBasketFull() && InBaskets < RemainingToStore
		&& (InBaskets == 0 || CountItemInBaskets(ItemID, Equipped) == InBaskets))
	{
		Source = FindIngredientSource(ItemID, true);
	}
	if (!Source && (InBaskets > 0 || (Equipped && Equipped->IsBasketFull())))
	{
		if (!Equipped)
			for (TActorIterator<AOutdoorBasket> It(GetWorld()); It; ++It)
				if (CountItemInBaskets(ItemID, *It) > 0) { FocusHint = FText::FromString(TEXT("食材在篮子里，先拿起这个篮子，再到仓库/冰箱按 E 存入。")); return Point(*It); }
		FocusHint = RemainingToStore > 0 && InBaskets >= RemainingToStore
			? NSLOCTEXT("Tutorial", "StoreCollectedSupplies", "食材已采够，到仓库/冰箱按 E 存入。")
			: NSLOCTEXT("Tutorial", "EmptyBasketBeforeGathering", "先到仓库/冰箱按 E 存入篮里的食材，再继续采集。");
		return Point(Warehouse());
	}
	bool bInBackpack = false;
	for (const FInventorySlot& Slot : Player->GetInventoryItems())
		if (Slot.ItemClass && Slot.ItemClass->GetDefaultObject<AItemActor>()->GetItemID() == ItemID && Slot.Quantity > 0) bInBackpack = true;
	if (bInBackpack)
	{
		FocusHint = FText::FromString(bMustStore
			? FString::Printf(TEXT("%s已在背包中。到仓库/冰箱按 E 打开，再存入食材。"), Name)
			: FString::Printf(TEXT("%s已在背包中，按 B 打开背包，选中它取到手上，再继续当前操作。"), Name));
		return bMustStore ? Point(Warehouse()) : true;
	}
	if (bMustStore)
		if (const AItemActor* Held = Player->GetPickingItem())
			if (!Cast<AOutdoorBasket>(Held) && Held->GetItemID() == ItemID)
			{
				FocusHint = FText::FromString(TEXT("先按 B 将手上的食材收进背包，再到仓库/冰箱存入。"));
				return Point(Warehouse());
			}
	if (!Source) Source = FindIngredientSource(ItemID, bMustStore);
	if (!bMustStore && Cast<AFoodSpawner>(Source))
	{
		FocusHint = FText::FromString(FString::Printf(TEXT("到食材箱按 E 取一个%s到手上，再按当前任务提示操作。"), Name));
		return Point(Source);
	}
	// Restocking requires a basket; never point at a depleted ingredient box.
	if (!Equipped)
	{
		for (TActorIterator<AOutdoorBasket> It(GetWorld()); It; ++It)
			if (!It->GetAttachParentActor())
			{
				FocusHint = FText::FromString(FString::Printf(TEXT("%s库存不足。先拿篮子，再去%s采集，最后存入仓库/冰箱。"), Name, ItemID == 0 ? TEXT("田地") : TEXT("鸡舍")));
				return Point(*It);
			}
	}
	if (Source)
	{
		FocusHint = FText::FromString(FString::Printf(TEXT("拿着篮子去%s按 E %s；采够后到仓库/冰箱存入，再返回完成任务。"), ItemID == 0 ? TEXT("田地") : TEXT("鸡舍"), ItemID == 0 ? TEXT("摘番茄") : TEXT("捡鸡蛋")));
		return Point(Source);
	}
	FocusHint = FText::FromString(FString::Printf(TEXT("暂时没有可采集的%s，等待%s恢复后再采集；篮子采集后可在营业中补入仓库。"), Name, ItemID == 0 ? TEXT("田地") : TEXT("鸡舍")));
	return false;
}

FText ATutorialTaskManager::FormatObjectiveProgress(const FActiveTask& Task) const
{
	const int32 Offset = FMath::Max(0, Task.Row.DisplayProgressOffset);
	const int32 Target = FMath::Max(1, Task.Row.TargetCount - Offset);
	const FText Description = Task.Row.Description.IsEmpty()
		? (Task.Row.HintText.IsEmpty() ? NSLOCTEXT("Tutorial", "DefaultObjective", "任务") : Task.Row.HintText)
		: Task.Row.Description;
	const bool bPendingStock = Task.Row.TaskType == ETutorialTaskType::StockSupplies && !Task.bDone;
	const int32 InBaskets = bPendingStock ? CountItemInBaskets(Task.Row.TargetItemID) : 0;
	const int32 Collected = FMath::Clamp(Task.Progress + InBaskets - Offset, 0, Target);
	const FText Progress = FText::Format(NSLOCTEXT("Tutorial", "ObjectiveProgress", "{0}  {1}/{2}"),
		Description, FText::AsNumber(Collected), FText::AsNumber(Target));
	if (InBaskets > 0 && Collected == Target)
	{
		return FText::Format(NSLOCTEXT("Tutorial", "SuppliesPendingStorage", "{0} · 待入库"), Progress);
	}
	return Progress;
}

TArray<FTutorialChecklistEntry> ATutorialTaskManager::GetVisibleChecklistEntries() const
{
	TArray<FTutorialChecklistEntry> Entries;
	int32 CurrentPhase = 2;
	for (const FActiveTask& Task : ActiveTasks) if (!Task.bDone) { CurrentPhase = GetPhase(Task); break; }
	const TCHAR* Names[] = {TEXT("备料"), TEXT("学习新技能"), TEXT("营业")};
	FTutorialChecklistEntry& Phase = Entries.AddDefaulted_GetRef();
	Phase.bNote = true;
	Phase.Text = FText::FromString(FString::Printf(TEXT("备料 → 学习新技能 → 营业\n当前：%s"), Names[CurrentPhase]));
	int32 Completed = 0;
	for (const FActiveTask& Task : ActiveTasks) if (Task.bDone) ++Completed;
	if (Completed > 0)
	{
		FTutorialChecklistEntry& History = Entries.AddDefaulted_GetRef();
		History.bNote = true;
		History.Text = FText::FromString(FString::Printf(TEXT("已完成 %d 项 · 历史步骤已收起"), Completed));
	}
	for (const FActiveTask& Task : ActiveTasks)
	{
		if (Task.bDone) continue;
		FTutorialChecklistEntry& Current = Entries.AddDefaulted_GetRef();
		if (NeedsOpeningGuidance())
		{
			Current.Text = NSLOCTEXT("Tutorial", "ReopenObjective", "走到入口大门，按 E 开始营业");
			break;
		}
		Current.Text = FormatObjectiveProgress(Task);
		break;
	}
	if (Entries.Num() == (Completed > 0 ? 2 : 1))
	{
		FTutorialChecklistEntry& Done = Entries.AddDefaulted_GetRef();
		Done.bCompleted = true;
		Done.Text = NSLOCTEXT("Tutorial", "DayComplete", "今日引导完成，自由经营吧！");
	}
	return Entries;
}
