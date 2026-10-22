// Fill out your copyright notice in the Description page of Project Settings.

#include "Machine/AttractCounter.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "ShootCharacter.h"
#include "NPC/GuestManager.h"
#include "Game/BaseGameInstance.h"
#include "Game/EconomySystem.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"

AAttractCounter::AAttractCounter()
{
	PrimaryActorTick.bCanEverTick = false;

	CounterMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CounterMesh"));
	RootComponent = CounterMesh;

	InteractionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionSphere"));
	InteractionSphere->SetupAttachment(RootComponent);
	InteractionSphere->SetSphereRadius(160.f);
	InteractionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionSphere->SetCollisionResponseToAllChannels(ECR_Overlap);
	InteractionSphere->SetGenerateOverlapEvents(true); // 必须显式开启，否则不会触发 OnComponentBeginOverlap
}

void AAttractCounter::BeginPlay()
{
	Super::BeginPlay();

	InteractionSphere->OnComponentBeginOverlap.AddDynamic(this, &AAttractCounter::OnInteractionOverlap);
	InteractionSphere->OnComponentEndOverlap.AddDynamic(this, &AAttractCounter::OnInteractionEndOverlap);
}

AGuestManager* AAttractCounter::FindGuestManager() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	TArray<AActor*> Managers;
	UGameplayStatics::GetAllActorsOfClass(World, AGuestManager::StaticClass(), Managers);
	return Managers.Num() > 0 ? Cast<AGuestManager>(Managers[0]) : nullptr;
}

void AAttractCounter::AttractCustomers()
{
	if (GetWorld() && (GetWorld()->GetTimeSeconds() - LastAttractTime < AttractCooldown))
	{
		return; // 冷却中
	}
	LastAttractTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;

	UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance());
	if (!GI || !GI->economySystem)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Red, TEXT("[招揽台] 未找到经济系统，无法提升声誉"));
		return;
	}

	// 读取霓虹招牌道具加成
	int32 NeonSigns = GI->economySystem->GetOwnedItemCount(EconomyShopItemIDs::NeonSign);

	// 招揽不再直接生成客人，而是提升声誉；声誉越高，GuestManager 的容量上限越高，
	// 自动生成(SpawnInterval)会把在场客人补满到新上限，实现“声誉高则客人多”。
	int32 RepGain = BaseReputationGain + NeonSigns * ExtraReputationPerSign;
	RepGain = FMath::Clamp(RepGain, 0, MaxReputationGainPerPress);
	GI->economySystem->AddReputation(RepGain);

	// 反馈：当前声誉 与 提升后的客人容量上限
	int32 NewReputation = GI->economySystem->GetReputation();
	int32 Capacity = 0;
	if (AGuestManager* Mgr = FindGuestManager())
	{
		Capacity = Mgr->GetEffectiveMaxGuests();
	}

	OnAttract(RepGain);
	GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Green,
		FString::Printf(TEXT("[招揽台] 提升声誉 +%d（当前声誉=%d，客人容量上限=%d）"),
			RepGain, NewReputation, Capacity));
}

void AAttractCounter::OnInteractionOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (AShootCharacter* Character = Cast<AShootCharacter>(OtherActor))
	{
		Character->SetOverlappingAttractCounter(this);
	}
}

void AAttractCounter::OnInteractionEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (AShootCharacter* Character = Cast<AShootCharacter>(OtherActor))
	{
		if (Character->GetOverlappingAttractCounter() == this)
		{
			Character->SetOverlappingAttractCounter(nullptr);
		}
	}
}
