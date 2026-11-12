// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/SeasonEndingWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Game/BaseLevelBP.h"
#include "GameFramework/PlayerController.h"
#include "Styling/CoreStyle.h"

namespace
{
	TMap<TWeakObjectPtr<APlayerController>, TWeakObjectPtr<USeasonEndingWidget>> ActiveEndings;

	FLinearColor EndingColor(ESeasonEnding Ending)
	{
		switch (Ending)
		{
		case ESeasonEnding::Famous:      return FLinearColor(1.f, 0.82f, 0.3f, 1.f);
		case ESeasonEnding::Established: return FLinearColor(0.5f, 0.92f, 0.6f, 1.f);
		case ESeasonEnding::Reluctant:   return FLinearColor(0.55f, 0.78f, 1.f, 1.f);
		case ESeasonEnding::Failed:      return FLinearColor(0.95f, 0.34f, 0.32f, 1.f);
		default:                         return FLinearColor(0.8f, 0.8f, 0.8f, 1.f);
		}
	}
}

void USeasonEndingWidget::ShowForPlayer(APlayerController* PC, USeasonSystem* Season, bool bWon, ESeasonEnding InEnding)
{
	if (!PC || !Season)
	{
		return;
	}

	USeasonEndingWidget* Widget = nullptr;
	if (const TWeakObjectPtr<USeasonEndingWidget>* Found = ActiveEndings.Find(TWeakObjectPtr<APlayerController>(PC)))
	{
		Widget = Found->Get();
	}

	if (IsValid(Widget))
	{
		// 上一轮结局关闭时已 RemoveFromParent，复用实例必须重新挂回视口
		if (!Widget->IsInViewport())
		{
			Widget->AddToViewport(static_cast<int32>(EWidgetZOrder::SeasonEnding));
		}
	}
	else
	{
		Widget = CreateWidget<USeasonEndingWidget>(PC, USeasonEndingWidget::StaticClass());
		if (!Widget)
		{
			return;
		}
		Widget->OwnerController = PC;
		Widget->AddToViewport(static_cast<int32>(EWidgetZOrder::SeasonEnding));
		ActiveEndings.Add(TWeakObjectPtr<APlayerController>(PC), Widget);
	}

	Widget->bClosed = false;
	Widget->Apply(Season, bWon, InEnding);
	Widget->SetUIInputEnabled(true);
}

