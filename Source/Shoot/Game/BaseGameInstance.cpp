// Fill out your copyright notice in the Description page of Project Settings.


#include "Game/BaseGameInstance.h"

#include "MySaveGame.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "NPC/GuestCharacter.h"
#include "Machine/StorageWarehouse.h"

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
	businessSimulation = NewObject<UBusinessSimulation>(this);
	businessSimulation->Init(economySystem);

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
	SaveData->DiscoveredItemKeys.Empty();
	curSaveGameData = SaveData;
	UnlockedIngredientIDs.Reset();
	DiscoveredItemKeys.Reset();
	SaveGameData();

	// 新游戏：将经营模拟（日期 / 员工）重置回初始（存档默认 BizYear=1、BizRoster 空）
	if (businessSimulation)
	{
		businessSimulation->Load();
	}
}
void UBaseGameInstance::SaveGameData()
{

	if (curSaveGameData != nullptr)
	{
		// 保存到指定插槽（SlotName为文件名，UserIndex通常为0）
		UGameplayStatics::SaveGameToSlot(curSaveGameData, TEXT("SaveSlot_01"), 0);
	}
}

bool UBaseGameInstance::LoadGameData() {
	// 从插槽加载存档
	UMySaveGame* LoadedData = Cast<UMySaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("SaveSlot_01"), 0));
	if (!LoadedData) return false;
	curSaveGameData = LoadedData;
	if (economySystem)
	{
		economySystem->Load();
	}
	if (businessSimulation)
	{
		businessSimulation->Load();
	}
	// 同步已解锁食材集合（跨存档持久化）
	UnlockedIngredientIDs.Reset();
	if (curSaveGameData)
	{
		for (int32 ID : curSaveGameData->UnlockedIngredients)
		{
			UnlockedIngredientIDs.Add(ID);
		}

		// 同步已发现物品（首次发现只触发一次，跨存档保留）
		DiscoveredItemKeys.Reset();
		for (const FName& Key : curSaveGameData->DiscoveredItemKeys)
		{
			DiscoveredItemKeys.AddUnique(Key);
		}
	}
	return true;
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
	return UnlockedRecipeIDs.Contains(FoodID);
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