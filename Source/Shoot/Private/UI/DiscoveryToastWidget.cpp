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
#include "WidgetBlueprint.h"
#include "Kismet2/KismetEditorUtilities.h"

namespace
{
	constexpr float ToastFadeInSeconds = 0.22f;
	constexpr float ToastHoldSeconds = 3.6f;
	constexpr float ToastFadeOutSeconds = 0.35f;
	constexpr int32 ToastZOrder = 90;
	constexpr float HarvestHoldSeconds = 2.f;
	constexpr float HarvestHintSpacing = 100.f;
	constexpr float HarvestHintRightMargin = 28.f;
	const TCHAR* HintWidgetBlueprintPath = TEXT("/Game/_Demo/Blueprints/UI_2D/Tips/WBP_HarvestHintCard.WBP_HarvestHintCard_C");

	UClass* GetHintWidgetClass()
	{
		static UClass* CachedClass = nullptr;
		if (!CachedClass)
		{
			CachedClass = LoadObject<UClass>(nullptr, HintWidgetBlueprintPath);
		}
		return CachedClass ? CachedClass : UDiscoveryToastWidget::StaticClass();
	}

	/** 普通弹窗每个本地玩家只保留一个容器，队列播放 */
	TMap<TWeakObjectPtr<APlayerController>, TWeakObjectPtr<UDiscoveryToastWidget>> ActiveToasts;
	/** 采摘提示每次独立一张卡，允许同时堆叠多条 */
	TMap<TWeakObjectPtr<APlayerController>, TArray<TWeakObjectPtr<UDiscoveryToastWidget>>> ActiveHarvestHints;

	enum class EToastStyle : uint8
	{
		Normal,
		HarvestRight,
	};

	TMap<TWeakObjectPtr<UDiscoveryToastWidget>, EToastStyle> WidgetStyles;

	EToastStyle GetToastStyle(UDiscoveryToastWidget* Widget)
	{
		if (const EToastStyle* Style = WidgetStyles.Find(TWeakObjectPtr<UDiscoveryToastWidget>(Widget)))
		{
			return *Style;
		}
		return EToastStyle::Normal;
	}

	void RefreshHarvestHintStack(APlayerController* PC)
	{
		if (!PC)
		{
			return;
		}
		TArray<TWeakObjectPtr<UDiscoveryToastWidget>>* Widgets = ActiveHarvestHints.Find(TWeakObjectPtr<APlayerController>(PC));
		if (!Widgets)
		{
			return;
		}
		// 数组末尾是最新的一条，从下往上重新排布，避免中间某条消失后留下空位
		int32 StackIndex = 0;
		for (int32 i = Widgets->Num() - 1; i >= 0; --i)
		{
			if (UDiscoveryToastWidget* Widget = (*Widgets)[i].Get())
			{
				Widget->SetHarvestHintStackIndex(StackIndex++);
			}
		}
	}

