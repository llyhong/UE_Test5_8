// Fill out your copyright notice in the Description page of Project Settings.


#include "Game/PlateSystem.h"

APlateSystem::APlateSystem()
{
	PrimaryActorTick.bCanEverTick = true;
	TimeAccumulator = 0.0f;
}

void APlateSystem::BeginPlay()
{
	Super::BeginPlay();
	
}

void APlateSystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (CurOrderIndex<TotalCount)
	{
		TimeAccumulator += DeltaTime;
		if (TimeAccumulator >= TotalTime)
		{
			TimeAccumulator = 0.0f; // 重置计时器
			AddPlate();
			UE_LOG(LogTemp, Warning, TEXT("CurOrderIndex incremented to: %d"), CurOrderIndex);
		}
	}
}

void APlateSystem::InitParam(int PlateCount, float SpawnPlateTime)
{
	TotalCount = PlateCount;
	CurOrderIndex = PlateCount;
	TotalTime = SpawnPlateTime;
	TimeAccumulator = 0.0f;
}

void APlateSystem::AddPlate()
{
	if (CurOrderIndex>=TotalCount)
	{
		CurOrderIndex = TotalCount;
		return;
	}
	CurOrderIndex++;
	OnAddPlate.Broadcast();
}

void APlateSystem::RemovePlate()
{
	if (CurOrderIndex<=0)
	{
		CurOrderIndex = 0;
		return;
	}
	CurOrderIndex--;
}

