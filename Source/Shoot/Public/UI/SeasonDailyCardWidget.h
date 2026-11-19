// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateBrush.h"
#include "Game/SeasonSystem.h"
#include "SeasonDailyCardWidget.generated.h"

class UBorder;
class UTextBlock;
class UButton;
class APlayerController;
class SRevenueChart;

/**
 * 日结卡（C++ 构建，无需 WBP）：每天打烊结算时弹出。
 * 显示日评级 / 分数 / 当日统计 / 净收入 / 改进提示。
 * 营业结束触发的结算卡显示在屏幕正中间，期间隐藏其它所有 HUD 面板，
 * 并显示“进入下一天”按钮，点击后才推进日期并恢复 HUD；其它日结卡仍从右侧滑出、7 秒后淡出。
 */
UCLASS()
class SHOOT_API USeasonDailyCardWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 向指定本地玩家弹出一张日结卡（数据取自 SeasonSystem 的 Last* 快照）
	 *  @param bAwaitNextDay true = 营业结束的日结卡：不自动淡出，显示“进入下一天”按钮 */
	static void ShowForPlayer(APlayerController* PC, const USeasonSystem* Season, bool bAwaitNextDay = false);

	/** 收起所有本地玩家的日结卡（进入下一天时调用，双人分屏两张卡一起消失） */
	static void DismissAll();

	/** 是否是正在等“进入下一天”的营业结束结算卡（此时卡片居中，其它 HUD 全部隐藏） */
	bool IsAwaitingNextDay() const { return bShowing && bInteractive; }

	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void NativeDestruct() override;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	void Apply(const USeasonSystem* Season);
	void SetUIInputEnabled(bool bEnabled);
	void Dismiss();

	/** 摆放卡片：营业结束结算卡居中放大，其它日结卡仍贴右侧 */
	void ApplyLayout(bool bCentered);

	/** 入场 / 退场动画的位移：居中卡从下往上浮，右侧卡从右往左滑 */
	FVector2D GetSlideOffset(float Alpha) const;

	UFUNCTION()
	void HandleNextDayClicked();

	UPROPERTY()
	UBorder* CardBorder = nullptr;

	/** 奶油纸卡与蜂蜜金描边。 */
	UPROPERTY()
	FSlateBrush CardBrush;

	UPROPERTY()
	UTextBlock* DayText = nullptr;

	UPROPERTY()
	UTextBlock* GradeText = nullptr;

	UPROPERTY()
	UTextBlock* GradeLabelText = nullptr;

	UPROPERTY()
	UTextBlock* ScoreText = nullptr;

	UPROPERTY()
	UTextBlock* StatsText = nullptr;

	UPROPERTY()
	UTextBlock* NetText = nullptr;

	UPROPERTY()
	UTextBlock* RevenueText = nullptr;

	UPROPERTY()
	UTextBlock* ExpenseText = nullptr;

	UPROPERTY()
	UTextBlock* ProfitText = nullptr;

	UPROPERTY()
	UTextBlock* CashText = nullptr;

	UPROPERTY()
	TArray<TObjectPtr<UTextBlock>> DetailValues;

	TSharedPtr<SRevenueChart> RevenueChart;
	FVector2D LastLayoutSize = FVector2D::ZeroVector;

	UPROPERTY()
	UTextBlock* SeasonText = nullptr;

	UPROPERTY()
	UTextBlock* HintText = nullptr;

	UPROPERTY()
	UButton* NextDayButton = nullptr;

	UPROPERTY()
	UTextBlock* NextDayButtonText = nullptr;

	TWeakObjectPtr<APlayerController> OwnerController;

	bool bShowing = false;
	bool bInteractive = false;
	float Elapsed = 0.f;

	/** 结算期间重新隐藏其它 HUD 的计时 */
	float HudSuppressAccum = 0.f;
};