	void RemoveHarvestHint(APlayerController* PC, UDiscoveryToastWidget* Widget)
	{
		if (!PC || !Widget)
		{
			return;
		}
		const TWeakObjectPtr<APlayerController> PCKey(PC);
		if (TArray<TWeakObjectPtr<UDiscoveryToastWidget>>* Widgets = ActiveHarvestHints.Find(PCKey))
		{
			Widgets->Remove(TWeakObjectPtr<UDiscoveryToastWidget>(Widget));
			if (Widgets->IsEmpty())
			{
				ActiveHarvestHints.Remove(PCKey);
			}
			else
			{
				RefreshHarvestHintStack(PC);
			}
		}
	}
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
		Toast = CreateWidget<UDiscoveryToastWidget>(PC, GetHintWidgetClass());
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
		Toast = CreateWidget<UDiscoveryToastWidget>(PC, GetHintWidgetClass());
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

void UDiscoveryToastWidget::ShowHarvestHint(APlayerController* PC, const FString& ItemName, UTexture2D* ItemIcon)
{
	if (!PC || ItemName.IsEmpty())
	{
		return;
	}

	const TWeakObjectPtr<APlayerController> PCKey(PC);
	TArray<TWeakObjectPtr<UDiscoveryToastWidget>>* Widgets = ActiveHarvestHints.Find(PCKey);
	if (!Widgets)
	{
		ActiveHarvestHints.Add(PCKey, TArray<TWeakObjectPtr<UDiscoveryToastWidget>>());
		Widgets = ActiveHarvestHints.Find(PCKey);
	}

	// 顺手清掉已经失效的旧引用（关卡切换 / 控件销毁）
	for (int32 i = Widgets->Num() - 1; i >= 0; --i)
	{
		if (!(*Widgets)[i].IsValid())
		{
			Widgets->RemoveAt(i);
		}
	}

	// 每次采摘都新建一张独立卡片，三条提示可同时显示，各自 2 秒后淡出
	UDiscoveryToastWidget* Toast = CreateWidget<UDiscoveryToastWidget>(PC, GetHintWidgetClass());
	if (!Toast)
	{
		return;
	}
	WidgetStyles.Add(TWeakObjectPtr<UDiscoveryToastWidget>(Toast), EToastStyle::HarvestRight);
	Toast->OwnerController = PC;
	Toast->AddToViewport(ToastZOrder);
	Widgets->Add(Toast);
	RefreshHarvestHintStack(PC);

	FDiscoveryToastEntry Entry;
	Entry.Info.DisplayName = FString::Printf(TEXT("【%s】"), *ItemName);
	Entry.Info.Icon = ItemIcon;
	Entry.Accent = FLinearColor(0.55f, 0.86f, 0.55f, 1.f);
	Entry.bDiscovery = false;
	Toast->Enqueue(Entry);
}

void UDiscoveryToastWidget::SetHarvestHintStackIndex(int32 Index)
{
	if (!CardBorder)
	{
		return;
	}
	if (UCanvasPanelSlot* CardSlot = Cast<UCanvasPanelSlot>(CardBorder->Slot))
	{
		CardSlot->SetPosition(FVector2D(-HarvestHintRightMargin, FMath::Max(Index, 0) * HarvestHintSpacing));
	}
}

void UDiscoveryToastWidget::BindNamedWidgets()
{
	if (!WidgetTree)
	{
		return;
	}
	CardBorder = WidgetTree->FindWidget<UBorder>(TEXT("Card"));
	IconBox = WidgetTree->FindWidget<USizeBox>(TEXT("IconBox"));
	IconImage = WidgetTree->FindWidget<UImage>(TEXT("Icon"));
	KickerText = WidgetTree->FindWidget<UTextBlock>(TEXT("Kicker"));
	NameText = WidgetTree->FindWidget<UTextBlock>(TEXT("Name"));
	DescText = WidgetTree->FindWidget<UTextBlock>(TEXT("Desc"));
	RewardText = WidgetTree->FindWidget<UTextBlock>(TEXT("Reward"));
	IconSlot = IconBox ? Cast<UHorizontalBoxSlot>(IconBox->Slot) : nullptr;
}

TSharedRef<SWidget> UDiscoveryToastWidget::RebuildWidget()
{
	if (WidgetTree && WidgetTree->RootWidget)
	{
		// 蓝图子类已有可视化控件树：按名字绑定，后续逻辑完全复用
		BindNamedWidgets();
		if (CardBorder)
		{
			CardBorder->SetVisibility(ESlateVisibility::Collapsed);
			return Super::RebuildWidget();
		}
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
		if (GetToastStyle(this) == EToastStyle::HarvestRight)
		{
			// 采摘提示贴屏幕右侧中部，避开左上的任务卡与右下的缺货提示
			CardSlot->SetAnchors(FAnchors(1.f, 0.5f));
			CardSlot->SetAlignment(FVector2D(1.f, 0.5f));
			CardSlot->SetPosition(FVector2D(-28.f, 0.f));
		}
		else
		{
			CardSlot->SetAnchors(FAnchors(0.5f, 1.f));
			CardSlot->SetAlignment(FVector2D(0.5f, 1.f));
			CardSlot->SetPosition(FVector2D(0.f, -150.f));
		}
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

void UDiscoveryToastWidget::BuildVisualWidgetBlueprint(const FString& AssetPath)
{
#if WITH_EDITOR
	UWidgetBlueprint* BP = LoadObject<UWidgetBlueprint>(nullptr, *AssetPath);
	if (!BP)
	{
		return;
	}

	BP->Modify();
	UWidgetTree* Tree = BP->WidgetTree;
	if (!Tree)
	{
		Tree = NewObject<UWidgetTree>(BP, TEXT("WidgetTree"));
		BP->WidgetTree = Tree;
	}
	Tree->Modify();
	Tree->RootWidget = nullptr;

	UCanvasPanel* Root = Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	Tree->RootWidget = Root;

	UBorder* Card = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Card"));
	if (UCanvasPanelSlot* CardSlot = Root->AddChildToCanvas(Card))
	{
		CardSlot->SetAnchors(FAnchors(1.f, 0.5f));
		CardSlot->SetAlignment(FVector2D(1.f, 0.5f));
		CardSlot->SetPosition(FVector2D(-HarvestHintRightMargin, 0.f));
		CardSlot->SetAutoSize(true);
	}

	Card->SetPadding(FMargin(18.f, 14.f, 20.f, 14.f));
	Card->SetBrushColor(FLinearColor(0.04f, 0.045f, 0.05f, 0.94f));
	FSlateBrush Brush;
	Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
	Brush.OutlineSettings.CornerRadii = FVector4(10.f, 10.f, 10.f, 10.f);
	Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
	Brush.OutlineSettings.Width = 1.5f;
	Brush.OutlineSettings.Color = FSlateColor(FLinearColor(0.95f, 0.78f, 0.35f, 0.9f));
	Card->SetBrush(Brush);

	UHorizontalBox* Row = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Row"));
	Card->SetContent(Row);

	USizeBox* IconBox = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("IconBox"));
	IconBox->SetWidthOverride(64.f);
	IconBox->SetHeightOverride(64.f);
	if (UHorizontalBoxSlot* IconSlot = Row->AddChildToHorizontalBox(IconBox))
	{
		IconSlot->SetPadding(FMargin(0.f, 0.f, 14.f, 0.f));
		IconSlot->SetVerticalAlignment(VAlign_Center);
	}

	UImage* Icon = Tree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("Icon"));
	IconBox->AddChild(Icon);

	UVerticalBox* TextColumn = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("TextColumn"));
	if (UHorizontalBoxSlot* TextSlot = Row->AddChildToHorizontalBox(TextColumn))
	{
		TextSlot->SetVerticalAlignment(VAlign_Center);
	}

