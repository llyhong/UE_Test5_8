// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/DataTable.h"
#include "Data/PlantInfo.h"
#include "Data/QualityTypes.h"
#include "Plant.generated.h"

class UStaticMeshComponent;
class USphereComponent;
class AShootCharacter;
class UBaseGameInstance;

/** 阶段变化时广播（供 UI / 蓝图刷新），参数为新阶段索引与阶段名 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPlantStageChanged, int32, StageIndex, FName, StageName);

/**
 * 植物类：支持浇水、驱虫、按配置的生长阶段生长，成熟后在挂点生成果实。
 * 所有可调参数均来自 DataTable（FPlantInfo），通过 PlantInfoRow 引用。
 */
UCLASS(Blueprintable)
class SHOOT_API APlant : public AActor
{
	GENERATED_BODY()

public:
	APlant();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	/** 植物配置数据表行引用（DataTable: PlantInfo） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Plant", meta = (RowType = "PlantInfo"))
	FDataTableRowHandle PlantInfoRow;

	// ========== 对外功能（可在蓝图 / 角色交互中调用） ==========

	/** 浇水：增加水分 */
	UFUNCTION(BlueprintCallable, Category = "Plant")
	void Water(float Amount = 20.f);

	/** 驱虫：清除当前害虫状态 */
	UFUNCTION(BlueprintCallable, Category = "Plant")
	void RemovePest();

	/** 收获：在挂点生成果实，返回是否成功（需处于结果阶段且尚未收获过） */
	UFUNCTION(BlueprintCallable, Category = "Plant")
	bool Harvest();

	// ========== 按 E 采摘：直接入背包并让植被消失 ==========
	/** 按 E 采摘后是否直接把果实放入玩家背包（true=直接入背包并销毁植被；
	 *  false=旧逻辑，果实挂到玩家手上并支持自动重新结果）。默认开启新行为。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Plant|Harvest")
	bool bHarvestToInventory = true;

	/** 采摘后直接入背包的果实数量；<=0 表示使用 DataTable 的 FruitCount。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Plant|Harvest")
	int32 HarvestYield = -1;

	// ========== 解锁（食材未解锁时禁止采摘） ==========
	/** 食材解锁 ID：>=0 表示需要对应 ID 解锁后才能采摘果实（如玉米田里的玉米）；<0 表示无需解锁，默认可摘。
	 *  解锁由 UBaseGameInstance 统一管理（关卡达成目标后 UnlockIngredients）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Plant|Unlock")
	int32 IngredientUnlockID = -1;

	/** 运行时是否已解锁（<0 的 ID 永远为 true） */
	UPROPERTY(BlueprintReadOnly, Category = "Plant|Unlock")
	bool bIngredientUnlocked = true;

	/** 解锁状态变化通知（true=已解锁可采摘；false=已锁定）。蓝图可做锁定/解锁视觉与提示 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Plant|Unlock")
	void OnPlantUnlockStateChanged(bool bUnlocked);

	/** 尝试采摘却被锁定（未解锁）时通知，供 UI 飘字“需达成目标才能采摘” */
	UFUNCTION(BlueprintImplementableEvent, Category = "Plant|Unlock")
	void OnHarvestLocked();

	/** 设置解锁状态并广播 OnPlantUnlockStateChanged */
	UFUNCTION(BlueprintCallable, Category = "Plant|Unlock")
	void SetIngredientUnlocked(bool bUnlocked);

	/** 监听 GameInstance 的食材解锁广播 */
	UFUNCTION()
	void HandleIngredientsUnlocked(const TArray<int32>& UnlockedIDs);

	/** 根据当前状态刷新“靠近可采摘”描边提示（仅当前目标植被且可收获时显示） */
	void UpdateHarvestHintOutline();

	// ========== 查询（供 UI / 蓝图） ==========

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Plant")
	int32 GetCurrentStageIndex() const { return CurrentStageIndex; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Plant")
	FName GetCurrentStageName() const;

	/** 当前阶段进度 0..1 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Plant")
	float GetGrowthProgress() const;

	/** 是否已到结果阶段（可收获） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Plant")
	bool IsFruiting() const;

	/** 当前采摘品质（依据结果阶段内的熟度计时）：成熟窗口=佳品，熟过头=次品，未成熟/未启用=普通。
	 *  供采摘时写入果实，并最终影响成品品质与上菜奖励。 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Plant")
	EIngredientQuality GetCurrentQuality() const;

	/** 熟度进度 0..1（距“熟过头”的进度），供 UI 进度条显示（未启用熟度时返回 0） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Plant")
	float GetRipenessProgress() const;

	/** 当前熟度子阶段名（未成熟 / 成熟 / 熟过头），用于 UI 提示当前素材阶段 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Plant")
	FName GetRipenessStageName() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Plant")
	float GetWater() const { return WaterLevel; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Plant")
	bool HasPest() const { return bPest; }

	/** 阶段变化通知（蓝图可重载） */
	UFUNCTION(BlueprintImplementableEvent, Category = "Plant")
	void OnStageChanged(int32 NewStageIndex, FName StageName);

	/** 生虫通知（蓝图可重载，例如播放虫特效） */
	UFUNCTION(BlueprintImplementableEvent, Category = "Plant")
	void OnPestAppeared();

	/** 可收获通知（蓝图可重载） */
	UFUNCTION(BlueprintImplementableEvent, Category = "Plant")
	void OnReadyToHarvest();

	/** 阶段变化广播（供 Widget 绑定） */
	UPROPERTY(BlueprintAssignable, Category = "Plant")
	FOnPlantStageChanged OnPlantStageChanged;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UStaticMeshComponent* PlantMesh;

	/** 交互范围：角色进入后注册为可交互植物 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	USphereComponent* InteractionSphere;

	/** 调试开关：在屏幕上打印植物状态（可在蓝图/实例上设置） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bDebug = false;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION()
	void OnInteractionOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnInteractionEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	/** 取配置行（无效时返回 nullptr） */
	const FPlantInfo* GetPlantInfo() const;

	void ApplyStageMesh(int32 StageIndex);
	void AdvanceStage();
	void SpawnFruit();

	/** 在指定挂点索引处生出一个果实（供一次性结满与逐个重结果共用） */
	void SpawnSingleFruit(int32 Index);

	/** 自动重新结果：每隔 RefruitInterval 秒长出一个果实，直到达到 FruitCount */
	void RefruitOneFruit();

	/** 调试：将当前状态打印到屏幕（由 Tick 节流调用） */
	void PrintDebugInfo();

	float DebugPrintTimer = 0.f;

	// ========== 运行时状态 ==========
	UPROPERTY()
	int32 CurrentStageIndex = 0;

	float StageTimer = 0.f;
	float WaterLevel = 0.f;
	bool bPest = false;
	float PestTimer = 0.f;
	bool bHarvested = false;

	/** 当前长在树上的果实（进入结果阶段时生成，收获时收集）。熟度状态已下沉到每个果实自身，
	 *  由植物在生成果实时写入配置并启动计时，果实独立推进与广播。 */
	UPROPERTY()
	TArray<TObjectPtr<AItemActor>> SpawnedFruits;

	/** 正在交互的角色（重叠时设置，用于收获时把果实放入其背包） */
	UPROPERTY()
	TObjectPtr<AShootCharacter> InteractingCharacter;

	/** 当前采摘描边是否已开启（缓存，避免每帧重复调用 SetActorsOutline） */
	bool bHintOutlineActive = false;

	/** 自动重新结果定时器（逐个生长时用） */
	FTimerHandle RefruitTimerHandle;
};
