// Fill out your copyright notice in the Description page of Project Settings.


#include "Game/BaseGameInstance.h"

#include "MySaveGame.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "NPC/GuestCharacter.h"
#include "Machine/StorageWarehouse.h"
#include "Item/Plant.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "GameFramework/PlayerController.h"
#include "UI/RecipeBookWidget.h"

/** 控制台命令：切换空碟玩法开关，方便 A/B 对比两种玩法（运行时输入：Game.ToggleDirtyPlateFlow） */
static FAutoConsoleCommand CmdToggleDirtyPlateFlow(
	TEXT("Game.ToggleDirtyPlateFlow"),
	TEXT("切换空碟玩法：true=客人吃完留空碟（需按E收走）/ false=吃完直接离座桌面清空。用于对比两种玩法。"),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		UBaseGameInstance* GI = nullptr;
		if (GEngine)
		{
			for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
			{
				UWorld* W = Ctx.World();
				if (W && (Ctx.WorldType == EWorldType::PIE || Ctx.WorldType == EWorldType::Game))
				{
					GI = Cast<UBaseGameInstance>(W->GetGameInstance());
					if (GI)
					{
						break;
					}
				}
			}
		}

		if (GI)
		{
			GI->SetDirtyPlateFlow(!GI->bUseDirtyPlateFlow);
			UE_LOG(LogTemp, Log, TEXT("[Plate] 空碟玩法已切换为：%s"),
				GI->bUseDirtyPlateFlow ? TEXT("留空碟（需按E收走）") : TEXT("不留空碟（吃完即清桌）"));
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[Plate] 未找到运行中的 GameInstance，无法切换空碟玩法"));
		}
	}));


/** 控制台命令：开关菜谱图鉴（没有研究炉的地图也能查看；运行时输入：Game.RecipeBook） */
static FAutoConsoleCommand CmdToggleRecipeBook(
	TEXT("Game.RecipeBook"),
	TEXT("打开/关闭菜谱图鉴。用于测试，或在未摆放研究炉的地图里查看已解锁/未解锁菜谱。"),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		if (!GEngine)
		{
			return;
		}
		for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
		{
			UWorld* W = Ctx.World();
			if (W && (Ctx.WorldType == EWorldType::PIE || Ctx.WorldType == EWorldType::Game))
			{
				for (FConstPlayerControllerIterator It = W->GetPlayerControllerIterator(); It; ++It)
				{
					if (APlayerController* PC = It->Get())
					{
						if (PC->IsLocalController())
						{
							URecipeBookWidget::ToggleForPlayer(PC);
						}
					}
				}
				break;
			}
		}
	}));


