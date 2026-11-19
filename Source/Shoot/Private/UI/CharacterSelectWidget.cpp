// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/CharacterSelectWidget.h"

#include "Game/BaseGameInstance.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "CommonInputSubsystem.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Engine/LocalPlayer.h"
#include "Kismet/GameplayStatics.h"
#include "Styling/CoreStyle.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	FLinearColor Color(const TCHAR* Hex)
	{
		return FLinearColor::FromSRGBColor(FColor::FromHex(Hex));
	}

	struct FCardPalette
	{
		FLinearColor Fill;
		FLinearColor Rim;
		FLinearColor Ink;
	};

	FCardPalette GetPalette(int32 Index)
	{
		static const FCardPalette Palettes[] = {
			{Color(TEXT("FFE8A3")), Color(TEXT("E5B947")), Color(TEXT("A76513"))},
			{Color(TEXT("CDE8FA")), Color(TEXT("87BDD9")), Color(TEXT("28657F"))},
			{Color(TEXT("DFD2F3")), Color(TEXT("AF96D1")), Color(TEXT("64508E"))},
			{Color(TEXT("FAD5DB")), Color(TEXT("E8A0AD")), Color(TEXT("A54351"))}
		};
		return Palettes[Index % UE_ARRAY_COUNT(Palettes)];
	}

	FText GetRoleDisplayName(const FRoleInfo& Info)
	{
		if (!Info.DisplayName.IsEmpty())
		{
			return Info.DisplayName;
		}
		return FText::FromString(Info.Title.IsEmpty() ? TEXT("未命名角色") : Info.Title);
	}

	FText JoinLikes(const TArray<FText>& Likes)
	{
		TArray<FString> Values;
		for (const FText& Like : Likes)
		{
			Values.Add(Like.ToString());
		}
		return FText::FromString(Values.IsEmpty() ? TEXT("—") : FString::Join(Values, TEXT("、")));
	}

	void Place(UCanvasPanel* Canvas, UWidget* Widget, FVector2D Position, FVector2D Size)
	{
		UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(Widget);
		Slot->SetPosition(Position);
		Slot->SetSize(Size);
	}

	UBorder* Panel(UWidgetTree* Tree, FLinearColor Fill, float Radius,
		FLinearColor Outline = FLinearColor::Transparent, float OutlineWidth = 0.f)
	{
		UBorder* Border = Tree->ConstructWidget<UBorder>();
		Border->SetBrush(FSlateRoundedBoxBrush(Fill, Radius, Outline, OutlineWidth));
		Border->SetPadding(FMargin(0.f));
		Border->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		return Border;
	}

	UTextBlock* Text(UWidgetTree* Tree, FName Name, FText Value, int32 FontSize,
		FLinearColor Ink, int32 OutlineSize = 0)
	{
		UTextBlock* Block = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Bold", FontSize);
		Font.OutlineSettings.OutlineSize = OutlineSize;
		Font.OutlineSettings.OutlineColor = Color(TEXT("FFF9E9"));
		Block->SetFont(Font);
		Block->SetText(Value);
		Block->SetColorAndOpacity(FSlateColor(Ink));
		Block->SetVisibility(ESlateVisibility::HitTestInvisible);
		return Block;
	}

	FButtonStyle ButtonStyle(FLinearColor Fill, FLinearColor Outline, float Radius)
	{
		FButtonStyle Style;
		Style.SetNormal(FSlateRoundedBoxBrush(Fill, Radius, Outline, 3.f));
		Style.SetHovered(FSlateRoundedBoxBrush(FMath::Lerp(Fill, FLinearColor::White, 0.25f), Radius, Color(TEXT("FFF8DC")), 4.f));
		Style.SetPressed(FSlateRoundedBoxBrush(Fill * 0.85f, Radius, Outline, 3.f));
		Style.SetNormalPadding(FMargin(8.f));
		Style.SetPressedPadding(FMargin(8.f, 10.f, 8.f, 6.f));
		return Style;
	}

	void Leaf(UWidgetTree* Tree, UCanvasPanel* Canvas, FVector2D Position, float Angle, FLinearColor Fill)
	{
		UImage* Image = Tree->ConstructWidget<UImage>();
		Image->SetBrush(FSlateRoundedBoxBrush(Fill, FVector4(2.f, 18.f, 2.f, 18.f)));
		Image->SetVisibility(ESlateVisibility::HitTestInvisible);
		Image->SetRenderTransformAngle(Angle);
		Place(Canvas, Image, Position, FVector2D(19.f, 34.f));
	}
}

