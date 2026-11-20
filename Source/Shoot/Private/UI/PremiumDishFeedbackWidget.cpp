#include "UI/PremiumDishFeedbackWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "HAL/PlatformTime.h"
#include "Styling/CoreStyle.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr float RevealDuration = 0.8f;
	const FLinearColor Cream(1.f, 0.93f, 0.72f);
	const FLinearColor Cocoa(0.18f, 0.08f, 0.025f, 0.94f);

	void PlaceOnCanvas(UCanvasPanel& Canvas, UWidget& Widget, FVector2D Position, FVector2D Size)
	{
		UCanvasPanelSlot* Slot = Canvas.AddChildToCanvas(&Widget);
		Slot->SetPosition(Position);
		Slot->SetSize(Size);
	}

	FSlateBrush MakeBadgeBrush(float Radius)
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.TintColor = FSlateColor(Cocoa);
		Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		Brush.OutlineSettings.CornerRadii = FVector4(Radius, Radius, Radius, Radius);
		Brush.OutlineSettings.Color = FSlateColor(FLinearColor(0.95f, 0.55f, 0.12f, 0.95f));
		Brush.OutlineSettings.Width = 1.f;
		return Brush;
	}
}

UPremiumDishFeedbackWidget::UPremiumDishFeedbackWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	static ConstructorHelpers::FObjectFinder<UTexture2D> StarFinder(
		TEXT("/Game/_Demo/Textures/UI/T_YellowStar.T_YellowStar"));
	StarTexture = StarFinder.Object;
}

TSharedRef<SWidget> UPremiumDishFeedbackWidget::RebuildWidget()
{
	if (WidgetTree->RootWidget)
	{
		return Super::RebuildWidget();
	}

	SetIsFocusable(false);
	SetVisibility(ESlateVisibility::HitTestInvisible);
	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
	WidgetTree->RootWidget = Canvas;

	UBorder* Pin = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PremiumDishStar"));
	Pin->SetBrush(MakeBadgeBrush(9.f));
	Pin->SetPadding(FMargin(2.f));
	UImage* StarIcon = WidgetTree->ConstructWidget<UImage>();
	StarIcon->SetBrushFromTexture(StarTexture);
	Pin->SetContent(StarIcon);
	PlaceOnCanvas(*Canvas, *Pin, FVector2D(71.f, 40.f), FVector2D(18.f, 18.f));

	UBorder* LabelBadge = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PremiumDishReveal"));
	LabelBadge->SetBrush(MakeBadgeBrush(10.f));
	UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>();
	Label->SetText(NSLOCTEXT("Cooking", "PremiumDish", "佳品"));
	Label->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 16));
	Label->SetColorAndOpacity(FSlateColor(Cream));
	Label->SetJustification(ETextJustify::Center);
	LabelBadge->SetContent(Label);
	LabelBadge->SetVerticalAlignment(VAlign_Center);
	PlaceOnCanvas(*Canvas, *LabelBadge, FVector2D(26.f, 24.f), FVector2D(52.f, 23.f));
	RevealLabel = LabelBadge;

	const FVector2D SparkPositions[] = { FVector2D(27.f, 51.f), FVector2D(47.f, 53.f), FVector2D(68.f, 49.f) };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(SparkPositions); ++Index)
	{
		UImage* Spark = WidgetTree->ConstructWidget<UImage>();
		Spark->SetBrushFromTexture(StarTexture);
		const float Size = Index == 1 ? 12.f : 9.f;
		PlaceOnCanvas(*Canvas, *Spark, SparkPositions[Index], FVector2D(Size));
		RevealStars.Add(Spark);
	}

	return Super::RebuildWidget();
}

void UPremiumDishFeedbackWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!RevealLabel || bRevealFinished)
	{
		return;
	}

	const double Now = FPlatformTime::Seconds();
	if (RevealStartTime == 0.0)
	{
		RevealStartTime = Now;
	}
	const float Progress = FMath::Min(static_cast<float>((Now - RevealStartTime) / RevealDuration), 1.f);
	const float Fade = FMath::Clamp((1.f - Progress) / 0.36f, 0.f, 1.f);
	RevealLabel->SetRenderTranslation(FVector2D(0.f, -19.f * Progress));
	RevealLabel->SetRenderOpacity(Fade);

	for (int32 Index = 0; Index < RevealStars.Num(); ++Index)
	{
		UImage* Spark = RevealStars[Index];
		const float SparkProgress = FMath::Clamp(Progress * 1.7f - 0.09f * Index, 0.f, 1.f);
		const float Flash = FMath::Sin(PI * SparkProgress);
		const float Drift = (Index - 1) * 16.f * SparkProgress;
		Spark->SetRenderTranslation(FVector2D(Drift, -24.f * SparkProgress));
		Spark->SetRenderScale(FVector2D(0.5f + Flash * 0.75f));
		Spark->SetRenderOpacity(Flash * Fade);
	}

	if (Progress >= 1.f)
	{
		bRevealFinished = true;
		RevealLabel->SetVisibility(ESlateVisibility::Collapsed);
		for (UImage* Spark : RevealStars)
		{
			Spark->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}
