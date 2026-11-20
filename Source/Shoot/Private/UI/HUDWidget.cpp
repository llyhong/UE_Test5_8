// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/HUDWidget.h"
#include "Game/EconomySystem.h"
#include "Game/BaseLevelBP.h"
#include "Game/TutorialTaskManager.h"
#include "UI/TutorialGuideWidget.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/TextBlock.h"
#include "Styling/CoreStyle.h"
#include "GameFramework/PlayerController.h"

void UHUDWidget::BindEconomy(UEconomySystem* Eco)
{
	Economy = Eco;
	if (Eco)
	{
		Eco->OnReputationChanged.AddDynamic(this, &UHUDWidget::HandleReputationChanged);
		Eco->OnMoneyChanged.AddDynamic(this, &UHUDWidget::HandleMoneyChanged);
		RefreshGoal();
	}
}

void UHUDWidget::BindLevel(ABaseLevelBP* Level)
{
	// 若之前已绑定过其它关卡，先解绑旧委托，避免跨关卡残留
	if (CachedLevel && CachedLevel != Level)
	{
		CachedLevel->LevelUpdateScore.RemoveDynamic(this, &UHUDWidget::HandleLevelScoreChanged);
	}
	CachedLevel = Level;
	if (Level)
	{
		// 分数变化（加分 / 服务客人）时刷新目标进度
		Level->LevelUpdateScore.AddDynamic(this, &UHUDWidget::HandleLevelScoreChanged);
		// 目标达成时显示通关结算面板（携带本关统计）
		Level->OnLevelGoalCompleted.AddDynamic(this, &UHUDWidget::HandleLevelGoalCompleted);
		// 进入下一关“打烊”时显示打烊横幅与倒计时
		Level->OnLevelClosing.AddDynamic(this, &UHUDWidget::HandleLevelClosing);
	}
	RefreshGoal();
}

namespace
{
	/** 每日任务面板由 HUD 持有：按 HUD 记录唯一实例，避免分屏时每个玩家复制一份 */
	struct FTutorialGuideBinding
	{
		TWeakObjectPtr<UTutorialGuideWidget> Guide;
		bool bInsideHud = false;
	};

	TMap<UHUDWidget*, FTutorialGuideBinding> GTutorialGuides;
}

void UHUDWidget::EnsureTutorialGuide(ATutorialTaskManager* InManager)
{
	if (!IsValid(InManager))
	{
		return;
	}

	FTutorialGuideBinding& Binding = GTutorialGuides.FindOrAdd(this);
	UTutorialGuideWidget* Guide = Binding.Guide.Get();
	if (!Guide)
	{
		APlayerController* PC = GetOwningPlayer();
		Guide = PC
			? CreateWidget<UTutorialGuideWidget>(PC, UTutorialGuideWidget::StaticClass())
			: CreateWidget<UTutorialGuideWidget>(GetWorld(), UTutorialGuideWidget::StaticClass());
		if (!Guide)
		{
			return;
		}
		Guide->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

		// 优先并入 WBP_HUD 自己的控件树，让每日任务和常驻 HUD 共用同一层、同一生命周期；
		// 分屏时 HUD 只有一份，任务面板也就只显示一次。
		Binding.bInsideHud = false;
		if (UWidgetTree* Tree = WidgetTree)
		{
			if (UCanvasPanel* Canvas = Cast<UCanvasPanel>(Tree->RootWidget))
			{
				if (UCanvasPanelSlot* GuideSlot = Canvas->AddChildToCanvas(Guide))
				{
					GuideSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
					GuideSlot->SetOffsets(FMargin(0.f));
					GuideSlot->SetAlignment(FVector2D(0.f, 0.f));
					// 画布内压在本 HUD 其它元素之上，但仍低于订单/赛季等独立面板
					GuideSlot->SetZOrder(60);
					Binding.bInsideHud = true;
				}
			}
			else if (UOverlay* Overlay = Cast<UOverlay>(Tree->RootWidget))
			{
				if (UOverlaySlot* GuideSlot = Overlay->AddChildToOverlay(Guide))
				{
					GuideSlot->SetHorizontalAlignment(HAlign_Fill);
					GuideSlot->SetVerticalAlignment(VAlign_Fill);
					Binding.bInsideHud = true;
				}
			}
		}

		// 根控件不是画布/Overlay 时（例如蓝图结构被改动）退回主玩家屏幕，保证功能不丢。
		if (!Binding.bInsideHud && !Guide->AddToPlayerScreen(60))
		{
			Guide->RemoveFromParent();
			GTutorialGuides.Remove(this);
			return;
		}
		Binding.Guide = Guide;
	}

	Guide->SetManager(InManager);
}

