// Fill out your copyright notice in the Description page of Project Settings.

#include "Event/RandomEventManager.h"

#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Event/PestActor.h"
#include "Game/BaseGameInstance.h"
#include "NPC/GuestCharacter.h"

ARandomEventManager::ARandomEventManager()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ARandomEventManager::BeginPlay()
{
	Super::BeginPlay();

	EnsureRandomReady();
	EnsureTimeSystemBound();

	if (bShowDebugInfo && !EventTable)
	{
		ShowDebug(TEXT("[突发事件] 未指定 EventTable（数据表），事件不会自动触发"), FLinearColor::Yellow);
	}
	if (bShowDebugInfo && EventTable)
	{
		ShowDebug(FString::Printf(TEXT("[突发事件] 已加载配置表 %s，共 %d 条事件"),
			*EventTable->GetName(), EventTable->GetRowNames().Num()), FLinearColor::Green);
	}

	if (GetWorld())
	{
		NextCheckWorldTime = GetWorld()->GetTimeSeconds() + FMath::Max(1.f, CheckIntervalSeconds);
	}
}

void ARandomEventManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (BoundTimeSystem)
	{
		BoundTimeSystem->OnDayChanged.RemoveDynamic(this, &ARandomEventManager::HandleDayChanged);
		BoundTimeSystem->OnBusinessOpenChanged.RemoveDynamic(this, &ARandomEventManager::HandleBusinessOpenChanged);
		BoundTimeSystem = nullptr;
	}

	ActivePests.Empty();

	Super::EndPlay(EndPlayReason);
}

void ARandomEventManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	EnsureTimeSystemBound();
	EnsureRandomReady();

	// 事件进行中：只推进当前事件，不再触发新事件
	if (HasActiveEvent())
	{
		UpdateActiveEvent(DeltaSeconds);
		return;
	}

	if (!bAutoTrigger || !EventTable || !GetWorld())
	{
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < NextCheckWorldTime)
	{
		return;
	}
	NextCheckWorldTime = Now + FMath::Max(1.f, CheckIntervalSeconds);

	// 全局冷却：上一次事件结算后隔一段时间再做下一次检查
	if (LastResolveWorldTime >= 0.f && (Now - LastResolveWorldTime) < MinTimeBetweenEventsSeconds)
	{
		return;
	}

	TryTriggerRandomEvent();
}

void ARandomEventManager::EnsureRandomReady()
{
	if (bRandReady)
	{
		return;
	}
	Rand.Initialize(RandomSeed != 0 ? RandomSeed : FMath::Rand());
	bRandReady = true;
}

void ARandomEventManager::EnsureTimeSystemBound()
{
	UTimeSystem* Current = nullptr;
	if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance()))
	{
		if (GI->businessSimulation)
		{
			Current = GI->businessSimulation->TimeSystem;
		}
	}

	if (Current == BoundTimeSystem.Get())
	{
		return;
	}

	if (BoundTimeSystem)
	{
		BoundTimeSystem->OnDayChanged.RemoveDynamic(this, &ARandomEventManager::HandleDayChanged);
		BoundTimeSystem->OnBusinessOpenChanged.RemoveDynamic(this, &ARandomEventManager::HandleBusinessOpenChanged);
	}

	BoundTimeSystem = Current;
	if (BoundTimeSystem)
	{
		BoundTimeSystem->OnDayChanged.AddDynamic(this, &ARandomEventManager::HandleDayChanged);
		BoundTimeSystem->OnBusinessOpenChanged.AddDynamic(this, &ARandomEventManager::HandleBusinessOpenChanged);
		NextCheckWorldTime = 0.f; // 绑定后立刻评估一次
	}
}

void ARandomEventManager::HandleDayChanged(int32 InYear, int32 InMonth, int32 InDay)
{
	TriggerCountToday.Empty();
	NextCheckWorldTime = 0.f;
}

void ARandomEventManager::HandleBusinessOpenChanged(bool bOpen)
{
	// 营业状态刚切换：马上评估一次（例如刚打烊，打烊期间的鼠患才可能出现）
	NextCheckWorldTime = 0.f;

	if (HasActiveEvent() && ActiveEventInfo.bCancelOnBusinessStateChange && !IsTimingAllowed(ActiveEventInfo))
	{
		ResolveActiveEvent(false);
	}
}

