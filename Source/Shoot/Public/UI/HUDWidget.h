// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Game/EconomySystem.h"
#include "Game/LevelGoalData.h"
#include "HUDWidget.generated.h"

class ABaseLevelBP;

/**
 * 常驻 HUD 基类：在编辑器里用 WBP 继承它即可显示声誉/金钱。
 * 由 ShootPlayerController 在 BeginPlay 创建并 BindEconomy，
 * 之后会自动监听经济系统的变化事件刷新界面。
 */
UCLASS()
class SHOOT_API UHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 绑定经济系统并立即刷新一次；由 PlayerController 创建后调用
	UFUNCTION(BlueprintCallable, Category = "HUD")
	void BindEconomy(UEconomySystem* Eco);

	// 绑定关卡对象（ABaseLevelBP），用于自动刷新关卡目标 UI；由 Level 创建 HUD 后调用
	UFUNCTION(BlueprintCallable, Category = "HUD")
	void BindLevel(ABaseLevelBP* Level);

	// 强制刷新 HUD 全部内容（声誉 / 金钱 / 关卡目标：描述、进度比例、明细、是否达成）。
	// 由经济、分数、声誉变化或关卡绑定/达成时自动调用。
	UFUNCTION(BlueprintCallable, Category = "HUD")
	void RefreshGoal();

	UFUNCTION(BlueprintCallable, Category = "HUD")
	int32 GetReputation() const;

	UFUNCTION(BlueprintCallable, Category = "HUD")
	int32 GetMoney() const;

	/** 显示/更新当前对准食物箱的剩余数量（Remaining<0 表示无限，会自动隐藏） */
	UFUNCTION(BlueprintCallable, Category = "HUD")
	void SetSpawnerRemaining(int32 Remaining);

	/** 隐藏食物箱剩余数量提示（对准物不是食物箱时调用） */
	UFUNCTION(BlueprintCallable, Category = "HUD")
	void HideSpawnerRemaining();

	/** 蓝图实现：刷新食物箱剩余数量显示。bVisible=false 时隐藏（如无限模式或对准物非食物箱）。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "HUD")
	void OnSpawnerRemainingChanged(int32 Remaining, bool bVisible);

	/** 刷新时钟显示：CurrentHour 为 0~24（由 DayProgress 映射），bIsOpen 为是否营业中 */
	UFUNCTION(BlueprintCallable, Category = "HUD")
	void SetClock(float CurrentHour, bool bIsOpen);

	/** 蓝图实现：显示当前时间（如 17:30）与营业状态（营业中 / 备货中 / 已打烊） */
	UFUNCTION(BlueprintImplementableEvent, Category = "HUD")
	void OnClockChanged(float CurrentHour, bool bIsOpen);

	/** 把 0~24 的浮点小时格式化为 "HH:MM"（如 17.5 -> "17:30"）；蓝图在 OnClockChanged 里调用以显示时间文本 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "HUD")
	static FString FormatClock(float CurrentHour);

	// 蓝图实现：刷新 HUD 全部内容。
	//  - Reputation：当前声誉（来自经济系统，全局持久）
	//  - MoneyEarned：本关已赚金钱（MoneyEarnedThisLevel，开局为 0，上菜才累加；非全局钱包）
	//  - Score：本关当前得分（CachedLevel->Score）
	//  - GuestsServed：本关累计服务的客人数量（GuestsServedThisLevel）
	//  - ResolvedGoal：当前关卡已解析的目标结构体（FLevelGoalRow，含分数/金钱/客人/声誉等目标值与描述；无目标时为默认空行）
	//  - GoalRatio：关卡整体完成度（0~1，取最拖后腿目标的最小比例；无目标时为 1.0）
	//  - GoalDetail：关卡目标明细文本（如"分数 30/50 · 金钱 60/100"）
	//  - bGoalAchieved：关卡目标是否已达成
	// 原 RefreshHUD 与 RefreshGoalHUD 已合并为此单一事件。
	UFUNCTION(BlueprintImplementableEvent, Category = "HUD")
	void RefreshHUD(int32 Reputation, int32 MoneyEarned, int32 Score, int32 GuestsServed, const FLevelGoalRow& ResolvedGoal, float GoalRatio, const FString& GoalDetail, bool bGoalAchieved);

	// 蓝图实现：声誉变化时播放飘字提示（Delta>0 飘 "+N 声誉"，Delta<0 飘 "-N 声誉"）。
	// 由 HandleReputationChanged 在声誉变化时自动调用，招揽/完成订单/超时扣分都会触发。
	UFUNCTION(BlueprintImplementableEvent, Category = "HUD")
	void PlayReputationFloating(int32 Delta);

	// 蓝图实现：关卡目标达成时显示通关结算面板（携带本关最终统计）。
	// 由 HandleLevelGoalCompleted 在 OnLevelGoalCompleted 广播时自动调用。
	UFUNCTION(BlueprintImplementableEvent, Category = "HUD")
	void ShowLevelComplete(int32 GuestsServed, int32 MoneyEarned, int32 Score, int32 Reputation);

	// 蓝图实现：进入下一关“打烊”过渡时显示打烊横幅与倒计时（CountdownSeconds 为总倒计时秒数）。
	// 由 HandleLevelClosing 在 OnLevelClosing 广播时自动调用。
	UFUNCTION(BlueprintImplementableEvent, Category = "HUD")
	void ShowClosingBanner(float CountdownSeconds);

protected:
	virtual void NativeDestruct() override;

	UFUNCTION()
	void HandleReputationChanged(int32 NewValue, int32 Delta);

	UFUNCTION()
	void HandleMoneyChanged(int32 NewValue, int32 Delta, EMoneySource Source);

	// 关卡分数变化回调：刷新目标 UI（服务客人 / 加分都会触发 LevelUpdateScore）
	UFUNCTION()
	void HandleLevelScoreChanged(float Score);

	// 关卡目标达成回调：转发给蓝图显示通关结算面板
	UFUNCTION()
	void HandleLevelGoalCompleted(int32 GuestsServed, int32 MoneyEarned, int32 Score, int32 Reputation);

	// 关卡打烊回调：转发给蓝图显示打烊横幅与倒计时
	UFUNCTION()
	void HandleLevelClosing(float CountdownSeconds);

	UPROPERTY()
	TObjectPtr<UEconomySystem> Economy;

	// 绑定的关卡对象，用于刷新目标进度。UPROPERTY 持有避免被 GC
	UPROPERTY()
	TObjectPtr<ABaseLevelBP> CachedLevel;
};
