// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/CharacterSelectWidget.h"

#include "Game/BaseGameInstance.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Styling/CoreStyle.h"

namespace
{
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
		if (Likes.Num() == 0)
		{
			return FText::FromString(TEXT("—"));
		}
		FString Joined;
		for (int32 Index = 0; Index < Likes.Num(); ++Index)
		{
			if (Index > 0)
			{
				Joined += TEXT("、");
			}
			Joined += Likes[Index].ToString();
		}
		return FText::FromString(Joined);
	}
}

// ==================== URoleSelectCardButton ====================

URoleSelectCardButton::URoleSelectCardButton()
{
	// 卡片不参与键盘焦点（键盘操作由选角界面统一处理）
}

void URoleSelectCardButton::InitCard(int32 InRoleID, UCharacterSelectWidget* InOwner)
{
	RoleID = InRoleID;
	OwnerWidget = InOwner;
	OnClicked.AddDynamic(this, &URoleSelectCardButton::HandleClicked);
}

void URoleSelectCardButton::HandleClicked()
{
	if (UCharacterSelectWidget* Owner = OwnerWidget.Get())
	{
		Owner->SelectRole(RoleID);
	}
}

// ==================== UCharacterSelectWidget ====================

UCharacterSelectWidget::UCharacterSelectWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 需要接收键盘（←→ 切换 / 回车确认）
	SetIsFocusable(true);
}

void UCharacterSelectWidget::SetInitialRole(int32 RoleID)
{
	InitialRoleID = RoleID;
}

