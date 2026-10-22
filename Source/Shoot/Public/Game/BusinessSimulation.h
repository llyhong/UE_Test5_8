// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Game/EconomySystem.h"
#include "Game/BusinessTime.h"
#include "Game/StaffSystem.h"
#include "BusinessSimulation.generated.h"

/** 一份经营报表（日 / 月 / 年 结算时广播给 UI） */
USTRUCT(BlueprintType)
struct FBusinessReport
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Business")
	int32 Year = 1;
	UPROPERTY(BlueprintReadOnly, Category = "Business")
	int32 Month = 1;
	UPROPERTY(BlueprintReadOnly, Category = "Business")
	int32 Day = 1;

	UPROPERTY(BlueprintReadOnly, Category = "Business")
	int32 Income = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Business")
	int32 Expense = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Business")
	int32 Net = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Business")
	int32 Money = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Business")
	int32 Reputation = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Business")
	int32 Stars = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBusinessReport, const FBusinessReport&, Report);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnStarsChanged, int32, NewStars);

/**
 * 开罗式经营模拟核心（取代"关卡制"）：
 *  - 持有 时钟(UTimeSystem) + 员工(UStaffSystem) + 经济(UEconomySystem)；
 *  - 日 / 月 / 年 结算：收入(顾客消费、租金、赛事奖金) 与 支出(工资、维护、投资)；
 *  - 由声誉 + 设施 + 服务品质 计算"经营星级"(RestaurantStars)，
 *    取代原 FLevelInfo（已合并进 FLevelGoalRow）里用于关卡过关的 star 字段。
 *
 * 营收→扩建 / 研发 / 招人 的"投入"走现有 EconomySystem(BuyItem / SpendMoney)；
 * 招人→培养属性→(出餐更快 / 服务更好 / 维护更低 / 招客更多) 形成资源循环。
 */
UCLASS(Blueprintable)
class SHOOT_API UBusinessSimulation : public UObject
{
	GENERATED_BODY()

public:
	/** 时钟（Init 时自动创建） */
	UPROPERTY(BlueprintReadOnly, Category = "Business")
	TObjectPtr<UTimeSystem> TimeSystem;

	/** 员工系统（Init 时自动创建） */
	UPROPERTY(BlueprintReadOnly, Category = "Business")
	TObjectPtr<UStaffSystem> StaffSystem;

	/** 经济系统（由 GameInstance 注入） */
	UPROPERTY(BlueprintReadOnly, Category = "Business")
	TObjectPtr<UEconomySystem> Economy;

	// ---- 经营参数（也可后续迁移到数据表） ----
	/** 每日租金 / 设施被动收入 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Business", meta = (ClampMin = "0"))
	int32 DailyRentIncome = 50;
	/** 每日设施维护支出（受管理加成减免） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Business", meta = (ClampMin = "0"))
	int32 DailyMaintenance = 20;
	/** 每年赛事奖金池上限（年末随机抽取 0~该值 作为奖金） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Business", meta = (ClampMin = "0"))
	int32 YearlyPrizePool = 2000;

	// ---- 账本（滚动累计，结算时重置对应周期） ----
	UPROPERTY(BlueprintReadOnly, Category = "Business") int32 TodayIncome = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Business") int32 TodayExpense = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Business") int32 MonthIncome = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Business") int32 MonthExpense = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Business") int32 YearIncome = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Business") int32 YearExpense = 0;

	/** 经营星级（0~5），取代关卡 star */
	UPROPERTY(BlueprintReadOnly, Category = "Business")
	int32 RestaurantStars = 0;

	// ---- 事件 ----
	UPROPERTY(BlueprintAssignable, Category = "Business")
	FOnBusinessReport OnDailyReport;
	UPROPERTY(BlueprintAssignable, Category = "Business")
	FOnBusinessReport OnMonthlyReport;
	UPROPERTY(BlueprintAssignable, Category = "Business")
	FOnBusinessReport OnYearlyReport;
	UPROPERTY(BlueprintAssignable, Category = "Business")
	FOnStarsChanged OnStarsChanged;

	/** 初始化：创建时钟/员工、绑定日期事件与经济变化监听 */
	UFUNCTION(BlueprintCallable, Category = "Business")
	void Init(UEconomySystem* InEconomy);

	/** 当前星级 */
	UFUNCTION(BlueprintCallable, Category = "Business")
	int32 GetStars() const { return RestaurantStars; }

	/** 写入存档（日期 + 员工名册）。会在每次日结算时自动调用，也可手动调用。 */
	UFUNCTION(BlueprintCallable, Category = "Business")
	void Save();

	/** 从存档读取并恢复（需在 TimeSystem / StaffSystem 已创建后调用，如 LoadGameData / NewGameData） */
	UFUNCTION(BlueprintCallable, Category = "Business")
	void Load();

private:
	/** 经济金钱变化：按正负自动计入收入 / 支出账本（含被动收支与玩家消费） */
	UFUNCTION()
	void HandleMoneyChanged(int32 NewValue, int32 Delta, EMoneySource Source);

	/** 日结算：产生当日被动收支 → 广播日报表 → 重置日账本 → 重算星级 */
	UFUNCTION()
	void OnDay(int32 Y, int32 M, int32 D);
	/** 月结算：发工资 → 广播月报表 → 重置月账本 */
	UFUNCTION()
	void OnMonth(int32 Y, int32 M, int32 D);
	/** 年结算：赛事奖金 → 广播年报表 → 重置年账本 */
	UFUNCTION()
	void OnYear(int32 Y, int32 M, int32 D);

	/** 根据声誉 + 服务品质 重算经营星级（变化才广播） */
	void RecomputeStars();

	/** 组装一份当前账本快照 */
	FBusinessReport MakeReport(int32 Y, int32 M, int32 D) const;
};
