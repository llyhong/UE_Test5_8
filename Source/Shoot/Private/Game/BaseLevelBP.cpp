// Fill out your copyright notice in the Description page of Project Settings.


#include "Game/BaseLevelBP.h"

#include "Game/BaseGameInstance.h"
#include "NPC/GuestManager.h"
#include "NPC/ChairSeat.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Blueprint/UserWidget.h"
#include "UI/OrderWidget.h"
#include "UI/HUDWidget.h"
#include "Game/BusinessTime.h"
#include "Engine/DirectionalLight.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

// Sets default values
ABaseLevelBP::ABaseLevelBP()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	// HUD 类回退默认值：若关卡实例未显式配置 HUDWidgetClass，则使用项目内置的 WBP_HUD，
	// 避免“启动后 HUD 不显示”。关卡蓝图中若显式指定了其它 HUD 类，会覆盖此默认值。
	static ConstructorHelpers::FClassFinder<UHUDWidget> HUDClassFinder(
		TEXT("/Game/_Demo/Blueprints/UI_2D/WBP_HUD.WBP_HUD_C"));
	if (HUDClassFinder.Succeeded())
	{
		HUDWidgetClass = HUDClassFinder.Class;
	}
}

// Called when the game starts or when spawned
void ABaseLevelBP::BeginPlay()
{
    Cast<UBaseGameInstance>(GetGameInstance())->curLevelBP = this;
	Super::BeginPlay();

	// 自动创建订单系统：若关卡蓝图尚未调用 InitOrderSystem（orderSystem 仍为空），
	// 则按本关卡的 LevelID 选择订单配置自动创建并启动，避免客人/上菜时因订单系统未就绪而失败。
	if (!orderSystem)
	{
		// 优先按 LevelID 取专属配置，找不到再回退到 DefaultOrderConfig
		ULevelOrderCfg* Cfg = DefaultOrderConfig;
		if (const TObjectPtr<ULevelOrderCfg>* Found = LevelOrderConfigs.Find(LevelID))
		{
			Cfg = *Found;
		}

		if (Cfg)
		{
			InitOrderSystem(Cfg);
			UE_LOG(LogTemp, Log, TEXT("[BaseLevelBP] 已自动创建订单系统（LevelID=%d）"), LevelID);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[BaseLevelBP] 自动创建订单系统失败：订单配置为空（LevelID=%d），请检查 LevelOrderConfigs / DefaultOrderConfig 或手动调用 InitOrderSystem"), LevelID);
		}
	}

	// 创建订单 UI（与订单系统同生命周期）：立即绑定，无需轮询
	if (orderSystem && OrderWidgetClass)
	{
		// CreateWidget 的 OwningObject 仅接受具体支持类型（APlayerController / UWorld 等），不能用 UObject* 或混合类型的三元
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
		{
			OrderWidget = CreateWidget<UOrderWidget>(PC, OrderWidgetClass);
		}
		else
		{
			OrderWidget = CreateWidget<UOrderWidget>(GetWorld(), OrderWidgetClass);
		}
		if (OrderWidget)
		{
			OrderWidget->AddToViewport(static_cast<int32>(EWidgetZOrder::Order));
			OrderWidget->BindOrderSystem(orderSystem);
			if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance()))
			{
				OrderWidget->BindEconomy(GI->economySystem);
			}
		}
	}

	// 绑定客人管理器的“打烊收尾完成”事件：
	// 已点菜客人全部处理完（玩家上完菜 / 客人等待超时离场）后才真正打烊、结束这一天。
	{
		TArray<AActor*> GuestManagers;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), AGuestManager::StaticClass(), GuestManagers);
		for (AActor* Actor : GuestManagers)
		{
			if (AGuestManager* GM = Cast<AGuestManager>(Actor))
			{
				GM->OnClosingFinished.AddDynamic(this, &ABaseLevelBP::HandleClosingFinished);
			}
		}
	}

// 关卡目标：先解析本关通过条件并订阅经济变化，确保 HUD 绑定（BindLevel）触发首次 RefreshHUD 时目标数据已就绪
if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance()))
{
	ResolveLevelGoal();
	if (GI->economySystem)
	{
		GI->economySystem->OnMoneyChanged.AddDynamic(this, &ABaseLevelBP::OnEconomyMoneyChanged);
		GI->economySystem->OnReputationChanged.AddDynamic(this, &ABaseLevelBP::OnEconomyReputationChanged);
	}
	// 启动时也检查一次（例如存档已满足目标）
	CheckLevelGoal();
}

