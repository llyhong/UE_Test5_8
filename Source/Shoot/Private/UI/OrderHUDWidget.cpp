// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/OrderHUDWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Game/BaseGameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Styling/CoreStyle.h"
#include "UObject/ConstructorHelpers.h"

namespace OrderHUD
{
	// —— 卡片外观常量（1080p 设计尺寸，与概念图一致）——
	constexpr float CardWidth = 358.f;
	constexpr float CardHeight = 176.f;
	constexpr float CardGap = 26.f;
	constexpr float TopMargin = 25.f;

	constexpr float DishSize = 106.f;

	// 食材格
	constexpr float TileSize = 42.f;
	constexpr float TileGap = 6.f;

	// 等待条
	constexpr float BarX = 175.f;
	constexpr float BarY = 118.f;
	constexpr float BarWidth = 156.f;
	constexpr float BarHeight = 13.f;

	const FLinearColor ColorCardInner(249.f / 255.f, 228.f / 255.f, 201.f / 255.f, 1.f);
	const FLinearColor ColorGold(224.f / 255.f, 170.f / 255.f, 64.f / 255.f, 1.f);
	const FLinearColor ColorInnerLine(246.f / 255.f, 220.f / 255.f, 180.f / 255.f, 1.f);
	const FLinearColor ColorWell(252.f / 255.f, 242.f / 255.f, 226.f / 255.f, 1.f);
	const FLinearColor ColorTileFill(250.f / 255.f, 229.f / 255.f, 197.f / 255.f, 1.f);
	const FLinearColor ColorTileLine(236.f / 255.f, 205.f / 255.f, 163.f / 255.f, 1.f);
	const FLinearColor ColorText(43.f / 255.f, 33.f / 255.f, 20.f / 255.f, 1.f);
	const FLinearColor ColorBarTrack(244.f / 255.f, 214.f / 255.f, 178.f / 255.f, 1.f);
	const FLinearColor ColorBarTrackLine(232.f / 255.f, 196.f / 255.f, 152.f / 255.f, 1.f);
	const FLinearColor ColorBarFill(240.f / 255.f, 138.f / 255.f, 20.f / 255.f, 1.f);
	const FLinearColor ColorBarLow(214.f / 255.f, 69.f / 255.f, 54.f / 255.f, 1.f);
	const FLinearColor ColorTransparent(0.f, 0.f, 0.f, 0.f);

	/** 圆角矩形画刷：边框为 0 时就是纯色圆角块 */
	static FSlateBrush RoundedBrush(float Radius, const FLinearColor& Fill, float LineWidth = 0.f, const FLinearColor& LineColor = FLinearColor::Transparent)
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.TintColor = FSlateColor(Fill);
		Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		Brush.OutlineSettings.CornerRadii = FVector4(Radius, Radius, Radius, Radius);
		Brush.OutlineSettings.Width = LineWidth;
		Brush.OutlineSettings.Color = FSlateColor(LineColor);
		return Brush;
	}
}

UOrderHUDWidget::UOrderHUDWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	static ConstructorHelpers::FObjectFinder<USoundBase> ChimeFinder(
		TEXT("/Game/StackOBot/Audio/SFX/Waves/SFX_Coin_Collect.SFX_Coin_Collect"));
	StreakChime = ChimeFinder.Object;

	static ConstructorHelpers::FObjectFinder<UTexture2D> CoinFinder(TEXT("/Game/_Demo/Textures/ICON/T_UI_Coin.T_UI_Coin"));
	if (CoinFinder.Succeeded())
	{
		CoinTexture = CoinFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UTexture2D> ClockFinder(TEXT("/Game/_Demo/Textures/ICON/T_UI_Clock.T_UI_Clock"));
	if (ClockFinder.Succeeded())
	{
		ClockTexture = ClockFinder.Object;
	}
}

TSharedRef<SWidget> UOrderHUDWidget::RebuildWidget()
{
	// 首次挂载（WBP 设计器里没有搭控件树）时在这里一次性建出根画布与订单行；
	// 已建过则直接复用，避免重复挂载时重复创建
	if (WidgetTree && !WidgetTree->FindWidget(TEXT("RootCanvas")))
	{
		BuildLayout();
	}

	return Super::RebuildWidget();
}

