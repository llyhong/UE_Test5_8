// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/DiscoveryToastWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "GameFramework/PlayerController.h"
#include "Styling/CoreStyle.h"

namespace
{
	constexpr float ToastFadeInSeconds = 0.22f;
	constexpr float ToastHoldSeconds = 3.6f;
	constexpr float ToastFadeOutSeconds = 0.35f;
	constexpr int32 ToastZOrder = 90;

	/** 每个本地玩家只保留一个弹窗容器，避免多人同屏共享/重复创建 */
	TMap<TWeakObjectPtr<APlayerController>, TWeakObjectPtr<UDiscoveryToastWidget>> ActiveToasts;
}

void UDiscoveryToastWidget::ShowForPlayer(APlayerController* PC, const FDiscoveryInfo& Info)
{
	if (!PC)
	{
		return;
	}

	UDiscoveryToastWidget* Toast = nullptr;
	if (const TWeakObjectPtr<UDiscoveryToastWidget>* Found = ActiveToasts.Find(TWeakObjectPtr<APlayerController>(PC)))
	{
		Toast = Found->Get();
	}

	if (!IsValid(Toast))
	{
		Toast = CreateWidget<UDiscoveryToastWidget>(PC, UDiscoveryToastWidget::StaticClass());
		if (!Toast)
		{
			return;
		}
		Toast->OwnerController = PC;
		Toast->AddToViewport(ToastZOrder);
		ActiveToasts.Add(TWeakObjectPtr<APlayerController>(PC), Toast);
	}

	FDiscoveryToastEntry Entry;
	Entry.Info = Info;
	Entry.Accent = FLinearColor(0.95f, 0.78f, 0.35f, 1.f);
	Entry.bDiscovery = true;
	Toast->Enqueue(Entry);
}

void UDiscoveryToastWidget::ShowMessage(APlayerController* PC, const FString& Title, const FString& Body, FLinearColor Accent)
{
	if (!PC)
	{
		return;
	}

	UDiscoveryToastWidget* Toast = nullptr;
	if (const TWeakObjectPtr<UDiscoveryToastWidget>* Found = ActiveToasts.Find(TWeakObjectPtr<APlayerController>(PC)))
	{
		Toast = Found->Get();
	}

	if (!IsValid(Toast))
	{
		Toast = CreateWidget<UDiscoveryToastWidget>(PC, UDiscoveryToastWidget::StaticClass());
		if (!Toast)
		{
			return;
		}
		Toast->OwnerController = PC;
		Toast->AddToViewport(ToastZOrder);
		ActiveToasts.Add(TWeakObjectPtr<APlayerController>(PC), Toast);
	}

	FDiscoveryToastEntry Entry;
	Entry.Info.DisplayName = Title;
	Entry.Info.Description = Body;
	Entry.Accent = Accent;
	Entry.bDiscovery = false;
	Toast->Enqueue(Entry);
}

TSharedRef<SWidget> UDiscoveryToastWidget::RebuildWidget()
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
	CardBorder->SetPadding(FMargin(18.f, 14.f, 20.f, 14.f));
	CardBorder->SetBrushColor(FLinearColor(0.04f, 0.045f, 0.05f, 0.94f));

	CardBrush.DrawAs = ESlateBrushDrawType::RoundedBox;
	CardBrush.OutlineSettings.CornerRadii = FVector4(10.f, 10.f, 10.f, 10.f);
	CardBrush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
	CardBrush.OutlineSettings.Width = 1.5f;
	CardBrush.OutlineSettings.Color = FSlateColor(FLinearColor(0.95f, 0.78f, 0.35f, 0.9f));
	CardBorder->SetBrush(CardBrush);

	if (UCanvasPanelSlot* CardSlot = Root->AddChildToCanvas(CardBorder))
	{
		CardSlot->SetAnchors(FAnchors(0.5f, 1.f));
		CardSlot->SetAlignment(FVector2D(0.5f, 1.f));
		CardSlot->SetPosition(FVector2D(0.f, -150.f));
		CardSlot->SetAutoSize(true);
	}

	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Row"));
	CardBorder->SetContent(Row);

	IconBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("IconBox"));
	IconBox->SetWidthOverride(64.f);
	IconBox->SetHeightOverride(64.f);

	IconImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("Icon"));
	IconImage->SetColorAndOpacity(FLinearColor::White);
	IconBox->AddChild(IconImage);

	IconSlot = Row->AddChildToHorizontalBox(IconBox);
	if (IconSlot)
	{
		IconSlot->SetPadding(FMargin(0.f, 0.f, 14.f, 0.f));
		IconSlot->SetVerticalAlignment(VAlign_Center);
	}

	UVerticalBox* TextColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("TextColumn"));
	if (UHorizontalBoxSlot* TextSlot = Row->AddChildToHorizontalBox(TextColumn))
	{
		TextSlot->SetVerticalAlignment(VAlign_Center);
	}

	KickerText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Kicker"));
	KickerText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 12));
	KickerText->SetColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.78f, 0.35f, 1.f)));
	if (UVerticalBoxSlot* KickerSlot = TextColumn->AddChildToVerticalBox(KickerText))
	{
		KickerSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 2.f));
	}

	NameText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Name"));
	NameText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 22));
	NameText->SetColorAndOpacity(FSlateColor(FLinearColor(0.96f, 0.96f, 0.96f, 1.f)));
	if (UVerticalBoxSlot* NameSlot = TextColumn->AddChildToVerticalBox(NameText))
	{
		NameSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
	}

	DescText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Desc"));
	DescText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 13));
	DescText->SetColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.74f, 0.78f, 1.f)));
	DescText->SetAutoWrapText(true);
	DescText->SetWrapTextAt(380.f);
	if (UVerticalBoxSlot* DescSlot = TextColumn->AddChildToVerticalBox(DescText))
	{
		DescSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
	}

	RewardText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Reward"));
	RewardText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 12));
	RewardText->SetColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.9f, 0.55f, 1.f)));
	TextColumn->AddChildToVerticalBox(RewardText);

	CardBorder->SetVisibility(ESlateVisibility::Collapsed);

	return Super::RebuildWidget();
}