// 创建常驻 HUD（显示声誉/金钱/时钟），绑定经济系统自动刷新（与关卡同生命周期）
if (HUDWidgetClass)
{
		// CreateWidget 的 OwningObject 仅接受具体支持类型（APlayerController / UWorld 等），故分支创建
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
		{
			HUDWidget = CreateWidget<UHUDWidget>(PC, HUDWidgetClass);
		}
		else
		{
			HUDWidget = CreateWidget<UHUDWidget>(GetWorld(), HUDWidgetClass);
		}
		if (HUDWidget)
		{
			// 先加入视口，确保控件 Construct（子控件实例化）完成，
			// 否则下方 BindEconomy/BindLevel 触发的首次 RefreshHUD 会因控件尚未就绪而丢数据。
			HUDWidget->AddToViewport(static_cast<int32>(EWidgetZOrder::HUD));
			if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance()))
			{
				HUDWidget->BindEconomy(GI->economySystem);
			}
			// 绑定本关卡，使 HUD 自动刷新关卡目标 UI（描述/进度/明细/达成）
			HUDWidget->BindLevel(this);
		}
		else
		{
			// 类已配置但创建失败：通常是该蓝图（如 WBP_HUD）存在编译错误，需在编辑器里重新编译
			UE_LOG(LogTemp, Error,
				TEXT("[BaseLevelBP] HUD 创建失败：HUDWidgetClass=%s 可能未编译或存在蓝图错误，请在编辑器重新编译该 HUD 蓝图。"),
				*HUDWidgetClass->GetPathName());
		}
}
else
{
		UE_LOG(LogTemp, Error,
			TEXT("[BaseLevelBP] HUDWidgetClass 为空，HUD 未创建。请在关卡 BaseLevelBP 实例的 \"UI|HUD\" 分类下指定 HUD 类（如 WBP_HUD）。"));
}

	// 启动时钟刷新：每秒把当前时间与营业状态推给 HUD（白天/傍晚/打烊一目了然）
	if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance()))
	{
		// 若关卡指定了 BusinessSimulation 蓝图子类，则确保 GameInstance 使用的是该类的实例。
		// 仅在类不匹配时重建（避免每次进关都重置）；重建后从存档恢复日期 / 员工名册。
		if (GI->businessSimulation && BusinessSimulationClass && !GI->businessSimulation->IsA(BusinessSimulationClass))
		{
			UBusinessSimulation* NewSim = NewObject<UBusinessSimulation>(GI, BusinessSimulationClass.Get());
			if (GI->economySystem)
			{
				NewSim->Init(GI->economySystem);
			}
			NewSim->Load();
			GI->businessSimulation = NewSim;
		}

		if (GI->businessSimulation && GI->businessSimulation->TimeSystem)
		{
			UTimeSystem* TS = GI->businessSimulation->TimeSystem;
			// 诊断：确认实际生效的 TimeSystem 类与开关状态（排查“一进游戏就营业”）
			UE_LOG(LogTemp, Log,
				TEXT("[BaseLevelBP] 生效 TimeSystem=%s | bAutoOpenBusiness=%d | OpenHour=%.1f CloseHour=%.1f | DayStart=%.1f DayEnd=%.1f | DayProgress=%.3f | 当前小时=%.2f | bIsBusinessOpen=%d | BusinessSim=%s"),
				*TS->GetClass()->GetName(), TS->bAutoOpenBusiness,
				TS->OpenHour, TS->CloseHour, TS->DayStartHour, TS->DayEndHour,
				TS->DayProgress, TS->GetCurrentHour(), TS->bIsBusinessOpen,
				*GI->businessSimulation->GetClass()->GetName());
			GetWorld()->GetTimerManager().SetTimer(ClockTimerHandle, this, &ABaseLevelBP::RefreshClock, 1.f, true);

			// 自动绑定太阳：若 TimeSystem 未指定 SunLight，则按标签"Sun"优先、否则首个 DirectionalLight 绑定，并立即应用一次
			if (TS->bDriveSun && TS->SunLight == nullptr)
			{
				ADirectionalLight* FoundSun = nullptr;
				for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
				{
					if (It->ActorHasTag(FName("Sun")))
					{
						FoundSun = *It;
						break;
					}
				}
				if (!FoundSun)
				{
					for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
					{
						FoundSun = *It;
						break;
					}
				}
				TS->SunLight = FoundSun;
			}
			TS->UpdateSun();
		}
	}
}

