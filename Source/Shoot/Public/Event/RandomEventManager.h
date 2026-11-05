// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Game/BusinessTime.h"
#include "Event/RandomEventData.h"
#include "RandomEventManager.generated.h"

class APestActor;
class UDataTable;

/** 事件生成物的撒点区域（对应配置表里的 SpawnAreaLabel） */
USTRUCT(BlueprintType)
struct FEventSpawnArea
{
	GENERATED_BODY()

	/** 区域标签：配置表里用同名字符串引用（如 Kitchen / DiningRoom） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Event")
	FName Label = TEXT("Kitchen");

	/** 圆心（世界坐标；关卡里改本 Actor 的位置不影响这里，直接在数组元素上填） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Event")
	FVector Center = FVector::ZeroVector;

	/** 半径（cm） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Event", meta = (ClampMin = "0"))
	float Radius = 350.f;

	/** 是否向下打线贴地 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Event")
	bool bSnapToGround = true;
};

/** 事件开始：HUD 用 Title/Description/StartText 显示提示 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRandomEventStarted, const FRandomEventInfo&, EventInfo);

/** 事件结算：bSuccess=true 已解决 / false 失败 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnRandomEventResolved, const FRandomEventInfo&, EventInfo, bool, bSuccess);

/** 进度变化（抓住 1 只老鼠等）：供 HUD 刷新“已处理 x/y” */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnRandomEventProgress, const FRandomEventInfo&, EventInfo, int32, ClearedCount, int32, TotalCount);

/**
 * 突发事件管理器（关卡里放一个）。
 *
 * 职责：
 *  - 按 CheckIntervalSeconds 周期检查配置表（EventTable，行结构 FRandomEventInfo）；
 *  - 按“营业/打烊时段 + 小时窗口 + 冷却天数 + 当日次数 + ChancePerCheck”筛出可触发事件，
 *    多个候选按 Weight 加权挑选；
 *  - 开始事件时按 SpawnAreaLabel 在 SpawnAreas 里对应的区域生成害虫（BP_Mouse 等）；
 *  - 害虫全部抓住 → 成功（奖励声誉）；DurationSeconds 内没处理完 → 失败（扣声誉，可吓跑客人）；
 *  - 通过 OnEventStarted / OnEventResolved / OnEventProgress 把配置表里的文案推给 HUD。
 *
 * 所有事件内容（文案、概率、时段、数量、结算数值）都在配置表里改；
 * 代码侧只需要在关卡里放好本 Actor、指定 DataTable，并在 SpawnAreas 里标好厨房等区域。
 */
UCLASS(Blueprintable)
class SHOOT_API ARandomEventManager : public AActor
{
	GENERATED_BODY()

public:
	ARandomEventManager();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// ==================== 配置 ====================
	/** 突发事件配置表（行结构 FRandomEventInfo；RowName 即 EventID） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|RandomEvent|Config")
	TObjectPtr<UDataTable> EventTable;

	/** 生成区域列表：配置表用 SpawnAreaLabel 引用；找不到时退回本 Actor 位置 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|RandomEvent|Config")
	TArray<FEventSpawnArea> SpawnAreas;

	/** 是否自动随机触发（关闭后只能蓝图/控制台手动 StartEventByName，适合调试） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|RandomEvent|Config")
	bool bAutoTrigger = true;

	/** 检查间隔（秒）：每隔这么久掷一次骰子。越小事件越密集 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|RandomEvent|Config", meta = (ClampMin = "1.0"))
	float CheckIntervalSeconds = 15.f;

	/** 两次事件之间的全局冷却（秒，从上一次结算算起） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|RandomEvent|Config", meta = (ClampMin = "0.0"))
	float MinTimeBetweenEventsSeconds = 60.f;

	/** 随机种子：0 = 每局不同；非 0 可复现（调试用） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|RandomEvent|Config")
	int32 RandomSeed = 0;

	/** 打印触发 / 结算日志与屏幕提示 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|RandomEvent|Debug")
	bool bShowDebugInfo = true;

	// ==================== 运行时状态 ====================
	/** 当前事件 ID（RowName）；None 表示没有进行中的事件 */
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|RandomEvent|Runtime")
	FName ActiveEventID;

	/** 当前事件配置副本（HUD 可直接读取） */
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|RandomEvent|Runtime")
	FRandomEventInfo ActiveEventInfo;

	/** 当前事件剩余时间（秒）；<=0 且仍激活表示没有超时限制 */
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|RandomEvent|Runtime")
	float ActiveEventRemainingSeconds = 0.f;

	/** 已处理数量（抓住的害虫数） */
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|RandomEvent|Runtime")
	int32 ActiveClearedCount = 0;

	/** 需要处理的总数量 */
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|RandomEvent|Runtime")
	int32 ActiveTotalCount = 0;