void UBaseGameInstance::Init()
{
	foodSystem = NewObject<UFoodSystem>(this);
	foodSystem->InitMenuData(MenueDataTable);
	foodSystem->InitItemInfoTable(ItemInfoTable);
	economySystem = NewObject<UEconomySystem>(this);
	economySystem->ConfigTable = EconomyConfigTable;
	economySystem->ConfigRowName = EconomyConfigRowName;
	economySystem->DefaultConfig = EconomyDefaultConfig;
	economySystem->ShopItemsTable = ShopItemsTable;
	// 若未配置默认商品，则填充几个示例道具，方便开箱即用（编辑器里可覆盖）
	if (EconomyDefaultShopItems.Num() == 0)
	{
		auto MakeItem = [](int32 ID, const TCHAR* Name, int32 Cost, const TCHAR* Desc) -> FEconomyShopItem
		{
			FEconomyShopItem It;
			It.ItemID = ID;
			It.DisplayName = FText::FromString(Name);
			It.Cost = Cost;
			It.Description = FText::FromString(Desc);
			return It;
		};
		EconomyDefaultShopItems.Add(MakeItem(1001, TEXT("新餐桌"), 50, TEXT("提升店铺可容纳顾客数（示例道具）")));
		EconomyDefaultShopItems.Add(MakeItem(1002, TEXT("高级灶台"), 80, TEXT("加快烹饪速度（示例道具）")));
		EconomyDefaultShopItems.Add(MakeItem(1003, TEXT("霓虹招牌"), 30, TEXT("提升招揽顾客数量（示例道具）")));
	}
	economySystem->DefaultShopItems = EconomyDefaultShopItems;
	economySystem->Init();

	// 经营模拟核心：依赖经济系统，负责 时钟 / 员工 / 日-月-年结算 / 经营星级
	// 直接按配置类创建，避免关卡 BeginPlay 再替换实例、留下旧时钟继续并行推进
	UClass* SimClass = UBusinessSimulation::StaticClass();
	if (!BusinessSimulationClassPath.IsNull())
	{
		if (UClass* LoadedClass = BusinessSimulationClassPath.TryLoadClass<UBusinessSimulation>())
		{
			SimClass = LoadedClass;
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[GameInstance] BusinessSimulationClassPath 无法加载：%s，回退原生 UBusinessSimulation"),
				*BusinessSimulationClassPath.ToString());
		}
	}
	businessSimulation = NewObject<UBusinessSimulation>(this, SimClass);
	businessSimulation->Init(economySystem);

	// 赛季：日评级 / 胜负 / 幕检查点，日结算由 BusinessSimulation 驱动
	seasonSystem = NewObject<USeasonSystem>(this);
	seasonSystem->ConfigTable = SeasonConfigTable;
	seasonSystem->ConfigRowName = SeasonConfigRowName;
	seasonSystem->Init(economySystem, businessSimulation);
	businessSimulation->SeasonSystem = seasonSystem;
	businessSimulation->RefreshStars();

	playerInfo = CreateDefaultPlayerInfo();
	Super::Init();
	UE_LOG(LogTemp, Warning, TEXT("GameInstance Initialized ok"));

}

FPlayerInfo UBaseGameInstance::CreateDefaultPlayerInfo()
{
	FPlayerInfo o;
	o.Name = TEXT("LiliHong");
	o.ModelID = 0;
	o.UserID = 0;
	o.Coin = 0;
	return o;
}

void UBaseGameInstance::NewGameData()
{
	// 创建或覆盖存档对象
	UMySaveGame* SaveData = Cast<UMySaveGame>(UGameplayStatics::CreateSaveGameObject(UMySaveGame::StaticClass()));
	SaveData->Money = 0;
	SaveData->Reputation = 0;
	SaveData->UnlockedIngredients.Empty();
	SaveData->UnlockedRecipes.Empty();
	SaveData->DiscoveredItemKeys.Empty();
	curSaveGameData = SaveData;
	UnlockedIngredientIDs.Reset();
	UnlockedRecipeIDs.Reset();
	DiscoveredItemKeys.Reset();
	WildPlantRegrowRemaining.Reset();
	WildPlantSnapshotTimes.Reset();
	RestoreCurrentWorldWildPlants();
	SaveGameData();

	// 新游戏：将经营模拟（日期 / 员工）重置回初始（存档默认 BizYear=1、BizRoster 空）
	if (businessSimulation)
	{
		businessSimulation->Load();
	}
	if (seasonSystem)
	{
		seasonSystem->ResetForNewGame();
	}
}
void UBaseGameInstance::SaveGameData()
{
	if (ActiveWildHarvestTransactions > 0)
	{
		bWildHarvestSavePending = true;
		return;
	}

	if (curSaveGameData != nullptr)
	{
		// 先刷新已卸载区域的剩余时间，再抓取当前世界植株快照；不在每帧写存档。
		RefreshWildPlantRegrowSnapshots();
		if (UWorld* World = GetWorld())
		{
			for (TActorIterator<APlant> It(World); It; ++It)
			{
				if (It->UsesWildHarvest())
				{
					It->SaveWildHarvestState();
				}
			}
		}
		curSaveGameData->WildPlantRegrowRemaining = WildPlantRegrowRemaining;

		// 保存到指定插槽（SlotName为文件名，UserIndex通常为0）
		UGameplayStatics::SaveGameToSlot(curSaveGameData, TEXT("SaveSlot_01"), 0);
	}
}