TSharedRef<SWidget> UCharacterSelectWidget::RebuildWidget()
{
	GameInstance = Cast<UBaseGameInstance>(GetOwningPlayer() ? GetOwningPlayer()->GetGameInstance() : nullptr);

	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RoleSelectRoot"));
	WidgetTree->RootWidget = Root;

	// 全屏底色：做成独立选角界面的观感
	UBorder* Backdrop = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Backdrop"));
	Backdrop->SetBrushColor(FLinearColor(0.012f, 0.015f, 0.028f, 0.985f));
	if (UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(Backdrop))
	{
		PanelSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		PanelSlot->SetOffsets(FMargin(0.f));
	}

	UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Title"));
	Title->SetText(FText::FromString(TEXT("选择你的角色")));
	Title->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 40));
	Title->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.82f, 0.3f, 1.f)));
	Title->SetShadowOffset(FVector2D(2.f, 2.f));
	Title->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.85f));
	if (UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(Title))
	{
		PanelSlot->SetAnchors(FAnchors(0.5f, 0.f));
		PanelSlot->SetAlignment(FVector2D(0.5f, 0.f));
		PanelSlot->SetAutoSize(true);
		PanelSlot->SetPosition(FVector2D(0.f, 46.f));
	}

	UTextBlock* Subtitle = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Subtitle"));
	Subtitle->SetText(FText::FromString(TEXT("← → / A D 切换角色 · 回车或点击「开始经营」进入餐厅")));
	Subtitle->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 16));
	Subtitle->SetColorAndOpacity(FSlateColor(FLinearColor(0.75f, 0.75f, 0.78f, 1.f)));
	if (UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(Subtitle))
	{
		PanelSlot->SetAnchors(FAnchors(0.5f, 0.f));
		PanelSlot->SetAlignment(FVector2D(0.5f, 0.f));
		PanelSlot->SetAutoSize(true);
		PanelSlot->SetPosition(FVector2D(0.f, 108.f));
	}

	// 角色卡片行（数量由配置表决定）
	CardRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("CardRow"));
	if (UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(CardRow))
	{
		PanelSlot->SetAnchors(FAnchors(0.5f, 0.5f));
		PanelSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		PanelSlot->SetAutoSize(true);
		PanelSlot->SetPosition(FVector2D(0.f, -30.f));
	}

	// 底部角色信息面板
	UBorder* InfoPanel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("InfoPanel"));
	InfoPanel->SetPadding(FMargin(28.f, 18.f));
	InfoPanel->SetBrushColor(FLinearColor(0.055f, 0.065f, 0.09f, 0.96f));
	if (UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(InfoPanel))
	{
		PanelSlot->SetAnchors(FAnchors(0.5f, 1.f));
		PanelSlot->SetAlignment(FVector2D(0.5f, 1.f));
		PanelSlot->SetAutoSize(false);
		PanelSlot->SetSize(FVector2D(1000.f, 208.f));
		PanelSlot->SetPosition(FVector2D(0.f, -112.f));
	}

	UVerticalBox* InfoColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("InfoColumn"));
	InfoPanel->SetContent(InfoColumn);

	NameText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("RoleName"));
	NameText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 30));
	NameText->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.85f, 0.4f, 1.f)));
	InfoColumn->AddChildToVerticalBox(NameText);

	PersonalityText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Personality"));
	PersonalityText->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 18));
	PersonalityText->SetColorAndOpacity(FSlateColor(FLinearColor(0.85f, 0.9f, 1.f, 1.f)));
	if (UVerticalBoxSlot* PanelSlot = InfoColumn->AddChildToVerticalBox(PersonalityText))
	{
		PanelSlot->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));
	}

	LikesText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Likes"));
	LikesText->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 18));
	LikesText->SetColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.8f, 0.75f, 1.f)));
	InfoColumn->AddChildToVerticalBox(LikesText);

	DescriptionText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Description"));
	DescriptionText->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 16));
	DescriptionText->SetColorAndOpacity(FSlateColor(FLinearColor(0.88f, 0.88f, 0.86f, 1.f)));
	DescriptionText->SetAutoWrapText(true);
	if (UVerticalBoxSlot* PanelSlot = InfoColumn->AddChildToVerticalBox(DescriptionText))
	{
		PanelSlot->SetPadding(FMargin(0.f, 8.f, 0.f, 0.f));
	}

	// 开始按钮
	ConfirmButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("ConfirmButton"));
	ConfirmButton->SetBackgroundColor(FLinearColor(1.f, 0.68f, 0.22f, 1.f));
	if (UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(ConfirmButton))
	{
		PanelSlot->SetAnchors(FAnchors(1.f, 1.f));
		PanelSlot->SetAlignment(FVector2D(1.f, 1.f));
		PanelSlot->SetAutoSize(false);
		PanelSlot->SetSize(FVector2D(240.f, 64.f));
		PanelSlot->SetPosition(FVector2D(-60.f, -36.f));
	}
	UBorder* ConfirmContent = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ConfirmContent"));
	ConfirmContent->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.f));
	ConfirmContent->SetHorizontalAlignment(HAlign_Center);
	ConfirmContent->SetVerticalAlignment(VAlign_Center);
	UTextBlock* ConfirmText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ConfirmText"));
	ConfirmText->SetText(FText::FromString(TEXT("开始经营")));
	ConfirmText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 26));
	ConfirmText->SetColorAndOpacity(FSlateColor(FLinearColor(0.12f, 0.06f, 0.f, 1.f)));
	ConfirmContent->SetContent(ConfirmText);
	ConfirmButton->SetContent(ConfirmContent);
	ConfirmButton->OnClicked.AddDynamic(this, &UCharacterSelectWidget::OnConfirmClicked);

	// 生成卡片并确定预选角色
	RoleIDs = GameInstance ? GameInstance->GetSelectableRoleIDs() : TArray<int32>();
	for (int32 Index = 0; Index < RoleIDs.Num(); ++Index)
	{
		BuildCard(Index, RoleIDs[Index]);
	}

	if (RoleIDs.Contains(InitialRoleID))
	{
		CurrentRoleID = InitialRoleID;
	}
	else if (RoleIDs.Num() > 0)
	{
		CurrentRoleID = RoleIDs[0];
	}
	else
	{
		CurrentRoleID = 0;
	}

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

	URoleSelectCardButton* Card = WidgetTree->ConstructWidget<URoleSelectCardButton>(URoleSelectCardButton::StaticClass());
	Card->InitCard(RoleID, this);

	UBorder* CardBg = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	CardBg->SetPadding(FMargin(16.f, 16.f, 16.f, 12.f));
	CardBg->SetBrushColor(FLinearColor(0.075f, 0.085f, 0.11f, 1.f));
	Card->SetContent(CardBg);

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	CardBg->SetContent(Column);

	// 图标（200×200）；没有配图时用占位色块 + 名字首字
	USizeBox* IconBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	IconBox->SetWidthOverride(200.f);
	IconBox->SetHeightOverride(200.f);
	UImage* Icon = nullptr;
	if (Info.UIIcon)
	{
		Icon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
		Icon->SetBrushFromTexture(Info.UIIcon, false);
		IconBox->AddChild(Icon);
	}
	else
	{
		UBorder* Placeholder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Placeholder->SetBrushColor(FLinearColor(0.14f, 0.15f, 0.19f, 1.f));
		Placeholder->SetHorizontalAlignment(HAlign_Center);
		Placeholder->SetVerticalAlignment(VAlign_Center);
		UTextBlock* PlaceholderText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		const FString NameStr = GetRoleDisplayName(Info).ToString();
		PlaceholderText->SetText(FText::FromString(NameStr.Left(1)));
		PlaceholderText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 64));
		PlaceholderText->SetColorAndOpacity(FSlateColor(FLinearColor(0.45f, 0.48f, 0.55f, 1.f)));
		Placeholder->SetContent(PlaceholderText);
		IconBox->AddChild(Placeholder);
	}
	if (UVerticalBoxSlot* PanelSlot = Column->AddChildToVerticalBox(IconBox))
	{
		PanelSlot->SetHorizontalAlignment(HAlign_Center);
	}

	UTextBlock* CardName = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	CardName->SetText(GetRoleDisplayName(Info));
	CardName->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 24));
	CardName->SetJustification(ETextJustify::Center);
	CardName->SetColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.95f, 0.92f, 1.f)));
	if (UVerticalBoxSlot* PanelSlot = Column->AddChildToVerticalBox(CardName))
	{
		PanelSlot->SetPadding(FMargin(0.f, 10.f, 0.f, 0.f));
		PanelSlot->SetHorizontalAlignment(HAlign_Center);
	}

	UTextBlock* CardPersonality = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	CardPersonality->SetText(Info.Personality);
	CardPersonality->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 15));
	CardPersonality->SetJustification(ETextJustify::Center);
	CardPersonality->SetColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.75f, 0.8f, 1.f)));
	CardPersonality->SetVisibility(Info.Personality.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	if (UVerticalBoxSlot* PanelSlot = Column->AddChildToVerticalBox(CardPersonality))
	{
		PanelSlot->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));
		PanelSlot->SetHorizontalAlignment(HAlign_Center);
	}

	UHorizontalBoxSlot* RowSlot = CardRow->AddChildToHorizontalBox(Card);
	RowSlot->SetPadding(FMargin(14.f, 0.f));
	RowSlot->SetVerticalAlignment(VAlign_Top);

	CardButtons.Add(Card);
	CardBorders.Add(CardBg);
	CardIcons.Add(Icon);
	CardNameTexts.Add(CardName);
	CardPersonalityTexts.Add(CardPersonality);
}

