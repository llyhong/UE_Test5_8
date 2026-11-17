// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/SupplyWarningWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Styling/CoreStyle.h"

#include "Data/ItemActorInfo.h"
#include "Game/BaseGameInstance.h"
#include "Item/FoodSpawner.h"
#include "Item/ItemActor.h"

namespace
{
	constexpr float WarningRefreshInterval = 0.5f;
	constexpr int32 WarningZOrder = 85;

	TMap<TWeakObjectPtr<APlayerController>, TWeakObjectPtr<USupplyWarningWidget>> ActiveWarnings;
}

void USupplyWarningWidget::EnsureForPlayer(APlayerController* PC)
{
	if (!PC || ActiveWarnings.Contains(TWeakObjectPtr<APlayerController>(PC)))
	{
		return;
	}

	USupplyWarningWidget* Warning = CreateWidget<USupplyWarningWidget>(PC, USupplyWarningWidget::StaticClass());
	if (!Warning)
	{
		return;
	}
	Warning->OwnerController = PC;
	Warning->AddToViewport(WarningZOrder);
	ActiveWarnings.Add(TWeakObjectPtr<APlayerController>(PC), Warning);
}

TSharedRef<SWidget> USupplyWarningWidget::RebuildWidget()
{
	if (WidgetTree->RootWidget)
	{
		return Super::RebuildWidget();
	}

	SetIsFocusable(false);
	SetVisibility(ESlateVisibility::HitTestInvisible);

	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;

	BannerBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Banner"));
	BannerBorder->SetPadding(FMargin(20.f, 10.f, 20.f, 12.f));
	BannerBorder->SetBrushColor(FLinearColor(0.06f, 0.05f, 0.03f, 0.86f));

	BannerBrush.DrawAs = ESlateBrushDrawType::RoundedBox;
	BannerBrush.OutlineSettings.CornerRadii = FVector4(8.f, 8.f, 8.f, 8.f);
	BannerBrush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
	BannerBrush.OutlineSettings.Width = 1.5f;
	BannerBrush.OutlineSettings.Color = FSlateColor(FLinearColor(0.95f, 0.65f, 0.2f, 0.9f));
	BannerBorder->SetBrush(BannerBrush);

	if (UCanvasPanelSlot* BannerSlot = Root->AddChildToCanvas(BannerBorder))
	{
		BannerSlot->SetAnchors(FAnchors(0.5f, 0.f));
		BannerSlot->SetAlignment(FVector2D(0.5f, 0.f));
		BannerSlot->SetPosition(FVector2D(0.f, 28.f));
		BannerSlot->SetAutoSize(true);
	}

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
	BannerBorder->SetContent(Column);

	TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Title"));
	TitleText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 16));
	TitleText->SetColorAndOpacity(FSlateColor(FLinearColor(0.98f, 0.75f, 0.25f, 1.f)));
	TitleText->SetText(FText::FromString(TEXT("食材不足，去野外采摘补充")));
	if (UVerticalBoxSlot* TitleSlot = Column->AddChildToVerticalBox(TitleText))
	{
		TitleSlot->SetHorizontalAlignment(HAlign_Center);
	}

	BodyText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Body"));
	BodyText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 13));
	BodyText->SetColorAndOpacity(FSlateColor(FLinearColor(0.86f, 0.86f, 0.88f, 1.f)));
	BodyText->SetAutoWrapText(true);
	if (UVerticalBoxSlot* BodySlot = Column->AddChildToVerticalBox(BodyText))
	{
		BodySlot->SetHorizontalAlignment(HAlign_Center);
		BodySlot->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));
	}

	BannerBorder->SetVisibility(ESlateVisibility::Collapsed);

	return Super::RebuildWidget();
}

void USupplyWarningWidget::Refresh()
{
	APlayerController* PC = OwnerController.Get();
	UWorld* World = PC ? PC->GetWorld() : nullptr;
	UBaseGameInstance* GI = World ? Cast<UBaseGameInstance>(World->GetGameInstance()) : nullptr;
	if (!BannerBorder || !GI)
	{
		return;
	}

	const int32 Threshold = FMath::Max(1, GI->SpawnerLowStockThreshold);

	// 按食材类型聚合：同一食材可能有多个生成箱
	TMap<UClass*, int32> StockByClass;
	TArray<UClass*> Order;
	for (TActorIterator<AFoodSpawner> It(World); It; ++It)
	{
		AFoodSpawner* Spawner = *It;
		UClass* ItemClass = Spawner->SpawnItemActorClass.Get();
		if (!ItemClass)
		{
			continue;
		}

		int32 Stock = 0;
		if (GI->bLinkSpawnerToWarehouse)
		{
			// 仓库库存是全局口径，同一食材多个箱子只统计一次
			if (StockByClass.Contains(ItemClass))
			{
				continue;
			}
			Stock = GI->GetWarehouseStock(Spawner->SpawnItemActorClass);
		}
		else
		{
			// 无限箱永远够用，不参与提醒
			if (Spawner->RemainingCount < 0)
			{
				continue;
			}
			Stock = Spawner->RemainingCount;
		}

		if (int32* Found = StockByClass.Find(ItemClass))
		{
			*Found += Stock;
		}
		else
		{
			StockByClass.Add(ItemClass, Stock);
			Order.Add(ItemClass);
		}
	}

	TArray<FString> LowItems;
	for (UClass* ItemClass : Order)
	{
		const int32 Stock = StockByClass.FindRef(ItemClass);
		if (Stock >= Threshold)
		{
			continue;
		}

		FString Name = ItemClass->GetName();
		if (const AItemActor* CDO = ItemClass->GetDefaultObject<AItemActor>())
		{
			if (const FItemActorInfo* Row = CDO->ItemInfoRow.GetRow<FItemActorInfo>(TEXT("SupplyWarning")))
			{
				if (!Row->ItemDes.IsEmpty())
				{
					Name = Row->ItemDes;
				}
			}
		}
		LowItems.Add(FString::Printf(TEXT("%s %d/%d"), *Name, Stock, Threshold));
	}

	const bool bShow = LowItems.Num() > 0;
	if (BodyText)
	{
		BodyText->SetText(FText::FromString(FString::Join(LowItems, TEXT("    "))));
	}
	if (bShow != bWarningVisible)
	{
		bWarningVisible = bShow;
		BannerBorder->SetVisibility(bShow ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void USupplyWarningWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	RefreshAccumulator += InDeltaTime;
	if (RefreshAccumulator < WarningRefreshInterval)
	{
		return;
	}
	RefreshAccumulator = 0.f;
	Refresh();
}

void USupplyWarningWidget::NativeDestruct()
{
	if (APlayerController* PC = OwnerController.Get())
	{
		ActiveWarnings.Remove(TWeakObjectPtr<APlayerController>(PC));
	}
	Super::NativeDestruct();
}
