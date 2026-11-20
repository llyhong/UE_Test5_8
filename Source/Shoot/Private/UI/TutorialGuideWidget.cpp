// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/TutorialGuideWidget.h"

#include "Game/TutorialTaskManager.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "SceneView.h"
#include "UnrealClient.h"
#include "Styling/CoreStyle.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** 每日任务面板整局唯一：挂在主 HUD 内，分屏不按玩家复制 */
	TWeakObjectPtr<UTutorialGuideWidget> GSharedGuide;

	FLinearColor TutorialColor(const TCHAR* Hex)
	{
		return FLinearColor::FromSRGBColor(FColor::FromHex(Hex));
	}

	UBorder* TutorialPanel(UWidgetTree* Tree, FLinearColor Fill, float Radius,
		FLinearColor Outline = FLinearColor::Transparent, float OutlineWidth = 0.f)
	{
		UBorder* Border = Tree->ConstructWidget<UBorder>();
		Border->SetBrush(FSlateRoundedBoxBrush(Fill, Radius, Outline, OutlineWidth));
		Border->SetPadding(FMargin(0.f));
		Border->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		return Border;
	}

	void Leaf(UWidgetTree* Tree, UCanvasPanel* Canvas, bool bRight, FVector2D Offset, float Angle)
	{
		UImage* Image = Tree->ConstructWidget<UImage>();
		Image->SetBrush(FSlateRoundedBoxBrush(TutorialColor(TEXT("A4B879")), FVector4(1.f, 9.f, 1.f, 9.f)));
		Image->SetVisibility(ESlateVisibility::HitTestInvisible);
		Image->SetRenderTransformAngle(Angle);
		UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(Image);
		Slot->SetAnchors(FAnchors(bRight ? 1.f : 0.f, 0.5f));
		Slot->SetAlignment(FVector2D(bRight ? 1.f : 0.f, 0.5f));
		Slot->SetPosition(Offset);
		Slot->SetSize(FVector2D(9.f, 16.f));
	}
}

UTutorialGuideWidget::UTutorialGuideWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	static ConstructorHelpers::FObjectFinder<UTexture2D> CompleteIcon(
		TEXT("/Game/_Demo/Textures/UI/T_TutorialComplete_Pastoral_v1.T_TutorialComplete_Pastoral_v1"));
	CompletionTexture = CompleteIcon.Object;
}

void UTutorialGuideWidget::EnsureForPlayer(APlayerController* PC, ATutorialTaskManager* InManager)
{
	if (!IsValid(PC) || !PC->IsLocalController() || !PC->GetLocalPlayer() || !IsValid(InManager))
	{
		return;
	}

	// 已有实例时只更新绑定：分屏下第二个本地玩家不会复制出第二份每日任务 UI。
	if (UTutorialGuideWidget* Existing = GSharedGuide.Get())
	{
		if (Existing->GetWorld() == PC->GetWorld())
		{
			Existing->SetManager(InManager);
			return;
		}
		GSharedGuide.Reset();
	}

	UTutorialGuideWidget* Guide = CreateWidget<UTutorialGuideWidget>(PC, UTutorialGuideWidget::StaticClass());
	if (!Guide)
	{
		return;
	}
	Guide->SetManager(InManager);
	Guide->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	if (Guide->AddToPlayerScreen(60))
	{
		GSharedGuide = Guide;
	}
}

void UTutorialGuideWidget::SetManager(ATutorialTaskManager* InManager)
{
	if (Manager.Get() != InManager)
	{
		CachedEntries.Reset();
		CachedDayTitle = FText::GetEmpty();
		ScrollTargetIndex = INDEX_NONE;
	}
	Manager = InManager;
	GSharedGuide = this;

	if (!OwnerController.IsValid())
	{
		if (APlayerController* PC = GetOwningPlayer())
		{
			OwnerController = PC;
		}
		else if (UWorld* World = GetWorld())
		{
			if (APlayerController* LocalPC = UGameplayStatics::GetPlayerController(World, 0))
			{
				OwnerController = LocalPC;
			}
		}
	}
}