TSharedRef<SWidget> USeasonEndingWidget::RebuildWidget()
{
	if (WidgetTree->RootWidget)
	{
		return Super::RebuildWidget();
	}

	// 结局界面要接收按钮点击与键盘焦点
	SetIsFocusable(true);

	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;

	DimBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Dim"));
	DimBorder->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.72f));
	DimBorder->SetVisibility(ESlateVisibility::Visible);
	if (UCanvasPanelSlot* DimSlot = Root->AddChildToCanvas(DimBorder))
	{
		DimSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		DimSlot->SetOffsets(FMargin(0.f));
	}

	CardBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Card"));
	CardBorder->SetPadding(FMargin(38.f, 30.f, 42.f, 32.f));
	CardBorder->SetBrushColor(FLinearColor(0.045f, 0.05f, 0.06f, 0.98f));

	CardBrush.DrawAs = ESlateBrushDrawType::RoundedBox;
	CardBrush.OutlineSettings.CornerRadii = FVector4(14.f, 14.f, 14.f, 14.f);
	CardBrush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
	CardBrush.OutlineSettings.Width = 2.f;
	CardBrush.OutlineSettings.Color = FSlateColor(FLinearColor(1.f, 0.82f, 0.3f, 0.9f));
	CardBorder->SetBrush(CardBrush);

	if (UCanvasPanelSlot* CardSlot = Root->AddChildToCanvas(CardBorder))
	{
		CardSlot->SetAnchors(FAnchors(0.5f, 0.5f));
		CardSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		CardSlot->SetPosition(FVector2D(0.f, 0.f));
		CardSlot->SetAutoSize(true);
	}

	USizeBox* CardSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("CardSize"));
	CardSize->SetMinDesiredWidth(560.f);
	CardBorder->SetContent(CardSize);

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
	CardSize->AddChild(Column);

	TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Title"));
	TitleText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 13));
	TitleText->SetColorAndOpacity(FSlateColor(FLinearColor(0.68f, 0.71f, 0.76f, 1.f)));
	if (UVerticalBoxSlot* PanelSlot = Column->AddChildToVerticalBox(TitleText))
	{
		PanelSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
	}

	EndingText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Ending"));
	EndingText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 34));
	EndingText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	if (UVerticalBoxSlot* PanelSlot = Column->AddChildToVerticalBox(EndingText))
	{
		PanelSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));
	}

	SummaryText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Summary"));
	SummaryText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 14));
	SummaryText->SetColorAndOpacity(FSlateColor(FLinearColor(0.86f, 0.88f, 0.9f, 1.f)));
	SummaryText->SetAutoWrapText(true);
	SummaryText->SetWrapTextAt(520.f);
	if (UVerticalBoxSlot* PanelSlot = Column->AddChildToVerticalBox(SummaryText))
	{
		PanelSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));
	}

	DetailText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Detail"));
	DetailText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 13));
	DetailText->SetColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.74f, 0.79f, 1.f)));
	DetailText->SetAutoWrapText(true);
	DetailText->SetWrapTextAt(520.f);
	if (UVerticalBoxSlot* PanelSlot = Column->AddChildToVerticalBox(DetailText))
	{
		PanelSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 22.f));
	}

	UHorizontalBox* Buttons = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Buttons"));
	Column->AddChildToVerticalBox(Buttons);

	RetryButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("RetryButton"));
	RetryButton->SetBackgroundColor(FLinearColor(0.72f, 0.55f, 0.16f, 1.f));
	RetryButton->OnClicked.AddDynamic(this, &USeasonEndingWidget::HandleRetryClicked);
	if (UHorizontalBoxSlot* PanelSlot = Buttons->AddChildToHorizontalBox(RetryButton))
	{
		PanelSlot->SetPadding(FMargin(0.f, 0.f, 14.f, 0.f));
	}
	{
		UBorder* Box = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("RetryBox"));
		// 透明底：让按钮自身的背景色透出来，否则默认白色 Border 会盖住按钮底色
		Box->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.f));
		Box->SetPadding(FMargin(20.f, 10.f, 20.f, 10.f));
		RetryButtonText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("RetryText"));
		RetryButtonText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 15));
		RetryButtonText->SetColorAndOpacity(FSlateColor(FLinearColor(0.06f, 0.06f, 0.07f, 1.f)));
		RetryButtonText->SetJustification(ETextJustify::Center);
		Box->SetContent(RetryButtonText);
		RetryButton->SetContent(Box);
	}

	ContinueButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("ContinueButton"));
	ContinueButton->SetBackgroundColor(FLinearColor(0.22f, 0.24f, 0.28f, 1.f));
	ContinueButton->OnClicked.AddDynamic(this, &USeasonEndingWidget::HandleContinueClicked);
	Buttons->AddChildToHorizontalBox(ContinueButton);
	{
		UBorder* Box = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ContinueBox"));
		Box->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.f));
		Box->SetPadding(FMargin(20.f, 10.f, 20.f, 10.f));
		ContinueButtonText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ContinueText"));
		ContinueButtonText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 15));
		ContinueButtonText->SetColorAndOpacity(FSlateColor(FLinearColor(0.93f, 0.94f, 0.95f, 1.f)));
		ContinueButtonText->SetJustification(ETextJustify::Center);
		Box->SetContent(ContinueButtonText);
		ContinueButton->SetContent(Box);
	}

	return Super::RebuildWidget();
}

