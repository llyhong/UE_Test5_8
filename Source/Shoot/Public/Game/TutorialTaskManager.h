// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Data/TutorialTaskData.h"
#include "TutorialTaskManager.generated.h"

class UDataTable;
class AShootCharacter;
class UTimeSystem;
class ADiningHygieneManager;
class AOutdoorBasket;
class AItemActor;

USTRUCT(BlueprintType)
struct SHOOT_API FTutorialChecklistEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Tutorial")
	FText Text;

	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Tutorial")
	bool bCompleted = false;

	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Tutorial")
	bool bNote = false;
};

/**
 * 新手任务管理器（关卡里放一个）：
 *  - 读取 FTutorialTaskRow 配置表，按 UTimeSystem::Day 自动切换当天的任务；
 *  - 轮询统计各类任务进度（清理 / 接待 / 拾取 / 采摘 / 研究 / 入库）；
 *  - 为每个本地玩家创建引导 HUD（任务清单 + 指向目标的箭头）；
 *  - 当天配置了“清理垃圾”时，接管就餐区卫生管理器：只刷指定数量的垃圾并暂停自动生成。
 */
UCLASS(Blueprintable)
class SHOOT_API ATutorialTaskManager : public AActor
{
	GENERATED_BODY()

public:
	ATutorialTaskManager();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 新手任务配置表（RowStruct = FTutorialTaskRow）。留空时不启用 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Tutorial",
		meta = (RequiredAssetDataTags = "RowStructure=/Script/Shoot.TutorialTaskRow"))
	TObjectPtr<UDataTable> TaskTable;

	/** 总开关 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Tutorial")
	bool bEnable = true;

	/** 进度轮询间隔（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Tutorial", meta = (ClampMin = "0.1"))
	float PollInterval = 0.25f;

	/** 当前是否还有未完成任务（供 UI） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Tutorial")
	bool HasActiveTasks() const { return ActiveTasks.Num() > 0; }

	/** 按天追加的提示行（如第 3 天提醒“客人有耐心限制了”），显示在任务清单末尾 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Tutorial")
	TMap<int32, FText> DayExtraNotes;

	/** 前三天不出现垃圾/积水，第四天开始清理教学；可追加其他无垃圾日。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Tutorial")
	TArray<int32> NoMessesDays = { 1, 2, 3, 4 };

	UFUNCTION(BlueprintCallable, Category = "Shoot|Tutorial")
	TArray<FTutorialChecklistEntry> GetChecklistEntries() const;
	UFUNCTION(BlueprintCallable, Category = "Shoot|Tutorial")
	TArray<FTutorialChecklistEntry> GetVisibleChecklistEntries() const;

	UFUNCTION(BlueprintCallable, Category = "Shoot|Tutorial")
	bool CanOpenForTutorial(FText& OutReason);

	UFUNCTION(BlueprintCallable, Category = "Shoot|Tutorial")
	void CaptureTutorialState();

	/** 调试：立刻跳过全部新手引导（清任务、隐藏任务卡、释放引导暂停的时钟） */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Tutorial|Debug")
	void SkipTutorial();

	/** 调试：跳过教程并从第 3 天开始（时钟/存档/赛季一起跳）。蓝图里直接调用，或勾选下方开关 PIE 自动执行 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Tutorial|Debug")
	void DebugStartFromDay3();

	/** 勾选后 PIE 开始时自动跳过教程并从第 3 天开始（放在 TutorialTaskManager 蓝图 Class Defaults 上调） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Tutorial|Debug")
	bool bAutoStartDay3 = false;

	/** 当前引导箭头应指向的文本；无指引返回空 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Tutorial")
	FText GetFocusHint() const { return FocusHint; }

	/** 当前引导目标位置；返回 false 表示当前没有可指引的目标 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Tutorial")
	bool GetFocusTargetLocation(FVector& OutLocation) const;

	/** 当天任务标题，如“第 1 天任务” */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Tutorial")
	FText GetDayTitle() const;

	/** 成功交互事件才推进对应步骤；投锅任务不再用拾取量代替。 */
	static void NotifyAction(UWorld* World, ETutorialTaskType Type, int32 ItemID, AActor* Target = nullptr, int32 Quantity = 1);
	bool IsFirstServiceTutorialActive() const;
	bool IsFirstDayPlateLessonActive() const;
	bool IsDayTwoServiceLesson() const;
	bool CanAdmitDayTwoGuest(int32 UnservedGuests) const;

