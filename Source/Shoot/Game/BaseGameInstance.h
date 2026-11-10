
#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Game/DataClass.h"
#include "FoodSystem.h"
#include "Game/BaseLevelBP.h"
#include "Game/LevelGoalData.h"
#include "OrderSystem.h"
#include "PlateSystem.h"
#include "Game/EconomySystem.h"
#include "Game/BusinessSimulation.h"
#include "GameFramework/SaveGame.h"
#include "MySaveGame.h"
#include "Data/DiscoveryInfo.h"
#include "Data/ItemActorInfo.h"
#include "BaseGameInstance.generated.h"

class AItemActor;
class AStorageWarehouse;

/**
 * 
 */



/** 食材解锁广播：参数 = 本次新解锁的食材 ID 列表（对应 AItemActor::IngredientUnlockID） */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnIngredientsUnlocked, const TArray<int32>&, UnlockedIDs);

/** 菜谱解锁广播：参数 = 本次新解锁的菜品 FoodID 列表（由研究炉研究成功后解锁） */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRecipesUnlocked, const TArray<int32>&, UnlockedFoodIDs);

/** 空碟玩法开关变化（true=留空碟，false=不留） */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDirtyPlateFlowChanged, bool, bEnabled);

UCLASS()
class SHOOT_API UBaseGameInstance : public UGameInstance
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Gameplay")
	FPlayerInfo playerInfo;
	void Init() override;
	FPlayerInfo CreateDefaultPlayerInfo();
	UFUNCTION(BlueprintCallable, Category = "Shoot|Gameplay")
	void NewGameData();
	UFUNCTION(BlueprintCallable, Category = "Shoot|Gameplay")
	void SaveGameData();
	UFUNCTION(BlueprintCallable, Category = "Shoot|Gameplay")
	bool LoadGameData();
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Gameplay")
	TObjectPtr<UMySaveGame> curSaveGameData;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Gameplay")
	TObjectPtr<UDataTable> MenueDataTable;

	/** 物品信息表（RowStruct = FItemActorInfo，如 DT_ItemInfo）：食材图标 / 描述按 ItemID 从这里取。
	 *  未配置时菜品成品图标仍可用（菜品表 MenuIcon），但食材图标取不到。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Gameplay")
	TObjectPtr<UDataTable> ItemInfoTable = nullptr;
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Gameplay")
	TObjectPtr<UFoodSystem> foodSystem;
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Gameplay")
	TObjectPtr<ABaseLevelBP> curLevelBP;

	/** 经济/声誉系统（声誉、金钱、购买/升级） */
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Gameplay")
	TObjectPtr<UEconomySystem> economySystem;

	/** 空碟玩法总开关（一键切换 A/B 对比）：
	 *   true  = 客人吃完后留下空碟，玩家需按 E 收走（原方案B）；
	 *   false = 客人吃完直接离座、桌面立即清空，无需收碟环节。
	 *  客人落座时会读取此值；运行时调用 SetDirtyPlateFlow 可立即影响在场客人。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Gameplay|Plate")
	bool bUseDirtyPlateFlow = false;

	/** 空碟玩法开关变化广播（参数 = 新值），蓝图可据此刷新提示 */
	UPROPERTY(BlueprintAssignable, Category = "Shoot|Gameplay|Plate")
	FOnDirtyPlateFlowChanged OnDirtyPlateFlowChanged;

	/** 运行时切换空碟玩法（通知所有在场客人立即应用新值） */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Gameplay|Plate")
	void SetDirtyPlateFlow(bool bEnable);

	/** 打烊（未营业）期间“拾取物品”的行为路由开关：
	 *   true  = 打烊时拾取物品自动收进背包（玩家手上不残留物品）；营业时仍正常拿在手上（默认）；
	 *   false = 不论营业与否，拾取都拿在手上（原行为，不做打烊特判）。
	 *   用于对比“打烊后拾取是直接入包、还是拿在手上”的玩法差异。运行时可在蓝图 Class Defaults 调整。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Gameplay|Inventory")
	bool bAutoStoreToBackpackWhileClosed = true;

	/** 食材生成箱是否由仓库库存驱动：
	 *   false = 食材箱各自独立，可无限(-1)/有限(>0)生成，与仓库无关（默认，原行为）；
	 *   true  = 食材箱改为从仓库库存取料：箱内“剩余数量”= 仓库中该食材的存量，取一件仓库减一件，仓库为空则箱不出料。
	 *   用于对比“食材箱独立供料”与“中央仓库统一供料”两种玩法。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Gameplay|Spawner")
	bool bLinkSpawnerToWarehouse = false;

	/** 仓库驱动模式下：所有仓库中某物品类的总存量（无仓库返回 0）。由 AFoodSpawner 取料时查询。 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Gameplay|Spawner")
	int32 GetWarehouseStock(TSubclassOf<AItemActor> ItemClass) const;

	/** 仓库驱动模式下：从仓库消耗一件某物品类（库存 -1）。成功返回 true（所有仓库都缺货则 false）。 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Gameplay|Spawner")
	bool ConsumeWarehouseItem(TSubclassOf<AItemActor> ItemClass);

	/** 开罗式经营模拟核心（时钟 / 员工 / 日-月-年结算 / 经营星级） */
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Gameplay")
	TObjectPtr<UBusinessSimulation> businessSimulation;

	/** 经济配置数据表（RowStruct = FEconomyConfigRow）。可为空（用下方内置默认） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Economy")
	TObjectPtr<UDataTable> EconomyConfigTable = nullptr;

	/** 经济配置表行名 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Economy")
	FName EconomyConfigRowName = TEXT("Default");

	/** 经济内置默认配置（未配置 EconomyConfigTable 时使用） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Economy")
	FEconomyConfigRow EconomyDefaultConfig;

	/** 商店商品数据表（RowStruct = FEconomyShopItem）。可为空（用下方内置默认） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Economy|Shop")
	TObjectPtr<UDataTable> ShopItemsTable = nullptr;

	/** 商店内置默认商品列表（未配置 ShopItemsTable 时使用） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Economy|Shop")
	TArray<FEconomyShopItem> EconomyDefaultShopItems;

	/** 关卡目标（通过条件）数据表（RowStruct = FLevelGoalRow）。为空则关卡无通过条件 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Level")
	TObjectPtr<UDataTable> LevelGoalTable = nullptr;

	/** 食材解锁广播：达成关卡目标后解锁场景上预先放置的食材（如玉米田里的玉米） */
	UPROPERTY(BlueprintAssignable, Category = "Shoot|Ingredient")
	FOnIngredientsUnlocked OnIngredientsUnlocked;

	/** 食材 ID 是否已解锁（ID<0 视为无需解锁，永远返回 true） */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Ingredient")
	bool IsIngredientUnlocked(int32 IngredientID) const;

	/** 解锁一批食材 ID，并广播 OnIngredientsUnlocked（已解锁的集合不变时不会重复广播/存盘） */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Ingredient")
	void UnlockIngredients(const TArray<int32>& IngredientIDs);

	// ========== 菜谱（菜品）解锁：由研究炉研究成功后解锁 ==========
	/** 已解锁的菜谱（菜品 FoodID 集合） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shoot|Recipe")
	TArray<int32> UnlockedRecipeIDs;

	/** 菜谱解锁广播（研究炉研究成功时触发） */
	UPROPERTY(BlueprintAssignable, Category = "Shoot|Recipe")
	FOnRecipesUnlocked OnRecipesUnlocked;

	/** 菜品 FoodID 是否已解锁（ID<0 视为无需解锁，永远返回 true） */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Recipe")
	bool IsRecipeUnlocked(int32 FoodID) const;

	/** 解锁一批菜谱（菜品 FoodID），并广播 OnRecipesUnlocked（全部已解锁时不广播） */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Recipe")
	void UnlockRecipes(const TArray<int32>& FoodIDs);

	// ========== 首次发现（食材图鉴雏形） ==========
	/** 可选的发现配置表（RowStruct = FDiscoveryInfo），行名与物品信息表行名一致（如 "7"）。
	 *  未配置的品类回退到物品信息表的名称/图标 + 下方默认奖励。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Discovery")
	TObjectPtr<UDataTable> DiscoveryTable = nullptr;

	/** 未在 DiscoveryTable 配置时，首次发现给的金币奖励 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Discovery", meta = (ClampMin = "0"))
	int32 DefaultDiscoveryMoney = 5;

	/** 未在 DiscoveryTable 配置时，首次发现给的声誉奖励 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Discovery", meta = (ClampMin = "0"))
	int32 DefaultDiscoveryReputation = 1;

	/** 已发现的物品键集合（运行时），与存档 MySaveGame::DiscoveredItemKeys 同步 */
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Discovery")
	TArray<FName> DiscoveredItemKeys;

	/** 首次发现广播（用于图鉴 UI / 成就） */
	UPROPERTY(BlueprintAssignable, Category = "Shoot|Discovery")
	FOnItemDiscovered OnItemDiscovered;

	/** 该物品是否已被发现过 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Discovery")
	bool IsItemDiscovered(FName ItemKey) const;

	/** 标记首次发现：已发现返回 false；首次发现时写存档、发奖励并广播。
	 *  FallbackInfo 用于未配置 DiscoveryTable 时提供名称/图标。 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Discovery")
	bool MarkItemDiscovered(FName ItemKey, const FItemActorInfo& FallbackInfo, FDiscoveryInfo& OutInfo);

	/** 读取某食物箱已存档的剩余数量；无记录返回 -1（表示用 Actor 自身默认值） */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Spawner")
	int32 GetSpawnerRemaining(const FGuid& SpawnerGuid) const;

	/** 写入某食物箱的剩余数量到存档（不立即落盘，调用方决定是否 SaveGameData） */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Spawner")
	void SetSpawnerRemaining(const FGuid& SpawnerGuid, int32 Value);

	/** 当前已解锁的食材 ID 集合（运行时） */
	TSet<int32> UnlockedIngredientIDs;

	UFUNCTION(BlueprintCallable, Category = "Shoot|Gameplay")
	FORCEINLINE UEconomySystem* GetEconomySystem() const { return economySystem; }


	UFUNCTION(BlueprintCallable, Category = "Shoot|Gameplay")
	FORCEINLINE bool GetMultiPlayerState() const { return IsMultiPlayerState; }

	UFUNCTION(BlueprintCallable, Category = "Shoot|Gameplay")
	FORCEINLINE void SetMultiPlayerState(bool state){IsMultiPlayerState = state;}
private:
	bool IsMultiPlayerState = false;
	
};


