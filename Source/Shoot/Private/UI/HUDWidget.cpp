// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/HUDWidget.h"
#include "Game/EconomySystem.h"
#include "Game/BaseLevelBP.h"

void UHUDWidget::BindEconomy(UEconomySystem* Eco)
{
	Economy = Eco;
	if (Eco)
	{
		Eco->OnReputationChanged.AddDynamic(this, &UHUDWidget::HandleReputationChanged);
		Eco->OnMoneyChanged.AddDynamic(this, &UHUDWidget::HandleMoneyChanged);
		RefreshGoal();
	}
}

void UHUDWidget::BindLevel(ABaseLevelBP* Level)
{
	// 若之前已绑定过其它关卡，先解绑旧委托，避免跨关卡残留
	if (CachedLevel && CachedLevel != Level)
	{
		CachedLevel->LevelUpdateScore.RemoveDynamic(this, &UHUDWidget::HandleLevelScoreChanged);
	}
	CachedLevel = Level;
	if (Level)
	{
		// 分数变化（加分 / 服务客人）时刷新目标进度
		Level->LevelUpdateScore.AddDynamic(this, &UHUDWidget::HandleLevelScoreChanged);
		// 目标达成时显示通关结算面板（携带本关统计）
		Level->OnLevelGoalCompleted.AddDynamic(this, &UHUDWidget::HandleLevelGoalCompleted);
		// 进入下一关“打烊”时显示打烊横幅与倒计时
		Level->OnLevelClosing.AddDynamic(this, &UHUDWidget::HandleLevelClosing);
	}
	RefreshGoal();
}

void UHUDWidget::RefreshGoal()
{
	// 统一收集经济与关卡目标数据，调用合并后的单一路由 RefreshHUD
	const int32 Rep = GetReputation();

	// 顶部“金钱”显示本关已赚金钱（MoneyEarnedThisLevel），开局为 0，上菜才累加；
	// 不显示全局持久钱包（Economy->GetMoney()，来自存档，会造成“一道菜没上却显示 30”的错觉）。
	int32 Mon = 0;
	int32 LvScore = 0;
	int32 Guests = 0;

	FLevelGoalRow ResolvedGoal;
	float GoalRatio = 1.0f;
	FString GoalDetail;
	bool bGoalAchieved = false;
	if (CachedLevel)
	{
		Mon = CachedLevel->MoneyEarnedThisLevel;
		LvScore = CachedLevel->Score;
		Guests = CachedLevel->GuestsServedThisLevel;
		ResolvedGoal = CachedLevel->ResolvedGoal;
		GoalRatio = CachedLevel->GetGoalProgressRatio();
		GoalDetail = CachedLevel->GetGoalProgressText();
		bGoalAchieved = CachedLevel->bGoalAchieved;
	}
	else
	{
		GoalDetail = TEXT("无目标");
	}

	RefreshHUD(Rep, Mon, LvScore, Guests, ResolvedGoal, GoalRatio, GoalDetail, bGoalAchieved);
}

void UHUDWidget::NativeDestruct()
{
	if (CachedLevel)
	{
		CachedLevel->LevelUpdateScore.RemoveDynamic(this, &UHUDWidget::HandleLevelScoreChanged);
		CachedLevel->OnLevelGoalCompleted.RemoveDynamic(this, &UHUDWidget::HandleLevelGoalCompleted);
		CachedLevel->OnLevelClosing.RemoveDynamic(this, &UHUDWidget::HandleLevelClosing);
		CachedLevel = nullptr;
	}
	if (Economy)
	{
		Economy->OnReputationChanged.RemoveDynamic(this, &UHUDWidget::HandleReputationChanged);
		Economy->OnMoneyChanged.RemoveDynamic(this, &UHUDWidget::HandleMoneyChanged);
		Economy = nullptr;
	}
	Super::NativeDestruct();
}

int32 UHUDWidget::GetReputation() const
{
	return Economy ? Economy->GetReputation() : 0;
}

int32 UHUDWidget::GetMoney() const
{
	return Economy ? Economy->GetMoney() : 0;
}

void UHUDWidget::HandleReputationChanged(int32 NewValue, int32 Delta)
{
	// 触发蓝图飘字（招揽/完成订单/超时扣分 均会通过此路径）
	PlayReputationFloating(Delta);
	// 声誉变化同时影响声誉/金钱显示与关卡目标进度（若目标含声誉维度）
	RefreshGoal();
}

void UHUDWidget::HandleMoneyChanged(int32 NewValue, int32 Delta, EMoneySource Source)
{
	// 金钱变化同时影响声誉/金钱显示与关卡目标进度（若目标含金钱维度）
	RefreshGoal();
}

void UHUDWidget::HandleLevelScoreChanged(float Score)
{
	// 分数（含服务客人加分）变化时刷新目标进度
	RefreshGoal();
}

void UHUDWidget::HandleLevelGoalCompleted(int32 GuestsServed, int32 MoneyEarned, int32 Score, int32 Reputation)
{
	// 转发给蓝图，显示通关结算面板（本关最终统计）
	ShowLevelComplete(GuestsServed, MoneyEarned, Score, Reputation);
}

void UHUDWidget::HandleLevelClosing(float CountdownSeconds)
{
	// 转发给蓝图，显示打烊横幅与倒计时
	ShowClosingBanner(CountdownSeconds);
}
