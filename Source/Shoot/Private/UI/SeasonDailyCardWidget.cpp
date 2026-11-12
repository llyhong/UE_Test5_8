// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/SeasonDailyCardWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Game/BaseLevelBP.h"
#include "GameFramework/PlayerController.h"
#include "Styling/CoreStyle.h"

namespace
{
	constexpr float CardFadeInSeconds = 0.25f;
	constexpr float CardHoldSeconds = 7.f;
	constexpr float CardFadeOutSeconds = 0.4f;

	/** 每个本地玩家只保留一张卡，避免多人同屏重复创建 */
	TMap<TWeakObjectPtr<APlayerController>, TWeakObjectPtr<USeasonDailyCardWidget>> ActiveCards;

	FLinearColor GradeColor(ESeasonGrade Grade)
	{
		switch (Grade)
		{
		case ESeasonGrade::Excellent: return FLinearColor(1.f, 0.82f, 0.3f, 1.f);
		case ESeasonGrade::Great:     return FLinearColor(0.45f, 0.9f, 0.55f, 1.f);
		case ESeasonGrade::Good:      return FLinearColor(0.45f, 0.72f, 1.f, 1.f);
		case ESeasonGrade::Fair:      return FLinearColor(1.f, 0.62f, 0.3f, 1.f);
		default:                      return FLinearColor(1.f, 0.36f, 0.33f, 1.f);
		}
	}

	FString GradeLabel(ESeasonGrade Grade)
	{
		switch (Grade)
		{
		case ESeasonGrade::Excellent: return TEXT("极佳");
		case ESeasonGrade::Great:     return TEXT("优秀");
		case ESeasonGrade::Good:      return TEXT("良好");
		case ESeasonGrade::Fair:      return TEXT("普通");
		default:                      return TEXT("不佳");
		}
	}
}

void USeasonDailyCardWidget::ShowForPlayer(APlayerController* PC, const USeasonSystem* Season)
{
	if (!PC || !Season)
	{
		return;
	}

	USeasonDailyCardWidget* Card = nullptr;
	if (const TWeakObjectPtr<USeasonDailyCardWidget>* Found = ActiveCards.Find(TWeakObjectPtr<APlayerController>(PC)))
	{
		Card = Found->Get();
	}

	if (!IsValid(Card))
	{
		Card = CreateWidget<USeasonDailyCardWidget>(PC, USeasonDailyCardWidget::StaticClass());
		if (!Card)
		{
			return;
		}
		Card->OwnerController = PC;
		Card->AddToViewport(static_cast<int32>(EWidgetZOrder::SeasonCard));
		ActiveCards.Add(TWeakObjectPtr<APlayerController>(PC), Card);
	}

	Card->Apply(Season);
	Card->Elapsed = 0.f;
	Card->bShowing = true;
	if (Card->CardBorder)
	{
		Card->CardBorder->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		Card->CardBorder->SetRenderOpacity(0.f);
		Card->CardBorder->SetRenderTranslation(FVector2D(36.f, 0.f));
	}
}

TSharedRef<SWidget> USeasonDailyCardWidget::RebuildWidget()
{
	if (WidgetTree->RootWidget)
	{
		return Super::RebuildWidget();
	}

	SetIsFocusable(false);
	SetVisibility(ESlateVisibility::HitTestInvisible);

	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;

	CardBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Card"));
	CardBorder->SetPadding(FMargin(20.f, 16.f, 24.f, 16.f));
	CardBorder->SetBrushColor(FLinearColor(0.045f, 0.05f, 0.06f, 0.95f));

	CardBrush.DrawAs = ESlateBrushDrawType::RoundedBox;
	CardBrush.OutlineSettings.CornerRadii = FVector4(12.f, 12.f, 12.f, 12.f);
	CardBrush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
	CardBrush.OutlineSettings.Width = 2.f;
	CardBrush.OutlineSettings.Color = FSlateColor(FLinearColor(0.45f, 0.72f, 1.f, 0.9f));
	CardBorder->SetBrush(CardBrush);

	if (UCanvasPanelSlot* CardSlot = Root->AddChildToCanvas(CardBorder))
	{
		// 右侧居中偏下：避开顶部 HUD 与底部订单栏
		CardSlot->SetAnchors(FAnchors(1.f, 0.5f));
		CardSlot->SetAlignment(FVector2D(1.f, 0.5f));
		CardSlot->SetPosition(FVector2D(-40.f, 60.f));
		CardSlot->SetAutoSize(true);
	}

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
	CardBorder->SetContent(Column);

	DayText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Day"));
	DayText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 13));
	DayText->SetColorAndOpacity(FSlateColor(FLinearColor(0.68f, 0.71f, 0.76f, 1.f)));
	if (UVerticalBoxSlot* PanelSlot = Column->AddChildToVerticalBox(DayText))
	{
		PanelSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
	}

	UHorizontalBox* GradeRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("GradeRow"));
	if (UVerticalBoxSlot* PanelSlot = Column->AddChildToVerticalBox(GradeRow))
	{
		PanelSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
	}

	GradeText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Grade"));
	GradeText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 58));
	GradeText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	if (UHorizontalBoxSlot* PanelSlot = GradeRow->AddChildToHorizontalBox(GradeText))
	{
		PanelSlot->SetPadding(FMargin(0.f, 0.f, 16.f, 0.f));
		PanelSlot->SetVerticalAlignment(VAlign_Center);
	}

	UVerticalBox* GradeMeta = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("GradeMeta"));
	if (UHorizontalBoxSlot* PanelSlot = GradeRow->AddChildToHorizontalBox(GradeMeta))
	{
		PanelSlot->SetVerticalAlignment(VAlign_Center);
	}

	ScoreText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Score"));
	ScoreText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 20));
	ScoreText->SetColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.95f, 0.95f, 1.f)));
	GradeMeta->AddChildToVerticalBox(ScoreText);

	GradeLabelText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("GradeLabel"));
	GradeLabelText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 13));
	GradeLabelText->SetColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.73f, 0.78f, 1.f)));
	GradeMeta->AddChildToVerticalBox(GradeLabelText);

	StatsText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Stats"));
	StatsText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 13));
	StatsText->SetColorAndOpacity(FSlateColor(FLinearColor(0.86f, 0.88f, 0.9f, 1.f)));
	StatsText->SetAutoWrapText(true);
	StatsText->SetWrapTextAt(330.f);
	if (UVerticalBoxSlot* PanelSlot = Column->AddChildToVerticalBox(StatsText))
	{
		PanelSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
	}

	NetText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Net"));
	NetText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 16));
	NetText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	if (UVerticalBoxSlot* PanelSlot = Column->AddChildToVerticalBox(NetText))
	{
		PanelSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 2.f));
	}

	SeasonText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Season"));
	SeasonText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 12));
	SeasonText->SetColorAndOpacity(FSlateColor(FLinearColor(0.62f, 0.66f, 0.72f, 1.f)));
	SeasonText->SetAutoWrapText(true);
	SeasonText->SetWrapTextAt(330.f);
	if (UVerticalBoxSlot* PanelSlot = Column->AddChildToVerticalBox(SeasonText))
	{
		PanelSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
	}

	HintText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Hint"));
	HintText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 12));
	HintText->SetColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.78f, 0.35f, 1.f)));
	HintText->SetAutoWrapText(true);
	HintText->SetWrapTextAt(330.f);
	Column->AddChildToVerticalBox(HintText);

	CardBorder->SetVisibility(ESlateVisibility::Collapsed);

	return Super::RebuildWidget();
}

