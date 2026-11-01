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

void APlateSystem::SetTotalCount(int32 NewTotal, bool bFillAvailable)
{
	TotalCount = FMath::Max(0, NewTotal);
	// 当前可用数不能超过新的总数
	if (CurOrderIndex > TotalCount)
	{
		CurOrderIndex = TotalCount;
	}
	// 开局初始化：所有碟子都是干净的，可用数直接补满到总数
	if (bFillAvailable)
	{
		RefillToFull();
	}
}

void APlateSystem::RefillToFull()
{
	CurOrderIndex = TotalCount;
	TimeAccumulator = 0.0f;
	UE_LOG(LogTemp, Log, TEXT("[PlateSystem] 碟子已补满：可用 %d / 总数 %d"), CurOrderIndex, TotalCount);
}

void APlateSystem::SetInfinite(bool bInfinite)
{
	bInfinitePlates = bInfinite;
	// 切回有限时恢复：可用碟子补满到总数，重新启用收碟闭环
	if (!bInfinitePlates)
	{
		RefillToFull();
	}
}

void APlateSystem::AddPlate()
{
	// 无限模式：无需回收，AddPlate 是 no-op（也不广播）
	if (bInfinitePlates)
	{
		return;
	}
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
	// 无限模式：始终“有碟子”，RemovePlate 是 no-op（烹饪机/上菜不会因没碟子卡住）
	if (bInfinitePlates)
	{
		return;
	}
	if (CurOrderIndex<=0)
	{
		CurOrderIndex = 0;
		return;
	}
	CurOrderIndex--;
}

