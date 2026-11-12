// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "UObject/NoExportTypes.h"
#include "SeasonSystem.generated.h"

class UEconomySystem;
class UBusinessSimulation;

/** 日评级：由完成率 / 超时 / 平均等待 / 附加项加权得出（见 营业忙乱机制配置.md 18.3） */
UENUM(BlueprintType)
enum class ESeasonGrade : uint8
{
	Poor      UMETA(DisplayName = "D 差"),
	Fair      UMETA(DisplayName = "C 平"),
	Good      UMETA(DisplayName = "B 良"),
	Great     UMETA(DisplayName = "A 优"),
	Excellent UMETA(DisplayName = "S 极"),
};

/** 赛季结局（第 48 天丰收祭结算后写入） */
UENUM(BlueprintType)
enum class ESeasonEnding : uint8
{
	None         UMETA(DisplayName = "未结束"),
	Failed       UMETA(DisplayName = "失败"),
	Reluctant    UMETA(DisplayName = "B 勉强"),
	Established  UMETA(DisplayName = "A 立足"),
	Famous       UMETA(DisplayName = "S 名店"),
};

/**
 * 赛季配置行（数据表 RowStruct = FSeasonConfigRow）。
 * 未配置 ConfigTable 时使用 USeasonSystem 内置默认值，调参不需要改代码。
 */
USTRUCT(BlueprintType)
struct FSeasonConfigRow : public FTableRowBase
{
	GENERATED_BODY()
public:
	/** 赛季总天数（默认 48 = 3 幕 × 16 天，约 3 小时） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Season")
	int32 SeasonDays = 48;

	/** 每幕天数；幕号 = (赛季日-1) / DaysPerAct + 1 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Season", meta = (ClampMin = "1"))
	int32 DaysPerAct = 16;

	/** 从第几天开始判定失败（之前只警告） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Season", meta = (ClampMin = "1"))
	int32 FailCheckStartDay = 8;

	/** 连续几天触发失败 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Season", meta = (ClampMin = "1"))
	int32 ConsecutiveDaysToFail = 3;

	/** 新游戏起始声誉（避免开局 0 星） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Season", meta = (ClampMin = "0"))
	int32 StartingReputation = 100;

	/** 1~5 星所需的声誉阈值（只由声誉决定，服务品质不再加星） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Season|Stars")
	int32 Star1Rep = 200;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Season|Stars")
	int32 Star2Rep = 600;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Season|Stars")
	int32 Star3Rep = 1100;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Season|Stars")
	int32 Star4Rep = 1600;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Season|Stars")
	int32 Star5Rep = 2100;

	// ---- 日评级 ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Season|Grade")
	int32 WeightCompletion = 40;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Season|Grade")
	int32 WeightTimeout = 30;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Season|Grade")
	int32 WeightWait = 20;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Season|Grade")
	int32 WeightBonus = 10;
	/** 平均等待满分线（秒）；超过后线性扣分 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Season|Grade")
	float AvgWaitTargetSeconds = 25.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Season|Grade")
	int32 GradeS = 90;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Season|Grade")
	int32 GradeA = 75;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Season|Grade")
	int32 GradeB = 60;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Season|Grade")
	int32 GradeC = 45;

	// ---- 季末丰收祭（第 SeasonDays 天） ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Season|Festival")
	int32 FestivalGuests = 30;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Season|Festival")
	int32 FestivalMaxTimeouts = 2;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Season|Festival")
	int32 FestivalWildRequired = 1;
	/** 达成胜利所需的赛季累计订单收入 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Season|Festival")
	int32 SeasonMoneyTarget = 15000;
	/** S 档额外要求的赛季累计收入 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Season|Festival")
	int32 EndingS_MoneyTarget = 18000;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnSeasonDailyGrade, ESeasonGrade, Grade, int32, Score, int32, Served, int32, Failed);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnSeasonWarning, FString, Reason, int32, StreakDays, int32, DaysLeft);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSeasonEnded, bool, bWon, ESeasonEnding, Ending);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSeasonDayChanged, int32, SeasonDay, int32, Act);

/**
 * 赛季系统：48 天三幕、每日评级、失败三线、丰收祭胜负与幕检查点。
 * 由 UBaseGameInstance 持有；日结算由 UBusinessSimulation::OnDay 调 EvaluateDay 驱动。
 * 规则见 营业忙乱机制配置.md 第 18 节。
 */
UCLASS(Blueprintable)
class SHOOT_API USeasonSystem : public UObject
{
	GENERATED_BODY()

public:
	/** 赛季配置表（RowStruct = FSeasonConfigRow）。为空则用 C++ 默认值 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Season")
	UDataTable* ConfigTable = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Season")
	FName ConfigRowName = TEXT("Default");

	/** 解析后的配置（Init 时确定） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Season")
	FSeasonConfigRow Config;

	// ---- 赛季状态（运行时只读） ----
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Season") int32 SeasonDay = 1;
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Season") int32 Act = 1;
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Season") int32 SeasonMoneyEarned = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Season") ESeasonGrade LastGrade = ESeasonGrade::Poor;
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Season") int32 LastGradeScore = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Season") int32 ConsecutivePoorDays = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Season") int32 ConsecutiveLowRepDays = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Season") int32 ConsecutiveDebtDays = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Season") bool bSeasonFailed = false;
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Season") bool bSeasonWon = false;
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Season") ESeasonEnding Ending = ESeasonEnding::None;

	/** 赛季是否已收尾（胜利 / 失败 / B 结局都算）：置位后日结算不再推进赛季 */
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Season") bool bSeasonEnded = false;