	auto AddText = [Tree, TextColumn](const TCHAR* Name, const TCHAR* FontStyle, int32 Size, FLinearColor Color)
	{
		UTextBlock* Text = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		Text->SetFont(FCoreStyle::GetDefaultFontStyle(FontStyle, Size));
		Text->SetColorAndOpacity(FSlateColor(Color));
		Text->SetText(FText::GetEmpty());
		TextColumn->AddChildToVerticalBox(Text);
		return Text;
	};

	AddText(TEXT("Kicker"), TEXT("Bold"), 12, FLinearColor(0.95f, 0.78f, 0.35f, 1.f));
	AddText(TEXT("Name"), TEXT("Bold"), 22, FLinearColor(0.96f, 0.96f, 0.96f, 1.f));
	UTextBlock* Desc = AddText(TEXT("Desc"), TEXT("Regular"), 13, FLinearColor(0.72f, 0.74f, 0.78f, 1.f));
	Desc->SetAutoWrapText(true);
	Desc->SetWrapTextAt(380.f);
	AddText(TEXT("Reward"), TEXT("Bold"), 12, FLinearColor(0.55f, 0.9f, 0.55f, 1.f));

	FKismetEditorUtilities::CompileBlueprint(BP);
	BP->GetOutermost()->MarkPackageDirty();
#endif
}

void UDiscoveryToastWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!bShowing || !CardBorder)
	{
		return;
	}

	Elapsed += InDeltaTime;

	// 采摘提示只停 2 秒就淡出；普通弹窗沿用原来的 3.6 秒
	const float HoldSeconds = (GetToastStyle(this) == EToastStyle::HarvestRight)
		? HarvestHoldSeconds : ToastHoldSeconds;
	float Alpha = 1.f;
	if (Elapsed < ToastFadeInSeconds)
	{
		Alpha = Elapsed / ToastFadeInSeconds;
	}
	else if (Elapsed > HoldSeconds)
	{
		Alpha = 1.f - (Elapsed - HoldSeconds) / ToastFadeOutSeconds;
	}
	Alpha = FMath::Clamp(Alpha, 0.f, 1.f);

	// 淡入淡出 + 轻微上浮，避免生硬出现
	CardBorder->SetRenderOpacity(Alpha);
	CardBorder->SetRenderTranslation(FVector2D(0.f, (1.f - Alpha) * 18.f));

	if (Elapsed >= HoldSeconds + ToastFadeOutSeconds)
	{
		bShowing = false;
		if (GetToastStyle(this) == EToastStyle::HarvestRight)
		{
			// 采摘提示用完即销毁整张卡，其它堆叠卡自动上移
			if (APlayerController* PC = OwnerController.Get())
			{
				RemoveHarvestHint(PC, this);
			}
			RemoveFromParent();
		}
		else
		{
			BeginNext();
		}
	}
}

void UDiscoveryToastWidget::NativeDestruct()
{
	const TWeakObjectPtr<UDiscoveryToastWidget> SelfKey(this);
	const EToastStyle Style = GetToastStyle(this);
	WidgetStyles.Remove(SelfKey);
	if (APlayerController* PC = OwnerController.Get())
	{
		const TWeakObjectPtr<APlayerController> PCKey(PC);
		if (Style == EToastStyle::HarvestRight)
		{
			RemoveHarvestHint(PC, this);
		}
		else
		{
			ActiveToasts.Remove(PCKey);
		}
	}
	Super::NativeDestruct();
}
