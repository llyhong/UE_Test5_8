// Fill out your copyright notice in the Description page of Project Settings.

#include "Game/SeasonSystem.h"
#include "Game/EconomySystem.h"
#include "Game/BusinessSimulation.h"
#include "Game/BaseGameInstance.h"
#include "MySaveGame.h"
#include "Kismet/GameplayStatics.h"

void USeasonSystem::Init(UEconomySystem* InEconomy, UBusinessSimulation* InBusiness)
{
	Economy = InEconomy;
	Business = InBusiness;

	Config = FSeasonConfigRow();
	if (ConfigTable)
	{
		if (const FSeasonConfigRow* Row = ConfigTable->FindRow<FSeasonConfigRow>(ConfigRowName, TEXT("SeasonSystem::Init")))
		{
			Config = *Row;
		}
	}

	// Init 时 GameInstance 尚未读档（curSaveGameData 由 BP 稍后 NewGame/Load 赋值），
	// 只有在磁盘上完全没有存档时才补起始声誉；否则读档会被顶回 100，相当于读档回血
	if (Economy && SeasonDay <= 1 && SeasonMoneyEarned <= 0
		&& !UGameplayStatics::DoesSaveGameExist(TEXT("SaveSlot_01"), 0))
	{
		const int32 Rep = Economy->GetReputation();
		if (Rep < Config.StartingReputation)
		{
			Economy->AddReputation(Config.StartingReputation - Rep);
		}
	}
}

void USeasonSystem::ResetForNewGame()
{
	SeasonDay = 1;
	Act = 1;
	SeasonMoneyEarned = 0;
	LastGrade = ESeasonGrade::Poor;
	LastGradeScore = 0;
	ConsecutivePoorDays = 0;
	ConsecutiveLowRepDays = 0;
	ConsecutiveDebtDays = 0;
	bSeasonFailed = false;
	bSeasonWon = false;
	bSeasonEnded = false;
	Ending = ESeasonEnding::None;
	LastServed = 0;
	LastFailed = 0;
	LastTimeouts = 0;
	LastWildServed = 0;
	LastAvgWait = 0.f;
	LastDailyNet = 0;
	LastMoney = 0;
	LastReputation = 0;
	LastStars = 0;
	LastHint.Reset();
	ResetToday();
	bCheckpointValid = false;

	if (Economy)
	{
		const int32 Rep = Economy->GetReputation();
		if (Rep < Config.StartingReputation)
		{
			Economy->AddReputation(Config.StartingReputation - Rep);
		}
		Economy->ClearUnpaidBills();
	}

	CaptureCheckpoint();
	OnSeasonDayChanged.Broadcast(SeasonDay, Act);
}

void USeasonSystem::NotifyOrderServed(float WaitSeconds, bool bWildOrder, int32 OrderPrice)
{
	++TodayServed;
	TodayWaitSum += FMath::Max(0.f, WaitSeconds);
	SeasonMoneyEarned += FMath::Max(0, OrderPrice);
	if (bWildOrder)
	{
		++TodayWildServed;
	}
}

void USeasonSystem::NotifyOrderFailed()
{
	++TodayFailed;
	++TodayTimeouts;
}

void USeasonSystem::NotifyGuestQueueTimeout()
{
	++TodayFailed;
}

int32 USeasonSystem::TakeUnpaidBills()
{
	return Economy ? Economy->TakeUnpaidBills() : 0;
}

void USeasonSystem::ResetToday()
{
	TodayServed = 0;
	TodayFailed = 0;
	TodayTimeouts = 0;
	TodayWildServed = 0;
	TodayWaitSum = 0.f;
}

void USeasonSystem::CaptureCheckpoint()
{
	bCheckpointValid = true;
	CheckpointDay = SeasonDay;
	CheckpointMoney = Economy ? Economy->GetMoney() : 0;
	CheckpointReputation = Economy ? Economy->GetReputation() : 0;
	CheckpointSeasonMoneyEarned = SeasonMoneyEarned;

	if (Business && Business->TimeSystem)
	{
		CheckpointYear = Business->TimeSystem->Year;
		CheckpointMonth = Business->TimeSystem->Month;
		CheckpointDayOfMonth = Business->TimeSystem->Day;
	}
}