bool UBaseGameInstance::LoadGameData() {
	// 从插槽加载存档
	UMySaveGame* LoadedData = Cast<UMySaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("SaveSlot_01"), 0));
	if (!LoadedData) return false;
	curSaveGameData = LoadedData;
	WildPlantRegrowRemaining = LoadedData->WildPlantRegrowRemaining;
	WildPlantSnapshotTimes.Reset();
	const double Now = FPlatformTime::Seconds();
	for (TPair<FString, float>& Entry : WildPlantRegrowRemaining)
	{
		Entry.Value = FMath::IsFinite(Entry.Value) ? FMath::Max(0.f, Entry.Value) : 0.f;
		WildPlantSnapshotTimes.Add(Entry.Key, Now);
	}
	if (economySystem)
	{
		economySystem->Load();
	}
	if (businessSimulation)
	{
		businessSimulation->Load();
	}
	if (seasonSystem)
	{
		seasonSystem->Load();
	}
	if (businessSimulation)
	{
		businessSimulation->RefreshStars();
	}
	// 同步已解锁食材集合（跨存档持久化）
	UnlockedIngredientIDs.Reset();
	if (curSaveGameData)
	{
		for (int32 ID : curSaveGameData->UnlockedIngredients)
		{
			UnlockedIngredientIDs.Add(ID);
		}

		// 同步已解锁菜谱（研究炉成果跨存档保留）
		UnlockedRecipeIDs.Reset();
		for (int32 ID : curSaveGameData->UnlockedRecipes)
		{
			UnlockedRecipeIDs.Add(ID);
		}

		// 同步已发现物品（首次发现只触发一次，跨存档保留）
		DiscoveredItemKeys.Reset();
		for (const FName& Key : curSaveGameData->DiscoveredItemKeys)
		{
			DiscoveredItemKeys.AddUnique(Key);
		}
	}
	RestoreCurrentWorldWildPlants();
	return true;
}

float UBaseGameInstance::GetWildPlantRegrowRemaining(const FString& Key) const
{
	const float* Remaining = WildPlantRegrowRemaining.Find(Key);
	if (!Remaining || !FMath::IsFinite(*Remaining) || *Remaining <= 0.f)
	{
		return 0.f;
	}
	const double* SnapshotTime = WildPlantSnapshotTimes.Find(Key);
	const double Elapsed = SnapshotTime ? FMath::Max(0.0, FPlatformTime::Seconds() - *SnapshotTime) : 0.0;
	return static_cast<float>(FMath::Max(0.0, static_cast<double>(*Remaining) - Elapsed));
}

void UBaseGameInstance::SetWildPlantRegrowRemaining(const FString& Key, float Remaining)
{
	WildPlantRegrowRemaining.FindOrAdd(Key) = FMath::IsFinite(Remaining) ? FMath::Max(0.f, Remaining) : 0.f;
	WildPlantSnapshotTimes.FindOrAdd(Key) = FPlatformTime::Seconds();
}

void UBaseGameInstance::BeginWildHarvestTransaction()
{
	++ActiveWildHarvestTransactions;
}

void UBaseGameInstance::EndWildHarvestTransaction()
{
	if (!ensure(ActiveWildHarvestTransactions > 0))
	{
		return;
	}
	--ActiveWildHarvestTransactions;
	if (ActiveWildHarvestTransactions == 0 && bWildHarvestSavePending)
	{
		bWildHarvestSavePending = false;
		SaveGameData(); // 仅兑现事务内已经请求的保存，不新增自动保存触发点。
	}
}

void UBaseGameInstance::RefreshWildPlantRegrowSnapshots()
{
	const double Now = FPlatformTime::Seconds();
	for (TPair<FString, float>& Entry : WildPlantRegrowRemaining)
	{
		Entry.Value = GetWildPlantRegrowRemaining(Entry.Key);
		WildPlantSnapshotTimes.FindOrAdd(Entry.Key) = Now;
	}
}

