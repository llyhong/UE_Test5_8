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

	// 自动补碟仅在开关开启时生效：洗碗机模式下碟子只能靠玩家交还脏碟回收
	if (!bAutoRegenPlates)
	{
		return;
	}

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

void APlateSystem::SetTotalCount(int32 NewTotal)
{
	TotalCount = FMath::Max(0, NewTotal);
	// 当前可用数不能超过新的总数
	if (CurOrderIndex > TotalCount)
	{
		CurOrderIndex = TotalCount;
	}
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