	// ---- 最近一次日结快照（日结卡用，当天数据在日结广播时写入） ----
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Season|Last") int32 LastServed = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Season|Last") int32 LastFailed = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Season|Last") int32 LastTimeouts = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Season|Last") int32 LastWildServed = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Season|Last") float LastAvgWait = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Season|Last") int32 LastDailyNet = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Season|Last") int32 LastMoney = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Season|Last") int32 LastReputation = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Season|Last") int32 LastStars = 0;
	/** 一句改进提示（指出最拖分的项），日结卡直接显示 */
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Season|Last") FString LastHint;

	// ---- 当日统计（日结后清零） ----
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Season|Today") int32 TodayServed = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Season|Today") int32 TodayFailed = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Season|Today") int32 TodayTimeouts = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Season|Today") int32 TodayWildServed = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Season|Today") float TodayWaitSum = 0.f;

	// ---- 幕检查点（失败只回退到本幕第 1 天） ----
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Season|Checkpoint") bool bCheckpointValid = false;
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Season|Checkpoint") int32 CheckpointDay = 1;
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Season|Checkpoint") int32 CheckpointMoney = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Season|Checkpoint") int32 CheckpointReputation = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Season|Checkpoint") int32 CheckpointSeasonMoneyEarned = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Season|Checkpoint") int32 CheckpointYear = 1;
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Season|Checkpoint") int32 CheckpointMonth = 1;
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Season|Checkpoint") int32 CheckpointDayOfMonth = 1;

	// ---- 事件 ----
	UPROPERTY(BlueprintAssignable, Category = "Shoot|Season")
	FOnSeasonDailyGrade OnDailyGrade;
	UPROPERTY(BlueprintAssignable, Category = "Shoot|Season")
	FOnSeasonWarning OnSeasonWarning;
	UPROPERTY(BlueprintAssignable, Category = "Shoot|Season")
	FOnSeasonEnded OnSeasonEnded;
	UPROPERTY(BlueprintAssignable, Category = "Shoot|Season")
	FOnSeasonDayChanged OnSeasonDayChanged;

	/** 初始化：解析配置、可选给新档补起始声誉 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Season")
	void Init(UEconomySystem* InEconomy, UBusinessSimulation* InBusiness);

	/** 新游戏重置（清空赛季状态并记录第 1 幕检查点） */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Season")
	void ResetForNewGame();

	/** 订单完成上报：等待秒数、是否野味单、结算价（计入赛季收入） */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Season")
	void NotifyOrderServed(float WaitSeconds, bool bWildOrder, int32 OrderPrice);

	/** 订单超时失败上报 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Season")
	void NotifyOrderFailed();

	/** 排队超时（未接单就离场）上报：计入失败数，不重复扣声誉 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Season")
	void NotifyGuestQueueTimeout();

	/**
	 * 日结算：算日评级、连续计数、失败/胜利判定，然后推进赛季日。
	 * @param DailyNet    当日净收入（收入 - 支出）
	 * @param Cash        当前现金
	 * @param Reputation  当前声誉
	 * @param Stars       当前经营星级
	 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Season")
	void EvaluateDay(int32 DailyNet, int32 Cash, int32 Reputation, int32 Stars);

	/** 失败后回退到本幕第 1 天（日期 / 金钱 / 声誉 / 季收入都回退，解锁内容保留） */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Season")
	void RetryAct();

	/** 声誉对应的星级（只按阈值，不含服务加成） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Season")
	int32 GetStarsForReputation(int32 Reputation) const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Season")
	int32 GetDaysRemaining() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Season")
	FString GetGradeName(ESeasonGrade Grade) const;

	/** 结局名称（结局界面标题下方一行） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Season")
	FString GetEndingName(ESeasonEnding InEnding) const;

	/** 当前星级（按实时声誉换算） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Season")
	int32 GetCurrentStars() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Season")
	int32 GetMoney() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Season")
	int32 GetReputation() const;

	/** HUD 用的一行进度：第 X/48 天 · 第 N 幕 · 声誉 Y · 评级 Z */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Season")
	FString GetSeasonProgressText() const;

	/** 写入 / 读取存档（由 GameInstance 在日结与读档时调用） */
	void Save();
	void Load();

private:
	UPROPERTY(Transient)
	TObjectPtr<UEconomySystem> Economy;

	UPROPERTY(Transient)
	TObjectPtr<UBusinessSimulation> Business;

	void ResetToday();
	void CaptureCheckpoint();
	void FailSeason(const FString& Reason);
	void WarnStreak(const FString& Reason, int32 StreakDays);
	int32 TakeUnpaidBills();
};
