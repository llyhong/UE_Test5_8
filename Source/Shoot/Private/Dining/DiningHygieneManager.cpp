// Fill out your copyright notice in the Description page of Project Settings.

#include "Dining/DiningHygieneManager.h"

#include "Dining/DiningMessActor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/BaseGameInstance.h"
#include "Game/EconomySystem.h"
#include "NPC/GuestCharacter.h"
#include "Components/SceneComponent.h"

ADiningHygieneManager::ADiningHygieneManager()
{
	PrimaryActorTick.bCanEverTick = true;

	// 没有根组件时 Actor 的 Transform 不可写：SpawnAreas 为空时就用不了“以本 Actor 为圆心”的兜底
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ADiningHygieneManager::BeginPlay()
{
	Super::BeginPlay();

	EnsureRandomReady();
	EnsureTimeSystemBound();

	if (bShowDebugInfo && SpawnAreas.Num() == 0)
	{
		ShowDebug(TEXT("[就餐区卫生] 未配置 SpawnAreas，将以本 Actor 位置为圆心、600cm 生成"), FLinearColor::Yellow);
	}

	if (bSpawnInitialMesses && InitialMessCount > 0)
	{
		for (int32 Index = 0; Index < InitialMessCount; ++Index)
		{
			SpawnRandomMess();
		}
	}

	NextSpawnWorldTime = GetWorld()
		? GetWorld()->GetTimeSeconds() + Rand.FRandRange(SpawnIntervalMin, SpawnIntervalMax)
		: 0.f;
	SlipCheckCountdown = SlipCheckInterval;
}

void ADiningHygieneManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	BoundTimeSystem = nullptr;
	ActiveMesses.Empty();
	PenaltyAccumSeconds.Empty();

	Super::EndPlay(EndPlayReason);
}

void ADiningHygieneManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	EnsureTimeSystemBound();
	EnsureRandomReady();
	PruneMesses();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();

	if (bAutoSpawn && IsSpawningAllowed()
		&& ActiveMesses.Num() < MaxActiveMesses
		&& Now >= NextSpawnWorldTime)
	{
		SpawnRandomMess();
		NextSpawnWorldTime = Now + Rand.FRandRange(SpawnIntervalMin, SpawnIntervalMax);
	}

	if (ActiveMesses.Num() > 0)
	{
		ApplyPenalties(DeltaSeconds);
		CheckGuestSlips(DeltaSeconds);
	}
}

void ADiningHygieneManager::EnsureRandomReady()
{
	if (bRandReady)
	{
		return;
	}
	Rand.Initialize(FMath::Rand());
	bRandReady = true;
}

void ADiningHygieneManager::EnsureTimeSystemBound()
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
	BoundTimeSystem = Current;
}

bool ADiningHygieneManager::IsSpawningAllowed() const
{
	if (!bOnlyWhenBusinessOpen || !BoundTimeSystem)
	{
		return true;
	}
	return BoundTimeSystem->IsBusinessOpen();
}

const FEventSpawnArea* ADiningHygieneManager::PickSpawnArea() const
{
	if (SpawnAreas.Num() == 0)
	{
		return nullptr;
	}
	return &SpawnAreas[Rand.RandRange(0, SpawnAreas.Num() - 1)];
}

FVector ADiningHygieneManager::PickSpawnLocation() const
{
	const FEventSpawnArea* Area = PickSpawnArea();
	const FVector Center = Area ? Area->Center : GetActorLocation();
	const float Radius = Area ? FMath::Max(0.f, Area->Radius) : 600.f;
	const bool bSnap = Area ? Area->bSnapToGround : true;

	FVector Result = Center;
	UWorld* World = GetWorld();

	for (int32 Attempt = 0; Attempt < 6; ++Attempt)
	{
		const float Angle = Rand.FRandRange(0.f, 2.f * PI);
		const float Dist = Radius * FMath::Sqrt(Rand.FRand());
		FVector Candidate = Center + FVector(FMath::Cos(Angle) * Dist, FMath::Sin(Angle) * Dist, 0.f);

		if (bSnap && World)
		{
			const FVector Start = Candidate + FVector(0.f, 0.f, 300.f);
			const FVector End = Candidate - FVector(0.f, 0.f, 1000.f);

			FCollisionQueryParams Params(FName(TEXT("DiningMessSpawn")), false, this);
			FHitResult Hit;
			if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
			{
				Candidate.Z = Hit.ImpactPoint.Z;
			}
			else
			{
				Candidate.Z = Center.Z;
			}
		}
		else
		{
			Candidate.Z = Center.Z;
		}

		// 别和已有散落物叠在一起（否则看起来像一坨、清理判定也互相遮挡）
		bool bTooClose = false;
		for (const ADiningMessActor* Mess : ActiveMesses)
		{
			if (IsValid(Mess) && FVector::DistSquared2D(Mess->GetActorLocation(), Candidate) < FMath::Square(120.f))
			{
				bTooClose = true;
				break;
			}
		}
		Result = Candidate;
		if (!bTooClose)
		{
			break;
		}
	}

	return Result;
}