void UOrderHUDWidget::NativeDestruct()
{
	// 把卡片从订单行上摘掉，避免重复挂载时残留旧卡片；控件树本身归 WidgetTree 所有
	if (OrderRow)
	{
		for (const FCardWidgets& Card : Cards)
		{
			if (Card.Root)
			{
				OrderRow->RemoveChild(Card.Root);
			}
		}
	}
	Cards.Reset();

	Super::NativeDestruct();
}

void UOrderHUDWidget::BuildLayout()
{
	SetIsFocusable(false);
	SetVisibility(ESlateVisibility::HitTestInvisible);

	RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
	RootCanvas->SetVisibility(ESlateVisibility::HitTestInvisible);
	WidgetTree->RootWidget = RootCanvas;

	// 顶部居中：锚点固定在画布顶部中点，整行随内容自动定宽
	OrderRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("OrderRow"));
	OrderRow->SetVisibility(ESlateVisibility::HitTestInvisible);

	UCanvasPanelSlot* RowSlot = RootCanvas->AddChildToCanvas(OrderRow);
	RowSlot->SetAnchors(FAnchors(0.5f, 0.f, 0.5f, 0.f));
	RowSlot->SetAlignment(FVector2D(0.5f, 0.f));
	RowSlot->SetAutoSize(true);
	RowSlot->SetPosition(FVector2D(0.f, OrderHUD::TopMargin));

	StreakBadge = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("OnTimeStreakBadge"));
	StreakBadge->SetBrush(OrderHUD::RoundedBrush(13.f, OrderHUD::ColorWell, 1.f, OrderHUD::ColorGold));
	StreakBadge->SetPadding(FMargin(16.f, 5.f));
	StreakBadge->SetVisibility(ESlateVisibility::Collapsed);
	StreakText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("OnTimeStreakText"));
	StreakText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 18));
	StreakText->SetColorAndOpacity(FSlateColor(OrderHUD::ColorText));
	StreakBadge->SetContent(StreakText);

	UCanvasPanelSlot* BadgeSlot = RootCanvas->AddChildToCanvas(StreakBadge);
	BadgeSlot->SetAnchors(FAnchors(0.5f, 0.f));
	BadgeSlot->SetAlignment(FVector2D(0.5f, 0.f));
	BadgeSlot->SetAutoSize(true);
	BadgeSlot->SetPosition(FVector2D(0.f, OrderHUD::TopMargin + OrderHUD::CardHeight + 9.f));

	bLayoutBuilt = true;
}

