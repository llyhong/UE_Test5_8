// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/TutorialGuideWidget.h"

#include "Game/TutorialTaskManager.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Texture2D.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Styling/CoreStyle.h"

namespace
{
	/** 每个本地玩家一份（分屏时各自独立），Widget 销毁后自动失效 */
	TMap<TWeakObjectPtr<APlayerController>, TWeakObjectPtr<UTutorialGuideWidget>> GPlayerGuides;
}

void UTutorialGuideWidget::EnsureForPlayer(APlayerController* PC, ATutorialTaskManager* InManager)
{
	if (!PC || !PC->IsLocalController())
	{
		return;
	}

	if (TWeakObjectPtr<UTutorialGuideWidget>* Found = GPlayerGuides.Find(PC))
	{
		if (UTutorialGuideWidget* Existing = Found->Get())
		{
			Existing->Manager = InManager;
			return;
		}
		GPlayerGuides.Remove(PC);
	}

	UTutorialGuideWidget* Guide = CreateWidget<UTutorialGuideWidget>(PC, UTutorialGuideWidget::StaticClass());
	if (!Guide)
	{
		return;
	}
	Guide->Manager = InManager;
	Guide->OwnerController = PC;
	Guide->AddToViewport(60);
	GPlayerGuides.Add(PC, Guide);
}

TSharedRef<SWidget> UTutorialGuideWidget::RebuildWidget()
{
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("TutorialRoot"));
	WidgetTree->RootWidget = Root;

	ChecklistBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ChecklistBorder"));
	ChecklistBorder->SetBrushColor(FLinearColor(0.02f, 0.02f, 0.02f, 0.55f));
	ChecklistBorder->SetPadding(FMargin(16.f, 12.f));
	if (UCanvasPanelSlot* BorderSlot = Root->AddChildToCanvas(ChecklistBorder))
	{
		// 任务清单贴右侧竖直居中：右缘留 40px 安全边距，视野中心留给玩法与箭头
		BorderSlot->SetAnchors(FAnchors(1.f, 0.5f));
		BorderSlot->SetAlignment(FVector2D(1.f, 0.5f));
		BorderSlot->SetAutoSize(true);
		BorderSlot->SetPosition(FVector2D(-40.f, 0.f));
	}

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ChecklistColumn"));
	ChecklistBorder->SetContent(Column);

	TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TitleText"));
	TitleText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 18));
	TitleText->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.82f, 0.3f, 1.f)));
	Column->AddChildToVerticalBox(TitleText);

	TaskColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("TaskColumn"));
	Column->AddChildToVerticalBox(TaskColumn);

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

void UTutorialGuideWidget::RebuildChecklist()
{
	ATutorialTaskManager* M = Manager.Get();
	if (!M || !TaskColumn)
	{
		return;
	}

	const TArray<FString> Lines = M->GetChecklistLines();
	while (TaskLines.Num() > Lines.Num())
	{
		if (UTextBlock* Extra = TaskLines.Pop())
		{
			Extra->RemoveFromParent();
		}
	}
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		if (!TaskLines.IsValidIndex(Index))
		{
			UTextBlock* Line = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			Line->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 15));
			Line->SetColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.95f, 0.9f, 1.f)));
			Line->SetShadowOffset(FVector2D(1.f, 1.f));
			Line->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.8f));
			TaskColumn->AddChildToVerticalBox(Line);
			TaskLines.Add(Line);
		}
		TaskLines[Index]->SetText(FText::FromString(Lines[Index]));
	}
}

void UTutorialGuideWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	ATutorialTaskManager* M = Manager.Get();
	if (!M || !M->HasActiveTasks())
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

	if (ChecklistBorder)
	{
		ChecklistBorder->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	TitleText->SetText(M->GetDayTitle());
	RebuildChecklist();

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

	// 视口尺寸取该玩家自己的（分屏时左右半屏尺寸不同）
	FVector2D ViewSize(1920.f, 1080.f);
	if (ULocalPlayer* LP = PC->GetLocalPlayer())
	{
		if (UGameViewportClient* VC = LP->ViewportClient)
		{
			if (VC->Viewport)
			{
				const FIntPoint SizeXY = VC->Viewport->GetSizeXY();
				if (SizeXY.X > 1 && SizeXY.Y > 1)
				{
					ViewSize = FVector2D(SizeXY.X, SizeXY.Y);
				}
			}
		}
	}

	FVector2D ScreenPos;
	const bool bProjected = UGameplayStatics::ProjectWorldToScreen(PC, TargetLocation, ScreenPos, true);
	if (bProjected)
	{
		const FVector2D Delta = ScreenPos - ViewSize * 0.5f;
		if (Delta.SizeSquared() > 1.f)
		{
			ArrowDir = Delta.GetSafeNormal();
		}
		bArrowVisible = true;
	}
	else
	{
		// 目标在身后：投影无效，本帧不显示箭头
		bArrowVisible = false;
	}

	if (ArrowImage)
	{
		ArrowImage->SetVisibility(bArrowVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (bArrowVisible)
		{
			const FVector2D Size = MyGeometry.GetLocalSize();
			const float Radius = FMath::Clamp(FMath::Min(Size.X, Size.Y) * 0.30f, 100.f, 320.f);
			ArrowImage->SetRenderTranslation(ArrowDir * Radius);
			// 贴图默认朝右：按目标方向的屏幕角度旋转（正值在 Slate 里为顺时针）
			ArrowImage->SetRenderTransformAngle(FMath::RadiansToDegrees(FMath::Atan2(ArrowDir.Y, ArrowDir.X)));
		}
	}
}

void UTutorialGuideWidget::NativeDestruct()
{
	for (auto It = GPlayerGuides.CreateIterator(); It; ++It)
	{
		if (It.Value().Get() == this || !It.Value().IsValid())
		{
			It.RemoveCurrent();
		}
	}
	Manager = nullptr;
	Super::NativeDestruct();
}
