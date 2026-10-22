
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
#include "BaseGameInstance.generated.h"

/**
 * 
 */



/** 食材解锁广播：参数 = 本次新解锁的食材 ID 列表（对应 AItemActor::IngredientUnlockID） */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnIngredientsUnlocked, const TArray<int32>&, UnlockedIDs);

UCLASS()
class SHOOT_API UBaseGameInstance : public UGameInstance
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gameplay")
	FPlayerInfo playerInfo;
	void Init() override;
	FPlayerInfo CreateDefaultPlayerInfo();
	UFUNCTION(BlueprintCallable, Category = "Gameplay")
	void NewGameData();
	UFUNCTION(BlueprintCallable, Category = "Gameplay")
	void SaveGameData();
	UFUNCTION(BlueprintCallable, Category = "Gameplay")
	bool LoadGameData();
	UPROPERTY(BlueprintReadOnly, Category = "Gameplay")
	TObjectPtr<UMySaveGame> curSaveGameData;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gameplay")
	TObjectPtr<UDataTable> MenueDataTable;
	UPROPERTY(BlueprintReadOnly, Category = "Gameplay")
	TObjectPtr<UFoodSystem> foodSystem;
	UPROPERTY(BlueprintReadOnly, Category = "Gameplay")
	TObjectPtr<ABaseLevelBP> curLevelBP;

	/** 经济/声誉系统（声誉、金钱、购买/升级） */
	UPROPERTY(BlueprintReadOnly, Category = "Gameplay")
	TObjectPtr<UEconomySystem> economySystem;

	/** 开罗式经营模拟核心（时钟 / 员工 / 日-月-年结算 / 经营星级） */
	UPROPERTY(BlueprintReadOnly, Category = "Gameplay")
	TObjectPtr<UBusinessSimulation> businessSimulation;

	/** 经济配置数据表（RowStruct = FEconomyConfigRow）。可为空（用下方内置默认） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	TObjectPtr<UDataTable> EconomyConfigTable = nullptr;

	/** 经济配置表行名 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	FName EconomyConfigRowName = TEXT("Default");

	/** 经济内置默认配置（未配置 EconomyConfigTable 时使用） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	FEconomyConfigRow EconomyDefaultConfig;

	/** 商店商品数据表（RowStruct = FEconomyShopItem）。可为空（用下方内置默认） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy|Shop")
	TObjectPtr<UDataTable> ShopItemsTable = nullptr;

	/** 商店内置默认商品列表（未配置 ShopItemsTable 时使用） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy|Shop")
	TArray<FEconomyShopItem> EconomyDefaultShopItems;

	/** 关卡目标（通过条件）数据表（RowStruct = FLevelGoalRow）。为空则关卡无通过条件 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level")
	TObjectPtr<UDataTable> LevelGoalTable = nullptr;

	/** 食材解锁广播：达成关卡目标后解锁场景上预先放置的食材（如玉米田里的玉米） */
	UPROPERTY(BlueprintAssignable, Category = "Ingredient")
	FOnIngredientsUnlocked OnIngredientsUnlocked;

	/** 食材 ID 是否已解锁（ID<0 视为无需解锁，永远返回 true） */
	UFUNCTION(BlueprintCallable, Category = "Ingredient")
	bool IsIngredientUnlocked(int32 IngredientID) const;

	/** 解锁一批食材 ID，并广播 OnIngredientsUnlocked（已解锁的集合不变时不会重复广播/存盘） */
	UFUNCTION(BlueprintCallable, Category = "Ingredient")
	void UnlockIngredients(const TArray<int32>& IngredientIDs);

	/** 当前已解锁的食材 ID 集合（运行时） */
	TSet<int32> UnlockedIngredientIDs;

	UFUNCTION(BlueprintCallable, Category = "Gameplay")
	FORCEINLINE UEconomySystem* GetEconomySystem() const { return economySystem; }


	UFUNCTION(BlueprintCallable, Category="Gameplay")
	FORCEINLINE bool GetMultiPlayerState() const { return IsMultiPlayerState; }

	UFUNCTION(BlueprintCallable, Category="Gameplay")
	FORCEINLINE void SetMultiPlayerState(bool state){IsMultiPlayerState = state;}
private:
	bool IsMultiPlayerState = false;
	
};


