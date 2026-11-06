#include "UI/InteractionPromptWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"

TSharedRef<SWidget> UInteractionPromptWidget::RebuildWidget()
{
	if (WidgetTree->RootWidget)
	{
		return Super::RebuildWidget();
	}
	SetIsFocusable(false);
	SetVisibility(ESlateVisibility::HitTestInvisible);

	USizeBox* Key = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Key"));
	Key->SetWidthOverride(26.f);
	Key->SetHeightOverride(28.f);
	WidgetTree->RootWidget = Key;

	UBorder* Background = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Background"));
	FSlateBrush Brush;
	Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
	Brush.OutlineSettings.CornerRadii = FVector4(4.f, 4.f, 4.f, 4.f);
	Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
	Background->SetBrush(Brush);
	Background->SetBrushColor(FLinearColor(0.025f, 0.025f, 0.025f, 0.9f));
	Background->SetPadding(FMargin(0.f));
	Background->SetHorizontalAlignment(HAlign_Center);
	Background->SetVerticalAlignment(VAlign_Center);
	Key->AddChild(Background);

	UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Label"));
	Label->SetText(FText::FromString(TEXT("E")));
	FSlateFontInfo Font = Label->GetFont();
	Font.Size = 16;
	Font.TypefaceFontName = TEXT("Bold");
	Label->SetFont(Font);
	Label->SetColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.95f, 0.95f, 1.f)));
	Background->AddChild(Label);
	return Super::RebuildWidget();
}