void UCharacterSelectWidget::SelectRole(int32 RoleID)
{
	if (!RoleIDs.Contains(RoleID) || CurrentRoleID == RoleID)
	{
		return;
	}
	CurrentRoleID = RoleID;
	RefreshUI();
}

int32 UCharacterSelectWidget::GetCurrentRoleID() const
{
	return CurrentRoleID;
}

void UCharacterSelectWidget::RefreshUI()
{
	// 卡片高亮：选中项加金色描边与暖色底
	for (int32 Index = 0; Index < CardButtons.Num(); ++Index)
	{
		const bool bSelected = RoleIDs.IsValidIndex(Index) && RoleIDs[Index] == CurrentRoleID;
		if (URoleSelectCardButton* Card = CardButtons[Index])
		{
			Card->SetBackgroundColor(bSelected
				? FLinearColor(1.f, 0.85f, 0.45f, 1.f)
				: FLinearColor(0.85f, 0.85f, 0.9f, 1.f));
		}
		if (UBorder* CardBg = CardBorders.IsValidIndex(Index) ? CardBorders[Index] : nullptr)
		{
			CardBg->SetBrushColor(bSelected
				? FLinearColor(0.22f, 0.17f, 0.07f, 1.f)
				: FLinearColor(0.075f, 0.085f, 0.11f, 1.f));
		}
		if (UTextBlock* CardName = CardNameTexts.IsValidIndex(Index) ? CardNameTexts[Index] : nullptr)
		{
			CardName->SetColorAndOpacity(FSlateColor(bSelected
				? FLinearColor(1.f, 0.88f, 0.45f, 1.f)
				: FLinearColor(0.9f, 0.9f, 0.88f, 1.f)));
		}
	}

	FRoleInfo Info;
	if (GameInstance && CurrentRoleID != 0)
	{
		GameInstance->FindRoleInfo(CurrentRoleID, Info);
	}

	if (NameText)
	{
		NameText->SetText(GetRoleDisplayName(Info));
	}
	if (PersonalityText)
	{
		PersonalityText->SetText(FText::FromString(FString::Printf(TEXT("性格：%s"),
			Info.Personality.IsEmpty() ? TEXT("—") : *Info.Personality.ToString())));
	}
	if (LikesText)
	{
		LikesText->SetText(FText::FromString(FString::Printf(TEXT("喜好：%s"), *JoinLikes(Info.Likes).ToString())));
	}
	if (DescriptionText)
	{
		DescriptionText->SetText(Info.Description.IsEmpty()
			? FText::FromString(TEXT("（暂无介绍）"))
			: Info.Description);
	}
}

void UCharacterSelectWidget::OnConfirmClicked()
{
	if (GameInstance && CurrentRoleID != 0)
	{
		GameInstance->ConfirmRoleSelection(CurrentRoleID);
	}
}

void UCharacterSelectWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetKeyboardFocus();
}

void UCharacterSelectWidget::NativeDestruct()
{
	if (FSlateApplication::IsInitialized())
	{
		// 界面移除后把焦点交还给游戏视口，避免残留 UI 焦点
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}
	Super::NativeDestruct();
}

FReply UCharacterSelectWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Left || Key == EKeys::A)
	{
		if (RoleIDs.Num() > 0)
		{
			const int32 Index = FMath::Max(0, RoleIDs.IndexOfByKey(CurrentRoleID));
			SelectRole(RoleIDs[(Index - 1 + RoleIDs.Num()) % RoleIDs.Num()]);
		}
		return FReply::Handled();
	}
	if (Key == EKeys::Right || Key == EKeys::D)
	{
		if (RoleIDs.Num() > 0)
		{
			const int32 Index = FMath::Max(0, RoleIDs.IndexOfByKey(CurrentRoleID));
			SelectRole(RoleIDs[(Index + 1) % RoleIDs.Num()]);
		}
		return FReply::Handled();
	}
	if (Key == EKeys::Enter || Key == EKeys::SpaceBar)
	{
		OnConfirmClicked();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}
