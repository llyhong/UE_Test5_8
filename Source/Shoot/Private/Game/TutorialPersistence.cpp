#include "Game/TutorialTaskManager.h"
#include "Game/BaseGameInstance.h"
#include "Game/BusinessTime.h"
#include "Game/FoodSystem.h"
#include "Machine/StorageWarehouse.h"
#include "Machine/ResearchStove.h"
#include "Machine/CookingMachine.h"
#include "Item/OutdoorBasket.h"
#include "Item/FoodSpawner.h"
#include "MySaveGame.h"
#include "ShootCharacter.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	FInventorySlot SaveItem(const AItemActor* Item)
	{
		FInventorySlot Slot;
		Slot.ItemClass = Item->GetClass();
		Slot.ItemInfoRow = Item->ItemInfoRow;
		Slot.FoodIdArray = Item->FoodIdArray;
		Slot.Quantity = 1;
		return Slot;
	}
	bool IsRecoverableItem(const AItemActor* Item)
	{
		// Preserve loose teaching ingredients and cooked products, not scenery,
		// plant fruit attached to plants, empty plates, or dishes already served.
		return IsValid(Item) && !Item->IsActorBeingDestroyed() && !Cast<AFoodSpawner>(Item) && !Cast<AOutdoorBasket>(Item)
			&& !Item->bIsDirtyPlate && (Item->GetMenuFoodID() >= 0 || Item->GetItemID() == 0 || Item->GetItemID() == 2);
	}
	FString MapKey(const UWorld* World)
	{
		return UGameplayStatics::GetCurrentLevelName(World, true);
	}
}

void ATutorialTaskManager::CaptureTutorialState()
{
	UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance());
	if (bRestoring || !GI || !GI->curSaveGameData || AppliedSave.Get() != GI->curSaveGameData
		|| AppliedDay < 2 || AppliedDay > 3 || AppliedDay != GetCurrentDay() || ActiveTasks.IsEmpty()) return;
	FTutorialDaySave& Day = GI->curSaveGameData->TutorialDays.FindOrAdd(AppliedDay);
	for (const FActiveTask& Task : ActiveTasks) Day.Steps.Add(Task.Row.StepOrder, Task.Progress);
	if (const UTimeSystem* TS = GetTimeSystem()) Day.bOpened |= TS->IsBusinessOpen();
	FTutorialWorldSave Snapshot;
	Snapshot.Day = AppliedDay;
	Snapshot.Map = MapKey(GetWorld());
	for (TActorIterator<AStorageWarehouse> It(GetWorld()); It; ++It)
	{
		FTutorialContainerSave& Container = Snapshot.Warehouses.AddDefaulted_GetRef();
		Container.Key = It->GetFName(); Container.Items = It->StoredItems;
	}
	for (TActorIterator<AOutdoorBasket> It(GetWorld()); It; ++It)
	{
		FTutorialContainerSave& Container = Snapshot.Baskets.AddDefaulted_GetRef();
		Container.Key = It->GetFName(); Container.Items = It->BasketItems;
	}
	TMap<const AItemActor*, int32> Holders;
	int32 PlayerIndex = 0;
	for (TActorIterator<AShootCharacter> It(GetWorld()); It; ++It, ++PlayerIndex)
	{
		FTutorialContainerSave& Backpack = Snapshot.Backpacks.AddDefaulted_GetRef();
		Backpack.Items = It->GetInventoryItems();
		// The held basket belongs to an existing level actor; don't spawn a second one.
		Backpack.Key = It->GetEquippedBasket() ? It->GetEquippedBasket()->GetFName() : NAME_None;
		if (const AItemActor* Held = It->GetPickingItem()) Holders.Add(Held, PlayerIndex);
	}
	for (TActorIterator<AItemActor> It(GetWorld()); It; ++It)
	{
		if (!IsRecoverableItem(*It) || (It->GetAttachParentActor() && !Holders.Contains(*It))) continue;
		FTutorialItemSave& Item = Snapshot.Items.AddDefaulted_GetRef();
		Item.Item = SaveItem(*It); Item.MenuID = It->GetMenuFoodID(); Item.Transform = It->GetActorTransform();
		Item.Holder = Holders.Contains(*It) ? Holders[*It] : INDEX_NONE;
	}
	for (TActorIterator<AResearchStove> It(GetWorld()); It; ++It)
	{
		FTutorialResearchSave& Research = Snapshot.Research.AddDefaulted_GetRef();
		Research.Key = It->GetFName(); Research.Items = It->StoredItemIDs;
		Research.Recipe = It->ActiveRecipeIndex; Research.Remaining = It->ResearchRemainTime;
		Research.Total = It->ResearchTotalTime; Research.bRunning = It->bResearching;
	}
	for (TActorIterator<ACookingMachine> It(GetWorld()); It; ++It)
	{
		FTutorialCookerSave& Cooker = Snapshot.Cookers.AddDefaulted_GetRef();
		Cooker.Key = It->GetFName(); Cooker.Materials = It->FoodMaterial; Cooker.MenuID = It->TargetMenuData.FoodID;
		Cooker.Time = It->CurCookingTime; Cooker.OverTime = It->OverCookTime;
		Cooker.bCooking = It->IsCooking; Cooker.bFinished = It->IsFinish; Cooker.bFire = It->OnFire;
	}
	GI->curSaveGameData->TutorialWorld = MoveTemp(Snapshot);
}

