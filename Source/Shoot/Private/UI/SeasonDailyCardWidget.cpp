// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/SeasonDailyCardWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Game/BaseGameInstance.h"
#include "Game/BaseLevelBP.h"
#include "Game/BusinessSimulation.h"
#include "Game/BusinessTime.h"
#include "GameFramework/PlayerController.h"
#include "Blueprint/GameViewportSubsystem.h"
#include "UObject/UObjectIterator.h"
#include "Styling/CoreStyle.h"

namespace
{
	constexpr float CardFadeInSeconds = 0.25f;
	constexpr float CardHoldSeconds = 7.f;
	constexpr float CardFadeOutSeconds = 0.4f;
	/** 营业结束结算卡居中显示时的放大倍数（卡片内容按右侧小卡设计，放到屏幕中间需要更醒目） */
	constexpr float CenteredCardScale = 1.35f;
	/** 结算期间多久重新扫一次视口控件（新弹出的 HUD 也要藏起来） */
	constexpr float HudSuppressInterval = 0.25f;

	/** 每个本地玩家只保留一张卡，避免多人同屏重复创建 */
	TMap<TWeakObjectPtr<APlayerController>, TWeakObjectPtr<USeasonDailyCardWidget>> ActiveCards;

	/** 结算期间被本卡隐藏的视口控件 → 隐藏前的可见性（收起结算卡时原样恢复） */
	TMap<TWeakObjectPtr<UUserWidget>, ESlateVisibility> SuppressedHudWidgets;

	/** 隐藏当前世界视口里除日结卡以外的所有顶层控件（HUD / 订单栏 / 任务清单 / 缺货横幅等） */
	void SuppressOtherHud(UWorld* World)
	{
		UGameViewportSubsystem* ViewportSubsystem = UGameViewportSubsystem::Get();
		if (!World || !ViewportSubsystem)
		{
			return;
		}

		for (TObjectIterator<UUserWidget> It; It; ++It)
		{
			UUserWidget* Widget = *It;
			if (!IsValid(Widget) || Widget->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject)
				|| Widget->IsA<USeasonDailyCardWidget>() || Widget->GetWorld() != World
				|| !ViewportSubsystem->IsWidgetAdded(Widget))
			{
				continue;
			}

			const ESlateVisibility Current = Widget->GetVisibility();
			if (Current == ESlateVisibility::Collapsed || Current == ESlateVisibility::Hidden)
			{
				continue;
			}

			// 只记第一次隐藏前的状态：控件自己又把自己显示出来时再压回去，但恢复值不变
			if (!SuppressedHudWidgets.Contains(Widget))
			{
				SuppressedHudWidgets.Add(Widget, Current);
			}
			Widget->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	/** 恢复结算期间被隐藏的控件（已被移除 / 销毁的跳过） */
	void RestoreOtherHud()
	{
		for (const TPair<TWeakObjectPtr<UUserWidget>, ESlateVisibility>& Pair : SuppressedHudWidgets)
		{
			UUserWidget* Widget = Pair.Key.Get();
			if (IsValid(Widget) && Widget->GetVisibility() == ESlateVisibility::Collapsed)
			{
				Widget->SetVisibility(Pair.Value);
			}
		}
		SuppressedHudWidgets.Reset();
	}

	/** 是否还有正在等“进入下一天”的结算卡（双人分屏时两张都收起才恢复 HUD） */
	bool AnyInteractiveCardShowing()
	{
		for (const TPair<TWeakObjectPtr<APlayerController>, TWeakObjectPtr<USeasonDailyCardWidget>>& Pair : ActiveCards)
		{
			const USeasonDailyCardWidget* Card = Pair.Value.Get();
			if (IsValid(Card) && Card->IsAwaitingNextDay())
			{
				return true;
			}
		}
		return false;
	}

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

void USeasonDailyCardWidget::ShowForPlayer(APlayerController* PC, const USeasonSystem* Season, bool bAwaitNextDay)
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

	Card->bInteractive = bAwaitNextDay;
	Card->Apply(Season);
	Card->Elapsed = 0.f;
	Card->HudSuppressAccum = 0.f;
	Card->bShowing = true;
	Card->ApplyLayout(bAwaitNextDay);

	// 营业结束卡停留等待按钮；其它日结卡仍在 7 秒后自动淡出
	Card->SetVisibility(bAwaitNextDay ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::HitTestInvisible);
	Card->SetUIInputEnabled(bAwaitNextDay);
	if (Card->NextDayButton)
	{
		Card->NextDayButton->SetVisibility(bAwaitNextDay ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (Card->CardBorder)
	{
		Card->CardBorder->SetVisibility(bAwaitNextDay ? ESlateVisibility::Visible : ESlateVisibility::SelfHitTestInvisible);
		Card->CardBorder->SetRenderOpacity(0.f);
		Card->CardBorder->SetRenderTranslation(Card->GetSlideOffset(0.f));
	}

	// 营业结束结算：屏幕上只留结算卡，其它 HUD（金钱/时钟、订单栏、任务清单、缺货横幅等）全部隐藏
	if (bAwaitNextDay)
	{
		SuppressOtherHud(PC->GetWorld());
	}
}

void USeasonDailyCardWidget::ApplyLayout(bool bCentered)
{
	if (!CardBorder)
	{
		return;
	}

	if (UCanvasPanelSlot* CardSlot = Cast<UCanvasPanelSlot>(CardBorder->Slot))
	{
		if (bCentered)
		{
			// 营业结束结算：正中间
			CardSlot->SetAnchors(FAnchors(0.5f, 0.5f));
			CardSlot->SetAlignment(FVector2D(0.5f, 0.5f));
			CardSlot->SetPosition(FVector2D::ZeroVector);
		}
		else
		{
			// 其它日结卡：右侧居中偏下，避开顶部 HUD 与底部订单栏
			CardSlot->SetAnchors(FAnchors(1.f, 0.5f));
			CardSlot->SetAlignment(FVector2D(1.f, 0.5f));
			CardSlot->SetPosition(FVector2D(-40.f, 60.f));
		}
		CardSlot->SetAutoSize(true);
	}

	// 以卡片中心为轴放大，居中时更醒目；右侧卡保持原尺寸
	CardBorder->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	CardBorder->SetRenderScale(FVector2D(bCentered ? CenteredCardScale : 1.f));
}

FVector2D USeasonDailyCardWidget::GetSlideOffset(float Alpha) const
{
	const float Offset = (1.f - FMath::Clamp(Alpha, 0.f, 1.f)) * 36.f;
	return bInteractive ? FVector2D(0.f, Offset) : FVector2D(Offset, 0.f);
}

void USeasonDailyCardWidget::DismissAll()
{
	for (const TPair<TWeakObjectPtr<APlayerController>, TWeakObjectPtr<USeasonDailyCardWidget>>& Pair : ActiveCards)
	{
		if (USeasonDailyCardWidget* Card = Pair.Value.Get())
		{
			Card->Dismiss();
		}
	}

	// 结算卡全部收起：把结算期间隐藏的 HUD 恢复原样
	RestoreOtherHud();
}

void USeasonDailyCardWidget::Dismiss()
{
	bShowing = false;
	bInteractive = false;
	Elapsed = 0.f;
	SetUIInputEnabled(false);
	SetVisibility(ESlateVisibility::HitTestInvisible);
	if (CardBorder)
	{
		CardBorder->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void USeasonDailyCardWidget::HandleNextDayClicked()
{
	if (APlayerController* PC = OwnerController.Get())
	{
		if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(PC->GetGameInstance()))
		{
			if (GI->businessSimulation && GI->businessSimulation->TimeSystem)
			{
				GI->businessSimulation->TimeSystem->BeginNextBusinessDay();
			}
		}
	}
	DismissAll();
}

void USeasonDailyCardWidget::SetUIInputEnabled(bool bEnabled)
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

TSharedRef<SWidget> USeasonDailyCardWidget::RebuildWidget()
{
	if (WidgetTree->RootWidget)
	{
		return Super::RebuildWidget();
	}

	SetIsFocusable(true);
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
	if (UVerticalBoxSlot* PanelSlot = Column->AddChildToVerticalBox(HintText))
	{
		PanelSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
	}

	// 营业结束后的日结卡：停留到玩家点这个按钮才进入下一天
	NextDayButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("NextDayButton"));
	NextDayButton->SetBackgroundColor(FLinearColor(0.18f, 0.42f, 0.68f, 1.f));
	NextDayButton->SetVisibility(ESlateVisibility::Collapsed);
	NextDayButton->OnClicked.AddDynamic(this, &USeasonDailyCardWidget::HandleNextDayClicked);

	NextDayButtonText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("NextDayLabel"));
	NextDayButtonText->SetText(FText::FromString(TEXT("进入下一天")));
	NextDayButtonText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 15));
	NextDayButtonText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	NextDayButtonText->SetJustification(ETextJustify::Center);
	NextDayButton->AddChild(NextDayButtonText);

