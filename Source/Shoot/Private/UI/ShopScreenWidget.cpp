// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/ShopScreenWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/BackgroundBlur.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/ScaleBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/BaseGameInstance.h"
#include "Game/EconomySystem.h"
#include "Game/FoodSystem.h"
#include "Game/OrderSystem.h"
#include "GameFramework/PlayerController.h"
#include "Item/FoodSpawner.h"
#include "Item/ItemActor.h"
#include "Item/PlateCollectionTray.h"
#include "Kismet/GameplayStatics.h"
#include "Machine/CookingMachine.h"
#include "Misc/PackageName.h"
#include "ShootCharacter.h"
#include "Sound/SoundBase.h"
#include "Styling/CoreStyle.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	FLinearColor ShopColor(const TCHAR* Hex)
	{
		return FLinearColor::FromSRGBColor(FColor::FromHex(Hex));
	}

	struct FShopPalette
	{
		FLinearColor Fill;
		FLinearColor Rim;
		FLinearColor Ink;
	};

	FShopPalette GetShopPalette(int32 Index)
	{
		static const FShopPalette Palettes[] = {
			{ShopColor(TEXT("FFE8A3")), ShopColor(TEXT("E5B947")), ShopColor(TEXT("A76513"))},
			{ShopColor(TEXT("CDE8FA")), ShopColor(TEXT("87BDD9")), ShopColor(TEXT("28657F"))},
			{ShopColor(TEXT("DFD2F3")), ShopColor(TEXT("AF96D1")), ShopColor(TEXT("64508E"))},
			{ShopColor(TEXT("FAD5DB")), ShopColor(TEXT("E8A0AD")), ShopColor(TEXT("A54351"))}
		};
		return Palettes[Index % UE_ARRAY_COUNT(Palettes)];
	}

	void ShopPlace(UCanvasPanel* Canvas, UWidget* Widget, FVector2D Position, FVector2D Size)
	{
		UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(Widget);
		Slot->SetPosition(Position);
		Slot->SetSize(Size);
	}

	UBorder* ShopPanel(UWidgetTree* Tree, FLinearColor Fill, float Radius,
		FLinearColor Outline = FLinearColor::Transparent, float OutlineWidth = 0.f)
	{
		UBorder* Border = Tree->ConstructWidget<UBorder>();
		Border->SetBrush(FSlateRoundedBoxBrush(Fill, Radius, Outline, OutlineWidth));
		Border->SetPadding(FMargin(0.f));
		Border->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		return Border;
	}

	UTextBlock* ShopText(UWidgetTree* Tree, FName Name, FText Value, int32 FontSize,
		FLinearColor Ink, int32 OutlineSize = 0)
	{
		UTextBlock* Block = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Bold", FontSize);
		Font.OutlineSettings.OutlineSize = OutlineSize;
		Font.OutlineSettings.OutlineColor = ShopColor(TEXT("FFF9E9"));
		Block->SetFont(Font);
		Block->SetText(Value);
		Block->SetColorAndOpacity(FSlateColor(Ink));
		Block->SetVisibility(ESlateVisibility::HitTestInvisible);
		return Block;
	}

	FButtonStyle ShopButtonStyle(FLinearColor Fill, FLinearColor Outline, float Radius, bool bHoney = false)
	{
		FButtonStyle Style;
		Style.SetNormal(FSlateRoundedBoxBrush(Fill, Radius, Outline, 3.f));
		Style.SetHovered(FSlateRoundedBoxBrush(FMath::Lerp(Fill, FLinearColor::White, 0.25f), Radius, ShopColor(TEXT("FFF8DC")), 4.f));
		Style.SetPressed(FSlateRoundedBoxBrush(Fill * 0.85f, Radius, Outline, 3.f));
		Style.SetDisabled(FSlateRoundedBoxBrush(ShopColor(bHoney ? TEXT("E7D9BA") : TEXT("E3D7C2")), Radius,
			ShopColor(TEXT("CDBB99")), 2.f));
		Style.SetNormalPadding(FMargin(8.f));
		Style.SetPressedPadding(FMargin(8.f, 10.f, 8.f, 6.f));
		return Style;
	}

	void ShopLeaf(UWidgetTree* Tree, UCanvasPanel* Canvas, FVector2D Position, float Angle, FLinearColor Fill)
	{
		UImage* Image = Tree->ConstructWidget<UImage>();
		Image->SetBrush(FSlateRoundedBoxBrush(Fill, FVector4(2.f, 18.f, 2.f, 18.f)));
		Image->SetVisibility(ESlateVisibility::HitTestInvisible);
		Image->SetRenderTransformAngle(Angle);
		ShopPlace(Canvas, Image, Position, FVector2D(19.f, 34.f));
	}

	void FillSpacer(UWidgetTree* Tree, UHorizontalBox* Row)
	{
		USpacer* Spacer = Tree->ConstructWidget<USpacer>();
		UHorizontalBoxSlot* Slot = Row->AddChildToHorizontalBox(Spacer);
		Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	enum class EShopOfferKind : uint8
	{
		Tray,
		Pan,
		Juicer
	};

	struct FShopOffer
	{
		int32 ItemID;
		const TCHAR* Name;
		const TCHAR* Desc;
		int32 Cost;
		int32 MaxLevel;
		EShopOfferKind Kind;
		int32 TrayTier; // 0=4 盘, 1=6 盘, 2=8 盘
	};

	const FShopOffer GShopOffers[] = {
		{EconomyShopItemIDs::TraySmall, TEXT("托盘 · 小"), TEXT("一次可收纳 4 个空碟，适合刚起步的小店。"), 80, 1, EShopOfferKind::Tray, 0},
		{EconomyShopItemIDs::TrayMedium, TEXT("托盘 · 中"), TEXT("一次可收纳 6 个空碟，收桌更省来回。"), 220, 1, EShopOfferKind::Tray, 1},
		{EconomyShopItemIDs::TrayLarge, TEXT("托盘 · 大"), TEXT("一次可收纳 8 个空碟，忙时也能一趟收完。"), 480, 1, EShopOfferKind::Tray, 2},
		{EconomyShopItemIDs::PanUpgrade, TEXT("煎锅升级"), TEXT("每级让所有灶台烹饪速度 +10%（最多 3 级）。"), 260, 3, EShopOfferKind::Pan, 0},
		{EconomyShopItemIDs::Juicer, TEXT("榨汁机"), TEXT("在厨房安装榨汁机，并解锁现榨果汁菜品。"), 600, 1, EShopOfferKind::Juicer, 0},
	};

	const FShopOffer* FindOffer(int32 ItemID)
	{
		for (const FShopOffer& Offer : GShopOffers)
		{
			if (Offer.ItemID == ItemID)
			{
				return &Offer;
			}
		}
		return nullptr;
	}

	int32 GetOfferCost(const FShopOffer& Offer, int32 Level)
	{
		// 煎锅按等级递增，其余一次性定价
		return Offer.Kind == EShopOfferKind::Pan ? Offer.Cost * (Level + 1) : Offer.Cost;
	}

	int32 GetHighestTrayTier(const UEconomySystem* Eco)
	{
		if (!Eco)
		{
			return -1;
		}
		if (Eco->GetOwnedItemCount(EconomyShopItemIDs::TrayLarge) > 0) return 2;
		if (Eco->GetOwnedItemCount(EconomyShopItemIDs::TrayMedium) > 0) return 1;
		if (Eco->GetOwnedItemCount(EconomyShopItemIDs::TraySmall) > 0) return 0;
		return -1;
	}

	/** 每位本地玩家一份商店实例（与菜谱图鉴同一套多人隔离方式） */
	TMap<TWeakObjectPtr<APlayerController>, TWeakObjectPtr<UShopScreenWidget>> ActiveShops;
}

