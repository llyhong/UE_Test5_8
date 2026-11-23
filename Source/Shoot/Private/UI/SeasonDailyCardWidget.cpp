// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/SeasonDailyCardWidget.h"

#include "UI/StarSettlementData.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/BackgroundBlur.h"
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
#include "Engine/DataTable.h"
#include "Engine/LocalPlayer.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"
#include "Game/BaseGameInstance.h"
#include "Game/BaseLevelBP.h"
#include "Game/OrderSystem.h"
#include "Game/BusinessSimulation.h"
#include "Game/BusinessTime.h"
#include "GameFramework/PlayerController.h"
#include "Blueprint/GameViewportSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UObjectIterator.h"

namespace
{
	constexpr float CardFadeInSeconds = 0.25f;
	constexpr float CardHoldSeconds = 7.f;
	constexpr float CardFadeOutSeconds = 0.4f;
	constexpr float HudSuppressInterval = 0.25f;

	/** 星级演出时间轴：逐颗点亮 → 定性文案 → 理由逐条 → 收尾 */
	constexpr float RevealStarStart = 0.35f;
	constexpr float RevealStarStep = 0.18f;
	constexpr float RevealStarPop = 0.28f;
	constexpr float RevealPhraseAt = 1.30f;
	constexpr float RevealReasonStart = 1.52f;
	constexpr float RevealReasonStep = 0.16f;
	constexpr float RevealDoneAt = 2.45f;

	const TCHAR* StarTierTablePath = TEXT("/Game/_Demo/Data/DT_StarTiers.DT_StarTiers");
	const TCHAR* StarReasonTablePath = TEXT("/Game/_Demo/Data/DT_StarReasons.DT_StarReasons");

	TMap<TWeakObjectPtr<APlayerController>, TWeakObjectPtr<USeasonDailyCardWidget>> ActiveCards;
	TMap<TWeakObjectPtr<UUserWidget>, ESlateVisibility> SuppressedHudWidgets;