void USeasonSystem::WarnStreak(const FString& Reason, int32 StreakDays)
{
	const int32 DaysLeft = FMath::Max(0, Config.ConsecutiveDaysToFail - StreakDays);
	OnSeasonWarning.Broadcast(Reason, StreakDays, DaysLeft);
	UE_LOG(LogTemp, Warning, TEXT("[Season] 警告：%s（连续 %d 天，还剩 %d 天触发失败）"), *Reason, StreakDays, DaysLeft);
}

void USeasonSystem::FailSeason(const FString& Reason)
{
	if (bSeasonFailed)
	{
		return;
	}
	bSeasonFailed = true;
	bSeasonWon = false;
	bSeasonEnded = true;
	Ending = ESeasonEnding::Failed;
	UE_LOG(LogTemp, Error, TEXT("[Season] 赛季失败：%s（第 %d 天）"), *Reason, SeasonDay);
	OnSeasonEnded.Broadcast(false, Ending);
}

void USeasonSystem::EvaluateDay(int32 DailyNet, int32 Cash, int32 Reputation, int32 Stars)
{
	if (bSeasonEnded)
	{
		ResetToday();
		return;
	}
	if (!bCheckpointValid)
	{
		CaptureCheckpoint();
	}

	// ---- 日评级 ----
	const int32 Total = TodayServed + TodayFailed;
	const float Completion = Total > 0 ? static_cast<float>(TodayServed) / static_cast<float>(Total) : 0.f;
	const float AvgWait = TodayServed > 0 ? TodayWaitSum / static_cast<float>(TodayServed) : 0.f;
	const float WaitTarget = FMath::Max(1.f, Config.AvgWaitTargetSeconds);

	const float CompletionScore = Config.WeightCompletion * FMath::Clamp((Completion - 0.5f) / 0.45f, 0.f, 1.f);
	const float TimeoutScore = Config.WeightTimeout * FMath::Clamp(1.f - static_cast<float>(TodayTimeouts) / 3.f, 0.f, 1.f);
	const float WaitScore = Config.WeightWait * FMath::Clamp(2.f - AvgWait / WaitTarget, 0.f, 1.f);
	const float BonusScore = TodayWildServed > 0 ? static_cast<float>(Config.WeightBonus) : 0.f;

	LastGradeScore = Total > 0 ? FMath::RoundToInt(CompletionScore + TimeoutScore + WaitScore + BonusScore) : 0;
	if (LastGradeScore >= Config.GradeS)      LastGrade = ESeasonGrade::Excellent;
	else if (LastGradeScore >= Config.GradeA) LastGrade = ESeasonGrade::Great;
	else if (LastGradeScore >= Config.GradeB) LastGrade = ESeasonGrade::Good;
	else if (LastGradeScore >= Config.GradeC) LastGrade = ESeasonGrade::Fair;
	else                                      LastGrade = ESeasonGrade::Poor;

	// 日结快照：必须在 ResetToday / 广播之前写入，日结卡在 OnDailyGrade 里同步读取
	LastServed = TodayServed;
	LastFailed = TodayFailed;
	LastTimeouts = TodayTimeouts;
	LastWildServed = TodayWildServed;
	LastAvgWait = AvgWait;
	LastDailyNet = DailyNet;
	LastMoney = Cash;
	LastReputation = Reputation;
	LastStars = Stars;
	LastHint = TEXT("今天没有营业记录");
	if (Total > 0)
	{
		if (LastGrade == ESeasonGrade::Excellent)
		{
			LastHint = TEXT("已是最高评级，保持住");
		}
		else
		{
			int32 NextThreshold = Config.GradeC;
			FString NextName = TEXT("C");
			if (LastGrade == ESeasonGrade::Fair) { NextThreshold = Config.GradeB; NextName = TEXT("B"); }
			else if (LastGrade == ESeasonGrade::Good) { NextThreshold = Config.GradeA; NextName = TEXT("A"); }
			else if (LastGrade == ESeasonGrade::Great) { NextThreshold = Config.GradeS; NextName = TEXT("S"); }

			// 取四项里丢分最多的一项给建议，玩家次日知道先改什么
			FString Tip = TEXT("提高完成率");
			float MaxMiss = Config.WeightCompletion - CompletionScore;
			if (Config.WeightTimeout - TimeoutScore > MaxMiss) { MaxMiss = Config.WeightTimeout - TimeoutScore; Tip = TEXT("减少超时"); }
			if (Config.WeightWait - WaitScore > MaxMiss) { MaxMiss = Config.WeightWait - WaitScore; Tip = TEXT("缩短平均等待"); }
			if (Config.WeightBonus - BonusScore > MaxMiss) { Tip = TEXT("用野味食材拿附加分"); }

			LastHint = FString::Printf(TEXT("再 +%d 分升 %s：%s"),
				FMath::Max(0, NextThreshold - LastGradeScore), *NextName, *Tip);
		}
	}

	OnDailyGrade.Broadcast(LastGrade, LastGradeScore, TodayServed, TodayFailed);
	UE_LOG(LogTemp, Log, TEXT("[Season] 第 %d 天结算：评级 %s（%d 分，完成 %d / 失败 %d，平均等待 %.1fs，净收入 %d）"),
		SeasonDay, *GetGradeName(LastGrade), LastGradeScore, TodayServed, TodayFailed, AvgWait, DailyNet);

	// ---- 连续计数（三条失败线） ----
	const bool bLowRep = Reputation < Config.Star1Rep;
	// 未付账单每天必须取走清零，不能放在短路表达式里，否则净收入为负时账单会滚到次日
	const int32 UnpaidBills = TakeUnpaidBills();
	const bool bDebt = DailyNet < 0 || UnpaidBills > 0;

	ConsecutivePoorDays = (LastGrade == ESeasonGrade::Poor) ? ConsecutivePoorDays + 1 : 0;
	ConsecutiveLowRepDays = bLowRep ? ConsecutiveLowRepDays + 1 : 0;
	ConsecutiveDebtDays = bDebt ? ConsecutiveDebtDays + 1 : 0;

	// ---- 失败判定（前 N 天为新手保护，只警告不判负） ----
	if (SeasonDay >= Config.FailCheckStartDay)
	{
		if (ConsecutivePoorDays >= Config.ConsecutiveDaysToFail)
		{
			FailSeason(TEXT("连续低评级"));
			ResetToday();
			return;
		}
		if (ConsecutiveLowRepDays >= Config.ConsecutiveDaysToFail)
		{
			FailSeason(TEXT("口碑崩盘"));
			ResetToday();
			return;
		}
		if (ConsecutiveDebtDays >= Config.ConsecutiveDaysToFail)
		{
			FailSeason(TEXT("入不敷出"));
			ResetToday();
			return;
		}
	}

	// 每条连续线只在第 1 天播一次警告，避免每天刷屏；新手保护期内也播，让玩家提前感知趋势
	if (ConsecutivePoorDays == 1) WarnStreak(TEXT("今日评级为 D"), ConsecutivePoorDays);
	if (ConsecutiveLowRepDays == 1) WarnStreak(TEXT("声誉跌破 1 星线"), ConsecutiveLowRepDays);
	if (ConsecutiveDebtDays == 1) WarnStreak(TEXT("今日未付食材账单"), ConsecutiveDebtDays);

	// ---- 季末丰收祭 ----
	if (SeasonDay >= Config.SeasonDays)
	{
		const bool bQuota = TodayServed >= Config.FestivalGuests
			&& TodayTimeouts <= Config.FestivalMaxTimeouts
			&& TodayWildServed >= Config.FestivalWildRequired;
		const bool bFiveStar = Stars >= 5;

		if (bQuota && bFiveStar && SeasonMoneyEarned >= Config.SeasonMoneyTarget)
		{
			bSeasonWon = true;
			Ending = (SeasonMoneyEarned >= Config.EndingS_MoneyTarget && TodayTimeouts == 0)
				? ESeasonEnding::Famous
				: ESeasonEnding::Established;
		}
		else if (bQuota && Stars >= 4)
		{
			Ending = ESeasonEnding::Reluctant;
		}
		else
		{
			bSeasonFailed = true;
			Ending = ESeasonEnding::Failed;
		}
		bSeasonEnded = true;

		UE_LOG(LogTemp, Warning, TEXT("[Season] 丰收祭结算：胜利=%d 结局=%d（达成 %d 单 / 超时 %d / 野味 %d / 5星=%d / 季收入 %d）"),
			bSeasonWon ? 1 : 0, static_cast<int32>(Ending), TodayServed, TodayTimeouts, TodayWildServed, bFiveStar ? 1 : 0, SeasonMoneyEarned);
		OnSeasonEnded.Broadcast(bSeasonWon, Ending);
		ResetToday();
		return;
	}

	// ---- 推进到次日 ----
	ResetToday();
	++SeasonDay;
	const int32 NewAct = FMath::Clamp((SeasonDay - 1) / FMath::Max(1, Config.DaysPerAct) + 1, 1, 3);
	if (NewAct != Act)
	{
		Act = NewAct;
		CaptureCheckpoint();
		UE_LOG(LogTemp, Log, TEXT("[Season] 进入第 %d 幕（第 %d 天），已记录检查点"), Act, SeasonDay);
	}
	OnSeasonDayChanged.Broadcast(SeasonDay, Act);
}