	if (UVerticalBoxSlot* PanelSlot = Column->AddChildToVerticalBox(NextDayButton))
	{
		PanelSlot->SetPadding(FMargin(0.f, 8.f, 0.f, 0.f));
		PanelSlot->SetHorizontalAlignment(HAlign_Fill);
	}

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

	// 结算期间新弹出的 HUD（打烊牌、缺货横幅、引导清单等）也要压下去
	if (bInteractive)
	{
		HudSuppressAccum += InDeltaTime;
		if (HudSuppressAccum >= HudSuppressInterval)
		{
			HudSuppressAccum = 0.f;
			SuppressOtherHud(GetWorld());
		}
	}

	float Alpha = 1.f;
	if (Elapsed < CardFadeInSeconds)
	{
		Alpha = Elapsed / CardFadeInSeconds;
	}
	else if (!bInteractive && Elapsed > CardHoldSeconds)
	{
		Alpha = 1.f - (Elapsed - CardHoldSeconds) / CardFadeOutSeconds;
	}
	Alpha = FMath::Clamp(Alpha, 0.f, 1.f);

	CardBorder->SetRenderOpacity(Alpha);
	CardBorder->SetRenderTranslation(GetSlideOffset(Alpha));

	// 营业结束卡不自动消失，必须等玩家点“进入下一天”
	if (!bInteractive && Elapsed >= CardHoldSeconds + CardFadeOutSeconds)
	{
		bShowing = false;
		CardBorder->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void USeasonDailyCardWidget::NativeDestruct()
{
	const bool bWasAwaiting = IsAwaitingNextDay();
	if (bInteractive)
	{
		SetUIInputEnabled(false);
	}
	bShowing = false;
	bInteractive = false;
	if (APlayerController* PC = OwnerController.Get())
	{
		ActiveCards.Remove(TWeakObjectPtr<APlayerController>(PC));
	}
	// 结算卡被直接销毁（切关 / 退出）且没有别的结算卡在等：恢复被隐藏的 HUD
	if (bWasAwaiting && !AnyInteractiveCardShowing())
	{
		RestoreOtherHud();
	}
	Super::NativeDestruct();
}