void ATutorialTaskManager::RestoreTutorialState()
{
	UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance());
	if (!GI || !GI->curSaveGameData) return;
	const FTutorialWorldSave& Snapshot = GI->curSaveGameData->TutorialWorld;
	if (Snapshot.Day != AppliedDay || Snapshot.Map != MapKey(GetWorld()) || Snapshot.Day < 2 || Snapshot.Day > 3) return;
	TGuardValue<bool> Restoring(bRestoring, true);
	TArray<AShootCharacter*> Players;
	for (TActorIterator<AShootCharacter> It(GetWorld()); It; ++It) Players.Add(*It);
	// Restore is called once per loaded save object after BeginPlay. Replacing
	// containers and loose ingredients, rather than adding, makes it idempotent.
	for (AShootCharacter* Player : Players) Player->ClearHeldItemState();
	TArray<AItemActor*> Remove;
	for (TActorIterator<AItemActor> It(GetWorld()); It; ++It)
		if (IsRecoverableItem(*It) && (!It->GetAttachParentActor() || Cast<AShootCharacter>(It->GetAttachParentActor()))) Remove.Add(*It);
	for (AItemActor* Item : Remove) Item->Destroy();
	for (TActorIterator<AStorageWarehouse> It(GetWorld()); It; ++It)
		if (const FTutorialContainerSave* Container = Snapshot.Warehouses.FindByPredicate([&](const FTutorialContainerSave& S) { return S.Key == It->GetFName(); }))
		{
			It->StoredItems = Container->Items; It->OnWarehouseChanged.Broadcast();
		}
	for (TActorIterator<AOutdoorBasket> It(GetWorld()); It; ++It)
		if (const FTutorialContainerSave* Container = Snapshot.Baskets.FindByPredicate([&](const FTutorialContainerSave& S) { return S.Key == It->GetFName(); }))
		{
			It->BasketItems = Container->Items; It->OnBasketChanged.Broadcast();
		}
	for (int32 Index = 0; Index < Players.Num(); ++Index)
		if (Snapshot.Backpacks.IsValidIndex(Index))
		{
			while (!Players[Index]->GetInventoryItems().IsEmpty()) Players[Index]->RemoveItemFromInventory(0);
			for (const FInventorySlot& Slot : Snapshot.Backpacks[Index].Items) Players[Index]->AddSlotToInventory(Slot);
			Players[Index]->OnInventoryChanged.Broadcast();
			if (!Snapshot.Backpacks[Index].Key.IsNone())
				for (TActorIterator<AOutdoorBasket> It(GetWorld()); It; ++It)
					if (It->GetFName() == Snapshot.Backpacks[Index].Key) Players[Index]->HoldItemInHand(*It, true);
		}
	for (const FTutorialItemSave& Saved : Snapshot.Items)
	{
		if (!Saved.Item.ItemClass) continue;
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AItemActor* Item = GetWorld()->SpawnActor<AItemActor>(Saved.Item.ItemClass, Saved.Transform, Params);
		if (!Item) continue;
		Item->ItemInfoRow = Saved.Item.ItemInfoRow; Item->FoodIdArray = Saved.Item.FoodIdArray; Item->SetMenuFoodID(Saved.MenuID);
		if (Players.IsValidIndex(Saved.Holder)) Players[Saved.Holder]->HoldItemInHand(Item, true);
	}
	for (TActorIterator<AResearchStove> It(GetWorld()); It; ++It)
		if (const FTutorialResearchSave* Saved = Snapshot.Research.FindByPredicate([&](const FTutorialResearchSave& S) { return S.Key == It->GetFName(); }))
		{
			It->StoredItemIDs = Saved->Items;
			It->bResearching = Saved->bRunning && It->Recipes.IsValidIndex(Saved->Recipe)
				&& !GI->IsRecipeUnlocked(It->Recipes[Saved->Recipe].UnlockFoodID);
			It->ActiveRecipeIndex = It->bResearching ? Saved->Recipe : INDEX_NONE;
			It->ResearchRemainTime = FMath::Max(.01f, Saved->Remaining);
			It->ResearchTotalTime = FMath::Max(.01f, Saved->Total);
		}
	for (TActorIterator<ACookingMachine> It(GetWorld()); It; ++It)
		if (const FTutorialCookerSave* Saved = Snapshot.Cookers.FindByPredicate([&](const FTutorialCookerSave& S) { return S.Key == It->GetFName(); }))
		{
			It->FoodMaterial = Saved->Materials;
			It->TargetMenuData = GI->foodSystem->GetMenuData(Saved->MenuID);
			It->TargetMenuID = Saved->MenuID; It->TargetMateiralCount = It->TargetMenuData.GetNumOfItems();
			It->CurFoodCount = 0;
			for (const auto& Material : Saved->Materials) It->CurFoodCount += Material.Value;
			It->IsFull = It->CurFoodCount > 0 && It->CurFoodCount == It->TargetMateiralCount;
			It->CurCookingTime = Saved->Time; It->OverCookTime = Saved->OverTime;
			It->IsCooking = Saved->bCooking; It->IsFinish = Saved->bFinished; It->OnFire = Saved->bFire;
			It->GreenCookTime = It->TargetMateiralCount > 0 ? It->CurFoodCount * It->GetRequiredCookTime() / It->TargetMateiralCount : 999.f;
		}
	// Guests are recreated on opening, with the next unserved teaching order.
	// Material, cooking and research state remain in place; the player chooses when to reopen.
	if (UTimeSystem* TS = GetTimeSystem()) TS->CloseBusiness();
}
