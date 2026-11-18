// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/SpawnerRestockTipWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Styling/CoreStyle.h"

void USpawnerRestockTipWidget::SetTip(const FText& InTitle, const FText& InBody)
{
	PendingTitle = InTitle;
	PendingBody = InBody;

	if (TitleText)
	{
		TitleText->SetText(InTitle);
	}
	if (BodyText)
	{
		BodyText->SetText(InBody);
	}
}

TSharedRef<SWidget> USpawnerRestockTipWidget::RebuildWidget()
{
	if (WidgetTree->RootWidget)
	{
		return Super::RebuildWidget();
	}

	SetIsFocusable(false);
	SetVisibility(ESlateVisibility::HitTestInvisible);

	UBorder* Background = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Background"));
	FSlateBrush Brush;
	Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
	Brush.OutlineSettings.CornerRadii = FVector4(8.f, 8.f, 8.f, 8.f);
	Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
	Brush.OutlineSettings.Width = 1.5f;
	Brush.OutlineSettings.Color = FSlateColor(FLinearColor(0.95f, 0.65f, 0.2f, 0.9f));
	Background->SetBrush(Brush);
	Background->SetBrushColor(FLinearColor(0.06f, 0.05f, 0.03f, 0.86f));
	Background->SetPadding(FMargin(14.f, 8.f, 14.f, 10.f));
	WidgetTree->RootWidget = Background;

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
	Background->SetContent(Column);

	TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Title"));
	TitleText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 16));
	TitleText->SetColorAndOpacity(FSlateColor(FLinearColor(0.98f, 0.75f, 0.25f, 1.f)));
	TitleText->SetText(PendingTitle.IsEmpty() ? FText::FromString(TEXT("食材不足")) : PendingTitle);
	if (UVerticalBoxSlot* TitleSlot = Column->AddChildToVerticalBox(TitleText))
	{
		TitleSlot->SetHorizontalAlignment(HAlign_Center);
	}

	BodyText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Body"));
	BodyText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 14));
	BodyText->SetColorAndOpacity(FSlateColor(FLinearColor(0.92f, 0.92f, 0.9f, 1.f)));
	BodyText->SetText(PendingBody.IsEmpty() ? FText::FromString(TEXT("需要到野外补货")) : PendingBody);
	if (UVerticalBoxSlot* BodySlot = Column->AddChildToVerticalBox(BodyText))
	{
		BodySlot->SetHorizontalAlignment(HAlign_Center);
		BodySlot->SetPadding(FMargin(0.f, 2.f, 0.f, 0.f));
	}

	return Super::RebuildWidget();
}