void UBaseGameInstance::RestoreCurrentWorldWildPlants()
{
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<APlant> It(World); It; ++It)
		{
			if (It->HasActorBegunPlay() && !It->IsActorBeingDestroyed() && It->UsesWildHarvest())
			{
				It->SetIngredientUnlocked(IsIngredientUnlocked(It->IngredientUnlockID));
				It->RestoreWildHarvestState();
			}
		}
	}
}

bool UBaseGameInstance::IsItemDiscovered(FName ItemKey) const
{
	return !ItemKey.IsNone() && DiscoveredItemKeys.Contains(ItemKey);
}

bool UBaseGameInstance::MarkItemDiscovered(FName ItemKey, const FItemActorInfo& FallbackInfo, FDiscoveryInfo& OutInfo)
{
	if (ItemKey.IsNone() || DiscoveredItemKeys.Contains(ItemKey))
	{
		return false;
	}

	const FDiscoveryInfo* ConfigRow = DiscoveryTable
		? DiscoveryTable->FindRow<FDiscoveryInfo>(ItemKey, TEXT("Discovery"), false)
		: nullptr;

	OutInfo = FDiscoveryInfo();
	if (ConfigRow)
	{
		OutInfo = *ConfigRow;
		if (OutInfo.DisplayName.IsEmpty())
		{
			OutInfo.DisplayName = FallbackInfo.ItemDes;
		}
		if (!OutInfo.Icon)
		{
			OutInfo.Icon = FallbackInfo.Icon;
		}
	}
	else
	{
		OutInfo.DisplayName = FallbackInfo.ItemDes.IsEmpty() ? ItemKey.ToString() : FallbackInfo.ItemDes;
		OutInfo.Icon = FallbackInfo.Icon;
		OutInfo.RewardMoney = DefaultDiscoveryMoney;
		OutInfo.RewardReputation = DefaultDiscoveryReputation;
	}
	if (OutInfo.DisplayName.IsEmpty())
	{
		OutInfo.DisplayName = ItemKey.ToString();
	}
	if (OutInfo.Description.IsEmpty())
	{
		OutInfo.Description = FString::Printf(TEXT("在野外发现了新的食材：%s"), *OutInfo.DisplayName);
	}

	DiscoveredItemKeys.Add(ItemKey);
	if (curSaveGameData)
	{
		curSaveGameData->DiscoveredItemKeys.AddUnique(ItemKey);
		SaveGameData();
	}

	if (economySystem)
	{
		if (OutInfo.RewardMoney > 0)
		{
			economySystem->AddMoney(OutInfo.RewardMoney, EMoneySource::None);
		}
		if (OutInfo.RewardReputation > 0)
		{
			economySystem->AddReputation(OutInfo.RewardReputation);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[Discovery] 首次发现 %s（%s），奖励 金币+%d 声誉+%d"),
		*ItemKey.ToString(), *OutInfo.DisplayName, OutInfo.RewardMoney, OutInfo.RewardReputation);

	OnItemDiscovered.Broadcast(OutInfo, ItemKey);
	return true;
}

bool UBaseGameInstance::IsIngredientUnlocked(int32 IngredientID) const
{
	if (IngredientID < 0)
	{
		return true; // ID<0 表示无需解锁，默认可交互
	}
	return UnlockedIngredientIDs.Contains(IngredientID);
}

void UBaseGameInstance::SetDirtyPlateFlow(bool bEnable)
{
	if (bUseDirtyPlateFlow == bEnable)
	{
		return;
	}
	bUseDirtyPlateFlow = bEnable;

	// 立即让在场的客人应用新值（已坐下的也即时切换；进行到一半的空碟流程保持不变）
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AGuestCharacter> It(World); It; ++It)
		{
			It->bUseDirtyPlateFlow = bEnable;
		}

		// 切换碟子模式：不收空碟 → 无限碟子；收空碟 → 有限（自动补满到总数）
		if (curLevelBP && curLevelBP->PlateSystem)
		{
			curLevelBP->PlateSystem->SetInfinite(!bEnable);
		}
	}

	OnDirtyPlateFlowChanged.Broadcast(bEnable);
}