void ABaseLevelBP::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 关卡结束时移除订单 UI（其 NativeDestruct 会自行解绑订单系统），避免残留 widget 跨关卡滞留
	if (OrderWidget)
	{
		OrderWidget->RemoveFromParent();
		OrderWidget = nullptr;
	}
	// 一并移除常驻 HUD，避免跨关卡残留（其 BindEconomy 委托在 NativeDestruct 中解绑）
	if (HUDWidget)
	{
		HUDWidget->RemoveFromParent();
		HUDWidget = nullptr;
	}
	Super::EndPlay(EndPlayReason);
	if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance()))
	{
		// 退出关卡时统一落盘：箱内剩余数量等已在内存中更新，这里一次性写入存档（避免每次摘取都写盘）
		GI->SaveGameData();
		GI->curLevelBP = nullptr;
		// 解除太阳引用，避免关卡卸载后悬空指针跨关残留
		if (GI->businessSimulation && GI->businessSimulation->TimeSystem)
		{
			GI->businessSimulation->TimeSystem->SunLight = nullptr;
		}
	}
}

UBusinessSimulation* ABaseLevelBP::GetBusinessSimulation() const
{
	if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance()))
	{
		return GI->businessSimulation;
	}
	return nullptr;
}

void ABaseLevelBP::RefreshClock()
{
	if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance()))
	{
		if (GI->businessSimulation && GI->businessSimulation->TimeSystem && HUDWidget)
		{
			UTimeSystem* TS = GI->businessSimulation->TimeSystem;
			HUDWidget->SetClock(TS->GetCurrentHour(), TS->IsBusinessOpen());
		}
	}
}

void ABaseLevelBP::ResolveLevelGoal()
{
	bGoalConfigured = false;
	ResolvedGoal = FLevelGoalRow();

	UDataTable* Table = LevelGoalTable;
	if (!Table)
	{
		if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance()))
		{
			Table = GI->LevelGoalTable;
		}
	}
	if (!Table)
	{
		UE_LOG(LogTemp, Log, TEXT("[BaseLevelBP] 未配置关卡目标表，本关无通过条件（LevelID=%d）"), LevelID);
		return;
	}

	// 优先按行名 = LevelID 字符串查找；找不到再遍历匹配 LevelID 字段
	FLevelGoalRow* Row = Table->FindRow<FLevelGoalRow>(FName(*FString::FromInt(LevelID)), TEXT("LevelGoal"));
	if (!Row)
	{
		for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
		{
			if (FLevelGoalRow* R = reinterpret_cast<FLevelGoalRow*>(Pair.Value))
			{
				if (R->LevelID == LevelID)
				{
					Row = R;
					break;
				}
			}
		}
	}

	if (Row)
	{
		ResolvedGoal = *Row;
		bGoalConfigured = true;
		UE_LOG(LogTemp, Log, TEXT("[BaseLevelBP] 已解析关卡目标（LevelID=%d）：分=%d 钱=%d 客人=%d 声誉=%d 解锁食材=%d 项"),
			LevelID, ResolvedGoal.TargetScore, ResolvedGoal.TargetMoneyEarned,
			ResolvedGoal.TargetGuestsServed, ResolvedGoal.TargetReputation, ResolvedGoal.UnlockIngredientIDs.Num());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[BaseLevelBP] 关卡目标表中找不到 LevelID=%d 的行，本关无通过条件"), LevelID);
	}
}

void ABaseLevelBP::CheckLevelGoal()
{
	if (bGoalAchieved || !bGoalConfigured)
	{
		return;
	}

	const int32 CurReputation = (Cast<UBaseGameInstance>(GetGameInstance()) && Cast<UBaseGameInstance>(GetGameInstance())->economySystem)
		? Cast<UBaseGameInstance>(GetGameInstance())->economySystem->GetReputation() : 0;

	bool bOk = true;
	if (ResolvedGoal.TargetScore > 0 && Score < ResolvedGoal.TargetScore) bOk = false;
	if (ResolvedGoal.TargetMoneyEarned > 0 && MoneyEarnedThisLevel < ResolvedGoal.TargetMoneyEarned) bOk = false;
	if (ResolvedGoal.TargetGuestsServed > 0 && GuestsServedThisLevel < ResolvedGoal.TargetGuestsServed) bOk = false;
	if (ResolvedGoal.TargetReputation > 0 && CurReputation < ResolvedGoal.TargetReputation) bOk = false;

	if (bOk)
	{
		AchieveLevelGoal();
	}
}