ADiningMessActor* ADiningHygieneManager::SpawnMess(EDiningMessType Type, FVector Location)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	UClass* Class = MessClass ? MessClass.Get() : ADiningMessActor::StaticClass();
	if (!Class)
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	const FRotator Rotation(0.f, Rand.FRandRange(0.f, 360.f), 0.f);
	ADiningMessActor* Mess = World->SpawnActor<ADiningMessActor>(Class, Location, Rotation, Params);
	if (!Mess)
	{
		return nullptr;
	}

	Mess->SetMessType(Type);
	Mess->bShowDebugInfo = bShowDebugInfo;
	Mess->OnCleaned.AddDynamic(this, &ADiningHygieneManager::HandleMessCleaned);
	ActiveMesses.Add(Mess);

	if (bShowDebugInfo)
	{
		ShowDebug(FString::Printf(TEXT("[就餐区卫生] %s散落（当前 %d 个）"),
			Type == EDiningMessType::Puddle ? TEXT("积水") : TEXT("垃圾"), ActiveMesses.Num()),
			FLinearColor(0.9f, 0.7f, 0.2f));
	}
	return Mess;
}

ADiningMessActor* ADiningHygieneManager::SpawnRandomMess()
{
	if (ActiveMesses.Num() >= MaxActiveMesses)
	{
		return nullptr;
	}
	const EDiningMessType Type = Rand.FRand() < TrashChance ? EDiningMessType::Trash : EDiningMessType::Puddle;
	return SpawnMess(Type, PickSpawnLocation());
}

void ADiningHygieneManager::ApplyPenalties(float DeltaSeconds)
{
	if (CleanGraceSeconds < 0.f)
	{
		return;
	}

	UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance());
	UEconomySystem* Economy = GI ? GI->economySystem.Get() : nullptr;
	UWorld* World = GetWorld();

	int32 PenaltyCount = 0;
	for (const TObjectPtr<ADiningMessActor>& Mess : ActiveMesses)
	{
		if (!IsValid(Mess))
		{
			continue;
		}
		if (Mess->GetAgeSeconds() < CleanGraceSeconds)
		{
			continue;
		}

		float& Accum = PenaltyAccumSeconds.FindOrAdd(Mess);
		Accum += DeltaSeconds;

		// 一帧内一般最多触发一次；用 while 保证低帧率下也不漏结算
		while (Accum >= PenaltyIntervalSeconds)
		{
			Accum -= PenaltyIntervalSeconds;
			++PenaltyCount;

			if (Economy && ReputationLossPerPenalty > 0)
			{
				Economy->AddReputation(-ReputationLossPerPenalty);
			}

			if (World && GuestWaitTimePenaltySeconds > 0.f)
			{
				for (TActorIterator<AGuestCharacter> It(World); It; ++It)
				{
					if (AGuestCharacter* Guest = *It)
					{
						Guest->ApplySatisfactionPenalty(GuestWaitTimePenaltySeconds);
					}
				}
			}

			if (bShowDebugInfo)
			{
				ShowDebug(FString::Printf(TEXT("[就餐区卫生] %s 已放置 %.0f 秒未清理：声誉 -%d，顾客满意度下降"),
					Mess->MessType == EDiningMessType::Puddle ? TEXT("积水") : TEXT("垃圾"),
					Mess->GetAgeSeconds(), ReputationLossPerPenalty),
					FLinearColor(1.f, 0.35f, 0.25f));
			}
		}
	}

	if (PenaltyCount > 0 && Economy)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DiningHygiene] 本轮未清理结算 %d 次，当前声誉 %d"), PenaltyCount, Economy->GetReputation());
	}
}