private:
	struct FActiveTask
	{
		FTutorialTaskRow Row;
		int32 Progress = 0;
		int32 MaxSeen = 0;
		/** 带篮子采摘：上一帧全场篮子里的目标食材总数，用于只累计“增加”的部分 */
		int32 LastBasketTotal = 0;
		bool bDone = false;
	};

	TArray<FActiveTask> ActiveTasks;

	/** 当前已应用的任务天数（-1 表示还没应用） */
	int32 AppliedDay = INDEX_NONE;

	bool bHygieneAutoSpawnOverridden = false;

	/** 任务基线：接待数 / 已清理数 / 收入（服务与收入可能跨天累计，按当天起点做差） */
	int32 ServeBaseline = 0;
	int32 TrashBaseline = 0;
	int32 MoneyBaseline = 0;

	float PollAccum = 0.f;
	float InitDelay = 0.4f;
	bool bHoldingTutorialClock = false;
	bool bDebugJumpPending = false;
	TWeakObjectPtr<class UMySaveGame> AppliedSave;

	/** 每个角色上一帧手持物品的 ItemID，用于统计“新采到手上”的次数 */
	TMap<TWeakObjectPtr<AShootCharacter>, int32> PrevHeldItemID;

	FText FocusHint;
	FVector FocusLocation = FVector::ZeroVector;
	bool bHasFocusTarget = false;
	bool bRestoring = false;
	bool bSavePending = false;
	UPROPERTY()
	TSubclassOf<AItemActor> PracticePlateClass;

	float SnapshotAccum = 0.f;
	void RestoreTutorialState();
	int32 GetPhase(const FActiveTask& Task) const;
	int32 GetServedToday() const;
	int32 GetSupplyRequirement(int32 ItemID) const;
	int32 CountReadyStock(int32 ItemID) const;
	int32 CountAvailableSupply(int32 ItemID) const;
	static constexpr int32 FirstDayEggItemID = 2;
	static const FName CollectedFirstDayEggTag;
	void ReplenishFirstDayEggs() const;
	bool GetDayThreeOpeningStatus(FText& OutReason, int32& OutMissingItem,
		TArray<FTutorialChecklistEntry>* Card = nullptr) const;
	bool ResolveIngredientRoute(int32 ItemID, bool bMustStore, int32 RemainingToStore = 0);
	FText FormatObjectiveProgress(const FActiveTask& Task) const;
	AActor* FindIngredientSource(int32 ItemID, bool bHarvestOnly) const;

	UTimeSystem* GetTimeSystem() const;
	/** 首日开门前、第二天完成备料后，引导到入口营业。 */
	bool NeedsOpeningGuidance() const;
	ADiningHygieneManager* FindHygieneManager() const;
	int32 GetCurrentDay() const;

	/** 按当前天数装载任务（并处理垃圾刷取 / 恢复卫生管理器） */
	void ApplyDay(int32 Day);
	void ResetRuntimeState();

	/** 统计某个 ItemID 在玩家背包+手上+篮子里的总持有量 */
	int32 CountItemAcrossPlayers(int32 ItemID) const;
	int32 CountItemInBaskets(int32 ItemID, const AOutdoorBasket* OnlyBasket = nullptr) const;
	int32 CountItemInStorages(int32 ItemID, FName ActorTag) const;
	int32 CountItemInResearch(int32 ItemID, FName ActorTag) const;
	const class ABaseLevelBP* GetLevelBP() const;

	void UpdateProgress();
	void SaveCompletedTask(const FActiveTask& Task);
	void UpdateFocusTarget();
	void RefreshWidgets() const;
	void EnsureFirstDayPracticePlate();
	bool HasDirtyPlateInHand() const;

	AActor* FindTargetActor(const FActiveTask& Task) const;
};
