
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
#include "Game/SeasonSystem.h"
#include "GameFramework/SaveGame.h"
#include "MySaveGame.h"
#include "Data/DiscoveryInfo.h"
#include "Data/ItemActorInfo.h"
#include "Data/RoleInfo.h"
#include "AI/ShootAIPartnerController.h"
#include "BaseGameInstance.generated.h"

class AItemActor;
class AStorageWarehouse;
class APlayerController;
class UCharacterSelectWidget;

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

	/** 直接运行 Kitchen_New 时使用独立的第一天进度，不读写正式经营存档。 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Shoot|Gameplay")
	bool bUseTemporaryKitchenProgress = false;

	// ========== 选角（登录 → 选角色 → 进入主关卡） ==========
	/** 角色配置表（RowStruct = FRoleInfo）。未配置时自动加载 /Game/_Demo/Data/DT_CharacterRoles。
	 *  新增可选角色 = 往表里加一行，无需改界面代码。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Role",
		meta = (RequiredAssetDataTags = "RowStructure=/Script/Shoot.RoleInfo"))
	TObjectPtr<UDataTable> RoleInfoTable = nullptr;

	/** 进入哪个地图时弹出选角界面（默认主关卡 Kitchen_New） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Role")
	FName CharacterSelectMapName = TEXT("Kitchen_New");

	/** 是否启用选角流程（关闭后直接沿用存档/默认外观进关卡） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Role")
	bool bEnableCharacterSelect = true;

	/** 当前选择的角色（DT_CharacterRoles 的 ModelID）；0 表示尚未选择 */
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Role")
	int32 SelectedRoleID = 0;

	/** 本会话是否已经选过角色（加载菜单地图时重置，用于避免在关卡间往返时反复弹窗） */
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Role")
	bool bRoleSelectedThisSession = false;

	/** 按 ModelID 查角色配置；找不到返回 false */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Role")
	bool FindRoleInfo(int32 RoleID, FRoleInfo& OutInfo) const;

	/** DT_CharacterRoles 中所有可选角色的 ModelID（按 ModelID 升序） */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Role")
	TArray<int32> GetSelectableRoleIDs() const;

	/** 取 AfterRoleID 之后的下一可选角色（AI 搭档用来避免和玩家撞外形）；没有则回退第一个 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Role")
	int32 GetNextSelectableRoleID(int32 AfterRoleID) const;

	/** 打开选角界面（为本地玩家 0 创建；已打开时忽略） */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Role")
	void ShowCharacterSelect();

	/** 确认角色：写入存档 + 应用外观到场上所有本地玩家 + 关闭界面并恢复输入 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Role")
	void ConfirmRoleSelection(int32 RoleID);

	/** 把当前选中角色的外观应用到所有本地玩家的 Pawn（选角确认 / 手动刷新用） */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Role")
	void ApplyRoleToLocalPlayers();

	/** 当前打开中的选角界面（无则 nullptr） */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Shoot|Role")
	TObjectPtr<UCharacterSelectWidget> CharacterSelectWidget;

	/** 本地联机（分屏）：确保本地玩家数达到 DesiredCount。
	 *  新增玩家立即获得 pawn（按 PlayerStart 顺序轮流分配出生点，避免叠在一起）。
	 *  可在主菜单「双人开始」里于进关卡前调用，也可在关卡内随时补人。返回最后创建的玩家控制器（无新建时返回 nullptr）。 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Coop")
	APlayerController* EnsureLocalPlayers(int32 DesiredCount);

	// ========== AI 搭档（单人测试的"第二个玩家"） ==========
	/** 生成 AI 搭档：一个由 AShootAIPartnerController 操控的 ShootCharacter pawn（独立于本地玩家，不占分屏）。
	 *  重复调用返回已存在的搭档。搭档计入双人数值缩放，用来单人验证双人平衡。 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Coop")
	class AShootAIPartnerController* SpawnAIPartner();

	/** 在指定位置生成 AI 搭档并设置初始职责与驻守点（关卡里的 AAIPartnerAnchor 用）。
	 *  huanglily：驻守点必须是导航可达的地面点，否则 Forager/Helper 无事可做时会一直卡在寻路失败重试。 */
	class AShootAIPartnerController* SpawnAIPartnerAt(const FTransform& SpawnTransform, const FVector& IdlePost, EAIPartnerMode InitialMode);

	/** 切换 AI 搭档行为模式（0=跟随 1=外勤采集 2=帮厨上菜）；没有搭档时先自动创建 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Coop")
	void SetAIPartnerMode(EAIPartnerMode NewMode);

	/** 移除 AI 搭档（pawn 一并销毁） */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Coop")
	void RemoveAIPartner();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Coop")
	bool HasAIPartner() const { return AIPartner != nullptr; }

	/** AI 搭档生成后是否自动开启上下/左右分屏，一半显示玩家、一半显示搭档视角。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Coop|AI Partner")
	bool bSplitScreenWithAIPartner = true;

	/** 联机人数：本地玩家数 + AI 搭档（1 人 + 搭档 = 2，按双人压力缩放） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Coop")
	int32 GetCoopActorCount() const { return GetNumLocalPlayers() + (AIPartner ? 1 : 0); }

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Shoot|Coop")
	TObjectPtr<class AShootAIPartnerController> AIPartner;

protected:
	/** 用来渲染 AI 搭档视角的第二个本地玩家（不输入、不吃 pawn，只盯搭档） */
	TObjectPtr<class ULocalPlayer> PartnerViewerLocalPlayer = nullptr;
	TObjectPtr<class APlayerController> PartnerViewerPC = nullptr;

	/** 创建第二个本地玩家并把视角绑定到 AI 搭档 pawn；搭档移除时自动清理。 */
	void EnablePartnerSplitScreen();
	void DisablePartnerSplitScreen();

