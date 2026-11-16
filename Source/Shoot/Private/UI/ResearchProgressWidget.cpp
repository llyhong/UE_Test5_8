// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/ResearchProgressWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"
#include "Game/BaseGameInstance.h"
#include "Game/FoodSystem.h"
#include "Styling/CoreStyle.h"

namespace
{
	constexpr float CardWidth = 220.f;
	constexpr float CardHeightBase = 48.f;
	constexpr float IconRowHeight = 26.f;
	constexpr float IconSize = 22.f;
	constexpr int32 MaxIconSlots = 4;
}

TSharedRef<SWidget> UResearchProgressWidget::RebuildWidget()
{
	if (WidgetTree->RootWidget)
	{
		return Super::RebuildWidget();
	}

	SetIsFocusable(false);
	SetVisibility(ESlateVisibility::HitTestInvisible);

	Root = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Root"));
	Root->SetWidthOverride(CardWidth);
	Root->SetHeightOverride(CardHeightBase);
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

	// 食材图标行：没有图标时折叠，卡片保持 48 高
	IconRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("IconRow"));
	IconRow->SetVisibility(ESlateVisibility::Collapsed);
	if (UVerticalBoxSlot* IconSlot = Column->AddChildToVerticalBox(IconRow))
	{
		IconSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
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

void UResearchProgressWidget::SetProgress(float Remaining, float Total, const FString& InVerb)
{
	Verb = InVerb;

	const float SafeTotal = FMath::Max(Total, KINDA_SMALL_NUMBER);
	const float Remain = FMath::Clamp(Remaining, 0.f, SafeTotal);
	const float Fraction = 1.f - Remain / SafeTotal;

	if (Bar)
	{
		Bar->SetPercent(Fraction);
	}
	if (Label)
	{
		Label->SetText(FText::FromString(FString::Printf(TEXT("%s · 剩余 %.1f 秒"), *Verb, Remain)));
	}
}

void UResearchProgressWidget::SetPending(int32 IngredientCount, const FString& InHint)
{
	if (Bar)
	{
		Bar->SetPercent(0.f);
	}
	if (Label)
	{
		Label->SetText(FText::FromString(FString::Printf(TEXT("已投入 %d 份 · %s"), IngredientCount, *InHint)));
	}
}

void UResearchProgressWidget::SetIngredientList(const TArray<int32>& ItemIDs)
{
	TArray<TPair<int32, int32>> Entries;
	Entries.Reserve(ItemIDs.Num());
	for (int32 ItemID : ItemIDs)
	{
		if (TPair<int32, int32>* Found = Entries.FindByPredicate(
			[ItemID](const TPair<int32, int32>& Entry) { return Entry.Key == ItemID; }))
		{
			Found->Value += 1;
		}
		else
		{
			Entries.Emplace(ItemID, 1);
		}
	}
	ApplyIngredientEntries(Entries);
}

void UResearchProgressWidget::SetIngredientCounts(const TMap<int32, int32>& Counts)
{
	TArray<TPair<int32, int32>> Entries;
	Entries.Reserve(Counts.Num());
	for (const TPair<int32, int32>& Pair : Counts)
	{
		if (Pair.Value > 0)
		{
			Entries.Emplace(Pair.Key, Pair.Value);
		}
	}
	// TMap 遍历顺序不稳定：按 ID 排序，避免图标行在帧间跳动
	Entries.Sort([](const TPair<int32, int32>& A, const TPair<int32, int32>& B)
	{
		return A.Key < B.Key;
	});
	ApplyIngredientEntries(Entries);
}

FVector2D UResearchProgressWidget::GetCardDrawSize() const
{
	return FVector2D(CardWidth, bHasIcons ? CardHeightBase + IconRowHeight : CardHeightBase);
}

void UResearchProgressWidget::ApplyIngredientEntries(const TArray<TPair<int32, int32>>& Entries)
{
	if (!IconRow || !Root)
	{
		return;
	}

	FString Signature;
	for (const TPair<int32, int32>& Entry : Entries)
	{
		Signature += FString::Printf(TEXT("%d:%d;"), Entry.Key, Entry.Value);
	}
	if (Signature == IconSignature)
	{
		return;
	}
	IconSignature = Signature;

	IconRow->ClearChildren();
	const int32 ShownCount = FMath::Min(Entries.Num(), MaxIconSlots);
	for (int32 i = 0; i < ShownCount; ++i)
	{
		if (UHorizontalBoxSlot* EntrySlot = IconRow->AddChildToHorizontalBox(MakeIconSlot(Entries[i].Key, Entries[i].Value)))
		{
			EntrySlot->SetVerticalAlignment(VAlign_Center);
			EntrySlot->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
		}
	}

	if (Entries.Num() > MaxIconSlots)
	{
		UTextBlock* MoreLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		MoreLabel->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 9));
		MoreLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.8f, 0.78f, 0.7f, 1.f)));
		MoreLabel->SetText(FText::FromString(FString::Printf(TEXT("+%d"), Entries.Num() - MaxIconSlots)));
		if (UHorizontalBoxSlot* MoreSlot = IconRow->AddChildToHorizontalBox(MoreLabel))
		{
			MoreSlot->SetVerticalAlignment(VAlign_Center);
		}
	}

	bHasIcons = Entries.Num() > 0;
	IconRow->SetVisibility(bHasIcons ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	Root->SetHeightOverride(GetCardDrawSize().Y);
}

UWidget* UResearchProgressWidget::MakeIconSlot(int32 ItemID, int32 Count)
{
	UHorizontalBox* SlotBox = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

	USizeBox* IconBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	IconBox->SetWidthOverride(IconSize);
	IconBox->SetHeightOverride(IconSize);

	UImage* Icon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
	UTexture2D* IconTex = nullptr;
	if (UWorld* World = GetWorld())
	{
		if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(World->GetGameInstance()))
		{
			if (UFoodSystem* FS = GI->foodSystem.Get())
			{
				IconTex = FS->GetItemIcon(ItemID);
			}
		}
	}
	if (IconTex)
	{
		Icon->SetBrushFromTexture(IconTex, true);
	}
	else
	{
		// 数据表缺图标时退成灰块，至少还能看出"有几种食材"
		Icon->SetColorAndOpacity(FLinearColor(0.35f, 0.35f, 0.38f, 1.f));
	}
	IconBox->SetContent(Icon);
	SlotBox->AddChildToHorizontalBox(IconBox);

	if (Count > 1)
	{
		UTextBlock* CountLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		CountLabel->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 9));
		CountLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.9f, 0.78f, 1.f)));
		CountLabel->SetText(FText::FromString(FString::Printf(TEXT("×%d"), Count)));
		if (UHorizontalBoxSlot* CountSlot = SlotBox->AddChildToHorizontalBox(CountLabel))
		{
			CountSlot->SetVerticalAlignment(VAlign_Center);
			CountSlot->SetPadding(FMargin(2.f, 0.f, 0.f, 0.f));
		}
	}

	return SlotBox;
}
