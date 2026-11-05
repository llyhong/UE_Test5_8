// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Engine/Texture2D.h"
#include "Event/PestActor.h"
#include "RandomEventData.generated.h"

/** 突发事件允许发生的时段：营业期间 / 打烊期间 / 两者皆可 */
UENUM(BlueprintType)
enum class ERandomEventTiming : uint8
{
	BusinessOpen   UMETA(DisplayName = "仅营业期间"),
	BusinessClosed UMETA(DisplayName = "仅打烊期间"),
	Both           UMETA(DisplayName = "营业/打烊都可"),
};

/**
 * 一条突发事件配置（DataTable 行结构，RowName = EventID，便于存档 / UI 引用）。
 *
 * 全部文案与数值都在这张表里调：新增一种突发事件只需加一行（并配好生成物蓝图），无需改代码。
 * 触发流程：RandomEventManager 每个 CheckIntervalSeconds 检查一次 → 过滤时段/时间窗/冷却/当日次数
 *          → 按 ChancePerCheck 掷骰 → 命中的事件里按 Weight 加权挑选 → 开始事件。
 * 事件进行中：生成物被全部抓住即成功；DurationSeconds 内没抓完即失败（扣声誉、可按配置吓跑客人）。
 */
USTRUCT(BlueprintType)
struct SHOOT_API FRandomEventInfo : public FTableRowBase
{
	GENERATED_BODY()

	// ==================== UI 文案 ====================
	/** 事件标题（如"老鼠出没"），HUD 常驻显示 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI")
	FText Title;

	/** 事件描述（如"厨房里窜进了老鼠，快抓起来，否则客人要被吓跑！"） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI")
	FText Description;

	/** 事件开始时的提示文案（飘字 / 弹窗用） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI")
	FText StartText;

	/** 成功处理完的文案 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI")
	FText SuccessText;

	/** 失败（超时）的文案 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI")
	FText FailText;

	/** 可选图标（HUD 用） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI")
	TSoftObjectPtr<UTexture2D> Icon;

	// ==================== 触发规则 ====================
	/** 允许在什么时候发生：仅营业 / 仅打烊 / 都可以 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trigger")
	ERandomEventTiming Timing = ERandomEventTiming::Both;

	/** 同时满足条件的多个事件之间的权重（越大越容易被选中） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trigger", meta = (ClampMin = "0.01", UIMin = "0.01"))
	float Weight = 1.f;

	/** 每个检查周期的触发概率（0~1）：0.15 = 每次检查约 15% 概率触发。
	 *  实际频率 ≈ ChancePerCheck / CheckIntervalSeconds，检查间隔在 Manager 上调。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trigger", meta = (ClampMin = "0", ClampMax = "1"))
	float ChancePerCheck = 0.15f;

	/** 允许触发的游戏内小时窗口（与 TimeSystem 的 DayStartHour~DayEndHour 同一坐标系）。
	 *  MaxStartHour <= MinStartHour 时视为不限时间（默认 0/0）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trigger")
	float MinStartHour = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trigger")
	float MaxStartHour = 0.f;

	/** 同一游戏日内最多触发几次；0 = 不限 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trigger", meta = (ClampMin = "0"))
	int32 MaxTriggersPerDay = 1;

	/** 触发后至少隔几天才可能再次出现；0 = 不冷却（当天也可再来） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trigger", meta = (ClampMin = "0"))
	int32 CooldownDays = 1;

	// ==================== 持续与结算 ====================
	/** 事件持续时间（秒）：超时未处理即失败；<=0 表示不自动失败（只靠外部逻辑结束） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resolve", meta = (ClampMin = "0"))
	float DurationSeconds = 40.f;

	/** 生成物（老鼠等）全部被抓住时是否直接判定成功 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resolve")
	bool bSuccessWhenTargetsCleared = true;

	/** 成功处理后的声誉奖励 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resolve")
	int32 ReputationGainOnSuccess = 2;

	/** 失败时的声誉损失（正数，扣减量） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resolve", meta = (ClampMin = "0"))
	int32 ReputationLossOnFail = 3;

	/** 失败时把在场客人 / 排队客人全部吓走（客人离场会取消未完成订单） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resolve")
	bool bScareAllGuestsOnFail = true;

	/** 营业/打烊状态切换后事件已不符合时段要求时，是否立即按失败结束
	 *  （如"仅营业期间"的老鼠事件，打烊时还没抓完就按失败结算）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resolve")
	bool bCancelOnBusinessStateChange = true;

	// ==================== 生成物（老鼠等） ====================
	/** 事件生成物蓝图类（需继承 APestActor，如 BP_Mouse）；留空表示该事件不生成实物 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	TSoftClassPtr<APestActor> PestClass;

	/** 生成区域标签（对应 Manager 的 SpawnAreas.Label，如 Kitchen） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	FName SpawnAreaLabel = TEXT("Kitchen");

	/** 一次生成几个 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn", meta = (ClampMin = "0"))
	int32 SpawnCount = 1;

	/** 生成物游走速度（cm/s） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn", meta = (ClampMin = "0"))
	float PestMoveSpeed = 150.f;

	/** 玩家靠近多少距离开始逃跑 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn", meta = (ClampMin = "0"))
	float PestFleeRadius = 350.f;

	/** 生成物围绕出生点的活动半径（cm） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn", meta = (ClampMin = "0"))
	float PestWanderRadius = 450.f;

	/** 生成物靠近客人时是否把客人吓跑（订单随之取消） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	bool bPestsScareGuests = true;

	/** 吓跑客人的触发距离（cm） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn", meta = (ClampMin = "0"))
	float PestScareRadius = 250.f;
};
