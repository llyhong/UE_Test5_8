// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/SeasonWarningWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Game/BaseLevelBP.h"
#include "GameFramework/PlayerController.h"
#include "Styling/CoreStyle.h"

namespace
{
	constexpr float BarFadeInSeconds = 0.18f;
	constexpr float BarHoldSeconds = 3.2f;
	constexpr float BarFadeOutSeconds = 0.3f;

	TMap<TWeakObjectPtr<APlayerController>, TWeakObjectPtr<USeasonWarningWidget>> ActiveBars;
}

void USeasonWarningWidget::ShowForPlayer(APlayerController* PC, const FString& Reason, int32 StreakDays, int32 DaysLeft)
{
	if (!PC)
	{
		return;
	}

	USeasonWarningWidget* Bar = nullptr;
	if (const TWeakObjectPtr<USeasonWarningWidget>* Found = ActiveBars.Find(TWeakObjectPtr<APlayerController>(PC)))
	{
		Bar = Found->Get();
	}

	if (!IsValid(Bar))
	{
		Bar = CreateWidget<USeasonWarningWidget>(PC, USeasonWarningWidget::StaticClass());
		if (!Bar)
		{
			return;
		}
		Bar->OwnerController = PC;
		Bar->AddToViewport(static_cast<int32>(EWidgetZOrder::SeasonWarning));
		ActiveBars.Add(TWeakObjectPtr<APlayerController>(PC), Bar);
	}

	FSeasonWarningEntry Entry;
	Entry.Reason = Reason;
	Entry.StreakDays = StreakDays;
	Entry.DaysLeft = DaysLeft;
	Bar->Enqueue(Entry);
}

TSharedRef<SWidget> USeasonWarningWidget::RebuildWidget()
{
	if (WidgetTree->RootWidget)
	{
		return Super::RebuildWidget();
	}

	SetIsFocusable(false);
	SetVisibility(ESlateVisibility::HitTestInvisible);

	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;

	BarBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Bar"));
	BarBorder->SetPadding(FMargin(22.f, 10.f, 24.f, 10.f));
	BarBorder->SetBrushColor(FLinearColor(0.24f, 0.04f, 0.045f, 0.96f));

	BarBrush.DrawAs = ESlateBrushDrawType::RoundedBox;
	BarBrush.OutlineSettings.CornerRadii = FVector4(8.f, 8.f, 8.f, 8.f);
	BarBrush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
	BarBrush.OutlineSettings.Width = 1.5f;
	BarBrush.OutlineSettings.Color = FSlateColor(FLinearColor(1.f, 0.34f, 0.3f, 0.9f));
	BarBorder->SetBrush(BarBrush);

	if (UCanvasPanelSlot* BarSlot = Root->AddChildToCanvas(BarBorder))
	{
		// 顶部居中但压在 HUD 下方，避免盖住常驻信息
		BarSlot->SetAnchors(FAnchors(0.5f, 0.f));
		BarSlot->SetAlignment(FVector2D(0.5f, 0.f));
		BarSlot->SetPosition(FVector2D(0.f, 112.f));
		BarSlot->SetAutoSize(true);
	}

	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Row"));
	BarBorder->SetContent(Row);

	UTextBlock* Kicker = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Kicker"));
	Kicker->SetText(FText::FromString(TEXT("警告")));
	Kicker->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 14));
	Kicker->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.42f, 0.38f, 1.f)));
	if (UHorizontalBoxSlot* PanelSlot = Row->AddChildToHorizontalBox(Kicker))
	{
		PanelSlot->SetPadding(FMargin(0.f, 0.f, 12.f, 0.f));
		PanelSlot->SetVerticalAlignment(VAlign_Center);
	}

	ReasonText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Reason"));
	ReasonText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 15));
	ReasonText->SetColorAndOpacity(FSlateColor(FLinearColor(0.97f, 0.95f, 0.93f, 1.f)));
	if (UHorizontalBoxSlot* PanelSlot = Row->AddChildToHorizontalBox(ReasonText))
	{
		PanelSlot->SetPadding(FMargin(0.f, 0.f, 14.f, 0.f));
		PanelSlot->SetVerticalAlignment(VAlign_Center);
	}

	DetailText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Detail"));
	DetailText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 13));
	DetailText->SetColorAndOpacity(FSlateColor(FLinearColor(0.85f, 0.72f, 0.7f, 1.f)));
	if (UHorizontalBoxSlot* PanelSlot = Row->AddChildToHorizontalBox(DetailText))
	{
		PanelSlot->SetVerticalAlignment(VAlign_Center);
	}

	BarBorder->SetVisibility(ESlateVisibility::Collapsed);

	return Super::RebuildWidget();
}

void USeasonWarningWidget::Enqueue(const FSeasonWarningEntry& Entry)
{
	Queue.Add(Entry);
	if (!bShowing)
	{
		BeginNext();
	}
}

void USeasonWarningWidget::BeginNext()
{
	if (Queue.Num() == 0)
	{
		bShowing = false;
		if (BarBorder)
		{
			BarBorder->SetVisibility(ESlateVisibility::Collapsed);
		}
		return;
	}

	const FSeasonWarningEntry Entry = Queue[0];
	Queue.RemoveAt(0);

	Apply(Entry);

	Elapsed = 0.f;
	bShowing = true;
	if (BarBorder)
	{
		BarBorder->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		BarBorder->SetRenderOpacity(0.f);
		BarBorder->SetRenderTranslation(FVector2D(0.f, -14.f));
	}
}

void USeasonWarningWidget::Apply(const FSeasonWarningEntry& Entry)
{
	if (ReasonText)
	{
		ReasonText->SetText(FText::FromString(Entry.Reason));
	}

	if (DetailText)
	{
		DetailText->SetText(FText::FromString(FString::Printf(TEXT("连续 %d 天 · 还剩 %d 天触发失败"),
			Entry.StreakDays, FMath::Max(0, Entry.DaysLeft))));
	}
}

void USeasonWarningWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!bShowing || !BarBorder)
	{
		return;
	}

	Elapsed += InDeltaTime;

	float Alpha = 1.f;
	if (Elapsed < BarFadeInSeconds)
	{
		Alpha = Elapsed / BarFadeInSeconds;
	}
	else if (Elapsed > BarHoldSeconds)
	{
		Alpha = 1.f - (Elapsed - BarHoldSeconds) / BarFadeOutSeconds;
	}
	Alpha = FMath::Clamp(Alpha, 0.f, 1.f);

	BarBorder->SetRenderOpacity(Alpha);
	BarBorder->SetRenderTranslation(FVector2D(0.f, -(1.f - Alpha) * 14.f));

	if (Elapsed >= BarHoldSeconds + BarFadeOutSeconds)
	{
		bShowing = false;
		BeginNext();
	}
}

void USeasonWarningWidget::NativeDestruct()
{
	if (APlayerController* PC = OwnerController.Get())
	{
		ActiveBars.Remove(TWeakObjectPtr<APlayerController>(PC));
	}
	Super::NativeDestruct();
}