void ABaseLevelBP::AchieveLevelGoal()
{
	if (bGoalAchieved)
	{
		return;
	}
	bGoalAchieved = true;

	// 解锁目标配置中列出的食材（场景上预先放置的食材，如玉米田里的玉米）
	if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance()))
	{
		GI->UnlockIngredients(ResolvedGoal.UnlockIngredientIDs);
	}

	UE_LOG(LogTemp, Warning, TEXT("[BaseLevelBP] 关卡目标达成！（LevelID=%d）解锁食材 %d 项，结束关卡=%s"),
		LevelID, ResolvedGoal.UnlockIngredientIDs.Num(), bFinishLevelOnGoalAchieved ? TEXT("是") : TEXT("否"));

	// 蓝图庆祝 / 解锁提示
	OnLevelGoalAchieved();

	// 广播通关统计（携带本关最终数据），供 HUD 显示通关结算面板
	int32 CurReputation = 0;
	if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance()))
	{
		if (GI->economySystem)
		{
			CurReputation = GI->economySystem->GetReputation();
		}
	}
	OnLevelGoalCompleted.Broadcast(GuestsServedThisLevel, MoneyEarnedThisLevel, Score, CurReputation);

	if (bFinishLevelOnGoalAchieved)
	{
		IsPlaying = false;
		LevelFinish();
	}
}

void ABaseLevelBP::OnEconomyMoneyChanged(int32 NewValue, int32 Delta, EMoneySource Source)
{
	// 仅“订单完成”来源的金钱才计入本关目标（每日被动收入、年末奖金、商店消费、维护/工资支出等均不计入）
	if (Source == EMoneySource::OrderComplete && Delta > 0)
	{
		MoneyEarnedThisLevel += Delta;
		UE_LOG(LogTemp, Log, TEXT("[BaseLevelBP] 订单收入 +%d，本关累计 %d"), Delta, MoneyEarnedThisLevel);
	}
	CheckLevelGoal();
}

void ABaseLevelBP::OnEconomyReputationChanged(int32 NewValue, int32 Delta)
{
	CheckLevelGoal();
}

FString ABaseLevelBP::GetGoalProgressText()
{
	if (!bGoalConfigured)
	{
		return FString();
	}

	const int32 CurReputation = (Cast<UBaseGameInstance>(GetGameInstance()) && Cast<UBaseGameInstance>(GetGameInstance())->economySystem)
		? Cast<UBaseGameInstance>(GetGameInstance())->economySystem->GetReputation() : 0;

	TArray<FString> Parts;
	if (ResolvedGoal.TargetScore > 0)
	{
		Parts.Add(FString::Printf(TEXT("分数 %d/%d"), Score, ResolvedGoal.TargetScore));
	}
	if (ResolvedGoal.TargetMoneyEarned > 0)
	{
		Parts.Add(FString::Printf(TEXT("金钱 %d/%d"), MoneyEarnedThisLevel, ResolvedGoal.TargetMoneyEarned));
	}
	if (ResolvedGoal.TargetGuestsServed > 0)
	{
		Parts.Add(FString::Printf(TEXT("客人 %d/%d"), GuestsServedThisLevel, ResolvedGoal.TargetGuestsServed));
	}
	if (ResolvedGoal.TargetReputation > 0)
	{
		Parts.Add(FString::Printf(TEXT("声誉 %d/%d"), CurReputation, ResolvedGoal.TargetReputation));
	}

	return FString::Join(Parts, TEXT(" · "));
}

float ABaseLevelBP::GetGoalProgressRatio()
{
	if (!bGoalConfigured)
	{
		return 1.0f;
	}

	const int32 CurReputation = (Cast<UBaseGameInstance>(GetGameInstance()) && Cast<UBaseGameInstance>(GetGameInstance())->economySystem)
		? Cast<UBaseGameInstance>(GetGameInstance())->economySystem->GetReputation() : 0;

	// 整体完成度 = 所有已配置目标完成比例的最小值（任一未达标则整体未达标）
	float MinRatio = 1.0f;
	auto Update = [&](int32 Current, int32 Target)
	{
		if (Target > 0)
		{
			const float Ratio = FMath::Clamp(static_cast<float>(Current) / static_cast<float>(Target), 0.f, 1.f);
			MinRatio = FMath::Min(MinRatio, Ratio);
		}
	};

	Update(Score, ResolvedGoal.TargetScore);
	Update(MoneyEarnedThisLevel, ResolvedGoal.TargetMoneyEarned);
	Update(GuestsServedThisLevel, ResolvedGoal.TargetGuestsServed);
	Update(CurReputation, ResolvedGoal.TargetReputation);

	return MinRatio;
}

