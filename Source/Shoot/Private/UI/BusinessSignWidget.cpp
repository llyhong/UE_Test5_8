#include "UI/BusinessSignWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Sound/SoundBase.h"
#include "Styling/CoreStyle.h"

void UBusinessSignWidget::InitializeSign(bool bOpen, float Seconds, const FText& OpenText, const FText& ClosedText,
	const FText& OpenDescription, const FText& ClosedDescription, USoundBase* Sound,
	float Volume, bool bPlaySound)
{
	bTargetOpen = bOpen;
	Duration = FMath::Max(Seconds, 0.1f);
	StartedAt = GetWorld()->GetTimeSeconds();
	OpenLabel = OpenText;
	ClosedLabel = ClosedText;
	OpenSubtitle = OpenDescription;
	ClosedSubtitle = ClosedDescription;
	FlipSound = Sound;
	SoundVolume = Volume;
	bShouldPlaySound = bPlaySound;
}

TSharedRef<SWidget> UBusinessSignWidget::RebuildWidget()
{
	if (WidgetTree->RootWidget)
	{
		return Super::RebuildWidget();
	}

	SetIsFocusable(false);
	SetVisibility(ESlateVisibility::HitTestInvisible);

	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;
	HangingSign = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("HangingSign"));
	UCanvasPanelSlot* SignSlot = Root->AddChildToCanvas(HangingSign);
	SignSlot->SetSize(FVector2D(560.f, 360.f));
	HangingSign->SetRenderTransformPivot(FVector2D(0.5f, 0.05f));

	FSlateBrush Brush;
	Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
	Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
	Brush.OutlineSettings.CornerRadii = FVector4(12.f, 12.f, 12.f, 12.f);

	UBorder* Hook = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Hook"));
	Hook->SetBrush(Brush);
	Hook->SetBrushColor(FLinearColor(0.83f, 0.62f, 0.32f, 1.f));
	UCanvasPanelSlot* HookSlot = HangingSign->AddChildToCanvas(Hook);
	HookSlot->SetPosition(FVector2D(274.f, 12.f));
	HookSlot->SetSize(FVector2D(12.f, 12.f));

	UBorder* LeftRope = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("LeftRope"));
	LeftRope->SetBrushColor(FLinearColor(0.65f, 0.43f, 0.23f, 1.f));
	UCanvasPanelSlot* LeftSlot = HangingSign->AddChildToCanvas(LeftRope);
	LeftSlot->SetPosition(FVector2D(132.f, 112.f));
	LeftSlot->SetSize(FVector2D(175.f, 4.f));
	LeftRope->SetRenderTransformPivot(FVector2D::ZeroVector);
	LeftRope->SetRenderTransformAngle(-33.f);

	UBorder* RightRope = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("RightRope"));
	RightRope->SetBrushColor(FLinearColor(0.65f, 0.43f, 0.23f, 1.f));
	UCanvasPanelSlot* RightSlot = HangingSign->AddChildToCanvas(RightRope);
	RightSlot->SetPosition(FVector2D(280.f, 18.f));
	RightSlot->SetSize(FVector2D(175.f, 4.f));
	RightRope->SetRenderTransformPivot(FVector2D::ZeroVector);
	RightRope->SetRenderTransformAngle(33.f);

	Plaque = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Plaque"));
	Plaque->SetBrush(Brush);
	Plaque->SetBrushColor(FLinearColor(0.22f, 0.115f, 0.065f, 1.f));
	Plaque->SetPadding(FMargin(7.f));
	UCanvasPanelSlot* PlaqueSlot = HangingSign->AddChildToCanvas(Plaque);
	PlaqueSlot->SetPosition(FVector2D(40.f, 100.f));
	PlaqueSlot->SetSize(FVector2D(480.f, 216.f));
	Plaque->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));

	Face = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Face"));
	Brush.OutlineSettings.CornerRadii = FVector4(7.f, 7.f, 7.f, 7.f);
	Brush.OutlineSettings.Width = 2.f;
	Brush.OutlineSettings.Color = FSlateColor(FLinearColor(0.88f, 0.69f, 0.38f, 1.f));
	Face->SetBrush(Brush);
	Face->SetPadding(FMargin(24.f, 22.f));
	Face->SetVerticalAlignment(VAlign_Center);
	Plaque->SetContent(Face);

	UVerticalBox* Labels = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Labels"));
	Face->SetContent(Labels);

	UTextBlock* Kicker = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Kicker"));
	Kicker->SetText(NSLOCTEXT("BusinessSign", "Restaurant", "餐厅营业状态"));
	Kicker->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 13));
	Kicker->SetColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.83f, 0.62f, 1.f)));
	Kicker->SetJustification(ETextJustify::Center);
	Labels->AddChildToVerticalBox(Kicker)->SetPadding(FMargin(0.f, 0.f, 0.f, 3.f));

	Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Title"));
	Title->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 54));
	Title->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.94f, 0.8f, 1.f)));
	Title->SetJustification(ETextJustify::Center);
	Title->SetShadowOffset(FVector2D(0.f, 2.f));
	Title->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.25f));
	Labels->AddChildToVerticalBox(Title);

	Description = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Description"));
	Description->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 18));
	Description->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.94f, 0.8f, 1.f)));
	Description->SetJustification(ETextJustify::Center);
	Labels->AddChildToVerticalBox(Description)->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));

	ApplySide(!bTargetOpen);
	HangingSign->SetRenderOpacity(0.f);
	return Super::RebuildWidget();
}

