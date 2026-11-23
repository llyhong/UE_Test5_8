// Fill out your copyright notice in the Description page of Project Settings.

#include "Game/BusinessSimulation.h"
#include "Game/BaseGameInstance.h"
#include "Game/BaseLevelBP.h"
#include "Game/SeasonSystem.h"
#include "MySaveGame.h"
#include "Engine/Engine.h"

void UBusinessSimulation::Init(UEconomySystem* InEconomy)
{
	Economy = InEconomy;

	if (!TimeSystem)
	{
		// 按可配置的 TimeSystemClass 创建（蓝图子类化调参），空则回退到 UTimeSystem
		UClass* Cls = TimeSystemClass.Get();
		if (!Cls)
		{
			Cls = UTimeSystem::StaticClass();
		}
		TimeSystem = NewObject<UTimeSystem>(this, Cls);
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
	TimeSystem->OnBusinessDayEnded.AddDynamic(this, &UBusinessSimulation::HandleBusinessDayEnded);

	// 监听金钱变化，自动分桶到收入 / 支出账本
	if (Economy)
	{
		Economy->OnMoneyChanged.AddDynamic(this, &UBusinessSimulation::HandleMoneyChanged);
	}

	RecomputeStars();
}

void UBusinessSimulation::Shutdown()
{
	if (TimeSystem)
	{
		// huanglily：时钟是独立的 FTickableGameObject，只置 bAutoAdvance 仍会执行 UpdateBusinessState 自动开关门
		if (TimeSystem->bIsBusinessOpen)
		{
			TimeSystem->CloseBusiness();
		}
		TimeSystem->SetTickableTickType(ETickableTickType::Never);
		TimeSystem->bAutoAdvance = false;
		TimeSystem->bAutoOpenBusiness = false;
		TimeSystem->OnDayChanged.Clear();
		TimeSystem->OnMonthChanged.Clear();
		TimeSystem->OnYearChanged.Clear();
		TimeSystem->OnSeasonChanged.Clear();
		TimeSystem->OnBusinessOpenChanged.Clear();
		TimeSystem->OnBusinessDayEnded.Clear();
	}
	if (Economy)
	{
		Economy->OnMoneyChanged.RemoveDynamic(this, &UBusinessSimulation::HandleMoneyChanged);
	}
	SeasonSystem = nullptr;
}

void UBusinessSimulation::HandleMoneyChanged(int32 NewValue, int32 Delta, EMoneySource Source)
{
	if (Source == EMoneySource::SeasonReset || Source == EMoneySource::Debug)
	{
		return;
	}
	if (Delta < 0 && Source == EMoneySource::ShopPurchase)
	{
		TodayInvestment += -Delta;
	}
	if (Delta > 0)
	{
		TodayIncome += Delta;
		if (Source == EMoneySource::OrderComplete) TodaySales += Delta;
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

	// 经济委托返回后即保存，先同步当日真实收支，避免读档丢账。
	if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetOuter()))
	{
		if (GI->curSaveGameData)
		{
			GI->curSaveGameData->BizTodayIncome = TodayIncome;
			GI->curSaveGameData->BizTodaySales = TodaySales;
			GI->curSaveGameData->BizTodayExpense = TodayExpense;
			GI->curSaveGameData->BizTodayInvestment = TodayInvestment;
		}
	}
}

FBusinessReport UBusinessSimulation::MakeReport(int32 Y, int32 M, int32 D) const
{
	FBusinessReport R;
	R.Year = Y;
	R.Month = M;
	R.Day = D;
	R.Income = TodayIncome;
	R.Sales = TodaySales;
	if (TimeSystem && TimeSystem->IsFirstRegularKitchenService())
	{
		R.SalesTarget = UTimeSystem::FirstRegularKitchenSalesTarget;
	}
	R.Expense = TodayExpense;
	R.Net = TodayIncome - TodayExpense;
	R.Investment = TodayInvestment;
	R.OperatingNet = R.Net + R.Investment;
	R.Money = Economy ? Economy->GetMoney() : 0;
	R.Reputation = Economy ? Economy->GetReputation() : 0;
	R.HygieneReputationLoss = TodayHygieneReputationLoss;
	R.HygieneCompensation = TodayHygieneCompensation;
	R.Stars = RestaurantStars;
	return R;
}

void UBusinessSimulation::RecordHygieneLoss(int32 ReputationLoss, int32 Compensation)
{
	TodayHygieneReputationLoss += ReputationLoss;
	TodayHygieneCompensation += Compensation;
	if (UBaseGameInstance* GI = GetTypedOuter<UBaseGameInstance>(); GI && GI->curSaveGameData)
	{
		GI->curSaveGameData->BizTodayHygieneReputationLoss = TodayHygieneReputationLoss;
		GI->curSaveGameData->BizTodayHygieneCompensation = TodayHygieneCompensation;
	}
}

void UBusinessSimulation::ApplyDailyPassiveCash()
{
	if (!Economy)
	{
		return;
	}

	// 租金 / 设施收入
	Economy->AddMoney(DailyRentIncome, EMoneySource::DailyIncome);

	// 维护支出：受经理"管理"属性减免；付不出时记一笔未付账单（钱的下限是 0，不能用负数判破产）
	const int32 Maint = FMath::Max(0, DailyMaintenance - (StaffSystem ? StaffSystem->GetManagementBonus() : 0));
	if (!Economy->SpendMoney(Maint, EMoneySource::Maintenance))
	{
		Economy->AddUnpaidBill(Maint);
	}

	// 员工工资按日计提（月薪 / 30），月末不再重复发薪
	if (bAccrueWagesDaily && StaffSystem)
	{
		const int32 DailyWage = StaffSystem->GetTotalMonthlyWages() / 30;
		if (DailyWage > 0 && !Economy->SpendMoney(DailyWage, EMoneySource::Wages))
		{
			Economy->AddUnpaidBill(DailyWage);
		}
	}
}