URoleSelectCardButton::URoleSelectCardButton()
{
	InitIsFocusable(true);
}

void URoleSelectCardButton::InitCard(int32 InRoleID, UCharacterSelectWidget* InOwner)
{
	RoleID = InRoleID;
	OwnerWidget = InOwner;
	OnPressed.AddDynamic(this, &URoleSelectCardButton::HandlePressed);
	OnClicked.AddDynamic(this, &URoleSelectCardButton::HandleClicked);
	OnReceivedFocus.BindUObject(InOwner, &UCharacterSelectWidget::SelectRole, InRoleID);
}

void URoleSelectCardButton::HandlePressed()
{
	const UCharacterSelectWidget* Owner = OwnerWidget.Get();
	const UCommonInputSubsystem* Input = Owner ? UCommonInputSubsystem::Get(Owner->GetOwningLocalPlayer()) : nullptr;
	// CommonUI marks simulated clicks only while routing pointer-down events.
	bGamepadSimulatedClick = Input && Input->GetIsGamepadSimulatedClick();
}

void URoleSelectCardButton::HandleClicked()
{
	if (UCharacterSelectWidget* Owner = OwnerWidget.Get())
	{
		Owner->OnRoleClicked(RoleID, bGamepadSimulatedClick);
	}
}

UCharacterSelectWidget::UCharacterSelectWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(true);
	static ConstructorHelpers::FObjectFinder<UTexture2D> Garden(TEXT("/Game/_Demo/Textures/Role/T_CharacterSelectGarden"));
	static ConstructorHelpers::FObjectFinder<UTexture2D> Panda(TEXT("/Game/_Demo/Textures/Role/T_SelectPortrait_Panda"));
	static ConstructorHelpers::FObjectFinder<UTexture2D> Fox(TEXT("/Game/_Demo/Textures/Role/T_SelectPortrait_Fox"));
	static ConstructorHelpers::FObjectFinder<UTexture2D> Raccoon(TEXT("/Game/_Demo/Textures/Role/T_SelectPortrait_Raccoon"));
	static ConstructorHelpers::FObjectFinder<UTexture2D> Rabbit(TEXT("/Game/_Demo/Textures/Role/T_SelectPortrait_Rabbit"));
	BackgroundTexture = Garden.Object;
	RolePortraits.Add(1, Panda.Object);
	RolePortraits.Add(2, Fox.Object);
	RolePortraits.Add(3, Raccoon.Object);
	RolePortraits.Add(4, Rabbit.Object);
}

void UCharacterSelectWidget::SetInitialRole(int32 RoleID)
{
	InitialRoleID = RoleID;
}

