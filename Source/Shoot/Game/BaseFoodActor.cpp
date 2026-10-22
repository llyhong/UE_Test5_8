// Fill out your copyright notice in the Description page of Project Settings.


#include "Game/BaseFoodActor.h"

// Sets default values
ABaseFoodActor::ABaseFoodActor()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	ItemMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ItemMeshComponent"));
	ItemMesh->SetCollisionResponseToAllChannels(ECollisionResponse::ECR_Ignore);
	ItemMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RootComponent = ItemMesh;
}

// Called when the game starts or when spawned
void ABaseFoodActor::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void ABaseFoodActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

void ABaseFoodActor::AttachMeshToSocket(USceneComponent* InParent, const FName& InSocketName)
{
	FAttachmentTransformRules TransformRules(EAttachmentRule::SnapToTarget, true);
	ItemMesh->AttachToComponent(InParent, TransformRules, InSocketName);
}