void UOrderHUDWidget::HandleOnTimeStreakChanged(int32 Count)
{
	if (!StreakBadge || !StreakText)
	{
		return;
	}
	if (Count < 3)
	{
		StreakTimeRemaining = 0.f;
		StreakBadge->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	StreakText->SetText(FText::Format(NSLOCTEXT("OrderHUD", "OnTimeStreak", "连续准时 ×{0}"), FText::AsNumber(Count)));
	StreakTimeRemaining = 2.25f;
	StreakBadge->SetRenderOpacity(1.f);
	StreakBadge->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (Count == 3 && StreakChime)
	{
		UGameplayStatics::PlaySound2D(this, StreakChime, 0.45f);
	}
}

void UOrderHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!bLayoutBuilt)
	{
		return;
	}

	SyncCards();
	if (StreakTimeRemaining > 0.f)
	{
		StreakTimeRemaining -= InDeltaTime;
		StreakBadge->SetRenderOpacity(FMath::Clamp(StreakTimeRemaining / 0.3f, 0.f, 1.f));
		if (StreakTimeRemaining <= 0.f)
		{
			StreakBadge->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	const UWorld* World = GetWorld();
	const float Now = World ? static_cast<float>(World->GetTimeSeconds()) : 0.f;

	for (FCardWidgets& Card : Cards)
	{
		if (!Card.Handle || !Card.WaitBar || !Card.Root)
		{
			continue;
		}

		// 等待条：百分比每帧刷新，快超时时改红提示
		const float Percent = FMath::Clamp(Card.Handle->GetPercent(), 0.f, 1.f);
		Card.WaitBar->SetPercent(Percent);
		Card.WaitBar->SetFillColorAndOpacity(Percent < 0.28f ? OrderHUD::ColorBarLow : OrderHUD::ColorBarFill);

		// 新卡片 0.16s 轻微上浮淡入，老卡片不受影响
		const float Age = Now - Card.SpawnTime;
		const float T = FMath::Clamp(Age / 0.16f, 0.f, 1.f);
		Card.Root->SetRenderOpacity(T);
		Card.Root->SetRenderTranslation(FVector2D(0.f, 8.f * (1.f - T)));
	}
}

void UOrderHUDWidget::SyncCards()
{
	// 过滤空句柄，得到本帧应显示的列表
	TArray<UOrderHandle*> Current;
	Current.Reserve(WaitingOrders.Num());
	for (UOrderHandle* Handle : WaitingOrders)
	{
		if (Handle)
		{
			Current.Add(Handle);
		}
	}

	bool bSame = Current.Num() == Cards.Num();
	if (bSame)
	{
		for (int32 Index = 0; Index < Current.Num(); ++Index)
		{
			if (Cards[Index].Handle != Current[Index])
			{
				bSame = false;
				break;
			}
		}
	}

	if (!bSame)
	{
		RebuildCards();
	}
}

void UOrderHUDWidget::RebuildCards()
{
	if (!OrderRow)
	{
		return;
	}

	// 记录已存在卡片的出场时间：重建后老卡片不再重播淡入
	TMap<UOrderHandle*, float> OldSpawnTimes;
	for (const FCardWidgets& Old : Cards)
	{
		if (Old.Handle && Old.Root)
		{
			OldSpawnTimes.Add(Old.Handle, Old.SpawnTime);
			OrderRow->RemoveChild(Old.Root);
		}
	}
	Cards.Reset();

	const UWorld* World = GetWorld();
	const float Now = World ? static_cast<float>(World->GetTimeSeconds()) : 0.f;

	for (UOrderHandle* Handle : WaitingOrders)
	{
		if (!Handle)
		{
			continue;
		}

		FCardWidgets Card = CreateCard(Handle);
		if (const float* OldSpawn = OldSpawnTimes.Find(Handle))
		{
			Card.SpawnTime = *OldSpawn;
		}
		else
		{
			Card.SpawnTime = Now;
		}
		Cards.Add(Card);
	}

	// 最后一张卡片去掉右侧间距，保证整行视觉居中
	if (Cards.Num() > 0 && Cards.Last().Root)
	{
		if (UHorizontalBoxSlot* LastSlot = Cast<UHorizontalBoxSlot>(Cards.Last().Root->Slot))
		{
			LastSlot->SetPadding(FMargin(0.f));
		}
	}
}

UOrderHUDWidget::FCardWidgets UOrderHUDWidget::CreateCard(UOrderHandle* Handle)
{
	using namespace OrderHUD;

	FCardWidgets Card;
	Card.Handle = Handle;

	UWidgetTree* Tree = WidgetTree;

	// —— 卡片外框：金色圆角边 + 内层奶油底 ——
	USizeBox* Root = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), NAME_None);
	Root->SetWidthOverride(CardWidth);
	Root->SetHeightOverride(CardHeight);
	Root->SetVisibility(ESlateVisibility::HitTestInvisible);
	Card.Root = Root;

	UHorizontalBoxSlot* RowSlot = OrderRow->AddChildToHorizontalBox(Root);
	RowSlot->SetPadding(FMargin(0.f, 0.f, CardGap, 0.f));
	RowSlot->SetVerticalAlignment(VAlign_Top);

	UBorder* Outer = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), NAME_None);
	Outer->SetBrush(RoundedBrush(18.f, ColorGold));
	Outer->SetBrushColor(FLinearColor::White);
	Outer->SetPadding(FMargin(3.f));
	Outer->SetVisibility(ESlateVisibility::HitTestInvisible);
	Root->SetContent(Outer);

	UBorder* Inner = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), NAME_None);
	Inner->SetBrush(RoundedBrush(15.f, ColorCardInner, 1.f, ColorInnerLine));
	Inner->SetBrushColor(FLinearColor::White);
	Inner->SetPadding(FMargin(0.f));
	Inner->SetVisibility(ESlateVisibility::HitTestInvisible);
	Outer->SetContent(Inner);

	UCanvasPanel* Canvas = Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), NAME_None);
	Canvas->SetVisibility(ESlateVisibility::HitTestInvisible);
	Inner->SetContent(Canvas);

	auto PlaceOnCanvas = [Canvas](UWidget* Widget, const FVector2D& Position, const FVector2D& Size)
	{
		UCanvasPanelSlot* PanelSlot = Canvas->AddChildToCanvas(Widget);
		PanelSlot->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
		PanelSlot->SetPosition(Position);
		PanelSlot->SetSize(Size);
	};

	// —— 成品图标：浅色底槽 + 菜品图 ——
	UBorder* Well = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), NAME_None);
	Well->SetBrush(RoundedBrush(16.f, ColorWell, 1.f, ColorTileLine));
	Well->SetBrushColor(FLinearColor::White);
	Well->SetPadding(FMargin(0.f));
	Well->SetVisibility(ESlateVisibility::HitTestInvisible);
	PlaceOnCanvas(Well, FVector2D(17.f, 17.f), FVector2D(118.f, 118.f));

	Card.DishIcon = Tree->ConstructWidget<UImage>(UImage::StaticClass(), NAME_None);
	Card.DishIcon->SetVisibility(ESlateVisibility::HitTestInvisible);
	PlaceOnCanvas(Card.DishIcon, FVector2D(23.f, 23.f), FVector2D(DishSize, DishSize));

	// —— 名称 ——
	Card.NameText = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), NAME_None);
	Card.NameText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 21));
	Card.NameText->SetColorAndOpacity(FSlateColor(ColorText));
	Card.NameText->SetJustification(ETextJustify::Left);
	Card.NameText->SetAutoWrapText(false);
	Card.NameText->SetVisibility(ESlateVisibility::HitTestInvisible);
	PlaceOnCanvas(Card.NameText, FVector2D(145.f, 24.f), FVector2D(132.f, 30.f));

	// —— 金币 + 价格 ——
	UImage* Coin = Tree->ConstructWidget<UImage>(UImage::StaticClass(), NAME_None);
	Coin->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (CoinTexture)
	{
		Coin->SetBrushFromTexture(CoinTexture, false);
	}
	PlaceOnCanvas(Coin, FVector2D(281.f, 22.f), FVector2D(24.f, 24.f));

	Card.PriceText = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), NAME_None);
	Card.PriceText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 23));
	Card.PriceText->SetColorAndOpacity(FSlateColor(ColorText));
	Card.PriceText->SetJustification(ETextJustify::Right);
	Card.PriceText->SetAutoWrapText(false);
	Card.PriceText->SetVisibility(ESlateVisibility::HitTestInvisible);
	PlaceOnCanvas(Card.PriceText, FVector2D(307.f, 18.f), FVector2D(40.f, 34.f));

	// —— 食材格 ×4（第 4 格在超过 4 种食材时显示 +N）——
	for (int32 Index = 0; Index < 4; ++Index)
	{
		UBorder* Tile = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), NAME_None);
		Tile->SetBrush(RoundedBrush(9.f, ColorTileFill, 1.f, ColorTileLine));
		Tile->SetBrushColor(FLinearColor::White);
		Tile->SetPadding(FMargin(5.f));
		Tile->SetVisibility(ESlateVisibility::HitTestInvisible);

		UOverlay* Overlay = Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), NAME_None);
		Overlay->SetVisibility(ESlateVisibility::HitTestInvisible);
		Tile->SetContent(Overlay);

		UImage* Ingredient = Tree->ConstructWidget<UImage>(UImage::StaticClass(), NAME_None);
		Ingredient->SetVisibility(ESlateVisibility::HitTestInvisible);
		UOverlaySlot* ImageSlot = Overlay->AddChildToOverlay(Ingredient);
		ImageSlot->SetHorizontalAlignment(HAlign_Fill);
		ImageSlot->SetVerticalAlignment(VAlign_Fill);
		Card.IngredientIcons.Add(Ingredient);
		Card.IngredientTiles.Add(Tile);

		if (Index == 3)
		{
			Card.MoreText = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), NAME_None);
			Card.MoreText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 17));
			Card.MoreText->SetColorAndOpacity(FSlateColor(ColorText));
			Card.MoreText->SetJustification(ETextJustify::Center);
			Card.MoreText->SetVisibility(ESlateVisibility::Collapsed);
			UOverlaySlot* TextSlot = Overlay->AddChildToOverlay(Card.MoreText);
			TextSlot->SetHorizontalAlignment(HAlign_Center);
			TextSlot->SetVerticalAlignment(VAlign_Center);
		}

		const FVector2D Position(145.f + Index * (TileSize + TileGap), 62.f);
		PlaceOnCanvas(Tile, Position, FVector2D(TileSize, TileSize));
	}

	// —— 时钟 + 等待条 ——
	Card.ClockIcon = Tree->ConstructWidget<UImage>(UImage::StaticClass(), NAME_None);
	Card.ClockIcon->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (ClockTexture)
	{
		Card.ClockIcon->SetBrushFromTexture(ClockTexture, false);
	}
	PlaceOnCanvas(Card.ClockIcon, FVector2D(147.f, 114.f), FVector2D(20.f, 20.f));

	Card.WaitBar = Tree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), NAME_None);
	Card.WaitBar->SetVisibility(ESlateVisibility::HitTestInvisible);
	Card.WaitBar->SetPercent(1.f);
	Card.WaitBar->SetFillColorAndOpacity(ColorBarFill);
	{
		FProgressBarStyle Style;
		Style.BackgroundImage = RoundedBrush(BarHeight * 0.5f, ColorBarTrack, 1.f, ColorBarTrackLine);
		Style.FillImage = RoundedBrush(BarHeight * 0.5f, FLinearColor::White);
		Card.WaitBar->SetWidgetStyle(Style);
	}
	PlaceOnCanvas(Card.WaitBar, FVector2D(BarX, BarY), FVector2D(BarWidth, BarHeight));

	FillCard(Card);
	return Card;
}