FText ABaseLevelBP::GetGoalDescriptionText() const
{
	if (!bGoalConfigured)
	{
		return FText::GetEmpty();
	}
	return ResolvedGoal.GoalDescription;
}

// Called every frame
void ABaseLevelBP::Tick(float DeltaTime)
{
	if (IsPlaying)
	{
		StartTime += DeltaTime;
		if (StartTime > EndTime)
		{
			IsPlaying =	false;
			LevelFinish();
		}
	}
	Super::Tick(DeltaTime);

}

void ABaseLevelBP::InitOrderSystem(UDataAsset* MenuDataTable)
{
	// 防重复创建：订单系统已存在则不再 SpawnActor，避免自动创建后又被手动调用导致生成多个实例
	if (orderSystem)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BaseLevelBP] InitOrderSystem 被调用但订单系统已存在，跳过重复创建"));
		return;
	}

	ULevelOrderCfg * Cfg = Cast<ULevelOrderCfg>(MenuDataTable);

	if (!Cfg)
	{
		UE_LOG(LogTemp, Error, TEXT("[BaseLevelBP] InitOrderSystem 失败：MenuDataTable 不是 ULevelOrderCfg（订单系统未创建）"));
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 8.0f, FColor::Red,
				TEXT("[Level] InitOrderSystem 失败：订单配置(MenuDataTable) 未指定或类型不对"));
		}
		return;
	}

	orderSystem = GetWorld()->SpawnActor<AOrderSystem>();// NewObject<AOrderSystem>();
	if (!orderSystem)
	{
		UE_LOG(LogTemp, Error, TEXT("[BaseLevelBP] InitOrderSystem 失败：SpawnActor<AOrderSystem> 返回空"));
		return;
	}

	orderSystem->InitCfg(Cfg);

	// 创建后默认立即启动：否则 IsStarted=false 会让 RequestOrderForGuest / PushFoods 全部静默失败
	if (bAutoStartOrderSystem)
	{
		orderSystem->StartSystem();
	}

	// 把订单完成/失败事件接到经济系统（声誉/金钱）
	orderSystem->OnFinishOrder.AddDynamic(this, &ABaseLevelBP::HandleOrderFinished);
	orderSystem->OnFailOrder.AddDynamic(this, &ABaseLevelBP::HandleOrderFailed);
}

void ABaseLevelBP::InitPlateSystem(int PlateCount,float SpawnPlateTime)
{
	PlateSystem = GetWorld()->SpawnActor<APlateSystem>();// NewObject<AOrderSystem>();
	PlateSystem->InitParam(PlateCount,SpawnPlateTime);
}

void ABaseLevelBP::AdvanceToLevel(int32 NewLevelID)
{
	// 进入下一关改为“打烊过渡”：先清场打烊，倒计时后再正式切换，避免上一关订单/客人残留。
	BeginLevelTransition(NewLevelID);
}

void ABaseLevelBP::AdvanceToNextLevel()
{
	AdvanceToLevel(LevelID + 1);
}

void ABaseLevelBP::DebugAdvanceToNextLevel()
{
	UE_LOG(LogTemp, Warning, TEXT("[BaseLevelBP] [DEBUG] 强制触发打烊过渡（LevelID %d -> %d）"), LevelID, LevelID + 1);
	AdvanceToNextLevel();
}

