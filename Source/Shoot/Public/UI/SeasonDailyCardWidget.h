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

/**
 * 日结卡（C++ 构建，无需 WBP）：每天打烊结算时从右侧滑出。
 * 显示日评级 / 分数 / 当日统计 / 净收入 / 改进提示。
 * 营业结束触发的结算卡会停留并显示“进入下一天”按钮，点击后才推进日期。
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

	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void NativeDestruct() override;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	void Apply(const USeasonSystem* Season);
	void SetUIInputEnabled(bool bEnabled);
	void Dismiss();

	UFUNCTION()
	void HandleNextDayClicked();

	UPROPERTY()
	UBorder* CardBorder = nullptr;

	/** 卡片底刷：描边颜色随评级改，需要保留可改副本 */
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
};