	/** 当前事件生成出来的害虫（内部持有，避免被 GC） */
	UPROPERTY()
	TArray<TObjectPtr<APestActor>> ActivePests;

	// ==================== 事件 ====================
	UPROPERTY(BlueprintAssignable, Category = "Shoot|RandomEvent|Events")
	FOnRandomEventStarted OnEventStarted;

	UPROPERTY(BlueprintAssignable, Category = "Shoot|RandomEvent|Events")
	FOnRandomEventResolved OnEventResolved;

	UPROPERTY(BlueprintAssignable, Category = "Shoot|RandomEvent|Events")
	FOnRandomEventProgress OnEventProgress;

	// ==================== 蓝图接口 ====================
	/** 手动触发指定事件（调试 / 剧情用）；成功返回 true */
	UFUNCTION(BlueprintCallable, Category = "Shoot|RandomEvent")
	bool StartEventByName(FName EventID);

	/** 结算当前事件：成功=奖励声誉；失败=扣声誉，并按配置吓跑客人 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|RandomEvent")
	void ResolveActiveEvent(bool bSuccess);

	/** 静默结束当前事件（不结算、不扣声誉）：清掉生成物 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|RandomEvent")
	void CancelActiveEvent();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|RandomEvent")
	bool HasActiveEvent() const { return ActiveEventID != NAME_None; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|RandomEvent")
	FText GetActiveEventTitle() const { return ActiveEventInfo.Title; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|RandomEvent")
	FText GetActiveEventDescription() const { return ActiveEventInfo.Description; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|RandomEvent")
	float GetActiveEventRemainingSeconds() const { return ActiveEventRemainingSeconds; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|RandomEvent")
	int32 GetActiveClearedCount() const { return ActiveClearedCount; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|RandomEvent")
	int32 GetActiveTotalCount() const { return ActiveTotalCount; }

protected:
	/** 已绑定的事件时钟（弱引用，关卡卸载后失效） */
	UPROPERTY()
	TObjectPtr<UTimeSystem> BoundTimeSystem;

	/** 随机流 */
	FRandomStream Rand;

	/** 每个事件最近一次触发的绝对天数（用于 CooldownDays） */
	TMap<FName, int32> LastTriggerAbsDay;

	/** 每个事件当日已触发次数（跨日清空） */
	TMap<FName, int32> TriggerCountToday;

	/** 上次结算的世界时间（用于全局冷却） */
	float LastResolveWorldTime = -1.f;

	/** 下次检查的世界时间 */
	float NextCheckWorldTime = 0.f;

	/** 是否已初始化随机流 */
	bool bRandReady = false;

	/** 初始化随机流（幂等） */
	void EnsureRandomReady();

	/** 确保绑定到当前时间系统（关卡可能用蓝图子类重建时钟，这里幂等重绑） */
	void EnsureTimeSystemBound();

	/** 新的一天：清空当日触发计数 */
	UFUNCTION()
	void HandleDayChanged(int32 InYear, int32 InMonth, int32 InDay);

	/** 营业状态变化：立即重新检查；进行中事件不再符合时段时按配置结束 */
	UFUNCTION()
	void HandleBusinessOpenChanged(bool bOpen);

	/** 该事件当前时段是否允许（营业 / 打烊 / 都可） */
	bool IsTimingAllowed(const FRandomEventInfo& Info) const;

	/** 该事件是否处于允许触发的小时窗口内 */
	bool IsHourAllowed(const FRandomEventInfo& Info) const;

	/** 冷却 / 当日次数 / 时段 / 小时窗口 全部满足才可参与本次随机 */
	bool IsEventEligible(FName RowName, const FRandomEventInfo& Info) const;

	/** 本次检查：筛出候选事件，逐个掷 ChancePerCheck，命中者按 Weight 选一个开始 */
	void TryTriggerRandomEvent();

	/** 在指定区域里随机取一个贴地点；找不到区域时退回本 Actor 位置 */
	FVector PickSpawnLocation(FName AreaLabel) const;

	/** 按标签查找生成区域 */
	const FEventSpawnArea* FindSpawnArea(FName Label) const;

	/** 生成事件所需的害虫并绑定抓取回调 */
	void SpawnEventPests(const FRandomEventInfo& Info);

	/** 害虫被抓住：更新进度，抓完即成功 */
	UFUNCTION()
	void HandlePestCaught(APestActor* Pest);

	/** 更新进行中的事件（倒计时 / 抓完判定 / 超时失败） */
	void UpdateActiveEvent(float DeltaSeconds);

	/** 失败时把在场与排队的客人全部吓走（客人离场会取消未完成订单） */
	void ScareAllGuests();

	/** 当前绝对天数（用于冷却换算）；时间系统未就绪时返回 0 */
	int32 GetAbsDay() const;

	/** 调试输出（屏幕 + 日志） */
	void ShowDebug(const FString& Message, const FLinearColor& Color) const;
};
