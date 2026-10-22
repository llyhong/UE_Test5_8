// Fill out your copyright notice in the Description page of Project Settings.


#include "Level/LevelActor.h"
#include "Components/WidgetComponent.h"

// Sets default values
ALevelActor::ALevelActor()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	// —— Widget 组件 ——
	WidgetComp = CreateDefaultSubobject<UWidgetComponent>(TEXT("WidgetComp"));
	WidgetComp->SetupAttachment(RootComponent);
	WidgetComp->SetVisibility(false);
}

// Called when the game starts or when spawned
void ALevelActor::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void ALevelActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

void ALevelActor::ShowWidget()
{
	if (WidgetComp)
	{
		WidgetComp->SetVisibility(true);
	}
}

void ALevelActor::HideWidget()
{
	if (WidgetComp)
	{
		WidgetComp->SetVisibility(false);
	}
}