void UHUDWidget::RefreshGoal()
{
	// 统一收集经济与关卡目标数据，调用合并后的单一路由 RefreshHUD
	const int32 Rep = GetReputation();

	// 顶部“金钱”显示本关已赚金钱（MoneyEarnedThisLevel），开局为 0，上菜才累加；
	// 不显示全局持久钱包（Economy->GetMoney()，来自存档，会造成“一道菜没上却显示 30”的错觉）。
	int32 Mon = 0;
	int32 LvScore = 0;
	int32 Guests = 0;

	FLevelGoalRow ResolvedGoal;
	float GoalRatio = 1.0f;
	FString GoalDetail;
	bool bGoalAchieved = false;
	if (CachedLevel)
	{
		Mon = CachedLevel->MoneyEarnedThisLevel;
		LvScore = CachedLevel->Score;
		Guests = CachedLevel->GuestsServedThisLevel;
		ResolvedGoal = CachedLevel->ResolvedGoal;
		GoalRatio = CachedLevel->GetGoalProgressRatio();
		GoalDetail = CachedLevel->GetGoalProgressText();
		bGoalAchieved = CachedLevel->bGoalAchieved;
	}
	else
	{
		GoalDetail = TEXT("无目标");
	}

	RefreshHUD(Rep, Mon, LvScore, Guests, ResolvedGoal, GoalRatio, GoalDetail, bGoalAchieved);
}

void UHUDWidget::NativeDestruct()
{
	if (FTutorialGuideBinding* Binding = GTutorialGuides.Find(this))
	{
		if (UTutorialGuideWidget* Guide = Binding->Guide.Get())
		{
			Guide->RemoveFromParent();
		}
		GTutorialGuides.Remove(this);
	}
	if (CachedLevel)
	{
		CachedLevel->LevelUpdateScore.RemoveDynamic(this, &UHUDWidget::HandleLevelScoreChanged);
		CachedLevel->OnLevelGoalCompleted.RemoveDynamic(this, &UHUDWidget::HandleLevelGoalCompleted);
		CachedLevel->OnLevelClosing.RemoveDynamic(this, &UHUDWidget::HandleLevelClosing);
		CachedLevel = nullptr;
	}
	if (Economy)
	{
		Economy->OnReputationChanged.RemoveDynamic(this, &UHUDWidget::HandleReputationChanged);
		Economy->OnMoneyChanged.RemoveDynamic(this, &UHUDWidget::HandleMoneyChanged);
		Economy = nullptr;
	}
	Super::NativeDestruct();
}

void UHUDWidget::SetClock(float CurrentHour, bool bIsOpen)
{
	OnClockChanged(CurrentHour, bIsOpen);
}