int32 UBaseGameInstance::GetWarehouseStock(TSubclassOf<AItemActor> ItemClass) const
{
	int32 Total = 0;
	if (!ItemClass)
	{
		return 0;
	}
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AStorageWarehouse> It(World); It; ++It)
		{
			Total += It->GetStoredQuantity(ItemClass);
		}
	}
	return Total;
}

bool UBaseGameInstance::ConsumeWarehouseItem(TSubclassOf<AItemActor> ItemClass)
{
	if (!ItemClass)
	{
		return false;
	}
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AStorageWarehouse> It(World); It; ++It)
		{
			if (It->ConsumeOne(ItemClass))
			{
				return true;
			}
		}
	}
	return false;
}

void UBaseGameInstance::UnlockIngredients(const TArray<int32>& IngredientIDs)
{
	bool bAnyNew = false;
	for (int32 ID : IngredientIDs)
	{
		if (ID < 0)
		{
			continue;
		}
		if (!UnlockedIngredientIDs.Contains(ID))
		{
			UnlockedIngredientIDs.Add(ID);
			if (curSaveGameData)
			{
				curSaveGameData->UnlockedIngredients.AddUnique(ID);
			}
			bAnyNew = true;
		}
	}
	if (bAnyNew)
	{
		if (curSaveGameData)
		{
			SaveGameData();
		}
		OnIngredientsUnlocked.Broadcast(IngredientIDs);
	}
}

bool UBaseGameInstance::IsRecipeUnlocked(int32 FoodID) const
{
	if (FoodID < 0)
	{
		return true; // ID<0 表示无需解锁，默认可用
	}
	if (UnlockedRecipeIDs.Contains(FoodID))
	{
		return true;
	}
	// 菜品表里没勾“需要研究”的菜，开局就可用（旧数据缺这一列时也是这个行为）
	return foodSystem ? foodSystem->IsRecipeDefaultUnlocked(FoodID) : true;
}

void UBaseGameInstance::UnlockRecipes(const TArray<int32>& FoodIDs)
{
	TArray<int32> NewlyUnlocked;
	for (int32 FoodID : FoodIDs)
	{
		if (FoodID < 0)
		{
			continue;
		}
		if (!UnlockedRecipeIDs.Contains(FoodID))
		{
			UnlockedRecipeIDs.Add(FoodID);
			NewlyUnlocked.Add(FoodID);
			UE_LOG(LogTemp, Log, TEXT("[GameInstance] 解锁新菜谱：FoodID=%d"), FoodID);
		}
	}

	if (NewlyUnlocked.Num() > 0)
	{
		// 研究成果跨存档保留：否则读档后研究过的菜又变成没解锁
		if (curSaveGameData)
		{
			for (const int32 FoodID : NewlyUnlocked)
			{
				curSaveGameData->UnlockedRecipes.AddUnique(FoodID);
			}
			SaveGameData();
		}
		OnRecipesUnlocked.Broadcast(NewlyUnlocked);
	}
}

int32 UBaseGameInstance::GetSpawnerRemaining(const FGuid& SpawnerGuid) const
{
	if (!curSaveGameData)
	{
		return -1;
	}
	if (const int32* Found = curSaveGameData->SpawnerRemaining.Find(SpawnerGuid.ToString()))
	{
		return *Found;
	}
	return -1;
}

void UBaseGameInstance::SetSpawnerRemaining(const FGuid& SpawnerGuid, int32 Value)
{
	if (!curSaveGameData)
	{
		return;
	}
	curSaveGameData->SpawnerRemaining.FindOrAdd(SpawnerGuid.ToString()) = Value;
}