TSharedRef<SWidget> UCharacterSelectWidget::RebuildWidget()
{
	GameInstance = Cast<UBaseGameInstance>(GetOwningPlayer() ? GetOwningPlayer()->GetGameInstance() : nullptr);
	CardButtons.Reset();
	CardBorders.Reset();
	CardNameTexts.Reset();
	CardSelectionBadges.Reset();

	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RoleSelectRoot"));
	WidgetTree->RootWidget = Root;
	UImage* Backdrop = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("GardenBackdrop"));
	Backdrop->SetBrushFromTexture(BackgroundTexture);
	Backdrop->SetVisibility(ESlateVisibility::HitTestInvisible);
	UCanvasPanelSlot* BackdropSlot = Root->AddChildToCanvas(Backdrop);
	BackdropSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
	BackdropSlot->SetOffsets(FMargin(0.f));

	// 固定设计尺寸整体等比缩放，宽屏和小视口都不会挤压或裁掉卡片。
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

	UTextBlock* Title = Text(WidgetTree, TEXT("Title"), FText::FromString(TEXT("选择你的角色")), 68, Color(TEXT("B87712")), 7);
	Title->SetJustification(ETextJustify::Center);
	Title->SetShadowOffset(FVector2D(0.f, 5.f));
	Title->SetShadowColorAndOpacity(Color(TEXT("BE985C")));
	Place(Design, Title, FVector2D(435.f, 32.f), FVector2D(730.f, 100.f));
	Leaf(WidgetTree, Design, FVector2D(507.f, 51.f), -32.f, Color(TEXT("98B53B")));
	Leaf(WidgetTree, Design, FVector2D(493.f, 78.f), -65.f, Color(TEXT("AFCA56")));
	Leaf(WidgetTree, Design, FVector2D(1080.f, 75.f), 36.f, Color(TEXT("98B53B")));
	Leaf(WidgetTree, Design, FVector2D(1098.f, 90.f), 63.f, Color(TEXT("AFCA56")));

	UBorder* SubtitlePanel = Panel(WidgetTree, Color(TEXT("FFF0CE")), 19.f, Color(TEXT("EBC995")), 2.f);
	SubtitlePanel->SetHorizontalAlignment(HAlign_Center);
	SubtitlePanel->SetVerticalAlignment(VAlign_Center);
	SubtitlePanel->SetContent(Text(WidgetTree, TEXT("Subtitle"), FText::FromString(TEXT("选一位喜欢的伙伴，开启你的田园经营之旅吧！")), 20, Color(TEXT("75512C"))));
	Place(Design, SubtitlePanel, FVector2D(505.f, 143.f), FVector2D(590.f, 42.f));

	HelpButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("HelpButton"));
	HelpButton->SetStyle(ButtonStyle(Color(TEXT("FFF7DF")), Color(TEXT("EDBD76")), 28.f));
	UTextBlock* Question = Text(WidgetTree, NAME_None, FText::FromString(TEXT("?")), 32, Color(TEXT("D49745")));
	Question->SetJustification(ETextJustify::Center);
	HelpButton->SetContent(Question);
	HelpButton->OnClicked.AddDynamic(this, &UCharacterSelectWidget::OnHelpClicked);
	Place(Design, HelpButton, FVector2D(46.f, 44.f), FVector2D(58.f, 58.f));
	UTextBlock* HelpLabel = Text(WidgetTree, NAME_None, FText::FromString(TEXT("帮助")), 17, Color(TEXT("FFF7DF")), 1);
	HelpLabel->SetJustification(ETextJustify::Center);
	HelpLabel->SetShadowOffset(FVector2D(0.f, 2.f));
	HelpLabel->SetShadowColorAndOpacity(Color(TEXT("9B6529")));
	Place(Design, HelpLabel, FVector2D(44.f, 107.f), FVector2D(62.f, 28.f));

	CardRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("CardRow"));
	UScaleBox* CardFit = WidgetTree->ConstructWidget<UScaleBox>();
	CardFit->SetStretch(EStretch::ScaleToFit);
	CardFit->SetStretchDirection(EStretchDirection::DownOnly);
	CardFit->SetContent(CardRow);
	Place(Design, CardFit, FVector2D(170.f, 225.f), FVector2D(1260.f, 414.f));

	UBorder* InfoShadow = Panel(WidgetTree, Color(TEXT("A6814F")).CopyWithNewOpacity(0.35f), 29.f);
	Place(Design, InfoShadow, FVector2D(290.f, 671.f), FVector2D(920.f, 192.f));
	UBorder* InfoFrame = Panel(WidgetTree, Color(TEXT("FFF9E7")), 29.f, Color(TEXT("E8C79C")), 3.f);
	InfoFrame->SetPadding(FMargin(9.f));
	Place(Design, InfoFrame, FVector2D(290.f, 663.f), FVector2D(920.f, 192.f));
	UBorder* InfoPanel = Panel(WidgetTree, Color(TEXT("FFF3DC")), 22.f, Color(TEXT("F1DDBA")), 2.f);
	InfoPanel->SetPadding(FMargin(32.f, 10.f));
	InfoFrame->SetContent(InfoPanel);
	UVerticalBox* InfoColumn = WidgetTree->ConstructWidget<UVerticalBox>();
	InfoPanel->SetContent(InfoColumn);
	NameText = Text(WidgetTree, TEXT("RoleName"), FText(), 32, Color(TEXT("B87712")), 1);
	InfoColumn->AddChildToVerticalBox(NameText);
	PersonalityText = Text(WidgetTree, TEXT("Personality"), FText(), 20, Color(TEXT("51412D")));
	InfoColumn->AddChildToVerticalBox(PersonalityText)->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));
	LikesText = Text(WidgetTree, TEXT("Likes"), FText(), 20, Color(TEXT("51412D")));
	InfoColumn->AddChildToVerticalBox(LikesText)->SetPadding(FMargin(0.f, 2.f, 0.f, 0.f));
	DescriptionText = Text(WidgetTree, TEXT("Description"), FText(), 17, Color(TEXT("645039")));
	DescriptionText->SetAutoWrapText(true);
	InfoColumn->AddChildToVerticalBox(DescriptionText)->SetPadding(FMargin(0.f, 9.f, 0.f, 0.f));
	Leaf(WidgetTree, Design, FVector2D(430.f, 685.f), 30.f, Color(TEXT("A4BD49")));
	Leaf(WidgetTree, Design, FVector2D(446.f, 702.f), 62.f, Color(TEXT("A4BD49")));
	const FLinearColor PawColor = Color(TEXT("EEC998")).CopyWithNewOpacity(0.45f);
	UBorder* Paw = Panel(WidgetTree, PawColor, 25.f);
	Paw->SetRenderTransformAngle(-25.f);
	Place(Design, Paw, FVector2D(1115.f, 730.f), FVector2D(50.f, 44.f));
	for (int32 Index = 0; Index < 3; ++Index)
	{
		UBorder* Toe = Panel(WidgetTree, PawColor, 12.f);
		Place(Design, Toe, FVector2D(1105.f + Index * 25.f, 704.f + FMath::Abs(Index - 1) * 12.f), FVector2D(20.f, 23.f));
	}

	UBorder* ConfirmShadow = Panel(WidgetTree, Color(TEXT("946329")).CopyWithNewOpacity(0.4f), 28.f);
	Place(Design, ConfirmShadow, FVector2D(1285.f, 788.f), FVector2D(256.f, 85.f));
	ConfirmButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("ConfirmButton"));
	ConfirmButton->SetStyle(ButtonStyle(Color(TEXT("F6C74E")), Color(TEXT("FFF0AC")), 28.f));
	UBorder* ConfirmInset = Panel(WidgetTree, Color(TEXT("FFDD75")), 21.f, Color(TEXT("FFEAA2")), 2.f);
	ConfirmInset->SetHorizontalAlignment(HAlign_Center);
	ConfirmInset->SetVerticalAlignment(VAlign_Center);
	ConfirmInset->SetContent(Text(WidgetTree, TEXT("ConfirmText"), FText::FromString(TEXT("开始经营")), 33, Color(TEXT("8D5817"))));
	ConfirmButton->SetContent(ConfirmInset);
	ConfirmButton->OnClicked.AddDynamic(this, &UCharacterSelectWidget::OnConfirmClicked);
	Place(Design, ConfirmButton, FVector2D(1285.f, 780.f), FVector2D(256.f, 85.f));

	UTextBlock* InputHint = Text(WidgetTree, TEXT("InputHint"), FText::FromString(TEXT("手柄：方向键 / 左摇杆切换角色 · A 确认 · Y 帮助 · B 返回\n键盘：← → / A D 切换角色 · 回车 / 空格确认 · F1 帮助 · Esc / 退格返回")), 14, Color(TEXT("FFF9E7")));
	InputHint->SetJustification(ETextJustify::Center);
	InputHint->SetShadowOffset(FVector2D(0.f, 1.f));
	InputHint->SetShadowColorAndOpacity(Color(TEXT("8B6B3F")));
	Place(Design, InputHint, FVector2D(350.f, 862.f), FVector2D(900.f, 38.f));

	HelpPanel = Panel(WidgetTree, Color(TEXT("FFF4DE")), 20.f, Color(TEXT("E8BF7C")), 2.f);
	HelpPanel->SetPadding(FMargin(20.f, 14.f));
	UTextBlock* HelpText = Text(WidgetTree, TEXT("HelpText"), FText::FromString(TEXT("点击卡片选择伙伴\n手柄左右 / 左摇杆、← → / A D：切换\n手柄上下：帮助 / 开始经营\nA / 回车 / 空格：确认\nY / F1：打开或关闭帮助\nB / Esc / 退格：关闭帮助并返回选角")), 18, Color(TEXT("73512D")));
	HelpPanel->SetContent(HelpText);
	HelpPanel->SetVisibility(ESlateVisibility::Collapsed);
	Place(Design, HelpPanel, FVector2D(44.f, 145.f), FVector2D(390.f, 190.f));

	RoleIDs = GameInstance ? GameInstance->GetSelectableRoleIDs() : TArray<int32>();
	for (int32 Index = 0; Index < RoleIDs.Num(); ++Index)
	{
		BuildCard(Index, RoleIDs[Index]);
	}
	CurrentRoleID = RoleIDs.Contains(InitialRoleID) ? InitialRoleID : (RoleIDs.IsEmpty() ? 0 : RoleIDs[0]);
	ConfirmButton->SetIsEnabled(CurrentRoleID != 0);
	RefreshUI();
	return Root->TakeWidget();
}

