// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateBrush.h"
#include "Game/SeasonSystem.h"
#include "SeasonEndingWidget.generated.h"

class UBorder;
class UButton;
class UTextBlock;
class APlayerController;

/**
 * 结局界面（C++ 构建，无需 WBP）：全屏压暗 + 中央结算卡。
 * 失败时提供“回退到本幕第 1 天”，打开期间接管 UI 输入，关闭后恢复游戏输入。
 */
UCLASS()
class SHOOT_API USeasonEndingWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	static void ShowForPlayer(APlayerController* PC, USeasonSystem* Season, bool bWon, ESeasonEnding Ending);

	virtual void NativeDestruct() override;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	UFUNCTION()
	void HandleRetryClicked();

	UFUNCTION()
	void HandleContinueClicked();

	void Apply(USeasonSystem* Season, bool bWon, ESeasonEnding InEnding);
	void Close();
	void SetUIInputEnabled(bool bEnabled);

	UPROPERTY()
	UBorder* DimBorder = nullptr;

	UPROPERTY()
	UBorder* CardBorder = nullptr;

	UPROPERTY()
	FSlateBrush CardBrush;

	UPROPERTY()
	UTextBlock* TitleText = nullptr;

	UPROPERTY()
	UTextBlock* EndingText = nullptr;

	UPROPERTY()
	UTextBlock* SummaryText = nullptr;

	UPROPERTY()
	UTextBlock* DetailText = nullptr;

	UPROPERTY()
	UButton* RetryButton = nullptr;

	UPROPERTY()
	UTextBlock* RetryButtonText = nullptr;

	UPROPERTY()
	UButton* ContinueButton = nullptr;

	UPROPERTY()
	UTextBlock* ContinueButtonText = nullptr;

	TWeakObjectPtr<APlayerController> OwnerController;
	TWeakObjectPtr<USeasonSystem> SeasonRef;

	bool bClosed = false;
};
