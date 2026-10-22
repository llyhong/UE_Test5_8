// Fill out your copyright notice in the Description page of Project Settings.


#include "Item/Weapon.h"

#include "Components/BoxComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Animal/AnimalCharacter.h"
#include "ShootCharacter.h"

AWeapon::AWeapon()
{
	WeaponBox = CreateDefaultSubobject<UBoxComponent>(TEXT("Weapon Box"));
	WeaponBox->SetupAttachment(GetRootComponent());
	WeaponBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WeaponBox->SetCollisionResponseToAllChannels(ECollisionResponse::ECR_Overlap);
	WeaponBox->SetCollisionResponseToChannel(ECollisionChannel::ECC_Pawn, ECollisionResponse::ECR_Ignore);

	BoxTraceStart = CreateDefaultSubobject<USceneComponent>(TEXT("Box Trace Start"));
	BoxTraceStart->SetupAttachment(GetRootComponent());

	BoxTraceEnd = CreateDefaultSubobject<USceneComponent>(TEXT("Box Trace End"));
	BoxTraceEnd->SetupAttachment(GetRootComponent());
}

void AWeapon::BeginPlay()
{
	Super::BeginPlay();
	WeaponBox->OnComponentBeginOverlap.AddDynamic(this, &AWeapon::OnBoxOverlap);
}

void AWeapon::OnBoxOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	
	// 仅在持有者(ShootCharacter)处于攻击状态时才进行碰撞检测
	const AShootCharacter* OwnerChar = Cast<AShootCharacter>(GetOwner());
	if (!OwnerChar || OwnerChar->GetActionState() != ECharacterActionState::EAS_Attacking)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Yellow, TEXT("Weapon: 持有者非攻击状态，跳过碰撞检测"));
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("Doweapon1111"));
		return;
	}

	//if (ActorIsSameType(OtherActor)) return;
	
	FHitResult BoxHit;
	BoxTrace(BoxHit);
	FString ActorName = OtherActor->GetName();
	if (BoxHit.GetActor())
	{
		//if (ActorIsSameType(BoxHit.GetActor())) return;
		
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("Doweapon2222"));
		//UGameplayStatics::ApplyDamage(BoxHit.GetActor(), Damage, GetInstigator()->GetController(), this, UDamageType::StaticClass());
		ExecuteGetHit(BoxHit);
		//CreateFields(BoxHit.ImpactPoint);
	}
}

bool AWeapon::ActorIsSameType(AActor* OtherActor)
{
	return GetOwner()->ActorHasTag(TEXT("Enemy")) && OtherActor->ActorHasTag(TEXT("Enemy"));
}

void AWeapon::ExecuteGetHit(FHitResult& BoxHit)
{
	AActor* HitActor = BoxHit.GetActor();
	if (!HitActor)
	{
		return;
	}
	UE_LOG(LogTemp, Warning, TEXT("ExecuteGetHit: %s"), *HitActor->GetName());

	// 命中动物：由武器自身结算血量(读取血量 / 触发死亡或扣血)
	if (AAnimalCharacter* Animal = Cast<AAnimalCharacter>(HitActor))
	{
		DealDamageToAnimal(Animal, BoxHit.ImpactPoint);
		return;
	}

	IHitInterface* HitInterface = Cast<IHitInterface>(HitActor);
	if (HitInterface)
	{
		HitInterface->Execute_GetHit(HitActor, BoxHit.ImpactPoint, GetOwner());
	}
}

void AWeapon::DealDamageToAnimal(AAnimalCharacter* Animal, const FVector& ImpactPoint)
{
	if (!Animal || Animal->IsDead())
	{
		return;
	}
	// 攻击状态已在 OnBoxOverlap 的碰撞检测入口校验过；
	// 这里武器只传入一刀的伤害量，读血 / 判死 / 扣血的命中处理交给动物自身
	Animal->TakeWeaponDamage(Damage);
}

void AWeapon::BoxTrace(FHitResult& BoxHit)
{
	const FVector Start = BoxTraceStart->GetComponentLocation();
	const FVector End = BoxTraceEnd->GetComponentLocation();

	TArray<AActor*> ActorsToIgnore;
	ActorsToIgnore.Add(this);
	ActorsToIgnore.Add(GetOwner());

	for (AActor* Actor : IgnoreActors)
	{
		ActorsToIgnore.AddUnique(Actor);
	}

	UKismetSystemLibrary::BoxTraceSingle(
		this,
		Start,
		End,
		BoxTraceExtent,
		BoxTraceStart->GetComponentRotation(),
		ETraceTypeQuery::TraceTypeQuery1,
		false,
		ActorsToIgnore,
		bShowBoxDebug ? EDrawDebugTrace::ForDuration : EDrawDebugTrace::None,
		BoxHit,
		true
	);
	IgnoreActors.AddUnique(BoxHit.GetActor());
}

