// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/RecipeBookWidget.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Data/MenuData.h"
#include "Game/BaseGameInstance.h"
#include "Game/FoodSystem.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Styling/CoreStyle.h"

namespace
{
	constexpr int32 RecipeBookZOrder = 80;
	constexpr float CardWidth = 430.f;
	constexpr float CardHeight = 178.f;
	constexpr float CardGap = 12.f;
	constexpr int32 MaxCardsPerRow = 3;
	constexpr float PanelWidth = 1380.f;
	constexpr float PanelHeight = 900.f;
	constexpr float PanelPaddingX = 52.f;

	const FLinearColor AccentGold(0.95f, 0.78f, 0.35f, 1.f);
	const FLinearColor TextPrimary(0.94f, 0.94f, 0.94f, 1.f);
	const FLinearColor TextMuted(0.66f, 0.69f, 0.74f, 1.f);

	/** 每个本地玩家一份，避免多人同屏共享同一个图鉴 */
	TMap<TWeakObjectPtr<APlayerController>, TWeakObjectPtr<URecipeBookWidget>> ActiveBooks;

	UTextBlock* MakeText(UWidgetTree* Tree, const FString& Text, int32 Size, const FLinearColor& Color, bool bBold = false)
	{
		UTextBlock* Label = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Label->SetFont(FCoreStyle::GetDefaultFontStyle(bBold ? TEXT("Bold") : TEXT("Regular"), Size));
		Label->SetColorAndOpacity(FSlateColor(Color));
		Label->SetText(FText::FromString(Text));
		return Label;
	}
}

void URecipeBookWidget::ToggleForPlayer(APlayerController* PC)
{
	if (!PC)
	{
		return;
	}

	URecipeBookWidget* Book = nullptr;
	if (const TWeakObjectPtr<URecipeBookWidget>* Found = ActiveBooks.Find(TWeakObjectPtr<APlayerController>(PC)))
	{
		Book = Found->Get();
	}

	// 已经打开：再按一次关闭
	if (IsValid(Book) && Book->IsInViewport())
	{
		Book->CloseSelf();
		return;
	}

	if (!IsValid(Book))
	{
		Book = CreateWidget<URecipeBookWidget>(PC, URecipeBookWidget::StaticClass());
		if (!Book)
		{
			return;
		}
		Book->OwnerController = PC;
		ActiveBooks.Add(TWeakObjectPtr<APlayerController>(PC), Book);
	}

	Book->AddToViewport(RecipeBookZOrder);
	Book->Refresh();
	Book->SetUIInputEnabled(true);
}