void UHUDWidget::SetKitchenPlateHint(int32 CleanPlates)
{
	static const FName HintName(TEXT("KitchenPlateHint"));
	const bool bShow = CleanPlates == 0 || CleanPlates == 1;
	UBorder* Hint = Cast<UBorder>(GetWidgetFromName(HintName));
	if (!bShow)
	{
		if (Hint)
		{
			Hint->SetVisibility(ESlateVisibility::Collapsed);
		}
		return;
	}

	if (!Hint)
	{
		UCanvasPanel* Canvas = WidgetTree ? Cast<UCanvasPanel>(WidgetTree->RootWidget) : nullptr;
		if (!Canvas)
		{
			return;
		}
		Hint = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), HintName);
		Hint->SetBrushColor(FLinearColor(0.12f, 0.07f, 0.04f, 0.92f));
		Hint->SetPadding(FMargin(16.f, 8.f));
		UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>();
		Label->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 17));
		Hint->SetContent(Label);

		UCanvasPanelSlot* HintSlot = Canvas->AddChildToCanvas(Hint);
		HintSlot->SetAnchors(FAnchors(0.5f, 1.f));
		HintSlot->SetAlignment(FVector2D(0.5f, 1.f));
		HintSlot->SetPosition(FVector2D(0.f, -72.f));
		HintSlot->SetAutoSize(true);
		HintSlot->SetZOrder(20);
	}

	UTextBlock* Label = CastChecked<UTextBlock>(Hint->GetContent());
	Label->SetColorAndOpacity(FSlateColor(CleanPlates == 0
		? FLinearColor(1.f, 0.65f, 0.5f) : FLinearColor(1.f, 0.88f, 0.61f)));
	Label->SetText(CleanPlates == 0
		? NSLOCTEXT("KitchenPlate", "NoCleanPlates", "没有干净盘子了 · 按 E 收空盘，送到洗碗机")
		: NSLOCTEXT("KitchenPlate", "OneCleanPlate", "还剩 1 个干净盘 · 可以收盘送洗"));
	Hint->SetVisibility(ESlateVisibility::HitTestInvisible);
}

FString UHUDWidget::FormatClock(float CurrentHour)
{
	// 取当天 0~24 范围内的小时（处理跨 24 的边界）
	const float Hour = FMath::Fmod(FMath::Max(CurrentHour, 0.f), 24.f);
	// 四舍五入到最近的 30 分钟，使显示只在整点 / 半点（如 8:00、8:30、9:00）
	int32 TotalMinutes = FMath::RoundToInt(Hour * 60.f);
	TotalMinutes = FMath::RoundToInt(static_cast<float>(TotalMinutes) / 30.f) * 30;
	const int32 H = TotalMinutes / 60;
	const int32 M = TotalMinutes % 60;
	return FString::Printf(TEXT("%02d:%02d"), H, M);
}

int32 UHUDWidget::GetReputation() const
{
	return Economy ? Economy->GetReputation() : 0;
}

int32 UHUDWidget::GetMoney() const
{
	return Economy ? Economy->GetMoney() : 0;
}

void UHUDWidget::SetSpawnerRemaining(int32 Remaining)
{
	if (Remaining < 0)
	{
		// 无限模式：隐藏提示
		OnSpawnerRemainingChanged(0, false);
	}
	else
	{
		OnSpawnerRemainingChanged(Remaining, true);
	}
}

void UHUDWidget::HideSpawnerRemaining()
{
	OnSpawnerRemainingChanged(0, false);
}

void UHUDWidget::HandleReputationChanged(int32 NewValue, int32 Delta)
{
	// 触发蓝图飘字（招揽/完成订单/超时扣分 均会通过此路径）
	PlayReputationFloating(Delta);
	// 声誉变化同时影响声誉/金钱显示与关卡目标进度（若目标含声誉维度）
	RefreshGoal();
}

void UHUDWidget::HandleMoneyChanged(int32 NewValue, int32 Delta, EMoneySource Source)
{
	// 金钱变化同时影响声誉/金钱显示与关卡目标进度（若目标含金钱维度）
	RefreshGoal();
}

void UHUDWidget::HandleLevelScoreChanged(float Score)
{
	// 分数（含服务客人加分）变化时刷新目标进度
	RefreshGoal();
}

void UHUDWidget::HandleLevelGoalCompleted(int32 GuestsServed, int32 MoneyEarned, int32 Score, int32 Reputation)
{
	// 转发给蓝图，显示通关结算面板（本关最终统计）
	ShowLevelComplete(GuestsServed, MoneyEarned, Score, Reputation);
}

void UHUDWidget::HandleLevelClosing(float CountdownSeconds)
{
	// 转发给蓝图，显示打烊横幅与倒计时
	ShowClosingBanner(CountdownSeconds);
}
