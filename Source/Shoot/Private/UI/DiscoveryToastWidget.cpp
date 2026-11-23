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
#include "Engine/Font.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Styling/CoreStyle.h"
#include "UObject/StrongObjectPtr.h"
#include "WidgetBlueprint.h"
#include "Kismet2/KismetEditorUtilities.h"

namespace
{
	constexpr float ToastFadeInSeconds = 0.22f;
	constexpr float ToastHoldSeconds = 3.6f;
	constexpr float ToastFadeOutSeconds = 0.35f;
	constexpr int32 ToastZOrder = 90;

	// 采摘提示节奏对齐参考视频：右侧快速滑入(~0.26s) → 停留 3.2s → 向右滑出(~0.3s)
	constexpr float HarvestSlideInSeconds = 0.26f;
	constexpr float HarvestHoldSeconds = 3.2f;
	constexpr float HarvestSlideOutSeconds = 0.30f;
	constexpr float HarvestSlideDistance = 460.f;
	constexpr float HarvestHintSpacing = 118.f;
	constexpr float HarvestHintRightMargin = 28.f;
	const TCHAR* HintWidgetBlueprintPath = TEXT("/Game/_Demo/Blueprints/UI_2D/Tips/WBP_HarvestHintCard.WBP_HarvestHintCard_C");

	/** 采摘提示音效直接复用金币拾取音（StackOBot 现成资源） */
	USoundBase* GetHarvestHintSound()
	{
		static TStrongObjectPtr<USoundBase> CachedSound(LoadObject<USoundBase>(nullptr, TEXT("/Game/StackOBot/Audio/SFX/Waves/SFX_Coin_Collect.SFX_Coin_Collect")));
		return CachedSound.Get();
	}

	UClass* GetHintWidgetClass()
	{
		static UClass* CachedClass = nullptr;
		if (!CachedClass)
		{
			CachedClass = LoadObject<UClass>(nullptr, HintWidgetBlueprintPath);
		}
		return CachedClass ? CachedClass : UDiscoveryToastWidget::StaticClass();
	}

	/** FCoreStyle 默认复合字体在本卡里渲染 Bold 中文会变豆腐块，统一换成带中文回退的引擎字体资产 */
	FSlateFontInfo MakeToastFont(const TCHAR* Typeface, int32 Size)
	{
		static TStrongObjectPtr<UFont> CachedFont(LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto")));
		if (UFont* Font = CachedFont.Get())
		{
			return FSlateFontInfo(Font, static_cast<float>(Size), Typeface);
		}
		return FCoreStyle::GetDefaultFontStyle(Typeface, Size);
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

	// 每次采摘都新建一张独立卡片，多条可同时堆叠，各自滑入后停留再从右侧收回
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
	Entry.Info.DisplayName = ItemName;
	Entry.Info.Icon = ItemIcon;
	Entry.Accent = FLinearColor(0.55f, 0.86f, 0.55f, 1.f);
	Entry.bDiscovery = false;
	Toast->Enqueue(Entry);

	if (USoundBase* HintSound = GetHarvestHintSound())
	{
		UGameplayStatics::PlaySound2D(PC, HintSound, 0.7f);
		UE_LOG(LogTemp, Log, TEXT("[采摘] 提示卡弹出，播放提示音效 %s"), *HintSound->GetName());
	}
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
	TextColumn = WidgetTree->FindWidget<UVerticalBox>(TEXT("TextColumn"));
	BagRow = WidgetTree->FindWidget<UHorizontalBox>(TEXT("BagRow"));
	BagKeyBorder = WidgetTree->FindWidget<UBorder>(TEXT("BagKey"));
	BagKeyText = WidgetTree->FindWidget<UTextBlock>(TEXT("BagKeyText"));
	BagText = WidgetTree->FindWidget<UTextBlock>(TEXT("BagHint"));
	IconSlot = IconBox ? Cast<UHorizontalBoxSlot>(IconBox->Slot) : nullptr;
	if (CardBorder)
	{
		// WBP 里画好的圆角/描边要保留，后续只改描边颜色
		CardBrush = CardBorder->Background;
	}
	// 覆盖 WBP 自带字体，避免中文缺字
	if (KickerText)
	{
		KickerText->SetFont(MakeToastFont(TEXT("Bold"), 12));
	}
	if (NameText)
	{
		NameText->SetFont(MakeToastFont(TEXT("Bold"), 22));
	}
	if (DescText)
	{
		DescText->SetFont(MakeToastFont(TEXT("Regular"), 13));
	}
	if (RewardText)
	{
		RewardText->SetFont(MakeToastFont(TEXT("Bold"), 12));
	}
	if (BagText)
	{
		BagText->SetFont(MakeToastFont(TEXT("Regular"), 12));
	}
	if (BagKeyText)
	{
		BagKeyText->SetFont(MakeToastFont(TEXT("Bold"), 11));
	}
	// 旧版 WBP 没有 BagRow 节点时，在构建阶段补出来
	EnsureBagHint();
}

void UDiscoveryToastWidget::EnsureBagHint()
{
	if (!WidgetTree)
	{
		return;
	}
	if (!TextColumn)
	{
		TextColumn = WidgetTree->FindWidget<UVerticalBox>(TEXT("TextColumn"));
	}
	if (!TextColumn)
	{
		return;
	}

	if (!BagRow)
	{
		BagRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("BagRow"));

		BagText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("BagHint"));
		BagText->SetFont(MakeToastFont(TEXT("Regular"), 12));
		BagText->SetColorAndOpacity(FSlateColor(FLinearColor(0.78f, 0.80f, 0.83f, 1.f)));
		BagText->SetText(FText::FromString(TEXT("打开背包")));
		if (UHorizontalBoxSlot* HintSlot = BagRow->AddChildToHorizontalBox(BagText))
		{
			HintSlot->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
			HintSlot->SetVerticalAlignment(VAlign_Center);
		}

		BagKeyBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("BagKey"));
		FSlateBrush KeyBrush;
		KeyBrush.DrawAs = ESlateBrushDrawType::RoundedBox;
		KeyBrush.OutlineSettings.CornerRadii = FVector4(4.f, 4.f, 4.f, 4.f);
		KeyBrush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		BagKeyBorder->SetBrush(KeyBrush);
		BagKeyBorder->SetBrushColor(FLinearColor(0.93f, 0.93f, 0.90f, 0.96f));
		BagKeyBorder->SetPadding(FMargin(5.f, 1.f));