TSharedRef<SWidget> URecipeBookWidget::RebuildWidget()
{
	if (WidgetTree->RootWidget)
	{
		return Super::RebuildWidget();
	}

	SetVisibility(ESlateVisibility::Visible);

	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;

	// 半透明底遮住场景，阅读时视线集中在图鉴上
	UBorder* Backdrop = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Backdrop"));
	Backdrop->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.55f));
	if (UCanvasPanelSlot* BackdropSlot = Root->AddChildToCanvas(Backdrop))
	{
		BackdropSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		BackdropSlot->SetOffsets(FMargin(0.f));
	}

	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Panel"));
	Panel->SetPadding(FMargin(26.f, 20.f, 26.f, 18.f));
	Panel->SetBrushColor(FLinearColor(0.055f, 0.06f, 0.07f, 0.97f));

	FSlateBrush PanelBrush;
	PanelBrush.DrawAs = ESlateBrushDrawType::RoundedBox;
	PanelBrush.OutlineSettings.CornerRadii = FVector4(14.f, 14.f, 14.f, 14.f);
	PanelBrush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
	PanelBrush.OutlineSettings.Width = 1.5f;
	PanelBrush.OutlineSettings.Color = FSlateColor(FLinearColor(0.95f, 0.78f, 0.35f, 0.55f));
	Panel->SetBrush(PanelBrush);

	// 面板按视口尺寸收窄：固定宽度在较小窗口 / 分屏下会超出视口，把最右一列卡片裁掉
	const FVector2D ViewportPixels = UWidgetLayoutLibrary::GetViewportSize(this);
	const float ViewportScale = FMath::Max(UWidgetLayoutLibrary::GetViewportScale(this), 0.01f);
	const FVector2D LogicalSize = (ViewportPixels.X > 0.f && ViewportPixels.Y > 0.f)
		? ViewportPixels / ViewportScale
		: FVector2D(PanelWidth, PanelHeight);
	const float PanelW = FMath::Max(LogicalSize.X - 40.f, 360.f);
	const float PanelH = FMath::Max(LogicalSize.Y - 40.f, 320.f);
	const float FitW = FMath::Min(PanelW, PanelWidth);
	const float FitH = FMath::Min(PanelH, PanelHeight);
	Columns = FMath::Clamp(
		FMath::FloorToInt((FitW - PanelPaddingX + CardGap) / (CardWidth + CardGap)), 1, MaxCardsPerRow);

	if (UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(Panel))
	{
		PanelSlot->SetAnchors(FAnchors(0.5f, 0.5f));
		PanelSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		PanelSlot->SetSize(FVector2D(FitW, FitH));
	}

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
	Panel->SetContent(Column);

	UTextBlock* Title = MakeText(WidgetTree, TEXT("菜谱图鉴"), 32, AccentGold, true);
	if (UVerticalBoxSlot* PanelSlot = Column->AddChildToVerticalBox(Title))
	{
		PanelSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
	}

	SummaryText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SummaryText"));
	SummaryText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 14));
	SummaryText->SetColorAndOpacity(FSlateColor(TextMuted));
	if (UVerticalBoxSlot* PanelSlot = Column->AddChildToVerticalBox(SummaryText))
	{
		PanelSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 14.f));
	}

	ScrollSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ScrollSize"));
	ScrollSize->SetHeightOverride(FMath::Max(FitH - 150.f, 200.f));
	if (UVerticalBoxSlot* PanelSlot = Column->AddChildToVerticalBox(ScrollSize))
	{
		PanelSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));
	}

	ScrollBox = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("Scroll"));
	ScrollBox->SetConsumeMouseWheel(EConsumeMouseWheel::Always);
	ScrollSize->SetContent(ScrollBox);

	CardList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("CardList"));
	ScrollBox->AddChild(CardList);

	FooterText = MakeText(WidgetTree, TEXT("滚轮 / ↑↓ 翻看 · E 或 Esc 关闭"), 12, TextMuted);
	Column->AddChildToVerticalBox(FooterText);

	return Super::RebuildWidget();
}

void URecipeBookWidget::Refresh()
{
	if (!CardList || !ScrollBox)
	{
		return;
	}

	CardList->ClearChildren();
	ScrollBox->SetScrollOffset(0.f);

	UWorld* World = GetWorld();
	UBaseGameInstance* GI = World ? Cast<UBaseGameInstance>(World->GetGameInstance()) : nullptr;
	UFoodSystem* FS = GI ? GI->foodSystem.Get() : nullptr;
	if (!FS)
	{
		if (SummaryText)
		{
			SummaryText->SetText(FText::FromString(TEXT("菜品表未初始化（GameInstance->foodSystem 为空），暂时无法显示菜谱。")));
		}
		return;
	}

	// 已解锁在前、未解锁在后，同组内按 FoodID 升序（GetAllFoodIDs 已排序）
	TArray<int32> UnlockedIDs;
	TArray<int32> LockedIDs;
	for (const int32 FoodID : FS->GetAllFoodIDs())
	{
		const FMenuData Menu = FS->GetMenuData(FoodID);
		if (Menu.FoodID < 0)
		{
			continue;
		}
		if (GI->IsRecipeUnlocked(FoodID))
		{
			UnlockedIDs.Add(FoodID);
		}
		else
		{
			LockedIDs.Add(FoodID);
		}
	}

	if (SummaryText)
	{
		SummaryText->SetText(FText::FromString(FString::Printf(
			TEXT("已解锁 %d / %d · 未解锁菜谱需在研究炉投入食材研究（剪影下方是研究线索）"),
			UnlockedIDs.Num(), UnlockedIDs.Num() + LockedIDs.Num())));
	}

	TArray<int32> OrderedIDs = UnlockedIDs;
	OrderedIDs.Append(LockedIDs);

	UHorizontalBox* CurrentRow = nullptr;
	for (int32 Index = 0; Index < OrderedIDs.Num(); ++Index)
	{
		const int32 FoodID = OrderedIDs[Index];
		const bool bUnlocked = Index < UnlockedIDs.Num();

		if (Index % FMath::Max(Columns, 1) == 0)
		{
			CurrentRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
			if (UVerticalBoxSlot* RowSlot = CardList->AddChildToVerticalBox(CurrentRow))
			{
				RowSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f));
			}
		}

		if (CurrentRow)
		{
			const FMenuData Menu = FS->GetMenuData(FoodID);
			if (UHorizontalBoxSlot* CardSlot = CurrentRow->AddChildToHorizontalBox(CreateCard(Menu, FoodID, bUnlocked)))
			{
				CardSlot->SetPadding(FMargin(0.f, 0.f, 12.f, 0.f));
			}
		}
	}
}