TSharedRef<SWidget> UTutorialGuideWidget::RebuildWidget()
{
	TaskRows.Reset();
	TaskLines.Reset();
	TaskIcons.Reset();
	CachedEntries.Reset();
	CachedDayTitle = FText::GetEmpty();
	CachedChecklistWidth = 0.f;
	CachedScrollHeight = 0.f;
	ScrollTargetIndex = INDEX_NONE;

	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("TutorialRoot"));
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	WidgetTree->RootWidget = Root;

	ChecklistSize = WidgetTree->ConstructWidget<USizeBox>();
	ChecklistSize->SetWidthOverride(440.f);
	ChecklistSize->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	if (UCanvasPanelSlot* BorderSlot = Root->AddChildToCanvas(ChecklistSize))
	{
		BorderSlot->SetAnchors(FAnchors(1.f, 0.5f));
		BorderSlot->SetAlignment(FVector2D(1.f, 0.5f));
		BorderSlot->SetAutoSize(true);
		BorderSlot->SetPosition(FVector2D(-24.f, 0.f));
	}

	ChecklistBorder = TutorialPanel(WidgetTree, TutorialColor(TEXT("FFF3D8")), 22.f, TutorialColor(TEXT("D4AA60")), 1.5f);
	ChecklistBorder->SetPadding(FMargin(5.f));
	ChecklistSize->SetContent(ChecklistBorder);
	UBorder* InnerBorder = TutorialPanel(WidgetTree, TutorialColor(TEXT("FFF9E9")), 17.f, TutorialColor(TEXT("E4C68A")), 1.f);
	InnerBorder->SetPadding(FMargin(12.f, 10.f));
	ChecklistBorder->SetContent(InnerBorder);

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ChecklistColumn"));
	InnerBorder->SetContent(Column);
	UOverlay* Header = WidgetTree->ConstructWidget<UOverlay>();
	Column->AddChildToVerticalBox(Header);

	TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TitleText"));
	TitleText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 20));
	TitleText->SetColorAndOpacity(FSlateColor(TutorialColor(TEXT("54351F"))));
	TitleText->SetJustification(ETextJustify::Center);
	TitleText->SetAutoWrapText(true);
	TitleText->SetVisibility(ESlateVisibility::HitTestInvisible);
	UOverlaySlot* TitleSlot = Header->AddChildToOverlay(TitleText);
	TitleSlot->SetPadding(FMargin(24.f, 4.f, 24.f, 10.f));
	TitleSlot->SetHorizontalAlignment(HAlign_Fill);

	UCanvasPanel* Leaves = WidgetTree->ConstructWidget<UCanvasPanel>();
	Leaves->SetVisibility(ESlateVisibility::HitTestInvisible);
	Header->AddChildToOverlay(Leaves);
	Leaf(WidgetTree, Leaves, false, FVector2D(2.f, 0.f), 28.f);
	Leaf(WidgetTree, Leaves, false, FVector2D(11.f, -8.f), 55.f);
	Leaf(WidgetTree, Leaves, true, FVector2D(-2.f, 0.f), -28.f);
	Leaf(WidgetTree, Leaves, true, FVector2D(-11.f, -8.f), -55.f);

	USizeBox* DividerSize = WidgetTree->ConstructWidget<USizeBox>();
	DividerSize->SetHeightOverride(1.f);
	DividerSize->SetContent(TutorialPanel(WidgetTree, TutorialColor(TEXT("E4C68A")), 0.f));
	Column->AddChildToVerticalBox(DividerSize);

	ScrollSize = WidgetTree->ConstructWidget<USizeBox>();
	ScrollSize->SetMaxDesiredHeight(420.f);
	Column->AddChildToVerticalBox(ScrollSize)->SetPadding(FMargin(0.f, 8.f, 0.f, 0.f));
	TaskScroll = WidgetTree->ConstructWidget<UScrollBox>();
	TaskScroll->SetConsumeMouseWheel(EConsumeMouseWheel::WhenScrollingPossible);
	TaskScroll->SetAnimateWheelScrolling(true);
	TaskScroll->SetScrollBarVisibility(ESlateVisibility::Visible);
	TaskScroll->SetScrollbarThickness(FVector2D(6.f, 6.f));
	TaskScroll->SetClipping(EWidgetClipping::ClipToBounds);
	ScrollSize->SetContent(TaskScroll);
	UVerticalBox* ScrollColumn = WidgetTree->ConstructWidget<UVerticalBox>();
	TaskScroll->AddChild(ScrollColumn);

	TaskColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("TaskColumn"));
	ScrollColumn->AddChildToVerticalBox(TaskColumn);

	HintText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("HintText"));
	HintText->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 16));
	HintText->SetColorAndOpacity(FSlateColor(TutorialColor(TEXT("826547"))));
	HintText->SetAutoWrapText(true);
	HintText->SetVisibility(ESlateVisibility::Collapsed);
	// 当前操作固定在列表外，历史步骤再多也不会把按键说明挤出可视区。
	Column->AddChildToVerticalBox(HintText)->SetPadding(FMargin(8.f, 12.f, 8.f, 4.f));

	ArrowImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("ArrowImage"));
	FVector2D ArrowSize(96.f, 96.f);
	if (UTexture2D* ArrowTexture = LoadObject<UTexture2D>(nullptr, TEXT("/Game/_Demo/Textures/UI/icon_arrow_nav_right.icon_arrow_nav_right")))
	{
		ArrowImage->SetBrushFromTexture(ArrowTexture, false);
		// 保持贴图宽高比，长边缩放到 96px
		const float TexW = static_cast<float>(FMath::Max(ArrowTexture->GetSizeX(), 1));
		const float TexH = static_cast<float>(FMath::Max(ArrowTexture->GetSizeY(), 1));
		const float Scale = 96.f / FMath::Max(TexW, TexH);
		ArrowSize = FVector2D(TexW * Scale, TexH * Scale);
	}
	ArrowImage->SetVisibility(ESlateVisibility::Collapsed);
	if (UCanvasPanelSlot* ArrowSlot = Root->AddChildToCanvas(ArrowImage))
	{
		// 锚在屏幕中心，由 Tick 按目标方向偏移并旋转（贴图本身朝右）
		ArrowSlot->SetAnchors(FAnchors(0.5f, 0.5f));
		ArrowSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		ArrowSlot->SetAutoSize(false);
		ArrowSlot->SetSize(ArrowSize);
		ArrowSlot->SetPosition(FVector2D(0.f, 0.f));
	}

	return Root->TakeWidget();
}

