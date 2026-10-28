// Fill out your copyright notice in the Description page of Project Settings.

#include "Item/FoodSpawner.h"
#include "Game/BaseGameInstance.h"

void AFoodSpawner::BeginPlay()
{
	Super::BeginPlay();

	// 仅有限模式参与存档；无限(-1)不需要持久化
	if (RemainingCount < 0)
	{
		return;
	}

	if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetWorld()->GetGameInstance()))
	{
		const int32 Saved = GI->GetSpawnerRemaining(GetActorGuid());
		if (Saved >= 0)
		{
			// 有存档记录：用存档值覆盖（玩家之前摘取过）
			RemainingCount = Saved;
		}
		else
		{
			// 首次出现：把编辑器里设置的初始值写入存档内存（落盘统一在关卡退出时由 BaseLevelBP::EndPlay 完成）
			GI->SetSpawnerRemaining(GetActorGuid(), RemainingCount);
		}
	}
}

void AFoodSpawner::ConsumeOne()
{
	// 仅在有限模式下递减；无限(-1)保持不动
	if (RemainingCount > 0)
	{
		--RemainingCount;
		// 写回存档内存（落盘统一在关卡退出时由 BaseLevelBP::EndPlay 完成）
		if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetWorld()->GetGameInstance()))
		{
			GI->SetSpawnerRemaining(GetActorGuid(), RemainingCount);
		}
	}
}

void AFoodSpawner::OnSpawnActorEvent()
{
	OnSpawnActor.Broadcast();
}