void UDiscoveryToastWidget::Enqueue(const FDiscoveryToastEntry& Entry)
{
	Queue.Add(Entry);
	if (!bShowing)
	{
		BeginNext();
	}
}

void UDiscoveryToastWidget::BeginNext()
{
	if (Queue.Num() == 0)
	{
		bShowing = false;
		if (CardBorder)
		{
			CardBorder->SetVisibility(ESlateVisibility::Collapsed);
		}
		return;
	}

	const FDiscoveryToastEntry Entry = Queue[0];
	Queue.RemoveAt(0);

	ApplyVisual(Entry);

	Elapsed = 0.f;
	bShowing = true;
	if (CardBorder)
	{
		CardBorder->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		CardBorder->SetRenderOpacity(0.f);
		CardBorder->SetRenderTranslation(FVector2D(0.f, 18.f));
	}
}

void UDiscoveryToastWidget::ApplyVisual(const FDiscoveryToastEntry& Entry)
{
	if (!CardBorder)
	{
		return;
	}

	// 卡片描边跟随本次提示的主色，让“新发现”和普通提示在视觉上可区分
	CardBrush.OutlineSettings.Color = FSlateColor(Entry.Accent);
	CardBorder->SetBrush(CardBrush);

	if (KickerText)
	{
		const FString Kicker = Entry.bDiscovery ? TEXT("新发现！") : FString();
		KickerText->SetText(FText::FromString(Kicker));
		KickerText->SetColorAndOpacity(FSlateColor(Entry.Accent));
		KickerText->SetVisibility(Kicker.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	}

	if (NameText)
	{
		NameText->SetText(FText::FromString(Entry.Info.DisplayName));
		NameText->SetVisibility(Entry.Info.DisplayName.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	}

	if (DescText)
	{
		DescText->SetText(FText::FromString(Entry.Info.Description));
		DescText->SetVisibility(Entry.Info.Description.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	}

	if (RewardText)
	{
		TArray<FString> Parts;
		if (Entry.Info.RewardMoney > 0)
		{
			Parts.Add(FString::Printf(TEXT("金币 +%d"), Entry.Info.RewardMoney));
		}
		if (Entry.Info.RewardReputation > 0)
		{
			Parts.Add(FString::Printf(TEXT("声誉 +%d"), Entry.Info.RewardReputation));
		}
		const FString RewardLine = FString::Join(Parts, TEXT("  ·  "));
		RewardText->SetText(FText::FromString(RewardLine));
		RewardText->SetVisibility(RewardLine.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	}

	// 没有图标时收起图标列，避免留下空白占位
	const bool bHasIcon = Entry.Info.Icon != nullptr;
	if (IconBox)
	{
		IconBox->SetVisibility(bHasIcon ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (IconSlot)
	{
		IconSlot->SetPadding(bHasIcon ? FMargin(0.f, 0.f, 14.f, 0.f) : FMargin(0.f));
	}
	if (IconImage && bHasIcon)
	{
		IconImage->SetBrushFromTexture(Entry.Info.Icon, false);
		IconImage->SetColorAndOpacity(FLinearColor::White);
	}
}

void UDiscoveryToastWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!bShowing || !CardBorder)
	{
		return;
	}

	Elapsed += InDeltaTime;

	float Alpha = 1.f;
	if (Elapsed < ToastFadeInSeconds)
	{
		Alpha = Elapsed / ToastFadeInSeconds;
	}
	else if (Elapsed > ToastHoldSeconds)
	{
		Alpha = 1.f - (Elapsed - ToastHoldSeconds) / ToastFadeOutSeconds;
	}
	Alpha = FMath::Clamp(Alpha, 0.f, 1.f);

	// 淡入淡出 + 轻微上浮，避免生硬出现
	CardBorder->SetRenderOpacity(Alpha);
	CardBorder->SetRenderTranslation(FVector2D(0.f, (1.f - Alpha) * 18.f));

	if (Elapsed >= ToastHoldSeconds + ToastFadeOutSeconds)
	{
		bShowing = false;
		BeginNext();
	}
}

void UDiscoveryToastWidget::NativeDestruct()
{
	if (APlayerController* PC = OwnerController.Get())
	{
		ActiveToasts.Remove(TWeakObjectPtr<APlayerController>(PC));
	}
	Super::NativeDestruct();
}