UShopBuyButton::UShopBuyButton()
{
	InitIsFocusable(false);
}

void UShopBuyButton::InitOffer(UShopScreenWidget* InOwner, int32 InItemID, bool bInEquipment)
{
	OwnerWidget = InOwner;
	ItemID = InItemID;
	bEquipment = bInEquipment;
	OnClicked.AddDynamic(this, &UShopBuyButton::HandleClicked);
}

void UShopBuyButton::HandleClicked()
{
	if (UShopScreenWidget* Owner = OwnerWidget.Get())
	{
		if (bEquipment)
		{
			Owner->BuyEquipment(ItemID);
		}
		else
		{
			Owner->BuyIngredient(ItemID);
		}
	}
}

UShopScreenWidget::UShopScreenWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(true);

	static ConstructorHelpers::FObjectFinder<USoundBase> BuyFinder(TEXT("/Game/StackOBot/Audio/SFX/Waves/SFX_Coin_Collect.SFX_Coin_Collect"));
	static ConstructorHelpers::FObjectFinder<USoundBase> FailFinder(TEXT("/Game/Audio/SFX/MS_UI_Select.MS_UI_Select"));
	BuySound = BuyFinder.Object;
	FailSound = FailFinder.Object;
}

void UShopScreenWidget::ToggleForPlayer(APlayerController* PC, AShootCharacter* Character)
{
	if (!PC || !PC->IsLocalController())
	{
		return;
	}

	UShopScreenWidget* Screen = nullptr;
	if (const TWeakObjectPtr<UShopScreenWidget>* Found = ActiveShops.Find(TWeakObjectPtr<APlayerController>(PC)))
	{
		Screen = Found->Get();
	}

	// 已打开：再按一次关闭
	if (IsValid(Screen) && Screen->IsInViewport())
	{
		Screen->CloseSelf();
		return;
	}

	if (!IsValid(Screen))
	{
		Screen = CreateWidget<UShopScreenWidget>(PC, UShopScreenWidget::StaticClass());
		if (!Screen)
		{
			return;
		}
		Screen->OwnerController = PC;
		Screen->OwnerPawn = Character;
		if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(PC->GetGameInstance()))
		{
			Screen->BindEconomy(GI->GetEconomySystem());
		}
		ActiveShops.Add(TWeakObjectPtr<APlayerController>(PC), Screen);
	}
	else
	{
		Screen->OwnerPawn = Character;
	}

	Screen->AddToViewport(20);
	Screen->HideToast();
	Screen->Refresh();
	Screen->SetUIInputEnabled(true);
}