bool ARandomEventManager::IsTimingAllowed(const FRandomEventInfo& Info) const
{
	const bool bOpen = BoundTimeSystem && BoundTimeSystem->IsBusinessOpen();
	switch (Info.Timing)
	{
	case ERandomEventTiming::BusinessOpen:
		return bOpen;
	case ERandomEventTiming::BusinessClosed:
		return !bOpen;
	case ERandomEventTiming::Both:
	default:
		return true;
	}
}

bool ARandomEventManager::IsHourAllowed(const FRandomEventInfo& Info) const
{
	if (!BoundTimeSystem)
	{
		return true;
	}
	// Max <= Min 视为不限时间（默认 0/0）
	if (Info.MaxStartHour <= Info.MinStartHour)
	{
		return true;
	}
	const float Hour = BoundTimeSystem->GetCurrentHour();
	return Hour >= Info.MinStartHour && Hour < Info.MaxStartHour;
}

int32 ARandomEventManager::GetAbsDay() const
{
	if (!BoundTimeSystem)
	{
		return 0;
	}
	return ((BoundTimeSystem->Year - 1) * 12 + (BoundTimeSystem->Month - 1)) * BoundTimeSystem->DaysPerMonth
		+ (BoundTimeSystem->Day - 1);
}

bool ARandomEventManager::IsEventEligible(FName RowName, const FRandomEventInfo& Info) const
{
	if (!IsTimingAllowed(Info) || !IsHourAllowed(Info))
	{
		return false;
	}

	const int32 AbsDay = GetAbsDay();
	if (const int32* LastDay = LastTriggerAbsDay.Find(RowName))
	{
		if (Info.CooldownDays > 0 && (AbsDay - *LastDay) < Info.CooldownDays)
		{
			return false;
		}
	}

	if (Info.MaxTriggersPerDay > 0)
	{
		if (const int32* Count = TriggerCountToday.Find(RowName))
		{
			if (*Count >= Info.MaxTriggersPerDay)
			{
				return false;
			}
		}
	}

	return true;
}

void ARandomEventManager::TryTriggerRandomEvent()
{
	if (!EventTable)
	{
		return;
	}

	TArray<FName> Candidates;
	TArray<float> Weights;
	float TotalWeight = 0.f;

	// 先逐条掷 ChancePerCheck，命中的事件再按 Weight 加权挑选
	for (const FName& RowName : EventTable->GetRowNames())
	{
		const FRandomEventInfo* Info = EventTable->FindRow<FRandomEventInfo>(RowName, TEXT("RandomEvent"), false);
		if (!Info || !IsEventEligible(RowName, *Info))
		{
			continue;
		}
		if (Rand.FRand() > FMath::Clamp(Info->ChancePerCheck, 0.f, 1.f))
		{
			continue;
		}

		Candidates.Add(RowName);
		const float W = FMath::Max(0.01f, Info->Weight);
		Weights.Add(W);
		TotalWeight += W;
	}

	if (Candidates.Num() == 0)
	{
		return;
	}

	float Pick = Rand.FRandRange(0.f, TotalWeight);
	int32 ChosenIndex = Candidates.Num() - 1;
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		Pick -= Weights[Index];
		if (Pick <= 0.f)
		{
			ChosenIndex = Index;
			break;
		}
	}

	StartEventByName(Candidates[ChosenIndex]);
}

bool ARandomEventManager::StartEventByName(FName EventID)
{
	if (!EventTable || EventID.IsNone() || HasActiveEvent())
	{
		return false;
	}

	const FRandomEventInfo* Info = EventTable->FindRow<FRandomEventInfo>(EventID, TEXT("RandomEvent"), false);
	if (!Info)
	{
		ShowDebug(FString::Printf(TEXT("[突发事件] 配置表里找不到事件 %s"), *EventID.ToString()), FLinearColor::Red);
		return false;
	}

	ActiveEventID = EventID;
	ActiveEventInfo = *Info;
	ActiveEventRemainingSeconds = FMath::Max(0.f, Info->DurationSeconds);
	ActiveClearedCount = 0;
	ActiveTotalCount = 0;
	ActivePests.Empty();

	LastTriggerAbsDay.Add(EventID, GetAbsDay());
	++TriggerCountToday.FindOrAdd(EventID);

	SpawnEventPests(*Info);

	const FString Title = Info->Title.IsEmpty() ? EventID.ToString() : Info->Title.ToString();
	ShowDebug(FString::Printf(TEXT("[突发事件] %s：%s"), *Title, *Info->StartText.ToString()),
		FLinearColor(1.f, 0.55f, 0.1f));

	OnEventProgress.Broadcast(ActiveEventInfo, ActiveClearedCount, ActiveTotalCount);
	OnEventStarted.Broadcast(ActiveEventInfo);
	return true;
}