void UOrderHUDWidget::FillCard(FCardWidgets& Card)
{
	using namespace OrderHUD;

	if (!Card.Handle)
	{
		return;
	}

	// 成品图标
	if (UTexture2D* Dish = ResolveDishIcon(Card.Handle))
	{
		Card.DishIcon->SetBrushFromTexture(Dish, false);
		Card.DishIcon->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	else
	{
		Card.DishIcon->SetVisibility(ESlateVisibility::Collapsed);
	}

	// 菜名（多菜的整单名会带 "+"，字号自动收一档）
	const FString DisplayName = Card.Handle->GetOrderDisplayName();
	const FString SafeName = DisplayName.IsEmpty() ? TEXT("订单") : DisplayName;
	const bool bLongName = SafeName.Len() > 5;
	Card.NameText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), bLongName ? 17 : 21));
	Card.NameText->SetText(FText::FromString(SafeName));

	// 价格
	const int32 Price = Card.Handle->GetOrderPrice();
	Card.PriceText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), Price >= 100 ? 20 : 23));
	Card.PriceText->SetText(FText::FromString(FString::FromInt(Price)));

	// 食材图标：最多 3 个图标 + 第 4 格 +N
	const TArray<UTexture2D*> FoodIcons = Card.Handle->GetOrderFoodIcons();
	const int32 Total = FoodIcons.Num();
	const bool bOverflow = Total > 4;
	const int32 IconCount = bOverflow ? 3 : FMath::Min(Total, 4);

	for (int32 Index = 0; Index < Card.IngredientTiles.Num(); ++Index)
	{
		const bool bShowTile = Index < IconCount || (bOverflow && Index == 3);
		Card.IngredientTiles[Index]->SetVisibility(bShowTile ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);

		if (!bShowTile)
		{
			continue;
		}

		if (bOverflow && Index == 3)
		{
			Card.IngredientIcons[Index]->SetVisibility(ESlateVisibility::Collapsed);
			if (Card.MoreText)
			{
				Card.MoreText->SetText(FText::FromString(FString::Printf(TEXT("+%d"), Total - 3)));
				Card.MoreText->SetVisibility(ESlateVisibility::HitTestInvisible);
			}
			continue;
		}

		if (FoodIcons.IsValidIndex(Index) && FoodIcons[Index])
		{
			Card.IngredientIcons[Index]->SetBrushFromTexture(FoodIcons[Index], false);
			Card.IngredientIcons[Index]->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else
		{
			Card.IngredientIcons[Index]->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	// 没有食材图标时整排收掉，避免留空槽
	if (Total == 0)
	{
		for (UBorder* Tile : Card.IngredientTiles)
		{
			if (Tile)
			{
				Tile->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
	}
}

UTexture2D* UOrderHUDWidget::ResolveDishIcon(UOrderHandle* Handle) const
{
	if (!Handle)
	{
		return nullptr;
	}

	const FOrderData Data = Handle->GetOrderData();
	if (Data.FoodIDs.Num() > 0)
	{
		if (const UWorld* World = GetWorld())
		{
			if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(World->GetGameInstance()))
			{
				if (GI->foodSystem)
				{
					if (UTexture2D* Icon = GI->foodSystem->GetFoodIcon(Data.FoodIDs[0]))
					{
						return Icon;
					}
				}
			}
		}
	}

	// 兜底：用第一个食材图标顶替成品图
	const TArray<UTexture2D*> Icons = Handle->GetOrderFoodIcons();
	return Icons.Num() > 0 ? Icons[0] : nullptr;
}
