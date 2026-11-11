#include "Item/LaunchPad.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Shoot.h"
#include "ShootCharacter.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

ALaunchPad::ALaunchPad()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	PadMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PadMesh"));
	PadMesh->SetupAttachment(Root);
	PadMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PadMesh->SetRelativeScale3D(FVector(14.f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> MeshFinder(TEXT("/Game/Fantastic_Village_Pack/meshes/props/food/SM_PROP_food_mushroom_01.SM_PROP_food_mushroom_01"));
	if (MeshFinder.Succeeded())
	{
		PadMesh->SetStaticMesh(MeshFinder.Object);
	}

	Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("Trigger"));
	Trigger->SetupAttachment(Root);
	Trigger->SetBoxExtent(FVector(110.f, 110.f, 90.f));
	Trigger->SetRelativeLocation(FVector(0.f, 0.f, 90.f));
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Trigger->SetCollisionObjectType(ECC_WorldDynamic);
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Trigger->SetGenerateOverlapEvents(true);
	Trigger->OnComponentBeginOverlap.AddDynamic(this, &ALaunchPad::OnTriggerBeginOverlap);

	static ConstructorHelpers::FObjectFinder<USoundBase> SoundFinder(TEXT("/Game/StackOBot/Audio/SFX/Waves/SFX_Coin_Spawn.SFX_Coin_Spawn"));
	if (SoundFinder.Succeeded())
	{
		LaunchSound = SoundFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> EffectFinder(TEXT("/Game/StackOBot/FX/Coins/NS_CoinGet.NS_CoinGet"));
	if (EffectFinder.Succeeded())
	{
		LaunchEffect = EffectFinder.Object;
	}
}

void ALaunchPad::BeginPlay()
{
	Super::BeginPlay();

	BaseMeshScale = PadMesh->GetRelativeScale3D();
}

void ALaunchPad::OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (AShootCharacter* Character = Cast<AShootCharacter>(OtherActor))
	{
		Launch(Character);
	}
}

void ALaunchPad::Launch(AShootCharacter* Character)
{
	const UWorld* World = GetWorld();
	if (!World || !Character)
	{
		return;
	}

	const double Now = World->GetTimeSeconds();
	if (Now - LastLaunchTime < CooldownSeconds)
	{
		return;
	}
	// 已经在上升的（例如刚被弹起、或从上方落下前的小回弹）不重复施力
	if (Character->GetVelocity().Z > 100.f)
	{
		return;
	}

	LastLaunchTime = Now;
	Character->LaunchCharacter(FVector(0.f, 0.f, LaunchVelocityZ), false, true);

	if (LaunchSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, LaunchSound, GetActorLocation());
	}
	if (LaunchEffect)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, LaunchEffect, GetActorLocation(), FRotator::ZeroRotator);
	}

	PadMesh->SetRelativeScale3D(FVector(
		BaseMeshScale.X * 1.1f,
		BaseMeshScale.Y * 1.1f,
		BaseMeshScale.Z * SquashScaleZ));
	GetWorldTimerManager().SetTimer(SquashTimer, this, &ALaunchPad::ResetSquash, SquashDuration, false);

	UE_LOG(LogShoot, Log, TEXT("[Explore] 蘑菇弹跳 %.0f cm/s"), LaunchVelocityZ);
}

void ALaunchPad::ResetSquash()
{
	PadMesh->SetRelativeScale3D(BaseMeshScale);
}