void ARandomEventManager::SpawnEventPests(const FRandomEventInfo& Info)
{
	if (Info.SpawnCount <= 0 || Info.PestClass.IsNull() || !GetWorld())
	{
		return;
	}

	UClass* PestClass = Info.PestClass.LoadSynchronous();
	if (!PestClass)
	{
		ShowDebug(FString::Printf(TEXT("[突发事件] 生成物蓝图加载失败：%s"), *Info.PestClass.ToString()), FLinearColor::Red);
		return;
	}

	for (int32 Index = 0; Index < Info.SpawnCount; ++Index)
	{
		const FVector SpawnLocation = PickSpawnLocation(Info.SpawnAreaLabel);
		const FRotator SpawnRotation(0.f, Rand.FRandRange(0.f, 360.f), 0.f);

		FActorSpawnParameters Params;
		Params.Owner = this;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		APestActor* Pest = GetWorld()->SpawnActor<APestActor>(PestClass, SpawnLocation, SpawnRotation, Params);
		if (!Pest)
		{
			continue;
		}

		Pest->Configure(Info.PestMoveSpeed, Info.PestFleeRadius, Info.PestWanderRadius,
			Info.bPestsScareGuests, Info.PestScareRadius);
		Pest->OnCaught.AddDynamic(this, &ARandomEventManager::HandlePestCaught);
		ActivePests.Add(Pest);
		++ActiveTotalCount;
	}

	if (bShowDebugInfo && ActiveTotalCount == 0)
	{
		ShowDebug(TEXT("[突发事件] 害虫一个都没生成出来，请检查 PestClass / SpawnAreas"), FLinearColor::Red);
	}
}

void ARandomEventManager::HandlePestCaught(APestActor* Pest)
{
	if (!HasActiveEvent())
	{
		return;
	}

	ActivePests.Remove(Pest);
	ActiveClearedCount = FMath::Min(ActiveClearedCount + 1, ActiveTotalCount);

	if (bShowDebugInfo)
	{
		ShowDebug(FString::Printf(TEXT("[突发事件] 已处理 %d/%d"), ActiveClearedCount, ActiveTotalCount),
			FLinearColor(0.2f, 0.8f, 1.f));
	}

	OnEventProgress.Broadcast(ActiveEventInfo, ActiveClearedCount, ActiveTotalCount);

	if (ActiveEventInfo.bSuccessWhenTargetsCleared && ActiveTotalCount > 0 && ActivePests.Num() == 0)
	{
		ResolveActiveEvent(true);
	}
}

void ARandomEventManager::UpdateActiveEvent(float DeltaSeconds)
{
	// 清理被外部销毁 / 逃出场景的生成物
	ActivePests.RemoveAll([](const TObjectPtr<APestActor>& Pest)
	{
		return !IsValid(Pest);
	});

	if (ActiveEventInfo.bSuccessWhenTargetsCleared && ActiveTotalCount > 0 && ActivePests.Num() == 0)
	{
		ResolveActiveEvent(true);
		return;
	}

	if (ActiveEventInfo.DurationSeconds > 0.f)
	{
		ActiveEventRemainingSeconds -= DeltaSeconds;
		if (ActiveEventRemainingSeconds <= 0.f)
		{
			ActiveEventRemainingSeconds = 0.f;
			ResolveActiveEvent(false);
		}
	}
}

