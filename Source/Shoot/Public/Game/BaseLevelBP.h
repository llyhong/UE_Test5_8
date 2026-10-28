// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Game/FoodSystem.h"
#include "Game/OrderSystem.h"
#include "Game/PlateSystem.h"
#include "Game/EconomySystem.h"
#include "Game/BusinessSimulation.h"
#include "Game/LevelGoalData.h"
#include "NPC/ChairSeat.h"
#include "BaseLevelBP.generated.h"

class UOrderWidget;
class UHUDWidget;

/** UI 层级（z-order）：HUD 在最底层，订单 UI 在其上，避免手动散写魔法数字 */
enum class EWidgetZOrder : int32
{
	HUD = 0,
	Order = 10,
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSocceEvent,float,s);

/** 关卡目标达成广播：携带本关最终统计（服务客人 / 赚取金钱 / 得分 / 声誉），供 HUD 显示通关面板 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FLevelGoalCompleted, int32, GuestsServed, int32, MoneyEarned, int32, Score, int32, Reputation);

/** 关卡切换广播（同一张大地图内“进入下一关”）：携带新的 LevelID，供 UI / NPC 重置自身状态（如奖励已领取标记） */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLevelChanged, int32, NewLevelID);

/** 关卡打烊广播（进入下一关过渡开始时触发）：携带倒计时秒数，供 HUD 显示“打烊”提示 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLevelClosing, float, CountdownSeconds);

UCLASS(Blueprintable)
class SHOOT_API ABaseLevelBP : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ABaseLevelBP();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:	
	UPROPERTY(BlueprintReadOnly, Category = "Gameplay")
	bool IsPlaying = false;

	/**
	 * 经营模拟类（可选）：在关卡实例的 Details 里指向你在编辑器创建的 BP_BusinessSimulation（UBusinessSimulation 蓝图子类），
	 * 即可在该蓝图里可视化调整经营参数（每日租金 / 维护 / 赛事奖金 / TimeSystemClass 等），无需改 C++。
	 * 留空则使用 GameInstance 默认的 UBusinessSimulation。仅在首次进入且类不匹配时重建，不会跨关卡重复重置日期。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Business")
	TSubclassOf<UBusinessSimulation> BusinessSimulationClass;

	/** 便捷引用：返回当前生效的经营模拟（实际由 GameInstance 持有）；未就绪时返回 nullptr */
	UFUNCTION(BlueprintCallable, Category = "Business")
	UBusinessSimulation* GetBusinessSimulation() const;
	// Called every frame
	UPROPERTY(BlueprintReadOnly, Category = "Gameplay")
	float StartTime = 0;
	UPROPERTY(EditAnywhere,BlueprintReadWrite, Category = "Gameplay")
	float EndTime = 300;
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOrderEvent2 OnFailOrder;
	virtual void Tick(float DeltaTime) override;
	UFUNCTION(BlueprintCallable)
	void InitOrderSystem(UDataAsset* MenuDataTable);

	/** 按当前 LevelID 切换订单配置并重启订单系统（切换关卡时复用，不重新 Spawn Actor） */
	void RebuildOrderSystem();
	UFUNCTION(BlueprintCallable)
	void InitPlateSystem(int PlateCount, float SpawnPlateTime);

	/** 每秒把当前时间与营业状态推给 HUD（白天/傍晚/打烊一目了然） */
	UFUNCTION()
	void RefreshClock();
	UPROPERTY(BlueprintReadOnly, Category = "Gameplay")
	TObjectPtr<AOrderSystem> orderSystem;

	/**
	 * 各关卡的订单配置映射：Key = LevelID，Value = 该关卡的 ULevelOrderCfg 资源。
	 * BeginPlay 时按本关卡的 LevelID 取对应配置自动创建订单系统；找不到对应项则回退到 DefaultOrderConfig。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Order")
	TMap<int32, TObjectPtr<ULevelOrderCfg>> LevelOrderConfigs;

	/**
	 * 兜底订单配置（ULevelOrderCfg）。当 LevelOrderConfigs 里没有当前 LevelID 的项时使用。
	 * 默认值指向 Content/_Demo/Data/Order/Level_06_Order_Shit，可在蓝图默认值里改。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Order")
	TObjectPtr<ULevelOrderCfg> DefaultOrderConfig ;
	UPROPERTY(BlueprintReadOnly, Category = "Gameplay")
	TObjectPtr<APlateSystem> PlateSystem;
	UFUNCTION(BlueprintCallable)
	void Play();
	UFUNCTION(BlueprintCallable)
	float GetRemainTime();
	void LevelFinish();
	UFUNCTION(BlueprintImplementableEvent, Category="Gameplay")
	void OnLevelFinish();
	UFUNCTION(BlueprintCallable, Category="Gameplay")
	static void AddScore(AActor* actor,int detal);
	UFUNCTION(BlueprintCallable, Category="Gameplay")
	static int GetScore(AActor* actor);

	// ========== 关卡目标（通过条件，DataTable 配置） ==========
	/** 关卡目标数据表（RowStruct = FLevelGoalRow）。为空则使用 UBaseGameInstance::LevelGoalTable；都为空则本关无通过条件 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level|Goal")
	TObjectPtr<UDataTable> LevelGoalTable;

	/** 达成目标后是否立即结束本关（复用现有 LevelFinish 流程）。
	 *  false（默认）：仅解锁食材并广播 OnLevelGoalAchieved，关卡继续（玩家可使用已解锁食材）；
	 *  true：达成即“通过”本关，结束关卡。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level|Goal")
	bool bFinishLevelOnGoalAchieved = false;

	/** 解析后的本关目标（无效/未配置则 bGoalConfigured=false，无通过条件） */
	UPROPERTY(BlueprintReadOnly, Category = "Level|Goal")
	FLevelGoalRow ResolvedGoal;

	UPROPERTY(BlueprintReadOnly, Category = "Level|Goal")
	bool bGoalConfigured = false;

	/** 是否已达成目标 */
	UPROPERTY(BlueprintReadOnly, Category = "Level|Goal")
	bool bGoalAchieved = false;

	/** 关卡是否已结束（防止时间到与达成目标重复触发 LevelFinish） */
	bool bLevelFinished = false;

	/** 本关累计赚取的钱（仅正向收入，用于目标判定） */
	UPROPERTY(BlueprintReadOnly, Category = "Level|Goal")
	int32 MoneyEarnedThisLevel = 0;

	/** 本关累计服务的客人数量（完成订单数） */
	UPROPERTY(BlueprintReadOnly, Category = "Level|Goal")
	int32 GuestsServedThisLevel = 0;

	/** 目标达成广播（蓝图做庆祝 / 解锁提示 UI） */
	UFUNCTION(BlueprintImplementableEvent, Category = "Level|Goal")
	void OnLevelGoalAchieved();

	/** 目标达成广播（携带本关统计，供 HUD 显示通关结算面板）。由 AchieveLevelGoal 自动广播 */
	UPROPERTY(BlueprintAssignable, Category = "Level|Goal")
	FLevelGoalCompleted OnLevelGoalCompleted;

	/** 从数据表解析本关目标（按 LevelID 查找行） */
	UFUNCTION(BlueprintCallable, Category = "Level|Goal")
	void ResolveLevelGoal();

	/** 检查是否满足目标，满足则触发达成逻辑（幂等，可反复调用） */
	UFUNCTION(BlueprintCallable, Category = "Level|Goal")
	void CheckLevelGoal();

	/** 触发目标达成：解锁食材 + 广播 +（可选）结束关卡 */
	UFUNCTION(BlueprintCallable, Category = "Level|Goal")
	void AchieveLevelGoal();

	/** 经济系统金钱变化回调：仅订单完成来源才累计本关收入并检查目标 */
	UFUNCTION()
	void OnEconomyMoneyChanged(int32 NewValue, int32 Delta, EMoneySource Source);

	/** 经济系统声誉变化回调：检查目标 */
	UFUNCTION()
	void OnEconomyReputationChanged(int32 NewValue, int32 Delta);

	/** 生成目标进度文本（如"金钱 60/100 · 客人 4/10"），仅显示配置了目标(>0)的项。供 HUD 显示进度 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Level|Goal")
	FString GetGoalProgressText();

	/** 整体目标完成度（0~1）：取所有已配置目标(>0)的最小完成比例。无配置目标时返回 1.0（视为已完成）。供 HUD 进度条 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Level|Goal")
	float GetGoalProgressRatio();

	/** 返回 DataTable 中配置的关卡目标描述文本（如"赚取 100 金币并服务 10 位客人"）。供 HUD 顶部说明任务 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Level|Goal")
	FText GetGoalDescriptionText() const;

	// ========== 同一张大地图内“进入下一关”（不切换地图，仅切换 LevelID / 目标 / 订单） ==========
	/** 触发进入下一关流程：先停单 / 清空上一关订单与在场客人（打烊），
	 *  广播打烊提示并倒计时 LevelTransitionDuration 秒后，再正式切换 LevelID / 目标 / 订单。
	 *  供升级 NPC 调用。过渡期间重复调用会被忽略。 */
	UFUNCTION(BlueprintCallable, Category = "Level")
	void AdvanceToLevel(int32 NewLevelID);

	/** 进入下一关（LevelID + 1） */
	UFUNCTION(BlueprintCallable, Category = "Level")
	void AdvanceToNextLevel();

	// ========== 调试：快速触发打烊过渡（无需完成目标） ==========
	/** 调试用：强制从当前关卡进入下一关的打烊过渡（跳过目标判定）。可在蓝图 / 控制台直接调用测试 */
	UFUNCTION(BlueprintCallable, Category = "Debug|Level")
	void DebugAdvanceToNextLevel();

	/** 关卡切换广播（NewLevelID），供 UI / NPC 重置自身状态（如奖励已领取标记） */
	UPROPERTY(BlueprintAssignable, Category = "Level")
	FOnLevelChanged OnLevelChanged;

	/** 打烊广播（进入下一关过渡开始时触发）：携带倒计时秒数，供 HUD 显示“打烊”提示 */
	UPROPERTY(BlueprintAssignable, Category = "Level")
	FOnLevelClosing OnLevelClosing;

	/** 打烊过渡时长（秒）：清场、显示打烊提示后，倒计时结束才正式进入下一关 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level")
	float LevelTransitionDuration = 3.0f;

	/** 打烊收尾完成（已点菜客人全部处理完）时，是否自动结束这一天、进入下一关。
	 *  默认 false：保持原有流程（由升级 NPC 对话触发进入下一关）；
	 *  置 true 则客人接待完就自动收工进入下一天。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level")
	bool bAutoEndDayOnClosingFinished = false;

	// —— 进入下一关的内部两阶段实现 ——
	/** 阶段一：停单 + 清空上一关订单与客人（打烊），广播打烊提示，并启动倒计时 */
	void BeginLevelTransition(int32 NewLevelID);

	/** 阶段二：倒计时结束后正式切换 LevelID / 目标 / 订单（原 AdvanceToLevel 主体） */
	void CommitLevelTransition();

	/** 是否正处于打烊过渡中（防止重复触发） */
	bool bInLevelTransition = false;

	/** 过渡中等待提交的目标关卡 ID */
	int32 PendingLevelID = INDEX_NONE;

	/** 打烊倒计时句柄 */
	FTimerHandle ClosingTimerHandle;

	/** 时钟刷新定时器句柄 */
	FTimerHandle ClockTimerHandle;
	UPROPERTY(BlueprintReadOnly, Category = "Gameplay")
	int Score = 0;
	UPROPERTY(EditAnywhere,BlueprintReadWrite, Category = "Gameplay")
	int LevelID = 0;

	/** InitOrderSystem 时是否自动启动订单系统（IsStarted=true）。关闭后需在蓝图里手动调用 StartSystem */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gameplay")
	bool bAutoStartOrderSystem = true;

	/** 订单 UI widget class（继承自 UOrderWidget）。BeginPlay 自动创建并绑定本关卡的订单系统 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Order")
	TSubclassOf<UOrderWidget> OrderWidgetClass;

	/** 订单 UI 实例（由 BeginPlay 创建，关卡结束时 RemoveFromParent） */
	UPROPERTY(BlueprintReadOnly, Category = "Order")
	TObjectPtr<UOrderWidget> OrderWidget;

	/** 常驻 HUD widget class（继承自 UHUDWidget，显示声誉/金钱）。BeginPlay 自动创建并绑定经济系统 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|HUD")
	TSubclassOf<UHUDWidget> HUDWidgetClass;

	/** 常驻 HUD 实例（由 BeginPlay 创建，关卡结束时 RemoveFromParent） */
	UPROPERTY(BlueprintReadOnly, Category = "UI|HUD")
	TObjectPtr<UHUDWidget> HUDWidget;

	UPROPERTY(BlueprintAssignable, Category = "Events")
	FSocceEvent LevelUpdateScore;
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FSocceEvent LevelGetScore;
	//保存分数
	void SaveGame();
	//获取历史最高分
	UFUNCTION(BlueprintCallable)
	int GetHistoryMaxScore();

	/** 订单完成回调：驱动经济系统（加声誉 + 加金钱） */
	UFUNCTION()
	void HandleOrderFinished(const UOrderHandle* Handle, const FOrderData& Order);
	/** 订单失败（超时）回调：驱动经济系统（扣声誉） */
	UFUNCTION()
	void HandleOrderFailed(const UOrderHandle* Handle, const FOrderData& Order);

	/** 打烊收尾完成回调（已点菜客人全部处理完）：按开关决定是否自动结束这一天 */
	UFUNCTION()
	void HandleClosingFinished();
};