void UBusinessSignWidget::ApplySide(bool bOpen)
{
	bShowingOpenSide = bOpen;
	Title->SetText(bOpen ? OpenLabel : ClosedLabel);
	Description->SetText(bOpen ? OpenSubtitle : ClosedSubtitle);
	Face->SetBrushColor(bOpen
		? FLinearColor(0.075f, 0.24f, 0.18f, 1.f)
		: FLinearColor(0.36f, 0.095f, 0.075f, 1.f));
}

void UBusinessSignWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!HangingSign || !Plaque)
	{
		return;
	}

	// huanglily：后台 Slate 会限制 Tick 的 DeltaTime，动画须与移除计时器共用世界时钟。
	const float Elapsed = static_cast<float>(GetWorld()->GetTimeSeconds() - StartedAt);
	AnimationProgress = FMath::Clamp(Elapsed / Duration, 0.f, 1.f);
	const float T = AnimationProgress;
	const float Flip = FMath::Clamp((T - 0.16f) / 0.32f, 0.f, 1.f);
	const float EasedFlip = FMath::InterpEaseInOut(0.f, 1.f, Flip, 2.f);
	if (Flip >= 0.5f && bShowingOpenSide != bTargetOpen)
	{
		ApplySide(bTargetOpen);
		if (bShouldPlaySound && FlipSound)
		{
			UGameplayStatics::PlaySound2D(this, FlipSound, SoundVolume);
			bFlipSoundPlayed = true;
		}
	}

	const float Width = FMath::Max(FMath::Abs(FMath::Cos(EasedFlip * UE_PI)), 0.025f);
	Plaque->SetRenderScale(FVector2D(Width, 1.f - 0.06f * FMath::Sin(EasedFlip * UE_PI)));
	const float Opacity = FMath::Min(FMath::Clamp(T / 0.1f, 0.f, 1.f), FMath::Clamp((1.f - T) / 0.14f, 0.f, 1.f));
	HangingSign->SetRenderOpacity(Opacity);
	HangingSign->SetRenderTranslation(FVector2D(0.f, -22.f * (1.f - Opacity)));
	HangingSign->SetRenderTransformAngle(7.f * FMath::Sin(T * 5.f * UE_PI) * FMath::Exp(-T * 4.f));
}