public:

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
	bool bUseDirtyPlateFlow = true;

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

	/** 生成箱低库存提醒阈值：某类食材可用数量低于该值时，HUD 提醒玩家外出采摘补充 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Gameplay|Spawner", meta = (ClampMin = "0"))
	int32 SpawnerLowStockThreshold = 3;

	/** 经营模拟类（DefaultGame.ini 的 [/Script/Shoot.BaseGameInstance] 段可配），留空则用原生 UBusinessSimulation。
	 *  在 Init 时直接创建正确类，避免进关后再替换实例（替换后旧时钟仍会推进，且已绑定旧时钟的灯饰接不到开门事件）。 */
	UPROPERTY(Config)
	FSoftClassPath BusinessSimulationClassPath;

	/** 开罗式经营模拟核心（时钟 / 员工 / 日-月-年结算 / 经营星级） */
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Gameplay")
	TObjectPtr<UBusinessSimulation> businessSimulation;

	/** 赛季系统（48 天三幕 / 日评级 / 胜负 / 幕检查点） */
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Gameplay|Season")
	TObjectPtr<USeasonSystem> seasonSystem;

	/** 赛季配置数据表（RowStruct = FSeasonConfigRow）。可为空（用 C++ 默认值） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Season")
	TObjectPtr<UDataTable> SeasonConfigTable = nullptr;

	/** 赛季配置表行名 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Season")
	FName SeasonConfigRowName = TEXT("Default");

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

	/** 野外植物运行时剩余秒数快照（含已卸载区域）；0 表示可采。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Shoot|Plant")
	TMap<FString, float> WildPlantRegrowRemaining;

	/** 同一运行会话内扣除快照后的时间，保证卸载区域也继续恢复。 */
	float GetWildPlantRegrowRemaining(const FString& Key) const;
	void SetWildPlantRegrowRemaining(const FString& Key, float Remaining);

	/** 入包中的首次发现等可能请求保存，延至采摘提交/失败回滚后才执行。 */
	void BeginWildHarvestTransaction();
	void EndWildHarvestTransaction();

	/** 当前已解锁的食材 ID 集合（运行时） */
	TSet<int32> UnlockedIngredientIDs;

	UFUNCTION(BlueprintCallable, Category = "Shoot|Gameplay")
	FORCEINLINE UEconomySystem* GetEconomySystem() const { return economySystem; }


	UFUNCTION(BlueprintCallable, Category = "Shoot|Gameplay")
	FORCEINLINE bool GetMultiPlayerState() const { return IsMultiPlayerState; }

	UFUNCTION(BlueprintCallable, Category = "Shoot|Gameplay")
	FORCEINLINE void SetMultiPlayerState(bool state){IsMultiPlayerState = state;}
private:
	// 仅实际运行过开始菜单的会话允许延续正式经营进度。
	bool bEnteredFromStartMenu = false;
	// 单调时钟仅用于本次进程，不写入存档。
	TMap<FString, double> WildPlantSnapshotTimes;
	int32 ActiveWildHarvestTransactions = 0;
	bool bWildHarvestSavePending = false;
	void RefreshWildPlantRegrowSnapshots();
	void RestoreCurrentWorldWildPlants();

	// ========== 选角（等待本地玩家控制器就绪后再弹界面） ==========
	/** 世界切换回调（PIE 复制世界不会触发 PostLoadMapWithWorld，须走此入口）：
	 *  进入主关卡时弹出选角；回到登录 / 菜单地图时重置本会话标记 */
	virtual void OnWorldChanged(UWorld* OldWorld, UWorld* NewWorld) override;
	void TryShowCharacterSelect();
	FTimerHandle CharacterSelectTimerHandle;
	int32 CharacterSelectAttempts = 0;

	bool IsMultiPlayerState = false;
	
};