void USeasonSystem::RetryAct()
{
	if (!bSeasonFailed || !bCheckpointValid)
	{
		return;
	}

	if (Economy)
	{
		Economy->AddMoney(CheckpointMoney - Economy->GetMoney(), EMoneySource::SeasonReset);
		Economy->AddReputation(CheckpointReputation - Economy->GetReputation());
		Economy->ClearUnpaidBills();
	}
	if (Business && Business->TimeSystem)
	{
		Business->TimeSystem->Year = CheckpointYear;
		Business->TimeSystem->Month = CheckpointMonth;
		Business->TimeSystem->Day = CheckpointDayOfMonth;
		Business->TimeSystem->DayProgress = 0.f;
	}

	SeasonDay = CheckpointDay;
	Act = FMath::Clamp((SeasonDay - 1) / FMath::Max(1, Config.DaysPerAct) + 1, 1, 3);
	SeasonMoneyEarned = CheckpointSeasonMoneyEarned;
	ConsecutivePoorDays = 0;
	ConsecutiveLowRepDays = 0;
	ConsecutiveDebtDays = 0;
	bSeasonFailed = false;
	bSeasonWon = false;
	bSeasonEnded = false;
	Ending = ESeasonEnding::None;
	LastGrade = ESeasonGrade::Poor;
	LastGradeScore = 0;
	LastHint.Reset();
	ResetToday();

	// 回退后立即落盘：日结时刚保存过失败状态，不覆盖的话玩家回退后退出会读回失败档
	if (Economy)
	{
		Economy->Save();
	}
	if (Business)
	{
		Business->Save();
	}
	else
	{
		Save();
	}

	UE_LOG(LogTemp, Warning, TEXT("[Season] 已回退到第 %d 幕第 1 天（第 %d 天）"), Act, SeasonDay);
	OnSeasonDayChanged.Broadcast(SeasonDay, Act);
}