UWidget* URecipeBookWidget::CreateCard(const FMenuData& Menu, int32 FoodID, bool bUnlocked)
{
	UWorld* World = GetWorld();
	UBaseGameInstance* GI = World ? Cast<UBaseGameInstance>(World->GetGameInstance()) : nullptr;
	UFoodSystem* FS = GI ? GI->foodSystem.Get() : nullptr;

	const FLinearColor OutlineColor = bUnlocked ? FLinearColor(0.95f, 0.78f, 0.35f, 0.85f) : FLinearColor(0.34f, 0.36f, 0.4f, 0.8f);

	USizeBox* CardSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	CardSize->SetWidthOverride(CardWidth);
	CardSize->SetHeightOverride(CardHeight);

	UBorder* Card = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Card->SetPadding(FMargin(14.f));
	Card->SetBrushColor(bUnlocked ? FLinearColor(0.095f, 0.1f, 0.115f, 0.96f) : FLinearColor(0.06f, 0.064f, 0.074f, 0.96f));

	FSlateBrush CardBrush;
	CardBrush.DrawAs = ESlateBrushDrawType::RoundedBox;
	CardBrush.OutlineSettings.CornerRadii = FVector4(10.f, 10.f, 10.f, 10.f);
	CardBrush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
	CardBrush.OutlineSettings.Width = 1.2f;
	CardBrush.OutlineSettings.Color = FSlateColor(OutlineColor);
	Card->SetBrush(CardBrush);
	CardSize->SetContent(Card);

	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	Card->SetContent(Row);

	// 成品图标：未解锁只显示暗剪影，不暴露具体菜品
	USizeBox* IconBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	IconBox->SetWidthOverride(76.f);
	IconBox->SetHeightOverride(76.f);
	UImage* Icon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
	if (bUnlocked)
	{
		if (UTexture2D* Tex = FS ? FS->GetFoodIcon(FoodID) : nullptr)
		{
			Icon->SetBrushFromTexture(Tex, true);
		}
		else
		{
			Icon->SetColorAndOpacity(FLinearColor(0.32f, 0.32f, 0.34f, 1.f));
		}
	}
	else
	{
		Icon->SetColorAndOpacity(FLinearColor(0.12f, 0.12f, 0.14f, 1.f));
	}
	IconBox->SetContent(Icon);

	if (UHorizontalBoxSlot* IconSlot = Row->AddChildToHorizontalBox(IconBox))
	{
		IconSlot->SetVerticalAlignment(VAlign_Center);
		IconSlot->SetPadding(FMargin(0.f, 0.f, 12.f, 0.f));
	}

	UVerticalBox* Info = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	if (UHorizontalBoxSlot* InfoSlot = Row->AddChildToHorizontalBox(Info))
	{
		InfoSlot->SetVerticalAlignment(VAlign_Center);
	}

	const FString DisplayName = bUnlocked && !Menu.FoodName.IsEmpty()
		? Menu.FoodName
		: (bUnlocked ? FString::Printf(TEXT("菜品 %d"), FoodID) : FString(TEXT("？？？")));
	UTextBlock* NameText = MakeText(WidgetTree, DisplayName, 20, bUnlocked ? TextPrimary : FLinearColor(0.55f, 0.57f, 0.62f, 1.f), true);
	if (UVerticalBoxSlot* PanelSlot = Info->AddChildToVerticalBox(NameText))
	{
		PanelSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
	}

	if (bUnlocked)
	{
		// 所需食材：小图标 + 数量
		UHorizontalBox* ItemRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		for (const TPair<int32, int32>& Item : Menu.MenuItems)
		{
			UTexture2D* ItemTex = FS ? FS->GetItemIcon(Item.Key) : nullptr;
			USizeBox* MiniBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			MiniBox->SetWidthOverride(26.f);
			MiniBox->SetHeightOverride(26.f);
			UImage* MiniIcon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
			if (ItemTex)
			{
				MiniIcon->SetBrushFromTexture(ItemTex, true);
			}
			else
			{
				MiniIcon->SetColorAndOpacity(FLinearColor(0.45f, 0.45f, 0.48f, 1.f));
			}
			MiniBox->SetContent(MiniIcon);

			UHorizontalBox* PairBox = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
			if (UHorizontalBoxSlot* MiniSlot = PairBox->AddChildToHorizontalBox(MiniBox))
			{
				MiniSlot->SetVerticalAlignment(VAlign_Center);
			}
			if (UHorizontalBoxSlot* CountSlot = PairBox->AddChildToHorizontalBox(
				MakeText(WidgetTree, FString::Printf(TEXT("×%d"), Item.Value), 12, TextMuted)))
			{
				CountSlot->SetVerticalAlignment(VAlign_Center);
				CountSlot->SetPadding(FMargin(2.f, 0.f, 10.f, 0.f));
			}
			ItemRow->AddChildToHorizontalBox(PairBox);
		}
		if (UVerticalBoxSlot* PanelSlot = Info->AddChildToVerticalBox(ItemRow))
		{
			PanelSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
		}

		UTextBlock* MetaText = MakeText(WidgetTree, FString::Printf(TEXT("售价 %d · 最长等待 %.0f 秒"),
			Menu.Price, Menu.MaxWaitTime), 12, TextMuted);
		Info->AddChildToVerticalBox(MetaText);
	}
	else
	{
		const FString Hint = Menu.ResearchHint.IsEmpty()
			? FString(TEXT("尚无研究记录：试着在研究炉里投入看起来能搭配的食材。"))
			: Menu.ResearchHint.ToString();
		UTextBlock* HintText = MakeText(WidgetTree, Hint, 12, FLinearColor(0.82f, 0.74f, 0.55f, 1.f));
		HintText->SetAutoWrapText(true);
		HintText->SetWrapTextAt(300.f);
		if (UVerticalBoxSlot* PanelSlot = Info->AddChildToVerticalBox(HintText))
		{
			PanelSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
		}

		Info->AddChildToVerticalBox(MakeText(WidgetTree, TEXT("未解锁 · 在研究炉投入食材研究"), 12, FLinearColor(0.55f, 0.57f, 0.62f, 1.f)));
	}

	return CardSize;
}

