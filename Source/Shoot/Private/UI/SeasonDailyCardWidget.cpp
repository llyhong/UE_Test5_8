// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/SeasonDailyCardWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/NativeWidgetHost.h"
#include "Components/SizeBox.h"
#include "Engine/LocalPlayer.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElementTypes.h"
#include "Rendering/SlateRenderer.h"
#include "Widgets/SLeafWidget.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Game/BaseGameInstance.h"
#include "Game/BaseLevelBP.h"
#include "Game/OrderSystem.h"
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

	FLinearColor DailyCardColor(const TCHAR* Hex)
	{
		return FLinearColor::FromSRGBColor(FColor::FromHex(Hex));
	}

	UBorder* Panel(UWidgetTree* Tree, FName Name, const TCHAR* Fill, const TCHAR* Rim, float Radius)
	{
		UBorder* Border = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), Name);
		Border->SetBrush(FSlateRoundedBoxBrush(DailyCardColor(Fill), Radius, DailyCardColor(Rim), 2.f));
		Border->SetPadding(FMargin(18.f, 12.f));
		Border->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		return Border;
	}

	UTextBlock* Text(UWidgetTree* Tree, FName Name, const FText& Value, int32 FontSize, const TCHAR* Ink)
	{
		UTextBlock* Block = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		Block->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), FontSize));
		Block->SetText(Value);
		Block->SetColorAndOpacity(FSlateColor(DailyCardColor(Ink)));
		Block->SetVisibility(ESlateVisibility::HitTestInvisible);
		return Block;
	}

	FLinearColor GradeColor(ESeasonGrade Grade)
	{
		switch (Grade)
		{
		case ESeasonGrade::Excellent: return DailyCardColor(TEXT("A76513"));
		case ESeasonGrade::Great:     return DailyCardColor(TEXT("527D32"));
		case ESeasonGrade::Good:      return DailyCardColor(TEXT("28657F"));
		case ESeasonGrade::Fair:      return DailyCardColor(TEXT("A96B2D"));
		default:                      return DailyCardColor(TEXT("A54351"));
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

class SRevenueChart : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SRevenueChart) {}
	SLATE_END_ARGS()

	void Construct(const FArguments&)
	{
		SetVisibility(EVisibility::HitTestInvisible);
	}

	void SetRecords(const TArray<FDailyFinancialRecord>& InRecords)
	{
		Records = InRecords;
		Invalidate(EInvalidateWidgetReason::Paint);
	}

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		return FVector2D(720.f, 258.f);
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
		FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool bEnabled) const override
	{
		const FVector2f Size(Geometry.GetLocalSize());
		const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 16);
		const auto Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		const FLinearColor Ink = DailyCardColor(TEXT("75512C")) * Style.GetColorAndOpacityTint();
		const ESlateDrawEffect Effects = bEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
		auto DrawText = [&](const FString& Value, FVector2f Position)
		{
			FSlateDrawElement::MakeText(Elements, Layer + 3,
				Geometry.ToPaintGeometry(FVector2f(1.f, 1.f), FSlateLayoutTransform(Position)), Value, Font, Effects, Ink);
		};
		if (Records.IsEmpty())
		{
			const FString Empty = TEXT("暂无营业记录");
			DrawText(Empty, FVector2f((Size.X - Measure->Measure(Empty, Font).X) * 0.5f, Size.Y * 0.45f));
			return Layer + 3;
		}

		int32 Highest = 0;
		for (const FDailyFinancialRecord& Record : Records)
		{
			Highest = FMath::Max(Highest, Record.Revenue);
		}
		const double RawStep = FMath::Max(1.0, Highest / 4.0);
		const double Magnitude = FMath::Pow(10.0, FMath::FloorToDouble(FMath::LogX(10.0, RawStep)));
		const double Step = FMath::CeilToDouble(RawStep / Magnitude) * Magnitude;
		const double Maximum = Step * 4.0;
		const FString TopLabel = FText::AsNumber(static_cast<int64>(Maximum)).ToString();
		const float Left = FMath::Max(64.f, static_cast<float>(Measure->Measure(TopLabel, Font).X) + 16.f);
		const float Right = FMath::Max(Left + 1.f, Size.X - 42.f);
		const float Top = 25.f;
		const float Bottom = FMath::Max(Top + 1.f, Size.Y - 37.f);
		for (int32 Index = 0; Index <= 4; ++Index)
		{
			const float Y = Bottom - (Bottom - Top) * Index / 4.f;
			const FString Label = FText::AsNumber(static_cast<int64>(Step * Index)).ToString();
			DrawText(Label, FVector2f(Left - Measure->Measure(Label, Font).X - 12.f, Y - 10.f));
			FSlateDrawElement::MakeLines(Elements, Layer, Geometry.ToPaintGeometry(),
				TArray<FVector2f>{FVector2f(Left, Y), FVector2f(Right, Y)}, Effects,
				DailyCardColor(TEXT("EBDDCA")) * Style.GetColorAndOpacityTint(), true, 1.f);
		}

		TArray<FVector2f> Points;
		for (int32 Index = 0; Index < Records.Num(); ++Index)
		{
			const float X = Records.Num() == 1 ? (Left + Right) * 0.5f
				: Left + (Right - Left) * Index / static_cast<float>(Records.Num() - 1);
			const float Y = Bottom - (Bottom - Top) * static_cast<double>(Records[Index].Revenue) / Maximum;
			Points.Add(FVector2f(X, Y));
			const FString Label = FString::Printf(TEXT("第%d天"), Records[Index].Day);
			DrawText(Label, FVector2f(X - Measure->Measure(Label, Font).X * 0.5f, Bottom + 10.f));
		}
		if (Points.Num() > 1)
		{
			TArray<FSlateVertex> Vertices;
			TArray<SlateIndex> Indices;
			const FColor Fill = (DailyCardColor(TEXT("A4BD79")).CopyWithNewOpacity(0.19f) * Style.GetColorAndOpacityTint()).ToFColor(true);
			for (int32 Index = 1; Index < Points.Num(); ++Index)
			{
				const SlateIndex Base = static_cast<SlateIndex>(Vertices.Num());
				for (const FVector2f Point : {Points[Index - 1], Points[Index], FVector2f(Points[Index].X, Bottom), FVector2f(Points[Index - 1].X, Bottom)})
				{
					Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Geometry.GetAccumulatedRenderTransform(),
						Point, FVector2f(0.5f, 0.5f), Fill));
				}
				Indices.Append({Base, static_cast<SlateIndex>(Base + 1), static_cast<SlateIndex>(Base + 2),
					Base, static_cast<SlateIndex>(Base + 2), static_cast<SlateIndex>(Base + 3)});
			}
			FSlateDrawElement::MakeCustomVerts(Elements, Layer + 1,
				FSlateApplication::Get().GetRenderer()->GetResourceHandle(*FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))),
				Vertices, Indices, nullptr, 0, 0, Effects);
			FSlateDrawElement::MakeLines(Elements, Layer + 2, Geometry.ToPaintGeometry(), Points, Effects,
				DailyCardColor(TEXT("6D8B43")) * Style.GetColorAndOpacityTint(), true, 3.f);
		}
		for (const FVector2f Point : Points)
		{
			FSlateDrawElement::MakeBox(Elements, Layer + 3,
				Geometry.ToPaintGeometry(FVector2f(12.f, 12.f), FSlateLayoutTransform(Point - FVector2f(6.f, 6.f))),
				&PointBrush, Effects, Style.GetColorAndOpacityTint());
		}
		return Layer + 3;
	}