TSharedRef<SWidget> UShopScreenWidget::RebuildWidget()
{
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("ShopRoot"));
	WidgetTree->RootWidget = Root;

	// 背景：实时模糊 + 奶油薄纱，不额外使用背景图
	UBackgroundBlur* BackdropBlur = WidgetTree->ConstructWidget<UBackgroundBlur>(UBackgroundBlur::StaticClass(), TEXT("BackdropBlur"));
	BackdropBlur->SetBlurStrength(14.f);
	BackdropBlur->SetApplyAlphaToBlur(false);
	BackdropBlur->SetPadding(FMargin(0.f));
	BackdropBlur->SetHorizontalAlignment(HAlign_Fill);
	BackdropBlur->SetVerticalAlignment(VAlign_Fill);
	BackdropBlur->SetVisibility(ESlateVisibility::HitTestInvisible);
	UCanvasPanelSlot* BlurSlot = Root->AddChildToCanvas(BackdropBlur);
	BlurSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
	BlurSlot->SetOffsets(FMargin(0.f));

	UBorder* Veil = WidgetTree->ConstructWidget<UBorder>();
	Veil->SetBrushColor(FLinearColor(0.99f, 0.94f, 0.83f, 0.30f));
	Veil->SetVisibility(ESlateVisibility::HitTestInvisible);
	UCanvasPanelSlot* VeilSlot = Root->AddChildToCanvas(Veil);
	VeilSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
	VeilSlot->SetOffsets(FMargin(0.f));

	// 固定 1600x900 设计尺寸整体等比缩放
	UScaleBox* ScreenFit = WidgetTree->ConstructWidget<UScaleBox>();
	ScreenFit->SetStretch(EStretch::ScaleToFit);
	UCanvasPanelSlot* FitSlot = Root->AddChildToCanvas(ScreenFit);
	FitSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
	FitSlot->SetOffsets(FMargin(0.f));
	USizeBox* DesignSize = WidgetTree->ConstructWidget<USizeBox>();
	DesignSize->SetWidthOverride(1600.f);
	DesignSize->SetHeightOverride(900.f);
	ScreenFit->SetContent(DesignSize);
	UCanvasPanel* Design = WidgetTree->ConstructWidget<UCanvasPanel>();
	DesignSize->SetContent(Design);

	// 标题与装饰
	UTextBlock* Title = ShopText(WidgetTree, TEXT("Title"), FText::FromString(TEXT("田园杂货铺")), 62, ShopColor(TEXT("B87712")), 7);
	Title->SetJustification(ETextJustify::Center);
	Title->SetShadowOffset(FVector2D(0.f, 5.f));
	Title->SetShadowColorAndOpacity(ShopColor(TEXT("BE985C")));
	ShopPlace(Design, Title, FVector2D(430.f, 24.f), FVector2D(740.f, 96.f));
	ShopLeaf(WidgetTree, Design, FVector2D(482.f, 42.f), -32.f, ShopColor(TEXT("98B53B")));
	ShopLeaf(WidgetTree, Design, FVector2D(468.f, 69.f), -65.f, ShopColor(TEXT("AFCA56")));
	ShopLeaf(WidgetTree, Design, FVector2D(1090.f, 66.f), 36.f, ShopColor(TEXT("98B53B")));
	ShopLeaf(WidgetTree, Design, FVector2D(1108.f, 81.f), 63.f, ShopColor(TEXT("AFCA56")));

	UBorder* SubtitlePanel = ShopPanel(WidgetTree, ShopColor(TEXT("FFF0CE")), 19.f, ShopColor(TEXT("EBC995")), 2.f);
	SubtitlePanel->SetHorizontalAlignment(HAlign_Center);
	SubtitlePanel->SetVerticalAlignment(VAlign_Center);
	SubtitlePanel->SetContent(ShopText(WidgetTree, TEXT("Subtitle"),
		FText::FromString(TEXT("把辛苦赚来的钱，换成更顺手的设备与新鲜食材吧！")), 19, ShopColor(TEXT("75512C"))));
	ShopPlace(Design, SubtitlePanel, FVector2D(520.f, 112.f), FVector2D(560.f, 40.f));

	// 金币胶囊
	UBorder* MoneyCapsule = ShopPanel(WidgetTree, ShopColor(TEXT("FFF7DF")), 31.f, ShopColor(TEXT("EDBD76")), 2.5f);
	MoneyCapsule->SetHorizontalAlignment(HAlign_Center);
	MoneyCapsule->SetVerticalAlignment(VAlign_Center);
	MoneyText = ShopText(WidgetTree, TEXT("MoneyText"), FText::FromString(TEXT("金币  ¥ 0")), 26, ShopColor(TEXT("8D5817")), 1);
	MoneyCapsule->SetContent(MoneyText);
	ShopPlace(Design, MoneyCapsule, FVector2D(1296.f, 40.f), FVector2D(248.f, 62.f));

	// 主面板
	UBorder* PanelShadow = ShopPanel(WidgetTree, ShopColor(TEXT("A6814F")).CopyWithNewOpacity(0.35f), 29.f);
	ShopPlace(Design, PanelShadow, FVector2D(104.f, 178.f), FVector2D(1400.f, 656.f));
	UBorder* PanelFrame = ShopPanel(WidgetTree, ShopColor(TEXT("FFF9E7")), 29.f, ShopColor(TEXT("E8C79C")), 3.f);
	PanelFrame->SetPadding(FMargin(10.f));
	ShopPlace(Design, PanelFrame, FVector2D(100.f, 170.f), FVector2D(1400.f, 656.f));
	UBorder* PanelInner = ShopPanel(WidgetTree, ShopColor(TEXT("FFF3DC")), 22.f, ShopColor(TEXT("F1DDBA")), 2.f);
	PanelInner->SetPadding(FMargin(0.f));
	PanelFrame->SetContent(PanelInner);
	UCanvasPanel* InnerCanvas = WidgetTree->ConstructWidget<UCanvasPanel>();
	PanelInner->SetContent(InnerCanvas);

	// 页签
	TabEquipmentButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("TabEquipment"));
	TabEquipmentText = ShopText(WidgetTree, TEXT("TabEquipmentText"), FText::FromString(TEXT("设备")), 24, ShopColor(TEXT("8D5817")), 1);
	TabEquipmentText->SetJustification(ETextJustify::Center);
	TabEquipmentButton->SetContent(TabEquipmentText);
	TabEquipmentButton->OnClicked.AddDynamic(this, &UShopScreenWidget::OnEquipmentTabClicked);
	ShopPlace(InnerCanvas, TabEquipmentButton, FVector2D(42.f, 34.f), FVector2D(198.f, 60.f));

	TabIngredientButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("TabIngredient"));
	TabIngredientText = ShopText(WidgetTree, TEXT("TabIngredientText"), FText::FromString(TEXT("食材")), 24, ShopColor(TEXT("A7814E")), 1);
	TabIngredientText->SetJustification(ETextJustify::Center);
	TabIngredientButton->SetContent(TabIngredientText);
	TabIngredientButton->OnClicked.AddDynamic(this, &UShopScreenWidget::OnIngredientTabClicked);
	ShopPlace(InnerCanvas, TabIngredientButton, FVector2D(252.f, 34.f), FVector2D(198.f, 60.f));

	// 内容区
	ContentHost = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("ContentHost"));
	ShopPlace(InnerCanvas, ContentHost, FVector2D(42.f, 112.f), FVector2D(1316.f, 436.f));

	// 提示与关闭
	ToastPanel = ShopPanel(WidgetTree, ShopColor(TEXT("FFF7DF")), 22.f, ShopColor(TEXT("EDBD76")), 2.f);
	ToastPanel->SetHorizontalAlignment(HAlign_Center);
	ToastPanel->SetVerticalAlignment(VAlign_Center);
	ToastText = ShopText(WidgetTree, TEXT("ToastText"), FText(), 18, ShopColor(TEXT("8D5817")));
	ToastPanel->SetContent(ToastText);
	ToastPanel->SetVisibility(ESlateVisibility::Collapsed);
	ShopPlace(InnerCanvas, ToastPanel, FVector2D(470.f, 562.f), FVector2D(420.f, 44.f));

	InventoryHintText = ShopText(WidgetTree, TEXT("InventoryHint"), FText(), 14, ShopColor(TEXT("8B6B3F")));
	ShopPlace(InnerCanvas, InventoryHintText, FVector2D(44.f, 596.f), FVector2D(760.f, 30.f));

	UButton* CloseButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("CloseButton"));
	CloseButton->SetStyle(ShopButtonStyle(ShopColor(TEXT("F6C74E")), ShopColor(TEXT("FFF0AC")), 26.f, true));
	UTextBlock* CloseText = ShopText(WidgetTree, NAME_None, FText::FromString(TEXT("离开商店")), 26, ShopColor(TEXT("8D5817")), 1);
	CloseText->SetJustification(ETextJustify::Center);
	CloseButton->SetContent(CloseText);
	CloseButton->OnClicked.AddDynamic(this, &UShopScreenWidget::OnCloseClicked);
	ShopPlace(InnerCanvas, CloseButton, FVector2D(1124.f, 566.f), FVector2D(196.f, 62.f));

	ShopLeaf(WidgetTree, Design, FVector2D(132.f, 766.f), 26.f, ShopColor(TEXT("A4BD49")));
	ShopLeaf(WidgetTree, Design, FVector2D(148.f, 784.f), 58.f, ShopColor(TEXT("A4BD49")));

	return Root->TakeWidget();
}

void UShopScreenWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (Economy && !bBoundMoney)
	{
		Economy->OnMoneyChanged.AddUniqueDynamic(this, &UShopScreenWidget::HandleMoneyChangedInternal);
		bBoundMoney = true;
	}
	SetUIInputEnabled(true);
}

void UShopScreenWidget::NativeDestruct()
{
	if (Economy && bBoundMoney)
	{
		Economy->OnMoneyChanged.RemoveDynamic(this, &UShopScreenWidget::HandleMoneyChangedInternal);
		bBoundMoney = false;
	}
	if (UWorld* W = GetWorld())
	{
		W->GetTimerManager().ClearTimer(ToastTimer);
	}
	SetUIInputEnabled(false);
	Super::NativeDestruct();
}

FReply UShopScreenWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::P || Key == EKeys::Escape)
	{
		CloseSelf();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UShopScreenWidget::OnEquipmentTabClicked()
{
	SelectTab(true);
}

void UShopScreenWidget::OnIngredientTabClicked()
{
	SelectTab(false);
}

void UShopScreenWidget::OnCloseClicked()
{
	CloseSelf();
}

void UShopScreenWidget::SelectTab(bool bNewEquipmentTab)
{
	bEquipmentTab = bNewEquipmentTab;
	if (TabEquipmentButton && TabIngredientButton && TabEquipmentText && TabIngredientText)
	{
		TabEquipmentButton->SetStyle(bEquipmentTab
			? ShopButtonStyle(ShopColor(TEXT("F6C74E")), ShopColor(TEXT("FFF0AC")), 22.f, true)
			: ShopButtonStyle(ShopColor(TEXT("F4E4C2")), ShopColor(TEXT("E3CFA5")), 22.f, true));
		TabIngredientButton->SetStyle(!bEquipmentTab
			? ShopButtonStyle(ShopColor(TEXT("F6C74E")), ShopColor(TEXT("FFF0AC")), 22.f, true)
			: ShopButtonStyle(ShopColor(TEXT("F4E4C2")), ShopColor(TEXT("E3CFA5")), 22.f, true));
		TabEquipmentText->SetColorAndOpacity(FSlateColor(bEquipmentTab ? ShopColor(TEXT("8D5817")) : ShopColor(TEXT("A7814E"))));
		TabIngredientText->SetColorAndOpacity(FSlateColor(!bEquipmentTab ? ShopColor(TEXT("8D5817")) : ShopColor(TEXT("A7814E"))));
	}
	if (ContentHost)
	{
		RebuildContent();
	}
}

void UShopScreenWidget::Refresh()
{
	UpdateMoneyText();
	SelectTab(bEquipmentTab);
}

void UShopScreenWidget::HandleMoneyChangedInternal(int32 NewValue, int32 Delta, EMoneySource Source)
{
	// 购买后金钱广播会比“购买成功”提示先到：这里只刷新界面，不动提示条
	UpdateMoneyText();
	RebuildContent();
}

void UShopScreenWidget::UpdateMoneyText()
{
	if (MoneyText)
	{
		MoneyText->SetText(FText::FromString(FString::Printf(TEXT("金币  ¥ %d"), GetMoney())));
	}
	if (InventoryHintText)
	{
		const AShootCharacter* Player = OwnerPawn.Get();
		const FString PlayerPart = Player
			? FString::Printf(TEXT("背包 %d 件"), Player->GetInventoryCount())
			: FString(TEXT("NPC 未绑定"));
		InventoryHintText->SetText(FText::FromString(
			FString::Printf(TEXT("%s · 点击卡片按钮购买 · P / Esc 关闭"), *PlayerPart)));
	}
}

void UShopScreenWidget::RebuildContent()
{
	if (!ContentHost)
	{
		return;
	}
	ContentHost->ClearChildren();
	if (bEquipmentTab)
	{
		RebuildEquipmentCards();
	}
	else
	{
		RebuildIngredientCards();
	}
}

void UShopScreenWidget::RebuildEquipmentCards()
{
	UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
	UUniformGridPanel* Grid = WidgetTree->ConstructWidget<UUniformGridPanel>();
	Grid->SetMinDesiredSlotWidth(410.f);
	Grid->SetMinDesiredSlotHeight(204.f);
	Scroll->AddChild(Grid);
	ShopPlace(ContentHost, Scroll, FVector2D(0.f, 0.f), FVector2D(1316.f, 436.f));

	const int32 HighestTray = GetHighestTrayTier(Economy);
	const int32 OfferCount = UE_ARRAY_COUNT(GShopOffers);
	for (int32 Index = 0; Index < OfferCount; ++Index)
	{
		const FShopOffer& Offer = GShopOffers[Index];
		const int32 Level = Economy ? Economy->GetOwnedItemCount(Offer.ItemID) : 0;

		bool bOwned = Level > 0;
		bool bSuperseded = false;
		if (Offer.Kind == EShopOfferKind::Tray)
		{
			bOwned = Level > 0;
			bSuperseded = HighestTray > Offer.TrayTier;
		}

		const bool bMaxed = bOwned || Level >= Offer.MaxLevel;
		const bool bButtonEnabled = !bMaxed && !bSuperseded;

		FString Status;
		FString ButtonLabel;
		if (Offer.Kind == EShopOfferKind::Pan)
		{
			Status = FString::Printf(TEXT("Lv %d / %d"), Level, Offer.MaxLevel);
			ButtonLabel = bMaxed ? TEXT("已满级") : TEXT("升级");
		}
		else if (Offer.Kind == EShopOfferKind::Juicer)
		{
			Status = bOwned ? TEXT("已安装") : TEXT("未安装");
			ButtonLabel = bMaxed ? TEXT("已安装") : TEXT("购买");
		}
		else
		{
			Status = bOwned ? TEXT("已拥有") : (bSuperseded ? TEXT("已有更高档") : TEXT("未拥有"));
			ButtonLabel = bOwned ? TEXT("已拥有") : (bSuperseded ? TEXT("已有更高档") : TEXT("购买"));
		}

		UWidget* Card = BuildOfferCard(Index, Level, Status, bButtonEnabled, ButtonLabel);
		UUniformGridSlot* GridSlot = Grid->AddChildToUniformGrid(Card, Index / 3, Index % 3);
		GridSlot->SetHorizontalAlignment(HAlign_Fill);
		GridSlot->SetVerticalAlignment(VAlign_Fill);
	}
}

UWidget* UShopScreenWidget::BuildOfferCard(int32 OfferIndex, int32 Level, const FString& StatusText,
	bool bButtonEnabled, const FString& ButtonLabel)
{
	const FShopOffer& Offer = GShopOffers[OfferIndex];
	const FShopPalette Palette = GetShopPalette(OfferIndex);
	const int32 Cost = GetOfferCost(Offer, Level);
	const bool bAffordable = GetMoney() >= Cost;

	UBorder* Card = ShopPanel(WidgetTree, Palette.Fill, 22.f, Palette.Rim, 2.f);
	Card->SetPadding(FMargin(18.f, 12.f));
	Card->SetClipping(EWidgetClipping::ClipToBounds);
	Card->SetVisibility(ESlateVisibility::Visible);

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>();
	Card->SetContent(Column);

	UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>();
	Column->AddChildToVerticalBox(Header);
	Header->AddChildToHorizontalBox(ShopText(WidgetTree, NAME_None, FText::FromString(Offer.Name), 24, Palette.Ink, 2));
	FillSpacer(WidgetTree, Header);

	UBorder* Pill = ShopPanel(WidgetTree, FMath::Lerp(Palette.Fill, FLinearColor::White, 0.45f), 12.f, Palette.Rim, 1.5f);
	Pill->SetPadding(FMargin(10.f, 2.f));
	Pill->SetHorizontalAlignment(HAlign_Center);
	Pill->SetVerticalAlignment(VAlign_Center);
	Pill->SetContent(ShopText(WidgetTree, NAME_None, FText::FromString(StatusText), 13, Palette.Ink));
	Header->AddChildToHorizontalBox(Pill);

	UTextBlock* Desc = ShopText(WidgetTree, NAME_None, FText::FromString(Offer.Desc), 13, ShopColor(TEXT("645039")));
	Desc->SetAutoWrapText(true);
	Desc->SetWrapTextAt(390.f);
	if (UVerticalBoxSlot* DescSlot = Column->AddChildToVerticalBox(Desc))
	{
		DescSlot->SetPadding(FMargin(0.f, 8.f, 0.f, 0.f));
	}

	USpacer* VerticalFill = WidgetTree->ConstructWidget<USpacer>();
	if (UVerticalBoxSlot* FillSlot = Column->AddChildToVerticalBox(VerticalFill))
	{
		FillSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	UHorizontalBox* Bottom = WidgetTree->ConstructWidget<UHorizontalBox>();
	Column->AddChildToVerticalBox(Bottom);
	UTextBlock* PriceText = ShopText(WidgetTree, NAME_None, FText::FromString(FString::Printf(TEXT("¥ %d"), Cost)), 26,
		bAffordable ? ShopColor(TEXT("8D5817")) : ShopColor(TEXT("B0563C")), 1);
	Bottom->AddChildToHorizontalBox(PriceText);
	FillSpacer(WidgetTree, Bottom);

	UShopBuyButton* BuyButton = WidgetTree->ConstructWidget<UShopBuyButton>();
	BuyButton->InitOffer(this, Offer.ItemID, true);
	BuyButton->SetStyle(ShopButtonStyle(ShopColor(TEXT("F6C74E")), ShopColor(TEXT("FFF0AC")), 20.f, true));
	UTextBlock* BuyText = ShopText(WidgetTree, NAME_None, FText::FromString(ButtonLabel), 17,
		bButtonEnabled ? ShopColor(TEXT("8D5817")) : ShopColor(TEXT("9C8A6D")), 1);
	BuyText->SetJustification(ETextJustify::Center);
	BuyButton->SetContent(BuyText);
	BuyButton->SetIsEnabled(bButtonEnabled);
	USizeBox* ButtonSize = WidgetTree->ConstructWidget<USizeBox>();
	ButtonSize->SetWidthOverride(136.f);
	ButtonSize->SetHeightOverride(46.f);
	ButtonSize->SetContent(BuyButton);
	Bottom->AddChildToHorizontalBox(ButtonSize);

	return Card;
}

void UShopScreenWidget::RebuildIngredientCards()
{
	GatherIngredientSources();

	UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
	UUniformGridPanel* Grid = WidgetTree->ConstructWidget<UUniformGridPanel>();
	Grid->SetMinDesiredSlotWidth(316.f);
	Grid->SetMinDesiredSlotHeight(206.f);
	Scroll->AddChild(Grid);
	ShopPlace(ContentHost, Scroll, FVector2D(0.f, 0.f), FVector2D(1316.f, 436.f));

	TArray<int32> ItemIDs;
	IngredientClasses.GetKeys(ItemIDs);
	ItemIDs.Sort();

	int32 Placed = 0;
	for (int32 ItemID : ItemIDs)
	{
		bool bAvailable = false;
		const int32 Price = GetIngredientPrice(ItemID, bAvailable);
		if (!bAvailable || Price <= 0)
		{
			continue;
		}

		const FDataTableRowHandle* RowHandle = IngredientRows.Find(ItemID);
		const FItemActorInfo* Info = RowHandle ? RowHandle->GetRow<FItemActorInfo>(TEXT("ShopScreenWidget::RebuildIngredientCards")) : nullptr;
		const FString ItemName = GetItemDisplayNameSafe(ItemID);
		UTexture2D* Icon = Info ? Info->Icon.Get() : nullptr;
		const bool bWildOnly = Info ? Info->bWildOnly : false;

		UWidget* Card = BuildIngredientCard(ItemID, ItemName, Icon, bWildOnly, Price, GetMoney() >= Price);
		UUniformGridSlot* GridSlot = Grid->AddChildToUniformGrid(Card, Placed / 4, Placed % 4);
		GridSlot->SetHorizontalAlignment(HAlign_Fill);
		GridSlot->SetVerticalAlignment(VAlign_Fill);
		++Placed;
	}

	if (Placed == 0)
	{
		UTextBlock* Empty = ShopText(WidgetTree, NAME_None,
			FText::FromString(TEXT("暂时没有可上架的食材：请检查物品表与食材蓝图。")), 18, ShopColor(TEXT("8B6B3F")));
		ShopPlace(ContentHost, Empty, FVector2D(20.f, 20.f), FVector2D(800.f, 40.f));
	}
}

UWidget* UShopScreenWidget::BuildIngredientCard(int32 ItemID, const FString& ItemName, UTexture2D* Icon,
	bool bWildOnly, int32 Price, bool bAffordable)
{
	const FShopPalette Palette = GetShopPalette(ItemID + 1);

	UBorder* Card = ShopPanel(WidgetTree, Palette.Fill, 22.f, Palette.Rim, 2.f);
	Card->SetPadding(FMargin(16.f, 12.f));
	Card->SetClipping(EWidgetClipping::ClipToBounds);
	Card->SetVisibility(ESlateVisibility::Visible);
	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>();
	Card->SetContent(Column);

	UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>();
	Column->AddChildToVerticalBox(Header);

	USizeBox* ItemIconBox = WidgetTree->ConstructWidget<USizeBox>();
	ItemIconBox->SetWidthOverride(58.f);
	ItemIconBox->SetHeightOverride(58.f);
	UImage* IconImage = WidgetTree->ConstructWidget<UImage>();
	if (Icon)
	{
		IconImage->SetBrushFromTexture(Icon, true);
	}
	IconImage->SetVisibility(ESlateVisibility::HitTestInvisible);
	ItemIconBox->SetContent(IconImage);
	Header->AddChildToHorizontalBox(ItemIconBox);

	UVerticalBox* NameColumn = WidgetTree->ConstructWidget<UVerticalBox>();
	if (UHorizontalBoxSlot* NameSlot = Header->AddChildToHorizontalBox(NameColumn))
	{
		NameSlot->SetPadding(FMargin(10.f, 2.f, 0.f, 0.f));
		NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}
	UTextBlock* NameText = ShopText(WidgetTree, NAME_None, FText::FromString(ItemName), 19, Palette.Ink, 1);
	NameText->SetAutoWrapText(true);
	NameText->SetWrapTextAt(190.f);
	NameColumn->AddChildToVerticalBox(NameText);

	UBorder* TagPill = ShopPanel(WidgetTree, FMath::Lerp(Palette.Fill, FLinearColor::White, 0.45f), 10.f, Palette.Rim, 1.2f);
	TagPill->SetPadding(FMargin(8.f, 1.f));
	TagPill->SetHorizontalAlignment(HAlign_Left);
	TagPill->SetVerticalAlignment(VAlign_Center);
	TagPill->SetContent(ShopText(WidgetTree, NAME_None,
		FText::FromString(bWildOnly ? TEXT("野外食材") : TEXT("厨房食材")), 11, Palette.Ink));
	if (UVerticalBoxSlot* TagSlot = NameColumn->AddChildToVerticalBox(TagPill))
	{
		TagSlot->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));
		TagSlot->SetHorizontalAlignment(HAlign_Left);
	}

	USpacer* VerticalFill = WidgetTree->ConstructWidget<USpacer>();
	if (UVerticalBoxSlot* FillSlot = Column->AddChildToVerticalBox(VerticalFill))
	{
		FillSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	UHorizontalBox* Bottom = WidgetTree->ConstructWidget<UHorizontalBox>();
	Column->AddChildToVerticalBox(Bottom);
	UTextBlock* PriceText = ShopText(WidgetTree, NAME_None, FText::FromString(FString::Printf(TEXT("¥ %d"), Price)), 22,
		bAffordable ? ShopColor(TEXT("8D5817")) : ShopColor(TEXT("B0563C")), 1);
	Bottom->AddChildToHorizontalBox(PriceText);
	FillSpacer(WidgetTree, Bottom);

	UShopBuyButton* BuyButton = WidgetTree->ConstructWidget<UShopBuyButton>();
	BuyButton->InitOffer(this, ItemID, false);
	BuyButton->SetStyle(ShopButtonStyle(ShopColor(TEXT("F6C74E")), ShopColor(TEXT("FFF0AC")), 18.f, true));
	UTextBlock* BuyText = ShopText(WidgetTree, NAME_None, FText::FromString(TEXT("购买")), 16, ShopColor(TEXT("8D5817")), 1);
	BuyText->SetJustification(ETextJustify::Center);
	BuyButton->SetContent(BuyText);
	USizeBox* ButtonSize = WidgetTree->ConstructWidget<USizeBox>();
	ButtonSize->SetWidthOverride(108.f);
	ButtonSize->SetHeightOverride(42.f);
	ButtonSize->SetContent(BuyButton);
	Bottom->AddChildToHorizontalBox(ButtonSize);

	return Card;
}

void UShopScreenWidget::BuyEquipment(int32 ItemID)
{
	const FShopOffer* Offer = FindOffer(ItemID);
	if (!Offer || !Economy)
	{
		return;
	}

	const int32 Level = Economy->GetOwnedItemCount(ItemID);
	if (Level >= Offer->MaxLevel)
	{
		ShowToast(FString::Printf(TEXT("%s 已经达到最高等级"), Offer->Name), false);
		return;
	}
	if (Offer->Kind == EShopOfferKind::Tray && GetHighestTrayTier(Economy) > Offer->TrayTier)
	{
		ShowToast(TEXT("你已经拥有更高档的托盘"), false);
		return;
	}

	const int32 Cost = GetOfferCost(*Offer, Level);
	if (!Economy->BuyItem(ItemID, Cost))
	{
		ShowToast(TEXT("金币不足，先去完成几单生意吧"), false);
		return;
	}

	ApplyEquipmentEffect(ItemID);
	ShowToast(FString::Printf(TEXT("购买成功：%s"), Offer->Name), true);
	Refresh();
}

void UShopScreenWidget::ApplyEquipmentEffect(int32 ItemID)
{
	if (const FShopOffer* Offer = FindOffer(ItemID))
	{
		if (Offer->Kind == EShopOfferKind::Tray)
		{
			if (UWorld* W = GetWorld())
			{
				for (TActorIterator<APlateCollectionTray> It(W); It; ++It)
				{
					It->RefreshCapacityFromUpgrades();
				}
			}
		}
		else if (Offer->Kind == EShopOfferKind::Juicer)
		{
			if (UWorld* W = GetWorld())
			{
				for (TActorIterator<ACookingMachine> It(W); It; ++It)
				{
					It->RefreshShopLock();
				}
				// 设备装好后立刻重建订单池，当天就能出果汁订单
				for (TActorIterator<AOrderSystem> It(W); It; ++It)
				{
					It->RebuildOrderPool();
				}
			}
		}
	}
}

void UShopScreenWidget::BuyIngredient(int32 ItemID)
{
	AShootCharacter* Player = OwnerPawn.Get();
	if (!Player || !Economy)
	{
		ShowToast(TEXT("商店还没有准备好"), false);
		return;
	}

	GatherIngredientSources();
	const TSubclassOf<AItemActor>* ClassPtr = IngredientClasses.Find(ItemID);
	const FDataTableRowHandle* RowPtr = IngredientRows.Find(ItemID);
	if (!ClassPtr || !RowPtr || !*ClassPtr)
	{
		ShowToast(TEXT("这件食材暂时没有货源"), false);
		return;
	}

	bool bAvailable = false;
	const int32 Price = GetIngredientPrice(ItemID, bAvailable);
	if (!bAvailable || Price <= 0)
	{
		ShowToast(TEXT("这件食材暂时没有上架"), false);
		return;
	}

	if (!Economy->SpendMoney(Price, EMoneySource::ShopPurchase))
	{
		ShowToast(TEXT("金币不足，先去完成几单生意吧"), false);
		return;
	}

	FInventorySlot NewSlot;
	NewSlot.ItemClass = *ClassPtr;
	NewSlot.ItemInfoRow = *RowPtr;
	NewSlot.DisplayName = GetItemDisplayNameSafe(ItemID);
	NewSlot.Quantity = 1;
	if (!Player->AddSlotToInventory(NewSlot))
	{
		Economy->AddMoney(Price, EMoneySource::ShopPurchase);
		ShowToast(TEXT("背包已满，先把东西放下或存进仓库吧"), false);
		return;
	}

	ShowToast(FString::Printf(TEXT("已放入背包：%s ×1"), *NewSlot.DisplayName), true);
	Refresh();
}

void UShopScreenWidget::GatherIngredientSources()
{
	if (bGatheredIngredients)
	{
		return;
	}
	bGatheredIngredients = true;
	IngredientClasses.Reset();
	IngredientRows.Reset();

	if (!bTriedLoadIngredientTable)
	{
		bTriedLoadIngredientTable = true;
		const TCHAR* TablePath = TEXT("/Game/_Demo/Data/DT_ShopIngredients");
		if (FPackageName::DoesPackageExist(TablePath))
		{
			IngredientTable = LoadObject<UDataTable>(nullptr, TablePath);
		}
	}

	const UBaseGameInstance* GI = GetWorld() ? Cast<UBaseGameInstance>(GetWorld()->GetGameInstance()) : nullptr;
	const UDataTable* ExpectedItemTable = GI && GI->foodSystem ? GI->foodSystem->ItemInfoTable.Get() : nullptr;

	// 扫描所有已加载的 AItemActor 蓝图子类，用它们的物品表行反推 ItemID -> 生成类
	TArray<UClass*> DerivedClasses;
	GetDerivedClasses(AItemActor::StaticClass(), DerivedClasses, true);
	for (UClass* Class : DerivedClasses)
	{
		if (!Class || Class->HasAnyClassFlags(CLASS_Abstract))
		{
			continue;
		}
		if (Class->IsChildOf(AFoodSpawner::StaticClass()) || Class->IsChildOf(APlateCollectionTray::StaticClass()))
		{
			continue;
		}
		AItemActor* CDO = Cast<AItemActor>(Class->GetDefaultObject());
		if (!CDO || !CDO->ItemInfoRow.DataTable)
		{
			continue;
		}
		if (ExpectedItemTable && CDO->ItemInfoRow.DataTable != ExpectedItemTable)
		{
			continue;
		}
		const int32 ItemID = CDO->GetItemID();
		if (ItemID < 0 || IngredientClasses.Contains(ItemID))
		{
			continue;
		}
		IngredientClasses.Add(ItemID, Class);
		IngredientRows.Add(ItemID, CDO->ItemInfoRow);
	}
}

int32 UShopScreenWidget::GetIngredientPrice(int32 ItemID, bool& bOutAvailable) const
{
	bOutAvailable = false;

	if (IngredientTable)
	{
		for (const TPair<FName, uint8*>& Pair : IngredientTable->GetRowMap())
		{
			const FShopIngredientRow* Row = reinterpret_cast<FShopIngredientRow*>(Pair.Value);
			if (!Row)
			{
				continue;
			}
			const FString RowNameStr = Pair.Key.ToString();
			const int32 RowItemID = RowNameStr.IsNumeric() ? FCString::Atoi(*RowNameStr) : Row->ItemID;
			if (RowItemID == ItemID)
			{
				bOutAvailable = Row->bAvailable && Row->Price > 0;
				return Row->Price;
			}
		}
		return 0;
	}

	// 内置默认价格：比“自己去野外采”要贵，但不至于让做菜亏本
	static const TMap<int32, int32> DefaultPrices = {
		{0, 18}, // 番茄
		{1, 18}, // 玉米
		{2, 24}, // 鸡蛋
		{3, 65}, // 牛排
		{4, 26}, // 生蘑菇
		{5, 46}, // 切好蘑菇
		{8, 52}, // 生菜（野外）
		{9, 22}  // 罗勒
	};
	if (const int32* Price = DefaultPrices.Find(ItemID))
	{
		bOutAvailable = *Price > 0;
		return *Price;
	}
	return 0;
}

FString UShopScreenWidget::GetItemDisplayNameSafe(int32 ItemID) const
{
	if (const FDataTableRowHandle* Handle = IngredientRows.Find(ItemID))
	{
		if (const FItemActorInfo* Row = Handle->GetRow<FItemActorInfo>(TEXT("ShopScreenWidget::GetItemDisplayNameSafe")))
		{
			if (!Row->DisplayName.IsEmpty()) return Row->DisplayName;
			if (!Row->ItemDes.IsEmpty()) return Row->ItemDes;
		}
	}
	return FString::Printf(TEXT("食材#%d"), ItemID);
}

void UShopScreenWidget::ShowToast(const FString& Message, bool bSuccess)
{
	if (UWorld* W = GetWorld())
	{
		W->GetTimerManager().ClearTimer(ToastTimer);
		if (ToastPanel && ToastText)
		{
			ToastPanel->SetVisibility(ESlateVisibility::HitTestInvisible);
			ToastPanel->SetBrush(FSlateRoundedBoxBrush(
				bSuccess ? ShopColor(TEXT("FFF7DF")) : ShopColor(TEXT("F8DCD0")), 22.f,
				bSuccess ? ShopColor(TEXT("EDBD76")) : ShopColor(TEXT("D79A86")), 2.f));
			ToastText->SetText(FText::FromString(Message));
			ToastText->SetColorAndOpacity(FSlateColor(bSuccess ? ShopColor(TEXT("8D5817")) : ShopColor(TEXT("9C4A32"))));
		}
		UGameplayStatics::PlaySound2D(this, bSuccess ? BuySound : FailSound, bSuccess ? 0.5f : 0.35f);
		W->GetTimerManager().SetTimer(ToastTimer, this, &UShopScreenWidget::HideToast, 2.4f, false);
	}
}

void UShopScreenWidget::HideToast()
{
	if (ToastPanel)
	{
		ToastPanel->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UShopScreenWidget::SetUIInputEnabled(bool bEnabled)
{
	APlayerController* PC = OwnerController.Get();
	if (!PC)
	{
		return;
	}
	if (bEnabled)
	{
		FInputModeUIOnly Mode;
		Mode.SetWidgetToFocus(TakeWidget());
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(Mode);
		PC->bShowMouseCursor = true;
	}
	else
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->bShowMouseCursor = false;
	}
}

void UShopScreenWidget::CloseSelf()
{
	SetUIInputEnabled(false);
	RemoveFromParent();
}