void UBusinessSimulation::ResetDailyLedger()
{
	TodayIncome = 0;
	TodaySales = 0;
	TodayExpense = 0;
	TodayInvestment = 0;
	TodayHygieneReputationLoss = 0;
	TodayHygieneCompensation = 0;
}

void UBusinessSimulation::HandleBusinessDayEnded()
{
	// huanglily：到点打烊即当日营业结束，日结卡应在此时出现；
	// 时钟已由 UTimeSystem 暂停，等玩家点“进入下一天”再推进日期。
	if (bDaySettledAtClose || !TimeSystem)
	{
		return;
	}
	bDaySettledAtClose = true;

	ApplyDailyPassiveCash();
	RecomputeStars();

	FBusinessReport R = MakeReport(TimeSystem->Year, TimeSystem->Month, TimeSystem->Day);
	if (UBaseGameInstance* GI = GetTypedOuter<UBaseGameInstance>())
	{
		const AOrderSystem* Orders = GI->curLevelBP ? GI->curLevelBP->orderSystem.Get() : nullptr;
		if (Orders)
		{
			const FKitchenShiftProgress Shift = Orders->GetKitchenShiftProgress();
			if (Shift.IsActive() && Shift.Day == TimeSystem->GetBusinessDayNumber())
			{
				R.KitchenShift = Shift;
				if (GI->curSaveGameData)
				{
					int32& Best = GI->curSaveGameData->KitchenShiftBestStars.FindOrAdd(Shift.Day);
					Best = FMath::Max(Best, Shift.GetStars());
				}
			}
		}
	}
	if (SeasonSystem)
	{
		SeasonSystem->RecordDailyReport(R);
	}
	OnDailyReport.Broadcast(R);

	if (SeasonSystem)
	{
		SeasonSystem->EvaluateDay(R.OperatingNet, R.Money, R.Reputation, RestaurantStars, R.Investment);
	}

	ResetDailyLedger();
	Save();
}

void UBusinessSimulation::OnDay(int32 Y, int32 M, int32 D)
{
	if (UBaseGameInstance* GI = GetTypedOuter<UBaseGameInstance>())
	{
		if (GI->businessSimulation == this && GI->curLevelBP && TimeSystem)
		{
			if (AOrderSystem* Orders = GI->curLevelBP->orderSystem)
			{
				Orders->ResetOnTimeStreak();
			}
			GI->curLevelBP->RefreshKitchenPlateFlow(*TimeSystem);
		}
	}

	// 打烊时已经结算过：这次只是“进入下一天”的日期推进，不能再扣一次被动收支 / 评级
	if (bSkipNextDaySettlement)
	{
		bSkipNextDaySettlement = false;
		bDaySettledAtClose = false;
		return;
	}

	ApplyDailyPassiveCash();

	RecomputeStars();

	// 日报表（已含当日被动收支与玩家当日消费）
	FBusinessReport R = MakeReport(Y, M, D);
	if (SeasonSystem)
	{
		SeasonSystem->RecordDailyReport(R);
	}
	OnDailyReport.Broadcast(R);

	// 赛季：日评级 → 连续失败线 → 丰收祭胜负（用当日报表，账本清零前调用）
	if (SeasonSystem)
	{
		SeasonSystem->EvaluateDay(R.OperatingNet, R.Money, R.Reputation, RestaurantStars, R.Investment);
	}

	// 重置日账本
	ResetDailyLedger();

	// 日结算后自动持久化（每游戏日一次，最多丢失一天进度）
	Save();
}

void UBusinessSimulation::OnMonth(int32 Y, int32 M, int32 D)
{
	// 月末发工资：工资已改为按日计提时不重复扣
	if (Economy && StaffSystem && !bAccrueWagesDaily)
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
	if (SeasonSystem)
	{
		// 星级只由声誉决定：服务品质加成会让 3 星声誉直接跳到 5 星，使季末胜利条件失效（见文档 18.6）
		Stars = SeasonSystem->GetStarsForReputation(Rep);
	}
	else if (Rep >= 200)      Stars = 5;
	else if (Rep >= 120)      Stars = 4;
	else if (Rep >= 70)       Stars = 3;
	else if (Rep >= 30)       Stars = 2;
	else if (Rep >= 10)       Stars = 1;

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

	GI->curSaveGameData->BizTodayIncome = TodayIncome;
	GI->curSaveGameData->BizTodaySales = TodaySales;
	GI->curSaveGameData->BizTodayExpense = TodayExpense;
	GI->curSaveGameData->BizTodayInvestment = TodayInvestment;
	GI->curSaveGameData->BizTodayHygieneReputationLoss = TodayHygieneReputationLoss;
	GI->curSaveGameData->BizTodayHygieneCompensation = TodayHygieneCompensation;

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
	if (SeasonSystem)
	{
		SeasonSystem->Save();
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

	TodayIncome = GI->curSaveGameData->BizTodayIncome;
	TodaySales = GI->curSaveGameData->BizTodaySales;
	TodayExpense = GI->curSaveGameData->BizTodayExpense;
	TodayInvestment = GI->curSaveGameData->BizTodayInvestment;
	TodayHygieneReputationLoss = GI->curSaveGameData->BizTodayHygieneReputationLoss;
	TodayHygieneCompensation = GI->curSaveGameData->BizTodayHygieneCompensation;

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