void UCharacterSelectWidget::BuildCard(int32 CardIndex, int32 RoleID)
{
	if (!CardRow || !GameInstance)
	{
		return;
	}
	FRoleInfo Info;
	GameInstance->FindRoleInfo(RoleID, Info);
	const FCardPalette Palette = GetPalette(CardIndex);
	USizeBox* CardSize = WidgetTree->ConstructWidget<USizeBox>();
	CardSize->SetWidthOverride(264.f);
	CardSize->SetHeightOverride(398.f);
	UOverlay* Layers = WidgetTree->ConstructWidget<UOverlay>();
	CardSize->SetContent(Layers);
	UBorder* Shadow = Panel(WidgetTree, Palette.Ink.CopyWithNewOpacity(0.3f), 30.f);
	Layers->AddChildToOverlay(Shadow)->SetPadding(FMargin(0.f, 9.f, 0.f, 0.f));

	URoleSelectCardButton* Card = WidgetTree->ConstructWidget<URoleSelectCardButton>();
	Card->InitCard(RoleID, this);
	Card->SetStyle(ButtonStyle(Palette.Rim, Color(TEXT("FFF3CE")), 30.f));
	Layers->AddChildToOverlay(Card)->SetPadding(FMargin(0.f, 0.f, 0.f, 9.f));
	UBorder* CardBg = Panel(WidgetTree, Palette.Fill, 23.f, Color(TEXT("FFF9E7")), 2.f);
	CardBg->SetPadding(FMargin(8.f, 9.f, 8.f, 8.f));
	Card->SetContent(CardBg);
	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>();
	CardBg->SetContent(Column);
	USizeBox* IconBox = WidgetTree->ConstructWidget<USizeBox>();
	IconBox->SetHeightOverride(262.f);
	UTexture2D* Portrait = Info.UIIcon;
	if (const TObjectPtr<UTexture2D>* SelectionPortrait = RolePortraits.Find(RoleID))
	{
		Portrait = SelectionPortrait->Get();
	}
	if (Portrait)
	{
		UImage* Icon = WidgetTree->ConstructWidget<UImage>();
		FSlateRoundedBoxBrush Brush(FLinearColor::White, 16.f);
		Brush.SetResourceObject(Portrait);
		Brush.ImageType = ESlateBrushImageType::FullColor;
		Brush.ImageSize = FVector2D(Portrait->GetSizeX(), Portrait->GetSizeY());
		Icon->SetBrush(Brush);
		Icon->SetVisibility(ESlateVisibility::HitTestInvisible);
		UScaleBox* PortraitFit = WidgetTree->ConstructWidget<UScaleBox>();
		PortraitFit->SetStretch(EStretch::ScaleToFit);
		PortraitFit->SetContent(Icon);
		IconBox->SetContent(PortraitFit);
	}
	else
	{
		UTextBlock* Placeholder = Text(WidgetTree, NAME_None, FText::FromString(GetRoleDisplayName(Info).ToString().Left(1)), 64, Palette.Ink);
		Placeholder->SetJustification(ETextJustify::Center);
		IconBox->SetContent(Placeholder);
	}
	Column->AddChildToVerticalBox(IconBox);
	UTextBlock* CardName = Text(WidgetTree, NAME_None, GetRoleDisplayName(Info), 31, Palette.Ink, 2);
	CardName->SetJustification(ETextJustify::Center);
	Column->AddChildToVerticalBox(CardName)->SetPadding(FMargin(0.f, 4.f, 0.f, 5.f));
	UBorder* PersonalityPill = Panel(WidgetTree, FMath::Lerp(Palette.Fill, FLinearColor::White, 0.4f), 17.f, Palette.Rim, 1.5f);
	PersonalityPill->SetPadding(FMargin(8.f, 5.f));
	UTextBlock* Personality = Text(WidgetTree, NAME_None, Info.Personality, 14, Palette.Ink);
	Personality->SetJustification(ETextJustify::Center);
	PersonalityPill->SetContent(Personality);
	Column->AddChildToVerticalBox(PersonalityPill);

	UBorder* Badge = Panel(WidgetTree, Color(TEXT("FFDA65")), 12.f, Color(TEXT("FFF8DC")), 2.f);
	Badge->SetPadding(FMargin(9.f, 2.f));
	Badge->SetContent(Text(WidgetTree, NAME_None, FText::FromString(TEXT("已选择")), 13, Color(TEXT("966116"))));
	UOverlaySlot* BadgeSlot = Layers->AddChildToOverlay(Badge);
	BadgeSlot->SetHorizontalAlignment(HAlign_Right);
	BadgeSlot->SetVerticalAlignment(VAlign_Top);
	BadgeSlot->SetPadding(FMargin(0.f, 13.f, 15.f, 0.f));
	Badge->SetVisibility(ESlateVisibility::HitTestInvisible);
	UHorizontalBoxSlot* RowSlot = CardRow->AddChildToHorizontalBox(CardSize);
	RowSlot->SetPadding(FMargin(14.f, 9.f));
	RowSlot->SetVerticalAlignment(VAlign_Center);
	CardButtons.Add(Card);
	CardBorders.Add(CardBg);
	CardNameTexts.Add(CardName);
	CardSelectionBadges.Add(Badge);
}