	void SuppressOtherHud(UWorld* World)
	{
		UGameViewportSubsystem* ViewportSubsystem = UGameViewportSubsystem::Get();
		if (!World || !ViewportSubsystem)
		{
			return;
		}
		for (TObjectIterator<UUserWidget> It; It; ++It)
		{
			UUserWidget* Widget = *It;
			if (!IsValid(Widget) || Widget->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject)
				|| Widget->IsA<USeasonDailyCardWidget>() || Widget->GetWorld() != World
				|| !ViewportSubsystem->IsWidgetAdded(Widget))
			{
				continue;
			}
			const ESlateVisibility Current = Widget->GetVisibility();
			if (Current == ESlateVisibility::Collapsed || Current == ESlateVisibility::Hidden)
			{
				continue;
			}
			if (!SuppressedHudWidgets.Contains(Widget))
			{
				SuppressedHudWidgets.Add(Widget, Current);
			}
			Widget->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	void RestoreOtherHud()
	{
		for (const TPair<TWeakObjectPtr<UUserWidget>, ESlateVisibility>& Pair : SuppressedHudWidgets)
		{
			UUserWidget* Widget = Pair.Key.Get();
			if (IsValid(Widget) && Widget->GetVisibility() == ESlateVisibility::Collapsed)
			{
				Widget->SetVisibility(Pair.Value);
			}
		}
		SuppressedHudWidgets.Reset();
	}

	bool AnyInteractiveCardShowing()
	{
		for (const TPair<TWeakObjectPtr<APlayerController>, TWeakObjectPtr<USeasonDailyCardWidget>>& Pair : ActiveCards)
		{
			const USeasonDailyCardWidget* Card = Pair.Value.Get();
			if (IsValid(Card) && Card->IsAwaitingNextDay())
			{
				return true;
			}
		}
		return false;
	}

	FLinearColor DailyCardColor(const TCHAR* Hex)
	{
		return FLinearColor::FromSRGBColor(FColor::FromHex(Hex));
	}

	FLinearColor ParseAccent(const FString& Hex, const TCHAR* Fallback)
	{
		const FString Clean = Hex.StartsWith(TEXT("#")) ? Hex.RightChop(1) : Hex;
		return Clean.Len() == 6 ? DailyCardColor(*Clean) : DailyCardColor(Fallback);
	}

	UBorder* Panel(UWidgetTree* Tree, FName Name, const TCHAR* Fill, const TCHAR* Rim, float Radius)
	{
		UBorder* Border = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), Name);
		Border->SetBrush(FSlateRoundedBoxBrush(DailyCardColor(Fill), Radius, DailyCardColor(Rim), 2.f));
		Border->SetPadding(FMargin(18.f, 12.f));
		Border->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		return Border;
	}

	UTextBlock* Text(UWidgetTree* Tree, FName Name, const FText& Value, int32 FontSize, const TCHAR* Ink)
	{
		UTextBlock* Block = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		Block->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), FontSize));
		Block->SetText(Value);
		Block->SetColorAndOpacity(FSlateColor(DailyCardColor(Ink)));
		Block->SetVisibility(ESlateVisibility::HitTestInvisible);
		return Block;
	}

	/** 把 {0} {1} 换成实际值 / 阈值 */
	FString FormatReason(const FString& Template, const FString& Actual, const FString& Target)
	{
		TArray<FStringFormatArg> Args;
		Args.Add(Actual);
		Args.Add(Target);
		return FString::Format(*Template, Args);
	}

	FString NumberText(float Value, bool bOneDecimal)
	{
		return bOneDecimal ? FString::Printf(TEXT("%.1f"), Value)
			: FText::AsNumber(FMath::RoundToInt(Value)).ToString();
	}

	/** 内置兜底档位（数据表缺失 / 缺行时仍能用） */
	struct FBuiltinTier
	{
		int32 Stars;
		int32 MinScore;
		const TCHAR* Name;
		const TCHAR* Phrase;
		const TCHAR* Accent;
	};

	const FBuiltinTier BuiltinTiers[] = {
		{5, 90, TEXT("完美"), TEXT("大丰收！"),      TEXT("F0B93F")},
		{4, 75, TEXT("出色"), TEXT("干得漂亮！"),    TEXT("527D32")},
		{3, 60, TEXT("合格"), TEXT("稳步前进"),      TEXT("28657F")},
		{2, 45, TEXT("勉强"), TEXT("还有提升空间"),  TEXT("A96B2D")},
		{1, 0,  TEXT("失利"), TEXT("明日再战"),      TEXT("A54351")},
	};

	/** 按评级分找星级：优先数据表，缺表则用内置阈值 */
	int32 ResolveStarCount(const USeasonSystem* Season)
	{
		const int32 Score = Season ? Season->LastGradeScore : 0;
		if (UDataTable* Table = LoadObject<UDataTable>(nullptr, StarTierTablePath))
		{
			int32 BestStars = 0;
			int32 BestMin = MIN_int32;
			for (const FName& RowName : Table->GetRowNames())
			{
				const FStarTierRow* Row = Table->FindRow<FStarTierRow>(RowName, TEXT("StarTier"));
				if (!Row || Row->Stars < 1 || Row->Stars > 5 || Row->MinScore > Score)
				{
					continue;
				}
				if (Row->MinScore > BestMin)
				{
					BestMin = Row->MinScore;
					BestStars = Row->Stars;
				}
			}
			if (BestStars >= 1)
			{
				return BestStars;
			}
		}
		for (const FBuiltinTier& Tier : BuiltinTiers)
		{
			if (Score >= Tier.MinScore)
			{
				return Tier.Stars;
			}
		}
		return 1;
	}

	/** 取某档星的配置：数据表优先，缺行回退内置 */
	void ResolveTier(int32 Stars, int32 Score, FString& OutName, FString& OutPhrase,
		FString& OutGap, FLinearColor& OutAccent)
	{
		OutName = TEXT("");
		OutPhrase = TEXT("");
		OutGap = TEXT("");
		OutAccent = DailyCardColor(TEXT("A76513"));

		FStarTierRow* Matched = nullptr;
		int32 NextMin = MAX_int32;
		FString NextName;
		if (UDataTable* Table = LoadObject<UDataTable>(nullptr, StarTierTablePath))
		{
			for (const FName& RowName : Table->GetRowNames())
			{
				FStarTierRow* Row = Table->FindRow<FStarTierRow>(RowName, TEXT("StarTier"));
				if (!Row)
				{
					continue;
				}
				if (Row->Stars == Stars)
				{
					Matched = Row;
				}
				else if (Row->Stars == Stars + 1)
				{
					NextMin = Row->MinScore;
					NextName = Row->TierName;
				}
			}
		}

		if (Matched)
		{
			OutName = Matched->TierName;
			OutPhrase = Matched->Phrase;
			OutAccent = ParseAccent(Matched->AccentHex, TEXT("F0B93F"));
			if (Stars >= 5)
			{
				OutGap = Matched->MaxPhrase;
			}
			else if (NextMin != MAX_int32)
			{
				TArray<FStringFormatArg> Args;
				Args.Add(FString::FromInt(FMath::Max(0, NextMin - Score)));
				Args.Add(FString::FromInt(Stars + 1));
				Args.Add(NextName);
				OutGap = FString::Format(*Matched->GapFormat, Args);
			}
		}
		else
		{
			for (const FBuiltinTier& Tier : BuiltinTiers)
			{
				if (Tier.Stars == Stars)
				{
					OutName = Tier.Name;
					OutPhrase = Tier.Phrase;
					OutAccent = DailyCardColor(Tier.Accent);
					break;
				}
			}
			if (Stars >= 5)
			{
				OutGap = TEXT("已是最高评价，保持住！");
			}
			else
			{
				const FBuiltinTier* Next = nullptr;
				for (const FBuiltinTier& Tier : BuiltinTiers)
				{
					if (Tier.Stars == Stars + 1)
					{
						Next = &Tier;
						break;
					}
				}
				if (Next)
				{
					TArray<FStringFormatArg> Args;
					Args.Add(FString::FromInt(FMath::Max(0, Next->MinScore - Score)));
					Args.Add(FString::FromInt(Stars + 1));
					Args.Add(FString(Next->Name));
					OutGap = FString::Format(TEXT("再得 {0} 分升到 {1} 星 · {2}"), Args);
				}
			}
		}
	}

	struct FReasonLine
	{
		FString Text;
		bool bPassed = false;
	};

	/** 生成最多 3 条理由（数据表优先，缺表用内置三条） */
	TArray<FReasonLine> ResolveReasons(const USeasonSystem* Season, int32 Revenue, int32 Profit)
	{
		TArray<FReasonLine> Result;
		UDataTable* Table = LoadObject<UDataTable>(nullptr, StarReasonTablePath);
		TArray<FStarReasonRow> Rows;
		if (Table)
		{
			for (const FName& RowName : Table->GetRowNames())
			{
				if (const FStarReasonRow* Row = Table->FindRow<FStarReasonRow>(RowName, TEXT("StarReason")))
				{
					Rows.Add(*Row);
				}
			}
			Rows.Sort([](const FStarReasonRow& A, const FStarReasonRow& B) { return A.SortOrder < B.SortOrder; });
		}

		if (Rows.Num() > 0)
		{
			for (const FStarReasonRow& Row : Rows)
			{
				if (Result.Num() >= 3)
				{
					break;
				}
				float Actual = 0.f;
				switch (Row.Stat)
				{
				case EStarReasonStat::Served:      Actual = Season ? Season->LastServed : 0; break;
				case EStarReasonStat::Failed:      Actual = Season ? Season->LastFailed : 0; break;
				case EStarReasonStat::Timeouts:    Actual = Season ? Season->LastTimeouts : 0; break;
				case EStarReasonStat::AvgWait:     Actual = Season ? Season->LastAvgWait : 0.f; break;
				case EStarReasonStat::WildServed:  Actual = Season ? Season->LastWildServed : 0; break;
				case EStarReasonStat::Profit:      Actual = static_cast<float>(Profit); break;
				case EStarReasonStat::Revenue:     Actual = static_cast<float>(Revenue); break;
				case EStarReasonStat::Reputation:  Actual = Season ? Season->LastReputation : 0; break;
				}
				const bool bPassed = Row.Compare == EStarReasonCompare::AtLeast
					? Actual >= Row.Threshold : Actual <= Row.Threshold;
				// 勾选“只在达标时显示”的行，未达标时跳过；默认 false = 达标/未达标都显示
				// （PassText / FailText 各自带 ✓ / ○ 图标，一眼看出哪项拖后腿）
				if (Row.bOnlyShowWhenPassed && !bPassed)
				{
					continue;
				}
				const bool bDecimal = Row.Stat == EStarReasonStat::AvgWait;
				const FString ActualText = NumberText(Actual, bDecimal);
				const FString TargetText = NumberText(Row.Threshold, bDecimal);
				const FString Template = bPassed ? Row.PassText : Row.FailText;
				if (Template.IsEmpty())
				{
					continue;
				}
				FReasonLine Line;
				Line.bPassed = bPassed;
				Line.Text = FormatReason(Template, ActualText, TargetText);
				Result.Add(Line);
			}
			return Result;
		}

		// 内置兜底：服务 / 超时 / 平均等待
		if (Season)
		{
			FReasonLine A;
			A.bPassed = Season->LastServed >= 5;
			A.Text = A.bPassed
				? FString::Printf(TEXT("✓ 服务 %d 位客人，招待到位"), Season->LastServed)
				: FString::Printf(TEXT("○ 仅服务 %d 位客人（目标 5）"), Season->LastServed);
			Result.Add(A);

			FReasonLine B;
			B.bPassed = Season->LastTimeouts == 0;
			B.Text = B.bPassed ? TEXT("✓ 无超时订单")
				: FString::Printf(TEXT("○ 超时 %d 单，拖了后腿"), Season->LastTimeouts);
			Result.Add(B);

			FReasonLine C;
			C.bPassed = Season->LastAvgWait <= 25.f && Season->LastServed > 0;
			C.Text = C.bPassed
				? FString::Printf(TEXT("✓ 平均等待 %.1fs，节奏不错"), Season->LastAvgWait)
				: FString::Printf(TEXT("○ 平均等待 %.1fs，客人等得有点久"), Season->LastAvgWait);
			Result.Add(C);
		}
		return Result;
	}
}