FReply URecipeBookWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();

	// 打开状态下 E / Esc 关闭：图鉴是模态界面，关闭由自己处理，避免 E 又被世界交互拿走
	if (Key == EKeys::E || Key == EKeys::Escape)
	{
		CloseSelf();
		return FReply::Handled();
	}

	if (ScrollBox)
	{
		float Delta = 0.f;
		if (Key == EKeys::Up) Delta = -120.f;
		else if (Key == EKeys::Down) Delta = 120.f;
		else if (Key == EKeys::PageUp) Delta = -560.f;
		else if (Key == EKeys::PageDown) Delta = 560.f;

		if (Delta != 0.f)
		{
			ScrollBox->SetScrollOffset(FMath::Max(0.f, ScrollBox->GetScrollOffset() + Delta));
			return FReply::Handled();
		}
	}

	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void URecipeBookWidget::CloseSelf()
{
	SetUIInputEnabled(false);
	RemoveFromParent();
}

void URecipeBookWidget::SetUIInputEnabled(bool bEnabled)
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

void URecipeBookWidget::NativeDestruct()
{
	if (APlayerController* PC = OwnerController.Get())
	{
		ActiveBooks.Remove(TWeakObjectPtr<APlayerController>(PC));
		PC->SetInputMode(FInputModeGameOnly());
		PC->bShowMouseCursor = false;
	}
	Super::NativeDestruct();
}