int32 USeasonSystem::GetStarsForReputation(int32 Reputation) const
{
	if (Reputation >= Config.Star5Rep) return 5;
	if (Reputation >= Config.Star4Rep) return 4;
	if (Reputation >= Config.Star3Rep) return 3;
	if (Reputation >= Config.Star2Rep) return 2;
	if (Reputation >= Config.Star1Rep) return 1;
	return 0;
}

int32 USeasonSystem::GetDaysRemaining() const
{
	return FMath::Max(0, Config.SeasonDays - SeasonDay + 1);
}

FString USeasonSystem::GetGradeName(ESeasonGrade Grade) const
{
	switch (Grade)
	{
	case ESeasonGrade::Excellent: return TEXT("S");
	case ESeasonGrade::Great:     return TEXT("A");
	case ESeasonGrade::Good:      return TEXT("B");
	case ESeasonGrade::Fair:      return TEXT("C");
	default:                      return TEXT("D");
	}
}

FString USeasonSystem::GetEndingName(ESeasonEnding InEnding) const
{
	switch (InEnding)
	{
	case ESeasonEnding::Failed:      return TEXT("经营失败");
	case ESeasonEnding::Reluctant:   return TEXT("勉强维持 · B");
	case ESeasonEnding::Established: return TEXT("站稳脚跟 · A");
	case ESeasonEnding::Famous:      return TEXT("远近闻名 · S");
	default:                         return TEXT("经营中");
	}
}

