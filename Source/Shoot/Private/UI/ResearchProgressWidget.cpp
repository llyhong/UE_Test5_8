// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/ResearchProgressWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Styling/CoreStyle.h"

TSharedRef<SWidget> UResearchProgressWidget::RebuildWidget()
{
	if (WidgetTree->RootWidget)
	{
		return Super::RebuildWidget();
	}

	SetIsFocusable(false);
	SetVisibility(ESlateVisibility::HitTestInvisible);

	USizeBox* Root = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Root"));
	Root->SetWidthOverride(220.f);
	Root->SetHeightOverride(48.f);
	WidgetTree->RootWidget = Root;

	CardBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Card"));
	CardBorder->SetPadding(FMargin(10.f, 6.f, 10.f, 8.f));
	CardBorder->SetBrushColor(FLinearColor(0.04f, 0.045f, 0.05f, 0.92f));

	FSlateBrush Brush;
	Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
	Brush.OutlineSettings.CornerRadii = FVector4(8.f, 8.f, 8.f, 8.f);
	Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
	Brush.OutlineSettings.Width = 1.2f;
	Brush.OutlineSettings.Color = FSlateColor(FLinearColor(0.95f, 0.78f, 0.35f, 0.9f));
	CardBorder->SetBrush(Brush);
	Root->SetContent(CardBorder);

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
	CardBorder->SetContent(Column);

	Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Label"));
	Label->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 11));
	Label->SetColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.9f, 0.78f, 1.f)));
	if (UVerticalBoxSlot* PanelSlot = Column->AddChildToVerticalBox(Label))
	{
		PanelSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
	}

	USizeBox* BarBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("BarBox"));
	BarBox->SetHeightOverride(8.f);
	Column->AddChildToVerticalBox(BarBox);

	Bar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("Bar"));
	Bar->SetPercent(0.f);
	Bar->SetFillColorAndOpacity(FLinearColor(0.95f, 0.78f, 0.35f, 1.f));
	BarBox->SetContent(Bar);

	return Super::RebuildWidget();
}

void UResearchProgressWidget::SetProgress(float Remaining, float Total)
{
	const float SafeTotal = FMath::Max(Total, KINDA_SMALL_NUMBER);
	const float Remain = FMath::Clamp(Remaining, 0.f, SafeTotal);
	const float Fraction = 1.f - Remain / SafeTotal;

	if (Bar)
	{
		Bar->SetPercent(Fraction);
	}
	if (Label)
	{
		Label->SetText(FText::FromString(FString::Printf(TEXT("研究中 · 剩余 %.1f 秒"), Remain)));
	}
}