USeasonDailyCardWidget::USeasonDailyCardWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	static ConstructorHelpers::FObjectFinder<USoundBase> TickFinder(TEXT("/Game/StackOBot/Audio/SFX/Waves/SFX_Coin_Collect.SFX_Coin_Collect"));
	static ConstructorHelpers::FObjectFinder<USoundBase> WinFinder(TEXT("/Game/Audio/MUSIC/Waves/MUS_Crop_Win_Long_All.MUS_Crop_Win_Long_All"));
	RowTickSound = TickFinder.Object;
	WinSound = WinFinder.Object;
}

void USeasonDailyCardWidget::ShowForPlayer(APlayerController* PC, const USeasonSystem* Season, bool bAwaitNextDay)
{
	if (!PC || !PC->IsLocalController() || !Season)
	{
		return;
	}

	USeasonDailyCardWidget* Card = nullptr;
	if (const TWeakObjectPtr<USeasonDailyCardWidget>* Found = ActiveCards.Find(TWeakObjectPtr<APlayerController>(PC)))
	{
		Card = Found->Get();
	}
	if (!IsValid(Card))
	{
		Card = CreateWidget<USeasonDailyCardWidget>(PC, USeasonDailyCardWidget::StaticClass());
		if (!Card)
		{
			return;
		}
		Card->OwnerController = PC;
		ActiveCards.Add(TWeakObjectPtr<APlayerController>(PC), Card);
	}
	if (!Card->IsInViewport() && !Card->AddToPlayerScreen(static_cast<int32>(EWidgetZOrder::SeasonCard)))
	{
		ActiveCards.Remove(TWeakObjectPtr<APlayerController>(PC));
		return;
	}

	Card->bInteractive = bAwaitNextDay;
	Card->bRevealFinished = false;
	Card->bRevealActive = false;
	Card->Apply(Season);
	Card->Elapsed = 0.f;
	Card->HudSuppressAccum = 0.f;
	Card->bShowing = true;

	Card->SetVisibility(bAwaitNextDay ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::HitTestInvisible);
	if (Card->NextDayButton)
	{
		Card->NextDayButton->SetVisibility(bAwaitNextDay ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (Card->CardBorder)
	{
		Card->CardBorder->SetVisibility(bAwaitNextDay ? ESlateVisibility::Visible : ESlateVisibility::SelfHitTestInvisible);
		Card->ApplyLayout(bAwaitNextDay);
		Card->CardBorder->SetRenderOpacity(0.f);
		Card->CardBorder->SetRenderTranslation(Card->GetSlideOffset(0.f));
	}
	if (bAwaitNextDay)
	{
		SuppressOtherHud(PC->GetWorld());
	}
	Card->SetUIInputEnabled(bAwaitNextDay);
}

void USeasonDailyCardWidget::ApplyLayout(bool bCentered)
{
	if (!CardBorder)
	{
		return;
	}
	if (UCanvasPanelSlot* CardSlot = Cast<UCanvasPanelSlot>(CardBorder->Slot))
	{
		if (bCentered)
		{
			CardSlot->SetAnchors(FAnchors(0.5f, 0.5f));
			CardSlot->SetAlignment(FVector2D(0.5f, 0.5f));
			CardSlot->SetPosition(FVector2D::ZeroVector);
		}
		else
		{
			CardSlot->SetAnchors(FAnchors(1.f, 0.5f));
			CardSlot->SetAlignment(FVector2D(1.f, 0.5f));
			CardSlot->SetPosition(FVector2D(-40.f, 60.f));
		}
		CardSlot->SetAutoSize(true);
	}

	const float Width = bCentered ? 760.f : 460.f;
	if (USizeBox* CardWidth = Cast<USizeBox>(WidgetTree->FindWidget(TEXT("CardWidth"))))
	{
		CardWidth->SetWidthOverride(Width);
	}
	for (UTextBlock* Block : {DayText, PhraseText, GapText, RevenueText, ProfitText})
	{
		if (Block)
		{
			Block->SetWrapTextAt(Width - 96.f);
		}
	}
	for (UTextBlock* Reason : ReasonTexts)
	{
		if (Reason)
		{
			Reason->SetWrapTextAt(Width - 120.f);
		}
	}
	for (UTextBlock* Star : StarTexts)
	{
		if (Star)
		{
			Star->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), bCentered ? 54 : 40));
		}
	}
	if (UWidget* Backdrop = WidgetTree->FindWidget(TEXT("BackdropBlur")))
	{
		Backdrop->SetVisibility(bCentered ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}

	ForceLayoutPrepass();
	FVector2D ViewportSize = GetCachedGeometry().GetLocalSize();
	if (ViewportSize.X <= 1.f || ViewportSize.Y <= 1.f)
	{
		ViewportSize = UWidgetLayoutLibrary::GetViewportSize(this) / UWidgetLayoutLibrary::GetViewportScale(this);
		if (const APlayerController* PC = OwnerController.Get())
		{
			if (const ULocalPlayer* Player = PC->GetLocalPlayer())
			{
				ViewportSize *= FVector2D(Player->Size);
			}
		}
	}
	LastLayoutSize = ViewportSize;
	const FVector2D CardSize = CardBorder->GetDesiredSize();
	const double Scale = bCentered
		? FMath::Min(ViewportSize.X * 0.86 / FMath::Max(1.0, CardSize.X), ViewportSize.Y * 0.90 / FMath::Max(1.0, CardSize.Y))
		: FMath::Min3(1.0, FMath::Max(1.0, ViewportSize.X - 80.0) / FMath::Max(1.0, CardSize.X),
			FMath::Max(1.0, ViewportSize.Y - 160.0) / FMath::Max(1.0, CardSize.Y));
	CardBorder->SetRenderTransformPivot(bCentered ? FVector2D(0.5f, 0.5f) : FVector2D(1.f, 0.5f));
	CardBorder->SetRenderScale(FVector2D(Scale));
}