int32 USeasonSystem::GetCurrentStars() const
{
	return GetStarsForReputation(GetReputation());
}

int32 USeasonSystem::GetMoney() const
{
	return Economy ? Economy->GetMoney() : 0;
}

int32 USeasonSystem::GetReputation() const
{
	return Economy ? Economy->GetReputation() : 0;
}

FString USeasonSystem::GetSeasonProgressText() const
{
	const int32 Rep = Economy ? Economy->GetReputation() : 0;
	return FString::Printf(TEXT("第 %d/%d 天 · 第 %d 幕 · 声誉 %d · 昨日 %s"),
		SeasonDay, Config.SeasonDays, Act, Rep, *GetGradeName(LastGrade));
}

void USeasonSystem::Save()
{
	UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetOuter());
	if (!GI || !GI->curSaveGameData)
	{
		return;
	}

	UMySaveGame* Data = GI->curSaveGameData;
	Data->SeasonDay = SeasonDay;
	Data->SeasonAct = Act;
	Data->SeasonMoneyEarned = SeasonMoneyEarned;
	Data->SeasonLastGrade = static_cast<int32>(LastGrade);
	Data->SeasonPoorDays = ConsecutivePoorDays;
	Data->SeasonLowRepDays = ConsecutiveLowRepDays;
	Data->SeasonDebtDays = ConsecutiveDebtDays;
	Data->bSeasonFailed = bSeasonFailed;
	Data->bSeasonWon = bSeasonWon;
	Data->bSeasonEnded = bSeasonEnded;
	Data->SeasonEnding = static_cast<int32>(Ending);
	Data->bSeasonCheckpointValid = bCheckpointValid;
	Data->CheckpointDay = CheckpointDay;
	Data->CheckpointMoney = CheckpointMoney;
	Data->CheckpointReputation = CheckpointReputation;
	Data->CheckpointSeasonMoneyEarned = CheckpointSeasonMoneyEarned;
	Data->CheckpointYear = CheckpointYear;
	Data->CheckpointMonth = CheckpointMonth;
	Data->CheckpointDayOfMonth = CheckpointDayOfMonth;
}

void USeasonSystem::Load()
{
	UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetOuter());
	if (!GI || !GI->curSaveGameData)
	{
		return;
	}

	const UMySaveGame* Data = GI->curSaveGameData;
	SeasonDay = FMath::Clamp(Data->SeasonDay, 1, FMath::Max(1, Config.SeasonDays));
	Act = FMath::Clamp(Data->SeasonAct, 1, 3);
	SeasonMoneyEarned = Data->SeasonMoneyEarned;
	LastGrade = static_cast<ESeasonGrade>(FMath::Clamp(Data->SeasonLastGrade, 0, 4));
	ConsecutivePoorDays = Data->SeasonPoorDays;
	ConsecutiveLowRepDays = Data->SeasonLowRepDays;
	ConsecutiveDebtDays = Data->SeasonDebtDays;
	bSeasonFailed = Data->bSeasonFailed;
	bSeasonWon = Data->bSeasonWon;
	bSeasonEnded = Data->bSeasonEnded;
	Ending = static_cast<ESeasonEnding>(FMath::Clamp(Data->SeasonEnding, 0, 4));
	bCheckpointValid = Data->bSeasonCheckpointValid;
	CheckpointDay = Data->CheckpointDay;
	CheckpointMoney = Data->CheckpointMoney;
	CheckpointReputation = Data->CheckpointReputation;
	CheckpointSeasonMoneyEarned = Data->CheckpointSeasonMoneyEarned;
	CheckpointYear = Data->CheckpointYear;
	CheckpointMonth = Data->CheckpointMonth;
	CheckpointDayOfMonth = Data->CheckpointDayOfMonth;
	ResetToday();
}
