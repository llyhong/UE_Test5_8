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
class USoundBase;
class APlayerController;

/**
 * 星级日结卡（C++ 构建，无需 WBP）：每天打烊结算时弹出。
 * 版面从财务报表压缩为“评价”：五颗星 + 一句定性文案 + 差距提示 + 营收/利润两个数 + 2~3 条勾圈理由。
 * 星级阈值 / 档位文案 / 理由规则来自 DT_StarTiers 与 DT_StarReasons（可在编辑器里调平衡）。
 * 营业结束触发的卡显示在屏幕正中间，隐藏其它 HUD，并显示“进入下一天”按钮；
 * 其它日结卡仍从右侧滑出、7 秒后淡出。
 */
UCLASS()
class SHOOT_API USeasonDailyCardWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	USeasonDailyCardWidget(const FObjectInitializer& ObjectInitializer);

	/** 向指定本地玩家弹出一张星级日结卡（数据取自 SeasonSystem 的 Last* 快照）
	 *  @param bAwaitNextDay true = 营业结束的结算卡：不自动淡出，显示“进入下一天”按钮 */
	static void ShowForPlayer(APlayerController* PC, const USeasonSystem* Season, bool bAwaitNextDay = false);

	/** 收起所有本地玩家的日结卡（双人分屏两张卡一起消失） */
	static void DismissAll();

	/** 是否正在等“进入下一天”（此时卡片居中，其它 HUD 全部隐藏） */
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

	// ---- 结算演出：星星逐颗点亮 → 定性文案 → 理由逐条亮出 ----
	void StartReveal(const USeasonSystem* Season);
	void UpdateReveal(float DeltaTime);
	void FinishReveal();
	void ApplyFinalVisual(const USeasonSystem* Season);
	void PlayRevealCue(USoundBase* Sound, float Volume, float Pitch = 1.f) const;

	bool bRevealActive = false;
	bool bRevealFinished = false;
	float RevealTime = 0.f;
	int32 RevealStarCount = 0;
	int32 RevealStarsShown = 0;
	int32 RevealReasonsShown = 0;
	TWeakObjectPtr<USeasonSystem> RevealSeason;

	/** 摆放卡片：营业结束结算卡居中放大，其它日结卡仍贴右侧 */
	void ApplyLayout(bool bCentered);

	/** 入场 / 退场动画的位移 */
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

	/** 五颗星（★ 点亮 / ☆ 未点亮） */
	UPROPERTY()
	TArray<TObjectPtr<UTextBlock>> StarTexts;

	/** 档位 + 定性文案，如“出色 · 干得漂亮！” */
	UPROPERTY()
	UTextBlock* PhraseText = nullptr;

	/** 差距提示，如“再得 12 分升到 4 星” */
	UPROPERTY()
	UTextBlock* GapText = nullptr;

	/** 营收（保留 Widget 名 MetricValue0，自动化渲染测试据此断言） */
	UPROPERTY()
	UTextBlock* RevenueText = nullptr;

	/** 营业利润（保留 Widget 名 MetricValue2） */
	UPROPERTY()
	UTextBlock* ProfitText = nullptr;

	/** 2~3 条勾圈理由 */
	UPROPERTY()
	TArray<TObjectPtr<UTextBlock>> ReasonTexts;

	/** 结算演出音效：逐星点亮 / 达标庆祝 */
	UPROPERTY()
	USoundBase* RowTickSound = nullptr;

	UPROPERTY()
	USoundBase* WinSound = nullptr;

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

	FVector2D LastLayoutSize = FVector2D::ZeroVector;
};