		BagKeyText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("BagKeyText"));
		BagKeyText->SetFont(MakeToastFont(TEXT("Bold"), 11));
		BagKeyText->SetColorAndOpacity(FSlateColor(FLinearColor(0.08f, 0.09f, 0.10f, 1.f)));
		BagKeyText->SetText(FText::FromString(TEXT("B")));
		BagKeyBorder->SetContent(BagKeyText);

		if (UHorizontalBoxSlot* KeySlot = BagRow->AddChildToHorizontalBox(BagKeyBorder))
		{
			KeySlot->SetVerticalAlignment(VAlign_Center);
		}

		if (UVerticalBoxSlot* BagRowSlot = TextColumn->AddChildToVerticalBox(BagRow))
		{
			BagRowSlot->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
			BagRowSlot->SetHorizontalAlignment(HAlign_Right);
		}
	}
}

void UDiscoveryToastWidget::SetBagHintVisible(bool bVisible)
{
	EnsureBagHint();
	if (BagRow)
	{
		BagRow->SetVisibility(bVisible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
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

	TextColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("TextColumn"));
	if (UHorizontalBoxSlot* TextSlot = Row->AddChildToHorizontalBox(TextColumn))
	{
		TextSlot->SetVerticalAlignment(VAlign_Center);
	}

	KickerText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Kicker"));
	KickerText->SetFont(MakeToastFont(TEXT("Bold"), 12));
	KickerText->SetColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.78f, 0.35f, 1.f)));
	if (UVerticalBoxSlot* KickerSlot = TextColumn->AddChildToVerticalBox(KickerText))
	{
		KickerSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 2.f));
	}

	NameText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Name"));
	NameText->SetFont(MakeToastFont(TEXT("Bold"), 22));
	NameText->SetColorAndOpacity(FSlateColor(FLinearColor(0.96f, 0.96f, 0.96f, 1.f)));
	if (UVerticalBoxSlot* NameSlot = TextColumn->AddChildToVerticalBox(NameText))
	{
		NameSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
	}

	DescText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Desc"));
	DescText->SetFont(MakeToastFont(TEXT("Regular"), 13));
	DescText->SetColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.74f, 0.78f, 1.f)));
	DescText->SetAutoWrapText(true);
	DescText->SetWrapTextAt(380.f);
	if (UVerticalBoxSlot* DescSlot = TextColumn->AddChildToVerticalBox(DescText))
	{
		DescSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
	}

	RewardText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Reward"));
	RewardText->SetFont(MakeToastFont(TEXT("Bold"), 12));
	RewardText->SetColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.9f, 0.55f, 1.f)));
	TextColumn->AddChildToVerticalBox(RewardText);

	EnsureBagHint();

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
		if (GetToastStyle(this) == EToastStyle::HarvestRight)
		{
			// 采摘提示从屏幕右侧外滑入
			CardBorder->SetRenderTransform(FWidgetTransform(
				FVector2D(HarvestSlideDistance, 0.f),
				FVector2D(0.96f, 0.96f),
				FVector2D::ZeroVector,
				0.f));
		}
		else
		{
			CardBorder->SetRenderTranslation(FVector2D(0.f, 18.f));
		}
	}
}