void ARandomEventManager::ResolveActiveEvent(bool bSuccess)
{
	if (!HasActiveEvent())
	{
		return;
	}

	const FRandomEventInfo Info = ActiveEventInfo;

	// 清场：剩余害虫直接销毁（失败时相当于“跑掉了”）
	for (APestActor* Pest : ActivePests)
	{
		if (IsValid(Pest))
		{
			Pest->OnCaught.RemoveDynamic(this, &ARandomEventManager::HandlePestCaught);
			Pest->Destroy();
		}
	}
	ActivePests.Empty();

	// 声誉结算
	if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance()))
	{
		if (GI->economySystem)
		{
			const int32 Delta = bSuccess ? Info.ReputationGainOnSuccess : -FMath::Abs(Info.ReputationLossOnFail);
			if (Delta != 0)
			{
				GI->economySystem->AddReputation(Delta);
			}
		}
	}

	// 失败：把客人全部吓走（客人走正常离场流程，会取消未完成订单）
	if (!bSuccess && Info.bScareAllGuestsOnFail)
	{
		ScareAllGuests();
	}

	ActiveEventID = NAME_None;
	ActiveEventInfo = FRandomEventInfo();
	ActiveEventRemainingSeconds = 0.f;
	ActiveClearedCount = 0;
	ActiveTotalCount = 0;

	LastResolveWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	NextCheckWorldTime = LastResolveWorldTime + MinTimeBetweenEventsSeconds;

	OnEventResolved.Broadcast(Info, bSuccess);

	const FString Text = bSuccess ? Info.SuccessText.ToString() : Info.FailText.ToString();
	ShowDebug(FString::Printf(TEXT("[突发事件] %s：%s"), bSuccess ? TEXT("已解决") : TEXT("失败"), *Text),
		bSuccess ? FLinearColor::Green : FLinearColor::Red);
}

void ARandomEventManager::CancelActiveEvent()
{
	if (!HasActiveEvent())
	{
		return;
	}

	for (APestActor* Pest : ActivePests)
	{
		if (IsValid(Pest))
		{
			Pest->OnCaught.RemoveDynamic(this, &ARandomEventManager::HandlePestCaught);
			Pest->Destroy();
		}
	}
	ActivePests.Empty();

	ActiveEventID = NAME_None;
	ActiveEventInfo = FRandomEventInfo();
	ActiveEventRemainingSeconds = 0.f;
	ActiveClearedCount = 0;
	ActiveTotalCount = 0;

	if (GetWorld())
	{
		NextCheckWorldTime = GetWorld()->GetTimeSeconds() + MinTimeBetweenEventsSeconds;
	}
}

void ARandomEventManager::ScareAllGuests()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	TArray<AGuestCharacter*> Guests;
	for (TActorIterator<AGuestCharacter> It(World); It; ++It)
	{
		AGuestCharacter* Guest = *It;
		if (IsValid(Guest) && Guest->GuestState != EGuestState::Leaving)
		{
			Guests.Add(Guest);
		}
	}

	for (AGuestCharacter* Guest : Guests)
	{
		Guest->ScareAway();
	}
}

const FEventSpawnArea* ARandomEventManager::FindSpawnArea(FName Label) const
{
	for (const FEventSpawnArea& Area : SpawnAreas)
	{
		if (Area.Label == Label)
		{
			return &Area;
		}
	}
	return nullptr;
}

FVector ARandomEventManager::PickSpawnLocation(FName AreaLabel) const
{
	const FEventSpawnArea* Area = FindSpawnArea(AreaLabel);
	const FVector Center = Area ? Area->Center : GetActorLocation();
	const float Radius = Area ? FMath::Max(0.f, Area->Radius) : 300.f;

	const float Angle = Rand.FRandRange(0.f, 2.f * PI);
	const float Dist = Radius * FMath::Sqrt(Rand.FRand());
	FVector Location = Center + FVector(FMath::Cos(Angle) * Dist, FMath::Sin(Angle) * Dist, 0.f);

	const bool bSnap = Area ? Area->bSnapToGround : true;
	if (bSnap && GetWorld())
	{
		const FVector Start = Location + FVector(0.f, 0.f, 300.f);
		const FVector End = Location - FVector(0.f, 0.f, 1000.f);

		FCollisionQueryParams Params(FName(TEXT("RandomEventSpawn")), false, this);
		FHitResult Hit;
		if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
		{
			Location.Z = Hit.ImpactPoint.Z;
		}
		else
		{
			Location.Z = Center.Z;
		}
	}
	else
	{
		Location.Z = Center.Z;
	}

	return Location;
}

void ARandomEventManager::ShowDebug(const FString& Message, const FLinearColor& Color) const
{
	if (!bShowDebugInfo)
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("%s"), *Message);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.f, Color.ToFColor(true), Message);
	}
}