FVector2D USeasonDailyCardWidget::GetSlideOffset(float Alpha) const
{
	const float Offset = (1.f - FMath::Clamp(Alpha, 0.f, 1.f)) * 36.f;
	return bInteractive ? FVector2D(0.f, Offset) : FVector2D(Offset, 0.f);
}

void USeasonDailyCardWidget::DismissAll()
{
	for (const TPair<TWeakObjectPtr<APlayerController>, TWeakObjectPtr<USeasonDailyCardWidget>>& Pair : ActiveCards)
	{
		if (USeasonDailyCardWidget* Card = Pair.Value.Get())
		{
			Card->Dismiss();
		}
	}
	RestoreOtherHud();
}

void USeasonDailyCardWidget::Dismiss()
{
	bShowing = false;
	bInteractive = false;
	bRevealActive = false;
	bRevealFinished = false;
	Elapsed = 0.f;
	SetUIInputEnabled(false);
	SetVisibility(ESlateVisibility::HitTestInvisible);
	if (CardBorder)
	{
		CardBorder->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (UWidget* Backdrop = WidgetTree->FindWidget(TEXT("BackdropBlur")))
	{
		Backdrop->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void USeasonDailyCardWidget::HandleNextDayClicked()
{
	if (!IsAwaitingNextDay())
	{
		return;
	}
	if (bRevealActive)
	{
		FinishReveal();
		return;
	}
	if (APlayerController* PC = OwnerController.Get())
	{
		if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(PC->GetGameInstance()))
		{
			if (GI->businessSimulation && GI->businessSimulation->TimeSystem)
			{
				GI->businessSimulation->TimeSystem->BeginNextBusinessDay();
			}
		}
	}
	DismissAll();
}

void USeasonDailyCardWidget::SetUIInputEnabled(bool bEnabled)
{
	APlayerController* PC = OwnerController.Get();
	if (!PC)
	{
		return;
	}
	if (bEnabled && NextDayButton)
	{
		FInputModeUIOnly Mode;
		Mode.SetWidgetToFocus(NextDayButton->TakeWidget());
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(Mode);
		NextDayButton->SetUserFocus(PC);
		PC->bShowMouseCursor = true;
	}
	else
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->bShowMouseCursor = false;
		if (const ULocalPlayer* Player = PC->GetLocalPlayer())
		{
			FSlateApplication::Get().SetUserFocusToGameViewport(Player->GetControllerId());
		}
	}
}

FReply USeasonDailyCardWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (!IsAwaitingNextDay())
	{
		return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
	}
	FSlateApplication& Slate = FSlateApplication::Get();
	const EUINavigationAction Action = Slate.GetNavigationActionFromKey(InKeyEvent);
	if (Action == EUINavigationAction::Accept || InKeyEvent.GetKey() == EKeys::SpaceBar)
	{
		if (!InKeyEvent.IsRepeat())
		{
			if (bRevealActive)
			{
				FinishReveal();
			}
			else
			{
				HandleNextDayClicked();
			}
		}
		return FReply::Handled();
	}
	if (Action == EUINavigationAction::Back || InKeyEvent.GetKey() == EKeys::BackSpace
		|| Slate.GetNavigationDirectionFromKey(InKeyEvent) != EUINavigation::Invalid)
	{
		if (NextDayButton)
		{
			NextDayButton->SetUserFocus(OwnerController.Get());
		}
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

FReply USeasonDailyCardWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (IsAwaitingNextDay())
	{
		if (bRevealActive)
		{
			FinishReveal();
		}
		if (NextDayButton)
		{
			return FReply::Handled().SetUserFocus(NextDayButton->TakeWidget(), EFocusCause::Mouse);
		}
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

TSharedRef<SWidget> USeasonDailyCardWidget::RebuildWidget()
{
	if (WidgetTree->RootWidget)
	{
		return Super::RebuildWidget();
	}

	SetIsFocusable(true);
	SetVisibility(ESlateVisibility::HitTestInvisible);

	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;

	UBackgroundBlur* BackdropBlur = WidgetTree->ConstructWidget<UBackgroundBlur>(UBackgroundBlur::StaticClass(), TEXT("BackdropBlur"));
	BackdropBlur->SetBlurStrength(12.f);
	BackdropBlur->SetApplyAlphaToBlur(false);
	BackdropBlur->SetPadding(FMargin(0.f));
	BackdropBlur->SetHorizontalAlignment(HAlign_Fill);
	BackdropBlur->SetVerticalAlignment(VAlign_Fill);
	BackdropBlur->SetVisibility(ESlateVisibility::Collapsed);
	UCanvasPanelSlot* BlurSlot = Root->AddChildToCanvas(BackdropBlur);
	BlurSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
	BlurSlot->SetOffsets(FMargin(0.f));

	CardBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Card"));
	CardBorder->SetPadding(FMargin(9.f));
	CardBrush = FSlateRoundedBoxBrush(DailyCardColor(TEXT("FFF9E7")), 29.f, DailyCardColor(TEXT("D4AA60")), 3.f);
	CardBorder->SetBrush(CardBrush);
	UCanvasPanelSlot* CardSlot = Root->AddChildToCanvas(CardBorder);
	CardSlot->SetAnchors(FAnchors(0.5f, 0.5f));
	CardSlot->SetAlignment(FVector2D(0.5f, 0.5f));
	CardSlot->SetPosition(FVector2D::ZeroVector);
	CardSlot->SetAutoSize(true);

	USizeBox* CardWidth = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("CardWidth"));
	CardWidth->SetWidthOverride(760.f);
	CardBorder->SetContent(CardWidth);
	UBorder* Inset = Panel(WidgetTree, TEXT("CardInset"), TEXT("FFF6E5"), TEXT("E4C68A"), 22.f);
	Inset->SetPadding(FMargin(26.f, 20.f));
	CardWidth->SetContent(Inset);
	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
	Inset->SetContent(Column);

	// 标题
	UHorizontalBox* TitleRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	Column->AddChildToVerticalBox(TitleRow)->SetHorizontalAlignment(HAlign_Center);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		if (Index == 1)
		{
			UTextBlock* Title = Text(WidgetTree, TEXT("Title"), FText::FromString(TEXT("今日评价")), 40, TEXT("75512C"));
			FSlateFontInfo Font = Title->GetFont();
			Font.OutlineSettings.OutlineSize = 2;
			Font.OutlineSettings.OutlineColor = DailyCardColor(TEXT("FFF9E9"));
			Title->SetFont(Font);
			Title->SetShadowOffset(FVector2D(0.f, 2.f));
			Title->SetShadowColorAndOpacity(DailyCardColor(TEXT("E8C79C")));
			TitleRow->AddChildToHorizontalBox(Title)->SetPadding(FMargin(12.f, 0.f));
		}
		else
		{
			UImage* Leaf = WidgetTree->ConstructWidget<UImage>();
			FSlateRoundedBoxBrush LeafBrush(DailyCardColor(TEXT("A4BD49")), FVector4(2.f, 16.f, 2.f, 16.f));
			LeafBrush.ImageSize = FVector2D(15.f, 27.f);
			Leaf->SetBrush(LeafBrush);
			Leaf->SetRenderTransformAngle(Index == 0 ? -35.f : 35.f);
			Leaf->SetVisibility(ESlateVisibility::HitTestInvisible);
			TitleRow->AddChildToHorizontalBox(Leaf)->SetVerticalAlignment(VAlign_Center);
		}
	}

	DayText = Text(WidgetTree, TEXT("Day"), FText(), 20, TEXT("966C43"));
	DayText->SetJustification(ETextJustify::Center);
	Column->AddChildToVerticalBox(DayText)->SetPadding(FMargin(0.f, 6.f, 0.f, 6.f));

	// 五颗星
	UHorizontalBox* StarRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("StarRow"));
	UVerticalBoxSlot* StarRowSlot = Column->AddChildToVerticalBox(StarRow);
	StarRowSlot->SetPadding(FMargin(0.f, 2.f, 0.f, 8.f));
	StarRowSlot->SetHorizontalAlignment(HAlign_Center);
	StarTexts.Reset();
	for (int32 Index = 0; Index < 5; ++Index)
	{
		UTextBlock* Star = Text(WidgetTree, *FString::Printf(TEXT("Star%d"), Index),
			FText::FromString(TEXT("☆")), 54, TEXT("D9CDB4"));
		Star->SetJustification(ETextJustify::Center);
		Star->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
		UHorizontalBoxSlot* StarSlot = StarRow->AddChildToHorizontalBox(Star);
		StarSlot->SetPadding(FMargin(Index == 0 ? 0.f : 4.f, 0.f, Index == 4 ? 0.f : 4.f, 0.f));
		StarTexts.Add(Star);
	}

	// 定性文案 + 差距提示
	PhraseText = Text(WidgetTree, TEXT("Phrase"), FText(), 34, TEXT("A76513"));
	PhraseText->SetJustification(ETextJustify::Center);
	PhraseText->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	Column->AddChildToVerticalBox(PhraseText)->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));

	GapText = Text(WidgetTree, TEXT("Gap"), FText(), 18, TEXT("966C43"));
	GapText->SetJustification(ETextJustify::Center);
	Column->AddChildToVerticalBox(GapText)->SetPadding(FMargin(0.f, 0.f, 0.f, 14.f));

	// 只保留营收 / 利润两个数（Widget 名保持兼容）
	UHorizontalBox* Metrics = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("FinancialHeader"));
	Column->AddChildToVerticalBox(Metrics)->SetPadding(FMargin(0.f, 0.f, 0.f, 14.f));
	const TCHAR* MetricLabels[] = {TEXT("今日营收"), TEXT("营业利润")};
	const TCHAR* MetricFills[] = {TEXT("E8F0D8"), TEXT("E5F0E7")};
	const TCHAR* MetricRims[] = {TEXT("ADBF83"), TEXT("A9BEA1")};
	UTextBlock** MetricValues[] = {&RevenueText, &ProfitText};
	for (int32 Index = 0; Index < 2; ++Index)
	{
		UBorder* Tile = Panel(WidgetTree, *FString::Printf(TEXT("Metric%d"), Index), MetricFills[Index], MetricRims[Index], 18.f);
		Tile->SetPadding(FMargin(20.f, 12.f));
		UHorizontalBoxSlot* TileSlot = Metrics->AddChildToHorizontalBox(Tile);
		TileSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		TileSlot->SetPadding(FMargin(Index == 0 ? 0.f : 8.f, 0.f, Index == 1 ? 0.f : 8.f, 0.f));
		UVerticalBox* TileColumn = WidgetTree->ConstructWidget<UVerticalBox>();
		Tile->SetContent(TileColumn);
		TileColumn->AddChildToVerticalBox(Text(WidgetTree, NAME_None, FText::FromString(MetricLabels[Index]), 20, TEXT("75512C")))
			->SetHorizontalAlignment(HAlign_Center);
		*MetricValues[Index] = Text(WidgetTree, *FString::Printf(TEXT("MetricValue%d"), Index == 0 ? 0 : 2),
			FText(), 38, TEXT("527D32"));
		(*MetricValues[Index])->SetJustification(ETextJustify::Center);
		TileColumn->AddChildToVerticalBox(*MetricValues[Index])->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
	}

	// 理由列表
	UVerticalBox* ReasonBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ReasonBox"));
	Column->AddChildToVerticalBox(ReasonBox)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
	ReasonTexts.Reset();
	for (int32 Index = 0; Index < 3; ++Index)
	{
		UTextBlock* Reason = Text(WidgetTree, *FString::Printf(TEXT("Reason%d"), Index), FText(), 20, TEXT("75512C"));
		Reason->SetRenderTransformPivot(FVector2D(0.f, 0.5f));
		ReasonBox->AddChildToVerticalBox(Reason)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
		ReasonTexts.Add(Reason);
	}

	// 进入下一天
	NextDayButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("NextDayButton"));
	FButtonStyle ButtonStyle;
	ButtonStyle.SetNormal(FSlateRoundedBoxBrush(DailyCardColor(TEXT("F6C74E")), 24.f, DailyCardColor(TEXT("FFF0AC")), 3.f));
	ButtonStyle.SetHovered(FSlateRoundedBoxBrush(DailyCardColor(TEXT("FFDD75")), 24.f, DailyCardColor(TEXT("FFF8DC")), 4.f));
	ButtonStyle.SetPressed(FSlateRoundedBoxBrush(DailyCardColor(TEXT("E5B947")), 24.f, DailyCardColor(TEXT("FFF0AC")), 3.f));
	ButtonStyle.SetNormalPadding(FMargin(8.f));
	ButtonStyle.SetPressedPadding(FMargin(8.f, 10.f, 8.f, 6.f));
	NextDayButton->SetStyle(ButtonStyle);
	for (const EUINavigation Direction : {EUINavigation::Left, EUINavigation::Right, EUINavigation::Up,
		EUINavigation::Down, EUINavigation::Next, EUINavigation::Previous})
	{
		NextDayButton->SetNavigationRuleBase(Direction, EUINavigationRule::Stop);
	}
	NextDayButton->SetVisibility(ESlateVisibility::Collapsed);
	NextDayButton->OnClicked.AddDynamic(this, &USeasonDailyCardWidget::HandleNextDayClicked);
	UBorder* ButtonInset = Panel(WidgetTree, TEXT("ButtonInset"), TEXT("FFDD75"), TEXT("FFEAA2"), 17.f);
	ButtonInset->SetPadding(FMargin(14.f, 9.f));
	ButtonInset->SetHorizontalAlignment(HAlign_Center);
	ButtonInset->SetVerticalAlignment(VAlign_Center);
	NextDayButtonText = Text(WidgetTree, TEXT("NextDayLabel"), FText::FromString(TEXT("进入下一天  ·  A / 回车 / 空格")), 23, TEXT("8D5817"));
	NextDayButtonText->SetJustification(ETextJustify::Center);
	ButtonInset->SetContent(NextDayButtonText);
	NextDayButton->SetContent(ButtonInset);
	Column->AddChildToVerticalBox(NextDayButton)->SetPadding(FMargin(0.f, 12.f, 0.f, 0.f));

	CardBorder->SetVisibility(ESlateVisibility::Collapsed);
	return Super::RebuildWidget();
}