private:
	TArray<FDailyFinancialRecord> Records;
	FSlateRoundedBoxBrush PointBrush = FSlateRoundedBoxBrush(DailyCardColor(TEXT("A4BD79")), 6.f, DailyCardColor(TEXT("6D8B43")), 1.5f);
};

void USeasonDailyCardWidget::ShowForPlayer(APlayerController* PC, const USeasonSystem* Season, bool bAwaitNextDay)
{
	if (!PC || !PC->IsLocalController() || !Season)
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
		ActiveCards.Add(TWeakObjectPtr<APlayerController>(PC), Card);
	}
	if (!Card->IsInViewport() && !Card->AddToPlayerScreen(static_cast<int32>(EWidgetZOrder::SeasonCard)))
	{
		ActiveCards.Remove(TWeakObjectPtr<APlayerController>(PC));
		return;
	}

	Card->bInteractive = bAwaitNextDay;
	Card->Apply(Season);
	Card->Elapsed = 0.f;
	Card->HudSuppressAccum = 0.f;
	Card->bShowing = true;

	// 营业结束卡停留等待按钮；其它日结卡仍在 7 秒后自动淡出
	Card->SetVisibility(bAwaitNextDay ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::HitTestInvisible);
	if (Card->NextDayButton)
	{
		Card->NextDayButton->SetVisibility(bAwaitNextDay ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (Card->CardBorder)
	{
		Card->CardBorder->SetVisibility(bAwaitNextDay ? ESlateVisibility::Visible : ESlateVisibility::SelfHitTestInvisible);
		Card->ApplyLayout(bAwaitNextDay);
		Card->CardBorder->SetRenderOpacity(0.f);
		Card->CardBorder->SetRenderTranslation(Card->GetSlideOffset(0.f));
	}

	// 营业结束结算：屏幕上只留结算卡，其它 HUD（金钱/时钟、订单栏、任务清单、缺货横幅等）全部隐藏
	if (bAwaitNextDay)
	{
		SuppressOtherHud(PC->GetWorld());
	}
	Card->SetUIInputEnabled(bAwaitNextDay);
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

	const float Width = bCentered ? 1320.f : 640.f;
	if (USizeBox* DailyCardWidth = Cast<USizeBox>(WidgetTree->FindWidget(TEXT("CardWidth"))))
	{
		DailyCardWidth->SetWidthOverride(Width);
	}
	for (UTextBlock* Block : {StatsText, NetText, SeasonText, HintText})
	{
		Block->SetWrapTextAt(Width - 96.f);
	}
	for (const FName Name : {FName(TEXT("FinancialHeader")), FName(TEXT("FinancialBody"))})
	{
		WidgetTree->FindWidget(Name)->SetVisibility(bCentered ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
	NetText->SetVisibility(bCentered ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	for (const FName Name : {FName(TEXT("GardenBackdrop")), FName(TEXT("BackdropWash"))})
	{
		if (UWidget* Backdrop = WidgetTree->FindWidget(Name))
		{
			Backdrop->SetVisibility(bCentered ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		}
	}

	ForceLayoutPrepass();
	FVector2D ViewportSize = GetCachedGeometry().GetLocalSize();
	if (ViewportSize.X <= 1.f || ViewportSize.Y <= 1.f)
	{
		ViewportSize = UWidgetLayoutLibrary::GetViewportSize(this) / UWidgetLayoutLibrary::GetViewportScale(this);
		if (const APlayerController* PC = OwnerController.Get())
		{
			if (const ULocalPlayer* Player = PC->GetLocalPlayer())
			{
				ViewportSize *= FVector2D(Player->Size);
			}
		}
	}
	LastLayoutSize = ViewportSize;
	const FVector2D CardSize = CardBorder->GetDesiredSize();
	const double Scale = bCentered
		? FMath::Min(ViewportSize.X * 0.86 / FMath::Max(1.0, CardSize.X), ViewportSize.Y * 0.90 / FMath::Max(1.0, CardSize.Y))
		: FMath::Min3(1.0, FMath::Max(1.0, ViewportSize.X - 80.0) / FMath::Max(1.0, CardSize.X),
			FMath::Max(1.0, ViewportSize.Y - 160.0) / FMath::Max(1.0, CardSize.Y));
	CardBorder->SetRenderTransformPivot(bCentered ? FVector2D(0.5f, 0.5f) : FVector2D(1.f, 0.5f));
	CardBorder->SetRenderScale(FVector2D(Scale));
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
	for (const FName Name : {FName(TEXT("GardenBackdrop")), FName(TEXT("BackdropWash"))})
	{
		if (UWidget* Backdrop = WidgetTree->FindWidget(Name))
		{
			Backdrop->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

void USeasonDailyCardWidget::HandleNextDayClicked()
{
	if (!IsAwaitingNextDay())
	{
		return;
	}
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
		Mode.SetWidgetToFocus(NextDayButton->TakeWidget());
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(Mode);
		NextDayButton->SetUserFocus(PC);
		PC->bShowMouseCursor = true;
	}
	else
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->bShowMouseCursor = false;
		FSlateApplication::Get().SetUserFocusToGameViewport(PC->GetLocalPlayer()->GetControllerId());
	}
}

FReply USeasonDailyCardWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (!IsAwaitingNextDay())
	{
		return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
	}

	FSlateApplication& Slate = FSlateApplication::Get();
	const EUINavigationAction Action = Slate.GetNavigationActionFromKey(InKeyEvent);
	if (Action == EUINavigationAction::Accept)
	{
		if (!InKeyEvent.IsRepeat())
		{
			HandleNextDayClicked();
		}
		return FReply::Handled();
	}
	if (Action == EUINavigationAction::Back || InKeyEvent.GetKey() == EKeys::BackSpace
		|| Slate.GetNavigationDirectionFromKey(InKeyEvent) != EUINavigation::Invalid)
	{
		NextDayButton->SetUserFocus(OwnerController.Get());
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

FReply USeasonDailyCardWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (IsAwaitingNextDay())
	{
		return FReply::Handled().SetUserFocus(NextDayButton->TakeWidget(), EFocusCause::Mouse);
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

TSharedRef<SWidget> USeasonDailyCardWidget::RebuildWidget()
{
	if (WidgetTree->RootWidget)
	{
		if (UNativeWidgetHost* Host = Cast<UNativeWidgetHost>(WidgetTree->FindWidget(TEXT("RevenueChart"))); Host && RevenueChart.IsValid())
		{
			Host->SetContent(RevenueChart.ToSharedRef());
		}
		return Super::RebuildWidget();
	}

	SetIsFocusable(true);
	SetVisibility(ESlateVisibility::HitTestInvisible);

	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;

	UImage* Backdrop = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("GardenBackdrop"));
	Backdrop->SetBrushFromTexture(LoadObject<UTexture2D>(nullptr, TEXT("/Game/_Demo/Textures/Role/T_CharacterSelectGarden")));
	Backdrop->SetVisibility(ESlateVisibility::Collapsed);
	UCanvasPanelSlot* BackdropSlot = Root->AddChildToCanvas(Backdrop);
	BackdropSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
	BackdropSlot->SetOffsets(FMargin(0.f));

	UBorder* Wash = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("BackdropWash"));
	Wash->SetBrushColor(DailyCardColor(TEXT("FFF9E7")).CopyWithNewOpacity(0.28f));
	Wash->SetVisibility(ESlateVisibility::Collapsed);
	UCanvasPanelSlot* WashSlot = Root->AddChildToCanvas(Wash);
	WashSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
	WashSlot->SetOffsets(FMargin(0.f));

	CardBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Card"));
	CardBorder->SetPadding(FMargin(9.f));
	CardBrush = FSlateRoundedBoxBrush(DailyCardColor(TEXT("FFF9E7")), 29.f, DailyCardColor(TEXT("D4AA60")), 3.f);
	CardBorder->SetBrush(CardBrush);

	UCanvasPanelSlot* CardSlot = Root->AddChildToCanvas(CardBorder);
	CardSlot->SetAnchors(FAnchors(1.f, 0.5f));
	CardSlot->SetAlignment(FVector2D(1.f, 0.5f));
	CardSlot->SetPosition(FVector2D(-40.f, 60.f));
	CardSlot->SetAutoSize(true);

	USizeBox* DailyCardWidth = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("CardWidth"));
	DailyCardWidth->SetWidthOverride(360.f);
	CardBorder->SetContent(DailyCardWidth);
	UBorder* Inset = Panel(WidgetTree, TEXT("CardInset"), TEXT("FFF6E5"), TEXT("E4C68A"), 22.f);
	Inset->SetPadding(FMargin(24.f, 18.f));
	DailyCardWidth->SetContent(Inset);
	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
	Inset->SetContent(Column);

	UHorizontalBox* TitleRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	Column->AddChildToVerticalBox(TitleRow)->SetHorizontalAlignment(HAlign_Center);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		if (Index == 1)
		{
			UTextBlock* Title = Text(WidgetTree, TEXT("Title"), FText::FromString(TEXT("今日财务报表")), 40, TEXT("75512C"));
			FSlateFontInfo Font = Title->GetFont();
			Font.OutlineSettings.OutlineSize = 2;
			Font.OutlineSettings.OutlineColor = DailyCardColor(TEXT("FFF9E9"));
			Title->SetFont(Font);
			Title->SetShadowOffset(FVector2D(0.f, 2.f));
			Title->SetShadowColorAndOpacity(DailyCardColor(TEXT("E8C79C")));
			TitleRow->AddChildToHorizontalBox(Title)->SetPadding(FMargin(12.f, 0.f));
		}
		else
		{
			UImage* Leaf = WidgetTree->ConstructWidget<UImage>();
			FSlateRoundedBoxBrush LeafBrush(DailyCardColor(TEXT("A4BD49")), FVector4(2.f, 16.f, 2.f, 16.f));
			LeafBrush.ImageSize = FVector2D(15.f, 27.f);
			Leaf->SetBrush(LeafBrush);
			Leaf->SetRenderTransformAngle(Index == 0 ? -35.f : 35.f);
			Leaf->SetVisibility(ESlateVisibility::HitTestInvisible);
			TitleRow->AddChildToHorizontalBox(Leaf)->SetVerticalAlignment(VAlign_Center);
		}
	}

	DayText = Text(WidgetTree, TEXT("Day"), FText(), 20, TEXT("966C43"));
	DayText->SetJustification(ETextJustify::Center);
	Column->AddChildToVerticalBox(DayText)->SetPadding(FMargin(0.f, 4.f, 0.f, 20.f));

	UHorizontalBox* Metrics = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("FinancialHeader"));
	Column->AddChildToVerticalBox(Metrics)->SetPadding(FMargin(0.f, 0.f, 0.f, 18.f));
	const TCHAR* MetricLabels[] = {TEXT("今日营收"), TEXT("营业支出"), TEXT("营业利润"), TEXT("当前现金")};
	const TCHAR* MetricFills[] = {TEXT("E8F0D8"), TEXT("FBE8DB"), TEXT("E5F0E7"), TEXT("E2EDF8")};
	const TCHAR* MetricRims[] = {TEXT("ADBF83"), TEXT("D9B59A"), TEXT("A9BEA1"), TEXT("A5BED4")};
	UTextBlock** MetricValues[] = {&RevenueText, &ExpenseText, &ProfitText, &CashText};
	for (int32 Index = 0; Index < 4; ++Index)
	{
		UBorder* Tile = Panel(WidgetTree, *FString::Printf(TEXT("Metric%d"), Index), MetricFills[Index], MetricRims[Index], 18.f);
		Tile->SetPadding(FMargin(18.f, 14.f));
		UHorizontalBoxSlot* TileSlot = Metrics->AddChildToHorizontalBox(Tile);
		TileSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		TileSlot->SetPadding(FMargin(Index == 0 ? 0.f : 7.f, 0.f, Index == 3 ? 0.f : 7.f, 0.f));
		UVerticalBox* TileColumn = WidgetTree->ConstructWidget<UVerticalBox>();
		Tile->SetContent(TileColumn);
		TileColumn->AddChildToVerticalBox(Text(WidgetTree, NAME_None, FText::FromString(MetricLabels[Index]), 20, TEXT("75512C")));
		*MetricValues[Index] = Text(WidgetTree, *FString::Printf(TEXT("MetricValue%d"), Index), FText(), 38, TEXT("527D32"));
		TileColumn->AddChildToVerticalBox(*MetricValues[Index])->SetPadding(FMargin(0.f, 8.f, 0.f, 0.f));
	}

	UHorizontalBox* Body = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("FinancialBody"));
	Column->AddChildToVerticalBox(Body)->SetPadding(FMargin(0.f, 0.f, 0.f, 16.f));
	UBorder* ChartPanel = Panel(WidgetTree, TEXT("ChartPanel"), TEXT("FFF9EB"), TEXT("DECBA4"), 20.f);
	UHorizontalBoxSlot* ChartSlot = Body->AddChildToHorizontalBox(ChartPanel);
	FSlateChildSize ChartSize(ESlateSizeRule::Fill);
	ChartSize.Value = 0.62f;
	ChartSlot->SetSize(ChartSize);
	ChartSlot->SetPadding(FMargin(0.f, 0.f, 16.f, 0.f));
	UVerticalBox* ChartColumn = WidgetTree->ConstructWidget<UVerticalBox>();
	ChartPanel->SetContent(ChartColumn);
	ChartColumn->AddChildToVerticalBox(Text(WidgetTree, NAME_None, FText::FromString(TEXT("近期营收趋势")), 24, TEXT("75512C")))->SetPadding(FMargin(4.f, 0.f, 0.f, 8.f));
	UNativeWidgetHost* ChartHost = WidgetTree->ConstructWidget<UNativeWidgetHost>(UNativeWidgetHost::StaticClass(), TEXT("RevenueChart"));
	RevenueChart = SNew(SRevenueChart);
	ChartHost->SetContent(RevenueChart.ToSharedRef());
	USizeBox* ChartHeight = WidgetTree->ConstructWidget<USizeBox>();
	ChartHeight->SetHeightOverride(258.f);
	ChartHeight->SetContent(ChartHost);
	ChartColumn->AddChildToVerticalBox(ChartHeight);
	ChartColumn->AddChildToVerticalBox(Text(WidgetTree, NAME_None, FText::FromString(TEXT("最近最多 7 个已结算营业日 · 营收（金币）")), 15, TEXT("966C43")))->SetPadding(FMargin(4.f, 10.f, 0.f, 0.f));

	UBorder* DetailsPanel = Panel(WidgetTree, TEXT("DetailsPanel"), TEXT("FFF6E5"), TEXT("DECBA4"), 20.f);
	UHorizontalBoxSlot* DetailsSlot = Body->AddChildToHorizontalBox(DetailsPanel);
	FSlateChildSize DetailsSize(ESlateSizeRule::Fill);
	DetailsSize.Value = 0.38f;
	DetailsSlot->SetSize(DetailsSize);
	UVerticalBox* DetailsColumn = WidgetTree->ConstructWidget<UVerticalBox>();
	DetailsPanel->SetContent(DetailsColumn);
	DetailsColumn->AddChildToVerticalBox(Text(WidgetTree, NAME_None, FText::FromString(TEXT("今日收支明细")), 24, TEXT("75512C")))->SetPadding(FMargin(0.f, 0.f, 0.f, 14.f));
	const TCHAR* DetailLabels[] = {TEXT("营收"), TEXT("营业支出"), TEXT("营业利润"), TEXT("设备 / 升级投资"), TEXT("现金净增加"), TEXT("待付账单")};
	DetailValues.Reset();
	for (int32 Index = 0; Index < 6; ++Index)
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		DetailsColumn->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));
		UHorizontalBoxSlot* LabelSlot = Row->AddChildToHorizontalBox(Text(WidgetTree, NAME_None, FText::FromString(DetailLabels[Index]), 19, TEXT("75512C")));
		LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		LabelSlot->SetVerticalAlignment(VAlign_Center);
		UTextBlock* Value = Text(WidgetTree, *FString::Printf(TEXT("DetailValue%d"), Index), FText(), 21, TEXT("527D32"));
		Value->SetJustification(ETextJustify::Right);
		Row->AddChildToHorizontalBox(Value)->SetVerticalAlignment(VAlign_Center);
		DetailValues.Add(Value);
	}
	UTextBlock* Basis = Text(WidgetTree, NAME_None, FText::FromString(TEXT("实收实付口径；投资单列，不计入营业支出。")), 14, TEXT("966C43"));
	Basis->SetAutoWrapText(true);
	DetailsColumn->AddChildToVerticalBox(Basis)->SetPadding(FMargin(0.f, 2.f, 0.f, 0.f));

	NetText = Text(WidgetTree, TEXT("Net"), FText(), 21, TEXT("527D32"));
	NetText->SetAutoWrapText(true);
	Column->AddChildToVerticalBox(NetText)->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f));

	UBorder* GradePanel = Panel(WidgetTree, TEXT("GradePanel"), TEXT("F4EBCF"), TEXT("DECBA4"), 18.f);
	UHorizontalBox* GradeRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("GradeRow"));
	GradePanel->SetContent(GradeRow);
	Column->AddChildToVerticalBox(GradePanel)->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f));
	GradeText = Text(WidgetTree, TEXT("Grade"), FText(), 34, TEXT("A76513"));
	UHorizontalBoxSlot* GradeSlot = GradeRow->AddChildToHorizontalBox(GradeText);
	GradeSlot->SetPadding(FMargin(4.f, 0.f, 16.f, 0.f));
	GradeSlot->SetVerticalAlignment(VAlign_Center);
	UVerticalBox* GradeMeta = WidgetTree->ConstructWidget<UVerticalBox>();
	GradeRow->AddChildToHorizontalBox(GradeMeta)->SetVerticalAlignment(VAlign_Center);
	ScoreText = Text(WidgetTree, TEXT("Score"), FText(), 20, TEXT("A76513"));
	GradeMeta->AddChildToVerticalBox(ScoreText);
	GradeLabelText = Text(WidgetTree, TEXT("GradeLabel"), FText(), 15, TEXT("75512C"));
	GradeMeta->AddChildToVerticalBox(GradeLabelText);
	StatsText = Text(WidgetTree, TEXT("Stats"), FText(), 18, TEXT("75512C"));
	StatsText->SetAutoWrapText(true);
	UHorizontalBoxSlot* StatsSlot = GradeRow->AddChildToHorizontalBox(StatsText);
	StatsSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	StatsSlot->SetPadding(FMargin(28.f, 0.f, 0.f, 0.f));
	StatsSlot->SetVerticalAlignment(VAlign_Center);

	SeasonText = Text(WidgetTree, TEXT("Season"), FText(), 17, TEXT("966C43"));
	SeasonText->SetAutoWrapText(true);
	SeasonText->SetJustification(ETextJustify::Center);
	Column->AddChildToVerticalBox(SeasonText)->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));
	UBorder* HintPanel = Panel(WidgetTree, TEXT("HintPanel"), TEXT("FFF0CE"), TEXT("EBC995"), 16.f);
	HintText = Text(WidgetTree, TEXT("Hint"), FText(), 17, TEXT("75512C"));
	HintText->SetAutoWrapText(true);
	HintPanel->SetContent(HintText);
	Column->AddChildToVerticalBox(HintPanel);

	NextDayButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("NextDayButton"));
	FButtonStyle ButtonStyle;
	ButtonStyle.SetNormal(FSlateRoundedBoxBrush(DailyCardColor(TEXT("F6C74E")), 24.f, DailyCardColor(TEXT("FFF0AC")), 3.f));
	ButtonStyle.SetHovered(FSlateRoundedBoxBrush(DailyCardColor(TEXT("FFDD75")), 24.f, DailyCardColor(TEXT("FFF8DC")), 4.f));
	ButtonStyle.SetPressed(FSlateRoundedBoxBrush(DailyCardColor(TEXT("E5B947")), 24.f, DailyCardColor(TEXT("FFF0AC")), 3.f));
	ButtonStyle.SetNormalPadding(FMargin(8.f));
	ButtonStyle.SetPressedPadding(FMargin(8.f, 10.f, 8.f, 6.f));
	NextDayButton->SetStyle(ButtonStyle);
	for (const EUINavigation Direction : {EUINavigation::Left, EUINavigation::Right, EUINavigation::Up,
		EUINavigation::Down, EUINavigation::Next, EUINavigation::Previous})
	{
		NextDayButton->SetNavigationRuleBase(Direction, EUINavigationRule::Stop);
	}
	NextDayButton->SetVisibility(ESlateVisibility::Collapsed);
	NextDayButton->OnClicked.AddDynamic(this, &USeasonDailyCardWidget::HandleNextDayClicked);
	UBorder* ButtonInset = Panel(WidgetTree, TEXT("ButtonInset"), TEXT("FFDD75"), TEXT("FFEAA2"), 17.f);
	ButtonInset->SetPadding(FMargin(14.f, 9.f));
	ButtonInset->SetHorizontalAlignment(HAlign_Center);
	ButtonInset->SetVerticalAlignment(VAlign_Center);
	NextDayButtonText = Text(WidgetTree, TEXT("NextDayLabel"), FText::FromString(TEXT("进入下一天  ·  A / 回车 / 空格")), 23, TEXT("8D5817"));
	NextDayButtonText->SetJustification(ETextJustify::Center);
	ButtonInset->SetContent(NextDayButtonText);
	NextDayButton->SetContent(ButtonInset);
	Column->AddChildToVerticalBox(NextDayButton)->SetPadding(FMargin(0.f, 16.f, 0.f, 0.f));

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
	const UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance());
	const AOrderSystem* Orders = GI && GI->curLevelBP ? GI->curLevelBP->orderSystem.Get() : nullptr;
	const FDailyFinancialRecord* Record = Season->DailyFinancialHistory.IsEmpty() ? nullptr : &Season->DailyFinancialHistory.Last();
	const int32 ReportDay = Record ? Record->Day : Season->SeasonDay;
	const bool bTrialComplete = ReportDay == 1 && Orders && Orders->GetFirstDayServiceTarget() > 0;
	const FLinearColor Accent = GradeColor(Grade);
	const int32 Profit = Record ? Record->Revenue - Record->OperatingExpense : Season->LastDailyNet;
	const int32 Cash = Record ? Record->Cash : Season->LastMoney;
	const int32 Investment = Record ? Record->Investment : Season->LastDailyInvestment;
	const auto Number = [](int64 Value) { return FText::AsNumber(Value).ToString(); };
	const auto SignedNumber = [&Number](int64 Value)
	{
		return Value > 0 ? FString(TEXT("+")) + Number(Value) : Number(Value);
	};
	const FLinearColor ProfitInk = DailyCardColor(Profit >= 0 ? TEXT("527D32") : TEXT("A54351"));
	RevenueText->SetText(Record ? FText::AsNumber(Record->Revenue) : FText::FromString(TEXT("暂无记录")));
	ExpenseText->SetText(Record ? FText::AsNumber(Record->OperatingExpense) : FText::FromString(TEXT("暂无记录")));
	ExpenseText->SetColorAndOpacity(FSlateColor(DailyCardColor(TEXT("A96B2D"))));
	ProfitText->SetText(FText::FromString(SignedNumber(Profit)));
	ProfitText->SetColorAndOpacity(FSlateColor(ProfitInk));
	CashText->SetText(FText::AsNumber(Cash));
	CashText->SetColorAndOpacity(FSlateColor(DailyCardColor(TEXT("28657F"))));
	if (RevenueChart.IsValid())
	{
		RevenueChart->SetRecords(Season->DailyFinancialHistory);
	}
	if (DetailValues.Num() == 6)
	{
		const int64 Values[] = {Record ? Record->Revenue : 0, Record ? -static_cast<int64>(Record->OperatingExpense) : 0,
			Profit, -static_cast<int64>(Investment), Record ? static_cast<int64>(Profit) - Investment : 0,
			Record ? Record->UnpaidBills : 0};
		for (int32 Index = 0; Index < 6; ++Index)
		{
			const bool bUnknown = !Record && (Index == 0 || Index == 1 || Index == 4 || Index == 5);
			DetailValues[Index]->SetText(FText::FromString(bUnknown ? TEXT("暂无记录")
				: (Index == 5 ? Number(Values[Index]) : SignedNumber(Values[Index]))));
			DetailValues[Index]->SetColorAndOpacity(FSlateColor(DailyCardColor(
				(Index == 1 || Index == 3) ? TEXT("A96B2D")
				: ((Index == 5 && Values[Index] > 0) || Values[Index] < 0) ? TEXT("A54351") : TEXT("527D32"))));
		}
	}

	if (DayText)
	{
		DayText->SetText(FText::FromString(bTrialComplete
			? FString::Printf(TEXT("试营业完成 · %d / %d 单"), Orders->GetFirstDayOrdersServed(), Orders->GetFirstDayServiceTarget())
			: FString::Printf(TEXT("第 %d / %d 天    ·    第 %d 幕    ·    金额单位：金币"), ReportDay, Season->Config.SeasonDays,
				(ReportDay - 1) / FMath::Max(1, Season->Config.DaysPerAct) + 1)));
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
		StatsText->SetText(FText::FromString(FString::Printf(TEXT("服务 %d 位 · 失败 %d 单（超时 %d）\n野味 %d 单 · 平均等待 %s"),
			Season->LastServed, Season->LastFailed, Season->LastTimeouts, Season->LastWildServed, *WaitPart)));
	}

	if (NetText)
	{
		NetText->SetText(FText::FromString(FString::Printf(TEXT("营业利润 %s · 投资 %s\n当前现金 %s"),
			*SignedNumber(Profit), *Number(Investment), *Number(Cash))));
		NetText->SetColorAndOpacity(FSlateColor(ProfitInk));
	}

	if (SeasonText)
	{
		SeasonText->SetText(FText::FromString(FString::Printf(TEXT("赛季订单进度 %d / %d · 剩余 %d 天"),
			Season->SeasonMoneyEarned, Season->Config.SeasonMoneyTarget, Season->GetDaysRemaining())));
	}

	if (HintText)
	{
		HintText->SetText(FText::FromString(bTrialComplete
			? TEXT("试营业完成！明日新菜：番茄汤\n开门前可在研究炉选择鸡蛋与番茄汤，试试不同菜单。")
			: Season->LastHint));
		if (UWidget* HintPanel = WidgetTree->FindWidget(TEXT("HintPanel")))
		{
			HintPanel->SetVisibility(!bTrialComplete && Season->LastHint.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
		}
	}
}

void USeasonDailyCardWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!bShowing || !CardBorder)
	{
		return;
	}

	if (!LastLayoutSize.Equals(MyGeometry.GetLocalSize(), 1.f))
	{
		ApplyLayout(bInteractive);
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