void ABaseLevelBP::BeginLevelTransition(int32 NewLevelID)
{
	if (bInLevelTransition)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BaseLevelBP] 已进入打烊过渡中，忽略重复进入下一关请求"));
		return;
	}
	if (NewLevelID == LevelID)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BaseLevelBP] 忽略：目标 LevelID 与当前相同 (%d)"), NewLevelID);
		return;
	}

	bInLevelTransition = true;
	PendingLevelID = NewLevelID;

	UE_LOG(LogTemp, Log, TEXT("[BaseLevelBP] 打烊过渡开始：准备从 LevelID %d 进入 %d（%.1fs 后正式切换）"), LevelID, NewLevelID, LevelTransitionDuration);

	// 1) 停止订单系统，不再接受新订单
	if (orderSystem)
	{
		orderSystem->StopSystem();
		// 清空上一关已接受的订单（静默清空，不打超时罚分）
		orderSystem->ClearAllOrders();
	}

	// 2) 同步清空订单 UI 显示
	if (OrderWidget)
	{
		OrderWidget->ClearAllOrdersUI();
	}

	// 3) 清空在场客人（已接受订单随客人离场而取消），让客人离场
	TArray<AActor*> GuestManagers;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AGuestManager::StaticClass(), GuestManagers);
	for (AActor* Actor : GuestManagers)
	{
		if (AGuestManager* GM = Cast<AGuestManager>(Actor))
		{
			// 先暂停自动生成，避免清场后倒计时内又冒出新客人重新占座
			GM->PauseSpawning();
			GM->ClearAllGuests();
		}
	}

	// 3.5) 防御性兜底：强制释放场景上仍被标记的座位（客户引用丢失等异常情况下避免“幽灵占用”残留）
	TArray<AActor*> SeatActors;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AChairSeat::StaticClass(), SeatActors);
	for (AActor* Actor : SeatActors)
	{
		if (AChairSeat* Seat = Cast<AChairSeat>(Actor))
		{
			if (Seat->bIsOccupied)
			{
				Seat->Release();
			}
		}
	}

	// 4) 广播打烊提示（HUD 显示“打烊”横幅 + 倒计时）
	OnLevelClosing.Broadcast(LevelTransitionDuration);

	// 5) 倒计时后正式进入下一关
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(ClosingTimerHandle, this, &ABaseLevelBP::CommitLevelTransition, LevelTransitionDuration, false);
	}
	else
	{
		// 无世界（极端情况）直接提交
		CommitLevelTransition();
	}
}

void ABaseLevelBP::HandleClosingFinished()
{
	UE_LOG(LogTemp, Log, TEXT("[BaseLevelBP] 收到“打烊收尾完成”：已点菜客人全部处理完（bAutoEndDayOnClosingFinished=%d）"),
		bAutoEndDayOnClosingFinished);

	if (!bAutoEndDayOnClosingFinished)
	{
		// 保持原有流程：由升级 NPC 对话触发进入下一关
		return;
	}

	// 已上完菜 / 客人等待超时 → 真正结束这一天，进入下一天（下一关）
	AdvanceToNextLevel();
}

void ABaseLevelBP::CommitLevelTransition()
{
	const int32 NewLevelID = PendingLevelID;
	bInLevelTransition = false;
	PendingLevelID = INDEX_NONE;

	if (NewLevelID == INDEX_NONE || NewLevelID == LevelID)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BaseLevelBP] CommitLevelTransition 跳过：无效或相同 LevelID (%d)"), NewLevelID);
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[BaseLevelBP] 正式进入下一关：LevelID %d -> %d（同一张大地图，不切换地图）"), LevelID, NewLevelID);
	LevelID = NewLevelID;

	// 重置本关进度（保留经济系统的金钱/声誉，关卡目标从零开始）
	bGoalAchieved = false;
	bLevelFinished = false;
	MoneyEarnedThisLevel = 0;
	GuestsServedThisLevel = 0;
	Score = 0;
	StartTime = 0;
	IsPlaying = true;

	// 重新解析本关目标（按新 LevelID 从数据表取对应行）
	ResolveLevelGoal();

	// 触发 HUD 刷新新关卡目标（HUD 仅在分数/经济变化时刷新，这里主动推一次）
	LevelUpdateScore.Broadcast(Score);

	// 切换订单配置并重启订单系统（关卡目标与订单均对应新关卡）
	RebuildOrderSystem();

	// 恢复客人自动生成（打烊期间被 PauseSpawning 暂停）
	TArray<AActor*> GuestManagers;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AGuestManager::StaticClass(), GuestManagers);
	for (AActor* Actor : GuestManagers)
	{
		if (AGuestManager* GM = Cast<AGuestManager>(Actor))
		{
			GM->ResumeSpawning();
		}
	}

	// 广播关卡切换，供 UI / NPC 重置自身状态（如奖励已领取标记）
	OnLevelChanged.Broadcast(LevelID);
}

