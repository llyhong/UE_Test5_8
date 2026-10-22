// Fill out your copyright notice in the Description page of Project Settings.

#include "Game/BusinessSimulation.h"
#include "Game/BaseGameInstance.h"
#include "Engine/Engine.h"

void UBusinessSimulation::Init(UEconomySystem* InEconomy)
{
	Economy = InEconomy;

	if (!TimeSystem)
	{
		TimeSystem = NewObject<UTimeSystem>(this);
	}
	if (!StaffSystem)
	{
		StaffSystem = NewObject<UStaffSystem>(this);
	}
	TimeSystem->Init();

	// 绑定日期推进事件
	TimeSystem->OnDayChanged.AddDynamic(this, &UBusinessSimulation::OnDay);
	TimeSystem->OnMonthChanged.AddDynamic(this, &UBusinessSimulation::OnMonth);
	TimeSystem->OnYearChanged.AddDynamic(this, &UBusinessSimulation::OnYear);

	// 监听金钱变化，自动分桶到收入 / 支出账本
	if (Economy)
	{
		Economy->OnMoneyChanged.AddDynamic(this, &UBusinessSimulation::HandleMoneyChanged);
	}

	RecomputeStars();
}

void UBusinessSimulation::HandleMoneyChanged(int32 NewValue, int32 Delta, EMoneySource Source)
{
	if (Delta > 0)
	{
		TodayIncome += Delta;
		MonthIncome += Delta;
		YearIncome  += Delta;
	}
	else if (Delta < 0)
	{
		const int32 Abs = -Delta;
		TodayExpense += Abs;
		MonthExpense += Abs;
		YearExpense  += Abs;
	}
}

FBusinessReport UBusinessSimulation::MakeReport(int32 Y, int32 M, int32 D) const
{
	FBusinessReport R;
	R.Year = Y;
	R.Month = M;
	R.Day = D;
	R.Income = TodayIncome;
	R.Expense = TodayExpense;
	R.Net = TodayIncome - TodayExpense;
	R.Money = Economy ? Economy->GetMoney() : 0;
	R.Reputation = Economy ? Economy->GetReputation() : 0;
	R.Stars = RestaurantStars;
	return R;
}

void UBusinessSimulation::OnDay(int32 Y, int32 M, int32 D)
{
	// —— 当日被动收支（属于刚结束的这一天） ——
	if (Economy)
	{
		// 租金 / 设施收入
		Economy->AddMoney(DailyRentIncome, EMoneySource::DailyIncome);

		// 维护支出：受经理"管理"属性减免
		const int32 Maint = FMath::Max(0, DailyMaintenance - (StaffSystem ? StaffSystem->GetManagementBonus() : 0));
		Economy->SpendMoney(Maint, EMoneySource::Maintenance);
	}

	// 日报表（已含当日被动收支与玩家当日消费）
	FBusinessReport R = MakeReport(Y, M, D);
	OnDailyReport.Broadcast(R);

	// 重置日账本
	TodayIncome = 0;
	TodayExpense = 0;

	RecomputeStars();

	// 日结算后自动持久化（每游戏日一次，最多丢失一天进度）
	Save();
}

void UBusinessSimulation::OnMonth(int32 Y, int32 M, int32 D)
{
	// 月末发工资
	if (Economy && StaffSystem)
	{
		Economy->SpendMoney(StaffSystem->GetTotalMonthlyWages(), EMoneySource::Wages);
	}

	FBusinessReport R;
	R.Year = Y; R.Month = M; R.Day = D;
	R.Income = MonthIncome;
	R.Expense = MonthExpense;
	R.Net = MonthIncome - MonthExpense;
	R.Money = Economy ? Economy->GetMoney() : 0;
	R.Reputation = Economy ? Economy->GetReputation() : 0;
	R.Stars = RestaurantStars;
	OnMonthlyReport.Broadcast(R);

	MonthIncome = 0;
	MonthExpense = 0;
}

void UBusinessSimulation::OnYear(int32 Y, int32 M, int32 D)
{
	// 年末赛事奖金（随机抽取）
	if (Economy && YearlyPrizePool > 0)
	{
		const int32 Prize = FMath::RandRange(0, YearlyPrizePool);
		Economy->AddMoney(Prize, EMoneySource::YearlyPrize);
	}

	FBusinessReport R;
	R.Year = Y; R.Month = M; R.Day = D;
	R.Income = YearIncome;
	R.Expense = YearExpense;
	R.Net = YearIncome - YearExpense;
	R.Money = Economy ? Economy->GetMoney() : 0;
	R.Reputation = Economy ? Economy->GetReputation() : 0;
	R.Stars = RestaurantStars;
	OnYearlyReport.Broadcast(R);

	YearIncome = 0;
	YearExpense = 0;
}

void UBusinessSimulation::RecomputeStars()
{
	if (!Economy)
	{
		return;
	}

	const int32 Rep = Economy->GetReputation();
	int32 Stars = 0;
	if (Rep >= 200)      Stars = 5;
	else if (Rep >= 120) Stars = 4;
	else if (Rep >= 70)  Stars = 3;
	else if (Rep >= 30)  Stars = 2;
	else if (Rep >= 10)  Stars = 1;

	// 服务品质加成（最多 +2 星）：服务属性之和每 20 点 +1 星
	if (StaffSystem)
	{
		const float Service = StaffSystem->GetServiceQuality();
		Stars += FMath::Clamp(FMath::FloorToInt(Service / 20.f), 0, 2);
	}
	Stars = FMath::Clamp(Stars, 0, 5);

	if (Stars != RestaurantStars)
	{
		RestaurantStars = Stars;
		OnStarsChanged.Broadcast(Stars);
	}
}

void UBusinessSimulation::Save()
{
	UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetOuter());
	if (!GI || !GI->curSaveGameData)
	{
		return;
	}

	if (TimeSystem)
	{
		GI->curSaveGameData->BizYear  = TimeSystem->Year;
		GI->curSaveGameData->BizMonth = TimeSystem->Month;
		GI->curSaveGameData->BizDay   = TimeSystem->Day;
	}
	if (StaffSystem)
	{
		GI->curSaveGameData->BizRoster = StaffSystem->Roster;
	}

	GI->SaveGameData();
}

void UBusinessSimulation::Load()
{
	UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetOuter());
	if (!GI || !GI->curSaveGameData)
	{
		return;
	}

	if (TimeSystem)
	{
		TimeSystem->Year  = GI->curSaveGameData->BizYear;
		TimeSystem->Month = GI->curSaveGameData->BizMonth;
		TimeSystem->Day   = GI->curSaveGameData->BizDay;
	}
	if (StaffSystem)
	{
		StaffSystem->Roster = GI->curSaveGameData->BizRoster;
	}

	RecomputeStars();
}
