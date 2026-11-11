#include "Item/CoinPickup.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Game/BaseGameInstance.h"
#include "Game/EconomySystem.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Shoot.h"
#include "ShootCharacter.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

ACoinPickup::ACoinPickup()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	CoinMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CoinMesh"));
	CoinMesh->SetupAttachment(Root);
	CoinMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CoinMesh->SetRelativeScale3D(FVector(0.55f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CoinFinder(TEXT("/Game/StackOBot/FX/Coins/SM_Coin.SM_Coin"));
	if (CoinFinder.Succeeded())
	{
		CoinMesh->SetStaticMesh(CoinFinder.Object);
	}

	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	Trigger->SetupAttachment(Root);
	Trigger->SetSphereRadius(70.f);
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Trigger->SetCollisionObjectType(ECC_WorldDynamic);
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Trigger->SetGenerateOverlapEvents(true);
	Trigger->OnComponentBeginOverlap.AddDynamic(this, &ACoinPickup::OnTriggerBeginOverlap);

	static ConstructorHelpers::FObjectFinder<USoundBase> SoundFinder(TEXT("/Game/StackOBot/Audio/SFX/Waves/SFX_Coin_Collect.SFX_Coin_Collect"));
	if (SoundFinder.Succeeded())
	{
		CollectSound = SoundFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> EffectFinder(TEXT("/Game/StackOBot/FX/Coins/NS_CoinGet.NS_CoinGet"));
	if (EffectFinder.Succeeded())
	{
		CollectEffect = EffectFinder.Object;
	}
}

void ACoinPickup::BeginPlay()
{
	Super::BeginPlay();

	BaseLocation = GetActorLocation();
	BobPhase = FMath::FRandRange(0.f, 2.f * PI);
}

void ACoinPickup::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bCollected)
	{
		return;
	}

	BobPhase += DeltaSeconds * BobSpeed;
	FVector Location = BaseLocation;
	Location.Z += FMath::Sin(BobPhase) * BobAmplitude;
	SetActorLocation(Location);

	if (!FMath::IsNearlyZero(SpinDegreesPerSecond))
	{
		AddActorLocalRotation(FRotator(0.f, SpinDegreesPerSecond * DeltaSeconds, 0.f));
	}
}

void ACoinPickup::OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (bCollected)
	{
		return;
	}

	if (AShootCharacter* Character = Cast<AShootCharacter>(OtherActor))
	{
		Collect(Character);
	}
}

void ACoinPickup::Collect(AShootCharacter* Character)
{
	if (bCollected || !Character)
	{
		return;
	}

	bCollected = true;
	Trigger->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CoinMesh->SetVisibility(false, true);
	SetActorTickEnabled(false);

	if (CollectSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, CollectSound, GetActorLocation());
	}
	if (CollectEffect)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, CollectEffect, GetActorLocation(), FRotator::ZeroRotator);
	}

	if (UBaseGameInstance* GameInstance = Cast<UBaseGameInstance>(UGameplayStatics::GetGameInstance(this)))
	{
		if (UEconomySystem* Economy = GameInstance->GetEconomySystem())
		{
			Economy->AddMoney(CoinValue, EMoneySource::None);
		}
	}

	UE_LOG(LogShoot, Log, TEXT("[Explore] 拾取金币 +%d 金钱"), CoinValue);

	if (RespawnSeconds > 0.f)
	{
		GetWorldTimerManager().SetTimer(RespawnHandle, this, &ACoinPickup::RespawnCoin, RespawnSeconds, false);
	}
}

void ACoinPickup::RespawnCoin()
{
	bCollected = false;
	CoinMesh->SetVisibility(true, true);
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SetActorTickEnabled(true);
}