void USeasonDailyCardWidget::Apply(const USeasonSystem* Season)
{
	if (!Season || !CardBorder)
	{
		return;
	}

	const int32 StarCount = ResolveStarCount(Season);
	FString TierName, Phrase, Gap;
	FLinearColor Accent;
	ResolveTier(StarCount, Season->LastGradeScore, TierName, Phrase, Gap, Accent);

	const FDailyFinancialRecord* Record = Season->DailyFinancialHistory.IsEmpty() ? nullptr : &Season->DailyFinancialHistory.Last();
	const int32 ReportDay = Record ? Record->Day : Season->SeasonDay;
	const int32 Revenue = Record ? Record->Revenue : 0;
	const int32 Profit = Record ? Record->Revenue - Record->OperatingExpense : Season->LastDailyNet;
	const FLinearColor ProfitInk = DailyCardColor(Profit >= 0 ? TEXT("527D32") : TEXT("A54351"));

	if (DayText)
	{
		DayText->SetText(FText::FromString(FString::Printf(TEXT("第 %d / %d 天    ·    第 %d 幕"),
			ReportDay, Season->Config.SeasonDays,
			(ReportDay - 1) / FMath::Max(1, Season->Config.DaysPerAct) + 1)));
	}
	if (PhraseText)
	{
		PhraseText->SetText(FText::FromString(TierName.IsEmpty() ? Phrase : TierName + TEXT(" · ") + Phrase));
		PhraseText->SetColorAndOpacity(FSlateColor(Accent));
	}
	if (GapText)
	{
		GapText->SetText(FText::FromString(Gap));
	}
	if (RevenueText)
	{
		RevenueText->SetText(Record ? FText::AsNumber(Revenue) : FText::FromString(TEXT("暂无记录")));
		RevenueText->SetColorAndOpacity(FSlateColor(DailyCardColor(TEXT("527D32"))));
	}
	if (ProfitText)
	{
		ProfitText->SetText(FText::FromString(Record
			? (Profit > 0 ? FString(TEXT("+")) + FText::AsNumber(Profit).ToString() : FText::AsNumber(Profit).ToString())
			: (Profit > 0 ? FString(TEXT("+")) + FText::AsNumber(Profit).ToString() : FText::AsNumber(Profit).ToString())));
		ProfitText->SetColorAndOpacity(FSlateColor(ProfitInk));
	}

	// 理由与星星的最终值先算好，演出时逐条亮出；数字保持静态（测试与阅读都稳定）
	RevealSeason = const_cast<USeasonSystem*>(Season);
	RevealStarCount = StarCount;

	if (bInteractive && !bRevealFinished)
	{
		StartReveal(Season);
	}
	else
	{
		ApplyFinalVisual(Season);
	}
}