void USeasonEndingWidget::Apply(USeasonSystem* Season, bool bWon, ESeasonEnding InEnding)
{
	if (!Season)
	{
		return;
	}

	SeasonRef = Season;
	const bool bFailed = (InEnding == ESeasonEnding::Failed);
	const FLinearColor Accent = EndingColor(InEnding);

	CardBrush.OutlineSettings.Color = FSlateColor(Accent);
	if (CardBorder)
	{
		CardBorder->SetBrush(CardBrush);
	}

	if (TitleText)
	{
		const FString Title = bWon
			? TEXT("丰收祭 · 成功")
			: (bFailed ? TEXT("赛季结束 · 失败") : TEXT("丰收祭 · 勉强达成"));
		TitleText->SetText(FText::FromString(Title));
	}

	if (EndingText)
	{
		EndingText->SetText(FText::FromString(Season->GetEndingName(InEnding)));
		EndingText->SetColorAndOpacity(FSlateColor(Accent));
	}

	if (SummaryText)
	{
		SummaryText->SetText(FText::FromString(FString::Printf(TEXT("第 %d/%d 天 · 第 %d 幕 · %d 星 · 声誉 %d · 现金 %d"),
			Season->SeasonDay, Season->Config.SeasonDays, Season->Act,
			Season->GetCurrentStars(), Season->GetReputation(), Season->GetMoney())));
	}

	if (DetailText)
	{
		const bool bFestivalDay = Season->SeasonDay >= Season->Config.SeasonDays;
		const FString StatsLabel = bFestivalDay ? TEXT("丰收祭") : TEXT("当日");
		const FString FootLine = bFailed
			? TEXT("回退会恢复本幕第 1 天的金钱与声誉，已解锁内容保留。")
			: TEXT("赛季已完成，可继续自由经营。");
		DetailText->SetText(FText::FromString(FString::Printf(
			TEXT("赛季累计收入 %d / %d（S 档 %d）\n%s：服务 %d 位 · 超时 %d 次 · 野味 %d 单\n%s"),
			Season->SeasonMoneyEarned, Season->Config.SeasonMoneyTarget, Season->Config.EndingS_MoneyTarget,
			*StatsLabel, Season->LastServed, Season->LastTimeouts, Season->LastWildServed, *FootLine)));
	}

	if (RetryButton)
	{
		const bool bCanRetry = bFailed && Season->bCheckpointValid;
		RetryButton->SetVisibility(bCanRetry ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		if (bCanRetry && RetryButtonText)
		{
			const int32 CheckpointAct = FMath::Clamp(
				(Season->CheckpointDay - 1) / FMath::Max(1, Season->Config.DaysPerAct) + 1, 1, 3);
			RetryButtonText->SetText(FText::FromString(FString::Printf(TEXT("回退到第 %d 幕第 1 天"), CheckpointAct)));
		}
	}

	if (ContinueButtonText)
	{
		ContinueButtonText->SetText(FText::FromString(bFailed ? TEXT("继续经营（保留现状）") : TEXT("继续经营")));
	}
}

void USeasonEndingWidget::HandleRetryClicked()
{
	if (USeasonSystem* Season = SeasonRef.Get())
	{
		Season->RetryAct();
	}
	Close();
}

void USeasonEndingWidget::HandleContinueClicked()
{
	Close();
}

void USeasonEndingWidget::Close()
{
	if (bClosed)
	{
		return;
	}
	bClosed = true;
	SetUIInputEnabled(false);
	RemoveFromParent();
}

void USeasonEndingWidget::SetUIInputEnabled(bool bEnabled)
{
	APlayerController* PC = OwnerController.Get();
	if (!PC)
	{
		return;
	}

	if (bEnabled)
	{
		FInputModeUIOnly Mode;
		Mode.SetWidgetToFocus(TakeWidget());
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(Mode);
		PC->bShowMouseCursor = true;
	}
	else
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->bShowMouseCursor = false;
	}
}

void USeasonEndingWidget::NativeDestruct()
{
	if (APlayerController* PC = OwnerController.Get())
	{
		ActiveEndings.Remove(TWeakObjectPtr<APlayerController>(PC));
	}
	if (!bClosed)
	{
		bClosed = true;
		SetUIInputEnabled(false);
	}
	Super::NativeDestruct();
}