void UCharacterSelectWidget::SelectRole(int32 RoleID)
{
	if (!RoleIDs.Contains(RoleID))
	{
		return;
	}
	if (CurrentRoleID != RoleID)
	{
		CurrentRoleID = RoleID;
		RefreshUI();
	}
	GetDesiredFocusTarget()->SetUserFocus(GetOwningPlayer());
}

int32 UCharacterSelectWidget::GetCurrentRoleID() const
{
	return CurrentRoleID;
}

UWidget* UCharacterSelectWidget::GetDesiredFocusTarget()
{
	const int32 Index = RoleIDs.IndexOfByKey(CurrentRoleID);
	if (CardButtons.IsValidIndex(Index))
	{
		return CardButtons[Index].Get();
	}
	return this;
}

void UCharacterSelectWidget::OnRoleClicked(int32 RoleID, bool bGamepadSimulatedClick)
{
	SelectRole(RoleID);
	if (bGamepadSimulatedClick)
	{
		OnConfirmClicked();
	}
}

void UCharacterSelectWidget::RefreshUI()
{
	for (int32 Index = 0; Index < CardButtons.Num(); ++Index)
	{
		const bool bSelected = RoleIDs[Index] == CurrentRoleID;
		const FCardPalette Palette = GetPalette(Index);
		URoleSelectCardButton* Card = CardButtons[Index];
		Card->SetStyle(ButtonStyle(bSelected ? Color(TEXT("F5C749")) : Palette.Rim,
			bSelected ? Color(TEXT("FFF3AD")) : Color(TEXT("FFF3CE")), 30.f));
		UWidget* CardContainer = Card->GetParent()->GetParent();
		CardContainer->SetRenderScale(bSelected ? FVector2D(1.045f) : FVector2D(1.f));
		CardContainer->SetRenderTranslation(FVector2D(0.f, bSelected ? -9.f : 0.f));
		CardBorders[Index]->SetBrush(FSlateRoundedBoxBrush(Palette.Fill, 23.f,
			bSelected ? Color(TEXT("FFF6CA")) : Color(TEXT("FFF9E7")), bSelected ? 3.f : 2.f));
		CardNameTexts[Index]->SetColorAndOpacity(FSlateColor(bSelected ? Color(TEXT("AC6A13")) : Palette.Ink));
		CardSelectionBadges[Index]->SetVisibility(bSelected ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	FRoleInfo Info;
	if (GameInstance && CurrentRoleID != 0)
	{
		GameInstance->FindRoleInfo(CurrentRoleID, Info);
	}
	NameText->SetText(GetRoleDisplayName(Info));
	PersonalityText->SetText(FText::FromString(FString::Printf(TEXT("性格：%s"),
		Info.Personality.IsEmpty() ? TEXT("—") : *Info.Personality.ToString())));
	LikesText->SetText(FText::FromString(FString::Printf(TEXT("喜好：%s"), *JoinLikes(Info.Likes).ToString())));
	DescriptionText->SetText(Info.Description);
}

void UCharacterSelectWidget::OnConfirmClicked()
{
	if (GameInstance && CurrentRoleID != 0)
	{
		GameInstance->ConfirmRoleSelection(CurrentRoleID);
	}
}

void UCharacterSelectWidget::OnHelpClicked()
{
	const bool bShowHelp = HelpPanel->GetVisibility() == ESlateVisibility::Collapsed;
	HelpPanel->SetVisibility(bShowHelp ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	UWidget* FocusTarget = bShowHelp ? HelpButton.Get() : GetDesiredFocusTarget();
	FocusTarget->SetUserFocus(GetOwningPlayer());
}

void UCharacterSelectWidget::NativeConstruct()
{
	Super::NativeConstruct();
	bGameWasPaused = UGameplayStatics::IsGamePaused(this);
	UGameplayStatics::SetGamePaused(this, true);
	GetDesiredFocusTarget()->SetUserFocus(GetOwningPlayer());
}

void UCharacterSelectWidget::NativeDestruct()
{
	if (GetWorld())
	{
		UGameplayStatics::SetGamePaused(this, bGameWasPaused);
	}
	if (FSlateApplication::IsInitialized() && GetOwningLocalPlayer())
	{
		FSlateApplication::Get().SetUserFocusToGameViewport(GetOwningLocalPlayer()->GetControllerId());
	}
	Super::NativeDestruct();
}

void UCharacterSelectWidget::Navigate(EUINavigation Direction)
{
	if (HelpPanel->GetVisibility() != ESlateVisibility::Collapsed)
	{
		return;
	}
	APlayerController* PC = GetOwningPlayer();
	if (Direction == EUINavigation::Left || Direction == EUINavigation::Right)
	{
		if (!RoleIDs.IsEmpty())
		{
			const int32 Index = FMath::Max(0, RoleIDs.IndexOfByKey(CurrentRoleID));
			const int32 Step = Direction == EUINavigation::Left ? -1 : 1;
			SelectRole(RoleIDs[(Index + Step + RoleIDs.Num()) % RoleIDs.Num()]);
		}
	}
	else if (Direction == EUINavigation::Up || Direction == EUINavigation::Previous)
	{
		UWidget* FocusTarget = ConfirmButton->HasUserFocus(PC) ? GetDesiredFocusTarget() : HelpButton.Get();
		FocusTarget->SetUserFocus(PC);
	}
	else if (Direction == EUINavigation::Down || Direction == EUINavigation::Next)
	{
		UWidget* FocusTarget = HelpButton->HasUserFocus(PC) ? GetDesiredFocusTarget() : ConfirmButton.Get();
		FocusTarget->SetUserFocus(PC);
	}
}

FReply UCharacterSelectWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	FSlateApplication& Slate = FSlateApplication::Get();
	EUINavigation Direction = Slate.GetNavigationDirectionFromKey(InKeyEvent);
	if (Key == EKeys::A || Key == EKeys::D)
	{
		Direction = Key == EKeys::A ? EUINavigation::Left : EUINavigation::Right;
	}
	if (Direction != EUINavigation::Invalid)
	{
		Navigate(Direction);
		return FReply::Handled();
	}
	const EUINavigationAction Action = Slate.GetNavigationActionFromKey(InKeyEvent);
	if (Action == EUINavigationAction::Back || Key == EKeys::BackSpace)
	{
		if (!InKeyEvent.IsRepeat())
		{
			HelpPanel->SetVisibility(ESlateVisibility::Collapsed);
			GetDesiredFocusTarget()->SetUserFocus(GetOwningPlayer());
		}
		return FReply::Handled();
	}
	if (Key == EKeys::Gamepad_FaceButton_Top || Key == EKeys::F1)
	{
		if (!InKeyEvent.IsRepeat())
		{
			OnHelpClicked();
		}
		return FReply::Handled();
	}
	if (Action == EUINavigationAction::Accept)
	{
		if (!InKeyEvent.IsRepeat())
		{
			if (HelpPanel->GetVisibility() != ESlateVisibility::Collapsed || HelpButton->HasUserFocus(GetOwningPlayer()))
			{
				OnHelpClicked();
			}
			else
			{
				OnConfirmClicked();
			}
		}
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

FReply UCharacterSelectWidget::NativeOnAnalogValueChanged(const FGeometry& InGeometry, const FAnalogInputEvent& InAnalogEvent)
{
	if (InAnalogEvent.GetKey() == EKeys::Gamepad_LeftX || InAnalogEvent.GetKey() == EKeys::Gamepad_LeftY)
	{
		Navigate(FSlateApplication::Get().GetNavigationDirectionFromAnalog(InAnalogEvent));
		return FReply::Handled();
	}
	return Super::NativeOnAnalogValueChanged(InGeometry, InAnalogEvent);
}

FReply UCharacterSelectWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	return FReply::Handled().SetUserFocus(GetDesiredFocusTarget()->TakeWidget(), EFocusCause::Mouse);
}