void USeasonDailyCardWidget::Apply(const USeasonSystem* Season)
{
	if (!Season || !CardBorder)
	{
		return;
	}

	const ESeasonGrade Grade = Season->LastGrade;
	const FLinearColor Accent = GradeColor(Grade);

	CardBrush.OutlineSettings.Color = FSlateColor(Accent);
	CardBorder->SetBrush(CardBrush);

	if (DayText)
	{
		DayText->SetText(FText::FromString(FString::Printf(TEXT("日结算 · 第 %d/%d 天 · 第 %d 幕"),
			Season->SeasonDay, Season->Config.SeasonDays, Season->Act)));
	}

	if (GradeText)
	{
		GradeText->SetText(FText::FromString(Season->GetGradeName(Grade)));
		GradeText->SetColorAndOpacity(FSlateColor(Accent));
	}

	if (GradeLabelText)
	{
		GradeLabelText->SetText(FText::FromString(GradeLabel(Grade)));
	}

	if (ScoreText)
	{
		ScoreText->SetText(FText::FromString(FString::Printf(TEXT("%d 分"), Season->LastGradeScore)));
	}

	if (StatsText)
	{
		const FString WaitPart = Season->LastServed > 0
			? FString::Printf(TEXT("%.1fs"), Season->LastAvgWait)
			: FString(TEXT("—"));
		StatsText->SetText(FText::FromString(FString::Printf(TEXT("服务 %d · 失败 %d（超时 %d） · 野味 %d · 平均等待 %s"),
			Season->LastServed, Season->LastFailed, Season->LastTimeouts, Season->LastWildServed, *WaitPart)));
	}

	if (NetText)
	{
		NetText->SetText(FText::FromString(FString::Printf(TEXT("净收入 %+d · 现金 %d"),
			Season->LastDailyNet, Season->LastMoney)));
		NetText->SetColorAndOpacity(FSlateColor(Season->LastDailyNet >= 0
			? FLinearColor(0.55f, 0.9f, 0.6f, 1.f)
			: FLinearColor(1.f, 0.45f, 0.42f, 1.f)));
	}

	if (SeasonText)
	{
		SeasonText->SetText(FText::FromString(FString::Printf(TEXT("赛季累计收入 %d / %d · 剩余 %d 天"),
			Season->SeasonMoneyEarned, Season->Config.SeasonMoneyTarget, Season->GetDaysRemaining())));
	}

	if (HintText)
	{
		HintText->SetText(FText::FromString(Season->LastHint));
		HintText->SetVisibility(Season->LastHint.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	}
}

void USeasonDailyCardWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!bShowing || !CardBorder)
	{
		return;
	}

	Elapsed += InDeltaTime;

	float Alpha = 1.f;
	if (Elapsed < CardFadeInSeconds)
	{
		Alpha = Elapsed / CardFadeInSeconds;
	}
	else if (Elapsed > CardHoldSeconds)
	{
		Alpha = 1.f - (Elapsed - CardHoldSeconds) / CardFadeOutSeconds;
	}
	Alpha = FMath::Clamp(Alpha, 0.f, 1.f);

	CardBorder->SetRenderOpacity(Alpha);
	CardBorder->SetRenderTranslation(FVector2D((1.f - Alpha) * 36.f, 0.f));

	if (Elapsed >= CardHoldSeconds + CardFadeOutSeconds)
	{
		bShowing = false;
		CardBorder->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void USeasonDailyCardWidget::NativeDestruct()
{
	if (APlayerController* PC = OwnerController.Get())
	{
		ActiveCards.Remove(TWeakObjectPtr<APlayerController>(PC));
	}
	Super::NativeDestruct();
}