void ABaseLevelBP::RebuildOrderSystem()
{
	// 优先按 LevelID 取专属配置，找不到再回退到 DefaultOrderConfig
	ULevelOrderCfg* Cfg = DefaultOrderConfig;
	if (const TObjectPtr<ULevelOrderCfg>* Found = LevelOrderConfigs.Find(LevelID))
	{
		Cfg = *Found;
	}
	if (!Cfg)
	{
		UE_LOG(LogTemp, Error, TEXT("[BaseLevelBP] RebuildOrderSystem 失败：找不到 LevelID=%d 的订单配置，请检查 LevelOrderConfigs / DefaultOrderConfig"), LevelID);
		return;
	}

	if (orderSystem)
	{
		// 复用同一订单系统 Actor：停掉旧订单，载入新配置并重启（避免重新 Spawn 导致委托重复绑定）
		orderSystem->StopSystem();
		orderSystem->InitCfg(Cfg);
		if (bAutoStartOrderSystem)
		{
			orderSystem->StartSystem();
		}
	}
	else
	{
		InitOrderSystem(Cfg);
	}

	// 订单 UI 重新绑定（先解绑再绑定，强制刷新等待列表并保持一致委托，避免残留旧关卡订单）
	if (OrderWidget)
	{
		OrderWidget->UnbindOrderSystem();
		OrderWidget->BindOrderSystem(orderSystem);
	}
}

void ABaseLevelBP::Play()
{
	StartTime = 0;
	Score = 0;
	IsPlaying = true;
}

float ABaseLevelBP::GetRemainTime()
{
	return EndTime - StartTime;
}

void ABaseLevelBP::LevelFinish()
{
	if (bLevelFinished)
	{
		return;
	}
	bLevelFinished = true;
	SaveGame();
	OnLevelFinish();
}

void ABaseLevelBP::AddScore(AActor* actor,int detal)
{
	auto level = Cast<UBaseGameInstance>(actor->GetGameInstance())->curLevelBP;
	if (level != nullptr)
	{
		level->Score += detal;
		level->LevelGetScore.Broadcast(detal);
		level->LevelUpdateScore.Broadcast(level->Score);
		level->CheckLevelGoal();
	}
}

int ABaseLevelBP::GetScore(AActor* actor)
{
	auto level = Cast<UBaseGameInstance>(actor->GetGameInstance())->curLevelBP;
	if (level != nullptr)
	{
		return level->Score;
	}
	return -1;
}

void ABaseLevelBP::SaveGame()
{
	auto GameInstance = Cast<UBaseGameInstance>(this->GetGameInstance());
	auto saveData = Cast<UBaseGameInstance>(this->GetGameInstance())->curSaveGameData;
	if (saveData != nullptr)
	{
		auto hasKey = saveData->LevelMaxScore.Contains(this->LevelID);
		if (hasKey)
		{
			int oldMaxSorce = saveData->LevelMaxScore[this->LevelID];
			if (this->Score > oldMaxSorce)
			{
				saveData->LevelMaxScore[this->LevelID] = this->Score;
			}
		}
		else
		{
			saveData->LevelMaxScore.Add(this->LevelID,this->Score);
		}
		GameInstance->SaveGameData();
	}
}

int ABaseLevelBP::GetHistoryMaxScore()
{
	auto saveData = Cast<UBaseGameInstance>(this->GetGameInstance())->curSaveGameData;
	if (saveData != nullptr)
	{
		auto hasKey = saveData->LevelMaxScore.Contains(this->LevelID);
		if (hasKey)
		{
			int oldMaxSorce = saveData->LevelMaxScore[this->LevelID];
			return oldMaxSorce;
		}
	}
	return 0;
}

void ABaseLevelBP::HandleOrderFinished(const UOrderHandle* Handle, const FOrderData& Order)
{
	// 每完成一单视为服务了一位客人，累计用于关卡目标判定
	GuestsServedThisLevel++;
	CheckLevelGoal();

	if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance()))
	{
		if (GI->economySystem)
		{
			GI->economySystem->OnOrderCompleted(Order.Price);
		}
	}
}

void ABaseLevelBP::HandleOrderFailed(const UOrderHandle* Handle, const FOrderData& Order)
{
	if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance()))
	{
		if (GI->economySystem)
		{
			GI->economySystem->OnOrderFailed();
		}
	}
}