void UDiscoveryToastWidget::ApplyVisual(const FDiscoveryToastEntry& Entry)
{
	if (!CardBorder)
	{
		return;
	}

	const bool bHarvestStyle = GetToastStyle(this) == EToastStyle::HarvestRight;

	// 卡片描边跟随本次提示的主色，让“新发现”和普通提示在视觉上可区分
	CardBrush.OutlineSettings.Color = FSlateColor(Entry.Accent);
	CardBorder->SetBrush(CardBrush);
	// 采摘提示用半透明墨绿横幅，贴近参考视频里的炸弹花提示
	CardBorder->SetBrushColor(bHarvestStyle
		? FLinearColor(0.055f, 0.11f, 0.065f, 0.88f)
		: FLinearColor(0.04f, 0.045f, 0.05f, 0.94f));

	// “打开背包 B”只在采摘提示上出现
	SetBagHintVisible(bHarvestStyle);

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
		Text->SetFont(MakeToastFont(FontStyle, Size));
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

	// “打开背包 B”指引行：采摘提示右下角展示 B 键
	UHorizontalBox* BagRow = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("BagRow"));
	UTextBlock* BagHint = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("BagHint"));
	BagHint->SetFont(MakeToastFont(TEXT("Regular"), 12));
	BagHint->SetColorAndOpacity(FSlateColor(FLinearColor(0.78f, 0.80f, 0.83f, 1.f)));
	BagHint->SetText(FText::FromString(TEXT("打开背包")));
	if (UHorizontalBoxSlot* HintSlot = BagRow->AddChildToHorizontalBox(BagHint))
	{
		HintSlot->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
		HintSlot->SetVerticalAlignment(VAlign_Center);
	}

	UBorder* BagKey = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("BagKey"));
	FSlateBrush KeyBrush;
	KeyBrush.DrawAs = ESlateBrushDrawType::RoundedBox;
	KeyBrush.OutlineSettings.CornerRadii = FVector4(4.f, 4.f, 4.f, 4.f);
	KeyBrush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
	BagKey->SetBrush(KeyBrush);
	BagKey->SetBrushColor(FLinearColor(0.93f, 0.93f, 0.90f, 0.96f));
	BagKey->SetPadding(FMargin(5.f, 1.f));

	UTextBlock* BagKeyText = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("BagKeyText"));
	BagKeyText->SetFont(MakeToastFont(TEXT("Bold"), 11));
	BagKeyText->SetColorAndOpacity(FSlateColor(FLinearColor(0.08f, 0.09f, 0.10f, 1.f)));
	BagKeyText->SetText(FText::FromString(TEXT("B")));
	BagKey->SetContent(BagKeyText);

	if (UHorizontalBoxSlot* KeySlot = BagRow->AddChildToHorizontalBox(BagKey))
	{
		KeySlot->SetVerticalAlignment(VAlign_Center);
	}

	if (UVerticalBoxSlot* BagRowSlot = TextColumn->AddChildToVerticalBox(BagRow))
	{
		BagRowSlot->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
		BagRowSlot->SetHorizontalAlignment(HAlign_Right);
	}

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

	if (GetToastStyle(this) == EToastStyle::HarvestRight)
	{
		// 参考视频节奏：快速滑入 → 稳停 3.2 秒 → 向右滑出收回
		float Alpha = 1.f;
		float OffsetX = 0.f;
		float Scale = 1.f;
		if (Elapsed < HarvestSlideInSeconds)
		{
			const float T = FMath::Clamp(Elapsed / HarvestSlideInSeconds, 0.f, 1.f);
			const float Eased = 1.f - FMath::Pow(1.f - T, 3.f);
			OffsetX = (1.f - Eased) * HarvestSlideDistance;
			Alpha = FMath::Clamp(Elapsed / (HarvestSlideInSeconds * 0.6f), 0.f, 1.f);
			Scale = FMath::Lerp(0.96f, 1.f, Eased);
		}
		else if (Elapsed > HarvestHoldSeconds)
		{
			const float T = FMath::Clamp((Elapsed - HarvestHoldSeconds) / HarvestSlideOutSeconds, 0.f, 1.f);
			OffsetX = T * T * HarvestSlideDistance;
			Alpha = 1.f - T;
		}

		CardBorder->SetRenderOpacity(Alpha);
		CardBorder->SetRenderTransform(FWidgetTransform(
			FVector2D(OffsetX, 0.f),
			FVector2D(Scale, Scale),
			FVector2D::ZeroVector,
			0.f));

		if (Elapsed >= HarvestHoldSeconds + HarvestSlideOutSeconds)
		{
			bShowing = false;
			// 采摘提示用完即销毁整张卡，其它堆叠卡自动补位
			if (APlayerController* PC = OwnerController.Get())
			{
				RemoveHarvestHint(PC, this);
			}
			RemoveFromParent();
		}
		return;
	}

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