void USeasonDailyCardWidget::ApplyFinalVisual(const USeasonSystem* Season)
{
	if (!Season)
	{
		return;
	}
	FString TierName, Phrase, Gap;
	FLinearColor Accent;
	ResolveTier(RevealStarCount, Season->LastGradeScore, TierName, Phrase, Gap, Accent);

	for (int32 Index = 0; Index < StarTexts.Num(); ++Index)
	{
		if (UTextBlock* Star = StarTexts[Index].Get())
		{
			Star->SetText(FText::FromString(Index < RevealStarCount ? TEXT("★") : TEXT("☆")));
			Star->SetColorAndOpacity(FSlateColor(Index < RevealStarCount ? Accent : DailyCardColor(TEXT("D9CDB4"))));
			Star->SetRenderScale(FVector2D(1.f));
		}
	}
	if (PhraseText)
	{
		PhraseText->SetText(FText::FromString(TierName.IsEmpty() ? Phrase : TierName + TEXT(" · ") + Phrase));
		PhraseText->SetColorAndOpacity(FSlateColor(Accent));
		PhraseText->SetRenderScale(FVector2D(1.f));
	}
	if (GapText)
	{
		GapText->SetText(FText::FromString(Gap));
	}

	const FDailyFinancialRecord* Record = Season->DailyFinancialHistory.IsEmpty() ? nullptr : &Season->DailyFinancialHistory.Last();
	const int32 Revenue = Record ? Record->Revenue : 0;
	const int32 Profit = Record ? Record->Revenue - Record->OperatingExpense : Season->LastDailyNet;
	const TArray<FReasonLine> Lines = ResolveReasons(Season, Revenue, Profit);
	for (int32 Index = 0; Index < ReasonTexts.Num(); ++Index)
	{
		if (UTextBlock* Reason = ReasonTexts[Index].Get())
		{
			if (Index < Lines.Num())
			{
				Reason->SetText(FText::FromString(Lines[Index].Text));
				Reason->SetColorAndOpacity(FSlateColor(DailyCardColor(Lines[Index].bPassed ? TEXT("527D32") : TEXT("A54351"))));
				Reason->SetVisibility(ESlateVisibility::HitTestInvisible);
				Reason->SetRenderOpacity(1.f);
				Reason->SetRenderTranslation(FVector2D::ZeroVector);
			}
			else
			{
				Reason->SetText(FText::GetEmpty());
				Reason->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
	}
	if (PhraseText)
	{
		PhraseText->SetRenderOpacity(1.f);
	}
	if (GapText)
	{
		GapText->SetRenderOpacity(1.f);
	}
}

void USeasonDailyCardWidget::PlayRevealCue(USoundBase* Sound, float Volume, float Pitch) const
{
	if (Sound)
	{
		UGameplayStatics::PlaySound2D(this, Sound, Volume, Pitch);
	}
}

void USeasonDailyCardWidget::StartReveal(const USeasonSystem* Season)
{
	if (!Season)
	{
		return;
	}
	bRevealActive = true;
	RevealTime = 0.f;
	RevealStarsShown = 0;
	RevealReasonsShown = 0;

	for (UTextBlock* Star : StarTexts)
	{
		if (Star)
		{
			Star->SetText(FText::FromString(TEXT("☆")));
			Star->SetColorAndOpacity(FSlateColor(DailyCardColor(TEXT("D9CDB4"))));
			Star->SetRenderScale(FVector2D(1.f));
		}
	}
	if (PhraseText)
	{
		PhraseText->SetText(FText::FromString(TEXT("结算中…")));
		PhraseText->SetRenderOpacity(0.f);
	}
	if (GapText)
	{
		GapText->SetText(FText::GetEmpty());
		GapText->SetRenderOpacity(0.f);
	}
	for (UTextBlock* Reason : ReasonTexts)
	{
		if (Reason)
		{
			Reason->SetText(FText::GetEmpty());
			Reason->SetRenderOpacity(0.f);
			Reason->SetRenderTranslation(FVector2D(18.f, 0.f));
			Reason->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
	}
}

void USeasonDailyCardWidget::UpdateReveal(float DeltaTime)
{
	if (!bRevealActive)
	{
		return;
	}
	RevealTime += DeltaTime;

	// 星星逐颗点亮 + 弹一下
	for (int32 Index = RevealStarsShown; Index < RevealStarCount; ++Index)
	{
		const float Start = RevealStarStart + Index * RevealStarStep;
		if (RevealTime < Start || Index >= StarTexts.Num())
		{
			break;
		}
		UTextBlock* Star = StarTexts[Index].Get();
		if (Star)
		{
			Star->SetText(FText::FromString(TEXT("★")));
			Star->SetColorAndOpacity(FSlateColor(DailyCardColor(TEXT("F0B93F"))));
			PlayRevealCue(RowTickSound, 0.42f, 0.94f + 0.06f * Index);
		}
		++RevealStarsShown;
	}
	if (RevealStarsShown > 0 && RevealStarsShown <= StarTexts.Num())
	{
		const float PopT = FMath::Clamp((RevealTime - RevealStarStart - (RevealStarsShown - 1) * RevealStarStep) / RevealStarPop, 0.f, 1.f);
		if (UTextBlock* Star = StarTexts[RevealStarsShown - 1].Get())
		{
			Star->SetRenderScale(FVector2D(1.f + 0.55f * FMath::Pow(1.f - PopT, 2.f)));
		}
	}

	const USeasonSystem* Season = RevealSeason.Get();
	if (!Season)
	{
		FinishReveal();
		return;
	}

	// 定性文案 + 差距提示
	if (RevealTime >= RevealPhraseAt)
	{
		FString TierName, Phrase, Gap;
		FLinearColor Accent;
		ResolveTier(RevealStarCount, Season->LastGradeScore, TierName, Phrase, Gap, Accent);
		if (PhraseText && PhraseText->GetText().ToString() == TEXT("结算中…"))
		{
			PhraseText->SetText(FText::FromString(TierName.IsEmpty() ? Phrase : TierName + TEXT(" · ") + Phrase));
			PhraseText->SetColorAndOpacity(FSlateColor(Accent));
			PlayRevealCue(RowTickSound, 0.6f, 1.2f);
		}
		if (PhraseText)
		{
			PhraseText->SetRenderOpacity(FMath::Clamp((RevealTime - RevealPhraseAt) / 0.35f, 0.f, 1.f));
		}
		if (GapText)
		{
			GapText->SetText(FText::FromString(Gap));
			GapText->SetRenderOpacity(FMath::Clamp((RevealTime - RevealPhraseAt - 0.1f) / 0.35f, 0.f, 1.f));
		}
	}

	// 理由逐条亮出
	const FDailyFinancialRecord* Record = Season->DailyFinancialHistory.IsEmpty() ? nullptr : &Season->DailyFinancialHistory.Last();
	const int32 Revenue = Record ? Record->Revenue : 0;
	const int32 Profit = Record ? Record->Revenue - Record->OperatingExpense : Season->LastDailyNet;
	const TArray<FReasonLine> Lines = ResolveReasons(Season, Revenue, Profit);
	for (int32 Index = RevealReasonsShown; Index < Lines.Num() && Index < ReasonTexts.Num(); ++Index)
	{
		const float Start = RevealReasonStart + Index * RevealReasonStep;
		if (RevealTime < Start)
		{
			break;
		}
		if (UTextBlock* Reason = ReasonTexts[Index].Get())
		{
			Reason->SetText(FText::FromString(Lines[Index].Text));
			Reason->SetColorAndOpacity(FSlateColor(DailyCardColor(Lines[Index].bPassed ? TEXT("527D32") : TEXT("A54351"))));
			PlayRevealCue(RowTickSound, 0.3f, 1.05f + 0.05f * Index);
		}
		++RevealReasonsShown;
	}
	for (int32 Index = 0; Index < ReasonTexts.Num(); ++Index)
	{
		if (UTextBlock* Reason = ReasonTexts[Index].Get(); Reason && Index < RevealReasonsShown)
		{
			const float Start = RevealReasonStart + Index * RevealReasonStep;
			const float T = FMath::Clamp((RevealTime - Start) / 0.3f, 0.f, 1.f);
			Reason->SetRenderOpacity(T);
			Reason->SetRenderTranslation(FVector2D(18.f * (1.f - T), 0.f));
		}
	}

	if (RevealStarCount >= 4 && RevealTime >= RevealPhraseAt + 0.25f && RevealStarsShown == RevealStarCount)
	{
		// 高星给一声庆祝（只放一次：下一次 Tick 前 StarsShown 仍相等，用时间窗限制）
		static constexpr float CelebrateAt = RevealPhraseAt + 0.25f;
		if (RevealTime - DeltaTime < CelebrateAt)
		{
			PlayRevealCue(WinSound, 0.4f);
		}
	}

	if (RevealTime >= RevealDoneAt)
	{
		FinishReveal();
	}
}

void USeasonDailyCardWidget::FinishReveal()
{
	if (!bRevealActive)
	{
		return;
	}
	bRevealActive = false;
	bRevealFinished = true;
	if (const USeasonSystem* Season = RevealSeason.Get())
	{
		ApplyFinalVisual(Season);
	}
}

void USeasonDailyCardWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!bShowing || !CardBorder)
	{
		return;
	}
	if (!LastLayoutSize.Equals(MyGeometry.GetLocalSize(), 1.f))
	{
		ApplyLayout(bInteractive);
	}
	Elapsed += InDeltaTime;
	UpdateReveal(InDeltaTime);

	if (bInteractive)
	{
		HudSuppressAccum += InDeltaTime;
		if (HudSuppressAccum >= HudSuppressInterval)
		{
			HudSuppressAccum = 0.f;
			SuppressOtherHud(GetWorld());
		}
	}

	float Alpha = 1.f;
	if (Elapsed < CardFadeInSeconds)
	{
		Alpha = Elapsed / CardFadeInSeconds;
	}
	else if (!bInteractive && Elapsed > CardHoldSeconds)
	{
		Alpha = 1.f - (Elapsed - CardHoldSeconds) / CardFadeOutSeconds;
	}
	Alpha = FMath::Clamp(Alpha, 0.f, 1.f);
	CardBorder->SetRenderOpacity(Alpha);
	CardBorder->SetRenderTranslation(GetSlideOffset(Alpha));

	if (!bInteractive && Elapsed >= CardHoldSeconds + CardFadeOutSeconds)
	{
		bShowing = false;
		CardBorder->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void USeasonDailyCardWidget::NativeDestruct()
{
	const bool bWasAwaiting = IsAwaitingNextDay();
	if (bInteractive)
	{
		SetUIInputEnabled(false);
	}
	bShowing = false;
	bInteractive = false;
	bRevealActive = false;
	if (APlayerController* PC = OwnerController.Get())
	{
		ActiveCards.Remove(TWeakObjectPtr<APlayerController>(PC));
	}
	if (bWasAwaiting && !AnyInteractiveCardShowing())
	{
		RestoreOtherHud();
	}
	Super::NativeDestruct();
}
