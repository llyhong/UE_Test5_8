// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/DataTable.h"
#include "Data/PlantInfo.h"
#include "Data/QualityTypes.h"
#include "Interaction/InteractionConfig.h"
#include "Plant.generated.h"

class UStaticMeshComponent;
class USphereComponent;
class AShootCharacter;
class UBaseGameInstance;
class UMaterialInstanceDynamic;
class UDecalComponent;
class USoundBase;
class UNiagaraSystem;

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

	/** 编辑器里改 InteractionRadius 时立即写进组件，关卡视口中即可看到真实判定球 */
	virtual void OnConstruction(const FTransform& Transform) override;

	/** 把 InteractionRadius（× Shoot.InteractScale 倍率）写进交互组件；OnConstruction/BeginPlay/运行时改倍率都会调用，必须幂等 */
	UFUNCTION()
	void ApplyInteractionScale();

	/** 植物配置数据表行引用（DataTable: PlantInfo） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Plant", meta = (RowType = "PlantInfo"))
	FDataTableRowHandle PlantInfoRow;

	// ========== 对外功能（可在蓝图 / 角色交互中调用） ==========

	/** 浇水：增加水分 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Plant")
	void Water(float Amount = 20.f);

	/** 驱虫：清除当前害虫状态 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Plant")
	void RemovePest();

	/** 收获：在挂点生成果实，返回是否成功（需处于结果阶段且尚未收获过） */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Plant")
	bool Harvest();

	// ========== 按 E 采摘：直接入背包并让植被消失 ==========
	/** 按 E 采摘后是否直接把果实放入玩家背包（true=直接入背包并销毁植被；
	 *  false=旧逻辑，果实挂到玩家手上并支持自动重新结果）。默认开启新行为。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Plant|Harvest")
	bool bHarvestToInventory = true;

	/** 采摘后直接入背包的果实数量；<=0 表示使用 DataTable 的 FruitCount。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Plant|Harvest")
	int32 HarvestYield = -1;

	// ========== 解锁（食材未解锁时禁止采摘） ==========
	/** 食材解锁 ID：>=0 表示需要对应 ID 解锁后才能采摘果实（如玉米田里的玉米）；<0 表示无需解锁，默认可摘。
	 *  解锁由 UBaseGameInstance 统一管理（关卡达成目标后 UnlockIngredients）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Plant|Unlock")
	int32 IngredientUnlockID = -1;

	/** 运行时是否已解锁（<0 的 ID 永远为 true） */
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Plant|Unlock")
	bool bIngredientUnlocked = true;

	/** 解锁状态变化通知（true=已解锁可采摘；false=已锁定）。蓝图可做锁定/解锁视觉与提示 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shoot|Plant|Unlock")
	void OnPlantUnlockStateChanged(bool bUnlocked);

	/** 尝试采摘却被锁定（未解锁）时通知，供 UI 飘字“需达成目标才能采摘” */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shoot|Plant|Unlock")
	void OnHarvestLocked();

	/** 设置解锁状态并广播 OnPlantUnlockStateChanged */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Plant|Unlock")
	void SetIngredientUnlocked(bool bUnlocked);

	/** 监听 GameInstance 的食材解锁广播 */
	UFUNCTION()
	void HandleIngredientsUnlocked(const TArray<int32>& UnlockedIDs);

	/** 根据当前状态刷新“靠近可采摘”描边提示（仅当前目标植被且可收获时显示） */
	void UpdateHarvestHintOutline();

	/** 角色指定"本株是当前唯一交互目标"（描边请求）。
	 *  真正是否点亮还要叠加本株的状态条件（缺水 / 已结果可采），见 UpdateHarvestHintOutline */
	void SetInteractionHighlight(bool bEnable);

	// ========== 查询（供 UI / 蓝图） ==========

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Plant")
	int32 GetCurrentStageIndex() const { return CurrentStageIndex; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Plant")
	FName GetCurrentStageName() const;

	/** 当前阶段进度 0..1 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Plant")
	float GetGrowthProgress() const;

	/** 是否已到结果阶段（可收获） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Plant")
	bool IsFruiting() const;

	/** 当前采摘品质（依据结果阶段内的熟度计时）：成熟窗口=佳品，熟过头=次品，未成熟/未启用=普通。
	 *  供采摘时写入果实，并最终影响成品品质与上菜奖励。 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Plant")
	EIngredientQuality GetCurrentQuality() const;

	/** 熟度进度 0..1（距“熟过头”的进度），供 UI 进度条显示（未启用熟度时返回 0） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Plant")
	float GetRipenessProgress() const;

	/** 当前熟度子阶段名（未成熟 / 成熟 / 熟过头），用于 UI 提示当前素材阶段 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Plant")
	FName GetRipenessStageName() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Plant")
	float GetWater() const { return WaterLevel; }

	/** 是否需要浇水：配置了需要浇水（bRequiresWater）且当前水位已干涸（<=0）。浇满后须等水分消耗回 0 才可再次浇水 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Plant")
	bool NeedsWater() const;

	/** 口渴窗口是否正打开（仅 bUseThirstWindows 植物有效）：窗口内浇水可获得“时机正好”反馈并恢复全速生长 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Plant|Thirst")
	bool IsThirstWindowOpen() const { return bThirstWindowOpen; }

	/** 口渴窗口剩余时间（秒），未打开时为 0 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Plant|Thirst")
	float GetThirstWindowRemaining() const { return bThirstWindowOpen ? ThirstWindowRemaining : 0.f; }

	/** 设置浇水提示抑制：浇水动作完成后调用以取消高亮；植株重新变干或玩家离开范围后自动恢复 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Plant")
	void SetWaterHintSuppressed(bool bSuppress);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Plant")
	bool HasPest() const { return bPest; }

	/** 阶段变化通知（蓝图可重载） */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shoot|Plant")
	void OnStageChanged(int32 NewStageIndex, FName StageName);

	/** 生虫通知（蓝图可重载，例如播放虫特效） */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shoot|Plant")
	void OnPestAppeared();

	/** 可收获通知（蓝图可重载） */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shoot|Plant")
	void OnReadyToHarvest();

	/** 浇水反馈（蓝图可重载：水花、音效、叶片抖动等） */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shoot|Plant|Feedback")
	void OnWatered(float NewWaterLevel);

	/** 驱虫反馈（蓝图可重载） */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shoot|Plant|Feedback")
	void OnPestRemoved();

	/** 口渴窗口打开（蓝图可重载：冒“口渴”气泡等） */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shoot|Plant|Thirst")
	void OnThirstWindowOpened();

	/** 窗口内及时浇水（蓝图可重载） */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shoot|Plant|Thirst")
	void OnThirstWindowSatisfied();

	/** 错过口渴窗口（本阶段将减速生长，蓝图可重载） */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shoot|Plant|Thirst")
	void OnThirstWindowMissed();

	/** 照料连击达成（参数为额外产量），蓝图可重载 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shoot|Plant|Care")
	void OnCareComboCompleted(int32 BonusYield);

	// ========== 浇水表现（可留空；留空时仍有叶片抖动反馈） ==========
	/** 浇水音效（为空则不播） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Plant|Feedback")
	TObjectPtr<USoundBase> WaterSound = nullptr;

	/** 浇水粒子特效（为空则不播；可挂 Niagara 水花） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Plant|Feedback")
	TObjectPtr<UNiagaraSystem> WaterEffect = nullptr;

	/** 浇水特效挂点（留空则用植株模型中心） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Plant|Feedback")
	FName WaterEffectSocket = NAME_None;

	/** 浇水特效相对挂点的偏移（世界空间） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Plant|Feedback")
	FVector WaterEffectOffset = FVector(0.f, 0.f, 30.f);

	/** 阶段变化广播（供 Widget 绑定） */
	UPROPERTY(BlueprintAssignable, Category = "Shoot|Plant")
	FOnPlantStageChanged OnPlantStageChanged;

	/** 湿度参数：0 为干裂，1 为湿润。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shoot|Plant|Soil")
	FName SoilWetnessParameterName = TEXT("SoilWetness");

	/** 材质裂纹随机种子参数名：按植株位置写入，避免相邻田块裂纹一模一样 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shoot|Plant|Soil")
	FName SoilCrackSeedParameterName = TEXT("CrackSeed");

	/** 浇水后从干到湿的渗开时长（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Plant|Soil", meta = (ClampMin = "0.0", Units = "s"))
	float SoilWetTransitionTime = 0.8f;

	/** 水分耗尽 / 采摘重置后从湿回干的时长（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Plant|Soil", meta = (ClampMin = "0.0", Units = "s"))
	float SoilDryTransitionTime = 3.0f;

	/** 当前显示的土壤湿度 0..1（已含过渡） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Plant|Soil")
	float GetSoilWetness() const { return SoilWetness; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shoot|Components")
	UStaticMeshComponent* PlantMesh;

	/** 交互范围：角色进入后注册为可交互植物 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shoot|Components")
	USphereComponent* InteractionSphere;

	/** 植株脚下的土壤干湿贴花（只投到地面；植株与果实不接收贴花）。尺寸/材质可在蓝图里改 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shoot|Components")
	UDecalComponent* SoilDecal;

	// ============ 交互判定尺度（统一标准，见 InteractionConfig.h） ============
	/** 交互球半径。C++ 在 BeginPlay 强制应用，覆盖蓝图残留旧值 */
	UPROPERTY(EditDefaultsOnly, Category = "Shoot|Interaction")
	float InteractionRadius = InteractionDistance::Plant;

	/** 球心抬高量：抬到植株中部，与角色胶囊的覆盖高度对齐 */
	UPROPERTY(EditDefaultsOnly, Category = "Shoot|Interaction")
	float InteractionCenterHeight = InteractionDistance::CenterHeight;

	/** 调试开关：在屏幕上打印植物状态（可在蓝图/实例上设置） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Debug")
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
	/** 摘取后重置回第一阶段（种子期）：清空收获/害虫状态并恢复生长，使植被重新生长结果（不再销毁植被） */
	void ResetToSeedStage();

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

	/** 是否已浇满：浇满后置位，之后不再消耗水分（保持满水长到结果），摘果重置回幼苗时清除 */
	bool bWatered = false;

	// ========== 需水窗口（bUseThirstWindows） ==========
	bool bThirstWindowOpen = false;
	bool bThirstWindowOpenedThisStage = false;
	bool bThirstWindowSatisfied = false;
	bool bThirstWindowMissed = false;
	float ThirstWindowRemaining = 0.f;

	void ResetThirstWindowState();
	void OpenThirstWindow();
	void CloseThirstWindow(bool bSatisfied);
	void UpdateThirstWindow(float DeltaTime);

	/** 是否启用需水窗口：以 DataTable 配置为准；控制台 Game.PlantThirstWindows 1 可全局强制开启用于 A/B 验证 */
	bool UsesThirstWindows() const;

	// ========== 照料连击 ==========
	/** 最近一次浇水 / 驱虫的世界时间（初始为很小的值，保证未操作时判定为超时） */
	float CareWaterTime = -1000.f;
	float CarePestTime = -1000.f;
	bool bPestAppearedThisCycle = false;

	int32 EvaluateCareComboBonus() const;

	// ========== 浇水表现 ==========
	void PlayWaterFeedback();
	void UpdateWaterFeedback(float DeltaTime);

	float WaterShakeRemaining = 0.f;
	float WaterShakeElapsed = 0.f;
	FRotator WaterShakeBaseRotation = FRotator::ZeroRotator;

	/** 当前长在树上的果实（进入结果阶段时生成，收获时收集）。熟度状态已下沉到每个果实自身，
	 *  由植物在生成果实时写入配置并启动计时，果实独立推进与广播。 */
	UPROPERTY()
	TArray<TObjectPtr<AItemActor>> SpawnedFruits;

	/** 正在交互的角色（重叠时设置，用于收获时把果实放入其背包） */
	UPROPERTY()
	TObjectPtr<AShootCharacter> InteractingCharacter;

	/** 当前采摘描边是否已开启（缓存，避免每帧重复调用 SetActorsOutline） */
	/** 角色是否把本株选为"当前唯一交互目标"（描边请求，非最终是否点亮） */
	bool bHighlightRequested = false;

	/** 当前描边是否真的亮着（缓存，避免重复改写自定义深度） */
	bool bHintOutlineActive = false;

	/** 浇水提示抑制：浇水动作完成后置位，避免“刚浇完”立刻又被高亮；植株重新变干或玩家离开范围后自动清除 */
	bool bWaterHintSuppressed = false;

	/** 自动重新结果定时器（逐个生长时用） */
	FTimerHandle RefruitTimerHandle;

	/** 采摘后重新生长前的等待时长（秒）：并入“幼苗→下一阶段”的时长，等效于该阶段 StageDuration + 此值。由 Harvest 重置植被时写入，Tick 中逐渐消耗。 */
	float RegrowDelayRemaining = 0.f;

	void InitSoilDecal();
	void UpdateSoilWetness(float DeltaTime);
	float GetTargetSoilWetness() const;
	/** 让本 Actor 上除贴花外的所有图元都不接收贴花，避免植株/果实被土壤贴花染色 */
	static void DisableReceivingDecals(AActor* Actor);

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SoilDecalMID;

	float SoilWetness = 0.f;
};