void ADiningHygieneManager::CheckGuestSlips(float DeltaSeconds)
{
	if (!bEnableSlip)
	{
		return;
	}

	SlipCheckCountdown -= DeltaSeconds;
	if (SlipCheckCountdown > 0.f)
	{
		return;
	}
	SlipCheckCountdown = SlipCheckInterval;

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 只有正在走动 / 排队的客人才会“踩到”；坐着吃饭的客人不受影响
	for (TActorIterator<AGuestCharacter> It(World); It; ++It)
	{
		AGuestCharacter* Guest = *It;
		if (!IsValid(Guest) || !Guest->CanSlip())
		{
			continue;
		}

		const FVector GuestLocation = Guest->GetActorLocation();
		for (const TObjectPtr<ADiningMessActor>& Mess : ActiveMesses)
		{
			if (!IsValid(Mess))
			{
				continue;
			}
			if (FVector::DistSquared2D(GuestLocation, Mess->GetActorLocation()) > FMath::Square(SlipRadius))
			{
				continue;
			}
			if (Rand.FRand() > SlipChancePerCheck)
			{
				continue;
			}

			Guest->ReactToSlip(SlipFallSeconds);
			PaySlipCompensation();

			if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance()))
			{
				if (GI->economySystem && SlipReputationLoss > 0)
				{
					GI->economySystem->AddReputation(-SlipReputationLoss);
				}
			}

			if (bShowDebugInfo)
			{
				ShowDebug(FString::Printf(TEXT("[就餐区卫生] 顾客踩到%s摔倒：赔偿 %d，声誉 -%d"),
					Mess->MessType == EDiningMessType::Puddle ? TEXT("积水") : TEXT("垃圾"),
					SlipCompensationMoney, SlipReputationLoss),
					FLinearColor(1.f, 0.2f, 0.2f));
			}
			return;
		}
	}
}

void ADiningHygieneManager::PaySlipCompensation()
{
	if (SlipCompensationMoney <= 0)
	{
		return;
	}

	UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance());
	UEconomySystem* Economy = GI ? GI->economySystem.Get() : nullptr;
	if (!Economy)
	{
		return;
	}

	// 赔偿是刚性支出：付不起也要赔，差额按未付账单记账（赛季据此判定入不敷出）
	if (!Economy->SpendMoney(SlipCompensationMoney, EMoneySource::Maintenance))
	{
		Economy->AddUnpaidBill(SlipCompensationMoney);
	}
}

void ADiningHygieneManager::PruneMesses()
{
	ActiveMesses.RemoveAll([](const TObjectPtr<ADiningMessActor>& Mess)
	{
		return !IsValid(Mess);
	});

	for (auto It = PenaltyAccumSeconds.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

void ADiningHygieneManager::HandleMessCleaned(ADiningMessActor* Mess)
{
	if (!Mess)
	{
		return;
	}

	ActiveMesses.Remove(Mess);
	PenaltyAccumSeconds.Remove(Mess);
	++TotalCleanedCount;

	if (bShowDebugInfo)
	{
		ShowDebug(FString::Printf(TEXT("[就餐区卫生] 已清理 1 处（剩余 %d 个）"), ActiveMesses.Num()),
			FLinearColor(0.3f, 0.9f, 0.4f));
	}
}

void ADiningHygieneManager::SetAutoSpawnEnabled(bool bEnabled)
{
	bAutoSpawn = bEnabled;
	if (UWorld* World = GetWorld())
	{
		// 重新开启时从当前时刻重新计时，避免立刻补刷一堆
		NextSpawnWorldTime = World->GetTimeSeconds() + Rand.FRandRange(SpawnIntervalMin, SpawnIntervalMax);
	}
}

void ADiningHygieneManager::ClearAllMesses()
{
	TArray<TObjectPtr<ADiningMessActor>> ToDestroy = ActiveMesses;
	ActiveMesses.Empty();
	PenaltyAccumSeconds.Empty();
	for (ADiningMessActor* Mess : ToDestroy)
	{
		if (IsValid(Mess))
		{
			Mess->Destroy();
		}
	}
}

ADiningMessActor* ADiningHygieneManager::SpawnMessInArea(EDiningMessType Type)
{
	EnsureRandomReady();
	return SpawnMess(Type, PickSpawnLocation());
}

void ADiningHygieneManager::ShowDebug(const FString& Message, const FLinearColor& Color) const
{
	if (!bShowDebugInfo)
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("%s"), *Message);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 6.f, Color.ToFColor(true), Message);
	}
}