void UTutorialGuideWidget::RebuildChecklist(float Width)
{
	ATutorialTaskManager* M = Manager.Get();
	if (!IsValid(M) || !TaskColumn || !TaskScroll)
	{
		return;
	}

	TArray<FTutorialChecklistEntry> Entries = M->GetVisibleChecklistEntries();
	const bool bLayoutChanged = !FMath::IsNearlyEqual(Width, CachedChecklistWidth);
	while (TaskRows.Num() > Entries.Num())
	{
		if (UBorder* Extra = TaskRows.Pop())
		{
			Extra->RemoveFromParent();
		}
		TaskLines.Pop();
		TaskIcons.Pop();
	}

	int32 CurrentIndex = INDEX_NONE;
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const FTutorialChecklistEntry& Entry = Entries[Index];
		if (!Entry.bNote && !Entry.bCompleted && CurrentIndex == INDEX_NONE)
		{
			CurrentIndex = Index;
		}
		const bool bNewRow = !TaskRows.IsValidIndex(Index);
		if (bNewRow)
		{
			UBorder* Row = TutorialPanel(WidgetTree, FLinearColor::Transparent, 9.f);
			Row->SetPadding(FMargin(8.f, 6.f));
			TaskColumn->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 2.f));
			UHorizontalBox* RowContent = WidgetTree->ConstructWidget<UHorizontalBox>();
			Row->SetContent(RowContent);

			USizeBox* TutorialIconSize = WidgetTree->ConstructWidget<USizeBox>();
			TutorialIconSize->SetWidthOverride(30.f);
			TutorialIconSize->SetHeightOverride(30.f);
			UImage* Icon = WidgetTree->ConstructWidget<UImage>();
			Icon->SetVisibility(ESlateVisibility::HitTestInvisible);
			TutorialIconSize->SetContent(Icon);
			RowContent->AddChildToHorizontalBox(TutorialIconSize)->SetVerticalAlignment(VAlign_Top);

			UTextBlock* Line = WidgetTree->ConstructWidget<UTextBlock>();
			Line->SetAutoWrapText(true);
			Line->SetVisibility(ESlateVisibility::HitTestInvisible);
			UHorizontalBoxSlot* LineSlot = RowContent->AddChildToHorizontalBox(Line);
			LineSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			LineSlot->SetPadding(FMargin(10.f, 2.f, 0.f, 2.f));
			LineSlot->SetVerticalAlignment(VAlign_Center);
			TaskRows.Add(Row);
			TaskIcons.Add(Icon);
			TaskLines.Add(Line);
		}

		const bool bEntryChanged = !CachedEntries.IsValidIndex(Index)
			|| CachedEntries[Index].bCompleted != Entry.bCompleted
			|| CachedEntries[Index].bNote != Entry.bNote
			|| !CachedEntries[Index].Text.EqualTo(Entry.Text);
		if (bNewRow || bEntryChanged || bLayoutChanged)
		{
			const bool bCurrent = Index == CurrentIndex;
			TaskRows[Index]->SetBrush(FSlateRoundedBoxBrush(
				bCurrent ? TutorialColor(TEXT("FFE9B7")) : FLinearColor::Transparent, 9.f));
			UImage* Icon = TaskIcons[Index];
			Icon->GetParent()->SetVisibility(Entry.bNote ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
			if (Entry.bCompleted && CompletionTexture)
			{
				Icon->SetBrushFromTexture(CompletionTexture, false);
				Icon->SetDesiredSizeOverride(FVector2D(30.f, 30.f));
			}
			else
			{
				Icon->SetBrush(FSlateRoundedBoxBrush(TutorialColor(TEXT("FFF6DF")), 6.f, TutorialColor(TEXT("D4AA60")), 1.2f));
				Icon->SetDesiredSizeOverride(FVector2D(30.f, 30.f));
			}
			UTextBlock* Line = TaskLines[Index];
			Line->SetText(Entry.Text);
			Line->SetFont(FCoreStyle::GetDefaultFontStyle(Entry.bNote ? "Regular" : "Bold", Entry.bNote ? 16 : 18));
			Line->SetColorAndOpacity(FSlateColor(TutorialColor(Entry.bCompleted ? TEXT("456B34")
				: (Entry.bNote ? TEXT("826547") : TEXT("54351F")))));
			// 为滚动条、图标及内边距预留空间，换行宽度不超过实际文本区域。
			Line->SetWrapTextAt(FMath::Max(48.f, Width - (Entry.bNote ? 92.f : 122.f)));
		}
	}

	const int32 DesiredScrollIndex = CurrentIndex != INDEX_NONE ? CurrentIndex : Entries.Num() - 1;
	if ((DesiredScrollIndex != ScrollTargetIndex || bLayoutChanged) && TaskRows.IsValidIndex(DesiredScrollIndex))
	{
		// 最新任务位于列表末尾；在布局完成后滚至底部，长任务和当天备注都能进入视区。
		TaskScroll->ScrollToEnd();
	}
	ScrollTargetIndex = DesiredScrollIndex;
	CachedChecklistWidth = Width;
	CachedEntries = MoveTemp(Entries);
	ChecklistBorder->SetVisibility(CachedEntries.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
}

void UTutorialGuideWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	ATutorialTaskManager* M = Manager.Get();
	if (!IsValid(M) || !M->bEnable || !TaskColumn || !TitleText || !HintText || !ChecklistSize || !ScrollSize)
	{
		if (ChecklistBorder)
		{
			ChecklistBorder->SetVisibility(ESlateVisibility::Collapsed);
		}
		if (ArrowImage)
		{
			ArrowImage->SetVisibility(ESlateVisibility::Collapsed);
		}
		bArrowVisible = false;
		return;
	}

	const FVector2D LocalSize = MyGeometry.GetLocalSize();
	const float Width = FMath::Clamp(LocalSize.X - 48.f, 160.f, 440.f);
	bool bMeasureFixedContent = !FMath::IsNearlyEqual(Width, CachedChecklistWidth);
	if (!FMath::IsNearlyEqual(Width, CachedChecklistWidth))
	{
		ChecklistSize->SetWidthOverride(Width);
		TitleText->SetWrapTextAt(FMath::Max(48.f, Width - 82.f));
		HintText->SetWrapTextAt(FMath::Max(48.f, Width - 66.f));
	}
	const FText DayTitle = M->GetDayTitle();
	if (!CachedDayTitle.EqualTo(DayTitle))
	{
		bMeasureFixedContent = true;
		TitleText->SetText(DayTitle);
		CachedDayTitle = DayTitle;
		CachedEntries.Reset();
		ScrollTargetIndex = INDEX_NONE;
	}
	RebuildChecklist(Width);
	const FText FocusHint = M->GetFocusHint();
	if (!HintText->GetText().EqualTo(FocusHint))
	{
		bMeasureFixedContent = true;
		HintText->SetText(FocusHint);
	}
	HintText->SetVisibility(FocusHint.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	// 文案换行后再计算剩余高度，为固定标题和操作说明留足位置。
	if (bMeasureFixedContent) ForceLayoutPrepass();
	const float FixedHeight = TitleText->GetDesiredSize().Y + 18.f
		+ (FocusHint.IsEmpty() ? 0.f : HintText->GetDesiredSize().Y + 16.f) + 39.f;
	const float TutorialCardHeight = FMath::Min(720.f, FMath::Max(180.f, LocalSize.Y - 48.f));
	const float ScrollHeight = FMath::Max(48.f, TutorialCardHeight - FixedHeight);
	if (!FMath::IsNearlyEqual(ScrollHeight, CachedScrollHeight))
	{
		ScrollSize->SetMaxDesiredHeight(ScrollHeight);
		CachedScrollHeight = ScrollHeight;
		TaskScroll->ScrollToEnd();
	}

	APlayerController* PC = OwnerController.Get();
	FVector TargetLocation;
	if (!PC || !M->GetFocusTargetLocation(TargetLocation))
	{
		if (ArrowImage)
		{
			ArrowImage->SetVisibility(ESlateVisibility::Collapsed);
		}
		bArrowVisible = false;
		return;
	}

	FIntRect PlayerViewRect(0, 0, 0, 0);
	FIntPoint WindowSize(0, 0);
	if (ULocalPlayer* LP = PC->GetLocalPlayer())
	{
		if (UGameViewportClient* VC = LP->ViewportClient)
		{
			FSceneViewProjectionData ProjectionData;
			if (VC->Viewport && LP->GetProjectionData(VC->Viewport, ProjectionData))
			{
				PlayerViewRect = ProjectionData.GetConstrainedViewRect();
				WindowSize = VC->Viewport->GetSizeXY();
			}
		}
	}
	if (PlayerViewRect.Width() <= 1 || PlayerViewRect.Height() <= 1 || WindowSize.X <= 1 || WindowSize.Y <= 1)
	{
		if (ArrowImage)
		{
			ArrowImage->SetVisibility(ESlateVisibility::Collapsed);
		}
		bArrowVisible = false;
		return;
	}

	// 面板挂在 WBP_HUD 内、铺满整个窗口；分屏时用该玩家自己的视口矩形换算，
	// 保证单屏和分屏下箭头都落在玩家自己的画面区域内。
	const FVector2D WindowScale(LocalSize.X / WindowSize.X, LocalSize.Y / WindowSize.Y);
	auto PlayerViewToLocal = [&](const FVector2D& InViewPos)
	{
		return FVector2D((PlayerViewRect.Min.X + InViewPos.X) * WindowScale.X,
			(PlayerViewRect.Min.Y + InViewPos.Y) * WindowScale.Y);
	};
	const FVector2D ViewMin = PlayerViewToLocal(FVector2D::ZeroVector);
	const FVector2D ViewMax = PlayerViewToLocal(FVector2D(PlayerViewRect.Width(), PlayerViewRect.Height()));
	const FVector2D ViewSize = ViewMax - ViewMin;
	const FVector2D ViewCenterLocal = (ViewMin + ViewMax) * 0.5f;

	FVector2D ScreenPos;
	// 可见目标直接在物件上方标记，避免玩家把屏幕中心的方向箭头误认成目的地。
	FVector2D MarkerPos = FVector2D::ZeroVector;
	const FVector MarkerLocation = TargetLocation + FVector(0.f, 0.f, 160.f);
	const bool bMarkerProjected = UGameplayStatics::ProjectWorldToScreen(PC, MarkerLocation, MarkerPos, true);
	const FVector2D MarkerLocal = PlayerViewToLocal(MarkerPos);
	const float Bob = FMath::Sin(GetWorld()->GetTimeSeconds() * (2.f * PI / 1.4f)) * 8.f;
	const FVector2D MarkerCenter = MarkerLocal + FVector2D(0.f, -40.f + Bob);
	const bool bMarkerOnScreen = bMarkerProjected
		&& MarkerCenter.X >= ViewMin.X + 48.f && MarkerCenter.X <= ViewMax.X - 48.f
		&& MarkerCenter.Y >= ViewMin.Y + 48.f && MarkerCenter.Y <= ViewMax.Y - 48.f;
	if (ArrowImage && bMarkerOnScreen)
	{
		ArrowImage->SetVisibility(ESlateVisibility::HitTestInvisible);
		ArrowImage->SetRenderTranslation(MarkerCenter - LocalSize * 0.5f);
		ArrowImage->SetRenderTransformAngle(90.f);
		bArrowVisible = true;
		return;
	}
	// 目标离开画面时仍用方向引导，走近后自动切回目标上方的浮动标记。
	const bool bProjected = UGameplayStatics::ProjectWorldToScreen(PC, TargetLocation, ScreenPos, true);
	if (bProjected)
	{
		const FVector2D Delta = PlayerViewToLocal(ScreenPos) - ViewCenterLocal;
		if (Delta.SizeSquared() > 1.f)
		{
			ArrowDir = Delta.GetSafeNormal();
		}
		bArrowVisible = true;
	}
	else
	{
		// 目标在身后仍提供转身方向，避免初次开门时箭头消失。
		FVector ViewLocation;
		FRotator ViewRotation;
		PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
		const FVector LocalTarget = ViewRotation.UnrotateVector(TargetLocation - ViewLocation);
		ArrowDir = FVector2D(LocalTarget.Y, -LocalTarget.Z).GetSafeNormal();
		if (ArrowDir.IsNearlyZero()) ArrowDir = FVector2D(1.f, 0.f);
		bArrowVisible = true;
	}

	if (ArrowImage)
	{
		ArrowImage->SetVisibility(bArrowVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (bArrowVisible)
		{
			const float Radius = FMath::Clamp(FMath::Min(ViewSize.X, ViewSize.Y) * 0.30f, 60.f, 320.f);
			// 箭头槽位锚在窗口中心，先把圆心挪到该玩家画面中心，再按半径偏移。
			ArrowImage->SetRenderTranslation((ViewCenterLocal - LocalSize * 0.5f) + ArrowDir * Radius);
			// 贴图默认朝右：按目标方向的屏幕角度旋转（正值在 Slate 里为顺时针）
			ArrowImage->SetRenderTransformAngle(FMath::RadiansToDegrees(FMath::Atan2(ArrowDir.Y, ArrowDir.X)));
		}
	}
}

void UTutorialGuideWidget::NativeDestruct()
{
	if (GSharedGuide.Get() == this)
	{
		GSharedGuide.Reset();
	}
	Manager = nullptr;
	Super::NativeDestruct();
}
